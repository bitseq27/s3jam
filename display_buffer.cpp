#include "display_buffer.h"
#include "esp_heap_caps.h"

#include <Arduino.h>
#include <string.h>

#ifndef MALLOC_CAP_DMA
#define MALLOC_CAP_DMA 0
#endif

UWORD *BlackImage = nullptr;
uint32_t Imagesize = LCD_1IN28_HEIGHT * LCD_1IN28_WIDTH * 2;

static bool getFont24Bit(const uint8_t *glyph, int bytesPerLine, int row, int col)
{
  uint8_t mask = (uint8_t)(0x80 >> (col & 7));
  return (glyph[row * bytesPerLine + (col >> 3)] & mask) != 0;
}

static void drawScaledFont24Char(char c, int originX, int originY, int scale, UWORD color)
{
  if (c < ' ') {
    return;
  }

  int bytesPerLine = (Font24.Width + 7) / 8;
  int charIndex = c - ' ';
  int charSize = Font24.Height * bytesPerLine;

  const uint8_t *glyph = &Font24.table[charIndex * charSize];

  for (int row = 0; row < Font24.Height; row++) {
    for (int col = 0; col < Font24.Width; col++) {
      if (getFont24Bit(glyph, bytesPerLine, row, col)) {
        int x0 = originX + col * scale;
        int y0 = originY + row * scale;

        for (int yy = 0; yy < scale; yy++) {
          int y = y0 + yy;

          if (y < 0 || y >= LCD_1IN28_HEIGHT) {
            continue;
          }

          for (int xx = 0; xx < scale; xx++) {
            int x = x0 + xx;

            if (x < 0 || x >= LCD_1IN28_WIDTH) {
              continue;
            }

            Paint_SetPixel((UWORD)x, (UWORD)y, color);
          }
        }
      }
    }
  }
}

void drawScaledTextPage(const char *text, UWORD color, bool halfSize)
{
  Paint_SelectImage((UBYTE *)BlackImage);
  Paint_Clear(BLACK);

  int len = strlen(text);

  if (len == 0) {
    LCD_1IN28_Display(BlackImage);
    return;
  }

  int fontW = Font24.Width;
  int fontH = Font24.Height;
  int bytesPerLine = (fontW + 7) / 8;
  int charSize = fontH * bytesPerLine;

  int minCol = 999999;
  int maxCol = -1;
  int minRow = 999999;
  int maxRow = -1;

  for (int i = 0; i < len; i++) {
    char c = text[i];

    if (c < ' ') {
      continue;
    }

    const uint8_t *glyph = &Font24.table[(c - ' ') * charSize];

    for (int row = 0; row < fontH; row++) {
      for (int col = 0; col < fontW; col++) {
        if (getFont24Bit(glyph, bytesPerLine, row, col)) {
          int absCol = i * fontW + col;

          if (absCol < minCol) minCol = absCol;
          if (absCol > maxCol) maxCol = absCol;

          if (row < minRow) minRow = row;
          if (row > maxRow) maxRow = row;
        }
      }
    }
  }

  if (maxCol < 0) {
    minCol = 0;
    maxCol = len * fontW - 1;
    minRow = 0;
    maxRow = fontH - 1;
  }

  int cropW = maxCol - minCol + 1;
  int cropH = maxRow - minRow + 1;

  int availW = LCD_1IN28_WIDTH;
  int availH = LCD_1IN28_HEIGHT;

  int scaleW = availW / cropW;
  int scaleH = availH / cropH;

  int scale = (scaleW < scaleH) ? scaleW : scaleH;

  if (halfSize) {
    scale = scale / 2;
  }

  if (scale < 1) {
    scale = 1;
  }

  int drawW = cropW * scale;
  int drawH = cropH * scale;

  int targetX = (LCD_1IN28_WIDTH - drawW) / 2;
  int targetY = (LCD_1IN28_HEIGHT - drawH) / 2;

  int originX = targetX - minCol * scale;
  int originY = targetY - minRow * scale;

  for (int i = 0; i < len; i++) {
    int charX = originX + i * fontW * scale;
    drawScaledFont24Char(text[i], charX, originY, scale, color);
  }

  LCD_1IN28_Display(BlackImage);
}

int fitTextScale(const char *text, int maxWidth)
{
  int len = strlen(text);

  if (len == 0) {
    return 1;
  }

  for (int s = 6; s >= 1; s--) {
    if (len * (int)Font24.Width * s <= maxWidth) {
      return s;
    }
  }

  return 1;
}

void drawCenteredScaledLine(const char *text, int centerY, int scale, UWORD color)
{
  int len = strlen(text);
  int textW = len * (int)Font24.Width * scale;
  int textH = (int)Font24.Height * scale;

  int x = ((int)LCD_1IN28_WIDTH - textW) / 2;
  int y = centerY - textH / 2;

  if (x < 0) x = 0;
  if (y < 0) y = 0;

  for (int i = 0; i < len; i++) {
    drawScaledFont24Char(text[i], x + i * (int)Font24.Width * scale, y, scale, color);
  }
}

void displayBuffer_init(void)
{
  if (psramInit()) {
    Serial.println("PSRAM is correctly initialized");
  } else {
    Serial.println("PSRAM not available");
  }

  BlackImage = (UWORD *)heap_caps_malloc(
    Imagesize,
    MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA | MALLOC_CAP_8BIT
  );

  if (BlackImage == NULL) {
    BlackImage = (UWORD *)ps_malloc(Imagesize);
  }

  if (BlackImage == NULL) {
    BlackImage = (UWORD *)malloc(Imagesize);
  }

  if (BlackImage == NULL) {
    Serial.println("Failed to apply for black memory...");
    exit(0);
  }

  if (DEV_Module_Init() != 0) {
    Serial.println("GPIO Init Fail!");
  } else {
    Serial.println("GPIO Init successful!");
  }

  // DEV_SET_Backlight(50); // uncomment for 50% backlight brightness

  LCD_1IN28_Init(HORIZONTAL);
  LCD_1IN28_Clear(WHITE);

  Paint_NewImage((UBYTE *)BlackImage, LCD_1IN28.WIDTH, LCD_1IN28.HEIGHT, 0, WHITE);
  Paint_SetScale(65);
  Paint_SetRotate(ROTATE_0);
  Paint_Clear(WHITE);
}