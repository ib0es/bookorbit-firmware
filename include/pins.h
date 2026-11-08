#pragma once

#include <Arduino.h>

/**
 * Hardware Pinout for XTEINK X4 (Original ESP32-C3 revision)
 * 
 * Display: GDEQ0426T82 (4.26" 800x480 E-Ink) with SSD1677 driver
 * Storage: MicroSD via shared hardware SPI
 * Buttons: Power (GPIO 3) + 6 navigation keys via dual ADC resistor ladders
 */

// ======================== E-Paper Display (SSD1677) ========================
#define EPD_SCK_PIN          8   // SPI Clock (shared with MicroSD)
#define EPD_MOSI_PIN         10  // SPI MOSI (shared with MicroSD)
#define EPD_CS_PIN           21  // Display Chip Select (Active LOW)
#define EPD_DC_PIN           4   // Data / Command (HIGH = Data, LOW = Command)
#define EPD_RST_PIN          5   // Reset (Active LOW)
#define EPD_BUSY_PIN         6   // Busy (Active HIGH: HIGH = busy, LOW = idle)

// Native (landscape) panel size. X3 (UC8253/UC8279, 3.7") shares the X4 pinout
// but has a 792x528 panel; each device is its own build env, so this is fixed
// at compile time and must match FreeInkDisplay's geometry after begin().
#if defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3 && !(defined(FREEINK_DEVICE_X4) && FREEINK_DEVICE_X4)
#define EPD_WIDTH            792
#define EPD_HEIGHT           528
#else
#define EPD_WIDTH            800
#define EPD_HEIGHT           480
#endif
#define EPD_BUFFER_SIZE      ((EPD_WIDTH * EPD_HEIGHT) / 8) // 48,000 bytes (X4), 52,272 (X3)

// ======================== MicroSD Card ========================
#define SD_SCK_PIN           8   // Shared SPI Clock
#define SD_MOSI_PIN          10  // Shared SPI MOSI
#define SD_MISO_PIN          7   // MicroSD MISO (Input to ESP32)
#define SD_CS_PIN            12  // MicroSD Chip Select (Active LOW)

// ======================== Buttons & Inputs ========================
// Digital Power Key (Wake-up pin from deep sleep)
#define BTN_POWER_PIN        3

// Resistor Ladder 1: Back, Confirm, Left, Right
#define BTN_ADC1_PIN         1

// Resistor Ladder 2: Up, Down
#define BTN_ADC2_PIN         2

// Nominal 12-bit ADC values (0 - 4095) for buttons:
#define ADC_TOLERANCE        280  // Allowed +/- window for matching button values

#define ADC_UNPRESSED        4095

// ADC Line 1 values
#define ADC1_VAL_BACK        3564
#define ADC1_VAL_CONFIRM     2731
#define ADC1_VAL_LEFT        1516
#define ADC1_VAL_RIGHT       10   // Near 0 (typically 0 - 50)

// ADC Line 2 values
#define ADC2_VAL_UP          2255
#define ADC2_VAL_DOWN        10   // Near 0 (typically 0 - 50)

// ======================== Battery & Power ========================
#define BATTERY_ADC_PIN      0   // Battery voltage divider ADC input
#define USB_DETECT_PIN       20  // USB plug-in / 5V charging detection

// USB Native CDC Pins
#define USB_DM_PIN           18
#define USB_DP_PIN           19
