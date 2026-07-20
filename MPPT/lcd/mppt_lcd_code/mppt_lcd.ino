#include <Arduino.h>
#include <LiquidCrystal.h>

// LCD Pins
const int rs = 3;
const int en = 2;
const int d4 = 4;
const int d5 = 5;
const int d6 = 6;
const int d7 = 7;

// Buttons
#define BTN_OK     9
#define BTN_BACK   8
#define BTN_UP     1
#define BTN_DOWN   0

LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

// =========================
// MENU VARIABLES
// =========================

int menuIndex = 0;      // selected item
int menuTop = 0;        // first visible item
bool pageSelected = false;

const char* menuItems[] =
{
  "Home",
  "Readings",
  "MPPT Status",
  "Battery Type",
  "Charge Voltages",
  "Current Limit",
  "System Info",
  "Factory Reset"
};

const int menuCount = 8;

// =========================
// BUTTON EDGE DETECTION
// =========================

bool lastUp    = HIGH;
bool lastDown  = HIGH;
bool lastOK    = HIGH;
bool lastBack  = HIGH;

// =========================
void startup()
{

lcd.clear();

lcd.setCursor(3,0);
lcd.print("TIVANA SOLAR MPPT");


lcd.setCursor(6,1);
lcd.print("Version 1");


lcd.setCursor(1,2);
lcd.print("Initializing");


for(int i=0;i<3;i++)
{
 lcd.print(".");
 delay(500);
}

showHome();

}


void drawMenu()
{
  lcd.clear();

  lcd.setCursor(0,0);
  lcd.print("MAIN MENU");

  for(int row=0; row<3; row++)
  {
    int item = menuTop + row;

    if(item >= menuCount)
      break;

    lcd.setCursor(0,row+1);

    if(item == menuIndex)
      lcd.print("> ");
    else
      lcd.print("  ");

    lcd.print(menuItems[item]);
  }
}

// =========================

void showHome()
{
  lcd.clear();

  lcd.setCursor(0,0);
  lcd.print("HOME PAGE : BULK");

  lcd.setCursor(0,1);
  lcd.print("PV: 38.5V  I: 8A");

  lcd.setCursor(0,2);
  lcd.print("BAT: 14.4.5V  I: 20A");

  lcd.setCursor(0,3);
  lcd.print("OK=Menu");
}

// =========================

void showReadings()
{
  lcd.clear();

  lcd.setCursor(0,0);
  lcd.print("READINGS");

  lcd.setCursor(0,1);
  lcd.print("PV:38.5V 5.2A");

  lcd.setCursor(0,2);
  lcd.print("BAT:13.8V");

  lcd.setCursor(0,3);
  lcd.print("BACK=Menu");
}

// =========================

void showSettings()
{
  lcd.clear();

  lcd.setCursor(0,0);
  lcd.print("SETTINGS");

  lcd.setCursor(0,1);
  lcd.print("Battery:Lith");

  lcd.setCursor(0,2);
  lcd.print("Bulk:14.4V");

  lcd.setCursor(0,3);
  lcd.print("BACK=Menu");
}

// =========================

void showAbout()
{
  lcd.clear();

  lcd.setCursor(0,0);
  lcd.print("ABOUT");

  lcd.setCursor(0,1);
  lcd.print("ESP32 MPPT");

  lcd.setCursor(0,2);
  lcd.print("LCD 20x4 Demo");

  lcd.setCursor(0,3);
  lcd.print("BACK=Menu");
}

// =========================

void openPage()
{
  switch(menuIndex)
  {
    case 0:
      showHome();
      break;

    case 1:
      showReadings();
      break;

    case 2:
      showSettings();
      break;

    case 3:
      showAbout();
      break;
  }
}

// =========================

void setup()
{
  lcd.begin(20,4);

  pinMode(BTN_OK, INPUT_PULLUP);
  pinMode(BTN_BACK, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

startup();
  

  pageSelected = true;
}

// =========================

void loop()
{
  bool upNow   = digitalRead(BTN_UP);
  bool downNow = digitalRead(BTN_DOWN);
  bool okNow   = digitalRead(BTN_OK);
  bool backNow = digitalRead(BTN_BACK);

  // UP
  if(lastUp == HIGH && upNow == LOW)
{
  if(!pageSelected)
  {
    menuIndex--;

    if(menuIndex < 0)
    {
      menuIndex = menuCount - 1;
      menuTop = menuCount - 3;
    }

    if(menuIndex < menuTop)
      menuTop--;

    drawMenu();
  }
}

  // DOWN
 if(lastDown == HIGH && downNow == LOW)
{
  if(!pageSelected)
  {
    menuIndex++;

    if(menuIndex >= menuCount)
      menuIndex = 0;

    if(menuIndex > menuTop + 2)
      menuTop++;

    if(menuIndex == 0)
      menuTop = 0;

    drawMenu();
  }
}

  // OK
 if(lastOK == HIGH && okNow == LOW)
{
  if(pageSelected)
  {
    pageSelected = false;
    drawMenu();
  }
  else
  {
    pageSelected = true;
    openPage();
  }
}

  // BACK
if(lastBack == HIGH && backNow == LOW)
{
  pageSelected = true;
  showHome();
}

  lastUp = upNow;
  lastDown = downNow;
  lastOK = okNow;
  lastBack = backNow;

  delay(20);
}
