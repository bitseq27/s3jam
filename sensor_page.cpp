#include "sensor_page.h"
#include "display_buffer.h"

#include <Arduino.h>
#include <Wire.h>

#define ACCEL_REFRESH_MS 250

static float acc[3], gyro[3];
static unsigned int tim_count = 0;
static uint16_t result;

static const float conversion_factor = 3.3f / (1 << 12) * 3;

static bool imuOK = false;
static uint8_t qmiAddress = 0;

static unsigned long lastAccelUpdateMs = 0;

static bool i2cAddressResponds(uint8_t addr)
{
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}

static bool qmiReadByte(uint8_t addr, uint8_t reg, uint8_t &value)
{
  Wire.beginTransmission(addr);
  Wire.write(reg);

  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  if (Wire.requestFrom(addr, 1) != 1) {
    return false;
  }

  value = Wire.read();
  return true;
}

static bool detectQMI8658()
{
  const uint8_t candidates[] = {0x6A, 0x6B};

  for (int retry = 0; retry < 5; retry++) {
    for (uint8_t i = 0; i < 2; i++) {
      uint8_t addr = candidates[i];

      if (!i2cAddressResponds(addr)) {
        continue;
      }

      uint8_t id = 0;

      if (qmiReadByte(addr, 0x00, id)) {
        Serial.printf("I2C 0x%02X WHO_AM_I reg 0x00 = 0x%02X\n",
                      (unsigned)addr, (unsigned)id);

        if (id != 0x00 && id != 0xFF) {
          qmiAddress = addr;
          return true;
        }
      }

      id = 0;

      if (qmiReadByte(addr, 0x0A, id)) {
        Serial.printf("I2C 0x%02X WHO_AM_I reg 0x0A = 0x%02X\n",
                      (unsigned)addr, (unsigned)id);

        if (id != 0x00 && id != 0xFF) {
          qmiAddress = addr;
          return true;
        }
      }
    }

    delay(50);
  }

  Serial.println("QMI8658 not found. Starting I2C scan...");

  bool found = false;

  for (uint8_t addr = 1; addr < 127; addr++) {
    if (i2cAddressResponds(addr)) {
      Serial.printf("Found I2C device at 0x%02X\n", (unsigned)addr);
      found = true;
    }
  }

  if (!found) {
    Serial.println("No I2C devices found.");
  }

  return false;
}

static void drawAccelerometerStatic(void)
{
  Paint_SelectImage((UBYTE *)BlackImage);
  Paint_Clear(WHITE);

  Paint_DrawRectangle(0, 0, 240, 47, 0XF410, DOT_PIXEL_2X2, DRAW_FILL_FULL);
  Paint_DrawRectangle(0, 47, 240, 120, 0X4F30, DOT_PIXEL_2X2, DRAW_FILL_FULL);
  Paint_DrawRectangle(0, 120, 240, 195, 0XAD55, DOT_PIXEL_2X2, DRAW_FILL_FULL);
  Paint_DrawRectangle(0, 195, 240, 240, 0X2595, DOT_PIXEL_2X2, DRAW_FILL_FULL);

  const char *title = imuOK ? "Sensor Data" : "No IMU";

  Paint_DrawString_EN(45, 30, title, &Font16, WHITE, BLACK);

  Paint_DrawString_EN(45, 50, "ACC_X = ", &Font16, WHITE, BLACK);
  Paint_DrawString_EN(45, 75, "ACC_Y = ", &Font16, WHITE, BLACK);
  Paint_DrawString_EN(45, 100, "ACC_Z = ", &Font16, WHITE, BLACK);

  Paint_DrawString_EN(45, 125, "GYR_X = ", &Font16, WHITE, BLACK);
  Paint_DrawString_EN(45, 150, "GYR_Y = ", &Font16, WHITE, BLACK);
  Paint_DrawString_EN(45, 175, "GYR_Z = ", &Font16, WHITE, BLACK);

  Paint_DrawString_EN(45, 200, "BAT(V)=", &Font16, WHITE, BLACK);

  LCD_1IN28_Display(BlackImage);
}

static void updateAccelerometerValues(void)
{
  result = DEC_ADC_Read();

  if (imuOK) {
    QMI8658_read_xyz(acc, gyro, &tim_count);
  } else {
    acc[0] = acc[1] = acc[2] = 0.0f;
    gyro[0] = gyro[1] = gyro[2] = 0.0f;
  }

  Paint_SelectImage((UBYTE *)BlackImage);
  Paint_Clear(WHITE);

  Paint_DrawRectangle(120, 47, 220, 120, 0X4F30, DOT_PIXEL_2X2, DRAW_FILL_FULL);
  Paint_DrawRectangle(120, 120, 220, 195, 0XAD55, DOT_PIXEL_2X2, DRAW_FILL_FULL);
  Paint_DrawRectangle(120, 195, 220, 240, 0X2595, DOT_PIXEL_2X2, DRAW_FILL_FULL);

  Paint_DrawNum(120, 50, acc[0], &Font16, 2, BLACK, WHITE);
  Paint_DrawNum(120, 75, acc[1], &Font16, 2, BLACK, WHITE);
  Paint_DrawNum(120, 100, acc[2], &Font16, 2, BLACK, WHITE);

  Paint_DrawNum(120, 125, gyro[0], &Font16, 2, BLACK, WHITE);
  Paint_DrawNum(120, 150, gyro[1], &Font16, 2, BLACK, WHITE);
  Paint_DrawNum(120, 175, gyro[2], &Font16, 2, BLACK, WHITE);

  Paint_DrawNum(130, 200, result * conversion_factor, &Font16, 2, BLACK, WHITE);

  LCD_1IN28_DisplayWindows(120, 50, 210, 200, BlackImage);
  LCD_1IN28_DisplayWindows(130, 200, 220, 220, BlackImage);
}

void sensorPage_init(void)
{
  bool detected = detectQMI8658();

  if (detected) {
    Serial.printf("Attempting QMI8658_init, detected address 0x%02X\n",
                  (unsigned)qmiAddress);

    if (QMI8658_init()) {
      imuOK = true;
      Serial.println("QMI8658_init done");
    } else {
      imuOK = false;
      Serial.println("QMI8658_init failed");
    }
  } else {
    imuOK = false;
    Serial.println("QMI8658 not detected. Skipping IMU initialization.");
  }
}

void sensorPage_enter(void)
{
  drawAccelerometerStatic();
  updateAccelerometerValues();
  lastAccelUpdateMs = millis();
}

void sensorPage_loop(unsigned long now)
{
  if (now - lastAccelUpdateMs >= ACCEL_REFRESH_MS) {
    updateAccelerometerValues();
    lastAccelUpdateMs = now;
  }
}