#include "frog_page.h"
#include "display_buffer.h"
#include "LCD_Test.h"
#include <Arduino.h>
#include <math.h>

extern UWORD *BlackImage;

/* ==================== palette ==================== */
/* frog */
#define FROG_GREEN     0x07E0
#define FROG_OUTLINE   0x03E0
#define FROG_BELLY     0xC618
#define FROG_SPOT      0x0660
#define FROG_FEET      0x06A0
#define FROG_PINK      0xFAB6
#define FROG_TONGUE    0xF813
/* world - day */
#define SKY_DAY_TOP    0xAEFF
#define SKY_DAY_MID    0xBEFF
#define SKY_DAY_HOR    0xCFFF
#define GRASS_DAY      0x2E65
#define GRASS_DAY_DK   0x1DC3
#define GRASS_DAY_LT   0x37E0
#define WATER_DAY      0x46BB
#define WATER_DAY_DK   0x3619
/* world - night */
#define SKY_NIGHT_TOP  0x0001
#define SKY_NIGHT_MID  0x0008
#define SKY_NIGHT_HOR  0x0013
#define GRASS_NIGHT    0x1324
#define GRASS_NIGHT_DK 0x0A63
#define GRASS_NIGHT_LT 0x1B45
#define WATER_NIGHT    0x0014
#define WATER_NIGHT_DK 0x000D
/* sky objects */
#define CLOUD_COLOR    0xFFFF
#define CLOUD_UNDER    0xC7FF
#define MOON_COLOR     0xFFDC
#define MOON_CRATER    0xCE79
#define STAR_COLOR     0xFFFF
#define RIPPLE_COLOR   0xFFFF
/* critters + effects */
#define FLY_COLOR      0x3186
#define WING_COLOR     0xCE79
#define FISH_COLOR     0xFD20
#define FISH_BELLY     0xFEA0
#define NOTE_COLOR     0x5187
#define ZZZ_COLOR      0x44DF
#define BURP_COLOR     0x9E79
#define BLACK_         0x0000
#define WHITE_         0xFFFF

/* ==================== geometry ==================== */
#define FS 4
#define GRID_W 60
#define WATER_Y 44
#define PAD_CX 30
#define PAD_CY 46
#define FROG_BASE_Y 43

/* ==================== states ==================== */
enum FrogState {
  ST_IDLE, ST_BLINK, ST_WINK, ST_YAWN, ST_SMILE, ST_SLEEP,
  ST_LOOK, ST_WHISTLE, ST_SING, ST_LOVE,
  ST_CATCH, ST_EAT, ST_BURP
};
enum EyeMode { EYE_OPEN, EYE_CLOSED, EYE_HALF };

static FrogState st = ST_IDLE;
static unsigned long stStart = 0;
static unsigned long stDur = 2000;
static unsigned long lastFrameMs = 0;
static unsigned long frogFrame = 0;

/* head turning */
static int headDx = 0;
static int headDy = 0;

/* day / night */
static bool isNight = false;
static unsigned long nightEndAt = 0;
static unsigned long nextNightAt = 0;

/* ==================== entities ==================== */
struct Fly    { bool active; float x, y, vx, vy; unsigned long born; };
struct Fish   { bool active; float x0, cx, cy, h; int dir; unsigned long start, dur; bool rip1, rip2; };
struct Note   { bool on; float x, y, dx; int age; };
struct Zzz    { bool on; float x, y; int age; bool big; };
struct Drop   { bool on; float x, y, vx, vy; };
struct Ripple { bool on; int x, y, age, max; bool big; };
struct SkyStar{ int x, y; };
struct Cloud  { int baseX, y, msPerPx; };

static Fly fly;
static Fish fish;
static Note notes[6];
static Zzz zzs[4];
static Drop drops[6];
static Ripple ripples[6];
static SkyStar skyStars[45];
static bool skyStarsInit = false;
static Cloud clouds[3] = { {30, 46, 90}, {140, 78, 140}, {210, 30, 120} };

static unsigned long nextFlyAt = 0;
static unsigned long nextFishAt = 0;
static unsigned long nextRippleAt = 0;
static unsigned long lastNoteAt = 0;
static unsigned long lastZzzAt = 0;
static int zzzToggle = 0;
static int noteToggle = 0;

static bool burping = false;
static unsigned long burpStart = 0;

/* ==================== pixel helpers ==================== */
static inline UWORD swap16(UWORD c) { return (UWORD)((c >> 8) | (c << 8)); }

static inline void putPix(int x, int y, UWORD c)
{
  if ((unsigned)x >= LCD_1IN28_WIDTH || (unsigned)y >= LCD_1IN28_HEIGHT) return;
  BlackImage[y * LCD_1IN28_WIDTH + x] = swap16(c);
}

static void fillRectPx(int x, int y, int w, int h, UWORD c)
{
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x >= LCD_1IN28_WIDTH || y >= LCD_1IN28_HEIGHT) return;
  if (x + w > LCD_1IN28_WIDTH)  w = LCD_1IN28_WIDTH - x;
  if (y + h > LCD_1IN28_HEIGHT) h = LCD_1IN28_HEIGHT - y;
  if (w <= 0 || h <= 0) return;

  UWORD sc = swap16(c);
  for (int yy = 0; yy < h; yy++) {
    UWORD *row = &BlackImage[(y + yy) * LCD_1IN28_WIDTH + x];
    for (int xx = 0; xx < w; xx++) row[xx] = sc;
  }
}

static inline void gpx(int gx, int gy, UWORD c) { fillRectPx(gx * FS, gy * FS, FS, FS, c); }

static void gEllipse(int cx, int cy, int rx, int ry, UWORD c)
{
  for (int dy = -ry; dy <= ry; dy++)
    for (int dx = -rx; dx <= rx; dx++)
      if (dx * dx * ry * ry + dy * dy * rx * rx <= rx * rx * ry * ry)
        gpx(cx + dx, cy + dy, c);
}

static void circleFill(int cx, int cy, int r, UWORD c)
{
  for (int dy = -r; dy <= r; dy++)
    for (int dx = -r; dx <= r; dx++)
      if (dx * dx + dy * dy <= r * r)
        putPix(cx + dx, cy + dy, c);
}

static void circlePx(int cx, int cy, int r, UWORD c)
{
  for (float a = 0.0f; a < 6.2832f; a += 0.22f)
    putPix((int)(cx + cosf(a) * r), (int)(cy + sinf(a) * r), c);
}

static void linePx(int x0, int y0, int x1, int y1, UWORD c, int thick)
{
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    fillRectPx(x0 - thick / 2, y0 - thick / 2, thick, thick, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

static float randomFloat(float lo, float hi)
{
  return lo + (hi - lo) * ((float)random(10000) / 10000.0f);
}

/* ==================== tiny glyph art ==================== */
static const char *heartG[] = {
  ".XX.XX.", "XXXXXXX", "XXXXXXX", ".XXXXX.", "..XXX..", "...X..."
};
static const char *noteG[] = {
  "..X.", "..XX", "..X.", "..X.", "XX..", "XX.."
};
static const char *zG[] = {
  "XXXX", "..X.", ".X..", "XXXX"
};

static void drawGlyph(const char *const *g, int gw, int gh, int x, int y, int s, UWORD c)
{
  for (int r = 0; r < gh; r++)
    for (int col = 0; col < gw; col++)
      if (g[r][col] == 'X')
        fillRectPx(x + col * s, y + r * s, s, s, c);
}

/* ==================== state machine ==================== */
static void setState(FrogState s, unsigned long now, unsigned long dur)
{
  st = s;
  stStart = now;
  stDur = dur;

  /* head returns to center unless this is a look */
  headDx = 0;
  headDy = 0;
  if (s == ST_LOOK) {
    int d = random(4);
    if (d == 0)      headDx = -2;   /* left  */
    else if (d == 1) headDx = 2;    /* right */
    else if (d == 2) headDy = -2;   /* up    */
    else             headDy = 2;    /* down  */
  }
}

static unsigned long stateDur(FrogState s)
{
  switch (s) {
    case ST_IDLE:    return 1800 + random(1500);
    case ST_BLINK:   return 320;
    case ST_WINK:    return 750;
    case ST_YAWN:    return 1900;
    case ST_SMILE:   return 1600;
    case ST_SLEEP:   return 6500 + random(3000);
    case ST_LOOK:    return 1600 + random(1200);
    case ST_WHISTLE: return 3000;
    case ST_SING:    return 4000;
    case ST_LOVE:    return 3400;
    default:         return 2000;
  }
}

static FrogState pickRandomState(void)
{
  int r = random(100);
  if (r < 24) return ST_IDLE;
  if (r < 36) return ST_BLINK;
  if (r < 44) return ST_WINK;
  if (r < 51) return ST_YAWN;
  if (r < 61) return ST_SMILE;
  if (r < 68) return ST_SLEEP;
  if (r < 78) return ST_LOOK;
  if (r < 85) return ST_WHISTLE;
  if (r < 92) return ST_SING;
  return ST_LOVE;
}

static void advanceState(unsigned long now)
{
  switch (st) {
    case ST_CATCH:
      fly.active = false;
      nextFlyAt = now + random(12000, 24000);
      setState(ST_EAT, now, 1300);
      break;
    case ST_EAT:
      burping = true;
      burpStart = now;
      setState(ST_BURP, now, 1500);
      break;
    case ST_BURP:
      burping = false;
      {
        int r = random(3);
        setState(r == 0 ? ST_SMILE : (r == 1 ? ST_BLINK : ST_IDLE), now, 1800);
      }
      break;
    default:
      setState(pickRandomState(), now, stateDur((FrogState)st));
      break;
  }
}

/* ==================== particles ==================== */
static void addRipple(int x, int y, bool big)
{
  for (int i = 0; i < 6; i++) {
    if (!ripples[i].on) {
      ripples[i].on = true;
      ripples[i].x = x;
      ripples[i].y = y;
      ripples[i].age = 0;
      ripples[i].max = big ? 34 : 20;
      ripples[i].big = big;
      return;
    }
  }
}

static void spawnDrops(float x)
{
  int n = 0;
  for (int i = 0; i < 6 && n < 4; i++) {
    if (!drops[i].on) {
      drops[i].on = true;
      drops[i].x = x + randomFloat(-4, 4);
      drops[i].y = WATER_Y * FS - 2;
      drops[i].vx = randomFloat(-0.8f, 0.8f);
      drops[i].vy = randomFloat(-2.6f, -1.4f);
      n++;
    }
  }
}

static void spawnNote(int dirSign)
{
  for (int i = 0; i < 6; i++) {
    if (!notes[i].on) {
      notes[i].on = true;
      notes[i].x = 126 + randomFloat(-2, 4);
      notes[i].y = 126;
      notes[i].dx = 0.55f * dirSign;
      notes[i].age = 0;
      return;
    }
  }
}

static void spawnZzz(void)
{
  for (int i = 0; i < 4; i++) {
    if (!zzs[i].on) {
      zzs[i].on = true;
      zzs[i].x = 152 + randomFloat(-3, 3);
      zzs[i].y = 82;
      zzs[i].age = 0;
      zzs[i].big = (zzzToggle++ % 2) == 0;
      return;
    }
  }
}

/* ==================== fly / fish ==================== */
static void updateFly(unsigned long now)
{
  if (isNight) { fly.active = false; return; }   /* flies sleep at night */

  if (fly.active) {
    if (st != ST_CATCH) {
      fly.vy += randomFloat(-0.09f, 0.09f);
      fly.vx += randomFloat(-0.05f, 0.05f);
      if (fly.vx > 1.2f) fly.vx = 1.2f;
      if (fly.vx < -1.2f) fly.vx = -1.2f;
      if (fly.vy > 0.8f) fly.vy = 0.8f;
      if (fly.vy < -0.8f) fly.vy = -0.8f;
      fly.x += fly.vx;
      fly.y += fly.vy + sinf(now * 0.02f) * 0.35f;
      if (fly.y < 36) fly.y = 36;
      if (fly.y > 150) fly.y = 150;

      if ((st == ST_IDLE || st == ST_SMILE || st == ST_LOOK ||
           st == ST_BLINK || st == ST_WINK) &&
          now - fly.born > 1200 && random(1000) < 9) {
        setState(ST_CATCH, now, 450);
      }
    }
    if (st != ST_CATCH && (fly.x < 8 || fly.x > 232 || now - fly.born > 8000)) {
      fly.active = false;
      nextFlyAt = now + random(9000, 20000);
    }
  } else {
    bool calm = (st != ST_CATCH && st != ST_EAT && st != ST_BURP && st != ST_SLEEP);
    if (now >= nextFlyAt && calm) {
      fly.active = true;
      fly.born = now;
      int dir = random(2) ? 1 : -1;
      fly.x = (dir > 0) ? 8 : 232;
      fly.y = randomFloat(45, 135);
      fly.vx = dir * randomFloat(0.5f, 0.9f);
      fly.vy = randomFloat(-0.4f, 0.4f);
    }
  }
}

static void updateFish(unsigned long now)
{
  int waterYpx = WATER_Y * FS;

  if (fish.active) {
    float t = (now - fish.start) / (float)fish.dur;
    if (t >= 1.0f) {
      fish.active = false;
      nextFishAt = now + random(10000, 22000);
    } else {
      fish.cx = fish.x0 + fish.dir * (t - 0.5f) * 36.0f;
      fish.cy = waterYpx - sinf(t * 3.14159f) * fish.h;
      if (t > 0.06f && !fish.rip1) { fish.rip1 = true; addRipple((int)fish.x0, waterYpx + 6, false); }
      if (t > 0.88f && !fish.rip2) { fish.rip2 = true; addRipple((int)fish.cx, waterYpx + 4, true); spawnDrops(fish.cx); }
    }
  } else if (now >= nextFishAt) {
    fish.active = true;
    fish.x0 = randomFloat(30, 210);
    fish.cx = fish.x0;
    fish.cy = waterYpx;
    fish.dir = random(2) ? 1 : -1;
    fish.h = randomFloat(38, 66);
    fish.dur = 1100;
    fish.start = now;
    fish.rip1 = false;
    fish.rip2 = false;
  }
}

static void updateAmbient(unsigned long now)
{
  if (now >= nextRippleAt) {
    addRipple(random(20, 220), WATER_Y * FS + random(8, 52), false);
    nextRippleAt = now + random(1200, 2800);
  }
  for (int i = 0; i < 6; i++) {
    if (ripples[i].on) { ripples[i].age++; if (ripples[i].age > ripples[i].max) ripples[i].on = false; }
    if (drops[i].on) {
      drops[i].x += drops[i].vx;
      drops[i].y += drops[i].vy;
      drops[i].vy += 0.18f;
      if (drops[i].y >= WATER_Y * FS) { drops[i].on = false; addRipple((int)drops[i].x, WATER_Y * FS + 4, false); }
    }
  }
}

static void updateParticles(unsigned long now)
{
  if ((st == ST_WHISTLE || st == ST_SING) && now - lastNoteAt >= 380) {
    lastNoteAt = now;
    if (st == ST_SING) noteToggle++;
    spawnNote(st == ST_WHISTLE ? 1 : (noteToggle % 2 ? 1 : -1));
  }
  if (st == ST_SLEEP && now - lastZzzAt >= 700) { lastZzzAt = now; spawnZzz(); }

  for (int i = 0; i < 6; i++) {
    if (notes[i].on) {
      notes[i].y -= 1.1f;
      notes[i].x += notes[i].dx + sinf(notes[i].age * 0.25f) * 0.5f;
      notes[i].age++;
      if (notes[i].age > 70) notes[i].on = false;
    }
  }
  for (int i = 0; i < 4; i++) {
    if (zzs[i].on) { zzs[i].y -= 0.5f; zzs[i].x += 0.3f; zzs[i].age++; if (zzs[i].age > 90) zzs[i].on = false; }
  }
}

/* ==================== world drawing ==================== */
static int grassTopAt(int gx) { return 38 + ((gx * 13 + 5) % 6); }   /* 38..43 */

static void initSkyStars(void)
{
  if (skyStarsInit) return;
  for (int i = 0; i < 45; i++) {
    skyStars[i].x = random(4, 236);
    skyStars[i].y = random(6, 138);
  }
  skyStarsInit = true;
}

static void drawCloud(int x, int y)
{
  fillRectPx(x, y, 28, 8, CLOUD_COLOR);
  fillRectPx(x + 6, y - 6, 18, 6, CLOUD_COLOR);
  fillRectPx(x + 10, y - 10, 10, 4, CLOUD_COLOR);
  fillRectPx(x + 2, y + 6, 24, 3, CLOUD_UNDER);
}

static void drawMoon(void)
{
  circleFill(185, 42, 13, MOON_COLOR);
  circleFill(180, 38, 3, MOON_CRATER);
  circleFill(190, 46, 2, MOON_CRATER);
  circleFill(186, 49, 2, MOON_CRATER);
}

static void drawSkyAndGround(unsigned long now)
{
  int waterPx = WATER_Y * FS;   /* 176 */

  /* layered sky */
  if (isNight) {
    fillRectPx(0, 0, 240, 80, SKY_NIGHT_TOP);
    fillRectPx(0, 80, 240, 50, SKY_NIGHT_MID);
    fillRectPx(0, 130, 240, waterPx - 130, SKY_NIGHT_HOR);
  } else {
    fillRectPx(0, 0, 240, 88, SKY_DAY_TOP);
    fillRectPx(0, 88, 240, 52, SKY_DAY_MID);
    fillRectPx(0, 140, 240, waterPx - 140, SKY_DAY_HOR);
  }

  /* sky life */
  if (isNight) {
    for (int i = 0; i < 45; i++) {
      if (((frogFrame + i * 3) % 8) != 0)
        fillRectPx(skyStars[i].x, skyStars[i].y, 2, 2, STAR_COLOR);
    }
    drawMoon();
  } else {
    for (int i = 0; i < 3; i++) {
      int cx = (clouds[i].baseX + (int)(now / clouds[i].msPerPx)) % 300 - 30;
      drawCloud(cx, clouds[i].y);
    }
  }

  /* grass bank (jagged top edge) */
  for (int gx = 0; gx < GRID_W; gx++) {
    int topPx = grassTopAt(gx) * FS;
    int h = waterPx - topPx;
    if (h <= 0) continue;
    UWORD base = isNight ? GRASS_NIGHT : GRASS_DAY;
    UWORD dk   = isNight ? GRASS_NIGHT_DK : GRASS_DAY_DK;
    UWORD lt   = isNight ? GRASS_NIGHT_LT : GRASS_DAY_LT;
    fillRectPx(gx * FS, topPx, FS, h, base);
    fillRectPx(gx * FS, topPx, FS, FS, lt);              /* sunlit tip */
    if ((gx * 7) % 3 == 0) fillRectPx(gx * FS, topPx + 2 * FS, FS, FS, dk);
  }

  /* pond */
  UWORD wc  = isNight ? WATER_NIGHT : WATER_DAY;
  UWORD wdk = isNight ? WATER_NIGHT_DK : WATER_DAY_DK;
  fillRectPx(0, waterPx, 240, 240 - waterPx, wc);
  fillRectPx(0, waterPx, 240, 2, wdk);

  /* drifting white glints on the surface */
  for (int i = 0; i < 8; i++) {
    if ((((now / 300) + i) % 3) == 0) {
      int sx = (i * 53 + (int)(now / 45) * 7) % 240;
      int sy = waterPx + 8 + (i * 29) % 48;
      fillRectPx(sx, sy, 5, 1, RIPPLE_COLOR);
    }
  }
}

static void drawRipples(void)
{
  for (int i = 0; i < 6; i++) {
    if (!ripples[i].on) continue;
    float rx = 3 + ripples[i].age * (ripples[i].big ? 1.5f : 0.9f);
    float ry = rx / 3.0f;
    bool fade = ripples[i].age > ripples[i].max * 2 / 3;
    int k = 0;
    for (float a = 0.0f; a < 6.2832f; a += 0.18f) {
      if (fade && (k++ % 2)) continue;
      putPix((int)(ripples[i].x + cosf(a) * rx),
             (int)(ripples[i].y + sinf(a) * ry), RIPPLE_COLOR);
    }
  }
}

/* ==================== frog drawing ==================== */
static void drawEye(int cx, int cy, int mode, int pdx, int pdy)
{
  gEllipse(cx, cy, 4, 4, FROG_OUTLINE);

  if (mode == EYE_CLOSED) {
    gEllipse(cx, cy, 3, 3, FROG_GREEN);
    for (int d = -2; d <= 2; d++) gpx(cx + d, cy + 1, FROG_OUTLINE);
    return;
  }

  gEllipse(cx, cy, 3, 3, WHITE_);

  if (mode == EYE_HALF) {
    for (int r = -3; r <= -1; r++)
      for (int c = -2; c <= 2; c++)
        gpx(cx + c, cy + r, FROG_GREEN);
    pdy = 1;
  }

  int x = cx - 1 + pdx;
  int y = cy - 1 + pdy;
  gpx(x, y, BLACK_);
  gpx(x + 1, y, BLACK_);
  gpx(x, y + 1, BLACK_);
  gpx(x + 1, y + 1, BLACK_);
}

static void drawFrog(unsigned long now, int bob)
{
  int by = FROG_BASE_Y + bob;
  int hx = 30 + headDx;            /* head follows the turn */
  int hy = by - 14 + headDy;
  int my = by - 10 + headDy;
  int ey = by - 20 + headDy;
  int ex1 = 23 + headDx;
  int ex2 = 37 + headDx;

  /* feet */
  gEllipse(19, by + 1, 3, 2, FROG_FEET);
  gEllipse(41, by + 1, 3, 2, FROG_FEET);

  /* body */
  gEllipse(30, by - 6, 13, 8, FROG_OUTLINE);
  gEllipse(30, by - 6, 12, 7, FROG_GREEN);
  gEllipse(30, by - 4, 8, 4, FROG_BELLY);
  gpx(24, by - 8, FROG_SPOT);
  gpx(36, by - 7, FROG_SPOT);

  /* head (offset by the turn) */
  gEllipse(hx, hy, 11, 8, FROG_OUTLINE);
  gEllipse(hx, hy, 10, 7, FROG_GREEN);

  /* pupil direction: follow the turn, or track the fly */
  int pdx = 0, pdy = 0;
  if (st == ST_LOOK) {
    pdx = headDx;
    pdy = headDy;
  } else if (fly.active && !isNight && st != ST_SLEEP) {
    pdx = constrain((int)((fly.x - 122) / 50), -2, 2);
    pdy = constrain((int)((fly.y - 94) / 50), -2, 2);
  }

  /* eye modes per state */
  int modeL = EYE_OPEN, modeR = EYE_OPEN;
  switch (st) {
    case ST_SLEEP:
    case ST_BLINK:
    case ST_YAWN:
      modeL = modeR = EYE_CLOSED;
      break;
    case ST_WINK:
      modeL = EYE_CLOSED;
      break;
    case ST_LOVE:
      modeL = modeR = EYE_HALF;
      break;
    default:
      break;
  }

  drawEye(ex1, ey, modeL, pdx, pdy);
  drawEye(ex2, ey, modeR, pdx, pdy);

  /* mouth per state */
  switch (st) {
    case ST_YAWN:
      gEllipse(hx, my + 1, 3, 3, FROG_OUTLINE);
      gEllipse(hx, my + 2, 2, 1, FROG_TONGUE);
      break;
    case ST_WHISTLE:
      gpx(hx - 1, my, FROG_OUTLINE);
      gpx(hx + 1, my, FROG_OUTLINE);
      gpx(hx, my - 1, FROG_OUTLINE);
      gpx(hx, my + 1, FROG_OUTLINE);
      break;
    case ST_SING:
      if ((now / 130) % 2) {
        gEllipse(hx, my + 1, 3, 2, FROG_OUTLINE);
        gEllipse(hx, my + 1, 2, 1, FROG_TONGUE);
      } else {
        for (int d = -3; d <= 3; d++) gpx(hx + d, my, FROG_OUTLINE);
      }
      break;
    case ST_EAT:
      if ((now / 110) % 2) gEllipse(hx, my + 1, 2, 2, FROG_OUTLINE);
      else for (int d = -2; d <= 2; d++) gpx(hx + d, my, FROG_OUTLINE);
      break;
    case ST_CATCH:
    case ST_BURP:
      gEllipse(hx, my + 1, 2, 2, FROG_OUTLINE);
      break;
    case ST_SMILE:
      gpx(hx - 4, my, FROG_OUTLINE);
      for (int d = -3; d <= 3; d++) gpx(hx + d, my + 1, FROG_OUTLINE);
      gpx(hx + 4, my, FROG_OUTLINE);
      break;
    default:
      for (int d = -3; d <= 3; d++) gpx(hx + d, my, FROG_OUTLINE);
      break;
  }

  /* rosy cheeks */
  if (st == ST_SMILE || st == ST_LOVE || st == ST_EAT || st == ST_BURP) {
    gpx(hx - 9, by - 11 + headDy, FROG_PINK);
    gpx(hx - 8, by - 11 + headDy, FROG_PINK);
    gpx(hx + 8, by - 11 + headDy, FROG_PINK);
    gpx(hx + 9, by - 11 + headDy, FROG_PINK);
  }
}

/* ==================== scene ==================== */
static void drawScene(unsigned long now)
{
  Paint_SelectImage((UBYTE *)BlackImage);

  int waterYpx = WATER_Y * FS;
  int bob = (int)(sinf(now * 0.0021f) * 1.4f);
  int padCy = PAD_CY + bob;
  int by = FROG_BASE_Y + bob;

  /* background world (sky, clouds/stars/moon, grass, pond) */
  drawSkyAndGround(now);

  /* white ripples on the surface */
  drawRipples();

  /* lily pad */
  gEllipse(PAD_CX, padCy, 17, 5, GRASS_DAY_DK);
  gEllipse(PAD_CX, padCy, 16, 4, isNight ? GRASS_NIGHT : GRASS_DAY);
  for (int dx = 3; dx <= 15; dx++) {
    int w = dx / 4;
    for (int dy = -w; dy <= w; dy++) gpx(PAD_CX + dx, padCy + dy, isNight ? WATER_NIGHT : WATER_DAY);
  }
  gpx(PAD_CX - 5, padCy, GRASS_DAY_DK);
  gpx(PAD_CX - 9, padCy - 1, GRASS_DAY_DK);
  gpx(PAD_CX - 9, padCy + 1, GRASS_DAY_DK);
  gpx(PAD_CX - 13, padCy, GRASS_DAY_DK);

  /* the frog */
  drawFrog(now, bob);

  /* tongue mid-catch */
  if (st == ST_CATCH && fly.active) {
    float t = (now - stStart) / 450.0f;
    if (t > 1.0f) t = 1.0f;
    int mouthX = (30 + headDx) * FS + 2;
    int mouthY = (by - 10 + headDy) * FS + 2;
    int tipX = (int)(mouthX + (fly.x - mouthX) * t);
    int tipY = (int)(mouthY + (fly.y - mouthY) * t);
    linePx(mouthX, mouthY, tipX, tipY, FROG_TONGUE, 3);
    fillRectPx(tipX - 2, tipY - 2, 5, 5, FROG_TONGUE);
  }

  /* the fly */
  if (fly.active) {
    int fx = (int)fly.x, fy = (int)fly.y;
    fillRectPx(fx, fy, 4, 3, FLY_COLOR);
    if ((now / 60) % 2) {
      putPix(fx, fy - 2, WING_COLOR);
      putPix(fx + 3, fy - 2, WING_COLOR);
    } else {
      putPix(fx + 1, fy - 2, WING_COLOR);
      putPix(fx + 2, fy - 2, WING_COLOR);
    }
  }

  /* jumping fish — FOREGROUND, in front of the frog */
  if (fish.active && fish.cy < waterYpx - 1) {
    int fx = (int)fish.cx, fy = (int)fish.cy;
    int tailX = (fish.dir > 0) ? fx - 3 : fx + 10;
    int headX = (fish.dir > 0) ? fx + 8 : fx + 1;
    fillRectPx(tailX, fy - 2, 3, 8, FISH_COLOR);
    fillRectPx(fx, fy, 10, 4, FISH_COLOR);
    fillRectPx(fx + 1, fy + 2, 8, 1, FISH_BELLY);
    putPix(headX, fy + 1, BLACK_);
  }

  /* splash droplets (also foreground) */
  for (int i = 0; i < 6; i++)
    if (drops[i].on)
      fillRectPx((int)drops[i].x, (int)drops[i].y, 2, 2, RIPPLE_COLOR);

  /* floating music notes */
  for (int i = 0; i < 6; i++)
    if (notes[i].on)
      drawGlyph(noteG, 4, 6, (int)notes[i].x, (int)notes[i].y, 2, NOTE_COLOR);

  /* z z Z Z */
  for (int i = 0; i < 4; i++)
    if (zzs[i].on)
      drawGlyph(zG, 4, 4, (int)zzs[i].x, (int)zzs[i].y, zzs[i].big ? 3 : 2, ZZZ_COLOR);

  /* love heart */
  if (st == ST_LOVE) {
    int hy = 54 + (int)(sinf(now * 0.006f) * 3);
    UWORD hc = ((now / 350) % 2) ? FROG_PINK : 0xFC9F;
    drawGlyph(heartG, 7, 6, 110, hy, 3, hc);
  }

  /* burp cloud + text */
  if (burping && now - burpStart < 1200) {
    unsigned long age = now - burpStart;
    int r = 2 + (int)(age / 250);
    bool fade = age > 800;
    circlePx(136, 114, r, BURP_COLOR);
    circlePx(148, 102, r + 2, BURP_COLOR);
    if (!fade) circlePx(140, 90, r + 1, BURP_COLOR);
    Paint_DrawString_EN(118, 74, "burp!", &Font8, BURP_COLOR, isNight ? SKY_NIGHT_HOR : SKY_DAY_HOR);
  }

  LCD_1IN28_Display(BlackImage);
}

/* ==================== tick / public API ==================== */
static void frogTick(unsigned long now)
{
  /* night falls... */
  if (!isNight && now >= nextNightAt) {
    isNight = true;
    nightEndAt = now + random(15000, 26000);
    fly.active = false;
    setState(ST_SLEEP, now, 60000);
  }
  /* ...and lifts */
  if (isNight && now >= nightEndAt) {
    isNight = false;
    nextNightAt = now + random(45000, 90000);
    setState(ST_IDLE, now, 2000);
  }

  if (isNight) {
    if (st != ST_SLEEP) setState(ST_SLEEP, now, 60000);
  } else {
    if (now - stStart >= stDur) advanceState(now);
  }

  updateFly(now);
  updateFish(now);
  updateAmbient(now);
  updateParticles(now);
  drawScene(now);

  frogFrame++;
}

void frogPage_enter(void)
{
  randomSeed((unsigned long)(millis() ^ 0x5F37));

  initSkyStars();

  fly.active = false;
  fish.active = false;
  burping = false;
  isNight = false;
  for (int i = 0; i < 6; i++) { notes[i].on = false; drops[i].on = false; ripples[i].on = false; }
  for (int i = 0; i < 4; i++) zzs[i].on = false;

  unsigned long now = millis();
  nextFlyAt = now + random(7000, 15000);
  nextFishAt = now + random(3000, 9000);
  nextRippleAt = now + 800;
  nextNightAt = now + random(30000, 70000);
  lastNoteAt = now;
  lastZzzAt = now;

  setState(ST_IDLE, now, 2200);
  lastFrameMs = 0;

  frogTick(now);
}

void frogPage_loop(unsigned long now)
{
  if (now - lastFrameMs < 33) return;   /* ~30 fps */
  lastFrameMs = now;
  frogTick(now);
}