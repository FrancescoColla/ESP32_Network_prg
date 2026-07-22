
#include <MyWiFi.hpp>

void MyWiFi::AP_Init(char *Pwd) 
{
    Serial.println("PWD.Len");
    Serial.println( strlen(Pwd));
    WiFi.mode(WIFI_MODE_APSTA);
    WiFi.disconnect();
    delay(500);
    
    local_ip=IPAddress(192,168,100,1);
    
    gateway=local_ip;
    subnet=IPAddress(255,255,255,0);
    WiFi.softAPConfig(local_ip, gateway, subnet);

    WiFi.softAP(WiFi.macAddress().c_str(), "87654321");
    delay(100);

    Serial.println("WiFi Initialized");
    strcpy(TestPassword, Pwd); //_SSID_STA_Current = ;
    Serial.print("Password: #");
    Serial.print(TestPassword); Serial.println("#");
}

String MyWiFi::macAddress()
    {
                uint8_t mac[6];
        /*WiFi.macAddress(mac);
        Serial.print("mac: ");
        Serial.println(mac[5]);*/

        return WiFi.macAddress().c_str(); }

MyWiFi::MyWiFi(){}

MyWiFi::~MyWiFi(){}

/// @brief Connessione ad una rete WiFi 
/// @param SSID_STA dimensione 31 caratteri
/// @param PWD_STA  password del WiFi
void MyWiFi::Connect(char *SSID_STA, char *PWD_STA)
{
    //Serial.print("Request connecting.... "); Serial.print(strlen(SSID_STA)); Serial.print("  "); Serial.println(Net_Scan_In_Progress ? "true" : "false");
    if (strlen(SSID_STA) > 0 && !Net_Scan_In_Progress)  //tenta connessione solo se esiste SSID
    {
        Serial.println(" step_1");
        bool_set_value(&wifi_is_connecting, _Connecting); // (WiFi.status() == WL_IDLE_STATUS));
        Serial.print(" wifi_is_connecting => "); Serial.print(wifi_is_connecting.Value ? "true " : "false "); Serial.println(_Connecting ? "true" : "false");
        if (wifi_is_connecting.Value == true && (TimeElapsed(wifi_is_connecting.ValueTime) > 15000))
        {
            Serial.print("Connecting long time "); Serial.println(wifi_is_connecting.Value ? "true" : "false");
            _Connecting = false;
            WiFi.disconnect();
            delay(500);
        }
        
        SSID_Changed = strcmp(SSID_STA, _SSID_STA_Current) != 0;
        PWD_Changed =  strcmp(PWD_STA, _PWD_STA_Current) != 0;
        if (SSID_Changed || PWD_Changed){
            strcpy(_SSID_STA_Current, SSID_STA); 
            strcpy(_PWD_STA_Current, PWD_STA); 
            WiFi.disconnect();
            delay(500);
            _Connecting = false;
        }
        
        if ((WiFi.status() != WL_CONNECTED))
        {
            if (!_Connecting) {
                if (AP_Network_Exist(SSID_STA))
                {
                    Serial.print("Connessione a: ");
                    _Connecting = true;
                    WiFi.setAutoReconnect(false);
                    WiFi.persistent(false);

                    WiFi.begin(SSID_STA, PWD_STA);
                    Serial.print(SSID_STA);
                    Serial.print("-");
                    Serial.println(PWD_STA);
                }
            }
        }
        else
        {
            if (_Connecting)
            {
                _Connecting = false;

                WiFi.printDiag(Serial);
            }
        }
        
    }
}

String MyWiFi::Verbose_Status(){
    int status = WiFi.status();
    switch (status)
    {
    case WL_IDLE_STATUS:
        return "WL_IDLE_STATUS";
        break;
    case WL_NO_SSID_AVAIL:
        return "WL_NO_SSID_AVAIL";
        break;
    case WL_SCAN_COMPLETED:
        return "WL_SCAN_COMPLETED";
        break;
    case WL_CONNECTED:
        _Connecting = false;
        return "WL_CONNECTED";
        break;
    case WL_CONNECT_FAILED:
        return "WL_CONNECT_FAILED";
        break;
    case WL_CONNECTION_LOST:
        return "WL_CONNECTION_LOST";
        break;
    case WL_DISCONNECTED:
        return "WL_DISCONNECTED";
        break;
    default:
        return "WL_UNKNOW_ERROR_" + String(WiFi.status());
        break;
    }
}

bool MyWiFi::config(IPAddress Local_IP, IPAddress Gateway_IP, IPAddress Subnet_Mask, IPAddress DNS_1, IPAddress DNS_2){
    return WiFi.config(Local_IP, Gateway_IP,Subnet_Mask, DNS_1, DNS_2);
}

bool MyWiFi::isConnected()
    {
        if (WiFi.isConnected()) {_Connecting = false;}
        return WiFi.isConnected();}

bool MyWiFi::hasIP()
    {
        IPAddress local_ip = WiFi.localIP();
        return local_ip != IPAddress(0, 0, 0, 0);
    }

bool MyWiFi::Connecting()
    {return _Connecting;}

void MyWiFi::Disconnect()
    {if (WiFi.status() != WL_DISCONNECTED)
        {
            Serial.print(" *** DISCONNECTING ***");
            WiFi.disconnect();
        }
    }

bool MyWiFi::status()
    {return WiFi.status();}

IPAddress MyWiFi::LocalIP()
    {return WiFi.localIP();}    

IPAddress MyWiFi::Broadcast_IP()
    {return WiFi.broadcastIP();}

bool MyWiFi::AP_Network_Exist(char *SSID_STA)
{
    byte result = false;

    String RetStr = "";

    Serial.print("AP_Network_Exist => WiFi scan ...  ");

//    if ((WiFi.status() != WL_DISCONNECTED) && (WiFi.status() != WL_NO_SSID_AVAIL))
//    {
        Serial.print(" *** DISCONNECTING ***");
        WiFi.disconnect();
        Serial.print("   ***** Waiting_Disconnected! *****");
        delay(500);
        while (WiFi.status() == WL_CONNECTED) // != WL_DISCONNECTED && (WiFi.status() != WL_NO_SSID_AVAIL) && (WiFi.status() != WL_CONNECTION_LOST))
        {
            Serial.print("AP_Network_Exist: "); Serial.print(Verbose_Status());
            delay(1000);
        }
//    }
    _Connecting = false;
    
    Serial.println("Starting Scan");
    Net_Scan_In_Progress = true;
    int n = WiFi.scanNetworks();
    Net_Scan_In_Progress = false;
    if (n == 0)
    {
        Serial.println("Nessun WiFi trovato");
    }
    else
    {
        Serial.println("Trovate " + String(n)+" Reti WiFi");
        for (size_t i = 0; i < n; i++)
        {
            if (WiFi.SSID(i) == SSID_STA)
            {
                result = true;
            }
        }
    }
    WiFi.scanDelete();
    Serial.print(" SSID: "); Serial.println(result > 0 ? " Esistente" : "Inesistente");
    return result;

}




String MyWiFi::HTTP_AP_Networks()
{

    String RetStr = "";

    Serial.print("WiFi scan ...  ");

    if ((WiFi.status() != WL_DISCONNECTED) && (WiFi.status() != WL_NO_SSID_AVAIL))
    {
        WiFi.disconnect();
        Serial.print("Waiting_Disconnected!");
        while (WiFi.status() != WL_DISCONNECTED && (WiFi.status() != WL_NO_SSID_AVAIL))
        {
            Serial.println(Verbose_Status());
            delay(500);
        }
    }

    _Connecting = false;
    
    Serial.println("Starting Scan");
    Net_Scan_In_Progress = true;
    int n = WiFi.scanNetworks();
    Net_Scan_In_Progress = false;
    if (n == 0)
    {
        Serial.println("Nessun WiFi trovato");
    }
    else
    {
        Serial.println("Trovate " + String(n)+" Reti WiFi");
        for (size_t i = 0; i < n; i++)
        {
            if (i > 3)
            {
                break;
            }
            Serial.println(String(i) + " SSID: " + WiFi.SSID(i) + " RSSI: " + String(WiFi.RSSI(i)));
            Serial.println((WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? " " : "*");
            delay(10);
         
        }

        for (size_t i = 0; i < n ; i++)
        {
            if (i > 3)
            {
                break;
            }
            RetStr.concat("<input type=\"radio\" name=\"wifi\" value=\""+ WiFi.SSID(i) +"\" />\n");
            RetStr.concat("<label for = \"" + WiFi.SSID(i) + "\"> SSID: " + WiFi.SSID(i) + " RSSI: " + String(WiFi.RSSI(i)) + " </label>\n");
            RetStr.concat("<br />\n");
            RetStr.concat("<br />\n");
        }
    }
    WiFi.scanDelete();
    return RetStr;

}


String MyWiFi::AP_SSID()
    {return WiFi.softAPSSID();}

IPAddress MyWiFi::ap_LocalIP()
    {return local_ip;}        

IPAddress MyWiFi::ap_BroadcastIP()
    {return WiFi.softAPBroadcastIP();}    


