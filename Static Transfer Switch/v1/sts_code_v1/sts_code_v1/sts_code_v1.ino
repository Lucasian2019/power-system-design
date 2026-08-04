#include <LiquidCrystal.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <math.h>


// =================================================
// LCD 16x4
// =================================================

const int rs = 3;
const int en = 10;
const int d4 = 4;
const int d5 = 5;
const int d6 = 6;
const int d7 = 7;

LiquidCrystal lcd(rs, en, d4, d5, d6, d7);


// =================================================
// PIN DEFINITIONS
// =================================================

#define ZERO_CROSS_PIN 2      // INT0 ONLY
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
// MEASUREMENTS
// =================================================

constexpr float ADC_REFERENCE_VOLTAGE = 5.0;

constexpr int ADC_MAX = 1023;

constexpr float RMS_VOLTAGE_DIVIDER_RATIO = 11.0;



volatile float frequencyHz = 50.0;

float phaseAngle = 0;

float rmsVoltage = 230;



volatile unsigned long lastZeroMicros = 0;

volatile unsigned long measuredHalfCycle = 10000;



unsigned long lastLcdUpdate = 0;



// =================================================
// CALCULATIONS
// =================================================


uint8_t calculatePulseDelay(int adc)
{

  return map(
          adc,
          0,
          1023,
          0,
          90);

}



float calculatePhaseAngle(uint8_t delay)
{

  float delayTime =
      delay * 100.0;


  return
   (delayTime /
    measuredHalfCycle)
    * 180.0;

}



float calculateFrequency(unsigned long halfCycle)
{

  if(halfCycle > 5000 &&
     halfCycle < 15000)
  {

    return
    1000000.0 /
    (halfCycle * 2.0);

  }


  return frequencyHz;

}




float calculateRMS()
{

  const int samples = 100;


  long sum = 0;

  long sumSquare = 0;



  for(int i=0;i<samples;i++)
  {

    int adc =
       analogRead(RMS_PIN);


    sum += adc;


    sumSquare +=
      (long)adc * adc;

  }



  float average =
      sum /
      (float)samples;



  float meanSquare =
      sumSquare /
      (float)samples;



  float variance =
      meanSquare -
      average * average;



  if(variance < 0)
      variance = 0;



  float rmsADC =
      sqrt(variance);



  float voltage =
      rmsADC *
      ADC_REFERENCE_VOLTAGE /
      ADC_MAX;



  return voltage *
         RMS_VOLTAGE_DIVIDER_RATIO;

}

// =================================================
// ZERO CROSS INTERRUPT
// =================================================

void zeroCrossISR()
{

  unsigned long now =
      micros();

  if(lastZeroMicros > 0)
  {

    unsigned long halfCycle =
        now -
        lastZeroMicros;


    if(halfCycle > 5000 &&
       halfCycle < 15000)
    {

      measuredHalfCycle =
           halfCycle;


      frequencyHz =
           calculateFrequency(
             halfCycle);

    }

  }

  lastZeroMicros = now;

  zeroCross = true;

  counter = 0;


  digitalWrite(
      TRIAC_PIN,
      LOW);

}

// =================================================
// TIMER1 INTERRUPT
// 100us
// =================================================

ISR(TIMER1_COMPA_vect)
{


  // TRIAC pulse OFF

  if(triacPulseActive)
  {

    pulseCounter++;


    if(pulseCounter >= 2)
    {

      digitalWrite(
        TRIAC_PIN,
        LOW);


      triacPulseActive = false;


      pulseCounter = 0;

    }

  }

  // Delay counting

  if(zeroCross)
  {

    if(counter >= pulseDelay)
    {

      digitalWrite(
        TRIAC_PIN,
        HIGH);

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
// LCD DISPLAY
// =================================================

void displayMainScreen()
{

  lcd.clear();

  // LINE 1

  lcd.setCursor(0,0);

  lcd.print("PD:");

  lcd.print(pulseDelay);

  lcd.print("us");

  lcd.setCursor(9,0);

  lcd.print("F:");

  lcd.print(frequencyHz,1);

  lcd.print("Hz");

  // LINE 2

  lcd.setCursor(0,1);

  lcd.print("P:");

  lcd.print(phaseAngle,0);

  lcd.print("deg");
  
  lcd.setCursor(9,1);

  lcd.print("Vr:");

  lcd.print(rmsVoltage,0);

  lcd.print("V");

  // LINE 3

  lcd.setCursor(0,2);

  lcd.print("GRID: SYNC OK");

  // LINE 4

  lcd.setCursor(0,3);

  lcd.print("STS CONTROLLER");

}

// =================================================
// SETUP
// =================================================

void setup()
{

  pinMode(TRIAC_PIN,OUTPUT);

  pinMode(ZERO_CROSS_PIN,INPUT);

  pinMode(POT_PIN,INPUT);

  pinMode(RMS_PIN,INPUT);

  digitalWrite(
    TRIAC_PIN,
    LOW);

  lcd.begin(16,4);

  lcd.clear();

  lcd.setCursor(0,0);

  lcd.print("Tivana STS V1");

  delay(1000);

  attachInterrupt(
    digitalPinToInterrupt(ZERO_CROSS_PIN),
    zeroCrossISR,
    RISING);

  // TIMER1 100us

  cli();

  TCCR1A = 0;

  TCCR1B = 0;

  TCNT1 = 0;

  OCR1A = 199;

  TCCR1B |=
     (1<<WGM12);

  TCCR1B |=
     (1<<CS11);

  TIMSK1 |=
     (1<<OCIE1A);

  sei();

}


// =================================================
// LOOP
// =================================================

void loop()
{

  potValue =
      analogRead(POT_PIN);

  pulseDelay =
      calculatePulseDelay(
          potValue);

  phaseAngle =
      calculatePhaseAngle(
          pulseDelay);

  rmsVoltage =
      calculateRMS();


  if(millis()-lastLcdUpdate >= 500)
  {

    lastLcdUpdate =
        millis();


    displayMainScreen();

  }


}
