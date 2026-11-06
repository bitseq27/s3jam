# step_counter_setup.ps1
# Creates step counter files and integrates them into S3_jam.ino

param(
    [string]$ProjectPath = (Get-Location).Path
)

Write-Host "=== Step Counter Setup Script ===" -ForegroundColor Cyan
Write-Host "Project path: $ProjectPath" -ForegroundColor Gray
Write-Host ""

# Verify project structure
$inoFile = Join-Path $ProjectPath "S3_jam.ino"
if (-not (Test-Path $inoFile)) {
    Write-Error "Cannot find S3_jam.ino at: $inoFile"
    Write-Host "Please run this script from the s3jam project directory." -ForegroundColor Yellow
    exit 1
}

# ============================================================
# 1. Create step_counter.h
# ============================================================
Write-Host "[1/3] Creating step_counter.h..." -ForegroundColor Green

$headerContent = @'
#pragma once

// Step counter - runs in background, displays overlay every 10s
// Uses QMI8658 accelerometer for step detection

void stepCounter_init(void);
void stepCounter_update(unsigned long now);  // Call every loop iteration
unsigned long stepCounter_getCount(void);
void stepCounter_reset(void);
void stepCounter_drawOverlay(unsigned long now);  // Draw overlay on current framebuffer
'@

$headerPath = Join-Path $ProjectPath "step_counter.h"
Set-Content -Path $headerPath -Value $headerContent -Encoding UTF8
Write-Host "  Created: step_counter.h" -ForegroundColor Gray

# ============================================================
# 2. Create step_counter.cpp
# ============================================================
Write-Host "[2/3] Creating step_counter.cpp..." -ForegroundColor Green

$cppContent = @'
#include "step_counter.h"
#include "display_buffer.h"
#include "QMI8658.h"
#include "LCD_Test.h"
#include <math.h>

// Configuration
#define STEP_SAMPLE_INTERVAL_MS    20      // 50 Hz sampling (plenty for walking/running)
#define STEP_THRESHOLD_HIGH      1.30f   // g - peak detection threshold
#define STEP_THRESHOLD_LOW       1.05f   // g - valley threshold for peak confirmation
#define STEP_MIN_INTERVAL_MS     250     // minimum ms between steps (max 4 steps/sec)
#define STEP_OVERLAY_INTERVAL_MS 10000   // show overlay every 10 seconds
#define STEP_OVERLAY_DURATION_MS 2500    // show overlay for 2.5 seconds

// Low-pass filter coefficient (0.0-1.0, higher = more responsive, lower = smoother)
#define MAG_FILTER_ALPHA         0.35f

// Internal state
static unsigned long stepCount = 0;
static unsigned long lastSampleMs = 0;
static unsigned long lastStepMs = 0;
static float filteredMag = 1.0f;

// Peak detection state machine
enum StepState { WAITING_FOR_PEAK, PEAK_DETECTED, RECOVERING };
static StepState stepState = WAITING_FOR_PEAK;
static float peakValue = 0;
static bool imuAvailable = false;

void stepCounter_init(void) {
    stepCount = 0;
    lastSampleMs = 0;
    lastStepMs = 0;
    filteredMag = 1.0f;
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
    
    // Read accelerometer (values in g)
    float acc[3];
    float gyro[3];
    unsigned long tim_count;
    QMI8658_read_xyz(acc, gyro, &tim_count);
    
    // Compute magnitude of acceleration vector
    float mag = sqrtf(acc[0]*acc[0] + acc[1]*acc[1] + acc[2]*acc[2]);
    
    // Low-pass filter to smooth out noise
    filteredMag = MAG_FILTER_ALPHA * mag + (1.0f - MAG_FILTER_ALPHA) * filteredMag;
    
    // Peak detection state machine
    switch (stepState) {
        case WAITING_FOR_PEAK:
            // Looking for acceleration spike above threshold
            if (filteredMag > STEP_THRESHOLD_HIGH) {
                stepState = PEAK_DETECTED;
                peakValue = filteredMag;
            }
            break;
            
        case PEAK_DETECTED:
            // Track the peak, wait for it to fall back down
            if (filteredMag > peakValue) {
                peakValue = filteredMag;
            } else if (filteredMag < STEP_THRESHOLD_LOW) {
                // Peak confirmed - count as step if enough time has passed
                if (now - lastStepMs > STEP_MIN_INTERVAL_MS) {
                    stepCount++;
                    lastStepMs = now;
                    Serial.printf("Step #%lu (peak: %.2fg)\n", stepCount, peakValue);
                }
                stepState = RECOVERING;
            }
            break;
            
        case RECOVERING:
            // Wait for acceleration to settle before looking for next step
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
    // Only show overlay every 10 seconds for 2.5 seconds
    unsigned long cyclePosition = now % STEP_OVERLAY_INTERVAL_MS;
    if (cyclePosition > STEP_OVERLAY_DURATION_MS) {
        return;  // Not in overlay window
    }
    
    // Draw overlay background (dark rectangle)
    int boxX = 40;
    int boxY = 10;
    int boxW = 160;
    int boxH = 40;
    
    // Draw dark background rectangle
    Paint_DrawRectangle(boxX, boxY, boxX + boxW, boxY + boxH, BLACK, DRAW_FILLN, 0);
    
    // Draw border
    Paint_DrawRectangle(boxX, boxY, boxX + boxW, boxY + boxH, 0x07E0, DRAW_EMPTY, 0);  // Green border
    
    // Draw step count text
    char buf[32];
    sprintf(buf, "Steps: %lu", stepCount);
    
    // Center the text in the box
    int textWidth = strlen(buf) * Font16.Width;
    int textX = boxX + (boxW - textWidth) / 2;
    int textY = boxY + (boxH - Font16.Height) / 2;
    
    Paint_DrawString_EN(textX, textY, buf, &Font16, 0x07E0, BLACK);  // Green text on black
    
    // Draw small step icon (simple footstep representation)
    int iconX = boxX + 8;
    int iconY = boxY + 8;
    Paint_DrawCircle(iconX + 4, iconY + 4, 3, 0x07E0, DRAW_FILLN, 0);
    Paint_DrawCircle(iconX + 10, iconY + 10, 2, 0x07E0, DRAW_FILLN, 0);
}
'@

$cppPath = Join-Path $ProjectPath "step_counter.cpp"
Set-Content -Path $cppPath -Value $cppContent -Encoding UTF8
Write-Host "  Created: step_counter.cpp" -ForegroundColor Gray

# ============================================================
# 3. Modify S3_jam.ino
# ============================================================
Write-Host "[3/3] Modifying S3_jam.ino..." -ForegroundColor Green

# Read the original file
$inoContent = Get-Content -Path $inoFile -Raw -Encoding UTF8
$originalBackup = $inoFile + ".backup"
Copy-Item -Path $inoFile -Destination $originalBackup -Force
Write-Host "  Backed up original to: S3_jam.ino.backup" -ForegroundColor Gray

$modified = $inoContent
$changes = 0

# --- Insert #include "step_counter.h" ---
# Find the last #include line and add after it
$includePattern = '(?m)^(#include\s+".+")\s*$'
$includeMatches = [regex]::Matches($modified, $includePattern)

if ($includeMatches.Count -gt 0) {
    $lastInclude = $includeMatches[$includeMatches.Count - 1]
    $insertPos = $lastInclude.Index + $lastInclude.Length
    $modified = $modified.Substring(0, $insertPos) + "`n#include `"step_counter.h`"" + $modified.Substring($insertPos)
    $changes++
    Write-Host "  Added: #include `"step_counter.h`"" -ForegroundColor Gray
} else {
    Write-Warning "  Could not find #include lines to insert after"
}

# --- Insert stepCounter_init() in setup() ---
# Look for sensorPage_init() and add after it
$sensorInitPattern = '(?m)(^\s*sensorPage_init\(\)\s*;\s*$)'
if ($modified -match $sensorInitPattern) {
    $modified = [regex]::Replace($modified, $sensorInitPattern, '$1
    stepCounter_init();')
    $changes++
    Write-Host "  Added: stepCounter_init() in setup()" -ForegroundColor Gray
} else {
    Write-Warning "  Could not find sensorPage_init() to insert after"
}

# --- Insert stepCounter_update(now) and stepCounter_drawOverlay(now) in loop() ---
# Look for the start of the loop() function and add after "unsigned long now = millis();"
$loopStartPattern = '(?m)(^\s*unsigned\s+long\s+now\s*=\s*millis\(\)\s*;\s*$)'
if ($modified -match $loopStartPattern) {
    $modified = [regex]::Replace($modified, $loopStartPattern, '$1
    // Update step counter in background (always runs)
    stepCounter_update(now);')
    $changes++
    Write-Host "  Added: stepCounter_update(now) in loop()" -ForegroundColor Gray
}

# Look for the delay(1) at end of loop and add overlay before it
$delayPattern = '(?m)(^\s*delay\(1\)\s*;\s*$)'
if ($modified -match $delayPattern) {
    $modified = [regex]::Replace($modified, $delayPattern, '    // Draw step counter overlay on top of current page
    stepCounter_drawOverlay(now);

$1')
    $changes++
    Write-Host "  Added: stepCounter_drawOverlay(now) in loop()" -ForegroundColor Gray
}

# Write the modified file
Set-Content -Path $inoFile -Value $modified -Encoding UTF8 -NoNewline

# ============================================================
# Summary
# ============================================================
Write-Host ""
Write-Host "=== Setup Complete ===" -ForegroundColor Cyan
Write-Host "Changes made: $changes" -ForegroundColor Green
Write-Host ""
Write-Host "Files created:" -ForegroundColor Yellow
Write-Host "  - step_counter.h" -ForegroundColor Gray
Write-Host "  - step_counter.cpp" -ForegroundColor Gray
Write-Host ""
Write-Host "Files modified:" -ForegroundColor Yellow
Write-Host "  - S3_jam.ino (backup: S3_jam.ino.backup)" -ForegroundColor Gray
Write-Host ""
Write-Host "Tuning tips (in step_counter.cpp):" -ForegroundColor Yellow
Write-Host "  - STEP_THRESHOLD_HIGH (1.30g): lower if missing steps, raise if false positives" -ForegroundColor Gray
Write-Host "  - STEP_MIN_INTERVAL_MS (250ms): lower for running, raise for slow walking" -ForegroundColor Gray
Write-Host "  - STEP_OVERLAY_INTERVAL_MS (10000ms): how often overlay appears" -ForegroundColor Gray
Write-Host ""
Write-Host "Please verify S3_jam.ino changes before compiling!" -ForegroundColor Yellow