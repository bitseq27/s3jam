#pragma once

// Step counter - runs in background, displays overlay every 10s
// Uses QMI8658 accelerometer for step detection

void stepCounter_init(void);
void stepCounter_update(unsigned long now);  // Call every loop iteration
unsigned long stepCounter_getCount(void);
void stepCounter_reset(void);
void stepCounter_drawOverlay(unsigned long now);  // Draw overlay on current framebuffer
