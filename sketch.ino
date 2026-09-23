

#include <Wire.h>
#include <RTClib.h>
#include <OneWire.h>
#include <DallasTemperature.h>


#define PIN_TRIG        6      
#define PIN_ECHO        7     
#define PIN_FLOW_IN     2     
#define PIN_FLOW_OUT    3      
#define PIN_TURBIDITY   A0     
#define PIN_PH          A1     
#define PIN_ONEWIRE     8      

#define PIN_RELAY_PUMP  9     
#define PIN_VALVE_MAIN  10     
#define PIN_VALVE_INLET 11     
#define PIN_LED_RED     12    
#define PIN_LED_YELLOW  13     
#define PIN_BUZZER      5      


const float TANK_HEIGHT_CM        = 100.0;   
const float LEVEL_START_NORMAL    = 20.0;    
const float LEVEL_STOP_NORMAL     = 90.0;    
const float LEVEL_START_OFFPEAK   = 40.0;  
const float LEVEL_STOP_OFFPEAK    = 100.0;   

const float LEAK_THRESHOLD_L      = 10.0;   
// const float LEAK_THRESHOLD_L      = 0.05; 

const unsigned long LEAK_DURATION_MS = 5UL * 60UL * 1000UL;   
// const unsigned long EMPTY_TIMEOUT_MS = 15UL * 60UL * 1000UL; 

// const unsigned long LEAK_DURATION_MS = 15UL * 1000UL;   
const unsigned long EMPTY_TIMEOUT_MS = 20UL * 1000UL;  

const float TURBIDITY_MAX_NTU     = 5.0;
const float PH_MIN                = 6.5;
const float PH_MAX                = 8.5;
const float TEMP_MIN_C            = 5.0;
const float TEMP_MAX_C            = 35.0;

const float FLOW_CAL_PULSES_PER_L = 450.0;  

const int OFFPEAK_START_HOUR = 23;
const int OFFPEAK_END_HOUR   = 6;


const unsigned long T1_PERIOD_LEVEL      = 1000;
const unsigned long T2_PERIOD_LEAK       = 1000;
const unsigned long T3_PERIOD_QUALITY    = 2000;
const unsigned long T4_PERIOD_WATCHDOG   = 1000;
const unsigned long T5_PERIOD_ALARM      = 50;

unsigned long lastRunT1 = 0, lastRunT2 = 0, lastRunT3 = 0, lastRunT4 = 0, lastRunT5 = 0;

RTC_DS3231 rtc;
OneWire oneWire(PIN_ONEWIRE);
DallasTemperature tempSensor(&oneWire);

volatile unsigned long flowInPulses  = 0;
volatile unsigned long flowOutPulses = 0;

bool pumpOn            = false;
bool mainValveClosed   = false;   
bool inletValveClosed  = false;   
bool emergencyStop     = false;  

float tankLevelPercent = 0;


float leakVolumeAccum   = 0;     
unsigned long leakStartTime = 0;
bool leakSuspected = false;


unsigned long fillStartTime = 0;
bool fillingTracked = false;


bool alarmLeakActive    = false;   
bool alarmQualityActive = false;   
bool alarmEmergencyActive = false; 


void isrFlowIn()  { flowInPulses++; }
void isrFlowOut() { flowOutPulses++; }

void setup() {
  Serial.begin(9600);

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);
  pinMode(PIN_FLOW_IN, INPUT_PULLUP);
  pinMode(PIN_FLOW_OUT, INPUT_PULLUP);
  pinMode(PIN_RELAY_PUMP, OUTPUT);
  pinMode(PIN_VALVE_MAIN, OUTPUT);
  pinMode(PIN_VALVE_INLET, OUTPUT);
  pinMode(PIN_LED_RED, OUTPUT);
  pinMode(PIN_LED_YELLOW, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  digitalWrite(PIN_RELAY_PUMP, LOW);
  digitalWrite(PIN_VALVE_MAIN, HIGH);   
  digitalWrite(PIN_VALVE_INLET, HIGH);
  digitalWrite(PIN_LED_RED, LOW);
  digitalWrite(PIN_LED_YELLOW, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  attachInterrupt(digitalPinToInterrupt(PIN_FLOW_IN), isrFlowIn, RISING);
  attachInterrupt(digitalPinToInterrupt(PIN_FLOW_OUT), isrFlowOut, RISING);

tempSensor.begin();
tempSensor.setWaitForConversion(false);
  if (!rtc.begin()) {
    Serial.println(F("RTC یافت نشد - ساعات اوج/کم‌مصرف نادیده گرفته می‌شود"));
  }

  Serial.println(F("سامانه مدیریت مصرف آب راه‌اندازی شد."));
}


void loop() {
  unsigned long now = millis();

  if (now - lastRunT1 >= T1_PERIOD_LEVEL)    { lastRunT1 = now; taskLevelAndPump(); }
  if (now - lastRunT2 >= T2_PERIOD_LEAK)     { lastRunT2 = now; taskLeakDetection(); }
  if (now - lastRunT3 >= T3_PERIOD_QUALITY)  { lastRunT3 = now; taskWaterQuality(); }
  if (now - lastRunT4 >= T4_PERIOD_WATCHDOG) { lastRunT4 = now; taskEmergencyWatchdog(); }
  if (now - lastRunT5 >= T5_PERIOD_ALARM)    { lastRunT5 = now; taskAlarmPatterns(); }
}


float readTankLevelPercent() {
  digitalWrite(PIN_TRIG, LOW);  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH); delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  long duration = pulseIn(PIN_ECHO, HIGH, 30000UL); 
  float distanceCm = duration * 0.0343 / 2.0;

  if (duration == 0) return tankLevelPercent;        

  float waterHeight = TANK_HEIGHT_CM - distanceCm;
  float percent = (waterHeight / TANK_HEIGHT_CM) * 100.0;
  return constrain(percent, 0, 100);
}

bool isOffPeakHour() {
  DateTime nowTime = rtc.now();
  int h = nowTime.hour();
  if (OFFPEAK_START_HOUR > OFFPEAK_END_HOUR) {
    return (h >= OFFPEAK_START_HOUR || h < OFFPEAK_END_HOUR);
  }
  return (h >= OFFPEAK_START_HOUR && h < OFFPEAK_END_HOUR);
}


void taskLevelAndPump() {
  tankLevelPercent = readTankLevelPercent();

  if (emergencyStop || mainValveClosed) {
    setPump(false);
    return;
  }

  bool offPeak = isOffPeakHour();
//   Serial.print("offPeak="); Serial.print(offPeak);
//  Serial.print(" level="); Serial.print(tankLevelPercent);
//   Serial.print(" pumpOn="); Serial.println(pumpOn);
  float startLevel = offPeak ? LEVEL_START_OFFPEAK : LEVEL_START_NORMAL;
  float stopLevel  = offPeak ? LEVEL_STOP_OFFPEAK  : LEVEL_STOP_NORMAL;

  if (!pumpOn && tankLevelPercent <= startLevel) {
    setPump(true);
    fillStartTime = millis();
    fillingTracked = true;
  } else if (pumpOn && tankLevelPercent >= stopLevel) {
    setPump(false);
    fillingTracked = false;
  }
}

void setPump(bool on) {
  pumpOn = on;
  digitalWrite(PIN_RELAY_PUMP, on ? HIGH : LOW);
}


void taskLeakDetection() {
  noInterrupts();
  unsigned long inPulses  = flowInPulses;
  unsigned long outPulses = flowOutPulses;
  flowInPulses = 0;
  flowOutPulses = 0;
  interrupts();

  float litersIn  = inPulses  / FLOW_CAL_PULSES_PER_L;
  float litersOut = outPulses / FLOW_CAL_PULSES_PER_L;
  float diff = litersIn - litersOut;

  if (diff > (LEAK_THRESHOLD_L / (LEAK_DURATION_MS / T2_PERIOD_LEAK))) {

    if (!leakSuspected) {
      leakSuspected = true;
      leakStartTime = millis();
      leakVolumeAccum = 0;
    }
    leakVolumeAccum += diff;

    if (leakVolumeAccum >= LEAK_THRESHOLD_L &&
        (millis() - leakStartTime) >= LEAK_DURATION_MS) {
      triggerLeakAlarm();
    }
  } else {
    
    leakSuspected = false;
    leakVolumeAccum = 0;
  }
}

void triggerLeakAlarm() {
  mainValveClosed = true;
  digitalWrite(PIN_VALVE_MAIN, LOW);  
  setPump(false);
  alarmLeakActive = true;
}


void taskWaterQuality() {
  int turbidityRaw = analogRead(PIN_TURBIDITY);
  float turbidityNTU = map(turbidityRaw, 0, 1023, 0, 30);  

  int phRaw = analogRead(PIN_PH);
  float phValue = (phRaw / 1023.0) * 14.0;                

  Serial.print("Turbidity NTU="); Serial.print(turbidityNTU);
  Serial.print(" | PH="); Serial.println(phValue);
  tempSensor.requestTemperatures();
  float tempC = tempSensor.getTempCByIndex(0);

  bool badQuality = (turbidityNTU > TURBIDITY_MAX_NTU) ||
                     (phValue < PH_MIN || phValue > PH_MAX) ||
                     (tempC < TEMP_MIN_C || tempC > TEMP_MAX_C);

  if (badQuality && !inletValveClosed) {
    inletValveClosed = true;
    digitalWrite(PIN_VALVE_INLET, LOW);  
    alarmQualityActive = true;
  } else if (!badQuality && inletValveClosed) {
    inletValveClosed = false;
    digitalWrite(PIN_VALVE_INLET, HIGH);  
    alarmQualityActive = false;
  }
}


void taskEmergencyWatchdog() {
  if (emergencyStop) return;

  if (fillingTracked && pumpOn) {
    if (tankLevelPercent < 5.0 &&
        (millis() - fillStartTime) >= EMPTY_TIMEOUT_MS) {
      emergencyStop = true;
      setPump(false);
      alarmEmergencyActive = true;
    }
  }
}


void taskAlarmPatterns() {
  unsigned long t = millis();
  //  Serial.print("leak="); Serial.print(alarmLeakActive);
  // Serial.print(" quality="); Serial.print(alarmQualityActive);
  // Serial.print(" emergency="); Serial.print(alarmEmergencyActive);
  // Serial.print(" buzzerPin="); Serial.println(digitalRead(PIN_BUZZER));

  // if (alarmLeakActive) {
    
  //   bool onPhase = (t % 1500UL) < 1000UL;
  //   digitalWrite(PIN_LED_RED, onPhase ? HIGH : LOW);
  //   if (!alarmEmergencyActive) {
  //     if (onPhase) tone(PIN_BUZZER, 1000); else noTone(PIN_BUZZER);
  //   }
  // } 
  if (alarmLeakActive) {
    bool onPhase = (t % 1500UL) < 1000UL;

    static bool lastPhase = !onPhase;
    if (onPhase != lastPhase) {
      // Serial.print("t="); Serial.print(t); Serial.print(" phase="); Serial.println(onPhase);
      lastPhase = onPhase;
    }

    digitalWrite(PIN_LED_RED, onPhase ? HIGH : LOW);
    if (!alarmEmergencyActive) {
      if (onPhase) tone(PIN_BUZZER, 1000); else noTone(PIN_BUZZER);
    }
  }
  else {
    digitalWrite(PIN_LED_RED, LOW);
  }

  if (alarmQualityActive && !alarmEmergencyActive) {
    unsigned long cycle = t % 1750UL;
    bool beepOn = (cycle < 150) || (cycle >= 300 && cycle < 450) ||
                  (cycle >= 600 && cycle < 750);
    digitalWrite(PIN_LED_YELLOW, beepOn ? HIGH : LOW);
    if (!alarmLeakActive) {
      if (beepOn) tone(PIN_BUZZER, 2000); else noTone(PIN_BUZZER);
    }
  } else {
    digitalWrite(PIN_LED_YELLOW, LOW);
  }

  if (alarmEmergencyActive) {
    tone(PIN_BUZZER, 1500);
  } else if (!alarmLeakActive && !alarmQualityActive) {
    noTone(PIN_BUZZER);
  }
}