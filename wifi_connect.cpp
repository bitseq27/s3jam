#include "wifi_connect.h"
#include "display_buffer.h"

#include <Arduino.h>
#include <WiFi.h>
#include <string.h>

#define WIFI_CONNECT_TIMEOUT_MS 50000
#define WIFI_CONNECT_SSID "bitseq"
#define WIFI_CONNECT_PASS "00000000"

static int wifiConnectStatus = 0;
static unsigned long wifiConnectStartMs = 0;
static unsigned long lastWifiConnectDrawMs = 0;

static void drawWifiConnectPage(int status, int dots)
{
  Paint_SelectImage((UBYTE *)BlackImage);
  Paint_Clear(BLACK);

  char msg[20];
  int scale;

  if (status == 1) {
    strcpy(msg, "LIVE");
    scale = fitTextScale(msg, LCD_1IN28_WIDTH - 20);
  } else if (status == 2) {
    strcpy(msg, "Failed");
    scale = fitTextScale(msg, LCD_1IN28_WIDTH - 20);
  } else {
    strcpy(msg, "Connecting");

    for (int i = 0; i < dots && i < 3; i++) {
      strcat(msg, ".");
    }

    scale = fitTextScale("Connecting...", LCD_1IN28_WIDTH - 10);
  }

  int centerY = LCD_1IN28_HEIGHT / 2 - 15;

  drawCenteredScaledLine(msg, centerY, scale, WHITE);

  const char *ssid = WIFI_CONNECT_SSID;
  int sw = strlen(ssid) * (int)Font16.Width;
  int sx = ((int)LCD_1IN28_WIDTH - sw) / 2;

  if (sx < 0) {
    sx = 0;
  }

  Paint_DrawString_EN((UWORD)sx, (UWORD)(centerY + 45), ssid, &Font16, WHITE, 0x0000);

  LCD_1IN28_Display(BlackImage);
}

static void wifiPowerCycleStation(void)
{
  WiFi.persistent(false);

  WiFi.mode(WIFI_OFF);
  delay(150);

  WiFi.mode(WIFI_STA);
  delay(100);
}

void wifiConnectPage_enter(void)
{
  wifiConnectStatus = 0;

  wifiPowerCycleStation();

  WiFi.setSleep(false);

  Serial.println("-----------------------------------");
  Serial.printf("WiFi: connecting to \"%s\" ...\n", WIFI_CONNECT_SSID);

  WiFi.begin(WIFI_CONNECT_SSID, WIFI_CONNECT_PASS);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  wifiConnectStartMs = millis();
  lastWifiConnectDrawMs = 0;

  drawWifiConnectPage(0, 0);
}

void wifiConnectPage_loop(unsigned long now)
{
  if (wifiConnectStatus == 0) {
    wl_status_t st = WiFi.status();

    if (st == WL_CONNECTED) {
      wifiConnectStatus = 1;
      Serial.println("WiFi: LIVE (connected)");
      Serial.print("WiFi: IP = ");
      Serial.println(WiFi.localIP());
      drawWifiConnectPage(1, 0);
      lastWifiConnectDrawMs = now;
    } else if (st == WL_CONNECT_FAILED ||
               st == WL_NO_SSID_AVAIL ||
               st == WL_NO_SHIELD) {
      wifiConnectStatus = 2;
      Serial.println("WiFi: Failed");
      drawWifiConnectPage(2, 0);
      lastWifiConnectDrawMs = now;
    } else if (now - wifiConnectStartMs >= WIFI_CONNECT_TIMEOUT_MS) {
      wifiConnectStatus = 2;
      Serial.println("WiFi: Failed (timeout)");
      drawWifiConnectPage(2, 0);
      lastWifiConnectDrawMs = now;
    }
  }

  if (lastWifiConnectDrawMs == 0 ||
      now - lastWifiConnectDrawMs >= 500) {
    int dots = ((now - wifiConnectStartMs) / 500) % 4;
    drawWifiConnectPage(wifiConnectStatus, dots);
    lastWifiConnectDrawMs = now;
  }
}

void wifiConnectPage_exit(void)
{
  WiFi.disconnect();
  delay(120);

  WiFi.mode(WIFI_STA);
  delay(60);

  Serial.println("WiFi: leaving page, disconnecting");
  Serial.println("-----------------------------------");
}