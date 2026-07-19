#include <Arduino.h>

#include <LiquidCrystal.h>

//================ BUTTONS =============

// Define LCD pins with ESP32 GPIO numbers
const int rs = 3, en = 2, d4 = 4, d5 = 5, d6 = 6, d7 = 7;

// Initialize the LCD library with these pins
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

#define BTN_UP      32
#define BTN_DOWN    33
#define BTN_ENTER   25
#define BTN_BACK    26



//================ MENU STATES ==========

enum Page
{
  HOME,
  READINGS,
  MPPT_STATUS,
  SETTINGS,
  BATTERY_MENU,
  VOLTAGE_MENU
};


Page currentPage = HOME;


//================ BATTERY ==============

enum BatteryType
{
  LITHIUM,
  GEL,
  LEAD_ACID
};


BatteryType batteryType = LITHIUM;



String batteryNames[] =
{
  "LITHIUM",
  "GEL",
  "LEAD ACID"
};



//================ SETTINGS =============

float bulkVoltage = 14.40;
float absorptionVoltage = 14.20;
float floatVoltage = 13.60;

int settingsCursor = 0;

bool editing=false;


//================ FAKE SENSOR DATA ======

float pvVoltage = 38.5;
float pvCurrent = 5.2;

float batteryVoltage = 13.8;

float temperature = 32;

float duty = 55;



//================ CHARGE STATE =========

enum ChargeStage
{
 BULK,
 ABSORPTION,
 FLOAT
};


ChargeStage chargeStage=BULK;



//================================================


void setup()
{

Serial.begin(115200);


lcd.begin(20, 4);
lcd.display();



pinMode(BTN_UP,INPUT_PULLUP);
pinMode(BTN_DOWN,INPUT_PULLUP);
pinMode(BTN_ENTER,INPUT_PULLUP);
pinMode(BTN_BACK,INPUT_PULLUP);



startup();


}



//================================================


void loop()
{


fakeSensors();


handleButtons();


drawDisplay();


delay(100);


}



//================================================
// STARTUP
//================================================


void startup()
{

lcd.clear();

lcd.setCursor(3,0);
lcd.print("SOLAR MPPT");


lcd.setCursor(6,1);
lcd.print("ESP32");


lcd.setCursor(1,2);
lcd.print("Initializing");


for(int i=0;i<3;i++)
{
 lcd.print(".");
 delay(500);
}


}



//================================================
// FAKE SENSOR SIMULATION
//================================================


void fakeSensors()
{

static float x=0;


x+=0.1;


pvVoltage = 36 + sin(x)*3;

pvCurrent = 5 + sin(x)*1;


batteryVoltage +=0.002;


if(batteryVoltage>14.5)
batteryVoltage=13.2;



duty =
map(
batteryVoltage*100,
1320,
1450,
80,
20
);



if(batteryVoltage<14.3)
chargeStage=BULK;

else if(batteryVoltage<14.45)
chargeStage=ABSORPTION;

else
chargeStage=FLOAT;


}




//================================================
// DISPLAY MANAGER
//================================================


void drawDisplay()
{


switch(currentPage)
{


case HOME:

showHome();

break;


case READINGS:

showReadings();

break;


case MPPT_STATUS:

showMPPT();

break;


case SETTINGS:

showSettings();

break;


case BATTERY_MENU:

showBatteryMenu();

break;


case VOLTAGE_MENU:

showVoltageMenu();

break;


}



}



//================================================
// HOME PAGE
//================================================


void showHome()
{

lcd.clear();


lcd.setCursor(0,0);

lcd.print("PV:");

lcd.print(pvVoltage,1);

lcd.print("V ");

lcd.print(pvCurrent,1);

lcd.print("A");



lcd.setCursor(0,1);

lcd.print("BAT:");

lcd.print(batteryVoltage,2);

lcd.print("V ");

lcd.print(stageText());



lcd.setCursor(0,2);

lcd.print("POWER:");

lcd.print(
pvVoltage*pvCurrent,
0
);

lcd.print("W");



lcd.setCursor(0,3);

lcd.print("ENTER=MENU");


}



//================================================
// READINGS PAGE
//================================================


void showReadings()
{

lcd.clear();


lcd.setCursor(0,0);
lcd.print("LIVE READINGS");


lcd.setCursor(0,1);

lcd.print("PV:");

lcd.print(pvVoltage,2);

lcd.print("V");



lcd.setCursor(0,2);

lcd.print("BAT:");

lcd.print(batteryVoltage,2);

lcd.print("V");



lcd.setCursor(0,3);

lcd.print("TEMP:");

lcd.print(temperature);

lcd.print("C");

}



//================================================
// MPPT PAGE
//================================================


void showMPPT()
{

lcd.clear();


lcd.setCursor(0,0);
lcd.print("MPPT STATUS");


lcd.setCursor(0,1);

lcd.print("MODE:");

lcd.print(stageText());



lcd.setCursor(0,2);

lcd.print("DUTY:");

lcd.print(duty);

lcd.print("%");



lcd.setCursor(0,3);

lcd.print("TRACKING ON");


}



//================================================
// SETTINGS MENU
//================================================


String settingsItems[] =
{

"Battery Type",
"Voltage Setup"

};



void showSettings()
{

lcd.clear();


lcd.setCursor(0,0);

lcd.print("SETTINGS");



lcd.setCursor(0,1);


if(settingsCursor==0)
lcd.print(">Battery Type");

else
lcd.print(" Battery Type");



lcd.setCursor(0,2);


if(settingsCursor==1)
lcd.print(">Voltage Setup");

else
lcd.print(" Voltage Setup");



lcd.setCursor(0,3);

lcd.print("ENTER SELECT");


}



//================================================
// BATTERY MENU
//================================================


int batteryCursor=0;


void showBatteryMenu()
{

lcd.clear();


lcd.setCursor(0,0);

lcd.print("BATTERY TYPE");



for(int i=0;i<3;i++)
{

lcd.setCursor(0,i+1);


if(i==batteryCursor)
lcd.print(">");

else
lcd.print(" ");


lcd.print(
batteryNames[i]
);


}


}



//================================================
// VOLTAGE MENU
//================================================


void showVoltageMenu()
{

lcd.clear();


lcd.setCursor(0,0);

lcd.print("CHARGE SETTINGS");


lcd.setCursor(0,1);

lcd.print("Bulk ");

lcd.print(bulkVoltage);


lcd.setCursor(0,2);

lcd.print("Abs ");

lcd.print(absorptionVoltage);



lcd.setCursor(0,3);

lcd.print("Float ");

lcd.print(floatVoltage);


}




//================================================
// BUTTON CONTROL
//================================================


void handleButtons()
{


if(!digitalRead(BTN_ENTER))
{

delay(200);


if(currentPage==HOME)
currentPage=SETTINGS;


else if(currentPage==SETTINGS)
{

if(settingsCursor==0)
currentPage=BATTERY_MENU;


if(settingsCursor==1)
currentPage=VOLTAGE_MENU;


}



else if(currentPage==BATTERY_MENU)
{

batteryType=
(BatteryType)batteryCursor;


currentPage=SETTINGS;

}



}



if(!digitalRead(BTN_BACK))
{

delay(200);

currentPage=HOME;

}




if(!digitalRead(BTN_UP))
{

delay(200);


if(currentPage==SETTINGS)
{

settingsCursor--;

if(settingsCursor<0)
settingsCursor=1;

}



if(currentPage==BATTERY_MENU)
{

batteryCursor--;

if(batteryCursor<0)
batteryCursor=2;

}


}



if(!digitalRead(BTN_DOWN))
{

delay(200);


if(currentPage==SETTINGS)
{

settingsCursor++;

if(settingsCursor>1)
settingsCursor=0;

}



if(currentPage==BATTERY_MENU)
{

batteryCursor++;

if(batteryCursor>2)
batteryCursor=0;

}


}



}



//================================================


String stageText()
{

switch(chargeStage)
{

case BULK:
return "BULK";


case ABSORPTION:
return "ABS";


case FLOAT:
return "FLOAT";


}

return "";

}
