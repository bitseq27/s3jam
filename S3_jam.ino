#include <Arduino.h>

#include "display_buffer.h"
#include "sensor_page.h"
#include "wifi_scan_page.h"
#include "ble_scan_page.h"
#include "temperature_page.h"
#include "starfield.h"
#include "cube.h"
#include "uptime.h"
#include "frog_page.h"
#include "city_page.h"
#include "wifi_connect.h"

enum {
  PAGE_ACCEL,
  PAGE_WIFI,
  PAGE_BT,
  PAGE_TEMP,
  PAGE_STARFIELD,
  PAGE_CUBE,
  PAGE_UPTIME,
  PAGE_FROG,
  PAGE_CITY,
  PAGE_WIFICONNECT
};

#define PAGE_ACCEL_ENABLED       false
#define PAGE_WIFI_ENABLED        false
#define PAGE_BT_ENABLED          false
#define PAGE_TEMP_ENABLED        false
#define PAGE_STARFIELD_ENABLED   true
#define PAGE_CUBE_ENABLED        false
#define PAGE_UPTIME_ENABLED      true
#define PAGE_FROG_ENABLED        true
#define PAGE_CITY_ENABLED        true
#define PAGE_WIFICONNECT_ENABLED false

#define ACCEL_PAGE_DURATION_MS       3000
#define WIFI_PAGE_DURATION_MS        3000
#define BT_PAGE_DURATION_MS          3000
#define TEMP_PAGE_DURATION_MS        3000
#define STARFIELD_PAGE_DURATION_MS   5000
#define CUBE_PAGE_DURATION_MS        1000
#define UPTIME_PAGE_DURATION_MS      3000
#define FROG_PAGE_DURATION_MS        240000
#define CITY_PAGE_DURATION_MS        30000
#define WIFICONNECT_PAGE_DURATION_MS 30000

static int currentPage = PAGE_ACCEL;
static unsigned long pageStartMs = 0;

static const int pageOrder[] = {
  PAGE_ACCEL,
  PAGE_WIFI,
  PAGE_BT,
  PAGE_TEMP,
  PAGE_STARFIELD,
  PAGE_CUBE,
  PAGE_UPTIME,
  PAGE_FROG,
  PAGE_CITY,
  PAGE_WIFICONNECT
};

static const int pageCount = sizeof(pageOrder) / sizeof(pageOrder[0]);

static bool isPageEnabled(int page)
{
  switch (page) {
    case PAGE_ACCEL:       return PAGE_ACCEL_ENABLED;
    case PAGE_WIFI:        return PAGE_WIFI_ENABLED;
    case PAGE_BT:          return PAGE_BT_ENABLED;
    case PAGE_TEMP:        return PAGE_TEMP_ENABLED;
    case PAGE_STARFIELD:   return PAGE_STARFIELD_ENABLED;
    case PAGE_CUBE:        return PAGE_CUBE_ENABLED;
    case PAGE_UPTIME:      return PAGE_UPTIME_ENABLED;
    case PAGE_FROG:        return PAGE_FROG_ENABLED;
    case PAGE_CITY:        return PAGE_CITY_ENABLED;
    case PAGE_WIFICONNECT: return PAGE_WIFICONNECT_ENABLED;
    default:               return false;
  }
}

static int firstEnabledPage(void)
{
  for (int i = 0; i < pageCount; i++) {
    if (isPageEnabled(pageOrder[i])) {
      return pageOrder[i];
    }
  }
  return PAGE_ACCEL;
}

static int nextEnabledPage(int current)
{
  int startIdx = 0;
  for (int i = 0; i < pageCount; i++) {
    if (pageOrder[i] == current) {
      startIdx = i;
      break;
    }
  }
  for (int step = 1; step <= pageCount; step++) {
    int idx = (startIdx + step) % pageCount;
    if (isPageEnabled(idx)) {
      return idx;
    }
  }
  return current;
}

static void enterPage(int page)
{
  currentPage = page;
  switch (page) {
    case PAGE_ACCEL:      sensorPage_enter();      break;
    case PAGE_WIFI:       wifiScanPage_enter();    break;
    case PAGE_BT:         bleScanPage_enter();     break;
    case PAGE_TEMP:       temperaturePage_enter(); break;
    case PAGE_STARFIELD:  starfield_enter();       break;
    case PAGE_CUBE:       cube_enter();            break;
    case PAGE_UPTIME:     uptimePage_enter();      break;
    case PAGE_FROG:       frogPage_enter();        break;
    case PAGE_CITY:       cityPage_enter();        break;
    case PAGE_WIFICONNECT: wifiConnectPage_enter(); break;
  }
  pageStartMs = millis();
}

static void leavePage(int page)
{
  if (page == PAGE_STARFIELD) {
    starfield_exit();
  }
  if (page == PAGE_CITY) {
    cityPage_exit();
  }
  if (page == PAGE_WIFICONNECT) {
    wifiConnectPage_exit();
  }
}

static void goToNextPage(void)
{
  leavePage(currentPage);
  enterPage(nextEnabledPage(currentPage));
}

void setup()
{
  Serial.begin(115200);

  displayBuffer_init();

  sensorPage_init();
  wifiScanPage_init();
  bleScanPage_init();

  enterPage(firstEnabledPage());
}

void loop()
{
  unsigned long now = millis();

  switch (currentPage) {
    case PAGE_ACCEL:
      sensorPage_loop(now);
      if (isPageEnabled(PAGE_WIFI)) {
        wifiScanPage_preStart(now, pageStartMs);
      }
      if (now - pageStartMs >= ACCEL_PAGE_DURATION_MS) {
        goToNextPage();
      }
      break;

    case PAGE_WIFI:
      if (now - pageStartMs >= WIFI_PAGE_DURATION_MS) {
        goToNextPage();
      }
      break;

    case PAGE_BT:
      if (now - pageStartMs >= BT_PAGE_DURATION_MS) {
        goToNextPage();
      }
      break;

    case PAGE_TEMP:
      if (now - pageStartMs >= TEMP_PAGE_DURATION_MS) {
        goToNextPage();
      }
      break;

    case PAGE_STARFIELD:
      starfield_loop(now);
      if (now - pageStartMs >= STARFIELD_PAGE_DURATION_MS) {
        goToNextPage();
      }
      break;

    case PAGE_CUBE:
      cube_loop(now);
      if (now - pageStartMs >= CUBE_PAGE_DURATION_MS) {
        goToNextPage();
      }
      break;

    case PAGE_UPTIME:
      uptimePage_loop(now);
      if (now - pageStartMs >= UPTIME_PAGE_DURATION_MS) {
        goToNextPage();
      }
      break;

    case PAGE_FROG:
      frogPage_loop(now);
      if (now - pageStartMs >= FROG_PAGE_DURATION_MS) {
        goToNextPage();
      }
      break;

    case PAGE_CITY:
      cityPage_loop(now);
      if (now - pageStartMs >= CITY_PAGE_DURATION_MS) {
        goToNextPage();
      }
      break;

    case PAGE_WIFICONNECT:
      wifiConnectPage_loop(now);
      if (now - pageStartMs >= WIFICONNECT_PAGE_DURATION_MS) {
        goToNextPage();
      }
      break;
  }

  delay(1);
}