#include <Arduino.h>
#include <LiquidCrystal.h>

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

constexpr int menuCount = 5;
const char *const menuItems[menuCount] = {
    "Home", "MPPT Status", "Battery Type", "Power Supply", "System Info"};

enum ScreenState {
  SCREEN_HOME, SCREEN_MENU, SCREEN_MPPT, SCREEN_BATTERY_LIST,
  SCREEN_BATTERY_PROFILE, SCREEN_EDIT_PROFILE, SCREEN_POWER_SUPPLY,
  SCREEN_EDIT_VOLTAGE, SCREEN_EDIT_CURRENT, SCREEN_PSU_ACTIVE, SCREEN_INFO
};

ScreenState screenState = SCREEN_HOME;
BatteryType activeBattery = LITHIUM;
int menuIndex = 0, menuTop = 0, subMenuIndex = 0, profileField = 0;
float psuVoltage = 12.0F, psuCurrentLimit = 5.0F, editValue = 0.0F;
bool psuActive = false;
bool returnToPsuActive = false;
bool lastUp = HIGH, lastDown = HIGH, lastOK = HIGH, lastBack = HIGH;

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

void showHome() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(psuActive ? "HOME: PSU MODE" : "HOME PAGE : BULK");
  lcd.setCursor(0, 1); lcd.print("PV:38.5V I:8A");
  lcd.setCursor(0, 2); lcd.print("BAT:"); lcd.print(getChargeVoltageTarget(false), 1);
  lcd.print("V I:"); lcd.print(getChargeCurrentTarget(), 0); lcd.print("A");
  lcd.setCursor(0, 3); lcd.print("OK=Menu");
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
  lcd.setCursor(0, 1); lcd.print("Target:"); lcd.print(getChargeVoltageTarget(false), 1); lcd.print("V");
  lcd.setCursor(0, 2); lcd.print("Limit :"); lcd.print(getChargeCurrentTarget(), 1); lcd.print("A");
  lcd.setCursor(0, 3); lcd.print("BACK=Menu");
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
    case 4: screenState = SCREEN_INFO; showSystemInfo(); break;
  }
}

void setup() {
  lcd.begin(20, 4);
  pinMode(BTN_OK, INPUT_PULLUP); pinMode(BTN_BACK, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP); pinMode(BTN_DOWN, INPUT_PULLUP);
  lcd.clear(); lcd.setCursor(2, 0); lcd.print("TIVANA SOLAR MPPT");
  lcd.setCursor(1, 2); lcd.print("Initializing..."); delay(1500);
  showHome();
}

void loop() {
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
      screenState = SCREEN_BATTERY_PROFILE; showBatteryProfile();
    } else if (screenState == SCREEN_POWER_SUPPLY) {
      if (subMenuIndex == 0) { returnToPsuActive = false; screenState = SCREEN_EDIT_VOLTAGE; showValueEditor(true); }
      else if (subMenuIndex == 1) { returnToPsuActive = false; screenState = SCREEN_EDIT_CURRENT; showValueEditor(false); }
      else { psuActive = true; subMenuIndex = 0; screenState = SCREEN_PSU_ACTIVE; showPsuActive(); }
    } else if (screenState == SCREEN_PSU_ACTIVE) {
      returnToPsuActive = true;
      screenState = subMenuIndex == 0 ? SCREEN_EDIT_VOLTAGE : SCREEN_EDIT_CURRENT;
      showValueEditor(screenState == SCREEN_EDIT_VOLTAGE);
    } else if (screenState == SCREEN_EDIT_VOLTAGE || screenState == SCREEN_EDIT_CURRENT) {
      screenState = returnToPsuActive ? SCREEN_PSU_ACTIVE : SCREEN_POWER_SUPPLY;
      if (returnToPsuActive) showPsuActive(); else showPowerSupply();
    }
  }

  if (backPressed) {
    if (screenState == SCREEN_HOME) {}
    else if (screenState == SCREEN_MENU) { screenState = SCREEN_HOME; showHome(); }
    else if (screenState == SCREEN_BATTERY_PROFILE) { screenState = SCREEN_BATTERY_LIST; showBatteryList(); }
    else if (screenState == SCREEN_EDIT_PROFILE) { screenState = SCREEN_BATTERY_PROFILE; showBatteryProfile(); }
    else if (screenState == SCREEN_EDIT_VOLTAGE || screenState == SCREEN_EDIT_CURRENT) {
      screenState = returnToPsuActive ? SCREEN_PSU_ACTIVE : SCREEN_POWER_SUPPLY;
      if (returnToPsuActive) showPsuActive(); else showPowerSupply();
    }
    else if (screenState == SCREEN_PSU_ACTIVE) {
      psuActive = false; returnToPsuActive = false; screenState = SCREEN_HOME; showHome();
    }
    else { screenState = SCREEN_MENU; drawMenu(); }
  }

  lastUp = upNow; lastDown = downNow; lastOK = okNow; lastBack = backNow;
  delay(20);
}
