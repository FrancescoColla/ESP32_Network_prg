
#include <Bit_Utils.h>


/// @brief Assegna il valore ad una struttura stBool
/// @param xBit 
/// @param NewValue 
void bool_set_value(struct Tstruct_bool *xBit, bool NewValue, uint16_t NoiseTime )
{
    xBit->JustDown = false;
    xBit ->JustUP = false;
    xBit->JustChanged = false;
    
    if (xBit ->ValueTmp != NewValue)
    {
      xBit ->ValueTmp = NewValue;
      xBit ->ValueTimeTmp = millis();
    }

    if (xBit ->Value != xBit ->ValueTmp)
    {
      if (TimeElapsed(xBit->ValueTimeTmp) > NoiseTime)
      {
        Serial.print("Noise: "); Serial.println(TimeElapsed(xBit->ValueTimeTmp));
        xBit->ValueTimePrevState = xBit->ValueTime;
        xBit->Value = xBit->ValueTmp;
        xBit->ValueTime = xBit->ValueTimeTmp; // millis();
        xBit->JustChanged = true;
        if (xBit -> Value) { xBit -> JustUP = true; }
        else { xBit -> JustDown = true; }
      }
    }
    
}

uint32_t TimeElapsed(uint32_t TimeStart) {
  uint32_t Mymillis = millis();
  if (Mymillis < TimeStart)
  {
    return 0xFFFFFFFF /*4294967295*/ - TimeStart + Mymillis;
  }
  else
  {
    return Mymillis - TimeStart;
  }
}

TON::TON()
  {_DelayTime = 100;}

TON::TON(uint32_t DelayTime)
  {_DelayTime = DelayTime ;
   Serial.begin(19200);
   delay(1000);
   Serial.println("TON_Created");
  }

void TON::Setup(uint32_t DelayTime)
  { _DelayTime = DelayTime;}

bool TON::Q(bool Value)
  {
    if (Value)
    {
      if (_StartMicros == 0)
      {
        _StartMicros = millis();
      }
      return TimeElapsed(_StartMicros) >= _DelayTime;
    }
    else
    {
      _StartMicros = 0;
      return 0;
    }
  }

uint32_t TON::ET()
  {
    if (_StartMicros > 0)
    {
      return TimeElapsed(_StartMicros);
    }
    else
    {
      return 0;
    }
  }

 void TON::Reset()
    {
      _StartMicros = 0;
    }



TOF::TOF(uint32_t DelayTime)
  { _DelayTime = DelayTime;}
    
TOF::TOF() 
  {_DelayTime = 100;};            //costruttore default
  //~TOF() {};            //distruttore

void TOF::Setup(uint32_t DelayTime) 
  {
    _DelayTime = DelayTime; 
  }

bool TOF::Q(bool Value)
  {
    if (!Value)
    {
      if (_StartMicros == 0)
      {
        _StartMicros = millis();
      }

      return ! (TimeElapsed(_StartMicros) >= _DelayTime);
    }
    else
    {
      _StartMicros = 0;
      return true;
    }
  }

uint32_t TOF::ET()
  {
    if (_StartMicros > 0)
    {
      return TimeElapsed(_StartMicros);
    }
    else
    {
      return 0;
    }
  }

void TOF::Reset()
  {
    _StartMicros = 0;
  }


RTrig_TON::RTrig_TON(uint16_t DelayTime)
  {
  _TON.Setup(DelayTime);
  }

bool RTrig_TON::Q(bool Value)
  {
    bool result = Value == true && _Value_Old == false && _TON.Q(Value) == true;
    _Value_Old = Value;
    return result;
  }





FTrig_TOF::FTrig_TOF(uint16_t DelayTime)
{
  _TOF.Setup(DelayTime);
}

FTrig_TOF::FTrig_TOF()
{
  _TOF.Setup(100);
}

bool FTrig_TOF::Q(bool Value)
{
  bool TOF_Value = _TOF.Q(Value);
  bool result = _TOF_Old == true && TOF_Value == false;
  _TOF_Old = TOF_Value;
  return result;
}

