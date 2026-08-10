 #include <LiquidCrystal.h>
#include <math.h>
#include <string.h>

// =====================================================
// LCD
// =====================================================

const int rs = 0;
const int en = 10;
const int d4 = 4;
const int d5 = 5;
const int d6 = 6;
const int d7 = 7;

LiquidCrystal lcd(rs, en, d4, d5, d6, d7);


// =====================================================
// ZERO CROSS DETECTOR
// =====================================================

#define ZCD_A_PIN 2
#define ZCD_B_PIN 3


// =====================================================
// RMS INPUTS
// =====================================================

#define RMS_A_PIN A2
#define RMS_B_PIN A3


// =====================================================
// ONLY ARDUINO OUTPUT
// =====================================================

#define SYNC_OUTPUT 8


// =====================================================
// USER SETTINGS
// =====================================================

#define NOMINAL_FREQUENCY 50.0

#define MAX_FREQ_ERROR 0.5

#define MIN_VALID_FREQ 49.0
#define MAX_VALID_FREQ 51.0

#define MAX_PHASE_ERROR 5.0

#define NOMINAL_VOLTAGE 230.0
#define MAX_VOLTAGE_DEVIATION 10.0
#define MAX_SOURCE_VOLTAGE_DIFFERENCE 10.0


// =====================================================
// TRANSFORMER
// =====================================================

#define PRIMARY_VOLTAGE   230.0
#define SECONDARY_VOLTAGE 13.5


// =====================================================
// VOLTAGE DIVIDER
// =====================================================

#define R_TOP    100000.0
#define R_BOTTOM 10000.0

const float DIVIDER_RATIO = R_BOTTOM / (R_TOP + R_BOTTOM);
const float TRANSFORMER_RATIO = PRIMARY_VOLTAGE / SECONDARY_VOLTAGE;


// =====================================================
// TRANSFER TIMING
// =====================================================

#define B_RECOVERY_DELAY 5000UL
#define SOURCE_FAILURE_DELAY 500UL
#define MIN_TRANSFER_INTERVAL 3000UL


// =====================================================
// FREQUENCY MEASUREMENT
// =====================================================

#define FREQUENCY_SAMPLES 8
#define MIN_VALID_PERIOD_US 7000UL
#define MAX_VALID_PERIOD_US 30000UL

volatile unsigned long lastEdgeA = 0;
volatile unsigned long lastEdgeB = 0;

volatile unsigned long periodA = 20000;
volatile unsigned long periodB = 20000;

volatile unsigned long zcA_time = 0;
volatile unsigned long zcB_time = 0;

float smoothedPeriodA = 10000.0f;
float smoothedPeriodB = 10000.0f;


// =====================================================
// FREQUENCY FILTER BUFFERS
// =====================================================

float frequencyBufferA[FREQUENCY_SAMPLES];
float frequencyBufferB[FREQUENCY_SAMPLES];

byte freqIndexA = 0;
byte freqIndexB = 0;

byte validSamplesA = 0;   // <-- NEW
byte validSamplesB = 0;   // <-- NEW

float freqA = 0.0;        // <-- changed from 50.0
float freqB = 0.0;        // <-- changed from 50.0


// =====================================================
// VOLTAGE
// =====================================================

float voltageA = 0.0;
float voltageB = 0.0;


// =====================================================
// PHASE
// =====================================================

float phaseAngle = 0.0;
float phaseOffset = 0.0;


// =====================================================
// CALIBRATION
// =====================================================

bool calibrated = false;
unsigned long calibrationStart = 0;
float phaseAccumulator = 0.0;
int phaseSamples = 0;


// =====================================================
// SYNCHRONIZATION
// =====================================================

bool syncOK = false;


// =====================================================
// SOURCE STATE
// =====================================================

enum Source
{
  SOURCE_A,
  SOURCE_B
};

Source activeSource = SOURCE_A;


// =====================================================
// TIMING
// =====================================================

unsigned long badSourceStart = 0;
unsigned long bHealthyStart = 0;
unsigned long lastTransferTime = 0;


// =====================================================
// LCD STATUS
// =====================================================

char statusMessage[21] = "STARTING";
unsigned long lastLCD = 0;


// =====================================================
// LOW PASS FILTER
// =====================================================

float filterValue(float oldValue, float newValue)
{
  return oldValue * 0.90 + newValue * 0.10;
}


// =====================================================
// ZCD A INTERRUPT
// =====================================================

void zcdA_ISR()
{
  unsigned long now = micros();

  if (lastEdgeA != 0)
  {
    unsigned long p = now - lastEdgeA;

    if (p >= MIN_VALID_PERIOD_US && p <= MAX_VALID_PERIOD_US)
    {
      periodA = p;
    }
  }

  lastEdgeA = now;
  zcA_time = now;
}


// =====================================================
// ZCD B INTERRUPT
// =====================================================

void zcdB_ISR()
{
  unsigned long now = micros();

  if (lastEdgeB != 0)
  {
    unsigned long p = now - lastEdgeB;

    if (p >= MIN_VALID_PERIOD_US && p <= MAX_VALID_PERIOD_US)
    {
      periodB = p;
    }
  }

  lastEdgeB = now;
  zcB_time = now;
}


// =====================================================
// CHECK SOURCE A ZCD
// =====================================================

bool sourceAHasSignal()
{
  unsigned long now = micros();
  unsigned long t;

  noInterrupts();
  t = zcA_time;
  interrupts();

  if (t == 0) return false;

  if ((unsigned long)(now - t) > 60000UL)
  {
    return false;
  }

  return true;
}


// =====================================================
// CHECK SOURCE B ZCD
// =====================================================

bool sourceBHasSignal()
{
  unsigned long now = micros();
  unsigned long t;

  noInterrupts();
  t = zcB_time;
  interrupts();

  if (t == 0) return false;

  if ((unsigned long)(now - t) > 60000UL)
  {
    return false;
  }

  return true;
}


// =====================================================
// UPDATE FREQUENCY  (REAL MEASUREMENT ONLY)
// =====================================================

void updateFrequency()
{
  unsigned long pA;
  unsigned long pB;

  noInterrupts();
  pA = periodA;
  pB = periodB;
  interrupts();

  // -------------------- SOURCE A --------------------
  if (pA >= MIN_VALID_PERIOD_US && pA <= MAX_VALID_PERIOD_US)
  {
    smoothedPeriodA = smoothedPeriodA * 0.85f + (float)pA * 0.15f;
    freqA = 1000000.0f / (smoothedPeriodA * 2.0f);
  }

  // -------------------- SOURCE B --------------------
  if (pB >= MIN_VALID_PERIOD_US && pB <= MAX_VALID_PERIOD_US)
  {
    smoothedPeriodB = smoothedPeriodB * 0.85f + (float)pB * 0.15f;
    freqB = 1000000.0f / (smoothedPeriodB * 2.0f);
  }
}


// =====================================================
// RAW PHASE
// =====================================================

float getRawPhase()
{
  unsigned long tA;
  unsigned long tB;
  unsigned long pA;
  unsigned long pB;

  noInterrupts();
  tA = zcA_time;
  tB = zcB_time;
  pA = periodA;
  pB = periodB;
  interrupts();

  if (tA == 0 || tB == 0)
  {
    return 999.0;
  }

  if (pA < 14000 || pA > 23000 || pB < 14000 || pB > 23000)
  {
    return 999.0;
  }

  float averagePeriod = (pA + pB) / 2.0;

  long difference = (long)tA - (long)tB;

  while (difference > averagePeriod / 2.0)
  {
    difference -= (long)averagePeriod;
  }

  while (difference < -averagePeriod / 2.0)
  {
    difference += (long)averagePeriod;
  }

  float phase = ((float)difference / averagePeriod) * 360.0;

  while (phase > 180.0) phase -= 360.0;
  while (phase < -180.0) phase += 360.0;

  return phase;
}


// =====================================================
// PHASE CALIBRATION
// =====================================================

void calibratePhase()
{
  if (calibrated) return;

  if (millis() - calibrationStart < 5000UL)
  {
    float p = getRawPhase();

    if (p > -180.0 && p < 180.0)
    {
      phaseAccumulator += p;
      phaseSamples++;
    }
    return;
  }

  if (phaseSamples > 100)
  {
    phaseOffset = phaseAccumulator / (float)phaseSamples;
  }
  else
  {
    phaseOffset = 0;
  }

  calibrated = true;
}


// =====================================================
// CALCULATE PHASE
// =====================================================

float calculatePhase()
{
  float p = getRawPhase();

  if (p > 900) return 999.0;

  p -= phaseOffset;

  while (p > 180.0) p -= 360.0;
  while (p < -180.0) p += 360.0;

  return p;
}


// =====================================================
// RMS VOLTAGE
// =====================================================

float measureVoltage(byte pin)
{
  const int samples = 300;
  double sumSq = 0;

  for (int i = 0; i < samples; i++)
  {
    int adc = analogRead(pin);
    float volts = adc * 5.0 / 1023.0;
    float centered = volts - 2.5;
    sumSq += centered * centered;
  }

  float rmsADC = sqrt(sumSq / samples);
  float secondaryRMS = rmsADC / DIVIDER_RATIO;
  float primaryRMS = secondaryRMS * TRANSFORMER_RATIO;

  return primaryRMS;
}


// =====================================================
// VOLTAGE CHECKS
// =====================================================

bool voltageAOK()
{
  if (voltageA < 1.0) return false;

  float deviation = fabs(voltageA - NOMINAL_VOLTAGE) / NOMINAL_VOLTAGE * 100.0;
  return deviation <= MAX_VOLTAGE_DEVIATION;
}

bool voltageBOK()
{
  if (voltageB < 1.0) return false;

  float deviation = fabs(voltageB - NOMINAL_VOLTAGE) / NOMINAL_VOLTAGE * 100.0;
  return deviation <= MAX_VOLTAGE_DEVIATION;
}


// =====================================================
// FREQUENCY CHECKS
// =====================================================

bool frequencyAOK()
{
  if (!sourceAHasSignal()) return false;
  return freqA >= MIN_VALID_FREQ && freqA <= MAX_VALID_FREQ;
}

bool frequencyBOK()
{
  if (!sourceBHasSignal()) return false;
  return freqB >= MIN_VALID_FREQ && freqB <= MAX_VALID_FREQ;
}


// =====================================================
// SOURCE HEALTH
// =====================================================

bool sourceAOK()
{
  return sourceAHasSignal() && frequencyAOK() && voltageAOK();
}

bool sourceBOK()
{
  return sourceBHasSignal() && frequencyBOK() && voltageBOK();
}


// =====================================================
// UPDATE SYNCHRONIZATION
// =====================================================

void updateSync()
{
  phaseAngle = calculatePhase();

  float frequencyDifference = fabs(freqA - freqB);

  float voltageDifference = 0;
  float averageVoltage = (voltageA + voltageB) / 2.0;

  if (averageVoltage > 1.0)
  {
    voltageDifference = fabs(voltageA - voltageB) / averageVoltage * 100.0;
  }

  bool phaseOK = (phaseAngle != 999.0) && (fabs(phaseAngle) <= MAX_PHASE_ERROR);
  bool frequencyOK = (frequencyDifference <= MAX_FREQ_ERROR);
  bool voltageDifferenceOK = (voltageDifference <= MAX_SOURCE_VOLTAGE_DIFFERENCE);

  bool A_OK = sourceAOK();
  bool B_OK = sourceBOK();

  syncOK = calibrated && A_OK && B_OK && phaseOK && frequencyOK && voltageDifferenceOK;
}


// =====================================================
// FORMAT FAULT MESSAGE
// =====================================================

void getSyncError(char *msg)
{
  if (!sourceBHasSignal())
  {
    strcpy(msg, "B NO ZCD");
    return;
  }

  if (!frequencyBOK())
  {
    char number[10];
    dtostrf(freqB, 4, 1, number);
    strcpy(msg, "B FREQ ");
    strcat(msg, number);
    return;
  }

  if (!voltageBOK())
  {
    char number[10];
    dtostrf(voltageB, 4, 0, number);
    strcpy(msg, "B VOLT ");
    strcat(msg, number);
    return;
  }

  if (!sourceAHasSignal())
  {
    strcpy(msg, "A NO ZCD");
    return;
  }

  if (!frequencyAOK())
  {
    char number[10];
    dtostrf(freqA, 4, 1, number);
    strcpy(msg, "A FREQ ");
    strcat(msg, number);
    return;
  }

  if (!voltageAOK())
  {
    char number[10];
    dtostrf(voltageA, 4, 0, number);
    strcpy(msg, "A VOLT ");
    strcat(msg, number);
    return;
  }

  if (phaseAngle == 999.0)
  {
    strcpy(msg, "PHASE INVALID");
    return;
  }

  if (fabs(phaseAngle) > MAX_PHASE_ERROR)
  {
    char number[10];
    dtostrf(phaseAngle, 4, 1, number);
    strcpy(msg, "PHASE ");
    strcat(msg, number);
    return;
  }

  float frequencyDifference = fabs(freqA - freqB);
  if (frequencyDifference > MAX_FREQ_ERROR)
  {
    char number[10];
    dtostrf(frequencyDifference, 4, 2, number);
    strcpy(msg, "DFREQ ");
    strcat(msg, number);
    return;
  }

  float averageVoltage = (voltageA + voltageB) / 2.0;
  if (averageVoltage > 1.0)
  {
    float voltageDifference = fabs(voltageA - voltageB) / averageVoltage * 100.0;
    if (voltageDifference > MAX_SOURCE_VOLTAGE_DIFFERENCE)
    {
      char number[10];
      dtostrf(voltageDifference, 4, 1, number);
      strcpy(msg, "DVOLT ");
      strcat(msg, number);
      return;
    }
  }

  if (!calibrated)
  {
    strcpy(msg, "CALIBRATING");
    return;
  }

  strcpy(msg, "NOT SYNCHRONIZED");
}


// =====================================================
// TRANSFER STATE MANAGEMENT
// =====================================================

void manageTransfer()
{
  bool A_OK = sourceAOK();
  bool B_OK = sourceBOK();
  unsigned long now = millis();

  // ========== CURRENTLY ON SOURCE B ==========
  if (activeSource == SOURCE_B)
  {
    bHealthyStart = 0;

    if (!B_OK)
    {
      if (badSourceStart == 0)
      {
        badSourceStart = now;
      }

      if (now - badSourceStart >= SOURCE_FAILURE_DELAY)
      {
        digitalWrite(SYNC_OUTPUT, LOW);
        activeSource = SOURCE_A;
        lastTransferTime = now;

        if (A_OK)
          strcpy(statusMessage, "B FAIL -> A");
        else
          strcpy(statusMessage, "A+B BAD");

        badSourceStart = 0;
      }
      else
      {
        strcpy(statusMessage, "B CHECK...");
      }
    }
    else
    {
      badSourceStart = 0;
      digitalWrite(SYNC_OUTPUT, HIGH);
      strcpy(statusMessage, "ON B OK");
    }
    return;
  }

  // ========== CURRENTLY ON SOURCE A ==========
  if (activeSource == SOURCE_A)
  {
    badSourceStart = 0;

    // A failed
    if (!A_OK)
    {
      if (B_OK && syncOK)
      {
        if (now - lastTransferTime >= MIN_TRANSFER_INTERVAL)
        {
          digitalWrite(SYNC_OUTPUT, HIGH);
          activeSource = SOURCE_B;
          lastTransferTime = now;
          strcpy(statusMessage, "A BAD -> B");
        }
        else
        {
          strcpy(statusMessage, "A BAD WAIT");
        }
      }
      else
      {
        digitalWrite(SYNC_OUTPUT, LOW);

        if (!B_OK)
          strcpy(statusMessage, "A+B BAD");
        else
          getSyncError(statusMessage);
      }
      return;
    }

    // A is healthy
    if (B_OK)
    {
      if (bHealthyStart == 0)
      {
        bHealthyStart = now;
      }

      if (now - bHealthyStart >= B_RECOVERY_DELAY)
      {
        if (syncOK)
        {
          if (now - lastTransferTime >= MIN_TRANSFER_INTERVAL)
          {
            digitalWrite(SYNC_OUTPUT, HIGH);
            activeSource = SOURCE_B;
            lastTransferTime = now;
            strcpy(statusMessage, "TRANSFER B");
          }
          else
          {
            strcpy(statusMessage, "B READY");
          }
        }
        else
        {
          digitalWrite(SYNC_OUTPUT, LOW);
          getSyncError(statusMessage);
        }
      }
      else
      {
        digitalWrite(SYNC_OUTPUT, LOW);
        strcpy(statusMessage, "B RECOVERING");
      }
    }
    else
    {
      bHealthyStart = 0;
      digitalWrite(SYNC_OUTPUT, LOW);
      getSyncError(statusMessage);
    }
  }
}


// =====================================================
// LCD (NO FLICKER)
// =====================================================

void updateLCD()
{
  // Line 0 - Frequencies
  lcd.setCursor(0, 0);
  lcd.print("A:");
  lcd.print(freqA, 2);
  lcd.print("Hz ");

  lcd.setCursor(10, 0);
  lcd.print("B:");
  lcd.print(freqB, 2);
  lcd.print("Hz");

  // Line 1 - Phase + dF
  lcd.setCursor(0, 1);
  lcd.print("PH:");
  if (phaseAngle > 900.0)
    lcd.print("---.-");
  else
    lcd.print(phaseAngle, 1);
  lcd.print((char)223);   // degree symbol
  lcd.print(" ");

  lcd.setCursor(11, 1);
  lcd.print("dF:");
  lcd.print(fabs(freqA - freqB), 2);

  // Line 2 - Voltages
  lcd.setCursor(0, 2);
  lcd.print("VA:");
  lcd.print(voltageA, 0);
  lcd.print("V ");

  lcd.setCursor(10, 2);
  lcd.print("VB:");
  lcd.print(voltageB, 0);
  lcd.print("V ");

  // Line 3 - Status
  lcd.setCursor(0, 3);
  lcd.print("                    ");  // clear line
  lcd.setCursor(0, 3);

  if (!calibrated)
    lcd.print("CALIBRATING");
  else
    lcd.print(statusMessage);
}


// =====================================================
// SETUP
// =====================================================

void setup()
{
  pinMode(ZCD_A_PIN, INPUT);
  pinMode(ZCD_B_PIN, INPUT);

  pinMode(SYNC_OUTPUT, OUTPUT);
  digitalWrite(SYNC_OUTPUT, LOW);

  lcd.begin(20, 4);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("STS Controller");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");

for (byte i = 0; i < FREQUENCY_SAMPLES; i++)
{
  frequencyBufferA[i] = 0.0;
  frequencyBufferB[i] = 0.0;
}

validSamplesA = 0;
validSamplesB = 0;
freqA = NOMINAL_FREQUENCY;
freqB = NOMINAL_FREQUENCY;
smoothedPeriodA = 10000.0f;
smoothedPeriodB = 10000.0f;

  activeSource = SOURCE_A;

  attachInterrupt(digitalPinToInterrupt(ZCD_A_PIN), zcdA_ISR, RISING);
  attachInterrupt(digitalPinToInterrupt(ZCD_B_PIN), zcdB_ISR, RISING);

  calibrationStart = millis();
  delay(1000);
}


// =====================================================
// LOOP
// =====================================================

void loop()
{
  updateFrequency();

  voltageA = filterValue(voltageA, measureVoltage(RMS_A_PIN));
  voltageB = filterValue(voltageB, measureVoltage(RMS_B_PIN));

  calibratePhase();

  if (calibrated)
  {
    updateSync();
    manageTransfer();
  }

  if (millis() - lastLCD > 400UL)
  {
    lastLCD = millis();
    updateLCD();
  }
}