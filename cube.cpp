#include "cube.h"
#include "display_buffer.h"

#include <Arduino.h>
#include <math.h>

#define CUBE_FRAME_MS 33
#define CUBE_DRAW_SCALE 35.0f

static const float CUBE_VERTS[8][3] = {
  {-1, -1, -1}, {1, -1, -1}, {1, 1, -1}, {-1, 1, -1},
  {-1, -1,  1}, {1, -1,  1}, {1, 1,  1}, {-1, 1,  1},
};

static const int CUBE_FACES[6][4] = {
  {4, 5, 6, 7},
  {1, 0, 3, 2},
  {5, 1, 2, 6},
  {0, 4, 7, 3},
  {7, 6, 2, 3},
  {5, 4, 0, 1},
};

static const UWORD CUBE_FACE_COLORS[6] = {
  0xF800, 0x07E0, 0x001F, 0xFFE0, 0x07FF, 0xF81F
};

static float cubeAx = 0.5f, cubeAy = 0.8f, cubeAz = 0.2f;
static float cubeSx = 0.030f, cubeSy = 0.020f, cubeSz = 0.015f;
static float cubeTSx = 0.030f, cubeTSy = 0.020f, cubeTSz = 0.015f;

static unsigned long cubeNextRandomMs = 0;
static unsigned long lastCubeFrameMs = 0;

static float cubeRand(float lo, float hi)
{
  return lo + (hi - lo) * ((float)random(10000) / 10000.0f);
}

static void randomizeCubeSpin(void)
{
  cubeTSx = cubeRand(-0.06f, 0.06f);
  cubeTSy = cubeRand(-0.06f, 0.06f);
  cubeTSz = cubeRand(-0.06f, 0.06f);

  if (fabsf(cubeTSx) < 0.012f) cubeTSx = (cubeTSx >= 0.0f) ? 0.02f : -0.02f;
  if (fabsf(cubeTSy) < 0.012f) cubeTSy = (cubeTSy >= 0.0f) ? 0.02f : -0.02f;
  if (fabsf(cubeTSz) < 0.012f) cubeTSz = (cubeTSz >= 0.0f) ? 0.015f : -0.015f;
}

static UWORD shadeRGB565(UWORD color, float factor)
{
  if (factor < 0.0f) factor = 0.0f;
  if (factor > 1.0f) factor = 1.0f;

  int r = (color >> 11) & 0x1F;
  int g = (color >> 5) & 0x3F;
  int b = color & 0x1F;

  r = (int)(r * factor);
  g = (int)(g * factor);
  b = (int)(b * factor);

  return (UWORD)((r << 11) | (g << 5) | b);
}

static void fillQuad(int xs[4], int ys[4], UWORD color)
{
  int miny = ys[0];
  int maxy = ys[0];

  for (int i = 1; i < 4; i++) {
    if (ys[i] < miny) miny = ys[i];
    if (ys[i] > maxy) maxy = ys[i];
  }

  if (miny < 0) miny = 0;
  if (maxy >= LCD_1IN28_HEIGHT) maxy = LCD_1IN28_HEIGHT - 1;

  for (int y = miny; y <= maxy; y++) {
    float inter[4];
    int n = 0;

    for (int i = 0; i < 4; i++) {
      int j = (i + 1) & 3;

      int y1 = ys[i];
      int y2 = ys[j];
      int x1 = xs[i];
      int x2 = xs[j];

      if ((y1 > y) != (y2 > y)) {
        float x = (float)x1 + ((float)(y - y1) * (float)(x2 - x1)) / (float)(y2 - y1);
        inter[n++] = x;
      }
    }

    if (n >= 2) {
      for (int a = 0; a < n - 1; a++) {
        for (int b = a + 1; b < n; b++) {
          if (inter[b] < inter[a]) {
            float t = inter[a];
            inter[a] = inter[b];
            inter[b] = t;
          }
        }
      }

      for (int k = 0; k + 1 < n; k += 2) {
        int xa = (int)(inter[k] + 0.5f);
        int xb = (int)(inter[k + 1] + 0.5f);

        if (xa > xb) {
          int t = xa;
          xa = xb;
          xb = t;
        }

        if (xa < 0) xa = 0;
        if (xb >= LCD_1IN28_WIDTH) xb = LCD_1IN28_WIDTH - 1;

        if (xa <= xb) {
          Paint_DrawLine(xa, y, xb, y, color, DOT_PIXEL_1X1, LINE_STYLE_SOLID);
        }
      }
    }
  }
}

static void updateAndDrawCube(void)
{
  Paint_SelectImage((UBYTE *)BlackImage);
  Paint_Clear(BLACK);

  cubeSx += (cubeTSx - cubeSx) * 0.025f;
  cubeSy += (cubeTSy - cubeSy) * 0.025f;
  cubeSz += (cubeTSz - cubeSz) * 0.025f;

  cubeAx += cubeSx;
  cubeAy += cubeSy;
  cubeAz += cubeSz;

  float ca = cosf(cubeAx), sa = sinf(cubeAx);
  float cb = cosf(cubeAy), sb = sinf(cubeAy);
  float cc = cosf(cubeAz), sc = sinf(cubeAz);

  float rx[8], ry[8], rz[8];
  int px[8], py[8];

  for (int i = 0; i < 8; i++) {
    float x = CUBE_VERTS[i][0];
    float y = CUBE_VERTS[i][1];
    float z = CUBE_VERTS[i][2];

    float y1 = y * ca - z * sa;
    float z1 = y * sa + z * ca;

    float x2 = x * cb + z1 * sb;
    float z2 = -x * sb + z1 * cb;

    float x3 = x2 * cc - y1 * sc;
    float y3 = x2 * sc + y1 * cc;

    rx[i] = x3;
    ry[i] = y3;
    rz[i] = z2;

    float persp = 3.0f / (z2 + 3.0f);

    px[i] = (int)(120.0f + rx[i] * persp * CUBE_DRAW_SCALE);
    py[i] = (int)(120.0f + ry[i] * persp * CUBE_DRAW_SCALE);
  }

  int order[6];
  float faceZ[6];

  for (int f = 0; f < 6; f++) {
    order[f] = f;

    faceZ[f] =
      (rz[CUBE_FACES[f][0]] +
       rz[CUBE_FACES[f][1]] +
       rz[CUBE_FACES[f][2]] +
       rz[CUBE_FACES[f][3]]) * 0.25f;
  }

  for (int i = 0; i < 5; i++) {
    for (int j = i + 1; j < 6; j++) {
      if (faceZ[j] > faceZ[i]) {
        float tz = faceZ[i];
        faceZ[i] = faceZ[j];
        faceZ[j] = tz;

        int to = order[i];
        order[i] = order[j];
        order[j] = to;
      }
    }
  }

  for (int idx = 0; idx < 6; idx++) {
    int f = order[idx];

    int a = CUBE_FACES[f][0];
    int b = CUBE_FACES[f][1];
    int c = CUBE_FACES[f][2];

    float abx = rx[b] - rx[a];
    float aby = ry[b] - ry[a];
    float abz = rz[b] - rz[a];

    float acx = rx[c] - rx[a];
    float acy = ry[c] - ry[a];
    float acz = rz[c] - rz[a];

    float nx = aby * acz - abz * acy;
    float ny = abz * acx - abx * acz;
    float nz = abx * acy - aby * acx;

    float len = sqrtf(nx * nx + ny * ny + nz * nz);

    if (len <= 0.0f) {
      continue;
    }

    float bright = 0.35f + 0.65f * (fabsf(nz) / len);
    UWORD col = shadeRGB565(CUBE_FACE_COLORS[f], bright);

    int xs[4], ys[4];

    for (int k = 0; k < 4; k++) {
      xs[k] = px[CUBE_FACES[f][k]];
      ys[k] = py[CUBE_FACES[f][k]];
    }

    fillQuad(xs, ys, col);
  }

  LCD_1IN28_Display(BlackImage);
}

void cube_enter(void)
{
  randomSeed((unsigned long)millis());

  cubeAx = cubeRand(0.0f, 6.2831f);
  cubeAy = cubeRand(0.0f, 6.2831f);
  cubeAz = cubeRand(0.0f, 6.2831f);

  randomizeCubeSpin();

  cubeSx = cubeTSx;
  cubeSy = cubeTSy;
  cubeSz = cubeTSz;

  cubeNextRandomMs = millis() + 2000 + (unsigned long)random(2000);
  lastCubeFrameMs = 0;

  Paint_SelectImage((UBYTE *)BlackImage);
  Paint_Clear(BLACK);
  LCD_1IN28_Display(BlackImage);
}

void cube_loop(unsigned long now)
{
  if (now - lastCubeFrameMs >= CUBE_FRAME_MS) {
    if ((long)(now - cubeNextRandomMs) >= 0) {
      randomizeCubeSpin();
      cubeNextRandomMs = now + 2000 + (unsigned long)random(2000);
    }

    updateAndDrawCube();
    lastCubeFrameMs = now;
  }
}