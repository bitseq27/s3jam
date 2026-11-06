#include "wifi_scan_page.h"
#include "display_buffer.h"

#include <Arduino.h>
#include <WiFi.h>

#define WIFI_COUNT_COLOR 0x07E0
#define WIFI_SCAN_START_OFFSET_MS 1000

static bool wifiScanStarted = false;

static void startWifiScan(void)
{
  if (!wifiScanStarted) {
    WiFi.mode(WIFI_STA);
    WiFi.scanNetworks(true);
    wifiScanStarted = true;
  }
}

static int finishWifiScan(void)
{
  if (!wifiScanStarted) {
    WiFi.mode(WIFI_STA);
    WiFi.scanNetworks(true);
    wifiScanStarted = true;
    delay(10);
  }

  int16_t status = WiFi.scanComplete();
  unsigned long waitStart = millis();

  while (status == -1 && (millis() - waitStart < 3000)) {
    delay(10);
    status = WiFi.scanComplete();
  }

  if (status < 0) {
    WiFi.scanDelete();
    status = WiFi.scanNetworks();
  }

  int count = 0;

  if (status >= 0) {
    count = status;
  }

  if (count == 0) {
    delay(250);
    WiFi.scanDelete();

    int retry = WiFi.scanNetworks();

    if (retry >= 0) {
      count = retry;
    }
  }

  Serial.println("-----------------------------------");
  Serial.printf("Wi-Fi networks found: %d\n", count);
  Serial.println("-----------------------------------");

  if (count == 0) {
    Serial.println("No Wi-Fi SSIDs found.");
  } else {
    for (int i = 0; i < count; i++) {
      String ssid = WiFi.SSID(i);

      if (ssid.length() == 0) {
        ssid = "<hidden>";
      }

      Serial.printf("%2d: %-32s CH:%2ld RSSI:%ld dBm\n",
                    i + 1,
                    ssid.c_str(),
                    (long)WiFi.channel(i),
                    (long)WiFi.RSSI(i));
    }
  }

  Serial.println("-----------------------------------");

  WiFi.scanDelete();
  wifiScanStarted = false;

  return count;
}

static void drawWifiCountPage(int count)
{
  if (count < 0) {
    count = 0;
  }

  char text[12];
  snprintf(text, sizeof(text), "%d", count);

  drawScaledTextPage(text, WIFI_COUNT_COLOR, true);
}

void wifiScanPage_init(void)
{
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
}

void wifiScanPage_preStart(unsigned long now, unsigned long pageStartMs)
{
  if (now - pageStartMs >= WIFI_SCAN_START_OFFSET_MS) {
    startWifiScan();
  }
}

void wifiScanPage_enter(void)
{
  int ssidCount = finishWifiScan();
  drawWifiCountPage(ssidCount);
}