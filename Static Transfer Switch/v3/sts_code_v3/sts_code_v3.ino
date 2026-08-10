#include <LiquidCrystal.h>
#include <math.h>

//=====================================================
// LCD
//=====================================================
const int rs = 0;
const int en = 10;
const int d4 = 4;
const int d5 = 5;
const int d6 = 6;
const int d7 = 7;

LiquidCrystal lcd(rs, en, d4, d5, d6, d7);
 

//=====================================================
// PINS
//=====================================================

#define ZCD_A_PIN 2
#define ZCD_B_PIN 3

#define RMS_A_PIN A2
#define RMS_B_PIN A3

#define SYNC_OUTPUT 8

//=====================================================
// USER SETTINGS
//=====================================================

#define MAX_PHASE_ERROR 5.0
#define MAX_FREQ_ERROR  0.5
#define MAX_VOLT_ERROR  10.0

// Transformer
#define PRIMARY_VOLTAGE   230.0
#define SECONDARY_VOLTAGE 13.5

// Divider
#define R_TOP    100000.0
#define R_BOTTOM 10000.0

//=====================================================
// CALCULATED CONSTANTS
//=====================================================

const float DIVIDER_RATIO =
R_BOTTOM / (R_TOP + R_BOTTOM);

const float TRANSFORMER_RATIO =
PRIMARY_VOLTAGE / SECONDARY_VOLTAGE;

//=====================================================
// VARIABLES
//=====================================================

volatile unsigned long lastA = 0;
volatile unsigned long lastB = 0;

volatile unsigned long halfA = 10000;
volatile unsigned long halfB = 10000;

volatile unsigned long zcA_time = 0;
volatile unsigned long zcB_time = 0;

float freqA = 50.0;
float freqB = 50.0;

float voltageA = 0;
float voltageB = 0;

float phaseAngle = 0;
float phaseOffset = 0;
float phaseSlip = 0;

bool syncOK = false;

bool calibrated = false;

unsigned long calibrationStart = 0;

long phaseAccumulator = 0;
int phaseSamples = 0;

unsigned long lastLCD = 0;

//=====================================================
// LOW PASS FILTER
//=====================================================

float filterValue(float oldValue, float newValue)
{
  return oldValue * 0.90 +
         newValue * 0.10;
}

//=====================================================
// ZCD A ISR
//=====================================================

void zcdA_ISR()
{
  unsigned long now = micros();

  if(lastA != 0)
  {
    unsigned long p = now - lastA;

    if(p > 7000 && p < 13000)
    {
      halfA = p;
    }
  }

  lastA = now;
  zcA_time = now;
}

//=====================================================
// ZCD B ISR
//=====================================================

void zcdB_ISR()
{
  unsigned long now = micros();

  if(lastB != 0)
  {
    unsigned long p = now - lastB;

    if(p > 7000 && p < 13000)
    {
      halfB = p;
    }
  }

  lastB = now;
  zcB_time = now;
}

//=====================================================
// RAW PHASE
//=====================================================

float getRawPhase()
{
  long difference;

  noInterrupts();

  difference =
    (long)zcA_time -
    (long)zcB_time;

  unsigned long hA = halfA;
  unsigned long hB = halfB;

  interrupts();

  float fullCycle =
    ((hA + hB) / 2.0) * 2.0;

  float phase =
    ((float)difference /
     fullCycle) * 360.0;

  while(phase > 180)
    phase -= 360;

  while(phase < -180)
    phase += 360;

  return phase;
}

//=====================================================
// PHASE CALIBRATION
//=====================================================

void calibratePhase()
{
  if(calibrated)
    return;

  if(millis() - calibrationStart < 5000)
  {
    phaseAccumulator += getRawPhase();
    phaseSamples++;
    return;
  }

  if(phaseSamples > 100)
  {
    phaseOffset =
      phaseAccumulator /
      (float)phaseSamples;
  }

  calibrated = true;
}

//=====================================================
// PHASE
//=====================================================

float calculatePhase()
{
  float p =
    getRawPhase() -
    phaseOffset;

  while(p > 180)
    p -= 360;

  while(p < -180)
    p += 360;

  return p;
}

//=====================================================
// RMS MEASUREMENT
//=====================================================

float measureVoltage(byte pin)
{
  const int samples = 300;

  double sumSq = 0;

  for(int i = 0; i < samples; i++)
  {
    int adc = analogRead(pin);

    float volts =
      adc * 5.0 / 1023.0;

    float centered =
      volts - 2.5;

    sumSq +=
      centered * centered;
  }

  float rmsADC =
    sqrt(sumSq / samples);

  float secondaryRMS =
    rmsADC / DIVIDER_RATIO;

  float primaryRMS =
    secondaryRMS *
    TRANSFORMER_RATIO;

  return primaryRMS;
}

//=====================================================
// UPDATE FREQUENCY
//=====================================================

void updateFrequency()
{
  float newA =
    1000000.0 /
    (2.0 * halfA);

  float newB =
    1000000.0 /
    (2.0 * halfB);

  freqA =
    filterValue(freqA, newA);

  freqB =
    filterValue(freqB, newB);
}

//=====================================================
// SYNC CHECK
//=====================================================

void updateSync()
{
  phaseAngle =
    calculatePhase();

  phaseSlip =
    freqA - freqB;

  float freqError =
    fabs(freqA - freqB);

  float avgVoltage =
    (voltageA + voltageB) / 2.0;

  float voltError = 0;

  if(avgVoltage > 1)
  {
    voltError =
      fabs(voltageA - voltageB) /
      avgVoltage * 100.0;
  }

  bool phaseOK =
    fabs(phaseAngle)
    <= MAX_PHASE_ERROR;

  bool freqOK =
    freqError
    <= MAX_FREQ_ERROR;

  bool voltOK =
    voltError
    <= MAX_VOLT_ERROR;

  syncOK =
    phaseOK &&
    freqOK &&
    voltOK;

  digitalWrite(
    SYNC_OUTPUT,
    syncOK);
}

//=====================================================
// LCD
//=====================================================

void updateLCD()
{
  lcd.clear();

  lcd.setCursor(0,0);
  lcd.print("A:");
  lcd.print(freqA,2);
  lcd.print(" B:");
  lcd.print(freqB,2);

  lcd.setCursor(0,1);
  lcd.print("PH:");
  lcd.print(phaseAngle,1);

  lcd.print(" S:");
  lcd.print(phaseSlip,2);

  lcd.setCursor(0,2);
  lcd.print("VA:");
  lcd.print(voltageA,0);

  lcd.print(" VB:");
  lcd.print(voltageB,0);

  lcd.setCursor(0,3);

  if(!calibrated)
  {
    lcd.print("CALIBRATING");
  }
  else if(syncOK)
  {
    lcd.print("SYNC ENABLE");
  }
  else
  {
    lcd.print("TRANSFER BLOCK");
  }
}

//=====================================================
// SETUP
//=====================================================

void setup()
{
  pinMode(ZCD_A_PIN, INPUT);
  pinMode(ZCD_B_PIN, INPUT);

  pinMode(SYNC_OUTPUT, OUTPUT);

  digitalWrite(
    SYNC_OUTPUT,
    LOW);

  lcd.begin(20,4);

  lcd.clear();
  lcd.print("STS Controller");

  attachInterrupt(
    digitalPinToInterrupt(ZCD_A_PIN),
    zcdA_ISR,
    RISING);

  attachInterrupt(
    digitalPinToInterrupt(ZCD_B_PIN),
    zcdB_ISR,
    RISING);

  calibrationStart =
    millis();

  delay(1000);
}

//=====================================================
// LOOP
//=====================================================

void loop()
{
  updateFrequency();

  voltageA =
    filterValue(
      voltageA,
      measureVoltage(RMS_A_PIN));

  voltageB =
    filterValue(
      voltageB,
      measureVoltage(RMS_B_PIN));

  calibratePhase();

  if(calibrated)
  {
    updateSync();
  }

  if(millis() - lastLCD > 500)
  {
    lastLCD = millis();
    updateLCD();
  }
}
