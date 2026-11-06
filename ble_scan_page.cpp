#include "ble_scan_page.h"
#include "display_buffer.h"

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <string.h>

#define BLE_SCAN_SECONDS 3
#define BT_COUNT_COLOR 0x001F

static BLEScan *pBLEScan = nullptr;

static int runBluetoothScan(void)
{
  const int MAX_BLE_DEVICES = 200;
  static char seenAddresses[MAX_BLE_DEVICES][18];

  Serial.println("-----------------------------------");
  Serial.println("Starting Bluetooth (BLE) scan...");

  if (pBLEScan == nullptr) {
    pBLEScan = BLEDevice::getScan();
    pBLEScan->setActiveScan(true);
  }

  pBLEScan->clearResults();

  BLEScanResults *results = pBLEScan->start(BLE_SCAN_SECONDS, false);

  int totalResults = (results != nullptr) ? results->getCount() : 0;
  int uniqueCount = 0;

  Serial.printf("BLE raw results: %d\n", totalResults);
  Serial.println("Bluetooth devices:");

  for (int i = 0; i < totalResults && uniqueCount < MAX_BLE_DEVICES; i++) {
    BLEAdvertisedDevice device = results->getDevice(i);

    String addr = device.getAddress().toString().c_str();

    bool alreadySeen = false;

    for (int j = 0; j < uniqueCount; j++) {
      if (strcmp(seenAddresses[j], addr.c_str()) == 0) {
        alreadySeen = true;
        break;
      }
    }

    if (alreadySeen) {
      continue;
    }

    strncpy(seenAddresses[uniqueCount], addr.c_str(), sizeof(seenAddresses[0]) - 1);
    seenAddresses[uniqueCount][sizeof(seenAddresses[0]) - 1] = '\0';

    String name = "<unknown>";

    if (device.haveName()) {
      String deviceName = device.getName().c_str();

      if (deviceName.length() > 0) {
        name = deviceName;
      }
    }

    Serial.printf("%2d: %-32s ADDR:%s RSSI:%d\n",
                  uniqueCount + 1,
                  name.c_str(),
                  addr.c_str(),
                  device.getRSSI());

    uniqueCount++;
  }

  if (uniqueCount == 0) {
    Serial.println("No Bluetooth devices found.");
  }

  Serial.printf("Bluetooth device count: %d\n", uniqueCount);
  Serial.println("-----------------------------------");

  pBLEScan->clearResults();

  return uniqueCount;
}

static void drawBluetoothCountPage(int count)
{
  if (count < 0) {
    count = 0;
  }

  char text[12];
  snprintf(text, sizeof(text), "%d", count);

  drawScaledTextPage(text, BT_COUNT_COLOR, true);
}

void bleScanPage_init(void)
{
  BLEDevice::init("ESP32-S3-LCD");
  Serial.println("Bluetooth initialized");
}

void bleScanPage_enter(void)
{
  int btCount = runBluetoothScan();
  drawBluetoothCountPage(btCount);
}