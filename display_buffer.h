#pragma once

#include "LCD_Test.h"
#include <stdint.h>

extern UWORD *BlackImage;
extern uint32_t Imagesize;

void displayBuffer_init(void);

void drawScaledTextPage(const char *text, UWORD color, bool halfSize);
int fitTextScale(const char *text, int maxWidth);
void drawCenteredScaledLine(const char *text, int centerY, int scale, UWORD color);