/*
  Documentazione in drive
  D:\Lavori\Arduino\ESP32_Network\pio
  Derivata da Easy_Bridge
  La configurazione della WiFi SSID è gestita via HTTP
  la configurazione degli ingressi e uscite è gestita da app fatta con mit app inventor
*/

#include <Arduino.h>
#include <MyWiFi.hpp>
#include <WiFiUdp.h>
#include <EEPROM.h>
#include <BluetoothSerial.h>
#include <Bit_Utils.h>
#include <WebServer.h>

static const uint16_t HTTP_SERVER_PORT = 80;
static const size_t MAX_LOG_BUFFER_SIZE = 2048;
static const size_t UDP_WORK_RX_SIZE = 128;
static const size_t UDP_SETUP_RX_SIZE = 640;
static const size_t UDP_TX_BUFFER_SIZE = 640;

WebServer server(HTTP_SERVER_PORT);

//---------- Log (Buffer Circolare per evitare la frammentazione dell'Heap)
char log_buffer[MAX_LOG_BUFFER_SIZE] = {0};
size_t log_head = 0;

void log_add(const String &NewLog) {
  Serial.print(NewLog);
  for (size_t i = 0; i < NewLog.length(); i++) {
    log_buffer[log_head] = NewLog[i];
    log_head = (log_head + 1) % MAX_LOG_BUFFER_SIZE;
  }
}

String get_logs_as_string() {
  String out = "";
  out.reserve(MAX_LOG_BUFFER_SIZE);
  for (size_t i = 0; i < MAX_LOG_BUFFER_SIZE; i++) {
    size_t idx = (log_head + i) % MAX_LOG_BUFFER_SIZE;
    if (log_buffer[idx] != '\0') {
      out += log_buffer[idx];
    }
  }
  return out;
}

#pragma region Variable Declaration
//---------- [COSTANTI]
const int16_t IX_COUNT = 10;
const int16_t QX_COUNT = 4;

const uint16_t QX_TYPE_TOGGLE = 1;
const uint16_t QX_TYPE_REPLICATE = 2;
const uint16_t QX_TYPE_REPLICATE_NEGATE = 3;

struct struct_in_config {
  bool disable_time_analisis = false; 
  bool internal_pullup = true;
  int16_t noise = 0;
  char qx_short[20] = {0};
  char qx_long[20] = {0};
};

struct struct_out_config {
  bool All_ON_OFF_Member = true;
  int16_t type = 1;
  unsigned long timeout = 0;
  char qx_name[20] = {0};
};

struct struct_Board_config {
  char myname[20] = {0};
  char location[30] = {0};
  struct_in_config ix[IX_COUNT];
  struct_out_config qx[QX_COUNT];
  int16_t boardVersion = 4;
  int DHCP = 0;
  char IP[16] = {0};
} Board_Config;

//---------- LOOP
ulong time_wifi = 0;
ulong time_udp_work = 0;
ulong time_udp_setup = 0;

//---------- WiFi
bool wifi_started = false;
ulong time_wifi_millis_last_try = 0;
MyWiFi _WiFi;

char _soft_ap_password[31] = "87654321";

struct struct_WiFi_Parameters {
  char SSID[31] = ""; 
  char PWD[31] = "";  
} WiFi_Parameters;

stBool wifi_is_conneted;

//---------- UDP
WiFiUDP udp_setup;
WiFiUDP udp_work;
const uint16_t udp_work_port = 54324;
const uint16_t udp_setup_port = 54323;

uint8_t udp_work_data_tx[UDP_TX_BUFFER_SIZE];
char udp_work_data_rx[UDP_WORK_RX_SIZE];

uint8_t udp_setup_data_tx[UDP_TX_BUFFER_SIZE];
char udp_setup_data_rx[UDP_SETUP_RX_SIZE];

ulong time_last_QState_Sended = 0;
ulong restart_start_time = 0;
bool restart_requested = false;

//---------- EEPROM
int16_t eeprom_initialized = 0;

//---------- RELAY
struct struct_IO_Q {
  int16_t q;
  unsigned long TimeSwichedON = 0;
};
struct_IO_Q io_q[4];
int16_t io_i[10] = {GPIO_NUM_13, GPIO_NUM_14, GPIO_NUM_15, GPIO_NUM_16, GPIO_NUM_17, GPIO_NUM_18, GPIO_NUM_19, GPIO_NUM_21, GPIO_NUM_22, GPIO_NUM_23};
stBool ix[10];
#pragma endregion Variable Declaration

void EEPROM_Read();

static bool validStaticIP(const char *ip_str, IPAddress &out) {
  if (ip_str == nullptr || ip_str[0] == '\0') {
    return false;
  }
  return out.fromString(ip_str);
}

void gpioConfig() {
  io_q[1].q = GPIO_NUM_33;
  io_q[2].q = GPIO_NUM_25;
  io_q[3].q = GPIO_NUM_26;

  switch (Board_Config.boardVersion) {
  case 1:
    io_q[0].q = GPIO_NUM_16;
    io_i[3] = GPIO_NUM_27;
    break;
  case 2:
  case 3:
    break;
  case 4:
    io_q[0].q = GPIO_NUM_32;
    io_i[3] = GPIO_NUM_16;
    break;
  default:
    break;
  }

  for (size_t i = 0; i < QX_COUNT; i++) {
    pinMode(io_q[i].q, OUTPUT);
  }
  for (size_t i = 0; i < IX_COUNT; i++) {
    log_add("Input: ");
    if (Board_Config.ix[i].internal_pullup == 1) {
      log_add("PullUP\n");
      pinMode(io_i[i], INPUT_PULLUP);
    } else {
      log_add("\n");
      pinMode(io_i[i], INPUT);
    }
  }
}

#pragma region EEPROM
void EEPROM_Write() {
  log_add("EEPROM_Write\n");
  int16_t p = 0;
  eeprom_initialized = 852;
  EEPROM.put(p, eeprom_initialized);
  p += sizeof(eeprom_initialized);
  EEPROM.put(p, WiFi_Parameters);
  p += sizeof(WiFi_Parameters);
  EEPROM.put(p, Board_Config);

  EEPROM.commit();
}

void EEPROM_Read() {
  eeprom_initialized = EEPROM.readInt(0);

  if (eeprom_initialized == 852) {
    log_add("EEPROM_Read\n");
    int16_t p = 0;
    p += sizeof(eeprom_initialized);
    EEPROM.get(p, WiFi_Parameters);
    p += sizeof(WiFi_Parameters);
    EEPROM.get(p, Board_Config);
    
    if (Board_Config.boardVersion < 1) {
      Board_Config.boardVersion = 4;
    }

    int nsize = sizeof(Board_Config.ix[0].qx_short);
    int nlen = strnlen(Board_Config.ix[0].qx_short, nsize);
    if (nlen >= nsize) {
      Serial.println("EEProm_Error");
      for (int16_t i = 0; i < IX_COUNT; i++) {
        Board_Config.ix[i].qx_long[0] = '\0';
        Board_Config.ix[i].qx_short[0] = '\0';
      }
      for (size_t i = 0; i < QX_COUNT; i++) {
        Board_Config.qx[i].qx_name[0] = '\0';
      }
      EEPROM_Write();
    }
  } else {
    WiFi_Parameters.SSID[0] = 0;
    WiFi_Parameters.PWD[0] = 0;
    EEPROM_Write();
  }
}
#pragma endregion EEPROM

#pragma region HTTP

    static bool HTTP_Request_UsesAP() {
      return server.client().localIP() == _WiFi.ap_LocalIP();
    }

    static bool HTTP_WiFi_Scan_WouldDropSession() {
      return _WiFi.isConnected() && _WiFi.hasIP() && !HTTP_Request_UsesAP();
    }

    void HTTP_Event_WiFi_Setup_Manual() {
      String ToSend =
          "<!DOCTYPE html>\n"
          "<html>\n"
          "<head>\n"
          "<style>\n"
          ".style_1 {\n"
          "  background-color: #4CAF50;\n"
          "  color: yellow;\n"
          "  padding: 20px 32px;\n"
          "  text-decoration: none;\n"
          "  font-size: 40px;\n"
          "  margin: 4px 30px;\n"
          "  border-radius: 8px;\n"
          "}\n"
          "input {\n"
          "  font-size: 40px;\n"
          "  width: 90%;\n"
          "  max-width: 900px;\n"
          "}\n"
          "</style>\n"
          "</head>\n"
          "<body style=\"font-size:20px;\">\n"
          "<h1 style=\"font-size:60px;\">ESP2_Network</h1>\n"
          "<p style=\"color:red; font-size:34px;\">La scansione WiFi scollega temporaneamente la STA.</p>\n"
          "<p style=\"font-size:30px;\">Se stai usando l'IP della rete di casa perderesti questa pagina a meta'.</p>\n"
          "<p style=\"font-size:30px;\">Per fare la scansione collega il telefono/PC all'AP dell'ESP <b>";
      ToSend += _WiFi.AP_SSID();
      ToSend += "</b> e apri <b>http://";
      ToSend += _WiFi.ap_LocalIP().toString();
      ToSend += "</b>.</p>\n";
      ToSend += "<p style=\"font-size:30px;\">Da qui puoi comunque inserire SSID e password manualmente.</p>\n";
      ToSend += "<p style=\"font-size:30px;\">WiFi corrente: <b>";
      ToSend += (strlen(WiFi_Parameters.SSID) ? WiFi_Parameters.SSID : "Non configurata");
      ToSend += "</b></p>\n";
      ToSend +=
          "<form action=\"/WiFi_Selected\" method=\"get\">\n"
          "<p>SSID:</p>\n"
          "<input type=\"text\" id=\"wifi\" name=\"wifi\" maxlength=\"30\" />\n"
          "<br><br>\n"
          "<p>Password:</p>\n"
          "<input type=\"password\" id=\"password\" name=\"pwd\" maxlength=\"30\" />\n"
          "<br><br>\n"
          "<a class=\"style_1\" href=\"/\">Cancel</a>\n"
          "<button class=\"style_1\" type=\"submit\">Ok</button>\n"
          "<a class=\"style_1\" href=\"/WiFi_Server\">Server</a>\n"
          "</form>\n"
          "</body>\n"
          "</html>\n";
      server.send(200, "text/html", ToSend);
    }

  #pragma region HTTP_Events_Pages
    void HTTP_Event_Reset() {
      String ToSend =
          "<!DOCTYPE html>\n"
          "<html>\n"
          "<body style=\"font-size:30px;\">\n"
          "<h1 style=\"font-size:60px;\">ESP2_Network</h1>\n"
          "<br/><br/><br/>\n"
          "<h2>Reset unita' tra 10 secondi\n"
          "<br/>\n"
          "La connessione sara' interrotta e bisognera' ricollegarsi</h2>\n"
          "</body>\n"
          "</html>\n";
      server.send(200, "text/html", ToSend);
      restart_requested = true;
      restart_start_time = millis(); // Corretto overflow millis
    }

    void HTTP_Event_Connected() {
      String HTML = 
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "<meta http-equiv=\"refresh\" content=\"10\">\n"
        "<style>\n"
        ".My_buttonstyle {\n"
        "  border: none;\n"
        "  color: white;\n"
        "  padding: 20px 32px;\n"
        "  text-align: center;\n"
        "  text-decoration: none;\n"
        "  display: inline-block;\n"
        "  font-size: 50px;\n"
        "  margin: 4px 2px;\n"
        "  cursor: pointer;\n"
        "  border-radius: 8px;\n"
        "  width: 60%;\n"
        "  display: block;\n"
        "  -ms-transform: translateY(-50%);\n"
        "  transform: translateY(-50%);\n"
        "}\n"
        ".button1 { background-color: #4CAF50; margin: 0; position: absolute; top: 55%; left: 20%; }\n"
        ".button1:active {background-color: #2980b9;}\n"
        ".button2 { background-color: #008CBA; margin: 0; position: absolute; top: 65%; left: 20%; }\n"
        ".button2:active {background-color: #2980b9;}\n"
        ".button3 { background-color: #008CBA; margin: 0; position: absolute; top: 75%; left: 20%; }\n"
        ".button3:active {background-color: #2980b9;}\n"
        ".button4 { background-color: #008CBA; margin: 0; position: absolute; top: 85%; left: 20%; }\n"
        ".button4:active {background-color: #2980b9;}\n"
        "</style>\n"
        "</head>\n"
        "<body style=\"font-size:22px;\">\n"
        "<h1 style=\"font-size:60px;\">ESP2_Network</h1>\n";

      HTML += "<h2>";
      HTML += _WiFi.AP_SSID(); 
      HTML += " IP: ";
      HTML += _WiFi.isConnected() ? _WiFi.LocalIP().toString() : "not connected";
      HTML += "</h2>\n";

      HTML += "<h2>Name:  ["; HTML += Board_Config.myname; HTML += "]</h2>\n";
      HTML += "<h2>Location:  ["; HTML += Board_Config.location; HTML += "]</h2>\n";
      HTML += "<h2>WiFi corrente:   ";
      HTML += (strlen(WiFi_Parameters.SSID) ? WiFi_Parameters.SSID : "Non configurata!");
      HTML += "<br>\n";

      HTML += "<h2>Status: "; HTML += _WiFi.Verbose_Status(); HTML += "</h2>\n";

      HTML += "<a class=\"My_buttonstyle button1\" href=\"/WiFi_Setup\">Configura WiFi</a>\n";
      HTML += "<a class=\"My_buttonstyle button2\" href=\"/App_Setup\">Setup</a>\n";
      HTML += "<a class=\"My_buttonstyle button3\" href=\"/ESP_reset\">Reset unità</a>\n";
      HTML += "<a class=\"My_buttonstyle button4\" href=\"/logs\">Log</a>\n";

      HTML += "</body>\n</html>\n";

      server.send(200, "text/html", HTML);
    }

    void HTTP_Event_WiFi_Setup() {
      if (HTTP_WiFi_Scan_WouldDropSession()) {
        HTTP_Event_WiFi_Setup_Manual();
        return;
      }

      String ToSend =
          "<!DOCTYPE html>\n"
          "<html>\n"
          "<head><meta http-equiv = \"refresh\" content = \"0; url = /WiFi_Setup_Scan\" /></head>\n"
          "<body style=\"font-size:30px;\">\n"
          "<h1 style=\"font-size:60px;\">ESP2_Network</h1>\n"
          "<br/><br/><br/>\n"
          "<h2>Ricerca reti WiFi accessibili\n"
          "<br/>\n"
          "Attendere il completamento della scansione .....</h2>\n"
          "</body>\n"
          "</html>\n";
      server.send(200, "text/html", ToSend);
    }

    void HTTP_Event_WiFi_Setup_Scan() {
      if (HTTP_WiFi_Scan_WouldDropSession()) {
        HTTP_Event_WiFi_Setup_Manual();
        return;
      }

      String ToSend = R"rawliteral(
      <!DOCTYPE html>
      <html>
      <head>
        <style>
          .style_1 {
            background-color: #4CAF50;
            color: yellow;
            padding: 20px 32px;
            text-decoration: none;
            font-size: 40px;
            margin: 4px 60px;
            border-radius: 8px;
          }
        </style>
      </head>
      <body style="font-size:20px;">
        <h1 style="font-size:60px;">ESP2_Network</h1>
        <br/>
        <h1><p style="color:red">Seleziona la rete WiFi</p></h1>

        <form action="/WiFi_Selected" method="get" id="form1">
          <fieldset>
            <legend>WiFi:</legend>
            <div>
      )rawliteral";

      ToSend.concat(_WiFi.HTTP_AP_Networks());

      ToSend.concat(R"rawliteral(
            </div>
          </fieldset>
          <br /><br />
          <p style="color:red">Password:</p>
          <input type="password" id="password" name="pwd" style="font-size:40px;"/>
          <br><br>
          <a class="style_1" href="/">Cancel</a>
          <button class="style_1" type="submit">Ok</button>
          <a class="style_1" href="/WiFi_Server">Server</a>
        </form>
      </body>
      </html>
      )rawliteral");

      log_add(ToSend + "\n");
      server.send(200, "text/html", ToSend);
    }

    void HTTP_Event_WiFi_Selected() {
      if (server.hasArg("wifi")) {
        String selWiFi = server.arg("wifi");
        String password = server.hasArg("pwd") ? server.arg("pwd") : "";
        selWiFi.toCharArray(WiFi_Parameters.SSID, selWiFi.length() + 1);
        password.toCharArray(WiFi_Parameters.PWD, password.length() + 1);
        EEPROM_Write();
      }
      HTTP_Event_Connected();
    }

    void HTTP_Event_WiFi_Server() {
      String str_void = "";
      str_void.toCharArray(WiFi_Parameters.SSID, 1);
      str_void.toCharArray(WiFi_Parameters.PWD, 1);
      _WiFi.Disconnect();
      EEPROM_Write();
      HTTP_Event_Connected();
    }

    void HTTP_Event_App_Setup() {
      String modelType = String(Board_Config.boardVersion); 
      String currentIP = Board_Config.IP; 

      String html = R"rawliteral(
        <!DOCTYPE html>
        <html>
        <head>
          <title>App_Setup</title>
          <style>
            .space { width: 20px; height: auto; display: inline-block; }
            .style_1 { color: black; padding: 20px 32px; text-decoration: none; font-size: 50px; margin: 4px 2px; border-radius: 8px; }
            .st_buttons { background-color:#4CAF50; color: yellow; }
          </style>
        </head>
        <body style="font-size:40px;">
          <form action="/App_Setup_Change" method="post" id="form1">
            <h1 style="font-size:60px;">ESP2_Network</h1>
            <label for="UnitName">Name:<br>
            <input class="style_1" name="UnitName" value=")rawliteral";
            
            html += Board_Config.myname;
            
            html += R"rawliteral(" type="text" id="UnitName" maxlength="19"><br><br>
            <label for="Location">Location:<br>
            <input class="style_1" name="Location" value=")rawliteral";
            
            html += Board_Config.location;

            html += R"rawliteral(" type="text" id="Location" maxlength="29"><br><br>
            <label for="modeltype">Seleziona il modello: </label>
            <select style="font-size:50px" name="ModelType" id="ModelType">
          )rawliteral";

          for (int i = 1; i <= 4; i++) {
            html += "<option value=\"" + String(i) + "\"";
            if (String(i) == modelType) html += " selected"; 
            html += ">" + String(i) + " Relay</option>";
          }

          html += R"rawliteral(
            </select><br><br>
            <label for="dhcp">DHCP:</label>
            <input type="checkbox" id="dhcp" name="dhcp"
          )rawliteral";

          if (Board_Config.DHCP == 1) html += " checked";

          html += R"rawliteral(
            onchange="toggleIPAddress()"><br><br>
            <label for="ip_address">IP Address:<br>
            <input class="style_1" name="ip_address" value=")rawliteral";

          html += currentIP;

          html += R"rawliteral(" type="text" id="ip_address" maxlength="15"
          )rawliteral";

          if (Board_Config.DHCP) html += " disabled";

          html += R"rawliteral(
            ><br><br>
            <button class="style_1 st_buttons" type="submit">Ok</button>  
            <div class="space"></div>
            <a class="style_1 st_buttons" href="/">Cancel</a>
          </form>

          <script>
            function toggleIPAddress() {
              const ipField = document.getElementById('ip_address');
              const dhcpChecked = document.getElementById('dhcp').checked;
              ipField.disabled = dhcpChecked; 
            }
          </script>
        </body>
        </html>
        )rawliteral";

      server.send(200, "text/html", html);
    }

    void HTTP_Event_App_Setup_Change() {
      bool updated = false;
      if (server.hasArg("UnitName")) {
        server.arg("UnitName").toCharArray(Board_Config.myname, 20);
        updated = true; 
      }
      if (server.hasArg("Location")) {
        server.arg("Location").toCharArray(Board_Config.location, 30);
        updated = true; 
      }
      if (server.hasArg("ModelType")) {
        log_add("ModelTypeModelType_Changed\n");
        Board_Config.boardVersion = server.arg("ModelType").toInt();
        updated = true; 
        gpioConfig();
      }
      if (server.hasArg("dhcp")) {
        Board_Config.DHCP = 1;
        updated = true; 
      } else {
        Board_Config.DHCP = 0;
        updated = true; 
      }

      if (server.hasArg("ip_address")) {
        log_add("ip_address_Changed\n");
        server.arg("ip_address").toCharArray(Board_Config.IP, 16);
        updated = true; 
      }

      if (updated) {
        EEPROM_Write();
      }

      HTTP_Event_Connected();
    }

    #pragma region LOG
      void handle_Logs_Root() {
        String html = R"rawliteral(
            <!DOCTYPE html>
            <html>
            <head>
                <title>ESP32 Logger</title>
                <script>
                    function fetchLogs() {
                        fetch("/logs_data")
                            .then(response => response.text())
                            .then(data => {
                                document.getElementById("logs").innerText = data;
                            });
                    }
                    setInterval(fetchLogs, 1000);
                </script>
            </head>
            <body>
                <h1>ESP32 Logs</h1>
                <pre id="logs">Loading...</pre>
            </body>
            </html>
        )rawliteral";

        server.send(200, "text/html", html);
    }

    void handle_Logs_Data() {
      server.send(200, "text/plain", get_logs_as_string()); 
    }
    #pragma endregion LOG

  #pragma endregion HTTP_Events_Pages

  #pragma region HTTP_Events_Assign
  void HTTP_Init_Events() {
    server.onNotFound(HTTP_Event_Connected);
    server.on("/", HTTP_Event_Connected);
    server.on("/WiFi_Setup", HTTP_Event_WiFi_Setup);
    server.on("/WiFi_Setup_Scan", HTTP_Event_WiFi_Setup_Scan);
    server.on("/WiFi_Selected", HTTP_Event_WiFi_Selected);
    server.on("/WiFi_Server", HTTP_Event_WiFi_Server);
    server.on("/App_Setup", HTTP_Event_App_Setup);
    server.on("/App_Setup_Change", HTTP_Event_App_Setup_Change);
    server.on("/ESP_reset", HTTP_Event_Reset);

    server.on("/logs", handle_Logs_Root);     
    server.on("/logs_data", handle_Logs_Data); 
  }
  #pragma endregion HTTP_Events_Assign

#pragma endregion HTTP

#pragma region Varie
  void Send_Out_Q_State(String Context) {
    time_last_QState_Sended = millis();
    String MyOut = "<05&";
    bool OutPresent = false;
    for (size_t i = 0; i < QX_COUNT; i++) {
      if (strlen(Board_Config.qx[i].qx_name) > 0 && strcmp(Board_Config.qx[i].qx_name , "none") != 0) {
        if (OutPresent) {
          MyOut.concat("!");
        }
        OutPresent = true;
        MyOut.concat(Board_Config.qx[i].qx_name);
        MyOut.concat(";");
        MyOut.concat(digitalRead(io_q[i].q));
      }
    }
    if (OutPresent) {
      MyOut.concat(">");
      udp_setup.beginPacket(_WiFi.Broadcast_IP(), udp_setup_port);
      udp_setup.write((const uint8_t *)MyOut.c_str(), MyOut.length());
      udp_setup.endPacket();
      log_add(Context); log_add(" ");
      log_add("Ho inviato: ");
      log_add(MyOut);
      log_add("\n");
    }
  }
#pragma endregion Varie

#pragma region UDP_SETUP receive
void udp_setup_receive() {
  int packet_size = udp_setup.parsePacket();

  if (packet_size > 0) {
    int readed = udp_setup.read(udp_setup_data_rx, UDP_SETUP_RX_SIZE - 1);
    log_add("Readed: "); log_add(String(readed) + "\n");
    
    if (readed > 0) {
      udp_setup_data_rx[readed] = '\0';
      String strRX = String(udp_setup_data_rx);
      
      log_add(String(millis()));
      log_add(" ***** Full: ");
      log_add(strRX + "\n");

      int16_t indexCmdSep = strRX.indexOf("&");
      if (indexCmdSep <= 1) {
        log_add("udp_setup_receive: malformed packet (missing &)\n");
        return;
      }
      
      String strCMD = strRX.substring(1, indexCmdSep);
      int16_t indexEnd = strRX.lastIndexOf(">");
      String strPayload = "";
      
      if (indexEnd > indexCmdSep) {
        strPayload = strRX.substring(indexCmdSep + 1, indexEnd);
      } else {
        strPayload = strRX.substring(indexCmdSep + 1);
        if (strPayload.endsWith("\n") || strPayload.endsWith("\r")) {
          strPayload = strPayload.substring(0, strPayload.length() - 1);
        }
      }

      if (strCMD == "**") {
        char firstChar = _WiFi.macAddress().charAt(17);
        randomSeed((int)firstChar);
        
        // Delay ridotto a max 50ms per non bloccare il loop del microcontrollore
        delay(random(5, 50));

        String tx = "<01&";
        tx.concat(Board_Config.myname); tx.concat(",");
        tx.concat(Board_Config.location); tx.concat(",");
        tx.concat(_WiFi.macAddress()); tx.concat(",");
        tx.concat(String(Board_Config.boardVersion));
        tx.concat(">");

        size_t lx = tx.length() + 1; 

        udp_setup.beginPacket(udp_setup.remoteIP(), udp_setup_port);
        udp_setup.write((const uint8_t *)tx.c_str(), lx); 
        udp_setup.endPacket();

        log_add("Ho inviato a "); log_add(udp_setup.remoteIP().toString());
        log_add(": "); log_add(tx); log_add("\n");
      }

      if (strCMD == "??") {
        String tx = "<02&";

        for (size_t i = 0; i < IX_COUNT; i++) {
          tx.concat(Board_Config.ix[i].disable_time_analisis ? "1" : "0"); tx.concat(",");
          tx.concat(Board_Config.ix[i].internal_pullup ? "1" : "0"); tx.concat(",");
          tx.concat(Board_Config.ix[i].noise > 10 ? String(Board_Config.ix[i].noise) : "10"); tx.concat(",");
          tx.concat(strlen(Board_Config.ix[i].qx_short) ? Board_Config.ix[i].qx_short : "none"); tx.concat(",");
          tx.concat(strlen(Board_Config.ix[i].qx_long) ? Board_Config.ix[i].qx_long : "none");
          if (i < IX_COUNT - 1) tx.concat("!");
        }
        tx.concat("|");
        for (size_t i = 0; i < QX_COUNT; i++) {
          tx.concat(Board_Config.qx[i].All_ON_OFF_Member ? "1" : "0"); tx.concat(",");
          if (int(Board_Config.qx[i].qx_name[0]) > 127) {
            Board_Config.qx[i].qx_name[0] = '\0';
          }
          tx.concat(strlen(Board_Config.qx[i].qx_name) ? Board_Config.qx[i].qx_name : "none"); tx.concat(",");
          if (Board_Config.qx[i].type < 0 || Board_Config.qx[i].type > 3) {
            Board_Config.qx[i].type = 1;
          }
          tx.concat(String(Board_Config.qx[i].type)); tx.concat(",");
          if (Board_Config.qx[i].timeout < 0) Board_Config.qx[i].timeout = 0;
          tx.concat(String(Board_Config.qx[i].timeout));
          if (i < QX_COUNT - 1) tx.concat("!");
        }

        tx.concat(">");

        size_t lx = tx.length() + 1;
        udp_setup.beginPacket(udp_setup.remoteIP(), udp_setup_port);
        udp_setup.write((const uint8_t *)tx.c_str(), lx);
        udp_setup.endPacket();

        log_add("Ho inviato: "); log_add(tx); log_add("\n");
      }

      if (strCMD == "save") {
        log_add("SAVE\n");
        
        int16_t indexIOSep = strPayload.indexOf("|"); 
        if (indexIOSep < 0) {
          log_add("SAVE malformed payload\n");
          return;
        }

        int16_t indexLeft_element = 0;
        int16_t indexRight_element = indexLeft_element;
        int16_t indexLeft_field = 0;
        int16_t indexRight_field = 0;
        String IO_element = "";
        int16_t element_index = 0;

        // 1. INGRESSI
        while (true) {
          indexRight_element = strPayload.indexOf("!", indexLeft_element);
          if (indexRight_element > indexIOSep || indexRight_element < 0) {
            indexRight_element = indexIOSep;
          }
          
          IO_element = strPayload.substring(indexLeft_element, indexRight_element);
          
          if (element_index < IX_COUNT && IO_element.length() > 0) {
            indexLeft_field = 0;
            
            indexRight_field = IO_element.indexOf(",", indexLeft_field);
            Board_Config.ix[element_index].disable_time_analisis = IO_element.substring(indexLeft_field, indexRight_field).toInt() > 0;
            indexLeft_field = indexRight_field + 1;

            indexRight_field = IO_element.indexOf(",", indexLeft_field);
            Board_Config.ix[element_index].internal_pullup = IO_element.substring(indexLeft_field, indexRight_field).toInt() > 0;
            indexLeft_field = indexRight_field + 1;

            indexRight_field = IO_element.indexOf(",", indexLeft_field);
            Board_Config.ix[element_index].noise = IO_element.substring(indexLeft_field, indexRight_field).toInt();
            indexLeft_field = indexRight_field + 1;

            indexRight_field = IO_element.indexOf(",", indexLeft_field);
            IO_element.substring(indexLeft_field, indexRight_field).toCharArray(Board_Config.ix[element_index].qx_short, indexRight_field - indexLeft_field + 1);
            indexLeft_field = indexRight_field + 1;

            IO_element.substring(indexLeft_field).toCharArray(Board_Config.ix[element_index].qx_long, IO_element.length() - indexLeft_field + 1);
          }

          indexLeft_element = indexRight_element + 1;
          if (indexLeft_element >= indexIOSep) break; 
          element_index++;
        }

        // 2. USCITE
        indexLeft_element = indexIOSep + 1;
        element_index = 0;
        
        while (true) {
          if (indexLeft_element > strPayload.length()) break;

          indexRight_element = strPayload.indexOf("!", indexLeft_element);
          if (indexRight_element < 1) {
            indexRight_element = strPayload.length();
          }
          
          IO_element = strPayload.substring(indexLeft_element, indexRight_element);
          
          if (element_index < QX_COUNT && IO_element.length() > 0) {
            indexLeft_field = 0;
            indexRight_field = IO_element.indexOf(",", indexLeft_field);
            Board_Config.qx[element_index].All_ON_OFF_Member = IO_element.substring(indexLeft_field, indexRight_field).toInt() > 0;

            indexLeft_field = indexRight_field + 1;
            indexRight_field = IO_element.indexOf(",", indexLeft_field);
            IO_element.substring(indexLeft_field, indexRight_field).toCharArray(Board_Config.qx[element_index].qx_name, indexRight_field - indexLeft_field + 1);

            indexLeft_field = indexRight_field + 1;
            indexRight_field = IO_element.indexOf(",", indexLeft_field);
            Board_Config.qx[element_index].type = IO_element.substring(indexLeft_field, indexRight_field).toInt();

            indexLeft_field = indexRight_field + 1;
            Board_Config.qx[element_index].timeout = IO_element.substring(indexLeft_field).toInt();
          }

          if (indexRight_element >= strPayload.length()) break; 
          
          indexLeft_element = indexRight_element + 1;
          element_index++;
        }

        EEPROM_Write();

        String tx = "<03&>";
        size_t lx = tx.length() + 1; 
        
        udp_setup.beginPacket(udp_setup.remoteIP(), udp_setup_port);
        udp_setup.write((const uint8_t *)tx.c_str(), lx);
        udp_setup.endPacket();
        
        log_add("Inviato feedback di SAVE\n");
      }
      
      if (strCMD == "?*") {
        char firstChar = _WiFi.macAddress().charAt(17);
        randomSeed((int)firstChar);
        delay(random(5, 50)); // Delay ridotto per non bloccare
        Send_Out_Q_State("?*");
      }

      if (strCMD == "05") {
        if (udp_setup.remoteIP()[3] < _WiFi.LocalIP()[3]) {
          int16_t Char_Start = 0;
          String Q_Value = "";
          String Q_Name = "";
          
          for (size_t i = 0; i < QX_COUNT; i++) {
            Q_Name = Board_Config.qx[i].qx_name;
            Q_Name.concat(";");
            Char_Start = strRX.indexOf(Q_Name);
            
            if (Char_Start > -1) {
              Char_Start += Q_Name.length();
              Q_Value = strRX.substring(Char_Start, Char_Start + 1);
              digitalWrite(io_q[i].q, Q_Value.toInt());
            }
          }
        }
      }
    }
  }
}
#pragma endregion UDP_SETUP receive

#pragma region UDP_WORK receive  
void udp_work_receice() {
  int packet_size = udp_work.parsePacket();
  if (packet_size > 0) {
    if (packet_size >= 25) { // Garantito l'accesso sicuro fino al byte index 24
      int toRead = packet_size;
      if (toRead > (int)(sizeof(udp_work_data_rx) - 1)) {
        toRead = sizeof(udp_work_data_rx) - 1;
      }
      
      int readed = udp_work.read(udp_work_data_rx, toRead);
      if (readed <= 0) return;
      
      udp_work_data_rx[readed] = '\0';
      int16_t inState = (int)udp_work_data_rx[24];

      log_add("Rx: "); log_add((char *)udp_work_data_rx); log_add(" " + String(inState) + "\n");

      if (strcmp(udp_work_data_rx, "all_on") == 0) {
        for (size_t i = 0; i < QX_COUNT; i++) {
          if (Board_Config.qx[i].All_ON_OFF_Member) {
            digitalWrite(io_q[i].q, true);
            io_q[i].TimeSwichedON = millis();
          }
        }
        Send_Out_Q_State("All_On_1");
      }
      else if (strcmp(udp_work_data_rx, "all_off") == 0) {
        for (size_t i = 0; i < QX_COUNT; i++) {
          if (Board_Config.qx[i].All_ON_OFF_Member) {
            digitalWrite(io_q[i].q, false);
          }
        }
        Send_Out_Q_State("All_Off_1");
      }
      else {
        bool Out_Changed = false;
        for (size_t i = 0; i < QX_COUNT; i++) {
          if (strcmp(udp_work_data_rx, Board_Config.qx[i].qx_name) == 0) {
            if (Board_Config.qx[i].type == QX_TYPE_REPLICATE || Board_Config.qx[i].type == QX_TYPE_REPLICATE_NEGATE) {
              if (Board_Config.qx[i].type == QX_TYPE_REPLICATE) {
                if (inState == 0 && digitalRead(io_q[i].q) == 0) {
                  io_q[i].TimeSwichedON = millis();
                }
                digitalWrite(io_q[i].q, !inState);
              }
              else {
                if (inState == 0 && digitalRead(io_q[i].q) == 1) {
                  io_q[i].TimeSwichedON = millis();
                }
                digitalWrite(io_q[i].q, inState);
              }
            }
            else if (inState == 1) {
              Out_Changed = true;
              digitalWrite(io_q[i].q, !digitalRead(io_q[i].q));
              if (digitalRead(io_q[i].q) == 1) {
                io_q[i].TimeSwichedON = millis();
              }
            }
          }
        }
        if (Out_Changed) {
          delay(20); // Ridotto delay per risposta veloce
          Send_Out_Q_State("W_Changed");
        }
      }
    }
  }
}
#pragma endregion UDP_WORK receive

#pragma region ** IO **
void io_gest() {
  for (size_t itx = 0; itx < IX_COUNT; itx++) {
    if (strlen(Board_Config.ix[itx].qx_short) || strlen(Board_Config.ix[itx].qx_long)) {
      bool_set_value(&ix[itx], digitalRead(io_i[itx]), Board_Config.ix[itx].noise);

      if (ix[itx].JustChanged && ix[itx].ValueTimePrevState > 0) {
        log_add("*************************** ");
        log_add(Board_Config.ix[itx].qx_short); log_add(" ");
        log_add(Board_Config.ix[itx].qx_long); log_add(" ");
        log_add("Changed: "); log_add(String(itx));
        log_add(" pin: "); log_add(String(io_i[itx])); log_add(" ");
        log_add("Q_Name:"); log_add(String(Board_Config.ix[itx].qx_short)); log_add(" - ");
        log_add(String(Board_Config.ix[itx].qx_long));
        log_add(" disable_time_analisis: "); log_add(String(Board_Config.ix[itx].disable_time_analisis));
        log_add(" ");
        
        String q_name = "";

        if (Board_Config.ix[itx].disable_time_analisis == 0) {
          if (ix[itx].JustUP) {
            log_add("JustUP ");
            if (TimeElapsed(ix[itx].ValueTimePrevState) > 10000) {
              q_name = "all_off";
            } else if (TimeElapsed(ix[itx].ValueTimePrevState) > 5000) {
              q_name = "all_on";
            } else if (TimeElapsed(ix[itx].ValueTimePrevState) > 800) {
              q_name = String(Board_Config.ix[itx].qx_long);
            } else if (TimeElapsed(ix[itx].ValueTimePrevState) > 100) {
              q_name = String(Board_Config.ix[itx].qx_short);
            } else {
              log_add("Filtrato < 100ms ");
            }
          }
        } else {
          log_add(ix[itx].JustUP ? "JustUP "  : "JustDOWN ");
          q_name = String(Board_Config.ix[itx].qx_short);
        }

        log_add("q_name: " + q_name + "\n");
        
        if (q_name.length() != 0 && q_name != "none") {
          if (q_name == "all_off") {
            for (size_t i = 0; i < QX_COUNT; i++) {
              if (Board_Config.qx[i].All_ON_OFF_Member) {
                digitalWrite(io_q[i].q, false);
              }
            }
            Send_Out_Q_State("All_Off");
          } else if (q_name == "all_on") {
            for (size_t i = 0; i < QX_COUNT; i++) {
              if (Board_Config.qx[i].All_ON_OFF_Member) {
                digitalWrite(io_q[i].q, true);
                io_q[i].TimeSwichedON = millis();
              }
            }
            Send_Out_Q_State("All_On");
          } else {
            bool Q_Is_Local = false;
            for (size_t i = 0; i < QX_COUNT; i++) {
              if (strcmp(Board_Config.qx[i].qx_name, q_name.c_str()) == 0) {
                Q_Is_Local = true;
                if (Board_Config.qx[i].type == QX_TYPE_REPLICATE || Board_Config.qx[i].type == QX_TYPE_REPLICATE_NEGATE) {
                  log_add("Tipo Replica o Replica Negate\n");

                  if (ix[itx].JustDown) {
                    io_q[i].TimeSwichedON = millis();
                  }
                  
                  if (Board_Config.qx[i].type == QX_TYPE_REPLICATE) {
                    digitalWrite(io_q[i].q, !ix[itx].Value);
                  } else {
                    digitalWrite(io_q[i].q, ix[itx].Value);
                  }
                } else if (ix[itx].Value == true) {
                  log_add("ix[i].Value == true: ");
                  log_add(String(ix[itx].Value == true) + "\n");
                  digitalWrite(io_q[i].q, !digitalRead(io_q[i].q));
                  if (digitalRead(io_q[i].q) == 1) {
                    io_q[i].TimeSwichedON = millis();
                  }
                }
              }
            }
            if (Q_Is_Local) {
              Send_Out_Q_State("Local");
            }
          }

          IPAddress destination_IP = _WiFi.isConnected() ? _WiFi.Broadcast_IP() : _WiFi.ap_BroadcastIP();

          q_name.getBytes(udp_work_data_tx, q_name.length() + 1);
          udp_work_data_tx[24] = ix[itx].Value ? 1 : 0;
          udp_work.beginPacket(destination_IP, udp_work_port);
          udp_work.write(udp_work_data_tx, 25);
          udp_work.endPacket();
        }
      }
    }
  }

  // Gestione timeout uscite
  bool TimeOut_Occurred = false;
  for (size_t i = 0; i < QX_COUNT; i++) {
    if (Board_Config.qx[i].timeout > 0 && digitalRead(io_q[i].q) == 1) {
      if (TimeElapsed(io_q[i].TimeSwichedON) > Board_Config.qx[i].timeout) {
        digitalWrite(io_q[i].q, 0);
        TimeOut_Occurred = true;
      }
    }
  }
  if (TimeOut_Occurred) {
    Send_Out_Q_State("TOut");
  }
}
#pragma endregion **IO **

void setup() {
  Serial.begin(115200);
  delay(200);
  
  _WiFi.AP_Init(_soft_ap_password);
  HTTP_Init_Events();
  server.begin();

  if (EEPROM.begin(788)) {
    log_add("Ok to initialise EEPROM\n");
  } else {
    log_add("failed to initialise EEPROM\n");
  }

  EEPROM_Read();
  gpioConfig();

  _WiFi.Disconnect();
  delay(100);
  _WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  udp_work.stop();
  udp_setup.stop();
  delay(200);
  udp_work.begin(_WiFi.ap_LocalIP(), udp_work_port);
  udp_setup.begin(_WiFi.ap_LocalIP(), udp_setup_port);

  time_wifi_millis_last_try = 100000; 
  time_udp_work = millis();
  time_udp_setup = millis();
  memset(udp_work_data_tx, 0, sizeof(udp_work_data_tx)); 
}

void loop() {
  if (TimeElapsed(time_wifi) > 500) {
    time_wifi = millis();
    bool_set_value(&wifi_is_conneted, _WiFi.isConnected() && _WiFi.hasIP());

    if (!wifi_is_conneted.Value) {
      if (wifi_is_conneted.JustDown) {
        udp_work.stop();
        udp_setup.stop();
        delay(200);
        udp_work.begin(_WiFi.ap_LocalIP(), udp_work_port);
        udp_setup.begin(_WiFi.ap_LocalIP(), udp_setup_port);
      }

      if (TimeElapsed(time_wifi_millis_last_try) > 20000) {
        _WiFi.Disconnect();
        delay(200);
        time_wifi_millis_last_try = millis();

        if (!Board_Config.DHCP) {
            IPAddress ip, mask, gate, DNS1, DNS2;
            mask.fromString("255.255.255.0");
            validStaticIP("8.8.8.8", DNS1);
            validStaticIP("8.8.4.4", DNS2);
            if (validStaticIP(Board_Config.IP, ip)) {
                gate = ip;
                gate[3] = 1;
                _WiFi.config(ip, gate, mask, DNS1, DNS2);
            }
        }
        _WiFi.Connect(WiFi_Parameters.SSID, WiFi_Parameters.PWD);
      }
    } else {
      if (TimeElapsed(time_last_QState_Sended) > 15000) {
        Send_Out_Q_State("15000");
      }
      
      if (wifi_is_conneted.JustUP) {
        log_add("Connected!!!!!!!!\n");
        udp_work.stop();
        udp_setup.stop();
        delay(200);
        udp_work.begin(_WiFi.LocalIP(), udp_work_port);
        udp_setup.begin(_WiFi.LocalIP(), udp_setup_port);
        delay(200);
        Send_Out_Q_State("WiFi_UP");
      }
    }
  }

  server.handleClient();

  if (TimeElapsed(time_udp_work) > 10) {
    time_udp_work = millis();
    udp_work_receice();
  }

  if (TimeElapsed(time_udp_setup) > 100) {
    time_udp_setup = millis();
    udp_setup_receive();
  }

  io_gest();

  // Controllo differenziale di riavvio corretto
  if (restart_requested && (millis() - restart_start_time >= 10000)) {
    restart_requested = false;
    ESP.restart();
  }

  delay(5);
}