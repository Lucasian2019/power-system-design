#include <Arduino.h>
#include <LiquidCrystal.h>
#include <Preferences.h>
#include <esp_arduino_version.h>

const int rs = 3, en = 2, d4 = 4, d5 = 5, d6 = 6, d7 = 7;
constexpr uint8_t BTN_OK = 9, BTN_BACK = 8, BTN_UP = 1, BTN_DOWN = 0;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

enum BatteryType { LITHIUM, GEL, LEAD_ACID };
struct BatteryProfile {
  const char *name;
  float bulkVoltage;
  float floatVoltage;
  float maxCurrent;
};

BatteryProfile batteryProfiles[] = {
    {"Lithium", 14.4F, 13.6F, 20.0F},
    {"Gel", 14.1F, 13.8F, 15.0F},
    {"Lead Acid", 14.7F, 13.5F, 25.0F},
};

constexpr int menuCount = 6;
const char *const menuItems[menuCount] = {
    "Home", "MPPT Status", "Battery Type", "Power Supply", "System Limits", "System Info"};

enum ScreenState {
  SCREEN_HOME, SCREEN_MENU, SCREEN_MPPT, SCREEN_BATTERY_LIST,
  SCREEN_BATTERY_PROFILE, SCREEN_EDIT_PROFILE, SCREEN_POWER_SUPPLY,
  SCREEN_EDIT_VOLTAGE, SCREEN_EDIT_CURRENT, SCREEN_PSU_ACTIVE, SCREEN_LIMITS,
  SCREEN_EDIT_LIMIT, SCREEN_INFO
};

ScreenState screenState = SCREEN_HOME;
BatteryType activeBattery = LITHIUM;
int menuIndex = 0, menuTop = 0, subMenuIndex = 0, profileField = 0, limitField = 0;
float psuVoltage = 12.0F, psuCurrentLimit = 5.0F, editValue = 0.0F;
bool psuActive = false;
bool returnToPsuActive = false;
bool lastUp = HIGH, lastDown = HIGH, lastOK = HIGH, lastBack = HIGH;

// -----------------------------------------------------------------------------
// Hardware configuration. Change these values to match the actual PCB.
// PWM remains OFF at boot and whenever a fault is present.
// -----------------------------------------------------------------------------
constexpr uint8_t PIN_PWM = 15;
constexpr uint8_t PWM_CHANNEL = 0;
constexpr uint8_t PIN_PV_VOLTAGE = 10;
constexpr uint8_t PIN_PV_CURRENT = 11;
constexpr uint8_t PIN_BATTERY_VOLTAGE = 12;
constexpr uint8_t PIN_BATTERY_CURRENT = 13;
constexpr uint8_t PIN_DC_BUS_VOLTAGE = 14;
constexpr uint8_t PIN_TEMPERATURE = 16;
constexpr uint32_t PWM_FREQUENCY = 25000;
constexpr uint8_t PWM_RESOLUTION = 12;
constexpr uint32_t PWM_MAX_COUNT = (1U << PWM_RESOLUTION) - 1U;

struct SystemConfig {
  float pvMaximumVoltage = 60.0F;
  float batteryMinimumVoltage = 9.0F;
  float batteryMaximumVoltage = 16.0F;
  float maximumChargeCurrent = 30.0F;
  float maximumTemperature = 75.0F;
  float pvVoltageDividerRatio = 21.0F;
  float batteryVoltageDividerRatio = 6.0F;
  float dcBusVoltageDividerRatio = 21.0F;
  // ACS758-050B is typically 40 mV/A. Use 0.020 for ACS758-100B.
  float currentSensorSensitivity = 0.040F;
  float currentSensorZeroVoltage = 2.50F;
  // Set to 1.0 when the sensor output is already limited to 3.3 V.
  float currentSensorInputDividerRatio = 1.0F;
  // LM35 default: 10 mV/degC, 0 V at 0 degC.
  float temperatureVoltsPerDegree = 0.010F;
};

SystemConfig config;

struct Measurements {
  float pvVoltage = 0.0F;
  float pvCurrent = 0.0F;
  float batteryVoltage = 0.0F;
  float batteryCurrent = 0.0F;
  float dcBusVoltage = 0.0F;
  float temperatureC = 0.0F;
  float pvPower = 0.0F;
};

Measurements measurements;
Preferences preferences;

enum ChargeStage { CHARGE_IDLE, CHARGE_BULK_MPPT, CHARGE_ABSORPTION, CHARGE_FLOAT, CHARGE_PSU, CHARGE_FAULT };
enum FaultCode { FAULT_NONE, FAULT_PV_OVERVOLTAGE, FAULT_BATTERY_OVERVOLTAGE,
                 FAULT_OVERCURRENT, FAULT_OVERTEMPERATURE, FAULT_NO_BATTERY };

ChargeStage chargeStage = CHARGE_IDLE;
FaultCode faultCode = FAULT_NONE;
float pwmDuty = 0.0F;
float mpptDuty = 0.0F;
int mpptDirection = 1;
float previousPvPower = 0.0F;
uint32_t lastMeasurementMs = 0;
uint32_t lastControlMs = 0;
uint32_t lastMpptMs = 0;
uint32_t absorptionStartedMs = 0;
uint32_t lastDiagnosticsMs = 0;
constexpr uint32_t ABSORPTION_TIME_MS = 30UL * 60UL * 1000UL;

// MPPT control code should use these values rather than hard-coded settings.
const BatteryProfile &getActiveBatteryProfile() { return batteryProfiles[activeBattery]; }
bool isPsuModeActive() { return psuActive; }
float getChargeVoltageTarget(bool floatStage) {
  return psuActive ? psuVoltage
                   : (floatStage ? getActiveBatteryProfile().floatVoltage
                                 : getActiveBatteryProfile().bulkVoltage);
}
float getChargeCurrentTarget() {
  return psuActive ? psuCurrentLimit : getActiveBatteryProfile().maxCurrent;
}
float calculateDutyCycle(float pvVoltage, bool floatStage) {
  if (pvVoltage <= 0.1F) return 0.0F;
  return constrain(getChargeVoltageTarget(floatStage) / pvVoltage, 0.0F, 0.95F);
}

// -----------------------------------------------------------------------------
// Measurements. All voltage-divider ratios and sensor scales are configurable
// above. Calibrate them with a trusted multimeter before enabling PWM.
// -----------------------------------------------------------------------------
float readAdcVolts(uint8_t pin) {
  const int samples = 16;
  uint32_t totalMilliVolts = 0;
  for (int i = 0; i < samples; ++i) totalMilliVolts += analogReadMilliVolts(pin);
  return (totalMilliVolts / static_cast<float>(samples)) / 1000.0F;
}

float readPvVoltage() { return readAdcVolts(PIN_PV_VOLTAGE) * config.pvVoltageDividerRatio; }
float readBatteryVoltage() { return readAdcVolts(PIN_BATTERY_VOLTAGE) * config.batteryVoltageDividerRatio; }
float readDcBusVoltage() { return readAdcVolts(PIN_DC_BUS_VOLTAGE) * config.dcBusVoltageDividerRatio; }
float readCurrent(uint8_t pin) {
  const float sensorVoltage = readAdcVolts(pin) * config.currentSensorInputDividerRatio;
  return (sensorVoltage - config.currentSensorZeroVoltage) / config.currentSensorSensitivity;
}
float readPvCurrent() { return readCurrent(PIN_PV_CURRENT); }
float readBatteryCurrent() { return readCurrent(PIN_BATTERY_CURRENT); }
float readTemperatureC() { return readAdcVolts(PIN_TEMPERATURE) / config.temperatureVoltsPerDegree; }

void updateMeasurements() {
  if (millis() - lastMeasurementMs < 100) return;
  lastMeasurementMs = millis();
  measurements.pvVoltage = readPvVoltage();
  measurements.pvCurrent = max(0.0F, readPvCurrent());
  measurements.batteryVoltage = readBatteryVoltage();
  measurements.batteryCurrent = max(0.0F, readBatteryCurrent());
  measurements.dcBusVoltage = readDcBusVoltage();
  measurements.temperatureC = readTemperatureC();
  measurements.pvPower = measurements.pvVoltage * measurements.pvCurrent;
}

// -----------------------------------------------------------------------------
// PWM and protection. This is the only function that writes to the gate driver.
// -----------------------------------------------------------------------------
void setPwmDuty(float duty) {
  pwmDuty = constrain(duty, 0.0F, 0.95F);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(PIN_PWM, static_cast<uint32_t>(pwmDuty * PWM_MAX_COUNT));
#else
  ledcWrite(PWM_CHANNEL, static_cast<uint32_t>(pwmDuty * PWM_MAX_COUNT));
#endif
}

void disablePwm() { setPwmDuty(0.0F); }

const char *faultText() {
  switch (faultCode) {
    case FAULT_PV_OVERVOLTAGE: return "PV OVERVOLT";
    case FAULT_BATTERY_OVERVOLTAGE: return "BAT OVERVOLT";
    case FAULT_OVERCURRENT: return "OVERCURRENT";
    case FAULT_OVERTEMPERATURE: return "OVERTEMP";
    case FAULT_NO_BATTERY: return "NO BATTERY";
    default: return "NONE";
  }
}

void updateSafety() {
  if (measurements.pvVoltage > config.pvMaximumVoltage) faultCode = FAULT_PV_OVERVOLTAGE;
  else if (measurements.batteryVoltage > config.batteryMaximumVoltage) faultCode = FAULT_BATTERY_OVERVOLTAGE;
  else if (measurements.batteryCurrent > config.maximumChargeCurrent) faultCode = FAULT_OVERCURRENT;
  else if (measurements.temperatureC > config.maximumTemperature) faultCode = FAULT_OVERTEMPERATURE;
  else if (measurements.batteryVoltage < 1.0F && measurements.pvVoltage > 5.0F) faultCode = FAULT_NO_BATTERY;

  if (faultCode != FAULT_NONE) {
    chargeStage = CHARGE_FAULT;
    disablePwm();
  }
}

// Call only after the source and load measurements are validated.
void clearFault() {
  faultCode = FAULT_NONE;
  chargeStage = CHARGE_IDLE;
  disablePwm();
}

// -----------------------------------------------------------------------------
// MPPT and CC/CV control.
// -----------------------------------------------------------------------------
void runMpptAlgorithm() {
  if (millis() - lastMpptMs < 250) return;
  lastMpptMs = millis();
  if (measurements.pvPower < previousPvPower) mpptDirection = -mpptDirection;
  mpptDuty = constrain(mpptDuty + mpptDirection * 0.005F, 0.02F, 0.90F);
  previousPvPower = measurements.pvPower;
}

void runConstantVoltageCurrent(float targetVoltage, float targetCurrent, float baseDuty) {
  float duty = baseDuty;
  // Current limiting has priority over voltage regulation.
  if (measurements.batteryCurrent > targetCurrent) duty -= 0.015F;
  else if (measurements.batteryVoltage > targetVoltage) duty -= 0.010F;
  else if (measurements.batteryVoltage < targetVoltage - 0.05F &&
           measurements.batteryCurrent < targetCurrent - 0.10F) duty += 0.005F;
  setPwmDuty(duty);
}

void runPsuMode() {
  chargeStage = CHARGE_PSU;
  runConstantVoltageCurrent(psuVoltage, psuCurrentLimit, pwmDuty);
}

void runChargingStateMachine() {
  const BatteryProfile &profile = getActiveBatteryProfile();
  if (measurements.pvVoltage < measurements.batteryVoltage + 1.0F) {
    chargeStage = CHARGE_IDLE;
    disablePwm();
    return;
  }

  if (chargeStage == CHARGE_IDLE) {
    chargeStage = CHARGE_BULK_MPPT;
    mpptDuty = calculateDutyCycle(measurements.pvVoltage, false);
  }

  if (chargeStage == CHARGE_BULK_MPPT) {
    runMpptAlgorithm();
    runConstantVoltageCurrent(profile.bulkVoltage,
                              min(profile.maxCurrent, config.maximumChargeCurrent), mpptDuty);
    if (measurements.batteryVoltage >= profile.bulkVoltage - 0.05F) {
      chargeStage = CHARGE_ABSORPTION;
      absorptionStartedMs = millis();
    }
  } else if (chargeStage == CHARGE_ABSORPTION) {
    runConstantVoltageCurrent(profile.bulkVoltage,
                              min(profile.maxCurrent, config.maximumChargeCurrent), pwmDuty);
    if (millis() - absorptionStartedMs >= ABSORPTION_TIME_MS ||
        measurements.batteryCurrent < profile.maxCurrent * 0.10F) chargeStage = CHARGE_FLOAT;
  } else if (chargeStage == CHARGE_FLOAT) {
    runConstantVoltageCurrent(profile.floatVoltage,
                              min(profile.maxCurrent, config.maximumChargeCurrent), pwmDuty);
    if (measurements.batteryVoltage < profile.floatVoltage - 0.40F) chargeStage = CHARGE_BULK_MPPT;
  }
}

void updatePowerControl() {
  if (millis() - lastControlMs < 50 || faultCode != FAULT_NONE) return;
  lastControlMs = millis();
  if (psuActive) runPsuMode();
  else runChargingStateMachine();
}

struct StoredSettings {
  SystemConfig limits;
  float bulkVoltage[3];
  float floatVoltage[3];
  float maxCurrent[3];
  float savedPsuVoltage;
  float savedPsuCurrent;
};

void saveSettings() {
  StoredSettings settings;
  settings.limits = config;
  for (int i = 0; i < 3; ++i) {
    settings.bulkVoltage[i] = batteryProfiles[i].bulkVoltage;
    settings.floatVoltage[i] = batteryProfiles[i].floatVoltage;
    settings.maxCurrent[i] = batteryProfiles[i].maxCurrent;
  }
  settings.savedPsuVoltage = psuVoltage;
  settings.savedPsuCurrent = psuCurrentLimit;
  preferences.putBytes("settings", &settings, sizeof(settings));
}

void loadSettings() {
  StoredSettings settings;
  if (preferences.getBytesLength("settings") != sizeof(settings)) return;
  preferences.getBytes("settings", &settings, sizeof(settings));
  config = settings.limits;
  for (int i = 0; i < 3; ++i) {
    batteryProfiles[i].bulkVoltage = settings.bulkVoltage[i];
    batteryProfiles[i].floatVoltage = settings.floatVoltage[i];
    batteryProfiles[i].maxCurrent = settings.maxCurrent[i];
  }
  psuVoltage = settings.savedPsuVoltage;
  psuCurrentLimit = settings.savedPsuCurrent;
}

const char *chargeStageText() {
  switch (chargeStage) {
    case CHARGE_BULK_MPPT: return "BULK MPPT";
    case CHARGE_ABSORPTION: return "ABSORPTION";
    case CHARGE_FLOAT: return "FLOAT";
    case CHARGE_PSU: return "PSU CC/CV";
    case CHARGE_FAULT: return "FAULT";
    default: return "IDLE";
  }
}

void showHome() {
  lcd.clear();
  lcd.setCursor(0, 0);
  if (faultCode != FAULT_NONE) { lcd.print("FAULT: "); lcd.print(faultText()); }
  else lcd.print(chargeStageText());
  lcd.setCursor(0, 1); lcd.print("PV:"); lcd.print(measurements.pvVoltage, 1);
  lcd.print("V "); lcd.print(measurements.pvCurrent, 1); lcd.print("A");
  lcd.setCursor(0, 2); lcd.print("BAT:"); lcd.print(measurements.batteryVoltage, 1);
  lcd.print("V "); lcd.print(measurements.batteryCurrent, 1); lcd.print("A");
  lcd.setCursor(0, 3); lcd.print(faultCode == FAULT_NONE ? "OK=Menu" : "BACK=Reset fault");
}

void drawMenu() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("MAIN MENU");
  for (int row = 0; row < 3; ++row) {
    int item = menuTop + row;
    if (item >= menuCount) break;
    lcd.setCursor(0, row + 1);
    lcd.print(item == menuIndex ? "> " : "  ");
    lcd.print(menuItems[item]);
  }
}

void showMpptStatus() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(psuActive ? "PSU: CONSTANT V/I" : "MPPT STATUS");
  lcd.setCursor(0, 1); lcd.print("PV P:"); lcd.print(measurements.pvPower, 0); lcd.print("W D:");
  lcd.print(pwmDuty * 100.0F, 0); lcd.print("%");
  lcd.setCursor(0, 2); lcd.print("Target:"); lcd.print(getChargeVoltageTarget(false), 1);
  lcd.print("V "); lcd.print(getChargeCurrentTarget(), 1); lcd.print("A");
  lcd.setCursor(0, 3); lcd.print("T:"); lcd.print(measurements.temperatureC, 0); lcd.print("C BACK=Menu");
}

void showBatteryList() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("BATTERY TYPE");
  for (int i = 0; i < 3; ++i) {
    lcd.setCursor(0, i + 1);
    lcd.print(i == subMenuIndex ? "> " : "  ");
    lcd.print(batteryProfiles[i].name);
    if (i == activeBattery) lcd.print(" *");
  }
}

void showBatteryProfile() {
  const BatteryProfile &profile = batteryProfiles[subMenuIndex];
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(profile.name); lcd.print(" SETTINGS");
  lcd.setCursor(0, 1); lcd.print(profileField == 0 ? "> Bulk Voltage" : "  Bulk Voltage");
  lcd.setCursor(0, 2); lcd.print(profileField == 1 ? "> Float Voltage" : "  Float Voltage");
  lcd.setCursor(0, 3); lcd.print(profileField == 2 ? "> Max Current" : "  Max Current");
}

void showProfileEditor() {
  const char *const labels[] = {"SET BULK VOLTAGE", "SET FLOAT VOLTAGE", "SET MAX CURRENT"};
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(labels[profileField]);
  lcd.setCursor(0, 1); lcd.print(editValue, 1); lcd.print(profileField == 2 ? " A" : " V");
  lcd.setCursor(0, 3); lcd.print("UP/DN OK=SAVE");
}

void showPowerSupply() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("POWER SUPPLY");
  lcd.setCursor(0, 1); lcd.print(subMenuIndex == 0 ? "> Output Voltage" : "  Output Voltage");
  lcd.setCursor(0, 2); lcd.print(subMenuIndex == 1 ? "> Current Limit" : "  Current Limit");
  lcd.setCursor(0, 3);
  lcd.print(subMenuIndex == 2 ? "> " : "  ");
  lcd.print(psuActive ? "Activate *" : "Activate");
}

void showPsuActive() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("PSU ACTIVE *");
  lcd.setCursor(0, 1); lcd.print(subMenuIndex == 0 ? "> Voltage: " : "  Voltage: ");
  lcd.print(psuVoltage, 1); lcd.print("V");
  lcd.setCursor(0, 2); lcd.print(subMenuIndex == 1 ? "> Current: " : "  Current: ");
  lcd.print(psuCurrentLimit, 1); lcd.print("A");
  lcd.setCursor(0, 3); lcd.print("OK=Edit BACK=Leave");
}

void showValueEditor(bool voltage) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(voltage ? "SET OUTPUT VOLTAGE" : "SET CURRENT LIMIT");
  lcd.setCursor(0, 1); lcd.print(voltage ? psuVoltage : psuCurrentLimit, 1);
  lcd.print(voltage ? " V" : " A");
  lcd.setCursor(0, 3); lcd.print("UP/DN OK=SAVE");
}

const char *limitName(int field) {
  const char *const names[] = {"PV Maximum", "Battery Minimum", "Battery Maximum", "Charge Current Max", "Temperature Maximum"};
  return names[field];
}

float getLimitValue(int field) {
  switch (field) {
    case 0: return config.pvMaximumVoltage;
    case 1: return config.batteryMinimumVoltage;
    case 2: return config.batteryMaximumVoltage;
    case 3: return config.maximumChargeCurrent;
    default: return config.maximumTemperature;
  }
}

void setLimitValue(int field, float value) {
  switch (field) {
    case 0: config.pvMaximumVoltage = value; break;
    case 1: config.batteryMinimumVoltage = value; break;
    case 2: config.batteryMaximumVoltage = value; break;
    case 3: config.maximumChargeCurrent = value; break;
    default: config.maximumTemperature = value; break;
  }
}

void showLimits() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("SYSTEM LIMITS");
  lcd.setCursor(0, 1); lcd.print("> "); lcd.print(limitName(limitField));
  lcd.setCursor(0, 2); lcd.print(getLimitValue(limitField), 1);
  lcd.print(limitField == 3 ? " A" : limitField == 4 ? " C" : " V");
  lcd.setCursor(0, 3); lcd.print("UP/DN OK=EDIT");
}

void showLimitEditor() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("SET "); lcd.print(limitName(limitField));
  lcd.setCursor(0, 1); lcd.print(editValue, 1);
  lcd.print(limitField == 3 ? " A" : limitField == 4 ? " C" : " V");
  lcd.setCursor(0, 3); lcd.print("UP/DN OK=SAVE");
}

void showSystemInfo() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("SYSTEM INFO");
  lcd.setCursor(0, 1); lcd.print("Tivana MPPT");
  lcd.setCursor(0, 2); lcd.print("Version 1");
  lcd.setCursor(0, 3); lcd.print("BACK=Menu");
}

void openMenuItem() {
  switch (menuIndex) {
    case 0: screenState = SCREEN_HOME; showHome(); break;
    case 1: screenState = SCREEN_MPPT; showMpptStatus(); break;
    case 2: screenState = SCREEN_BATTERY_LIST; subMenuIndex = activeBattery; showBatteryList(); break;
    case 3: screenState = SCREEN_POWER_SUPPLY; subMenuIndex = 0; showPowerSupply(); break;
    case 4: screenState = SCREEN_LIMITS; limitField = 0; showLimits(); break;
    case 5: screenState = SCREEN_INFO; showSystemInfo(); break;
  }
}

// Refresh only live-data pages; menu screens must remain stable for navigation.
void updateDiagnosticsUi() {
  if (millis() - lastDiagnosticsMs < 500) return;
  lastDiagnosticsMs = millis();
  if (screenState == SCREEN_HOME) showHome();
  else if (screenState == SCREEN_MPPT) showMpptStatus();
}

void setup() {
  lcd.begin(20, 4);
  pinMode(BTN_OK, INPUT_PULLUP); pinMode(BTN_BACK, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP); pinMode(BTN_DOWN, INPUT_PULLUP);
  analogReadResolution(12);
  preferences.begin("tivana-mppt", false);
  loadSettings();
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_PWM, PWM_FREQUENCY, PWM_RESOLUTION);
#else
  ledcSetup(PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION);
  ledcAttachPin(PIN_PWM, PWM_CHANNEL);
#endif
  disablePwm();
  lcd.clear(); lcd.setCursor(2, 0); lcd.print("TIVANA SOLAR MPPT");
  lcd.setCursor(1, 2); lcd.print("Initializing..."); delay(1500);
  showHome();
}

void loop() {
  updateMeasurements();
  updateSafety();
  updatePowerControl();
  updateDiagnosticsUi();

  const bool upNow = digitalRead(BTN_UP), downNow = digitalRead(BTN_DOWN);
  const bool okNow = digitalRead(BTN_OK), backNow = digitalRead(BTN_BACK);
  const bool upPressed = lastUp && !upNow, downPressed = lastDown && !downNow;
  const bool okPressed = lastOK && !okNow, backPressed = lastBack && !backNow;

  if (upPressed || downPressed) {
    const int direction = downPressed ? 1 : -1;
    if (screenState == SCREEN_MENU) {
      menuIndex = (menuIndex + direction + menuCount) % menuCount;
      if (menuIndex < menuTop) menuTop = menuIndex;
      if (menuIndex > menuTop + 2) menuTop = menuIndex - 2;
      drawMenu();
    } else if (screenState == SCREEN_BATTERY_LIST) {
      subMenuIndex = (subMenuIndex + direction + 3) % 3; showBatteryList();
    } else if (screenState == SCREEN_BATTERY_PROFILE) {
      profileField = (profileField + direction + 3) % 3; showBatteryProfile();
    } else if (screenState == SCREEN_POWER_SUPPLY) {
      subMenuIndex = (subMenuIndex + direction + 3) % 3; showPowerSupply();
    } else if (screenState == SCREEN_LIMITS) {
      limitField = (limitField + direction + 5) % 5; showLimits();
    } else if (screenState == SCREEN_PSU_ACTIVE) {
      subMenuIndex = (subMenuIndex + direction + 2) % 2; showPsuActive();
    } else if (screenState == SCREEN_EDIT_PROFILE) {
      // UP raises a setting; DOWN lowers it.  Menu navigation uses the
      // opposite numeric direction because DOWN advances the menu index.
      const float adjustment = upPressed ? 0.1F : -0.1F;
      editValue += adjustment;
      editValue = constrain(editValue, 0.0F, profileField == 2 ? 100.0F : 60.0F);
      showProfileEditor();
    } else if (screenState == SCREEN_EDIT_VOLTAGE) {
      const float adjustment = upPressed ? 0.1F : -0.1F;
      psuVoltage = constrain(psuVoltage + adjustment, 0.0F, 60.0F); showValueEditor(true);
    } else if (screenState == SCREEN_EDIT_CURRENT) {
      const float adjustment = upPressed ? 0.1F : -0.1F;
      psuCurrentLimit = constrain(psuCurrentLimit + adjustment, 0.0F, 30.0F); showValueEditor(false);
    } else if (screenState == SCREEN_EDIT_LIMIT) {
      const float adjustment = upPressed ? 0.1F : -0.1F;
      editValue = constrain(editValue + adjustment, 0.0F, limitField == 4 ? 150.0F : 200.0F);
      showLimitEditor();
    }
  }

  if (okPressed) {
    if (screenState == SCREEN_HOME) { screenState = SCREEN_MENU; drawMenu(); }
    else if (screenState == SCREEN_MENU) openMenuItem();
    else if (screenState == SCREEN_BATTERY_LIST) {
      activeBattery = static_cast<BatteryType>(subMenuIndex);
      profileField = 0; screenState = SCREEN_BATTERY_PROFILE; showBatteryProfile();
    } else if (screenState == SCREEN_BATTERY_PROFILE) {
      const BatteryProfile &profile = batteryProfiles[subMenuIndex];
      editValue = profileField == 0 ? profile.bulkVoltage : profileField == 1 ? profile.floatVoltage : profile.maxCurrent;
      screenState = SCREEN_EDIT_PROFILE; showProfileEditor();
    } else if (screenState == SCREEN_EDIT_PROFILE) {
      BatteryProfile &profile = batteryProfiles[subMenuIndex];
      if (profileField == 0) profile.bulkVoltage = editValue;
      else if (profileField == 1) profile.floatVoltage = editValue;
      else profile.maxCurrent = editValue;
      saveSettings();
      screenState = SCREEN_BATTERY_PROFILE; showBatteryProfile();
    } else if (screenState == SCREEN_POWER_SUPPLY) {
      if (subMenuIndex == 0) { returnToPsuActive = false; screenState = SCREEN_EDIT_VOLTAGE; showValueEditor(true); }
      else if (subMenuIndex == 1) { returnToPsuActive = false; screenState = SCREEN_EDIT_CURRENT; showValueEditor(false); }
      else { psuActive = true; subMenuIndex = 0; screenState = SCREEN_PSU_ACTIVE; showPsuActive(); }
    } else if (screenState == SCREEN_PSU_ACTIVE) {
      returnToPsuActive = true;
      screenState = subMenuIndex == 0 ? SCREEN_EDIT_VOLTAGE : SCREEN_EDIT_CURRENT;
      showValueEditor(screenState == SCREEN_EDIT_VOLTAGE);
    } else if (screenState == SCREEN_LIMITS) {
      editValue = getLimitValue(limitField);
      screenState = SCREEN_EDIT_LIMIT; showLimitEditor();
    } else if (screenState == SCREEN_EDIT_LIMIT) {
      setLimitValue(limitField, editValue);
      saveSettings();
      screenState = SCREEN_LIMITS; showLimits();
    } else if (screenState == SCREEN_EDIT_VOLTAGE || screenState == SCREEN_EDIT_CURRENT) {
      saveSettings();
      screenState = returnToPsuActive ? SCREEN_PSU_ACTIVE : SCREEN_POWER_SUPPLY;
      if (returnToPsuActive) showPsuActive(); else showPowerSupply();
    }
  }

  if (backPressed) {
    if (screenState == SCREEN_HOME && faultCode != FAULT_NONE) { clearFault(); showHome(); }
    else if (screenState == SCREEN_HOME) {}
    else if (screenState == SCREEN_MENU) { screenState = SCREEN_HOME; showHome(); }
    else if (screenState == SCREEN_BATTERY_PROFILE) { screenState = SCREEN_BATTERY_LIST; showBatteryList(); }
    else if (screenState == SCREEN_EDIT_PROFILE) { screenState = SCREEN_BATTERY_PROFILE; showBatteryProfile(); }
    else if (screenState == SCREEN_EDIT_VOLTAGE || screenState == SCREEN_EDIT_CURRENT) {
      screenState = returnToPsuActive ? SCREEN_PSU_ACTIVE : SCREEN_POWER_SUPPLY;
      if (returnToPsuActive) showPsuActive(); else showPowerSupply();
    }
    else if (screenState == SCREEN_EDIT_LIMIT) { screenState = SCREEN_LIMITS; showLimits(); }
    else if (screenState == SCREEN_PSU_ACTIVE) {
      psuActive = false; returnToPsuActive = false; screenState = SCREEN_HOME; showHome();
    }
    else { screenState = SCREEN_MENU; drawMenu(); }
  }

  lastUp = upNow; lastDown = downNow; lastOK = okNow; lastBack = backNow;
  delay(20);
}
