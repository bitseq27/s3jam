#include "step_counter.h"
#include "display_buffer.h"
#include "QMI8658.h"
#include "LCD_Test.h"
#include <math.h>

// Configuration - thresholds in milli-g (mg)
#define STEP_SAMPLE_INTERVAL_MS    20      // 50 Hz sampling
#define STEP_THRESHOLD_HIGH      1150.0f // 1.15g in mg - peak detection threshold
#define STEP_THRESHOLD_LOW       1020.0f // 1.02g in mg - valley threshold
#define STEP_MIN_INTERVAL_MS     250     // minimum ms between steps

// Debug output interval
#define DEBUG_INTERVAL_MS        1000    // Print debug every 1 second

// Internal state
static unsigned long stepCount = 0;
static unsigned long lastSampleMs = 0;
static unsigned long lastStepMs = 0;
static unsigned long lastDebugMs = 0;
static float filteredMag = 1000.0f;  // Start at 1g (1000mg)

// Peak detection state machine
enum StepState { WAITING_FOR_PEAK, PEAK_DETECTED, RECOVERING };
static StepState stepState = WAITING_FOR_PEAK;
static float peakValue = 0;
static bool imuAvailable = false;

void stepCounter_init(void) {
    stepCount = 0;
    lastSampleMs = 0;
    lastStepMs = 0;
    lastDebugMs = 0;
    filteredMag = 1000.0f;
    stepState = WAITING_FOR_PEAK;
    peakValue = 0;
    imuAvailable = true;
    Serial.println("Step counter initialized");
}

void stepCounter_update(unsigned long now) {
    if (!imuAvailable) return;
    
    // Rate-limit sampling
    if (now - lastSampleMs < STEP_SAMPLE_INTERVAL_MS) return;
    lastSampleMs = now;
    
    // Read accelerometer (values in mg - milli-g)
    float acc[3];
    float gyro[3];
    unsigned int tim_count;
    QMI8658_read_xyz(acc, gyro, &tim_count);
    
    // Compute magnitude of acceleration vector (in mg)
    float mag = sqrtf(acc[0]*acc[0] + acc[1]*acc[1] + acc[2]*acc[2]);
    
    // Low-pass filter to smooth out noise
    filteredMag = 0.35f * mag + 0.65f * filteredMag;
    
    // Debug output every second
    if (now - lastDebugMs > DEBUG_INTERVAL_MS) {
        Serial.printf("Accel: %.0fmg | Filtered: %.0fmg | State: %d | Steps: %lu\n", 
                      mag, filteredMag, stepState, stepCount);
        lastDebugMs = now;
    }
    
    // Peak detection state machine
    switch (stepState) {
        case WAITING_FOR_PEAK:
            if (filteredMag > STEP_THRESHOLD_HIGH) {
                stepState = PEAK_DETECTED;
                peakValue = filteredMag;
            }
            break;
            
        case PEAK_DETECTED:
            if (filteredMag > peakValue) {
                peakValue = filteredMag;
            } else if (filteredMag < STEP_THRESHOLD_LOW) {
                if (now - lastStepMs > STEP_MIN_INTERVAL_MS) {
                    stepCount++;
                    lastStepMs = now;
                    Serial.printf(">>> STEP #%lu detected (peak: %.0fmg)\n", stepCount, peakValue);
                }
                stepState = RECOVERING;
            }
            break;
            
        case RECOVERING:
            if (filteredMag < STEP_THRESHOLD_LOW * 0.95f) {
                stepState = WAITING_FOR_PEAK;
            }
            break;
    }
}

unsigned long stepCounter_getCount(void) {
    return stepCount;
}

void stepCounter_reset(void) {
    stepCount = 0;
    stepState = WAITING_FOR_PEAK;
    Serial.println("Step counter reset");
}

void stepCounter_drawOverlay(unsigned long now) {
    char buf[32];
    sprintf(buf, "Steps:%lu", stepCount);
    
    int textWidth = strlen(buf) * Font16.Width;
    int textX = (240 - textWidth) / 2;
    int textY = 10;
    
    int boxX1 = textX - 3;
    int boxY1 = textY - 1;
    int boxX2 = textX + textWidth + 3;
    int boxY2 = textY + Font16.Height + 1;
    
    // Ensure bounds
    if (boxX1 < 0) boxX1 = 0;
    if (boxY1 < 0) boxY1 = 0;
    if (boxX2 > 240) boxX2 = 240;
    if (boxY2 > 240) boxY2 = 240;
    
    // Direct pixel manipulation - no Paint library calls
    // This is safe and won't corrupt the graphics state
    
    // 1. Draw black background rectangle directly to BlackImage
    for (int y = boxY1; y < boxY2; y++) {
        for (int x = boxX1; x < boxX2; x++) {
            BlackImage[y * 240 + x] = 0x0000; // BLACK in RGB565
        }
    }
    
    // 2. Draw text using the font data directly
    const char* p = buf;
    int currentX = textX;
    int currentY = textY;
    
    while (*p != '\0') {
        // Get character bitmap
        uint32_t charOffset = (*p - ' ') * Font16.Height * (Font16.Width / 8 + (Font16.Width % 8 ? 1 : 0));
        const unsigned char* charData = &Font16.table[charOffset];
        
        // Draw each pixel of the character
        for (int row = 0; row < Font16.Height; row++) {
            for (int col = 0; col < Font16.Width; col++) {
                // Check if this pixel is set in the font bitmap
                if (charData[row * ((Font16.Width + 7) / 8) + (col / 8)] & (0x80 >> (col % 8))) {
                    // Draw red pixel (0xF800 in RGB565)
                    int px = currentX + col;
                    int py = currentY + row;
                    if (px >= 0 && px < 240 && py >= 0 && py < 240) {
                        BlackImage[py * 240 + px] = 0xF800; // RED
                    }
                }
            }
        }
        
        currentX += Font16.Width;
        p++;
    }
    
    // 3. Push the overlay area directly to LCD
    LCD_1IN28_DisplayWindows(boxX1, boxY1, boxX2, boxY2, (UWORD*)BlackImage);
}