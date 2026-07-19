#include <Arduino.h>

#include <LiquidCrystal.h>

// Define LCD pins with ESP32 GPIO numbers
const int rs = 3, en = 2, d4 = 4, d5 = 5, d6 = 18, d7 = 19;

// Initialize the LCD library with these pins
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

void setup() {
  // Set up the LCD's number of columns and rows:
  lcd.begin(20, 4);        // 20 columns, 4 rows
  
  // Turn on backlight (if connected)
  // Note: Backlight is controlled by hardware (A & K pins)

  // Print welcome message
  lcd.setCursor(0, 0);     // Column 0, Row 0
  lcd.print("LM044L 20x4 LCD");
  
  lcd.setCursor(0, 1);     // Row 1
  lcd.print("Working with ESP32");
  
  lcd.setCursor(0, 2);     // Row 2
  lcd.print("Proteus Simulation");
  
  lcd.setCursor(0, 3);     // Row 3
  lcd.print("Ready to Use!");
}

void loop() {
  // Example: Scrolling text on row 3
  lcd.setCursor(0, 3);
  lcd.print("                "); // Clear row 3
  lcd.setCursor(0, 3);
  lcd.print("Scrolling Text...");

  for (int i = 0; i < 10; i++) {
    lcd.scrollDisplayLeft();
    delay(300);
  }

  delay(1000);

  // Static message example
  lcd.setCursor(0, 3);
  lcd.print("Hello from Grok!   ");
  delay(2000);
}