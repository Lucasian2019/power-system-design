#include <Arduino.h>
#include <LiquidCrystal.h>

// LCD pins
const int rs = 3, en = 2, d4 = 4, d5 = 5, d6 = 6, d7 = 7;

// Buttons are wired from the pin to GND (INPUT_PULLUP).
constexpr uint8_t BTN_OK = 9;
constexpr uint8_t BTN_BACK = 8;
constexpr uint8_t BTN_UP = 1;
constexpr uint8_t BTN_DOWN = 0;

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

enum ScreenState { SCREEN_HOME, SCREEN_MENU, SCREEN_MPPT, SCREEN_BATTERY,
                   SCREEN_POWER_SUPPLY, SCREEN_INFO, SCREEN_EDIT_VOLTAGE,
                   SCREEN_EDIT_CURRENT };

ScreenState screenState = SCREEN_HOME;
BatteryType activeBattery = LITHIUM;
int menuIndex = 0;
int menuTop = 0;
int subMenuIndex = 0;
float psuVoltage = 12.0F;
float psuCurrentLimit = 5.0F;

bool lastUp = HIGH, lastDown = HIGH, lastOK = HIGH, lastBack = HIGH;

void showHome() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("HOME PAGE : BULK");
  lcd.setCursor(0, 1); lcd.print("PV: 38.5V I: 8A");
  lcd.setCursor(0, 2); lcd.print("BAT: 14.4V I:20A");
  lcd.setCursor(0, 3); lcd.print("OK=Menu");
}

void drawMenu() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("MAIN MENU");
  for (int row = 0; row < 3; ++row) {
    const int item = menuTop + row;
    if (item >= menuCount) break;
    lcd.setCursor(0, row + 1);
    lcd.print(item == menuIndex ? "> " : "  ");
    lcd.print(menuItems[item]);
  }
}

void showMpptStatus() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("MPPT Status D=67%");
  lcd.setCursor(0, 1); lcd.print("PV:38.5V 5.2A");
  lcd.setCursor(0, 2); lcd.print("BAT:13.8V");
  lcd.setCursor(0, 3); lcd.print("BACK=Menu");
}

void showBatteryType() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("BATTERY TYPE");
  for (int i = 0; i < 3; ++i) {
    lcd.setCursor(0, i + 1);
    lcd.print(i == subMenuIndex ? "> " : "  ");
    lcd.print(batteryProfiles[i].name);
    if (i == activeBattery) lcd.print(" *");
  }
}

void showPowerSupply() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("POWER SUPPLY");
  lcd.setCursor(0, 1); lcd.print(subMenuIndex == 0 ? "> Output Voltage" : "  Output Voltage");
  lcd.setCursor(0, 2); lcd.print(subMenuIndex == 1 ? "> Current Limit" : "  Current Limit");
  lcd.setCursor(0, 3); lcd.print("OK=Edit BACK=Menu");
}

void showSystemInfo() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("SYSTEM INFO");
  lcd.setCursor(0, 1); lcd.print("Tivana MPPT");
  lcd.setCursor(0, 2); lcd.print("Version 1");
  lcd.setCursor(0, 3); lcd.print("BACK=Menu");
}

void showValueEditor(bool voltage) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print(voltage ? "SET OUTPUT VOLTAGE" : "SET CURRENT LIMIT");
  lcd.setCursor(0, 1); lcd.print(voltage ? psuVoltage : psuCurrentLimit, 1);
  lcd.print(voltage ? " V" : " A");
  lcd.setCursor(0, 3); lcd.print("UP/DN OK=SAVE");
}

void startup() {
  lcd.clear();
  lcd.setCursor(2, 0); lcd.print("TIVANA SOLAR MPPT");
  lcd.setCursor(6, 1); lcd.print("Version 1");
  lcd.setCursor(1, 2); lcd.print("Initializing...");
  delay(1500);
  showHome();
}

void openMenuItem() {
  switch (menuIndex) {
    case 0: screenState = SCREEN_HOME; showHome(); break;
    case 1: screenState = SCREEN_MPPT; showMpptStatus(); break;
    case 2: screenState = SCREEN_BATTERY; subMenuIndex = activeBattery; showBatteryType(); break;
    case 3: screenState = SCREEN_POWER_SUPPLY; subMenuIndex = 0; showPowerSupply(); break;
    case 4: screenState = SCREEN_INFO; showSystemInfo(); break;
  }
}

void setup() {
  lcd.begin(20, 4);
  pinMode(BTN_OK, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  startup();
}

void loop() {
  const bool upNow = digitalRead(BTN_UP);
  const bool downNow = digitalRead(BTN_DOWN);
  const bool okNow = digitalRead(BTN_OK);
  const bool backNow = digitalRead(BTN_BACK);
  const bool upPressed = lastUp == HIGH && upNow == LOW;
  const bool downPressed = lastDown == HIGH && downNow == LOW;
  const bool okPressed = lastOK == HIGH && okNow == LOW;
  const bool backPressed = lastBack == HIGH && backNow == LOW;

  if (screenState == SCREEN_MENU && (upPressed || downPressed)) {
    menuIndex += downPressed ? 1 : -1;
    if (menuIndex < 0) menuIndex = menuCount - 1;
    if (menuIndex >= menuCount) menuIndex = 0;
    if (menuIndex < menuTop) menuTop = menuIndex;
    if (menuIndex > menuTop + 2) menuTop = menuIndex - 2;
    drawMenu();
  } else if (screenState == SCREEN_BATTERY && (upPressed || downPressed)) {
    subMenuIndex = (subMenuIndex + (downPressed ? 1 : 2)) % 3;
    showBatteryType();
  } else if (screenState == SCREEN_POWER_SUPPLY && (upPressed || downPressed)) {
    subMenuIndex = 1 - subMenuIndex;
    showPowerSupply();
  } else if (screenState == SCREEN_EDIT_VOLTAGE && (upPressed || downPressed)) {
    psuVoltage += downPressed ? -0.1F : 0.1F;
    psuVoltage = constrain(psuVoltage, 0.0F, 60.0F);
    showValueEditor(true);
  } else if (screenState == SCREEN_EDIT_CURRENT && (upPressed || downPressed)) {
    psuCurrentLimit += downPressed ? -0.1F : 0.1F;
    psuCurrentLimit = constrain(psuCurrentLimit, 0.0F, 30.0F);
    showValueEditor(false);
  }

  if (okPressed) {
    if (screenState == SCREEN_HOME) {
      screenState = SCREEN_MENU;
      drawMenu();
    } else if (screenState == SCREEN_MENU) {
      openMenuItem();
    } else if (screenState == SCREEN_BATTERY) {
      activeBattery = static_cast<BatteryType>(subMenuIndex);
      showBatteryType();
    } else if (screenState == SCREEN_POWER_SUPPLY) {
      screenState = subMenuIndex == 0 ? SCREEN_EDIT_VOLTAGE : SCREEN_EDIT_CURRENT;
      showValueEditor(screenState == SCREEN_EDIT_VOLTAGE);
    } else if (screenState == SCREEN_EDIT_VOLTAGE || screenState == SCREEN_EDIT_CURRENT) {
      screenState = SCREEN_POWER_SUPPLY;
      showPowerSupply();
    }
  }

  if (backPressed) {
    if (screenState == SCREEN_HOME) {
      // Already at the top-level screen.
    } else if (screenState == SCREEN_MENU) {
      screenState = SCREEN_HOME;
      showHome();
    } else {
      screenState = SCREEN_MENU;
      drawMenu();
    }
  }

  // Always update edge-detection state, not only after BACK is pressed.
  lastUp = upNow;
  lastDown = downNow;
  lastOK = okNow;
  lastBack = backNow;
  delay(20);
}
