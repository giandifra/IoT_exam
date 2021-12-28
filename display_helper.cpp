#include "display_helper.h"
#include "Arduino.h"
#include <Adafruit_SSD1306.h>

//DisplayHelper::DisplayHelper() {
//}

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
 
void setupDisplay() {
  // SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); // Don't proceed, loop forever
  }
  display.display();
  delay(500);
  display.clearDisplay();
}

void printText(String title)
{
  display.setTextColor(SSD1306_WHITE);        // Draw white text
  display.setTextSize(1);             // Normal 1:1 pixel scale
  display.println(title);
}

void printTitle(String title) {
  display.clearDisplay();
  display.setCursor(0, 0);            // Start at top-left corner
  display.setTextColor(SSD1306_WHITE);        // Draw white text
  display.setTextSize(2);
  display.println(title);
}
