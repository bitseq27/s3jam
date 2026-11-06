#include "city_page.h"
#include "display_buffer.h"
#include "LCD_Test.h"
#include <Arduino.h>
#include <math.h>
#include <string.h>

/* =====================================================================
   ISOMETRIC PIXEL CITY  —  "Habbo / retro sim" style
   Static city baked once into cityLayer; pedestrians + traffic +
   ambient life drawn on top every frame.
   ===================================================================== */

/* ---- iso grid ---- */
#define N   12
#define HW  9          /* tile half width  */
#define HH  5          /* tile half height */
#define OX  120
#define OY  72

/* ---- palette ---- */
#define SKY_TOP   0x7D5B
#define SKY_HOR   0xB69D
#define CLOUD     0xFFFF
#define ASPHALT   0x6328
#define LANE      0xFFE0
#define WATER     0x036B
#define GRASS     0x2E65
#define PAVE      0xAD55
#define FORE_GRASS 0x2E65
#define PROM      0xAD55

#define SKIN  0xFEA0
#define LEGC  0x2104
#define POLE  0x4208
#define LRED  0xF800
#define LGRN  0x07E0
#define WINY  0xFFE0

static const char * const MAP[N] = {
  "GGhRhhttRggG",
  "GGhRhhttRggG",
  "bcbRcchhRgWW",
  "RRRRRRRRRRRR",
  "hhGRhhccRGWW",
  "hhGRhhccRGWy",
  "ttGRgghhRpWW",
  "ttGRgghhRpGG",
  "RRRRRRRRRRRR",
  "bcGRhhGGRbcG",
  "hhpRGGttRGGG",
  "GGGRGGGGRGGG"
};

/* ---- offscreen layer ---- */
static UWORD *cityLayer = NULL;

/* ================= small helpers ================= */
static float randomFloat(float lo, float hi)
{
  return lo + (hi - lo) * ((float)random(10000) / 10000.0f);
}

/* ================= low-level draw (any buffer, swapped RGB565) ================= */
static inline void putBuf(UWORD *b, int x, int y, UWORD c)
{
  if ((unsigned)x >= LCD_1IN28_WIDTH || (unsigned)y >= LCD_1IN28_HEIGHT) return;
  b[(uint32_t)y * LCD_1IN28_WIDTH + x] = (c == 0) ? 0x0000 : (UWORD)((c >> 8) | (c << 8));
}

static void fillHBuf(UWORD *b, int x, int y, int w, UWORD c)
{
  if (y < 0 || y >= LCD_1IN28_HEIGHT) return;
  if (x < 0) { w += x; x = 0; }
  if (x >= LCD_1IN28_WIDTH) return;
  if (x + w > LCD_1IN28_WIDTH) w = LCD_1IN28_WIDTH - x;
  if (w <= 0) return;
  UWORD sc = (c == 0) ? 0x0000 : (UWORD)((c >> 8) | (c << 8));
  UWORD *row = &b[(uint32_t)y * LCD_1IN28_WIDTH + x];
  for (int i = 0; i < w; i++) row[i] = sc;
}

static void lineBuf(UWORD *b, int x0, int y0, int x1, int y1, UWORD c)
{
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    putBuf(b, x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

static void diamondBuf(UWORD *b, int cx, int cy, int hw, int hh, UWORD c)
{
  for (int dy = -hh; dy <= hh; dy++) {
    int w = (int)((float)hw * (1.0f - (float)abs(dy) / (float)hh) + 0.5f);
    fillHBuf(b, cx - w, cy + dy, 2 * w + 1, c);
  }
}

static void leftFaceBuf(UWORD *b, int cx, int cy, int H, int hw, int hh, UWORD c)
{
  float sl = (float)hw / (float)hh;
  for (int y = cy - H; y <= cy + hh; y++) {
    float topE = (cx - hw) + (y - (cy - H)) * sl;
    float botE = (cx - hw) + (y - cy) * sl;
    int lx, rx;
    if (y < cy)            { lx = cx - hw; rx = (int)topE; }
    else if (y < cy + hh - H) { lx = (int)botE; rx = (int)topE; }
    else                   { lx = (int)botE; rx = cx; }
    if (rx >= lx) fillHBuf(b, lx, y, rx - lx + 1, c);
  }
}

static void rightFaceBuf(UWORD *b, int cx, int cy, int H, int hw, int hh, UWORD c)
{
  float sl = (float)hw / (float)hh;
  for (int y = cy - H; y <= cy + hh; y++) {
    float topE = cx - (y - (cy + hh - H)) * sl;
    float botE = cx - (y - (cy + hh)) * sl;
    int lx, rx;
    if (y < cy)            { lx = (int)topE; rx = cx + hw; }
    else if (y < cy + hh - H) { lx = (int)topE; rx = (int)botE; }
    else                   { lx = cx; rx = (int)botE; }
    if (rx >= lx) fillHBuf(b, lx, y, rx - lx + 1, c);
  }
}

static inline int isoXf(float gx, float gy) { return OX + (int)((gx - gy) * HW); }
static inline int isoYf(float gx, float gy) { return OY + (int)((gx + gy) * HH); }

static void isoBoxBuf(UWORD *b, float gx, float gy, int H, int hw, int hh,
                      UWORD top, UWORD left, UWORD right)
{
  int cx = isoXf(gx, gy), cy = isoYf(gx, gy);
  leftFaceBuf(b, cx, cy, H, hw, hh, left);
  rightFaceBuf(b, cx, cy, H, hw, hh, right);
  diamondBuf(b, cx, cy - H, hw, hh, top);
}

static UWORD cityShade(UWORD c, float f)
{
  if (f < 0) f = 0; if (f > 1) f = 1;
  int r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, bl = c & 0x1F;
  return (UWORD)(((int)(r * f) << 11) | ((int)(g * f) << 5) | (int)(bl * f));
}

static UWORD cityLerp(UWORD a, UWORD b, float t)
{
  if (t < 0) t = 0; if (t > 1) t = 1;
  int r1 = (a >> 11) & 0x1F, g1 = (a >> 5) & 0x3F, b1 = a & 0x1F;
  int r2 = (b >> 11) & 0x1F, g2 = (b >> 5) & 0x3F, b2 = b & 0x1F;
  return (UWORD)(((r1 + (int)((r2 - r1) * t)) << 11) |
                 ((g1 + (int)((g2 - g1) * t)) << 5) |
                  (b1 + (int)((b2 - b1) * t)));
}

/* ================= building types ================= */
static void buildingColors(char t, int &H, UWORD &top, UWORD &l, UWORD &r, bool &marq)
{
  marq = false;
  switch (t) {
    case 'h': H = 14; top = 0xBA86; l = 0xEE92; r = 0xBD0D; break;  /* house  */
    case 't': H = 30; top = 0x5AED; l = 0xAD77; r = 0x7BF0; break;  /* tower  */
    case 'c': H = 16; top = 0x5228; l = 0x9470; r = 0x6B2B; marq = true; break; /* casino */
    case 'g': H = 22; top = 0x7DB9; l = 0x4475; r = 0x2AEF; break;  /* glass  */
    case 'b': H = 9;  top = 0xC945; l = 0xFFFF; r = 0xB5B6; break;  /* bar    */
    default:  H = 0;  top = l = r = 0; break;
  }
}

static void drawMarquee(UWORD *b, int cx, int cy, int H)
{
  int y = cy - H / 2;
  for (int k = 1; k <= 6; k++) { putBuf(b, cx - HW + k, y, WINY); putBuf(b, cx - HW + k, y - 1, WINY); }
}

static void drawPalm(UWORD *b, int gx, int gy)
{
  int cx = isoXf(gx, gy), cy = isoYf(gx, gy);
  for (int i = 0; i < 12; i++) { putBuf(b, cx, cy - i, 0x6B45); putBuf(b, cx - 1, cy - i, 0x41A3); }
  int tx = cx, ty = cy - 12;
  lineBuf(b, tx, ty, tx - 7, ty - 2, 0x2E65);
  lineBuf(b, tx, ty, tx + 7, ty - 2, 0x2E65);
  lineBuf(b, tx, ty, tx - 5, ty - 6, 0x37E0);
  lineBuf(b, tx, ty, tx + 5, ty - 6, 0x37E0);
  lineBuf(b, tx, ty, tx,     ty - 8, 0x2E65);
  lineBuf(b, tx, ty, tx - 3, ty + 1, 0x1DC3);
  lineBuf(b, tx, ty, tx + 3, ty + 1, 0x1DC3);
}

static void drawYacht(UWORD *b, int gx, int gy)
{
  int cx = isoXf(gx, gy), cy = isoYf(gx, gy);
  diamondBuf(b, cx, cy, 7, 3, 0xFFFF);
  for (int yy = 0; yy < 3; yy++)        /* cabin: 8 wide x 3 high */
    fillHBuf(b, cx - 4, cy - 4 + yy, 8, 0xFFFF);
  putBuf(b, cx - 2, cy - 3, 0x001F); putBuf(b, cx + 1, cy - 3, 0x001F);
  lineBuf(b, cx, cy - 4, cx, cy - 12, 0xAD55);
  putBuf(b, cx + 1, cy - 10, 0xF800);
}

/* ================= bake the static city ================= */
static void renderCity(void)
{
  /* sky */
  for (int y = 0; y < LCD_1IN28_HEIGHT; y++) fillHBuf(cityLayer, 0, y, LCD_1IN28_WIDTH, SKY_TOP);
  for (int y = 20; y <= 70; y++) {
    UWORD c = cityLerp(SKY_TOP, SKY_HOR, (float)(y - 20) / 50.0f);
    fillHBuf(cityLayer, 0, y, LCD_1IN28_WIDTH, c);
  }
  /* baked clouds */
  diamondBuf(cityLayer, 55, 14, 9, 3, CLOUD);
  diamondBuf(cityLayer, 64, 12, 6, 2, CLOUD);
  diamondBuf(cityLayer, 168, 20, 7, 2, CLOUD);

  /* ground pass */
  for (int gy = 0; gy < N; gy++) {
    for (int gx = 0; gx < N; gx++) {
      char t = MAP[gy][gx];
      UWORD gc;
      switch (t) {
        case 'R': gc = ASPHALT; break;
        case 'W': gc = WATER;   break;
        case 'G': gc = GRASS;   break;
        default:  gc = PAVE;    break;
      }
      diamondBuf(cityLayer, isoXf(gx, gy), isoYf(gx, gy), HW, HH, gc);
      if (t == 'R') { putBuf(cityLayer, isoXf(gx, gy), isoYf(gx, gy), LANE); }
    }
  }

  /* structure pass (back to front) */
  for (int s = 0; s <= 2 * (N - 1); s++) {
    for (int gx = 0; gx < N; gx++) {
      int gy = s - gx;
      if (gy < 0 || gy >= N) continue;
      char t = MAP[gy][gx];
      int H; UWORD top, l, r; bool marq;
      buildingColors(t, H, top, l, r, marq);
      if (H > 0) {
        isoBoxBuf(cityLayer, gx, gy, H, HW, HH, top, l, r);
        if (marq) drawMarquee(cityLayer, isoXf(gx, gy), isoYf(gx, gy), H);
      } else if (t == 'p') {
        drawPalm(cityLayer, gx, gy);
      } else if (t == 'y') {
        drawYacht(cityLayer, gx, gy);
      }
    }
  }

  /* foreground promenade / park */
  for (int y = 186; y < LCD_1IN28_HEIGHT; y++) fillHBuf(cityLayer, 0, y, LCD_1IN28_WIDTH, FORE_GRASS);
  fillHBuf(cityLayer, 0, 186, LCD_1IN28_WIDTH, PROM);
  fillHBuf(cityLayer, 0, 187, LCD_1IN28_WIDTH, PROM);
}

/* ================= moving entities ================= */
struct Car  { float u, speed; int type; int line; UWORD col; };
struct Ped  { float u, speed; int type; int line; float off; UWORD shirt; int phase; };

/* type 0 = horizontal road (line = row), type 1 = vertical road (line = col) */
static Car cars[4] = {
  { 0.00f, 0.0016f, 0, 3, 0xF800 },
  { 0.50f, 0.0019f, 0, 8, 0x07E0 },
  { 0.25f, 0.0017f, 1, 3, 0x001F },
  { 0.75f, 0.0021f, 1, 8, 0xFFE0 }
};

static Ped peds[8] = {
  { 0.10f, 0.0007f, 0, 3,  0.35f, 0xF81F, 0 },
  { 0.60f, 0.0008f, 0, 3, -0.35f, 0x07FF, 1 },
  { 0.30f, 0.0006f, 0, 8,  0.35f, 0xFFFF, 0 },
  { 0.80f, 0.0009f, 0, 8, -0.35f, 0xA010, 1 },
  { 0.20f, 0.0007f, 1, 3,  0.35f, 0x07E0, 0 },
  { 0.70f, 0.0008f, 1, 3, -0.35f, 0xF800, 1 },
  { 0.40f, 0.0006f, 1, 8,  0.35f, 0xFFE0, 0 },
  { 0.90f, 0.0009f, 1, 8, -0.35f, 0x001F, 1 }
};

static float triWave(float u) { float p = u * 2.0f; return (p < 1.0f) ? p : (2.0f - p); }

static void drawCar(UWORD *b, float gx, float gy, UWORD col)
{
  isoBoxBuf(b, gx, gy, 4, 6, 3, cityShade(col, 1.0f), cityShade(col, 0.8f), cityShade(col, 0.6f));
  int cx = isoXf(gx, gy), cy = isoYf(gx, gy);
  putBuf(b, cx - 1, cy - 4, 0xAEFF); putBuf(b, cx, cy - 4, 0xAEFF);   /* windshield */
  putBuf(b, cx - 5, cy + 2, 0x0000); putBuf(b, cx + 4, cy + 2, 0x0000); /* wheels */
  putBuf(b, cx - 5, cy + 3, 0x0000); putBuf(b, cx + 4, cy + 3, 0x0000);
}

static void drawPed(UWORD *b, float gx, float gy, UWORD shirt, int phase)
{
  int sx = isoXf(gx, gy), sy = isoYf(gx, gy);
  putBuf(b, sx, sy - 3, SKIN);                       /* head */
  putBuf(b, sx, sy - 2, shirt); putBuf(b, sx, sy - 1, shirt); /* body */
  int la = phase ? 0 : -1, lb = phase ? 1 : 0;       /* walking legs */
  putBuf(b, sx + la, sy, LEGC); putBuf(b, sx + lb, sy, LEGC);
}

/* ================= ambient overlay life ================= */
static const int TLX[4] = { 3, 8, 3, 8 };
static const int TLY[4] = { 3, 3, 8, 8 };

static void drawOverlay(unsigned long now)
{
  UWORD *b = BlackImage;

  /* traffic lights at the four intersections */
  for (int i = 0; i < 4; i++) {
    int cx = isoXf(TLX[i], TLY[i]), cy = isoYf(TLX[i], TLY[i]);
    putBuf(b, cx, cy - 2, POLE); putBuf(b, cx, cy - 3, POLE); putBuf(b, cx, cy - 4, POLE);
    putBuf(b, cx, cy - 5, (((now / 1500) + i) & 1) ? LGRN : LRED);
  }

  /* flickering tower windows */
  int wx = isoXf(6, 0) - HW + 3, wy = isoYf(6, 0) - 15;
  if ((now / 700) & 1)      { putBuf(b, wx, wy, WINY); putBuf(b, wx + 1, wy, WINY); }
  if (((now / 700) + 1) & 1){ putBuf(b, wx, wy + 5, WINY); putBuf(b, wx + 1, wy + 5, WINY); }

  /* waving rooftop flag on the tall tower */
  int fx = isoXf(6, 0), fy = isoYf(6, 0) - 30;
  for (int p = 0; p < 8; p++) putBuf(b, fx, fy - p, POLE);
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 4; c++)
      if (((c + (int)(now / 120)) % 3) != 0)
        putBuf(b, fx + 1 + c, fy - 7 + r + ((c + (int)(now / 90)) & 1), 0xF800);

  /* drifting cloud */
  int clx = (int)((now / 45) % 300) - 30;
  putBuf(b, clx, 12, CLOUD); putBuf(b, clx + 1, 12, CLOUD); putBuf(b, clx + 2, 12, CLOUD);
  putBuf(b, clx + 1, 11, CLOUD); putBuf(b, clx + 3, 12, CLOUD);

  /* marina shimmer */
  for (int k = 0; k < 3; k++) {
    int wx2 = 160 + k * 15 + (int)(sinf(now * 0.005f + k) * 4.0f);
    int wy2 = 130 + k * 8;
    putBuf(b, wx2, wy2, 0xAEFF); putBuf(b, wx2 + 1, wy2, 0xAEFF);
  }
}

static void drawTicker(unsigned long now)
{
  char buf[24];
  unsigned long secs = now / 1000UL;
  snprintf(buf, sizeof(buf), "CITY  %02lu:%02lu  pop%d",
           (secs / 60) % 60, secs % 60, (int)(sizeof(peds) / sizeof(peds[0])));
  Paint_SelectImage((UBYTE *)BlackImage);
  int len = strlen(buf);
  int tw = len * Font8.Width;
  int tx = (LCD_1IN28_WIDTH - tw) / 2;
  if (tx < 0) tx = 0;
  Paint_DrawString_EN((UWORD)tx, 222, buf, &Font8, WHITE, FORE_GRASS);
}

/* ================= tick ================= */
static unsigned long lastCityFrameMs = 0;

static void cityTick(unsigned long now)
{
  memcpy(BlackImage, cityLayer, Imagesize);

  /* update + draw traffic */
  for (int i = 0; i < 4; i++) {
    cars[i].u += cars[i].speed;
    if (cars[i].u >= 1.0f) cars[i].u -= 1.0f;
    float w = triWave(cars[i].u) * (N - 1);
    float gx, gy;
    if (cars[i].type == 0) { gx = w; gy = cars[i].line; }
    else                   { gy = w; gx = cars[i].line; }
    drawCar(BlackImage, gx, gy, cars[i].col);
  }

  /* update + draw pedestrians */
  for (int i = 0; i < 8; i++) {
    peds[i].u += peds[i].speed;
    if (peds[i].u >= 1.0f) peds[i].u -= 1.0f;
    peds[i].phase = ((int)(peds[i].u * 60.0f)) & 1;
    float w = triWave(peds[i].u) * (N - 1);
    float gx, gy;
    if (peds[i].type == 0) { gx = w; gy = peds[i].line + peds[i].off; }
    else                   { gy = w; gx = peds[i].line + peds[i].off; }
    drawPed(BlackImage, gx, gy, peds[i].shirt, peds[i].phase);
  }

  drawOverlay(now);
  drawTicker(now);

  LCD_1IN28_Display(BlackImage);
}

/* ================= public API ================= */
void cityPage_enter(void)
{
  if (cityLayer == NULL) {
    cityLayer = (UWORD *)ps_malloc(Imagesize);
    if (cityLayer == NULL) cityLayer = (UWORD *)malloc(Imagesize);
  }
  if (cityLayer == NULL) return;

  renderCity();

  /* re-seed entity phases so each visit feels a little different */
  randomSeed((unsigned long)millis());
  for (int i = 0; i < 4; i++) cars[i].u = randomFloat(0.0f, 1.0f);
  for (int i = 0; i < 8; i++) peds[i].u = randomFloat(0.0f, 1.0f);

  lastCityFrameMs = 0;
  cityTick(millis());
}

void cityPage_loop(unsigned long now)
{
  if (cityLayer == NULL) return;
  if (now - lastCityFrameMs < 33) return;   /* ~30 fps */
  lastCityFrameMs = now;
  cityTick(now);
}

void cityPage_exit(void)
{
  /* free the layer so it doesn't sit in RAM while other pages run */
  if (cityLayer != NULL) { free(cityLayer); cityLayer = NULL; }
}