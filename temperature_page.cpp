#include "temperature_page.h"
#include "display_buffer.h"

#include <Arduino.h>

#define TEMP_TEXT_COLOR 0xF800

static float readTemperature(void)
{
  float t = temperatureRead();

  if (!(t >= -40.0f && t <= 125.0f)) {
    t = 0.0f;
  }

  return t;
}

static void logTemperature(float tempC)
{
  Serial.println("-----------------------------------");
  Serial.printf("ESP32 internal temperature: %.2f C\n", tempC);
  Serial.println("-----------------------------------");
}

static void drawTemperaturePage(float tempC)
{
  if (!(tempC >= -40.0f && tempC <= 125.0f)) {
    tempC = 0.0f;
  }

  char text[16];
  snprintf(text, sizeof(text), "%.1fC", tempC);

  drawScaledTextPage(text, TEMP_TEXT_COLOR, false);
}

void temperaturePage_enter(void)
{
  float tempC = readTemperature();
  logTemperature(tempC);
  drawTemperaturePage(tempC);
}