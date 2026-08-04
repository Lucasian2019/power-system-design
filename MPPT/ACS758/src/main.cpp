#include <LiquidCrystal.h>
#include <avr/io.h>
#include <avr/interrupt.h>

// =================================================
// LCD
// =================================================

const int rs = 3;
const int en = 2;
const int d4 = 4;
const int d5 = 5;
const int d6 = 6;
const int d7 = 7;

LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

// =================================================
// PIN DEFINITIONS (UNO)
// =================================================

#define ZERO_CROSS_PIN 9      // INT0
#define TRIAC_PIN      8
#define POT_PIN        A0
#define RMS_PIN        A2

// =================================================
// DIMMER VARIABLES
// =================================================

volatile bool zeroCross = false;

volatile uint16_t counter = 0;
volatile uint16_t pulseCounter = 0;

volatile uint8_t pulseDelay = 0;
volatile bool triacPulseActive = false;

int potValue = 0;

// =================================================
// DISPLAY VARIABLES
// =================================================

constexpr float FUNDAMENTAL_FREQUENCY_HZ = 50.0;
constexpr float ADC_REFERENCE_VOLTAGE = 5.0;
constexpr int ADC_MAX = 1023;
constexpr float RMS_VOLTAGE_DIVIDER_RATIO = 11.0; // adjust to your hardware

float stsFrequencyHz = FUNDAMENTAL_FREQUENCY_HZ;
float stsPhaseAngleDeg = 0.0;
float stsRmsVoltage = 0.0;

unsigned long lastLcdUpdateMs = 0;

// =================================================
// CALCULATION FUNCTIONS
// =================================================

float calculateFrequencyHz(unsigned long halfCycleMicros)
{
  if (halfCycleMicros > 5000 && halfCycleMicros < 15000)
  {
    return 1000000.0 / (halfCycleMicros * 2.0);
  }
  return stsFrequencyHz;
}

uint8_t calculatePulseDelay(int potValue)
{
  return map(potValue, 0, 1023, 0, 90);
}

float calculatePhaseAngleDeg(uint8_t pulseDelay)
{
  return (pulseDelay / 100.0) * 180.0;
}

float calculateRmsVoltage()
{
  const int samples = 50;
  long sumRaw = 0;
  long sumSqRaw = 0;

  for (int i = 0; i < samples; i++)
  {
    int raw = analogRead(RMS_PIN);
    sumRaw += raw;
    sumSqRaw += (long)raw * raw;
  }

  float meanRaw = sumRaw / (float)samples;
  float meanSqRaw = sumSqRaw / (float)samples;
  float varianceRaw = meanSqRaw - meanRaw * meanRaw;
  if (varianceRaw < 0) varianceRaw = 0;

  float rmsVolts = sqrt(varianceRaw) * ADC_REFERENCE_VOLTAGE / ADC_MAX;
  return rmsVolts * RMS_VOLTAGE_DIVIDER_RATIO;
}

// =================================================
// FREQUENCY MEASUREMENT
// =================================================

volatile unsigned long lastZeroMicros = 0;

// =================================================
// ZERO CROSS ISR
// =================================================

void zeroCrossISR()
{
  unsigned long now = micros();

  if (lastZeroMicros > 0)
  {
    unsigned long halfCycle = now - lastZeroMicros;

    if (halfCycle > 5000 && halfCycle < 15000)
    {
      stsFrequencyHz = calculateFrequencyHz(halfCycle);
    }
  }

  lastZeroMicros = now;

  zeroCross = true;
  counter = 0;

  digitalWrite(TRIAC_PIN, LOW);
}

// =================================================
// TIMER1 ISR
// Runs every 100us
// =================================================

ISR(TIMER1_COMPA_vect)
{
  if (triacPulseActive)
  {
    pulseCounter++;

    if (pulseCounter >= 2)
    {
      digitalWrite(TRIAC_PIN, LOW);

      triacPulseActive = false;
      pulseCounter = 0;
    }
  }

  if (zeroCross)
  {
    if (counter >= pulseDelay)
    {
      digitalWrite(TRIAC_PIN, HIGH);

      triacPulseActive = true;
      pulseCounter = 0;

      zeroCross = false;
    }
    else
    {
      counter++;
    }
  }
}

// =================================================
// LCD SCREEN
// =================================================

void displayMainScreen(
  float frequency,
  float phaseAngle,
  float rmsVoltage,
  uint8_t pulseDelay)
{
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print("PD:");
  lcd.print(pulseDelay);
  lcd.print(" F:");
  lcd.print(frequency, 1);

  lcd.setCursor(0, 1);
  lcd.print("P:");
  lcd.print(phaseAngle, 0);
  lcd.print(" Vrms:");
  lcd.print(rmsVoltage, 1);
}

// =================================================
// SETUP
// =================================================

void setup()
{
  pinMode(TRIAC_PIN, OUTPUT);
  pinMode(ZERO_CROSS_PIN, INPUT);
  pinMode(POT_PIN, INPUT);

  digitalWrite(TRIAC_PIN, LOW);

  lcd.begin(16, 2);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Tivana STS V1");
  delay(1000);

  displayMainScreen(
    stsFrequencyHz,
    stsPhaseAngleDeg,
    stsRmsVoltage,
    pulseDelay);

  attachInterrupt(
    digitalPinToInterrupt(ZERO_CROSS_PIN),
    zeroCrossISR,
    RISING);

  // =================================================
  // TIMER1 -> 100us
  // =================================================

  cli();

  TCCR1A = 0;
  TCCR1B = 0;

  TCNT1 = 0;

  // 16MHz / 8 = 2MHz
  // 100us = 200 counts
  OCR1A = 199;

  TCCR1B |= (1 << WGM12);
  TCCR1B |= (1 << CS11);

  TIMSK1 |= (1 << OCIE1A);

  sei();
}

// =================================================
// LOOP
// =================================================

void loop()
{
  potValue = analogRead(POT_PIN);

  pulseDelay = calculatePulseDelay(potValue);

  stsPhaseAngleDeg = calculatePhaseAngleDeg(pulseDelay);
  stsRmsVoltage = calculateRmsVoltage();

  if (millis() - lastLcdUpdateMs >= 500)
  {
    lastLcdUpdateMs = millis();

    displayMainScreen(
      stsFrequencyHz,
      stsPhaseAngleDeg,
      stsRmsVoltage,
      pulseDelay);
  }
}