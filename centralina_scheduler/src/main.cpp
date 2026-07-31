#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <RTClib.h>

static const uint16_t HTTP_SERVER_PORT = 80;
static const uint16_t UDP_WORK_PORT = 54324;
static const char *SCHEDULER_FILE = "/scheduler.json";
static const uint16_t OUTPUT_NAME_MAX = 20;

// Configura qui le credenziali STA della rete domotica.
static const char *WIFI_STA_SSID = "";
static const char *WIFI_STA_PWD = "";
static const char *WIFI_AP_PASSWORD = "87654321";

struct str_device {
  bool Enabled = false;
  uint8_t Validita_Start = 1;   // 1=gen ... 12=dic
  uint8_t Validita_End = 12;    // 1=gen ... 12=dic
  uint8_t WeekdayMask = 0x7F;  // bit0=lun ... bit6=dom, 0x7F = tutti i giorni
  char Ora_Attivazione[6] = {0}; // HH:MM
  bool ON_or_OFF = false;       // true=ON, false=OFF

  int16_t Ora_Minutes = -1;     // cache runtime 0..1439
  int32_t LastExecutedWeekMinute = -1;
};

struct str_group {
  char Name[OUTPUT_NAME_MAX] = {0};
  bool Enabled = false;
  uint16_t RuleCount = 0;
  str_device *Rules = nullptr;
};

struct scheduler_config_t {
  uint16_t OutputCount = 0;
  str_group *Outputs = nullptr;
} Scheduler;

WiFiUDP Udp;
WebServer Server(HTTP_SERVER_PORT);
RTC_DS3231 Rtc;

unsigned long LastWiFiRetryMs = 0;
unsigned long LastSchedulerTickMs = 0;
int32_t LastEvaluatedWeekMinute = -1;

// Libera la memoria allocata per le regole e i gruppi dello scheduler.
static void freeSchedulerConfig() {
  if (Scheduler.Outputs != nullptr) {
    for (uint16_t i = 0; i < Scheduler.OutputCount; i++) {
      delete[] Scheduler.Outputs[i].Rules;
      Scheduler.Outputs[i].Rules = nullptr;
      Scheduler.Outputs[i].RuleCount = 0;
    }
    delete[] Scheduler.Outputs;
    Scheduler.Outputs = nullptr;
  }
  Scheduler.OutputCount = 0;
}

// Converte una stringa HH:MM in minuti trascorsi dalla mezzanotte.
static bool parseTimeHHMM(const char *hhmm, int16_t &minutesOut) {
  if (hhmm == nullptr || strlen(hhmm) != 5 || hhmm[2] != ':') {
    return false;
  }

  if (!isdigit(hhmm[0]) || !isdigit(hhmm[1]) || !isdigit(hhmm[3]) || !isdigit(hhmm[4])) {
    return false;
  }

  int h = (hhmm[0] - '0') * 10 + (hhmm[1] - '0');
  int m = (hhmm[3] - '0') * 10 + (hhmm[4] - '0');

  if (h < 0 || h > 23 || m < 0 || m > 59) {
    return false;
  }

  minutesOut = (int16_t)(h * 60 + m);
  return true;
}

// Converte il giorno della settimana del RTC nel formato 1=lun ... 7=dom.
static uint8_t dayOfWeekMonToSun(const DateTime &now) {
  // RTClib: 0=dom ... 6=sab -> 1=lun ... 7=dom
  uint8_t dow = now.dayOfTheWeek();
  if (dow == 0) {
    return 7;
  }
  return dow;
}

// Verifica se un giorno ricade nell'intervallo di validità della regola.
// Verifica se il mese corrente rientra nell'intervallo di validità della regola.
static bool monthInRange(uint8_t month, uint8_t start, uint8_t end) {
  if (month < 1 || month > 12 || start < 1 || start > 12 || end < 1 || end > 12) {
    return false;
  }

  if (start <= end) {
    return month >= start && month <= end;
  }

  // Intervallo ciclico, esempio: ott(10) -> mar(3)
  return month >= start || month <= end;
}

// Verifica se il giorno della settimana è abilitato tramite bitmask.
static bool weekdayEnabled(uint8_t day, uint8_t mask) {
  if (day < 1 || day > 7) {
    return false;
  }

  return (mask & (1u << (day - 1))) != 0;
}

// Calcola l'indirizzo broadcast della rete Wi-Fi attiva.
static IPAddress getBroadcastIp() {
  IPAddress ip = WiFi.localIP();
  IPAddress mask = WiFi.subnetMask();
  IPAddress out;

  for (int i = 0; i < 4; i++) {
    out[i] = (uint8_t)((ip[i] & mask[i]) | (~mask[i]));
  }

  return out;
}

// Invia il comando ON/OFF di un output tramite UDP al dispositivo destinatario.
static void sendOutputCommand(const char *outputName, bool turnOn) {
  if (outputName == nullptr || outputName[0] == '\0') {
    return;
  }

  String tx = String(outputName);
  tx.concat("/");
  tx.concat(turnOn ? "ON" : "OFF");

  size_t len = tx.length() + 1; // Sempre +1 per includere '\0'
  IPAddress dst = (WiFi.isConnected() ? getBroadcastIp() : WiFi.softAPBroadcastIP());

  Udp.beginPacket(dst, UDP_WORK_PORT);
  Udp.write((const uint8_t *)tx.c_str(), len);
  Udp.endPacket();

  Serial.print("TX: ");
  Serial.println(tx);
}

// Legge il contenuto raw del file delle regole dalla LittleFS.
static String readRulesRaw() {
  if (!LittleFS.exists(SCHEDULER_FILE)) {
    return String();
  }

  File file = LittleFS.open(SCHEDULER_FILE, "r");
  if (!file) {
    return String();
  }

  String content = file.readString();
  file.close();
  return content;
}

// Salva il JSON delle regole sul file di configurazione della LittleFS.
static bool saveRulesRaw(const String &rawJson) {
  File file = LittleFS.open(SCHEDULER_FILE, "w");
  if (!file) {
    return false;
  }

  size_t written = file.print(rawJson);
  file.close();
  return written == rawJson.length();
}

// Carica e interpreta le regole dallo storage flash nella struttura in memoria.
static bool loadSchedulerFromFs(String &errorMessage) {
  String raw = readRulesRaw();
  if (raw.length() == 0) {
    errorMessage = "scheduler.json mancante o vuoto";
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, raw);
  if (err) {
    errorMessage = String("JSON invalido: ") + err.c_str();
    return false;
  }

  JsonArray outputs = doc["outputs"].as<JsonArray>();
  if (outputs.isNull()) {
    errorMessage = "Campo outputs mancante";
    return false;
  }

  freeSchedulerConfig();

  Scheduler.OutputCount = (uint16_t)outputs.size();
  Scheduler.Outputs = new str_group[Scheduler.OutputCount];

  for (uint16_t i = 0; i < Scheduler.OutputCount; i++) {
    JsonObject jOut = outputs[i].as<JsonObject>();
    const char *name = jOut["Name"] | "";
    bool enabled = jOut["Enabled"] | false;

    strncpy(Scheduler.Outputs[i].Name, name, OUTPUT_NAME_MAX - 1);
    Scheduler.Outputs[i].Name[OUTPUT_NAME_MAX - 1] = '\0';
    Scheduler.Outputs[i].Enabled = enabled;

    JsonArray rules = jOut["rules"].as<JsonArray>();
    Scheduler.Outputs[i].RuleCount = rules.isNull() ? 0 : (uint16_t)rules.size();

    if (Scheduler.Outputs[i].RuleCount > 0) {
      Scheduler.Outputs[i].Rules = new str_device[Scheduler.Outputs[i].RuleCount];
      for (uint16_t r = 0; r < Scheduler.Outputs[i].RuleCount; r++) {
        JsonObject jRule = rules[r].as<JsonObject>();

        Scheduler.Outputs[i].Rules[r].Enabled = jRule["Enabled"] | false;
        Scheduler.Outputs[i].Rules[r].Validita_Start = (uint8_t)(jRule["Validita_Start"] | 1);
        Scheduler.Outputs[i].Rules[r].Validita_End = (uint8_t)(jRule["Validita_End"] | 12);
        Scheduler.Outputs[i].Rules[r].WeekdayMask = (uint8_t)(jRule["WeekdayMask"] | 0x7F);

        const char *ora = jRule["Ora_Attivazione"] | "00:00";
        strncpy(Scheduler.Outputs[i].Rules[r].Ora_Attivazione, ora, 5);
        Scheduler.Outputs[i].Rules[r].Ora_Attivazione[5] = '\0';

        const char *onOff = jRule["ON_or_OFF"] | "OFF";
        Scheduler.Outputs[i].Rules[r].ON_or_OFF = (strcmp(onOff, "ON") == 0);

        int16_t minuteCache = -1;
        if (!parseTimeHHMM(Scheduler.Outputs[i].Rules[r].Ora_Attivazione, minuteCache)) {
          minuteCache = -1;
        }
        Scheduler.Outputs[i].Rules[r].Ora_Minutes = minuteCache;
        Scheduler.Outputs[i].Rules[r].LastExecutedWeekMinute = -1;
      }
    }
  }

  return true;
}

// Valuta le regole correnti e invia i comandi quando è il momento giusto.
static void evaluateScheduler() {
  DateTime now = Rtc.now();
  uint8_t day = dayOfWeekMonToSun(now);
  uint8_t month = (uint8_t)now.month();
  int16_t minuteOfDay = (int16_t)(now.hour() * 60 + now.minute());
  int32_t weekMinute = (int32_t)((day - 1) * 1440 + minuteOfDay);

  if (weekMinute == LastEvaluatedWeekMinute) {
    return;
  }
  LastEvaluatedWeekMinute = weekMinute;

  for (uint16_t i = 0; i < Scheduler.OutputCount; i++) {
    str_group &out = Scheduler.Outputs[i];

    if (!out.Enabled || out.Name[0] == '\0') {
      continue;
    }

    int16_t winner = -1;
    for (uint16_t r = 0; r < out.RuleCount; r++) {
      str_device &rule = out.Rules[r];
      if (!rule.Enabled) {
        continue;
      }

      if (rule.Ora_Minutes < 0 || rule.Ora_Minutes != minuteOfDay) {
        continue;
      }

      if (!monthInRange(month, rule.Validita_Start, rule.Validita_End)) {
        continue;
      }

      if (!weekdayEnabled(day, rule.WeekdayMask)) {
        continue;
      }

      // Ultima regola valida nell'array vince.
      winner = (int16_t)r;
    }

    if (winner >= 0) {
      str_device &rule = out.Rules[winner];
      if (rule.LastExecutedWeekMinute != weekMinute) {
        sendOutputCommand(out.Name, rule.ON_or_OFF);
        rule.LastExecutedWeekMinute = weekMinute;
      }
    }
  }
}

// Mantiene attiva la connessione Wi-Fi STA e riavvia il tentativo se serve.
static void ensureWifiConnected() {
  if (WiFi.isConnected()) {
    return;
  }

  if (millis() - LastWiFiRetryMs < 10000UL) {
    return;
  }

  LastWiFiRetryMs = millis();

  if (strlen(WIFI_STA_SSID) > 0) {
    Serial.print("WiFi reconnect to: ");
    Serial.println(WIFI_STA_SSID);
    WiFi.begin(WIFI_STA_SSID, WIFI_STA_PWD);
  }
}

// Registra gli endpoint HTTP per stato, regole e ricarica del scheduler.
static void setupHttp() {
  Server.on("/", HTTP_GET, []() {
    String html;
    html.reserve(512);
    html += "<html><body><h1>ESP32 Centralina Scheduler</h1>";
    html += "<p>IP: "; html += WiFi.localIP().toString(); html += "</p>";
    html += "<p>RTC: "; html += Rtc.now().timestamp(DateTime::TIMESTAMP_TIME); html += "</p>";
    html += "<p>Endpoint: GET /status, GET /rules, POST /rules, POST /reload</p>";
    html += "</body></html>";
    Server.send(200, "text/html", html);
  });

  Server.on("/status", HTTP_GET, []() {
    JsonDocument doc;
    doc["wifi_connected"] = WiFi.isConnected();
    doc["ip"] = WiFi.localIP().toString();
    doc["rtc_running"] = Rtc.isrunning();
    doc["outputs"] = Scheduler.OutputCount;

    String out;
    serializeJsonPretty(doc, out);
    Server.send(200, "application/json", out);
  });

  Server.on("/rules", HTTP_GET, []() {
    String raw = readRulesRaw();
    if (raw.length() == 0) {
      Server.send(404, "text/plain", "scheduler.json non trovato");
      return;
    }
    Server.send(200, "application/json", raw);
  });

  Server.on("/rules", HTTP_POST, []() {
    if (!Server.hasArg("plain")) {
      Server.send(400, "text/plain", "Body JSON mancante");
      return;
    }

    String payload = Server.arg("plain");
    if (!saveRulesRaw(payload)) {
      Server.send(500, "text/plain", "Errore salvataggio file");
      return;
    }

    String err;
    if (!loadSchedulerFromFs(err)) {
      Server.send(400, "text/plain", String("File salvato ma parse fallito: ") + err);
      return;
    }

    Server.send(200, "text/plain", "Regole aggiornate");
  });

  Server.on("/reload", HTTP_POST, []() {
    String err;
    if (!loadSchedulerFromFs(err)) {
      Server.send(400, "text/plain", err);
      return;
    }
    Server.send(200, "text/plain", "Reload OK");
  });

  Server.begin();
}

// Inizializza hardware, Wi-Fi, RTC, filesystem e scheduler all'avvio.
void setup() {
  Serial.begin(115200);
  delay(200);

  WiFi.mode(WIFI_MODE_APSTA);
  String apName = String("CENTRALINA_") + String((uint32_t)ESP.getEfuseMac(), HEX);
  WiFi.softAP(apName.c_str(), WIFI_AP_PASSWORD);

  if (strlen(WIFI_STA_SSID) > 0) {
    WiFi.begin(WIFI_STA_SSID, WIFI_STA_PWD);
  }

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS init fallita");
  }

  if (!Rtc.begin()) {
    Serial.println("RTC DS3231 non trovato");
  } else if (Rtc.lostPower()) {
    // Imposta data/ora di compilazione alla prima accensione.
    Rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  Udp.begin(UDP_WORK_PORT);

  String err;
  if (!loadSchedulerFromFs(err)) {
    Serial.print("Errore load scheduler: ");
    Serial.println(err);
  }

  setupHttp();

  Serial.println("Centralina scheduler pronta");
}

// Esegue il ciclo principale di servizio Wi-Fi e valutazione delle regole.
void loop() {
  ensureWifiConnected();
  Server.handleClient();

  if (millis() - LastSchedulerTickMs >= 1000UL) {
    LastSchedulerTickMs = millis();
    evaluateScheduler();
  }
}
