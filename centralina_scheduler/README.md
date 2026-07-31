# ESP32 Centralina Scheduler

Progetto separato per centralina domotica su ESP32.

## Funzione
- Legge regole orarie da `LittleFS` (`/scheduler.json`).
- Usa RTC DS3231 come sorgente ora.
- Ogni minuto valuta le regole attive.
- Invia comando UDP broadcast nel formato:
  - `Nome_Uscita/ON\0`
  - `Nome_Uscita/OFF\0`

La regola di conflitto e': **ultima regola valida nell'array vince**.

## File principali
- `src/main.cpp`: scheduler, gestione RTC, HTTP API, UDP TX.
- `data/scheduler.json`: configurazione iniziale regole.
- `platformio.ini`: target ESP32 + dipendenze.

## Configurazione WiFi
Nel file `src/main.cpp` impostare:
- `WIFI_STA_SSID`
- `WIFI_STA_PWD`

La centralina avvia anche un AP locale (fallback):
- SSID: `CENTRALINA_<MAC_HEX>`
- Password: `87654321`

## Strutture logiche
### str_a (regola)
Campi nel JSON:
- `Enabled` (bool)
- `Validita_Start` (1..7, 1=lun, 7=dom)
- `Validita_End` (1..7, 1=lun, 7=dom)
- `Ora_Attivazione` (`HH:MM`)
- `ON_or_OFF` (`ON` oppure `OFF`)

### str_b (uscita)
Campi nel JSON:
- `Name` (string, nome uscita)
- `Enabled` (bool)
- `rules` (array di `str_a`)

## API minime
- `GET /status`: stato centralina
- `GET /rules`: contenuto JSON regole
- `POST /rules`: aggiorna regole (body JSON plain)
- `POST /reload`: ricarica regole da file

## Esempio JSON
```json
{
  "outputs": [
    {
      "Name": "luce_1",
      "Enabled": true,
      "rules": [
        {
          "Enabled": true,
          "Validita_Start": 1,
          "Validita_End": 5,
          "Ora_Attivazione": "18:30",
          "ON_or_OFF": "ON"
        },
        {
          "Enabled": true,
          "Validita_Start": 1,
          "Validita_End": 5,
          "Ora_Attivazione": "23:30",
          "ON_or_OFF": "OFF"
        }
      ]
    }
  ]
}
```

## Note compatibilita'
- I nodi ESP32_Network devono avere il parser aggiornato per `Nome_Uscita/ON|OFF`.
- I pacchetti UDP includono sempre il terminatore nullo (`len + 1`).
