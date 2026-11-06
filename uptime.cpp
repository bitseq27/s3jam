#include "uptime.h"
#include "display_buffer.h"

#include <Arduino.h>

#define UPTIME_REFRESH_MS 1000
#define UPTIME_TEXT_COLOR WHITE

static unsigned long lastUptimeUpdateMs = 0;

static void drawUptimePage(void)
{
  unsigned long totalSeconds = millis() / 1000UL;

  unsigned long days = totalSeconds / 86400UL;
  unsigned long hours = (totalSeconds % 86400UL) / 3600UL;
  unsigned long minutes = (totalSeconds % 3600UL) / 60UL;
  unsigned long seconds = totalSeconds % 60UL;

  char line1[24];

  if (days > 0) {
    snprintf(line1, sizeof(line1), "%lud %02lu:%02lu:%02lu", days, hours, minutes, seconds);
  } else {
    snprintf(line1, sizeof(line1), "%02lu:%02lu:%02lu", hours, minutes, seconds);
  }

  drawScaledTextPage(line1, UPTIME_TEXT_COLOR, false);
}

void uptimePage_enter(void)
{
  drawUptimePage();
  lastUptimeUpdateMs = millis();
}

void uptimePage_loop(unsigned long now)
{
  if (now - lastUptimeUpdateMs >= UPTIME_REFRESH_MS) {
    drawUptimePage();
    lastUptimeUpdateMs = now;
  }
}