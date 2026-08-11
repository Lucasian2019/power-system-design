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

const float DIVIDER_RATIO =
    R_BOTTOM / (R_TOP + R_BOTTOM);

const float TRANSFORMER_RATIO =
    PRIMARY_VOLTAGE / SECONDARY_VOLTAGE;

// =====================================================
// TRANSFER TIMING
// =====================================================

#define B_RECOVERY_DELAY 5000UL
#define SOURCE_FAILURE_DELAY 500UL
#define MIN_TRANSFER_INTERVAL 3000UL

// =====================================================
// FREQUENCY MEASUREMENT
// =====================================================

// Expected zero-crossing interval at 50 Hz:
// 10 ms = 10000 us
//
// We reject measurements outside this range.

#define MIN_VALID_PERIOD_US 8000UL
#define MAX_VALID_PERIOD_US 12000UL

// Number of periods used for averaging
#define FREQUENCY_SAMPLES 8

// =====================================================
// ZCD VARIABLES
// =====================================================

volatile unsigned long lastEdgeA = 0;
volatile unsigned long lastEdgeB = 0;

volatile unsigned long periodA = 0;
volatile unsigned long periodB = 0;

volatile unsigned long zcA_time = 0;
volatile unsigned long zcB_time = 0;

// Flags indicate that a NEW valid period was measured
volatile bool newPeriodA = false;
volatile bool newPeriodB = false;

// =====================================================
// FREQUENCY FILTER BUFFERS
// =====================================================

float periodBufferA[FREQUENCY_SAMPLES];
float periodBufferB[FREQUENCY_SAMPLES];

byte periodIndexA = 0;
byte periodIndexB = 0;

byte validSamplesA = 0;
byte validSamplesB = 0;

float freqA = 0.0;
float freqB = 0.0;

// =====================================================
// VOLTAGE
// =====================================================

float voltageA = 0.0;
float voltageB = 0.0;

// =====================================================
// PHASE
// =====================================================

float phaseAngle = 999.0;
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

        if (p >= MIN_VALID_PERIOD_US &&
            p <= MAX_VALID_PERIOD_US)
        {
            periodA = p;
            newPeriodA = true;
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

        if (p >= MIN_VALID_PERIOD_US &&
            p <= MAX_VALID_PERIOD_US)
        {
            periodB = p;
            newPeriodB = true;
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

    if (t == 0)
        return false;

    if ((unsigned long)(now - t) > 60000UL)
        return false;

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

    if (t == 0)
        return false;

    if ((unsigned long)(now - t) > 60000UL)
        return false;

    return true;
}

// =====================================================
// CALCULATE AVERAGE PERIOD
// =====================================================

float getAveragePeriodA()
{
    if (validSamplesA == 0)
        return 0.0;

    float sum = 0.0;

    for (byte i = 0; i < validSamplesA; i++)
    {
        sum += periodBufferA[i];
    }

    return sum / validSamplesA;
}

// =====================================================

float getAveragePeriodB()
{
    if (validSamplesB == 0)
        return 0.0;

    float sum = 0.0;

    for (byte i = 0; i < validSamplesB; i++)
    {
        sum += periodBufferB[i];
    }

    return sum / validSamplesB;
}

// =====================================================
// UPDATE FREQUENCY
// =====================================================

void updateFrequency()
{
    unsigned long pA;
    unsigned long pB;

    bool updateA;
    bool updateB;

    // -------------------------------------------------
    // Copy ISR data safely
    // -------------------------------------------------

    noInterrupts();

    pA = periodA;
    pB = periodB;

    updateA = newPeriodA;
    updateB = newPeriodB;

    newPeriodA = false;
    newPeriodB = false;

    interrupts();

    // =================================================
    // SOURCE A
    // =================================================

    if (updateA &&
        pA >= MIN_VALID_PERIOD_US &&
        pA <= MAX_VALID_PERIOD_US)
    {
        periodBufferA[periodIndexA] = (float)pA;

        periodIndexA++;

        if (periodIndexA >= FREQUENCY_SAMPLES)
            periodIndexA = 0;

        if (validSamplesA < FREQUENCY_SAMPLES)
            validSamplesA++;

        float averagePeriod = getAveragePeriodA();

        if (averagePeriod > 0)
        {
            // One ZCD edge every half-cycle
            freqA = 1000000.0 /
                    (averagePeriod * 2.0);
        }
    }

    // =================================================
    // SOURCE B
    // =================================================

    if (updateB &&
        pB >= MIN_VALID_PERIOD_US &&
        pB <= MAX_VALID_PERIOD_US)
    {
        periodBufferB[periodIndexB] = (float)pB;

        periodIndexB++;

        if (periodIndexB >= FREQUENCY_SAMPLES)
            periodIndexB = 0;

        if (validSamplesB < FREQUENCY_SAMPLES)
            validSamplesB++;

        float averagePeriod = getAveragePeriodB();

        if (averagePeriod > 0)
        {
            // One ZCD edge every half-cycle
            freqB = 1000000.0 /
                    (averagePeriod * 2.0);
        }
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
        return 999.0;

    if (pA < MIN_VALID_PERIOD_US ||
        pA > MAX_VALID_PERIOD_US ||
        pB < MIN_VALID_PERIOD_US ||
        pB > MAX_VALID_PERIOD_US)
    {
        return 999.0;
    }

    // -------------------------------------------------
    // Average zero-crossing period
    // -------------------------------------------------

    float averagePeriod =
        ((float)pA + (float)pB) / 2.0;

    // -------------------------------------------------
    // Difference between zero crossings
    // -------------------------------------------------

    long difference =
        (long)tA - (long)tB;

    // -------------------------------------------------
    // Wrap into +/- half period
    // -------------------------------------------------

    while (difference > averagePeriod / 2.0)
    {
        difference -= (long)averagePeriod;
    }

    while (difference < -averagePeriod / 2.0)
    {
        difference += (long)averagePeriod;
    }

    // -------------------------------------------------
    // Convert time difference to degrees
    // -------------------------------------------------

    float phase =
        ((float)difference /
         averagePeriod) * 360.0;

    while (phase > 180.0)
        phase -= 360.0;

    while (phase < -180.0)
        phase += 360.0;

    return phase;
}

// =====================================================
// PHASE CALIBRATION
// =====================================================

void calibratePhase()
{
    if (calibrated)
        return;

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

    if (phaseSamples > 20)
    {
        phaseOffset =
            phaseAccumulator /
            (float)phaseSamples;
    }
    else
    {
        phaseOffset = 0.0;
    }

    calibrated = true;
}

// =====================================================
// CALCULATE PHASE
// =====================================================

float calculatePhase()
{
    float p = getRawPhase();

    if (p > 900.0)
        return 999.0;

    p -= phaseOffset;

    while (p > 180.0)
        p -= 360.0;

    while (p < -180.0)
        p += 360.0;

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

        float volts =
            adc * 5.0 / 1023.0;

        float centered =
            volts - 2.5;

        sumSq += centered * centered;
    }

    float rmsADC =
        sqrt(sumSq / samples);

    float secondaryRMS =
        rmsADC / DIVIDER_RATIO;

    float primaryRMS =
        secondaryRMS * TRANSFORMER_RATIO;

    return primaryRMS;
}

// =====================================================
// VOLTAGE CHECK A
// =====================================================

bool voltageAOK()
{
    if (voltageA < 1.0)
        return false;

    float deviation =
        fabs(voltageA - NOMINAL_VOLTAGE) /
        NOMINAL_VOLTAGE * 100.0;

    return deviation <= MAX_VOLTAGE_DEVIATION;
}

// =====================================================
// VOLTAGE CHECK B
// =====================================================

bool voltageBOK()
{
    if (voltageB < 1.0)
        return false;

    float deviation =
        fabs(voltageB - NOMINAL_VOLTAGE) /
        NOMINAL_VOLTAGE * 100.0;

    return deviation <= MAX_VOLTAGE_DEVIATION;
}

// =====================================================
// FREQUENCY CHECK A
// =====================================================

bool frequencyAOK()
{
    if (!sourceAHasSignal())
        return false;

    if (validSamplesA < FREQUENCY_SAMPLES)
        return false;

    return freqA >= MIN_VALID_FREQ &&
           freqA <= MAX_VALID_FREQ;
}

// =====================================================
// FREQUENCY CHECK B
// =====================================================

bool frequencyBOK()
{
    if (!sourceBHasSignal())
        return false;

    if (validSamplesB < FREQUENCY_SAMPLES)
        return false;

    return freqB >= MIN_VALID_FREQ &&
           freqB <= MAX_VALID_FREQ;
}

// =====================================================
// SOURCE A HEALTH
// =====================================================

bool sourceAOK()
{
    return sourceAHasSignal() &&
           frequencyAOK() &&
           voltageAOK();
}

// =====================================================
// SOURCE B HEALTH
// =====================================================

bool sourceBOK()
{
    return sourceBHasSignal() &&
           frequencyBOK() &&
           voltageBOK();
}

// =====================================================
// UPDATE SYNCHRONIZATION
// =====================================================

void updateSync()
{
    phaseAngle = calculatePhase();

    // -------------------------------------------------
    // Frequency difference
    // -------------------------------------------------

    float frequencyDifference =
        fabs(freqA - freqB);

    // -------------------------------------------------
    // Voltage difference
    // -------------------------------------------------

    float voltageDifference = 0.0;

    float averageVoltage =
        (voltageA + voltageB) / 2.0;

    if (averageVoltage > 1.0)
    {
        voltageDifference =
            fabs(voltageA - voltageB) /
            averageVoltage * 100.0;
    }

    // -------------------------------------------------
    // Individual checks
    // -------------------------------------------------

    bool phaseOK =
        (phaseAngle != 999.0) &&
        (fabs(phaseAngle) <= MAX_PHASE_ERROR);

    bool frequencySamplesOK =
        (validSamplesA >= FREQUENCY_SAMPLES) &&
        (validSamplesB >= FREQUENCY_SAMPLES);

    bool frequencyOK =
        frequencySamplesOK &&
        (frequencyDifference <= MAX_FREQ_ERROR);

    bool voltageDifferenceOK =
        (voltageDifference <=
         MAX_SOURCE_VOLTAGE_DIFFERENCE);

    bool A_OK = sourceAOK();
    bool B_OK = sourceBOK();

    // -------------------------------------------------
    // Final synchronization
    // -------------------------------------------------

    syncOK =
        calibrated &&
        A_OK &&
        B_OK &&
        phaseOK &&
        frequencyOK &&
        voltageDifferenceOK;
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

    if (validSamplesB < FREQUENCY_SAMPLES)
    {
        strcpy(msg, "B FREQ WAIT");
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

    if (validSamplesA < FREQUENCY_SAMPLES)
    {
        strcpy(msg, "A FREQ WAIT");
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

    float frequencyDifference =
        fabs(freqA - freqB);

    if (frequencyDifference > MAX_FREQ_ERROR)
    {
        char number[10];

        dtostrf(frequencyDifference, 4, 2, number);

        strcpy(msg, "DFREQ ");
        strcat(msg, number);

        return;
    }

    float averageVoltage =
        (voltageA + voltageB) / 2.0;

    if (averageVoltage > 1.0)
    {
        float voltageDifference =
            fabs(voltageA - voltageB) /
            averageVoltage * 100.0;

        if (voltageDifference >
            MAX_SOURCE_VOLTAGE_DIFFERENCE)
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

    // =================================================
    // CURRENTLY ON SOURCE B
    // =================================================

    if (activeSource == SOURCE_B)
    {
        bHealthyStart = 0;

        if (!B_OK)
        {
            if (badSourceStart == 0)
            {
                badSourceStart = now;
            }

            if (now - badSourceStart >=
                SOURCE_FAILURE_DELAY)
            {
                digitalWrite(SYNC_OUTPUT, LOW);

                activeSource = SOURCE_A;

                lastTransferTime = now;

                if (A_OK)
                {
                    strcpy(statusMessage,
                           "B FAIL -> A");
                }
                else
                {
                    strcpy(statusMessage,
                           "A+B BAD");
                }

                badSourceStart = 0;
            }
            else
            {
                strcpy(statusMessage,
                       "B CHECK...");
            }
        }
        else
        {
            badSourceStart = 0;

            digitalWrite(SYNC_OUTPUT, HIGH);

            strcpy(statusMessage,
                   "ON B OK");
        }

        return;
    }

    // =================================================
    // CURRENTLY ON SOURCE A
    // =================================================

    if (activeSource == SOURCE_A)
    {
        badSourceStart = 0;

        // =============================================
        // SOURCE A FAILED
        // =============================================

        if (!A_OK)
        {
            if (B_OK && syncOK)
            {
                if (now - lastTransferTime >=
                    MIN_TRANSFER_INTERVAL)
                {
                    digitalWrite(SYNC_OUTPUT, HIGH);

                    activeSource = SOURCE_B;

                    lastTransferTime = now;

                    strcpy(statusMessage,
                           "A BAD -> B");
                }
                else
                {
                    strcpy(statusMessage,
                           "A BAD WAIT");
                }
            }
            else
            {
                digitalWrite(SYNC_OUTPUT, LOW);

                if (!B_OK)
                {
                    strcpy(statusMessage,
                           "A+B BAD");
                }
                else
                {
                    getSyncError(statusMessage);
                }
            }

            return;
        }

        // =============================================
        // SOURCE A HEALTHY
        // =============================================

        if (B_OK)
        {
            if (bHealthyStart == 0)
            {
                bHealthyStart = now;
            }

            if (now - bHealthyStart >=
                B_RECOVERY_DELAY)
            {
                if (syncOK)
                {
                    if (now - lastTransferTime >=
                        MIN_TRANSFER_INTERVAL)
                    {
                        digitalWrite(SYNC_OUTPUT, HIGH);

                        activeSource = SOURCE_B;

                        lastTransferTime = now;

                        strcpy(statusMessage,
                               "TRANSFER B");
                    }
                    else
                    {
                        strcpy(statusMessage,
                               "B READY");
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

                strcpy(statusMessage,
                       "B RECOVERING");
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
// LCD
// =====================================================

void updateLCD()
{
    // =================================================
    // LINE 0 - FREQUENCY
    // =================================================

    lcd.setCursor(0, 0);

    lcd.print("A:");

    if (validSamplesA == 0)
        lcd.print("---.-");
    else
        lcd.print(freqA, 1);

    lcd.print("Hz ");

    lcd.setCursor(10, 0);

    lcd.print("B:");

    if (validSamplesB == 0)
        lcd.print("---.-");
    else
        lcd.print(freqB, 1);

    lcd.print("Hz");

    // =================================================
    // LINE 1 - PHASE + dF
    // =================================================

    lcd.setCursor(0, 1);

    lcd.print("PH:");

    if (phaseAngle > 900.0)
        lcd.print("---.-");
    else
        lcd.print(phaseAngle, 1);

    lcd.print((char)223);

    lcd.print(" ");

    lcd.setCursor(11, 1);

    lcd.print("dF:");

    if (validSamplesA == 0 ||
        validSamplesB == 0)
    {
        lcd.print("---");
    }
    else
    {
        lcd.print(fabs(freqA - freqB), 2);
    }

    // =================================================
    // LINE 2 - VOLTAGE
    // =================================================

    lcd.setCursor(0, 2);

    lcd.print("VA:");
    lcd.print(voltageA, 0);
    lcd.print("V ");

    lcd.setCursor(10, 2);

    lcd.print("VB:");
    lcd.print(voltageB, 0);
    lcd.print("V ");

    // =================================================
    // LINE 3 - STATUS
    // =================================================

    lcd.setCursor(0, 3);

    lcd.print("                    ");

    lcd.setCursor(0, 3);

    if (!calibrated)
    {
        lcd.print("CALIBRATING");
    }
    else
    {
        lcd.print(statusMessage);
    }
}

// =====================================================
// SETUP
// =====================================================

void setup()
{
    // =================================================
    // PINS
    // =================================================

    pinMode(ZCD_A_PIN, INPUT);
    pinMode(ZCD_B_PIN, INPUT);

    pinMode(SYNC_OUTPUT, OUTPUT);

    digitalWrite(SYNC_OUTPUT, LOW);

    // =================================================
    // LCD
    // =================================================

    lcd.begin(20, 4);

    lcd.clear();

    lcd.setCursor(0, 0);
    lcd.print("STS Controller");

    lcd.setCursor(0, 1);
    lcd.print("Starting...");

    // =================================================
    // FREQUENCY BUFFERS
    // =================================================

    for (byte i = 0;
         i < FREQUENCY_SAMPLES;
         i++)
    {
        periodBufferA[i] = 0.0;
        periodBufferB[i] = 0.0;
    }

    periodIndexA = 0;
    periodIndexB = 0;

    validSamplesA = 0;
    validSamplesB = 0;

    freqA = 0.0;
    freqB = 0.0;

    // =================================================
    // SOURCE
    // =================================================

    activeSource = SOURCE_A;

    // =================================================
    // INTERRUPTS
    // =================================================

    attachInterrupt(
        digitalPinToInterrupt(ZCD_A_PIN),
        zcdA_ISR,
        RISING
    );

    attachInterrupt(
        digitalPinToInterrupt(ZCD_B_PIN),
        zcdB_ISR,
        RISING
    );

    // =================================================
    // PHASE CALIBRATION
    // =================================================

    calibrationStart = millis();

    delay(1000);
}

// =====================================================
// LOOP
// =====================================================

void loop()
{
    // =================================================
    // FREQUENCY
    // =================================================

    updateFrequency();

    // =================================================
    // VOLTAGE
    // =================================================

    voltageA =
        filterValue(
            voltageA,
            measureVoltage(RMS_A_PIN)
        );

    voltageB =
        filterValue(
            voltageB,
            measureVoltage(RMS_B_PIN)
        );

    // =================================================
    // PHASE CALIBRATION
    // =================================================

    calibratePhase();

    // =================================================
    // SYNCHRONIZATION
    // =================================================

    if (calibrated)
    {
        updateSync();

        manageTransfer();
    }

    // =================================================
    // LCD
    // =================================================

    if (millis() - lastLCD > 400UL)
    {
        lastLCD = millis();

        updateLCD();
    }
}
