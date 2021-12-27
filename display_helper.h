#ifndef DISPLAY_HELPER_H
#define DISPLAY_HELPER_H

#include <Wire.h>
#include "Arduino.h"
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)

extern Adafruit_SSD1306 display;

void printTitle(String title);
void printText(String title);
void setupDisplay();

#endif
