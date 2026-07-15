#ifndef MyWiFi_lsDefined
    #define MyWiFi_lsDefined

    #include <Arduino.h>
    #include <WiFi.h>
    #include <Bit_Utils.h>

    class MyWiFi{
    private:
        char TestPassword[31] = {0};    //"Vodafone-34738566";// "\0";

        char _SSID_STA_Current[31] = {0};    //"Vodafone-34738566";// "\0";
        char _PWD_STA_Current[31] = {0};    //"Vodafone-34738566";// "\0";
        bool SSID_Changed = false;
        bool PWD_Changed = false;
        bool Net_Scan_In_Progress = false;
        IPAddress local_ip; //(192, 168, 100, 1);
        IPAddress gateway; 
        IPAddress subnet;  
        stBool wifi_is_connecting;

    public:
    
        MyWiFi ();
        ~MyWiFi ();

        void AP_Init(char *Pwd) ;

        bool config(IPAddress Local_IP, IPAddress Gateway_IP, IPAddress Subnet_Mask, IPAddress DNS_1, IPAddress DNS_2);

        void Connect(char *SSID_STA, char *PWD_STA);
        bool isConnected();

        void Disconnect();

        String HTTP_AP_Networks();

        bool AP_Network_Exist(char *SSID_STA);

        String AP_SSID();

        String macAddress();
        
        bool Connecting();
        bool status();
        IPAddress LocalIP();
        IPAddress Broadcast_IP();

        IPAddress ap_LocalIP();
        IPAddress ap_BroadcastIP();

        String Verbose_Status();

        bool _Connecting = false;


    };
#endif