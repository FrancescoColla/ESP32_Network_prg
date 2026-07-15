#ifndef MyBit_UtilsDefined
  #define MyBit_UtilsDefined

  #include <Arduino.h>

  typedef struct Tstruct_bool  
  {
      bool                ValueTmp = false;
      unsigned int        ValueTimeTmp = 0;
      bool                Value = false;
      bool                JustChanged = false;
      bool                JustUP = false;
      bool                JustDown = false;
      unsigned long       ValueTime = 0;
      unsigned long       ValueTimePrevState = 0;
  } stBool;

  void bool_set_value(struct Tstruct_bool *xBit, bool NewValue, uint16_t NoiseTime = 0);

  uint32_t TimeElapsed(uint32_t TimeStart);

  class TON {
    private:
      uint32_t _DelayTime = 0;
      uint32_t _StartMicros = 0;

    public:
      TON(uint32_t DelayTime);     //costruttore
      TON();                    //costruttore default
      //~TON() {};          /distruttore

      void Setup(uint32_t DelayTime); 

      bool Q(bool Value);
      uint32_t ET();
      void Reset();
  };

  class TOF {
    private:
      uint32_t _DelayTime = 0;
      uint32_t _StartMicros = 0;

    public:
      TOF(uint32_t DelayTime);
      TOF();            //costruttore default
      //~TOF() {};            //distruttore

      void Setup(uint32_t DelayTime);

      bool Q(bool Value);
      uint32_t ET();
      void Reset();
  };

  class RTrig_TON 
      {
    private:
      TON _TON;
      bool _Value_Old = false;
      
      public:
          RTrig_TON(uint16_t Delay);

          bool Q(bool Value);
      };



      class FTrig_TOF
      {
    private:
      TOF _TOF;
      bool _TOF_Old = false;
      
      public:
          FTrig_TOF(uint16_t DelayTime);
          FTrig_TOF();
          bool Q(bool Value);
      };

#endif
