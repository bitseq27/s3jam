#include "starfield.h"
#include "display_buffer.h"

#include <Arduino.h>
#include <math.h>
#include <string.h>

#define STAR_FRAME_MS 33

#define NUM_STARS 5000
#define STAR_Z_MAX 255
#define STAR_Z_MIN 25
#define STAR_SPEED 2
#define STAR_RANGE 60
#define STAR_FOCAL 120
#define STAR_FP_SHIFT 8

#define STAR_FRAMEBUFFER_BYTES ((uint32_t)LCD_1IN28_WIDTH * LCD_1IN28_HEIGHT * 2)

#define STARFIELD_ROTATION_ENABLED 1
#define STAR_ROT_CHANGE_MS 5000
#define STAR_ROT_TAU_MS 1200.0f

struct StarFixed {
  int8_t x;
  int8_t y;
  uint8_t px;
  uint8_t py;
  uint8_t z;
  uint8_t psize;
  bool visible;
};

static StarFixed stars[NUM_STARS];
static uint16_t starInvZ[STAR_Z_MAX + 1];

static bool starLutReady = false;
static bool starfieldActive = false;
static uint32_t starFrameCount = 0;

static unsigned long lastStarFrameMs = 0;
static unsigned long lastStarFrameStartUs = 0;
static unsigned long starFrames = 0;
static unsigned long starFpsLastMs = 0;
static unsigned long lastStarRenderUs = 0;

static float starFps = 0.0f;
static float starCpu = 0.0f;

static float starRotAngle = 0.0f;
static float starRotSpeed = 0.0f;
static float starTargetRotSpeed = 0.0f;
static unsigned long starRotLastChangeMs = 0;
static unsigned long starRotLastFrameMs = 0;

static float randomStarFloat(float lo, float hi)
{
  return lo + (hi - lo) * ((float)random(10000) / 10000.0f);
}

static inline void starPutPixel(int x, int y, uint16_t color)
{
  if ((unsigned)x >= LCD_1IN28_WIDTH || (unsigned)y >= LCD_1IN28_HEIGHT) {
    return;
  }

  uint32_t index = (uint32_t)y * LCD_1IN28_WIDTH + x;

  if (color == 0x0000) {
    BlackImage[index] = 0x0000;
  } else {
    BlackImage[index] = (uint16_t)((color >> 8) | (color << 8));
  }
}

static inline void starFillRect(int x, int y, int w, int h, uint16_t color)
{
  if (x < 0) {
    w += x;
    x = 0;
  }

  if (y < 0) {
    h += y;
    y = 0;
  }

  if (x >= LCD_1IN28_WIDTH || y >= LCD_1IN28_HEIGHT) {
    return;
  }

  if (x + w > LCD_1IN28_WIDTH) {
    w = LCD_1IN28_WIDTH - x;
  }

  if (y + h > LCD_1IN28_HEIGHT) {
    h = LCD_1IN28_HEIGHT - y;
  }

  if (w <= 0 || h <= 0) {
    return;
  }

  if (color == 0x0000) {
    for (int yy = 0; yy < h; yy++) {
      memset(&BlackImage[(uint32_t)(y + yy) * LCD_1IN28_WIDTH + x],
             0,
             (size_t)w * sizeof(uint16_t));
    }
  } else {
    uint16_t swapped = (uint16_t)((color >> 8) | (color << 8));

    for (int yy = 0; yy < h; yy++) {
      for (int xx = 0; xx < w; xx++) {
        BlackImage[(uint32_t)(y + yy) * LCD_1IN28_WIDTH + (x + xx)] = swapped;
      }
    }
  }
}

static inline bool starGetFont16Bit(const uint8_t *glyph, int bytesPerLine, int row, int col)
{
  uint8_t mask = (uint8_t)(0x80 >> (col & 7));
  return (glyph[row * bytesPerLine + (col >> 3)] & mask) != 0;
}

static void starDrawStringDirect(int x, int y, const char *text, uint16_t color)
{
  int len = strlen(text);

  if (len <= 0) {
    return;
  }

  int fontW = Font16.Width;
  int fontH = Font16.Height;
  int bytesPerLine = (fontW + 7) / 8;
  int charSize = fontH * bytesPerLine;

  for (int i = 0; i < len; i++) {
    char c = text[i];

    if (c < ' ') {
      continue;
    }

    const uint8_t *glyph = &Font16.table[(c - ' ') * charSize];
    int gx = x + i * fontW;

    for (int row = 0; row < fontH; row++) {
      for (int col = 0; col < fontW; col++) {
        if (starGetFont16Bit(glyph, bytesPerLine, row, col)) {
          starPutPixel(gx + col, y + row, color);
        }
      }
    }
  }
}

static void starReset(int i, bool randomZ)
{
  stars[i].x = (int8_t)random(-STAR_RANGE, STAR_RANGE + 1);
  stars[i].y = (int8_t)random(-STAR_RANGE, STAR_RANGE + 1);

  if (randomZ) {
    stars[i].z = (uint8_t)random(STAR_Z_MIN, STAR_Z_MAX + 1);
  } else {
    stars[i].z = STAR_Z_MAX;
  }

  stars[i].px = 0;
  stars[i].py = 0;
  stars[i].psize = 1;
  stars[i].visible = false;
}

static void starInitLut(void)
{
  if (starLutReady) {
    return;
  }

  starInvZ[0] = 0;

  for (uint16_t z = 1; z <= STAR_Z_MAX; z++) {
    starInvZ[z] = (uint16_t)((STAR_FOCAL << STAR_FP_SHIFT) / z);
  }

  starLutReady = true;
}

static void starChooseNewRotationTarget(void)
{
  float mag = randomStarFloat(0.00025f, 0.0008f);

  if (random(2) == 0) {
    mag = -mag;
  }

  starTargetRotSpeed = mag;
}

static void starUpdateRotation(unsigned long now)
{
#if STARFIELD_ROTATION_ENABLED
  if (starRotLastChangeMs == 0 ||
      now - starRotLastChangeMs >= STAR_ROT_CHANGE_MS) {
    starChooseNewRotationTarget();
    starRotLastChangeMs = now;
  }

  if (starRotLastFrameMs == 0) {
    starRotLastFrameMs = now;
  }

  float dt = (float)(now - starRotLastFrameMs);

  if (dt < 0.0f) {
    dt = 0.0f;
  }

  if (dt > 100.0f) {
    dt = (float)STAR_FRAME_MS;
  }

  float alpha = 1.0f - expf(-dt / STAR_ROT_TAU_MS);
  starRotSpeed += (starTargetRotSpeed - starRotSpeed) * alpha;

  starRotAngle += starRotSpeed * dt;

  const float twoPi = 6.28318530718f;

  if (starRotAngle > twoPi) {
    starRotAngle -= twoPi;
  } else if (starRotAngle < -twoPi) {
    starRotAngle += twoPi;
  }

  starRotLastFrameMs = now;
#else
  (void)now;
#endif
}

static void starUpdateAndDrawDirect(unsigned long now)
{
  if (!starfieldActive) {
    return;
  }

  // Fast full clear. Prevents ghost streaks.
  memset(BlackImage, 0x00, STAR_FRAMEBUFFER_BYTES);

  starUpdateRotation(now);

  const int centerX = LCD_1IN28_WIDTH / 2;
  const int centerY = LCD_1IN28_HEIGHT / 2;

#if STARFIELD_ROTATION_ENABLED
  float cosA = cosf(starRotAngle);
  float sinA = sinf(starRotAngle);
#endif

  for (int i = 0; i < NUM_STARS; i++) {
    StarFixed &s = stars[i];

    if (s.z <= STAR_Z_MIN + STAR_SPEED) {
      starReset(i, false);
      continue;
    }

    s.z -= STAR_SPEED;

    int32_t ox = (((int32_t)s.x * starInvZ[s.z]) >> STAR_FP_SHIFT);
    int32_t oy = (((int32_t)s.y * starInvZ[s.z]) >> STAR_FP_SHIFT);

    int sx;
    int sy;

#if STARFIELD_ROTATION_ENABLED
    float rx = (float)ox * cosA - (float)oy * sinA;
    float ry = (float)ox * sinA + (float)oy * cosA;

    sx = centerX + (int)rx;
    sy = centerY + (int)ry;
#else
    sx = centerX + ox;
    sy = centerY + oy;
#endif

    if (sx < 0 || sx >= LCD_1IN28_WIDTH ||
        sy < 0 || sy >= LCD_1IN28_HEIGHT) {
      starReset(i, false);
      continue;
    }

    uint8_t size = (s.z < 50) ? 2 : 1;
    uint16_t color;

    if (s.z < 50) {
      color = 0xFFFF;
    } else if (s.z < 100) {
      color = 0xCE59;
    } else if (s.z < 180) {
      color = 0x8410;
    } else {
      color = 0x4208;

      if (((i * 7) + (int)(starFrameCount & 0xFFFF)) % 5 == 0) {
        s.visible = false;
        continue;
      }
    }

    uint16_t tailZ = (uint16_t)s.z + (STAR_SPEED * 4);

    if (tailZ > STAR_Z_MAX) {
      tailZ = STAR_Z_MAX;
    }

    int32_t tox = (((int32_t)s.x * starInvZ[tailZ]) >> STAR_FP_SHIFT);
    int32_t toy = (((int32_t)s.y * starInvZ[tailZ]) >> STAR_FP_SHIFT);

    int tx;
    int ty;

#if STARFIELD_ROTATION_ENABLED
    float trx = (float)tox * cosA - (float)toy * sinA;
    float trY = (float)tox * sinA + (float)toy * cosA;

    tx = centerX + (int)trx;
    ty = centerY + (int)trY;
#else
    tx = centerX + tox;
    ty = centerY + toy;
#endif

    if (tx >= 0 && tx < LCD_1IN28_WIDTH &&
        ty >= 0 && ty < LCD_1IN28_HEIGHT) {
      starPutPixel(tx, ty, 0x8410);
    }

    for (uint8_t dy = 0; dy < size; dy++) {
      for (uint8_t dx = 0; dx < size; dx++) {
        starPutPixel(sx + dx, sy + dy, color);
      }
    }

    s.px = (uint8_t)sx;
    s.py = (uint8_t)sy;
    s.psize = size;
    s.visible = true;
  }

  starFrameCount++;
}

static void starDrawOverlay(float fps, float cpu)
{
  int fpsDisp = (int)fps;
  int cpuDisp = (int)cpu;

  if (fpsDisp < 0) fpsDisp = 0;
  if (fpsDisp > 999) fpsDisp = 999;

  if (cpuDisp < 0) cpuDisp = 0;
  if (cpuDisp > 100) cpuDisp = 100;

  char buf[24];
  snprintf(buf, sizeof(buf), "FPS:%3d CPU:%3d%%", fpsDisp, cpuDisp);

  int len = strlen(buf);
  int textW = len * (int)Font16.Width;
  int textH = (int)Font16.Height;

  int x = ((int)LCD_1IN28_WIDTH - textW) / 2;
  int y = 105;

  if (x < 0) x = 0;
  if (y < 0) y = 0;

  starFillRect(x, y, textW, textH, 0x0000);
  starDrawStringDirect(x, y, buf, 0xFFFF);
}

void starfield_enter(void)
{
  randomSeed((unsigned long)millis());

  starInitLut();

  for (int i = 0; i < NUM_STARS; i++) {
    starReset(i, true);
  }

  memset(BlackImage, 0x00, STAR_FRAMEBUFFER_BYTES);

  starFrames = 0;
  starFps = 0.0f;
  starCpu = 0.0f;
  lastStarRenderUs = 0;
  starFpsLastMs = millis();
  lastStarFrameMs = 0;
  lastStarFrameStartUs = 0;
  starFrameCount = 0;
  starfieldActive = true;

  starRotAngle = 0.0f;
  starRotSpeed = 0.0f;
  starTargetRotSpeed = 0.0f;
  starRotLastChangeMs = millis();
  starRotLastFrameMs = millis();

  starChooseNewRotationTarget();

  LCD_1IN28_Display(BlackImage);
}

void starfield_exit(void)
{
  starfieldActive = false;
}

void starfield_loop(unsigned long now)
{
  if (now - lastStarFrameMs >= STAR_FRAME_MS) {
    if (!starfieldActive) {
      starfield_enter();
    }

    unsigned long frameStartUs = micros();

    if (lastStarFrameStartUs != 0) {
      unsigned long framePeriodUs = frameStartUs - lastStarFrameStartUs;

      if (framePeriodUs > 0) {
        starCpu = ((float)lastStarRenderUs / (float)framePeriodUs) * 100.0f;

        if (!(starCpu >= 0.0f && starCpu <= 100.0f)) {
          starCpu = 0.0f;
        }
      }
    }

    if (now - starFpsLastMs >= 1000) {
      unsigned long elapsed = now - starFpsLastMs;

      starFps = (elapsed > 0) ? ((float)starFrames * 1000.0f / (float)elapsed) : 0.0f;

      starFrames = 0;
      starFpsLastMs = now;
    }

    starUpdateAndDrawDirect(now);
    starDrawOverlay(starFps, starCpu);
    LCD_1IN28_Display(BlackImage);

    starFrames++;

    lastStarRenderUs = micros() - frameStartUs;
    lastStarFrameStartUs = frameStartUs;
    lastStarFrameMs = now;
  }
}