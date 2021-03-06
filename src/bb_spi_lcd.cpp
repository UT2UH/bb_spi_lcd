// LCD direct communication using the SPI interface
// Copyright (c) 2017 Larry Bank
// email: bitbank@pobox.com
// Project started 5/15/2017
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//

// The ILITEK LCD display controllers communicate through the SPI interface
// and two GPIO pins to control the RESET, and D/C (data/command)
// control lines. 
//#if defined(ADAFRUIT_PYBADGE_M4_EXPRESS)
//#define SPI SPI1
//#endif

#ifdef _LINUX_
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <math.h>
#include <armbianio.h>
#define false 0
#define true 1
#define PROGMEM
#define memcpy_P memcpy
// convert wire library constants into ArmbianIO values
#define OUTPUT GPIO_OUT
#define INPUT GPIO_IN
#define INPUT_PULLUP GPIO_IN_PULLUP
#define HIGH 1
#define LOW 0
static int iHandle; // SPI handle
#else // Arduino

#include <Arduino.h>
#include <SPI.h>
#endif // LINUX

#include <bb_spi_lcd.h>

#ifdef HAL_ESP32_HAL_H_
// disable until the bugs are worked out
//#define ESP32_DMA
#endif

#ifdef ESP32_DMA
#include "driver/spi_master.h"

// ODROID-GO
//const gpio_num_t SPI_PIN_NUM_MISO = GPIO_NUM_19;
//const gpio_num_t SPI_PIN_NUM_MOSI = GPIO_NUM_23;
//const gpio_num_t SPI_PIN_NUM_CLK  = GPIO_NUM_18;
// M5StickC
//const gpio_num_t SPI_PIN_NUM_MISO = GPIO_NUM_19;
//const gpio_num_t SPI_PIN_NUM_MOSI = GPIO_NUM_15;
//const gpio_num_t SPI_PIN_NUM_CLK  = GPIO_NUM_13;
static spi_transaction_t trans[8];
static spi_device_handle_t spi;
//static TaskHandle_t xTaskToNotify = NULL;
// ESP32 has enough memory to spare 4K
DMA_ATTR uint8_t ucTXBuf[4096]="";
static unsigned char ucRXBuf[4096];
static int iTXBufSize = 4096; // max reasonable size
#else
static int iTXBufSize;
static unsigned char *ucTXBuf;
static unsigned char ucRXBuf[4096];
#endif
#define LCD_DELAY 0xff
#ifdef __AVR__
volatile uint8_t *outDC, *outCS; // port registers for fast I/O
uint8_t bitDC, bitCS; // bit mask for the chosen pins
#endif
static void myPinWrite(int iPin, int iValue);
static int32_t iSPISpeed;
static int iCSPin, iDCPin, iResetPin, iLEDPin; // pin numbers for the GPIO control lines
static int iScrollOffset; // current scroll amount
static int iOrientation = LCD_ORIENTATION_NATIVE; // default to 'natural' orientation
static int iLCDType;
static int iWidth, iHeight;
static int iCurrentWidth, iCurrentHeight; // reflects virtual size due to orientation
// User-provided callback for writing data/commands
static DATACALLBACK pfnDataCallback = NULL;
static RESETCALLBACK pfnResetCallback = NULL;
// For back buffer support
static int iScreenPitch, iOffset, iMaxOffset;
static int iSPIMode;
static int iMemoryX, iMemoryY; // display oddities with smaller LCDs
static uint8_t *pBackBuffer = NULL;
static int iWindowX, iWindowY, iCurrentX, iCurrentY;
static int iWindowCX, iWindowCY;
static int bSetPosition = 0; // flag telling myspiWrite() to ignore data writes to memory
static void spilcdWriteCommand(unsigned char);
static void spilcdWriteData8(unsigned char c);
static void spilcdWriteData16(unsigned short us, int bRender);
void spilcdSetPosition(int x, int y, int w, int h, int bRender);
int spilcdFill(unsigned short usData, int bRender);
const unsigned char ucE0_0[] PROGMEM = {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00};
const unsigned char ucE1_0[] PROGMEM = {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F};
const unsigned char ucE0_1[] PROGMEM = {0x1f, 0x1a, 0x18, 0x0a, 0x0f, 0x06, 0x45, 0x87, 0x32, 0x0a, 0x07, 0x02, 0x07, 0x05, 0x00};
const unsigned char ucE1_1[] PROGMEM = {0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3a, 0x78, 0x4d, 0x05, 0x18, 0x0d, 0x38, 0x3a, 0x1f};

// small (8x8) font
const uint8_t ucFont[]PROGMEM = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x5f,0x5f,0x06,0x00,0x00,
    0x00,0x07,0x07,0x00,0x07,0x07,0x00,0x00,0x14,0x7f,0x7f,0x14,0x7f,0x7f,0x14,0x00,
    0x24,0x2e,0x2a,0x6b,0x6b,0x3a,0x12,0x00,0x46,0x66,0x30,0x18,0x0c,0x66,0x62,0x00,
    0x30,0x7a,0x4f,0x5d,0x37,0x7a,0x48,0x00,0x00,0x04,0x07,0x03,0x00,0x00,0x00,0x00,
    0x00,0x1c,0x3e,0x63,0x41,0x00,0x00,0x00,0x00,0x41,0x63,0x3e,0x1c,0x00,0x00,0x00,
    0x08,0x2a,0x3e,0x1c,0x1c,0x3e,0x2a,0x08,0x00,0x08,0x08,0x3e,0x3e,0x08,0x08,0x00,
    0x00,0x00,0x80,0xe0,0x60,0x00,0x00,0x00,0x00,0x08,0x08,0x08,0x08,0x08,0x08,0x00,
    0x00,0x00,0x00,0x60,0x60,0x00,0x00,0x00,0x60,0x30,0x18,0x0c,0x06,0x03,0x01,0x00,
    0x3e,0x7f,0x59,0x4d,0x47,0x7f,0x3e,0x00,0x40,0x42,0x7f,0x7f,0x40,0x40,0x00,0x00,
    0x62,0x73,0x59,0x49,0x6f,0x66,0x00,0x00,0x22,0x63,0x49,0x49,0x7f,0x36,0x00,0x00,
    0x18,0x1c,0x16,0x53,0x7f,0x7f,0x50,0x00,0x27,0x67,0x45,0x45,0x7d,0x39,0x00,0x00,
    0x3c,0x7e,0x4b,0x49,0x79,0x30,0x00,0x00,0x03,0x03,0x71,0x79,0x0f,0x07,0x00,0x00,
    0x36,0x7f,0x49,0x49,0x7f,0x36,0x00,0x00,0x06,0x4f,0x49,0x69,0x3f,0x1e,0x00,0x00,
    0x00,0x00,0x00,0x66,0x66,0x00,0x00,0x00,0x00,0x00,0x80,0xe6,0x66,0x00,0x00,0x00,
    0x08,0x1c,0x36,0x63,0x41,0x00,0x00,0x00,0x00,0x14,0x14,0x14,0x14,0x14,0x14,0x00,
    0x00,0x41,0x63,0x36,0x1c,0x08,0x00,0x00,0x00,0x02,0x03,0x59,0x5d,0x07,0x02,0x00,
    0x3e,0x7f,0x41,0x5d,0x5d,0x5f,0x0e,0x00,0x7c,0x7e,0x13,0x13,0x7e,0x7c,0x00,0x00,
    0x41,0x7f,0x7f,0x49,0x49,0x7f,0x36,0x00,0x1c,0x3e,0x63,0x41,0x41,0x63,0x22,0x00,
    0x41,0x7f,0x7f,0x41,0x63,0x3e,0x1c,0x00,0x41,0x7f,0x7f,0x49,0x5d,0x41,0x63,0x00,
    0x41,0x7f,0x7f,0x49,0x1d,0x01,0x03,0x00,0x1c,0x3e,0x63,0x41,0x51,0x33,0x72,0x00,
    0x7f,0x7f,0x08,0x08,0x7f,0x7f,0x00,0x00,0x00,0x41,0x7f,0x7f,0x41,0x00,0x00,0x00,
    0x30,0x70,0x40,0x41,0x7f,0x3f,0x01,0x00,0x41,0x7f,0x7f,0x08,0x1c,0x77,0x63,0x00,
    0x41,0x7f,0x7f,0x41,0x40,0x60,0x70,0x00,0x7f,0x7f,0x0e,0x1c,0x0e,0x7f,0x7f,0x00,
    0x7f,0x7f,0x06,0x0c,0x18,0x7f,0x7f,0x00,0x1c,0x3e,0x63,0x41,0x63,0x3e,0x1c,0x00,
    0x41,0x7f,0x7f,0x49,0x09,0x0f,0x06,0x00,0x1e,0x3f,0x21,0x31,0x61,0x7f,0x5e,0x00,
    0x41,0x7f,0x7f,0x09,0x19,0x7f,0x66,0x00,0x26,0x6f,0x4d,0x49,0x59,0x73,0x32,0x00,
    0x03,0x41,0x7f,0x7f,0x41,0x03,0x00,0x00,0x7f,0x7f,0x40,0x40,0x7f,0x7f,0x00,0x00,
    0x1f,0x3f,0x60,0x60,0x3f,0x1f,0x00,0x00,0x3f,0x7f,0x60,0x30,0x60,0x7f,0x3f,0x00,
    0x63,0x77,0x1c,0x08,0x1c,0x77,0x63,0x00,0x07,0x4f,0x78,0x78,0x4f,0x07,0x00,0x00,
    0x47,0x63,0x71,0x59,0x4d,0x67,0x73,0x00,0x00,0x7f,0x7f,0x41,0x41,0x00,0x00,0x00,
    0x01,0x03,0x06,0x0c,0x18,0x30,0x60,0x00,0x00,0x41,0x41,0x7f,0x7f,0x00,0x00,0x00,
    0x08,0x0c,0x06,0x03,0x06,0x0c,0x08,0x00,0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
    0x00,0x00,0x03,0x07,0x04,0x00,0x00,0x00,0x20,0x74,0x54,0x54,0x3c,0x78,0x40,0x00,
    0x41,0x7f,0x3f,0x48,0x48,0x78,0x30,0x00,0x38,0x7c,0x44,0x44,0x6c,0x28,0x00,0x00,
    0x30,0x78,0x48,0x49,0x3f,0x7f,0x40,0x00,0x38,0x7c,0x54,0x54,0x5c,0x18,0x00,0x00,
    0x48,0x7e,0x7f,0x49,0x03,0x06,0x00,0x00,0x98,0xbc,0xa4,0xa4,0xf8,0x7c,0x04,0x00,
    0x41,0x7f,0x7f,0x08,0x04,0x7c,0x78,0x00,0x00,0x44,0x7d,0x7d,0x40,0x00,0x00,0x00,
    0x60,0xe0,0x80,0x84,0xfd,0x7d,0x00,0x00,0x41,0x7f,0x7f,0x10,0x38,0x6c,0x44,0x00,
    0x00,0x41,0x7f,0x7f,0x40,0x00,0x00,0x00,0x7c,0x7c,0x18,0x78,0x1c,0x7c,0x78,0x00,
    0x7c,0x78,0x04,0x04,0x7c,0x78,0x00,0x00,0x38,0x7c,0x44,0x44,0x7c,0x38,0x00,0x00,
    0x84,0xfc,0xf8,0xa4,0x24,0x3c,0x18,0x00,0x18,0x3c,0x24,0xa4,0xf8,0xfc,0x84,0x00,
    0x44,0x7c,0x78,0x4c,0x04,0x0c,0x18,0x00,0x48,0x5c,0x54,0x74,0x64,0x24,0x00,0x00,
    0x04,0x04,0x3e,0x7f,0x44,0x24,0x00,0x00,0x3c,0x7c,0x40,0x40,0x3c,0x7c,0x40,0x00,
    0x1c,0x3c,0x60,0x60,0x3c,0x1c,0x00,0x00,0x3c,0x7c,0x60,0x30,0x60,0x7c,0x3c,0x00,
    0x44,0x6c,0x38,0x10,0x38,0x6c,0x44,0x00,0x9c,0xbc,0xa0,0xa0,0xfc,0x7c,0x00,0x00,
    0x4c,0x64,0x74,0x5c,0x4c,0x64,0x00,0x00,0x08,0x08,0x3e,0x77,0x41,0x41,0x00,0x00,
    0x00,0x00,0x00,0x77,0x77,0x00,0x00,0x00,0x41,0x41,0x77,0x3e,0x08,0x08,0x00,0x00,
    0x02,0x03,0x01,0x03,0x02,0x03,0x01,0x00,0x70,0x78,0x4c,0x46,0x4c,0x78,0x70,0x00};
// AVR MCUs have very little memory; save 6K of FLASH by stretching the 'normal'
// font instead of using this large font
#ifndef __AVR__
const uint8_t ucBigFont[]PROGMEM = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xfc,0xfc,0xff,0xff,0xff,0xff,0xfc,0xfc,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x3f,0x3f,0x3f,0x3f,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,
    0x00,0x00,0x0f,0x0f,0x3f,0x3f,0x00,0x00,0x00,0x00,0x3f,0x3f,0x0f,0x0f,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xc0,0xc0,0xfc,0xfc,0xfc,0xfc,0xc0,0xc0,0xfc,0xfc,0xfc,0xfc,0xc0,0xc0,0x00,0x00,
    0xc0,0xc0,0xff,0xff,0xff,0xff,0xc0,0xc0,0xff,0xff,0xff,0xff,0xc0,0xc0,0x00,0x00,
    0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xf0,0xf0,0xf0,0xf0,0x00,0x00,0x00,0x00,0x00,0x00,
    0xfc,0xfc,0xff,0xff,0x03,0x03,0x03,0x03,0x03,0x03,0x0f,0x0f,0x3c,0x3c,0x00,0x00,
    0xf0,0xf0,0xc3,0xc3,0x03,0x03,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,
    0x00,0x00,0x03,0x03,0x03,0x03,0x3f,0x3f,0x3f,0x3f,0x03,0x03,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xf0,0xf0,0xf0,0xf0,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xf0,0xf0,0x00,0x00,
    0x00,0x00,0xc0,0xc0,0xf0,0xf0,0x3c,0x3c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,
    0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x3c,0x3c,0xff,0xff,0xc3,0xc3,0xff,0xff,0x3c,0x3c,0x00,0x00,0x00,0x00,
    0xfc,0xfc,0xff,0xff,0x03,0x03,0x0f,0x0f,0xfc,0xfc,0xff,0xff,0x03,0x03,0x00,0x00,
    0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x00,0x00,
    0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x30,0x30,0x3f,0x3f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xf0,0xf0,0xfc,0xfc,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0xfc,0xfc,0xf0,0xf0,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,
    0x0c,0x0c,0xcc,0xcc,0xff,0xff,0x3f,0x3f,0x3f,0x3f,0xff,0xff,0xcc,0xcc,0x0c,0x0c,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x0c,0x0c,0x0c,0x0c,0xff,0xff,0xff,0xff,0x0c,0x0c,0x0c,0x0c,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x30,0x30,0x3f,0x3f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xf0,0xf0,0x00,0x00,
    0x00,0x00,0xc0,0xc0,0xf0,0xf0,0x3c,0x3c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,
    0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xfc,0xfc,0xff,0xff,0x03,0x03,0x03,0x03,0xc3,0xc3,0xff,0xff,0xfc,0xfc,0x00,0x00,
    0xff,0xff,0xff,0xff,0x30,0x30,0x0f,0x0f,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,
    0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x30,0x30,0x3c,0x3c,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x03,0x03,0xc3,0xc3,0xff,0xff,0x3c,0x3c,0x00,0x00,
    0xc0,0xc0,0xf0,0xf0,0x3c,0x3c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,
    0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,
    0x00,0x00,0x00,0x00,0x03,0x03,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,
    0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xc0,0xc0,0xf0,0xf0,0x3c,0x3c,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
    0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0xff,0xff,0xff,0xff,0x0c,0x0c,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xff,0xff,0xff,0xff,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x00,
    0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x0f,0x0f,0xff,0xff,0xfc,0xfc,0x00,0x00,
    0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xf0,0xf0,0xfc,0xfc,0x0f,0x0f,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,
    0xff,0xff,0xff,0xff,0x03,0x03,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,
    0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x0f,0x0f,0x0f,0x0f,0x03,0x03,0x03,0x03,0x03,0x03,0xff,0xff,0xff,0xff,0x00,0x00,
    0x00,0x00,0x00,0x00,0xf0,0xf0,0xfc,0xfc,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xfc,0xfc,0xff,0xff,0x03,0x03,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,
    0xfc,0xfc,0xff,0xff,0x03,0x03,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,
    0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xfc,0xfc,0xff,0xff,0x03,0x03,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,
    0x00,0x00,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0xff,0xff,0xff,0xff,0x00,0x00,
    0x00,0x00,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xf0,0xf0,0xf0,0xf0,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xf0,0xf0,0xf0,0xf0,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xf0,0xf0,0x3c,0x3c,0x0c,0x0c,0x00,0x00,
    0x00,0x00,0x0c,0x0c,0x3f,0x3f,0xf3,0xf3,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xc3,0xc3,0xc3,0xc3,0xc3,0xc3,0xc3,0xc3,0xc3,0xc3,0xc3,0xc3,0xc3,0xc3,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x0c,0x0c,0x3c,0x3c,0xf0,0xf0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xf3,0xf3,0x3f,0x3f,0x0c,0x0c,0x00,0x00,
    0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x3c,0x3c,0x3f,0x3f,0x03,0x03,0x03,0x03,0xc3,0xc3,0xff,0xff,0x3c,0x3c,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x3f,0x3f,0x3f,0x3f,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xf0,0xf0,0xfc,0xfc,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0xfc,0xfc,0xf0,0xf0,0x00,0x00,
    0xff,0xff,0xff,0xff,0x00,0x00,0xff,0xff,0xff,0xff,0xff,0xff,0x3f,0x3f,0x00,0x00,
    0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xc0,0xc0,0xf0,0xf0,0x3c,0x3c,0x0f,0x0f,0x3c,0x3c,0xf0,0xf0,0xc0,0xc0,0x00,0x00,
    0xff,0xff,0xff,0xff,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0xff,0xff,0xff,0xff,0x00,0x00,
    0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x03,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,
    0x00,0x00,0xff,0xff,0xff,0xff,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,
    0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xf0,0xf0,0xfc,0xfc,0x0f,0x0f,0x03,0x03,0x03,0x03,0x0f,0x0f,0x3c,0x3c,0x00,0x00,
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0x00,0x00,
    0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x03,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0x0f,0x0f,0xfc,0xfc,0xf0,0xf0,0x00,0x00,
    0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,
    0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x03,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0xc3,0xc3,0x0f,0x0f,0x3f,0x3f,0x00,0x00,
    0x00,0x00,0xff,0xff,0xff,0xff,0x03,0x03,0x0f,0x0f,0x00,0x00,0xc0,0xc0,0x00,0x00,
    0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x03,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0xc3,0xc3,0x0f,0x0f,0x3f,0x3f,0x00,0x00,
    0x00,0x00,0xff,0xff,0xff,0xff,0x03,0x03,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,
    0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xf0,0xf0,0xfc,0xfc,0x0f,0x0f,0x03,0x03,0x03,0x03,0x0f,0x0f,0x3c,0x3c,0x00,0x00,
    0xff,0xff,0xff,0xff,0x00,0x00,0x0c,0x0c,0x0c,0x0c,0xfc,0xfc,0xfc,0xfc,0x00,0x00,
    0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x03,0x03,0x0f,0x0f,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,
    0xff,0xff,0xff,0xff,0x03,0x03,0x03,0x03,0x03,0x03,0xff,0xff,0xff,0xff,0x00,0x00,
    0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x03,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0x00,0x00,
    0xf0,0xf0,0xf0,0xf0,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
    0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x03,0x03,0xff,0xff,0xff,0xff,0x00,0x00,0xf0,0xf0,0xff,0xff,0x0f,0x0f,0x00,0x00,
    0x00,0x00,0xff,0xff,0xff,0xff,0x0f,0x0f,0x3f,0x3f,0xf0,0xf0,0xc0,0xc0,0x00,0x00,
    0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x03,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0x00,0x00,
    0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xff,0xff,0xff,0xff,0xfc,0xfc,0xf0,0xf0,0xfc,0xfc,0xff,0xff,0xff,0xff,0x00,0x00,
    0xff,0xff,0xff,0xff,0x00,0x00,0x03,0x03,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,
    0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xff,0xff,0xff,0xff,0xfc,0xfc,0xf0,0xf0,0xc0,0xc0,0xff,0xff,0xff,0xff,0x00,0x00,
    0xff,0xff,0xff,0xff,0x00,0x00,0x03,0x03,0x0f,0x0f,0xff,0xff,0xff,0xff,0x00,0x00,
    0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xf0,0xf0,0xfc,0xfc,0x0f,0x0f,0x03,0x03,0x0f,0x0f,0xfc,0xfc,0xf0,0xf0,0x00,0x00,
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,
    0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x03,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,
    0x00,0x00,0xff,0xff,0xff,0xff,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,
    0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xfc,0xfc,0xff,0xff,0x03,0x03,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,
    0xff,0xff,0xff,0xff,0x00,0x00,0xc0,0xc0,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,
    0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0f,0x0f,0xff,0xff,0xff,0xff,0xc3,0xc3,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x03,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,
    0x00,0x00,0xff,0xff,0xff,0xff,0x03,0x03,0x0f,0x0f,0xff,0xff,0xf0,0xf0,0x00,0x00,
    0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x3c,0x3c,0xff,0xff,0xc3,0xc3,0x03,0x03,0x03,0x03,0x3f,0x3f,0x3c,0x3c,0x00,0x00,
    0xc0,0xc0,0xc0,0xc0,0x03,0x03,0x03,0x03,0x0f,0x0f,0xfc,0xfc,0xf0,0xf0,0x00,0x00,
    0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x3f,0x3f,0x0f,0x0f,0xff,0xff,0xff,0xff,0x0f,0x0f,0x3f,0x3f,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,
    0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,
    0x3f,0x3f,0xff,0xff,0xc0,0xc0,0x00,0x00,0xc0,0xc0,0xff,0xff,0x3f,0x3f,0x00,0x00,
    0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,
    0xff,0xff,0xff,0xff,0xc0,0xc0,0xfc,0xfc,0xc0,0xc0,0xff,0xff,0xff,0xff,0x00,0x00,
    0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x0f,0x0f,0xff,0xff,0xf0,0xf0,0x00,0x00,0xf0,0xf0,0xff,0xff,0x0f,0x0f,0x00,0x00,
    0x00,0x00,0xf0,0xf0,0xff,0xff,0x0f,0x0f,0xff,0xff,0xf0,0xf0,0x00,0x00,0x00,0x00,
    0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,
    0x00,0x00,0x00,0x00,0x03,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x3f,0x3f,0x0f,0x0f,0x03,0x03,0x03,0x03,0xc3,0xc3,0xff,0xff,0x3f,0x3f,0x00,0x00,
    0xc0,0xc0,0xf0,0xf0,0x3c,0x3c,0x0f,0x0f,0x03,0x03,0x00,0x00,0xc0,0xc0,0x00,0x00,
    0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xfc,0xfc,0xf0,0xf0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x03,0x03,0x0f,0x0f,0x3f,0x3f,0xfc,0xfc,0xf0,0xf0,0xc0,0xc0,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x03,0x03,0x03,0x03,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xc0,0xc0,0xf0,0xf0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,
    0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,
    0x00,0x00,0x00,0x00,0xf0,0xf0,0xf0,0xf0,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,
    0xf0,0xf0,0xfc,0xfc,0x0c,0x0c,0x0c,0x0c,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
    0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x03,0x03,0xff,0xff,0xff,0xff,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,
    0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x03,0x03,0x00,0x00,
    0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xc0,0xc0,0xc3,0xc3,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
    0xfc,0xfc,0xff,0xff,0x03,0x03,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
    0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,
    0xff,0xff,0xff,0xff,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x00,0x00,
    0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xfc,0xfc,0xff,0xff,0x03,0x03,0x0f,0x0f,0x3c,0x3c,0x00,0x00,0x00,0x00,
    0x03,0x03,0xff,0xff,0xff,0xff,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
    0xc3,0xc3,0xcf,0xcf,0x0c,0x0c,0x0c,0x0c,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
    0x00,0x00,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,
    0x03,0x03,0xff,0xff,0xff,0xff,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,
    0x00,0x00,0xff,0xff,0xff,0xff,0x03,0x03,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,
    0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xc0,0xc0,0xcf,0xcf,0xcf,0xcf,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xcf,0xcf,0xcf,0xcf,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,
    0x00,0x00,0xf0,0xf0,0xf0,0xf0,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,
    0x00,0x00,0x00,0x00,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,
    0x03,0x03,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,
    0x00,0x00,0xff,0xff,0xff,0xff,0x3c,0x3c,0xff,0xff,0xc3,0xc3,0x00,0x00,0x00,0x00,
    0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x03,0x03,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,
    0xff,0xff,0xff,0xff,0x03,0x03,0xff,0xff,0x03,0x03,0xff,0xff,0xff,0xff,0x00,0x00,
    0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x0f,0x0f,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xc0,0xc0,0xc0,0xc0,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,
    0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,
    0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,
    0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xc0,0xc0,0xc0,0xc0,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,
    0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,
    0x00,0x00,0xff,0xff,0xff,0xff,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,
    0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
    0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x00,
    0xc0,0xc0,0xc0,0xc0,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,
    0x00,0x00,0xff,0xff,0xff,0xff,0x03,0x03,0x00,0x00,0x03,0x03,0x0f,0x0f,0x00,0x00,
    0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,
    0x03,0x03,0x0f,0x0f,0x3c,0x3c,0x30,0x30,0xf0,0xf0,0xc3,0xc3,0x03,0x03,0x00,0x00,
    0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xc0,0xc0,0xc0,0xc0,0xfc,0xfc,0xff,0xff,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
    0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,
    0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,
    0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,
    0xff,0xff,0xff,0xff,0x00,0x00,0xf0,0xf0,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,
    0x03,0x03,0x0f,0x0f,0x0f,0x0f,0x03,0x03,0x0f,0x0f,0x0f,0x0f,0x03,0x03,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,
    0x00,0x00,0x03,0x03,0xff,0xff,0xfc,0xfc,0xff,0xff,0x03,0x03,0x00,0x00,0x00,0x00,
    0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0xc0,0xc0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,0xc0,0xc0,0xc0,0xc0,0x00,0x00,
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,
    0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0xcc,0xcc,0xff,0xff,0x3f,0x3f,0x00,0x00,
    0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,
    0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0xc0,0x00,0x00,
    0x03,0x03,0xc3,0xc3,0xf0,0xf0,0x3c,0x3c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,
    0x0f,0x0f,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x0f,0x0f,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xfc,0xfc,0xff,0xff,0x03,0x03,0x03,0x03,0x00,0x00,
    0x00,0x00,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0c,0x0c,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xfc,0xfc,0xfc,0xfc,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x0f,0x0f,0x0f,0x0f,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x03,0x03,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0xfc,0xfc,0xff,0xff,0x03,0x03,0x03,0x03,0x00,0x00,
    0x00,0x00,0x0c,0x0c,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x0f,0x0f,0x0c,0x0c,0x0f,0x0f,0x03,0x03,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xc0,0xc0,0xf0,0xf0,0xc0,0xc0,0x00,0x00,0x00,0x00,0x00,0x00,
    0xfc,0xfc,0xff,0xff,0x03,0x03,0x00,0x00,0x03,0x03,0xff,0xff,0xfc,0xfc,0x00,0x00,
    0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x03,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00};
//
// Table of sine values for 0-360 degrees expressed as a signed 16-bit value
// from -32768 (-1) to 32767 (1)
//
int16_t i16SineTable[] = {0,572, 1144, 1715, 2286, 2856, 3425, 3993, 4560, 5126,  // 0-9
        5690,  6252, 6813, 7371, 7927, 8481, 9032, 9580, 10126, 10668, // 10-19
        11207,  11743, 12275, 12803, 13328, 13848, 14365, 14876, 15384, 15886,// 20-29
        16384,  16877, 17364, 17847, 18324, 18795, 19261, 19720, 20174, 20622,// 30-39
        21063,  21498, 21926, 22348, 22763, 23170, 23571, 23965, 24351, 24730,// 40-49
        25102,  25466, 25822, 26170, 26510, 26842, 27166, 27482, 27789, 28088,// 50-59
        28378,  28660, 28932, 29197, 29452, 29698, 29935, 30163, 30382, 30592,// 60-69
        30792,  30983, 31164, 31336, 31499, 31651, 31795, 31928, 32052, 32166,// 70-79
        32270,  32365, 32440, 32524, 32599, 32643, 32688, 32723, 32748, 32763,// 80-89
        32767,  32763, 32748, 32723, 32688, 32643, 32588, 32524, 32449, 32365,// 90-99
        32270,  32166, 32052, 31928, 31795, 31651, 31499, 31336, 31164, 30983,// 100-109
        30792,  30592, 30382, 30163, 29935, 29698, 29452, 29197, 28932, 28660,// 110-119
        28378,  28088, 27789, 27482, 27166, 26842, 26510, 26170, 25822, 25466,// 120-129
        25102,  24730, 24351, 23965, 23571, 23170, 22763, 22348, 21926, 21498,// 130-139
        21063,  20622, 20174, 19720, 19261, 18795, 18324, 17847, 17364, 16877,// 140-149
        16384,  15886, 15384, 14876, 14365, 13848, 13328, 12803, 12275, 11743,// 150-159
        11207,  10668, 10126, 9580, 9032, 8481, 7927, 7371, 6813, 6252,// 160-169
        5690,  5126, 4560, 3993, 3425, 2856, 2286, 1715, 1144, 572,//  170-179
        0,  -572, -1144, -1715, -2286, -2856, -3425, -3993, -4560, -5126,// 180-189
        -5690,  -6252, -6813, -7371, -7927, -8481, -9032, -9580, -10126, -10668,// 190-199
        -11207,  -11743, -12275, -12803, -13328, -13848, -14365, -14876, -15384, -15886,// 200-209
        -16384,  -16877, -17364, -17847, -18324, -18795, -19261, -19720, -20174, -20622,// 210-219
        -21063,  -21498, -21926, -22348, -22763, -23170, -23571, -23965, -24351, -24730, // 220-229
        -25102,  -25466, -25822, -26170, -26510, -26842, -27166, -27482, -27789, -28088, // 230-239
        -28378,  -28660, -28932, -29196, -29452, -29698, -29935, -30163, -30382, -30592, // 240-249
        -30792,  -30983, -31164, -31336, -31499, -31651, -31795, -31928, -32052, -32166, // 250-259
        -32270,  -32365, -32449, -32524, -32588, -32643, -32688, -32723, -32748, -32763, // 260-269
        -32768,  -32763, -32748, -32723, -32688, -32643, -32588, -32524, -32449, -32365, // 270-279
        -32270,  -32166, -32052, -31928, -31795, -31651, -31499, -31336, -31164, -30983, // 280-289
        -30792,  -30592, -30382, -30163, -29935, -29698, -29452, -29196, -28932, -28660, // 290-299
        -28378,  -28088, -27789, -27482, -27166, -26842, -26510, -26170, -25822, -25466, // 300-309
        -25102,  -24730, -24351, -23965, -23571, -23170, -22763, -22348, -21926, -21498, // 310-319
        -21063,  -20622, -20174, -19720, -19261, -18795, -18234, -17847, -17364, -16877, // 320-329
        -16384,  -15886, -15384, -14876, -14365, -13848, -13328, -12803, -12275, -11743, // 330-339
        -11207,  -10668, -10126, -9580, -9032, -8481, -7927, -7371, -6813, -6252,// 340-349
        -5690,  -5126, -4560, -3993, -3425, -2856, -2286, -1715, -1144, -572, // 350-359
// an extra 90 degrees to simulate the cosine function
        0,572,  1144, 1715, 2286, 2856, 3425, 3993, 4560, 5126,// 0-9
        5690,  6252, 6813, 7371, 7927, 8481, 9032, 9580, 10126, 10668,// 10-19
        11207,  11743, 12275, 12803, 13328, 13848, 14365, 14876, 15384, 15886,// 20-29
        16384,  16877, 17364, 17847, 18324, 18795, 19261, 19720, 20174, 20622,// 30-39
        21063,  21498, 21926, 22348, 22763, 23170, 23571, 23965, 24351, 24730,// 40-49
        25102,  25466, 25822, 26170, 26510, 26842, 27166, 27482, 27789, 28088,// 50-59
        28378,  28660, 28932, 29197, 29452, 29698, 29935, 30163, 30382, 30592,// 60-69
        30792,  30983, 31164, 31336, 31499, 31651, 31795, 31928, 32052, 32166,// 70-79
    32270,  32365, 32440, 32524, 32599, 32643, 32688, 32723, 32748, 32763}; // 80-89

#endif // !__AVR__

// 5x7 font (in 6x8 cell)
const uint8_t ucSmallFont[]PROGMEM = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x06,0x5f,0x06,0x00,0x00,0x07,0x03,0x00,
    0x07,0x03,0x00,0x24,0x7e,0x24,0x7e,0x24,0x00,0x24,0x2b,0x6a,0x12,0x00,0x00,0x63,
    0x13,0x08,0x64,0x63,0x00,0x36,0x49,0x56,0x20,0x50,0x00,0x00,0x07,0x03,0x00,0x00,
    0x00,0x00,0x3e,0x41,0x00,0x00,0x00,0x00,0x41,0x3e,0x00,0x00,0x00,0x08,0x3e,0x1c,
    0x3e,0x08,0x00,0x08,0x08,0x3e,0x08,0x08,0x00,0x00,0xe0,0x60,0x00,0x00,0x00,0x08,
    0x08,0x08,0x08,0x08,0x00,0x00,0x60,0x60,0x00,0x00,0x00,0x20,0x10,0x08,0x04,0x02,
    0x00,0x3e,0x51,0x49,0x45,0x3e,0x00,0x00,0x42,0x7f,0x40,0x00,0x00,0x62,0x51,0x49,
    0x49,0x46,0x00,0x22,0x49,0x49,0x49,0x36,0x00,0x18,0x14,0x12,0x7f,0x10,0x00,0x2f,
    0x49,0x49,0x49,0x31,0x00,0x3c,0x4a,0x49,0x49,0x30,0x00,0x01,0x71,0x09,0x05,0x03,
    0x00,0x36,0x49,0x49,0x49,0x36,0x00,0x06,0x49,0x49,0x29,0x1e,0x00,0x00,0x6c,0x6c,
    0x00,0x00,0x00,0x00,0xec,0x6c,0x00,0x00,0x00,0x08,0x14,0x22,0x41,0x00,0x00,0x24,
    0x24,0x24,0x24,0x24,0x00,0x00,0x41,0x22,0x14,0x08,0x00,0x02,0x01,0x59,0x09,0x06,
    0x00,0x3e,0x41,0x5d,0x55,0x1e,0x00,0x7e,0x11,0x11,0x11,0x7e,0x00,0x7f,0x49,0x49,
    0x49,0x36,0x00,0x3e,0x41,0x41,0x41,0x22,0x00,0x7f,0x41,0x41,0x41,0x3e,0x00,0x7f,
    0x49,0x49,0x49,0x41,0x00,0x7f,0x09,0x09,0x09,0x01,0x00,0x3e,0x41,0x49,0x49,0x7a,
    0x00,0x7f,0x08,0x08,0x08,0x7f,0x00,0x00,0x41,0x7f,0x41,0x00,0x00,0x30,0x40,0x40,
    0x40,0x3f,0x00,0x7f,0x08,0x14,0x22,0x41,0x00,0x7f,0x40,0x40,0x40,0x40,0x00,0x7f,
    0x02,0x04,0x02,0x7f,0x00,0x7f,0x02,0x04,0x08,0x7f,0x00,0x3e,0x41,0x41,0x41,0x3e,
    0x00,0x7f,0x09,0x09,0x09,0x06,0x00,0x3e,0x41,0x51,0x21,0x5e,0x00,0x7f,0x09,0x09,
    0x19,0x66,0x00,0x26,0x49,0x49,0x49,0x32,0x00,0x01,0x01,0x7f,0x01,0x01,0x00,0x3f,
    0x40,0x40,0x40,0x3f,0x00,0x1f,0x20,0x40,0x20,0x1f,0x00,0x3f,0x40,0x3c,0x40,0x3f,
    0x00,0x63,0x14,0x08,0x14,0x63,0x00,0x07,0x08,0x70,0x08,0x07,0x00,0x71,0x49,0x45,
    0x43,0x00,0x00,0x00,0x7f,0x41,0x41,0x00,0x00,0x02,0x04,0x08,0x10,0x20,0x00,0x00,
    0x41,0x41,0x7f,0x00,0x00,0x04,0x02,0x01,0x02,0x04,0x00,0x80,0x80,0x80,0x80,0x80,
    0x00,0x00,0x03,0x07,0x00,0x00,0x00,0x20,0x54,0x54,0x54,0x78,0x00,0x7f,0x44,0x44,
    0x44,0x38,0x00,0x38,0x44,0x44,0x44,0x28,0x00,0x38,0x44,0x44,0x44,0x7f,0x00,0x38,
    0x54,0x54,0x54,0x08,0x00,0x08,0x7e,0x09,0x09,0x00,0x00,0x18,0xa4,0xa4,0xa4,0x7c,
    0x00,0x7f,0x04,0x04,0x78,0x00,0x00,0x00,0x00,0x7d,0x40,0x00,0x00,0x40,0x80,0x84,
    0x7d,0x00,0x00,0x7f,0x10,0x28,0x44,0x00,0x00,0x00,0x00,0x7f,0x40,0x00,0x00,0x7c,
    0x04,0x18,0x04,0x78,0x00,0x7c,0x04,0x04,0x78,0x00,0x00,0x38,0x44,0x44,0x44,0x38,
    0x00,0xfc,0x44,0x44,0x44,0x38,0x00,0x38,0x44,0x44,0x44,0xfc,0x00,0x44,0x78,0x44,
    0x04,0x08,0x00,0x08,0x54,0x54,0x54,0x20,0x00,0x04,0x3e,0x44,0x24,0x00,0x00,0x3c,
    0x40,0x20,0x7c,0x00,0x00,0x1c,0x20,0x40,0x20,0x1c,0x00,0x3c,0x60,0x30,0x60,0x3c,
    0x00,0x6c,0x10,0x10,0x6c,0x00,0x00,0x9c,0xa0,0x60,0x3c,0x00,0x00,0x64,0x54,0x54,
    0x4c,0x00,0x00,0x08,0x3e,0x41,0x41,0x00,0x00,0x00,0x00,0x77,0x00,0x00,0x00,0x00,
    0x41,0x41,0x3e,0x08,0x00,0x02,0x01,0x02,0x01,0x00,0x00,0x3c,0x26,0x23,0x26,0x3c};

// wrapper/adapter functions to make the code work on Linux
#ifdef _LINUX_
static int digitalRead(int iPin)
{
  return AIOReadGPIO(iPin);
} /* digitalRead() */

static void digitalWrite(int iPin, int iState)
{
   AIOWriteGPIO(iPin, iState);
} /* digitalWrite() */

static void pinMode(int iPin, int iMode)
{
   AIOAddGPIO(iPin, iMode);
} /* pinMode() */

static void delayMicroseconds(int iMS)
{
  usleep(iMS);
} /* delayMicroseconds() */

static uint8_t pgm_read_byte(uint8_t *ptr)
{
  return *ptr;
}
static int16_t pgm_read_word(uint8_t *ptr)
{
  return ptr[0] + (ptr[1]<<8);
}
#endif // _LINUX_
//
// Provide a small temporary buffer for use by the graphics functions
//
void spilcdSetTXBuffer(uint8_t *pBuf, int iSize)
{
  ucTXBuf = pBuf;
  iTXBufSize = iSize;
} /* spilcdSetTXBuffer() */

// Sets the D/C pin to data or command mode
void spilcdSetMode(int iMode)
{
#ifdef __AVR__
    if (iMode == MODE_DATA)
       *outDC |= bitDC;
    else
       *outDC &= ~bitDC;
#else
	myPinWrite(iDCPin, iMode == MODE_DATA);
#endif
#ifdef HAL_ESP32_HAL_H_
	delayMicroseconds(1); // some systems are so fast that it needs to be delayed
#endif
} /* spilcdSetMode() */

// List of command/parameters to initialize the ST7789 LCD
const unsigned char uc240x240InitList[]PROGMEM = {
    1, 0x13, // partial mode off
    1, 0x21, // display inversion off
    2, 0x36,0x08,    // memory access 0xc0 for 180 degree flipped
    2, 0x3a,0x55,    // pixel format; 5=RGB565
    3, 0x37,0x00,0x00, //
    6, 0xb2,0x0c,0x0c,0x00,0x33,0x33, // Porch control
    2, 0xb7,0x35,    // gate control
    2, 0xbb,0x1a,    // VCOM
    2, 0xc0,0x2c,    // LCM
    2, 0xc2,0x01,    // VDV & VRH command enable
    2, 0xc3,0x0b,    // VRH set
    2, 0xc4,0x20,    // VDV set
    2, 0xc6,0x0f,    // FR control 2
    3, 0xd0, 0xa4, 0xa1,     // Power control 1
    15, 0xe0, 0x00,0x19,0x1e,0x0a,0x09,0x15,0x3d,0x44,0x51,0x12,0x03,
        0x00,0x3f,0x3f,     // gamma 1
    15, 0xe1, 0x00,0x18,0x1e,0x0a,0x09,0x25,0x3f,0x43,0x52,0x33,0x03,
        0x00,0x3f,0x3f,        // gamma 2
    1, 0x29,    // display on
    0
};
// List of command/parameters to initialize the ili9341 display
const unsigned char uc240InitList[]PROGMEM = {
        4, 0xEF, 0x03, 0x80, 0x02,
        4, 0xCF, 0x00, 0XC1, 0X30,
        5, 0xED, 0x64, 0x03, 0X12, 0X81,
        4, 0xE8, 0x85, 0x00, 0x78,
        6, 0xCB, 0x39, 0x2C, 0x00, 0x34, 0x02,
        2, 0xF7, 0x20,
        3, 0xEA, 0x00, 0x00,
        2, 0xc0, 0x23, // Power control
        2, 0xc1, 0x10, // Power control
        3, 0xc5, 0x3e, 0x28, // VCM control
        2, 0xc7, 0x86, // VCM control2
        2, 0x36, 0x48, // Memory Access Control
        1, 0x20,        // non inverted
        2, 0x3a, 0x55,
        3, 0xb1, 0x00, 0x18,
        4, 0xb6, 0x08, 0x82, 0x27, // Display Function Control
        2, 0xF2, 0x00, // Gamma Function Disable
        2, 0x26, 0x01, // Gamma curve selected
        16, 0xe0, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08,
                0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00, // Set Gamma
        16, 0xe1, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07,
                0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F, // Set Gamma
        3, 0xb1, 0x00, 0x10, // FrameRate Control 119Hz
        0
};
// Init sequence for the SSD1331 OLED display
// Courtesy of Adafruit's SSD1331 library
const unsigned char ucSSD1331InitList[] PROGMEM = {
    1, 0xae,    // display off
    1, 0xa0,    // set remap
    1, 0x72,    // RGB 0x76 == BGR
    2, 0xa1, 0x00,  // set start line
    2, 0xa2, 0x00,  // set display offset
    1, 0xa4,    // normal display
    2, 0xa8, 0x3f,  // set multiplex 1/64 duty
    2, 0xad, 0x8e, // set master configuration
    2, 0xb0, 0x0b, // disable power save
    2, 0xb1, 0x31, // phase period adjustment
    2, 0xb3, 0xf0, // clock divider
    2, 0x8a, 0x64, // precharge a
    2, 0x8b, 0x78, // precharge b
    2, 0x8c, 0x64, // precharge c
    2, 0xbb, 0x3a, // set precharge level
    2, 0xbe, 0x3e, // set vcomh
    2, 0x87, 0x06, // master current control
    2, 0x81, 0x91, // contrast for color a
    2, 0x82, 0x50, // contrast for color b
    2, 0x83, 0x7D, // contrast for color c
    1, 0xAF, // display on, normal
    0,0
};
// List of command/parameters to initialize the SSD1351 OLED display
const unsigned char ucOLEDInitList[] PROGMEM = {
	2, 0xfd, 0x12, // unlock the controller
	2, 0xfd, 0xb1, // unlock the command
	1, 0xae,	// display off
	2, 0xb3, 0xf1,  // clock divider
	2, 0xca, 0x7f,	// mux ratio
	2, 0xa0, 0x74,	// set remap
	3, 0x15, 0x00, 0x7f,	// set column
	3, 0x75, 0x00, 0x7f,	// set row
	2, 0xb5, 0x00,	// set GPIO state
	2, 0xab, 0x01,	// function select (internal diode drop)
	2, 0xb1, 0x32,	// precharge
	2, 0xbe, 0x05,	// vcomh
	1, 0xa6,	// set normal display mode
	4, 0xc1, 0xc8, 0x80, 0xc8, // contrast ABC
	2, 0xc7, 0x0f,	// contrast master
	4, 0xb4, 0xa0,0xb5,0x55,	// set VSL
	2, 0xb6, 0x01,	// precharge 2
	1, 0xaf,	// display ON
	0};
// List of command/parameters for the SSD1283A display
const unsigned char uc132InitList[]PROGMEM = {
    3, 0x10, 0x2F,0x8E,
    3, 0x11, 0x00,0x0C,
    3, 0x07, 0x00,0x21,
    3, 0x28, 0x00,0x06,
    3, 0x28, 0x00,0x05,
    3, 0x27, 0x05,0x7F,
    3, 0x29, 0x89,0xA1,
    3, 0x00, 0x00,0x01,
    LCD_DELAY, 100,
    3, 0x29, 0x80,0xB0,
    LCD_DELAY, 30,
    3, 0x29, 0xFF,0xFE,
    3, 0x07, 0x02,0x23,
    LCD_DELAY, 30,
    3, 0x07, 0x02,0x33,
    3, 0x01, 0x21,0x83,
    3, 0x03, 0x68,0x30,
    3, 0x2F, 0xFF,0xFF,
    3, 0x2C, 0x80,0x00,
    3, 0x27, 0x05,0x70,
    3, 0x02, 0x03,0x00,
    3, 0x0B, 0x58,0x0C,
    3, 0x12, 0x06,0x09,
    3, 0x13, 0x31,0x00,
    0
};

// List of command/parameters to initialize the ili9342 display
const unsigned char uc320InitList[]PROGMEM = {
        2, 0xc0, 0x23, // Power control
        2, 0xc1, 0x10, // Power control
        3, 0xc5, 0x3e, 0x28, // VCM control
        2, 0xc7, 0x86, // VCM control2
        2, 0x36, 0x08, // Memory Access Control (flip x/y/bgr/rgb)
        2, 0x3a, 0x55,
	1, 0x21,	// inverted display off
//        2, 0x26, 0x01, // Gamma curve selected
//        16, 0xe0, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08,
//                0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00, // Set Gamma
//        16, 0xe1, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07,
//                0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F, // Set Gamma
//        3, 0xb1, 0x00, 0x10, // FrameRate Control 119Hz
        0
};

// List of command/parameters to initialize the st7735s display
const unsigned char uc80InitList[]PROGMEM = {
//        4, 0xb1, 0x01, 0x2c, 0x2d,    // frame rate control
//        4, 0xb2, 0x01, 0x2c, 0x2d,    // frame rate control (idle mode)
//        7, 0xb3, 0x01, 0x2c, 0x2d, 0x01, 0x2c, 0x2d, // frctrl - partial mode
//        2, 0xb4, 0x07,    // non-inverted
//        4, 0xc0, 0xa2, 0x02, 0x84,    // power control
//        2, 0xc1, 0xc5,     // pwr ctrl2
//        2, 0xc2, 0x0a, 0x00, // pwr ctrl3
//        3, 0xc3, 0x8a, 0x2a, // pwr ctrl4
//        3, 0xc4, 0x8a, 0xee, // pwr ctrl5
//        2, 0xc5, 0x0e,        // vm ctrl1
    2, 0x3a, 0x05,    // pixel format RGB565
    2, 0x36, 0xc0, // MADCTL
    5, 0x2a, 0x00, 0x02, 0x00, 0x7f+0x02, // column address start
    5, 0x2b, 0x00, 0x01, 0x00, 0x9f+0x01, // row address start
    17, 0xe0, 0x09, 0x16, 0x09,0x20,
    0x21,0x1b,0x13,0x19,
    0x17,0x15,0x1e,0x2b,
    0x04,0x05,0x02,0x0e, // gamma sequence
    17, 0xe1, 0x0b,0x14,0x08,0x1e,
    0x22,0x1d,0x18,0x1e,
    0x1b,0x1a,0x24,0x2b,
    0x06,0x06,0x02,0x0f,
    1, 0x21,    // display inversion on
    0
};
// List of command/parameters to initialize the st7735r display
const unsigned char uc128InitList[]PROGMEM = {
//	4, 0xb1, 0x01, 0x2c, 0x2d,	// frame rate control
//	4, 0xb2, 0x01, 0x2c, 0x2d,	// frame rate control (idle mode)
//	7, 0xb3, 0x01, 0x2c, 0x2d, 0x01, 0x2c, 0x2d, // frctrl - partial mode
//	2, 0xb4, 0x07,	// non-inverted
//	4, 0xc0, 0x82, 0x02, 0x84,	// power control
//	2, 0xc1, 0xc5, 	// pwr ctrl2
//	2, 0xc2, 0x0a, 0x00, // pwr ctrl3
//	3, 0xc3, 0x8a, 0x2a, // pwr ctrl4
//	3, 0xc4, 0x8a, 0xee, // pwr ctrl5
//	2, 0xc5, 0x0e,		// pwr ctrl
//	1, 0x20,	// display inversion off
	2, 0x3a, 0x55,	// pixel format RGB565
	2, 0x36, 0xc0, // MADCTL
	17, 0xe0, 0x09, 0x16, 0x09,0x20,
		0x21,0x1b,0x13,0x19,
		0x17,0x15,0x1e,0x2b,
		0x04,0x05,0x02,0x0e, // gamma sequence
	17, 0xe1, 0x0b,0x14,0x08,0x1e,
		0x22,0x1d,0x18,0x1e,
		0x1b,0x1a,0x24,0x2b,
		0x06,0x06,0x02,0x0f,
	0
};
// List of command/parameters to initialize the ILI9486 display
const unsigned char ucILI9486InitList[] PROGMEM = {
   2, 0x01, 0x00,
   LCD_DELAY, 50,
   2, 0x28, 0x00,
   3, 0xc0, 0xd, 0xd,
   3, 0xc1, 0x43, 0x00,
   2, 0xc2, 0x00,
   3, 0xc5, 0x00, 0x48,
   4, 0xb6, 0x00, 0x22, 0x3b,
   16, 0xe0,0x0f,0x24,0x1c,0x0a,0x0f,0x08,0x43,0x88,0x32,0x0f,0x10,0x06,0x0f,0x07,0x00,
   16, 0xe1,0x0f,0x38,0x30,0x09,0x0f,0x0f,0x4e,0x77,0x3c,0x07,0x10,0x05,0x23,0x1b,0x00,
   1, 0x20,
   2, 0x36,0x0a,
   2, 0x3a,0x55,
   1, 0x11,
   LCD_DELAY, 150,
   1, 0x29,
   LCD_DELAY, 25,
   0
};
// List of command/parameters to initialize the hx8357 display
const unsigned char uc480InitList[]PROGMEM = {
	2, 0x3a, 0x55,
	2, 0xc2, 0x44,
	5, 0xc5, 0x00, 0x00, 0x00, 0x00,
	16, 0xe0, 0x0f, 0x1f, 0x1c, 0x0c, 0x0f, 0x08, 0x48, 0x98, 0x37,
		0x0a,0x13, 0x04, 0x11, 0x0d, 0x00,
	16, 0xe1, 0x0f, 0x32, 0x2e, 0x0b, 0x0d, 0x05, 0x47, 0x75, 0x37,
		0x06, 0x10, 0x03, 0x24, 0x20, 0x00,
	16, 0xe2, 0x0f, 0x32, 0x2e, 0x0b, 0x0d, 0x05, 0x47, 0x75, 0x37,
		0x06, 0x10, 0x03, 0x24, 0x20, 0x00,
	2, 0x36, 0x48,
	0	
};

//
// 16-bit memset
//
void memset16(uint16_t *pDest, uint16_t usPattern, int iCount)
{
    while (iCount--)
        *pDest++ = usPattern;
}

//
// Wrapper function for writing to SPI
//
static void myspiWrite(unsigned char *pBuf, int iLen, int iMode, int bRender)
{
    if (iMode == MODE_DATA && pBackBuffer != NULL && !bSetPosition) // write it to the back buffer
    {
        uint16_t *s, *d;
        int j, iOff, iStrip, iMaxX, iMaxY, i;
        iMaxX = iWindowX + iWindowCX;
        iMaxY = iWindowY + iWindowCY;
        iOff = 0;
        i = iLen/2;
        while (i > 0)
        {
            iStrip = iMaxX - iCurrentX; // max pixels that can be written in one shot
            if (iStrip > i)
                iStrip = i;
            s = (uint16_t *)&pBuf[iOff];
            d = (uint16_t *)&pBackBuffer[iOffset];
            if (iOffset > iMaxOffset || (iOffset+iStrip*2) > iMaxOffset)
            { // going to write past the end of memory, don't even try
                i = iStrip = 0;
            }
            for (j=0; j<iStrip; j++) // memcpy could be much slower for small runs
            {
                *d++ = *s++;
            }
            iOffset += iStrip*2; iOff += iStrip*2;
            i -= iStrip;
            iCurrentX += iStrip;
            if (iCurrentX >= iMaxX) // need to wrap around to the next line
            {
                iCurrentX = iWindowX;
                iCurrentY++;
                if (iCurrentY >= iMaxY)
                    iCurrentY = iWindowY;
                iOffset = (iScreenPitch * iCurrentY) + (iCurrentX * 2);
            }
        }
    }
    if (!bRender)
        return; // don't write it to the display
    if (pfnDataCallback != NULL)
    {
       (*pfnDataCallback)(pBuf, iLen, iMode);
       return;
    } 
    if (iCSPin != -1)
    {
#ifdef __AVR__
      *outCS &= ~bitCS;
#else
        myPinWrite(iCSPin, 0);
#endif // __AVR__
    }
#ifdef ESP32_DMA
    esp_err_t ret;
static spi_transaction_t t;
    memset(&t, 0, sizeof(t));       //Zero out the transaction
    t.length=iLen*8;  // length in bits
    t.tx_buffer=pBuf;
    t.user=(void*)iMode;
    // Queue the transaction
//    ret = spi_device_queue_trans(spi, &t, portMAX_DELAY);
    ret=spi_device_polling_transmit(spi, &t);  //Transmit!
    assert(ret==ESP_OK);            //Should have had no issues.
    // wait for it to complete
//    spi_transaction_t *rtrans;
//    spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY);
#else
    if (iMode == MODE_COMMAND)
        spilcdSetMode(MODE_COMMAND);
#ifdef _LINUX_
    AIOWriteSPI(iHandle, pBuf, iLen);
#else
    SPI.beginTransaction(SPISettings(iSPISpeed, MSBFIRST, iSPIMode));
#ifdef HAL_ESP32_HAL_H_
    SPI.transferBytes(pBuf, ucRXBuf, iLen);
#else
    SPI.transfer(pBuf, iLen);
#endif
    SPI.endTransaction();
#endif // _LINUX_
    if (iMode == MODE_COMMAND) // restore D/C pin to DATA
        spilcdSetMode(MODE_DATA);
#endif
    if (iCSPin != -1)
    {
#ifdef __AVR__
       *outCS |= bitCS;
#else
       myPinWrite(iCSPin, 1);
#endif
    }
} /* myspiWrite() */

//
// Public wrapper function to write data to the display
//
void spilcdWriteDataBlock(uint8_t *pData, int iLen, int bRender)
{
  myspiWrite(pData, iLen, MODE_DATA, bRender);
} /* spilcdWriteDataBlock() */

//
// Wrapper function to control a GPIO line
//
static void myPinWrite(int iPin, int iValue)
{
	digitalWrite(iPin, (iValue) ? HIGH: LOW);
} /* myPinWrite() */

//
// Choose the gamma curve between 2 choices (0/1)
// ILI9341 only
//
int spilcdSetGamma(int iMode)
{
int i;
unsigned char *sE0, *sE1;

	if (iMode < 0 || iMode > 1 || iLCDType != LCD_ILI9341)
		return 1;
	if (iMode == 0)
	{
		sE0 = (unsigned char *)ucE0_0;
		sE1 = (unsigned char *)ucE1_0;
	}
	else
	{
		sE0 = (unsigned char *)ucE0_1;
		sE1 = (unsigned char *)ucE1_1;
	}
	spilcdWriteCommand(0xe0);
	for(i=0; i<16; i++)
	{
		spilcdWriteData8(pgm_read_byte(sE0++));
	}
	spilcdWriteCommand(0xe1);
	for(i=0; i<16; i++)
	{
		spilcdWriteData8(pgm_read_byte(sE1++));
	}

	return 0;
} /* spilcdSetGamma() */
//
// Configure a GPIO pin for input
// Returns 0 if successful, -1 if unavailable
// all input pins are assumed to use internal pullup resistors
// and are connected to ground when pressed
//
int spilcdConfigurePin(int iPin)
{
        if (iPin == -1) // invalid
                return -1;
        pinMode(iPin, INPUT_PULLUP);
        return 0;
} /* spilcdConfigurePin() */
// Read from a GPIO pin
int spilcdReadPin(int iPin)
{
   if (iPin == -1)
      return -1;
   return (digitalRead(iPin) == HIGH);
} /* spilcdReadPin() */
#ifdef ESP32_DMA
//This function is called (in irq context!) just before a transmission starts. It will
//set the D/C line to the value indicated in the user field.
static void spi_pre_transfer_callback(spi_transaction_t *t)
{
    int iMode=(int)t->user;
    spilcdSetMode(iMode);
}
//static void spi_post_transfer_callback(spi_transaction_t *t)
//{
//}
#endif
//
// Give bb_spi_lcd two callback functions to talk to the LCD
// useful when not using SPI or providing an optimized interface
//
void spilcdSetCallbacks(RESETCALLBACK pfnReset, DATACALLBACK pfnData)
{
   pfnDataCallback = pfnData;
   pfnResetCallback = pfnReset;
}
//
// Initialize the LCD controller and clear the display
// LED pin is optional - pass as -1 to disable
//
int spilcdInit(int iType, int bFlipRGB, int bInvert, int bFlipped, int32_t iSPIFreq, int iCS, int iDC, int iReset, int iLED, int iMISOPin, int iMOSIPin, int iCLKPin)
{
unsigned char *s, *d;
int i, iCount;
   
   iMemoryX = iMemoryY = 0;
   iLCDType = iType;

  if (pfnResetCallback != NULL)
  {
     (*pfnResetCallback)();
     goto start_of_init;
  }
#ifdef __AVR__
(void)iMISOPin; (void)iMOSIPin; (void)iCLKPin;
{ // setup fast I/O
  uint8_t port;
    port = digitalPinToPort(iDC);
    outDC = portOutputRegister(port);
    bitDC = digitalPinToBitMask(iDC);
    if (iCS != -1) {
      port = digitalPinToPort(iCS);
      outCS = portOutputRegister(port);
      bitCS = digitalPinToBitMask(iCS);
    }
}
#endif

	iLEDPin = -1; // assume it's not defined
	if (iType <= LCD_INVALID || iType >= LCD_VALID_MAX)
	{
#ifndef _LINUX_
		Serial.println("Unsupported display type\n");
#endif // _LINUX_
		return -1;
	}
#ifndef _LINUX_
	iSPIMode = (iType == LCD_ST7789_NOCS) ? SPI_MODE3 : SPI_MODE0;
#endif
	iSPISpeed = iSPIFreq;
	iScrollOffset = 0; // current hardware scroll register value

	iDCPin = iDC;
	iCSPin = iCS;
	iResetPin = iReset;
	iLEDPin = iLED;

	if (iDCPin == -1)
	{
#ifndef _LINUX_
		Serial.println("One or more invalid GPIO pin numbers\n");
#endif
		return -1;
	}
#ifdef ESP32_DMA
    esp_err_t ret;
    spi_device_interface_config_t devcfg;
    spi_bus_config_t buscfg;
    
    memset(&buscfg, 0, sizeof(buscfg));
    buscfg.miso_io_num = iMISOPin;
    buscfg.mosi_io_num = iMOSIPin;
    buscfg.sclk_io_num = iCLKPin;
    buscfg.max_transfer_sz=240*9*2;
    buscfg.quadwp_io_num=-1;
    buscfg.quadhd_io_num=-1;
    //Initialize the SPI bus
    ret=spi_bus_initialize(VSPI_HOST, &buscfg, 1);
    assert(ret==ESP_OK);
    
    memset(&devcfg, 0, sizeof(devcfg));
    devcfg.clock_speed_hz = iSPIFreq;
    devcfg.mode = iSPIMode;                         //SPI mode 0 or 3
    devcfg.spics_io_num = iCS;               //CS pin
    devcfg.queue_size = 7;                          //We want to be able to queue 7 transactions at a time
    devcfg.pre_cb = spi_pre_transfer_callback;  //Specify pre-transfer callback to handle D/C line
//    devcfg.post_cb = spi_post_transfer_callback;
    devcfg.flags = SPI_DEVICE_HALFDUPLEX;
    //Attach the LCD to the SPI bus
    ret=spi_bus_add_device(VSPI_HOST, &devcfg, &spi);
    assert(ret==ESP_OK);
    memset(&trans[0], 0, sizeof(spi_transaction_t));
#else
#ifdef HAL_ESP32_HAL_H_
    SPI.begin(iCLKPin, iMISOPin, iMOSIPin, iCS);
#else
#ifdef _LINUX_
    iHandle = AIOOpenSPI(0, iSPIFreq); // DEBUG - open SPI channel 0 
#else
    SPI.begin(); // simple Arduino init (e.g. AVR)
#endif // _LINUX_
#endif
#endif
    
	pinMode(iCSPin, OUTPUT);
	pinMode(iDCPin, OUTPUT);
	if (iResetPin != -1)
		pinMode(iResetPin, OUTPUT);
	if (iLEDPin != -1)
		pinMode(iLEDPin, OUTPUT);

	if (iResetPin != -1)
	{
		myPinWrite(iResetPin, 1);
		delayMicroseconds(60000);
		myPinWrite(iResetPin, 0); // reset the controller
		delayMicroseconds(60000);
		myPinWrite(iResetPin, 1);
		delayMicroseconds(60000);
	}

	if (iLCDType != LCD_SSD1351 && iLCDType != LCD_SSD1331) // no backlight and no soft reset on OLED
	{
	if (iLEDPin != -1)
		myPinWrite(iLEDPin, 1); // turn on the backlight

	spilcdWriteCommand(0x01); // software reset
	delayMicroseconds(60000);

	spilcdWriteCommand(0x11);
	delayMicroseconds(60000);
	delayMicroseconds(60000);
	}
start_of_init:
    d = &ucRXBuf[256]; // point to middle otherwise full duplex SPI will overwrite our data
	if (iLCDType == LCD_ST7789 || iLCDType == LCD_ST7789_135 || iLCDType == LCD_ST7789_NOCS)
	{
        uint8_t iBGR = (bFlipRGB) ? 8:0;
		s = (unsigned char *)&uc240x240InitList[0];
        memcpy_P(d, s, sizeof(uc240x240InitList));
        s = d;
		if (bFlipped)
			s[6] = 0xc0 + iBGR; // flip 180
		else
			s[6] = 0x00 + iBGR;
		if (iLCDType == LCD_ST7789 || iLCDType == LCD_ST7789_NOCS)
		{
			iCurrentWidth = iWidth = 240;
			iCurrentHeight = iHeight = 240;
		}
		else
		{
			iCurrentWidth = iWidth = 135;
			iCurrentHeight = iHeight = 240;
		}
        if (iLCDType == LCD_ST7789_NOCS)
        {
            iMemoryY = 80; // 80 pixel offset to visible portion of display
            iLCDType = LCD_ST7789; // the rest of the behavior is the same
        }
        else if (iLCDType == LCD_ST7789_135)
        {
            iMemoryX = 52;
            iMemoryY = 40;
            iLCDType = LCD_ST7789;
        }
	} // ST7789
    else if (iLCDType == LCD_SSD1331)
    {
        s = (unsigned char *)ucSSD1331InitList;
        memcpy_P(d, ucSSD1331InitList, sizeof(ucSSD1331InitList));
        s = d;

        iCurrentWidth = iWidth = 96;
        iCurrentHeight = iHeight = 64;

        if (bFlipRGB)
        { // copy to RAM to modify it
            s[6] = 0x76;
        }
    }
	else if (iLCDType == LCD_SSD1351)
	{
		s = (unsigned char *)ucOLEDInitList; // do the commands manually
                memcpy_P(d, s, sizeof(ucOLEDInitList));
                iCurrentWidth = iWidth = 128;
                iCurrentHeight = iHeight = 128;
	}
    // Send the commands/parameters to initialize the LCD controller
	else if (iLCDType == LCD_ILI9341)
	{  // copy to RAM to modify
            s = (unsigned char *)uc240InitList;
            memcpy_P(d, s, sizeof(uc240InitList));
            s = d;
        if (bInvert)
            s[52] = 0x21; // invert pixels
        else
            s[52] = 0x20; // non-inverted
		if (bFlipped)
			s[50] = 0x88; // flip 180
		else
			s[50] = 0x48; // normal orientation
		iCurrentWidth = iWidth = 240;
		iCurrentHeight = iHeight = 320;
	}
        else if (iLCDType == LCD_SSD1283A)
        {
                s = (unsigned char *)uc132InitList;
                memcpy_P(d, s, sizeof(uc132InitList));
                iCurrentWidth = iWidth = 132;
                iCurrentHeight = iHeight = 132;
        }
	else if (iLCDType == LCD_ILI9342)
	{
		s = (unsigned char *)uc320InitList;
                memcpy_P(d, s, sizeof(uc320InitList));
                s = d;
		if (bFlipped)
			s[15] = 0xc8; // flip 180
		else
			s[15] = 0x08; // normal orientation
		iCurrentWidth = iWidth = 320;
		iCurrentHeight = iHeight = 240;
	}
	else if (iLCDType == LCD_HX8357)
	{
                spilcdWriteCommand(0xb0);
                spilcdWriteData16(0x00FF, 1);
                spilcdWriteData16(0x0001, 1);
                delayMicroseconds(60000);

		s = (unsigned char *)uc480InitList;
                memcpy_P(d, s, sizeof(uc480InitList));
                s = d;
		if (bFlipped)
			s[65] = 0x88; // flip 180
		else
			s[65] = 0x48; // normal orientation
		iCurrentWidth = iWidth = 320;
		iCurrentHeight = iHeight = 480;
	}
        else if (iLCDType == LCD_ILI9486)
        {
            uint8_t ucBGRFlags;
            s = (unsigned char *)ucILI9486InitList;
            memcpy_P(d, s, sizeof(ucILI9486InitList));
            s = d;
            ucBGRFlags = 0xa; // normal direction, RGB color order
            if (bInvert)
               d[63] = 0x21; // invert display command
            if (bFlipRGB || bFlipped)
            {
               if (bFlipRGB)
                  ucBGRFlags |= 8;
               if (bFlipped) // rotate 180
                  ucBGRFlags ^= 0xc0;
               d[66] = ucBGRFlags;
            }
            iCurrentWidth = iWidth = 320;
            iCurrentHeight = iHeight = 480;
        }
    else if (iLCDType == LCD_ST7735S || iLCDType == LCD_ST7735S_B)
    {
        uint8_t iBGR = 0;
        if (bFlipRGB)
            iBGR = 8;
        s = (unsigned char *)uc80InitList;
        memcpy_P(d, s, sizeof(uc80InitList));
        s = d;
        if (bInvert)
           s[55] = 0x21; // invert on
        else
           s[55] = 0x20; // invert off
        if (bFlipped)
            s[5] = 0xc0 + iBGR; // flipped 180 degrees
        else
            s[5] = 0x00 + iBGR; // normal orientation
        iCurrentWidth = iWidth = 80;
        iCurrentHeight = iHeight = 160;
        if (iLCDType == LCD_ST7735S_B)
        {
            iLCDType = LCD_ST7735S; // the rest is the same
            iMemoryX = 26; // x offset of visible area
            iMemoryY = 1;
        }
        else
        {
            iMemoryX = 24;
        }
    }
	else // ST7735R
	{
		s = (unsigned char *)uc128InitList;
                memcpy_P(d, s, sizeof(uc128InitList));
                s = d;
		if (bFlipped)
			s[5] = 0x00; // flipped 180 degrees
		else
			s[5] = 0xc0; // normal orientation
		iCurrentWidth = iWidth = 128;
		iCurrentHeight = iHeight = 160;
	}

	iCount = 1;
    bSetPosition = 1; // don't let the data writes affect RAM
    s = d; // start of RAM copy of our data
	while (iCount)
	{
		iCount = *s++;
		if (iCount != 0)
		{
               unsigned char uc;
               if (iCount == LCD_DELAY)
               {
                 uc = *s++;
                 delay(uc);
               }
               else
               {
                 uc = *s++;
                   spilcdWriteCommand(uc);
                 for (i=0; i<iCount-1; i++)
                 {
                    uc = *s++;
                 // hackhackhack
                 // the ssd1331 is kind of like the ssd1306 in that it expects the parameters
                 // to each command to have the DC pin LOW while being sent
                    if (iLCDType != LCD_SSD1331)
                        spilcdWriteData8(uc);
                    else
                        spilcdWriteCommand(uc);
                 } // for i
              }
          }
	  }
        bSetPosition = 0;
	if (iLCDType != LCD_SSD1351 && iLCDType != LCD_SSD1331)
	{
		spilcdWriteCommand(0x11); // sleep out
		delayMicroseconds(60000);
		spilcdWriteCommand(0x29); // Display ON
		delayMicroseconds(10000);
	}

	spilcdFill(0, 1); // erase memory
	spilcdScrollReset();
   
	return 0;

} /* spilcdInit() */

//
// Reset the scroll position to 0
//
void spilcdScrollReset(void)
{
	iScrollOffset = 0;
	if (iLCDType == LCD_SSD1351)
	{
		spilcdWriteCommand(0xa1); // set scroll start line
		spilcdWriteData8(0x00);
		spilcdWriteCommand(0xa2); // display offset
		spilcdWriteData8(0x00);
		return;
	}
    else if (iLCDType == LCD_SSD1331)
    {
        spilcdWriteCommand(0xa1);
        spilcdWriteCommand(0x00);
        spilcdWriteCommand(0xa2);
        spilcdWriteCommand(0x00);
        return;
    }
	spilcdWriteCommand(0x37); // scroll start address
	spilcdWriteData16(0, 1);
	if (iLCDType == LCD_HX8357)
	{
		spilcdWriteData16(0, 1);
	}
} /* spilcdScrollReset() */

//
// Scroll the screen N lines vertically (positive or negative)
// The value given represents a delta which affects the current scroll offset
// If iFillColor != -1, the newly exposed lines will be filled with that color
//
void spilcdScroll(int iLines, int iFillColor)
{
	iScrollOffset = (iScrollOffset + iLines) % iHeight;
	if (iLCDType == LCD_SSD1351)
	{
		spilcdWriteCommand(0xa1); // set scroll start line
		spilcdWriteData8(iScrollOffset);
		return;
	}
    else if (iLCDType == LCD_SSD1331)
    {
        spilcdWriteCommand(0xa1);
        spilcdWriteCommand(iScrollOffset);
        return;
    }
	else
	{
		spilcdWriteCommand(0x37); // Vertical scrolling start address
		if (iLCDType == LCD_ILI9341 || iLCDType == LCD_ILI9342 || iLCDType == LCD_ST7735R || iLCDType == LCD_ST7789 || iLCDType == LCD_ST7789_135 || iLCDType == LCD_ST7735S)
		{
			spilcdWriteData16(iScrollOffset, 1);
		}
		else
		{
			spilcdWriteData16(iScrollOffset >> 8, 1);
			spilcdWriteData16(iScrollOffset & -1, 1);
		}
	}
	if (iFillColor != -1) // fill the exposed lines
	{
	int i, iStart;
	uint16_t *usTemp = (uint16_t *)ucRXBuf;
	uint32_t *d;
	uint32_t u32Fill;
		// quickly prepare a full line's worth of the color
		u32Fill = (iFillColor >> 8) | ((iFillColor & -1) << 8);
		u32Fill |= (u32Fill << 16);
		d = (uint32_t *)&usTemp[0];
		for (i=0; i<iWidth/2; i++)
			*d++ = u32Fill;
		if (iLines < 0)
		{
			iStart = 0;
			iLines = 0 - iLines;
		}
		else
			iStart = iHeight - iLines;
		if (iOrientation == LCD_ORIENTATION_ROTATED)
			spilcdSetPosition(iStart, iWidth-1, iLines, iWidth, 1);
		else
			spilcdSetPosition(0, iStart, iWidth, iLines, 1);
		for (i=0; i<iLines; i++)
		{
			myspiWrite((unsigned char *)usTemp, iWidth*2, MODE_DATA, 1);
		}
	}

} /* spilcdScroll() */

//
// Draw a 1-bpp pattern with the given color and translucency
// 1 bits are drawn as color, 0 are transparent
// The translucency value can range from 1 (barely visible) to 32 (fully opaque)
// If there is a backbuffer, the bitmap is draw only into memory
// If there is no backbuffer, the bitmap is drawn on the screen with a black background color
//
void spilcdDrawPattern(uint8_t *pPattern, int iSrcPitch, int iDestX, int iDestY, int iCX, int iCY, uint16_t usColor, int iTranslucency)
{
    int x, y;
    uint8_t *s, uc, ucMask;
    uint16_t us, *d;
    uint32_t ulMask = 0x07e0f81f; // this allows all 3 values to be multipled at once
    uint32_t ulSrcClr, ulDestClr, ulDestTrans;
    
    ulDestTrans = 32-iTranslucency; // inverted to combine src+dest
    ulSrcClr = (usColor & 0xf81f) | ((uint32_t)(usColor & 0x06e0) << 16); // shift green to upper 16-bits
    ulSrcClr *= iTranslucency; // prepare for color blending
    if (iDestX+iCX > iCurrentWidth) // trim to fit on display
        iCX = (iCurrentWidth - iDestX);
    if (iDestY+iCY > iCurrentHeight)
        iCY = (iCurrentHeight - iDestY);
    if (pPattern == NULL || iDestX < 0 || iDestY < 0 || iCX <=0 || iCY <= 0 || iTranslucency < 1 || iTranslucency > 32)
        return;
    if (pBackBuffer == NULL) // no back buffer, draw opaque colors
    {
      uint16_t u16Clr;
      u16Clr = (usColor >> 8) | (usColor << 8); // swap low/high bytes
      spilcdSetPosition(iDestX, iDestY, iCX, iCY, 1);
      for (y=0; y<iCY; y++)
      {
        s = &pPattern[y * iSrcPitch];
        ucMask = uc = 0;
        d = (uint16_t *)&ucTXBuf[0];
        for (x=0; x<iCX; x++)
        {
            ucMask >>= 1;
            if (ucMask == 0)
            {
                ucMask = 0x80;
                uc = *s++;
            }
            if (uc & ucMask) // active pixel
               *d++ = u16Clr;
            else
               *d++ = 0;
        } // for x
        myspiWrite(ucTXBuf, iCX*2, MODE_DATA, 1);
      } // for y
      return;
    }
    for (y=0; y<iCY; y++)
    {
        int iDelta;
        if  (iOrientation == LCD_ORIENTATION_ROTATED)
        {
            iDelta = iScreenPitch/2;
            d = (uint16_t *)&pBackBuffer[(iDestX*iScreenPitch) + ((iCurrentHeight-iDestY-y)*2)];
        }
        else
        {
            iDelta = 1;
            d = (uint16_t *)&pBackBuffer[((iDestY+y)*iScreenPitch) + (iDestX*2)];
        }
        s = &pPattern[y * iSrcPitch];
        ucMask = uc = 0;
        for (x=0; x<iCX; x++)
        {
            ucMask >>= 1;
            if (ucMask == 0)
            {
                ucMask = 0x80;
                uc = *s++;
            }
            if (uc & ucMask) // active pixel
            {
                us = d[0]; // read destination pixel
                us = (us >> 8) | (us << 8); // fix the byte order
                // The fast way to combine 2 RGB565 colors
                ulDestClr = (us & 0xf81f) | ((uint32_t)(us & 0x06e0) << 16);
                ulDestClr = (ulDestClr * ulDestTrans);
                ulDestClr += ulSrcClr; // combine old and new colors
                ulDestClr = (ulDestClr >> 5) & ulMask; // done!
                ulDestClr = (ulDestClr >> 16) | (ulDestClr); // move green back into place
                us = (uint16_t)ulDestClr;
                us = (us >> 8) | (us << 8); // swap bytes for LCD
                d[0] = us;
            }
            d += iDelta;
        } // for x
    } // for y
    
}
void spilcdRectangle(int x, int y, int w, int h, unsigned short usColor1, unsigned short usColor2, int bFill, int bRender)
{
unsigned short *usTemp = (unsigned short *)ucRXBuf;
int i, ty, th, iPerLine, iStart;
uint16_t usColor;
    
	// check bounds
	if (x < 0 || x >= iCurrentWidth || x+w > iCurrentWidth)
		return; // out of bounds
	if (y < 0 || y >= iCurrentHeight || y+h > iCurrentHeight)
		return;

	ty = (iCurrentWidth == iWidth) ? y:x;
	th = (iCurrentWidth == iWidth) ? h:w;
	if (bFill)
	{
        int32_t iDR, iDG, iDB; // current colors and deltas
        int32_t iRAcc, iGAcc, iBAcc;
        uint16_t usRInc, usGInc, usBInc;
        iRAcc = iGAcc = iBAcc = 0; // color fraction accumulators
        iDB = (int32_t)(usColor2 & 0x1f) - (int32_t)(usColor1 & 0x1f); // color deltas
        usBInc = (iDB < 0) ? 0xffff : 0x0001;
        iDB = abs(iDB);
        iDR = (int32_t)(usColor2 >> 11) - (int32_t)(usColor1 >> 11);
        usRInc = (iDR < 0) ? 0xf800 : 0x0800;
        iDR = abs(iDR);
        iDG = (int32_t)((usColor2 & 0x06e0) >> 5) - (int32_t)((usColor1 & 0x06e0) >> 5);
        usGInc = (iDG < 0) ? 0xffe0 : 0x0020;
        iDG = abs(iDG);
        iDB = (iDB << 16) / th;
        iDR = (iDR << 16) / th;
        iDG = (iDG << 16) / th;
		iPerLine = (iCurrentWidth == iWidth) ? w:h; // line length
        spilcdSetPosition(x, y, w, h, bRender);
//	        if (((ty + iScrollOffset) % iHeight) > iHeight-th) // need to write in 2 parts since it won't wrap
//		{
//          	iStart = (iHeight - ((ty+iScrollOffset) % iHeight));
//			for (i=0; i<iStart; i++)
//           		myspiWrite((unsigned char *)usTemp, iStart*iPerLine, MODE_DATA, bRender); // first N lines
//			if (iCurrentWidth == iWidth)
//				spilcdSetPosition(x, y+iStart, w, h-iStart, bRender);
//			else
//				spilcdSetPosition(x+iStart, y, w-iStart, h, bRender);
//			for (i=0; i<th-iStart; i++)
 //         		myspiWrite((unsigned char *)usTemp, iPerLine, MODE_DATA, bRender);
//       		 }
//        	else // can write in one shot
//        	{
			for (i=0; i<th; i++)
            {
                usColor = (usColor1 >> 8) | (usColor1 << 8); // swap byte order
                memset16((uint16_t*)usTemp, usColor, iPerLine);
                myspiWrite((unsigned char *)usTemp, iPerLine*2, MODE_DATA, bRender);
                // Update the color components
                iRAcc += iDR;
                if (iRAcc >= 0x10000) // time to increment
                {
                    usColor1 += usRInc;
                    iRAcc -= 0x10000;
                }
                iGAcc += iDG;
                if (iGAcc >= 0x10000) // time to increment
                {
                    usColor1 += usGInc;
                    iGAcc -= 0x10000;
                }
                iBAcc += iDB;
                if (iBAcc >= 0x10000) // time to increment
                {
                    usColor1 += usBInc;
                    iBAcc -= 0x10000;
                }
            }
//        	}
	}
	else // outline
	{
        usColor = (usColor1 >> 8) | (usColor1 << 8); // swap byte order
		// draw top/bottom
		spilcdSetPosition(x, y, w, 1, bRender);
        memset16((uint16_t*)usTemp, usColor, w);
        myspiWrite((unsigned char *)usTemp, w*2, MODE_DATA, bRender);
		spilcdSetPosition(x, y + h-1, w, 1, bRender);
        memset16((uint16_t*)usTemp, usColor, w);
		myspiWrite((unsigned char *)usTemp, w*2, MODE_DATA, bRender);
		// draw left/right
		if (((ty + iScrollOffset) % iHeight) > iHeight-th)	
		{
			iStart = (iHeight - ((ty+iScrollOffset) % iHeight));
			spilcdSetPosition(x, y, 1, iStart, bRender);
            memset16((uint16_t*)usTemp, usColor, iStart);
			myspiWrite((unsigned char *)usTemp, iStart*2, MODE_DATA, bRender);
			spilcdSetPosition(x+w-1, y, 1, iStart, bRender);
            memset16((uint16_t*)usTemp, usColor, iStart);
			myspiWrite((unsigned char *)usTemp, iStart*2, MODE_DATA, bRender);
			// second half
			spilcdSetPosition(x,y+iStart, 1, h-iStart, bRender);
            memset16((uint16_t*)usTemp, usColor, h-iStart);
			myspiWrite((unsigned char *)usTemp, (h-iStart)*2, MODE_DATA, bRender);
			spilcdSetPosition(x+w-1, y+iStart, 1, h-iStart, bRender);
            memset16((uint16_t*)usTemp, usColor, h-iStart);
			myspiWrite((unsigned char *)usTemp, (h-iStart)*2, MODE_DATA, bRender);
		}
		else // can do it in 1 shot
		{
			spilcdSetPosition(x, y, 1, h, bRender);
            memset16((uint16_t*)usTemp, usColor, h);
			myspiWrite((unsigned char *)usTemp, h*2, MODE_DATA, bRender);
			spilcdSetPosition(x + w-1, y, 1, h, bRender);
            memset16((uint16_t*)usTemp, usColor, h);
			myspiWrite((unsigned char *)usTemp, h*2, MODE_DATA, bRender);
		}
	} // outline
} /* spilcdRectangle() */

//
// Show part or all of the back buffer on the display
// Used after delayed rendering of graphics
//
void spilcdShowBuffer(int iStartX, int iStartY, int cx, int cy)
{
    int x, y;
    uint8_t *s;
    
    if (pBackBuffer == NULL)
        return; // nothing to do
    if (iStartX + cx > iCurrentWidth || iStartY + cy > iCurrentHeight || iStartX < 0 || iStartY < 0)
        return; // invalid area
    spilcdSetPosition(iStartX, iStartY, cx, cy, 1);
    bSetPosition = 1;
    if (iOrientation == LCD_ORIENTATION_ROTATED) // coordinates are swapped
    {
        for (x=iStartX; x<iStartX+cx; x++)
        {
            s = &pBackBuffer[(x * iScreenPitch) + (iCurrentHeight - iStartY-cy)*2];
            myspiWrite(s, cy * 2, MODE_DATA, 1);
        }
    }
    else // normal orientation
    {
        for (y=iStartY; y<iStartY+cy; y++)
        {
            s = &pBackBuffer[(y * iScreenPitch) + iStartX*2];
            myspiWrite(s, cx * 2, MODE_DATA, 1);
        }
    } // normal orientation
    bSetPosition = 0;
} /* spilcdShowBuffer() */

//
// Sends a command to turn off the LCD display
// Turns off the backlight LED
// Closes the SPI file handle
//
void spilcdShutdown(void)
{
	if (iLCDType == LCD_SSD1351 || iLCDType == LCD_SSD1331)
		spilcdWriteCommand(0xae); // Display Off
	else
		spilcdWriteCommand(0x28); // Display OFF
	if (iLEDPin != -1)
		myPinWrite(iLEDPin, 0); // turn off the backlight
    spilcdFreeBackbuffer();
#ifdef _LINUX_
	AIOCloseSPI(iHandle);
	AIORemoveGPIO(iDCPin);
	AIORemoveGPIO(iResetPin);
	AIORemoveGPIO(iLEDPin);
#endif // _LINUX_
} /* spilcdShutdown() */

//
// Send a command byte to the LCD controller
// In SPI 8-bit mode, the D/C line must be set
// high during the write
//
static void spilcdWriteCommand(unsigned char c)
{
unsigned char buf[2];

	buf[0] = c;
	myspiWrite(buf, 1, MODE_COMMAND, 1);
} /* spilcdWriteCommand() */

//
// Write a single byte of data
//
static void spilcdWriteData8(unsigned char c)
{
unsigned char buf[2];

	buf[0] = c;
    myspiWrite(buf, 1, MODE_DATA, 1);

} /* spilcdWriteData8() */

//
// Write 16-bits of data
// The ILI9341 receives data in big-endian order
// (MSB first)
//
static void spilcdWriteData16(unsigned short us, int bRender)
{
unsigned char buf[2];

    buf[0] = (unsigned char)(us >> 8);
    buf[1] = (unsigned char)us;
    myspiWrite(buf, 2, MODE_DATA, bRender);

} /* spilcdWriteData16() */

//
// Position the "cursor" to the given
// row and column. The width and height of the memory
// 'window' must be specified as well. The controller
// allows more efficient writing of small blocks (e.g. tiles)
// by bounding the writes within a small area and automatically
// wrapping the address when reaching the end of the window
// on the curent row
//
void spilcdSetPosition(int x, int y, int w, int h, int bRender)
{
unsigned char ucBuf[8];

	if (iOrientation == LCD_ORIENTATION_ROTATED) // rotate 90 clockwise
	{
        int t;
		// rotate the coordinate system
		t = x;
		x = iWidth-y-h;
		y = t;
		// flip the width/height too
		t = w;
		w = h;
		h = t;
	}
    iWindowX = iCurrentX = x; iWindowY = iCurrentY = y;
    iWindowCX = w; iWindowCY = h;
    iOffset = (iScreenPitch * y) + (x * 2);

    if (!bRender) return; // nothing to do

    bSetPosition = 1; // flag to let myspiWrite know to ignore data writes
    y = (y + iScrollOffset) % iHeight; // scroll offset affects writing position

	if (iLCDType == LCD_SSD1351) // OLED has very different commands
	{
		spilcdWriteCommand(0x15); // set column
		ucBuf[0] = x;
		ucBuf[1] = x + w - 1;
		myspiWrite(ucBuf, 2, MODE_DATA, 1);
		spilcdWriteCommand(0x75); // set row
		ucBuf[0] = y;
		ucBuf[1] = y + h - 1;
		myspiWrite(ucBuf, 2, MODE_DATA, 1);
		spilcdWriteCommand(0x5c); // write RAM
		bSetPosition = 0;
		return;
	}
        else if (iLCDType == LCD_SSD1283A) // so does the SSD1283A
        {
		spilcdWriteCommand(0x44); // set col
		ucBuf[0] = x + w + 1;
                ucBuf[1] = x + 2;
                myspiWrite(ucBuf, 2, MODE_DATA, 1);
                spilcdWriteCommand(0x45); // set row
		ucBuf[0] = y + h + 1;
		ucBuf[1] = y + 2;
		myspiWrite(ucBuf, 2, MODE_DATA, 1);
		spilcdWriteCommand(0x21); // set col+row
		ucBuf[0] = y + 2;
		ucBuf[1] = x + 2;
		myspiWrite(ucBuf, 2, MODE_DATA, 1);
		spilcdWriteCommand(0x22); // write RAM
		return;
        }
    else if (iLCDType == LCD_SSD1331)
    {
        spilcdWriteCommand(0x15);
        ucBuf[0] = x;
        ucBuf[1] = x + w - 1;
        myspiWrite(ucBuf, 2, MODE_COMMAND, 1);

        spilcdWriteCommand(0x75);
        ucBuf[0] = y;
        ucBuf[1] = y + h - 1;
        myspiWrite(ucBuf, 2, MODE_COMMAND, 1);

        bSetPosition = 0;
        return;
    }
	spilcdWriteCommand(0x2a); // set column address
	if (iLCDType == LCD_ILI9341 || iLCDType == LCD_ILI9342 || iLCDType == LCD_ST7735R || iLCDType == LCD_ST7789 || iLCDType == LCD_ST7735S || iLCDType == LCD_ILI9486)
	{
		x += iMemoryX;
		ucBuf[0] = (unsigned char)(x >> 8);
		ucBuf[1] = (unsigned char)x;
		x = x + w - 1;
		if ((x-iMemoryX) > iWidth-1) x = iMemoryX + iWidth-1;
		ucBuf[2] = (unsigned char)(x >> 8);
		ucBuf[3] = (unsigned char)x; 
		myspiWrite(ucBuf, 4, MODE_DATA, 1);
	}
	else
	{
// combine coordinates into 1 write to save time
		ucBuf[0] = 0;
 		ucBuf[1] = (unsigned char)(x >> 8); // MSB first
		ucBuf[2] = 0;
		ucBuf[3] = (unsigned char)x;
		x = x + w -1;
		if (x > iWidth-1) x = iWidth-1;
		ucBuf[4] = 0;
		ucBuf[5] = (unsigned char)(x >> 8);
		ucBuf[6] = 0;
		ucBuf[7] = (unsigned char)x;
		myspiWrite(ucBuf, 8, MODE_DATA, 1);
	}
	spilcdWriteCommand(0x2b); // set row address
	if (iLCDType == LCD_ILI9341 || iLCDType == LCD_ILI9342 || iLCDType == LCD_ST7735R || iLCDType == LCD_ST7735S || iLCDType == LCD_ST7789 || iLCDType == LCD_ILI9486)
	{
		y += iMemoryY;
		ucBuf[0] = (unsigned char)(y >> 8);
		ucBuf[1] = (unsigned char)y;
		y = y + h;
		if ((y-iMemoryY) > iHeight-1) y = iMemoryY + iHeight;
		ucBuf[2] = (unsigned char)(y >> 8);
		ucBuf[3] = (unsigned char)y;
		myspiWrite(ucBuf, 4, MODE_DATA, 1);
	}
	else
	{
// combine coordinates into 1 write to save time
		ucBuf[0] = 0;
		ucBuf[1] = (unsigned char)(y >> 8); // MSB first
		ucBuf[2] = 0;
		ucBuf[3] = (unsigned char)y;
		y = y + h - 1;
		if (y > iHeight-1) y = iHeight-1;
		ucBuf[4] = 0;
		ucBuf[5] = (unsigned char)(y >> 8);
		ucBuf[6] = 0;
		ucBuf[7] = (unsigned char)y;
		myspiWrite(ucBuf, 8, MODE_DATA, 1);
	}
	spilcdWriteCommand(0x2c); // write memory begin
//	spilcdWriteCommand(0x3c); // write memory continue
    bSetPosition = 0;
} /* spilcdSetPosition() */

//
// Draw an individual RGB565 pixel
//
int spilcdSetPixel(int x, int y, unsigned short usColor, int bRender)
{
	spilcdSetPosition(x, y, 1, 1, bRender);
	spilcdWriteData16(usColor, bRender);
	return 0;
} /* spilcdSetPixel() */

#ifdef ESP32_DMA
// wait for previous transaction to complete
void spilcdWaitDMA(void)
{
spi_transaction_t *rtrans;
int bFirst = 1;
    
   if (!bFirst)
   {
     spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY);
//   spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY);
//       spi_device_get_trans_result(spi, &rtrans, portMAX_DELAY);
   }
    bFirst = 0;
}
// Queue a new transaction for the SPI DMA
void spilcdWriteDataDMA(int iLen)
{
esp_err_t ret;

    trans[0].tx_buffer = ucTXBuf;
    trans[0].length = iLen * 8; // Length in bits
    
    // Queue the transaction
//    ret = spi_device_polling_transmit(spi, &t);
    ret = spi_device_queue_trans(spi, &trans[0], portMAX_DELAY);
    assert (ret==ESP_OK);
//    iFirst = 0;
} /* spilcdWriteDataDMA() */
#endif

//
// Draw a string in a proportional font you supply
//
int spilcdWriteStringCustom(GFXfont *pFont, int x, int y, char *szMsg, uint16_t usFGColor, uint16_t usBGColor, int bBlank)
{
int i, j, k, iLen, dx, dy, cx, cy, c, iBitOff;
int tx, ty;
uint8_t *s, bits, uc;
GFXfont font;
GFXglyph glyph, *pGlyph;
#define TEMP_BUF_SIZE 64
#define TEMP_HIGHWATER (TEMP_BUF_SIZE-8)
uint16_t *d, u16Temp[TEMP_BUF_SIZE];

   if (pFont == NULL || x < 0)
      return -1;
   // in case of running on AVR, get copy of data from FLASH
   memcpy_P(&font, pFont, sizeof(font));
   pGlyph = &glyph;
   usFGColor = (usFGColor >> 8) | (usFGColor << 8); // swap h/l bytes
   usBGColor = (usBGColor >> 8) | (usBGColor << 8);

   i = 0;
   while (szMsg[i] && x < iCurrentWidth)
   {
      c = szMsg[i++];
      if (c < font.first || c > font.last) // undefined character
         continue; // skip it
      c -= font.first; // first char of font defined
      memcpy_P(&glyph, &font.glyph[c], sizeof(glyph));
      // set up the destination window (rectangle) on the display
      dx = x + pGlyph->xOffset; // offset from character UL to start drawing
      dy = y + pGlyph->yOffset;
      cx = pGlyph->width;
      cy = pGlyph->height;
      iBitOff = 0; // bitmap offset (in bits)
      if (dy + cy > iCurrentHeight)
         cy = iCurrentHeight - dy;
      else if (dy < 0) {
         cy += dy;
         iBitOff += (pGlyph->width * (-dy));
         dy = 0;
      }
      s = font.bitmap + pGlyph->bitmapOffset; // start of bitmap data
      // Bitmap drawing loop. Image is MSB first and each pixel is packed next
      // to the next (continuing on to the next character line)
      bits = uc = 0; // bits left in this font byte

      if (bBlank) { // erase the areas around the char to not leave old bits
         int miny, maxy;
         c = '0' - font.first;
         miny = y + (int8_t)pgm_read_byte(&font.glyph[c].yOffset);
         c = 'y' - font.first;
         maxy = y + (int8_t)pgm_read_byte(&font.glyph[c].yOffset) + pgm_read_byte(&font.glyph[c].height);
         spilcdSetPosition(x, miny, pGlyph->xAdvance, maxy-miny, 1);
         if (iOrientation == LCD_ORIENTATION_NATIVE) {
            // blank out area above character
            for (ty=miny; ty<y+pGlyph->yOffset; ty++) {
               for (tx=0; tx>pGlyph->xAdvance; tx++)
                  u16Temp[tx] = usBGColor;
               myspiWrite((uint8_t *)u16Temp, pGlyph->xAdvance*sizeof(uint16_t), MODE_DATA, 1);
            } // for ty
            // character area (with possible padding on L+R)
            for (ty=0; ty<pGlyph->height; ty++) {
               d = &u16Temp[0];
               for (tx=0; tx<pGlyph->xOffset; tx++) { // left padding
                  *d++ = usBGColor;
               }
            // character bitmap (center area)
               for (tx=0; tx<pGlyph->width; tx++) {
                  if (bits == 0) { // need more data
                     uc = pgm_read_byte(&s[iBitOff>>3]);
                     bits = 8;
                     iBitOff += bits;
                  }
                  *d++ = (uc & 0x80) ? usFGColor : usBGColor;
                  bits--;
                  uc <<= 1;
               } // for tx
               // right padding
               k = pGlyph->xAdvance - (int)(d - u16Temp); // remaining amount
               for (tx=0; tx<k; tx++)
               *d++ = usBGColor;
               myspiWrite((uint8_t *)u16Temp, pGlyph->xAdvance*sizeof(uint16_t), MODE_DATA, 1);
            } // for ty
            // padding below the current character
            ty = y + pGlyph->yOffset + pGlyph->height;
            for (; ty < maxy; ty++) {
               for (tx=0; tx<pGlyph->xAdvance; tx++)
                  u16Temp[tx] = usBGColor;
               myspiWrite((uint8_t *)u16Temp, pGlyph->xAdvance*sizeof(uint16_t), MODE_DATA, 1);
            } // for ty
         } else { // 90 degrees rotated
             for (tx=0; tx<pGlyph->xAdvance; tx++) { // sweep across the whole width of this character
                 d = &u16Temp[0];
                 if (tx <pGlyph->xOffset || tx >= pGlyph->xOffset + pGlyph->width) { // blank area to L or R of char
                     for (ty=miny; ty<maxy; ty++)
                         *d++ = usBGColor;
                 } else { // middle (drawn) area of character
                     k = y + pGlyph->yOffset + pGlyph->height;
                     // blank part below char
                     for (ty=k; ty<maxy; ty++)
                         *d++ = usBGColor;
                     // Character box
                     iBitOff = (tx-pGlyph->xOffset) + (pGlyph->width * (pGlyph->height-1)); // start at bottom
                     for (ty=0; ty<pGlyph->height; ty++) {
                        uc = pgm_read_byte(&s[iBitOff>>3]);
                        *d++ = (uc & (0x80 >> (iBitOff & 7))) ? usFGColor : usBGColor;
                        iBitOff -= pGlyph->width;
                     } // for ty
                     // blank part above char
                     for (ty=miny; ty<y+pGlyph->yOffset; ty++)
                         *d++ = usBGColor;
                 }
                 myspiWrite((uint8_t *)u16Temp, (maxy-miny)*sizeof(uint16_t), MODE_DATA, 1);
             } // for tx
         } // rotated, blanked
      } else { // just draw the current character box
         spilcdSetPosition(dx, dy, cx, cy, 1);
         if (iOrientation == LCD_ORIENTATION_NATIVE) {
            iLen = cx * cy; // total pixels to draw
            d = &u16Temp[0]; // point to start of output buffer
            for (j=0; j<iLen; j++) {
               if (uc == 0) { // need to read more font data
                  j += bits;
                  while (bits > 0) {
                     *d++ = usBGColor; // draw any remaining 0 bits
                     bits--;
                  }
                  uc = pgm_read_byte(&s[iBitOff>>3]); // get more font bitmap data
                  bits = 8 - (iBitOff & 7); // we might not be on a byte boundary
                  iBitOff += bits; // because of a clipped line
                  uc <<= (8-bits);
                  k = (int)(d-u16Temp); // number of words in output buffer
                  if (k >= TEMP_HIGHWATER) { // time to write it
                     myspiWrite((uint8_t *)u16Temp, k*sizeof(uint16_t), MODE_DATA, 1);
                     d = &u16Temp[0];
                  }
               } // if we ran out of bits
               *d++ = (uc & 0x80) ? usFGColor : usBGColor;
               bits--; // next bit
               uc <<= 1;
            } // for j
            k = (int)(d-u16Temp);
            if (k) // write any remaining data
               myspiWrite((uint8_t *)u16Temp, k*sizeof(uint16_t), MODE_DATA, 1);
         } else { // rotated 90 degrees
            for (tx=0; tx<pGlyph->width; tx++) {
               iBitOff = tx + (pGlyph->width * (pGlyph->height-1)); // start at bottom
               d = &u16Temp[0];
               for (ty=0; ty<pGlyph->height; ty++) {
                  uc = pgm_read_byte(&s[iBitOff>>3]);
                  uc <<= (iBitOff & 7);
                  *d++ = (uc & 0x80) ? usFGColor : usBGColor;
                  iBitOff -= pGlyph->width;
               } // for ty
               myspiWrite((uint8_t *)u16Temp, pGlyph->height*sizeof(uint16_t), MODE_DATA, 1);
            } // for tx
         } // rotated
      } // quicker drawing
      x += pGlyph->xAdvance; // width of this character
   } // while drawing characters
   return 0;

} /* spilcdWriteStringCustom() */
//
// Get the width of text in a custom font
//
void spilcdGetStringBox(GFXfont *pFont, char *szMsg, int *width, int *top, int *bottom)
{
int cx = 0;
int c, i = 0;
GFXfont font;
GFXglyph glyph, *pGlyph;
int miny, maxy;

   if (pFont == NULL)
      return;
   // in case of running on AVR, get copy of data from FLASH
   memcpy_P(&font, pFont, sizeof(font));
   pGlyph = &glyph;
   if (width == NULL || top == NULL || bottom == NULL || pFont == NULL || szMsg == NULL) return; // bad pointers
   miny = 100; maxy = 0;
   while (szMsg[i]) {
      c = szMsg[i++];
      if (c < font.first || c > font.last) // undefined character
         continue; // skip it
      c -= font.first; // first char of font defined
      memcpy_P(&glyph, &font.glyph[c], sizeof(glyph));
      cx += pGlyph->xAdvance;
      if (pGlyph->yOffset < miny) miny = pGlyph->yOffset;
      if (pGlyph->height+pGlyph->yOffset > maxy) maxy = pGlyph->height+pGlyph->yOffset;
   }
   *width = cx;
   *top = miny;
   *bottom = maxy;
} /* spilcdGetStringBox() */

#ifndef __AVR__
//
// Draw a string of small (8x8) text as quickly as possible
// by writing it to the LCD in a single SPI write
// The string must be 32 characters or shorter
//
int spilcdWriteStringFast(int x, int y, char *szMsg, unsigned short usFGColor, unsigned short usBGColor, int iFontSize)
{
int i, j, k, iMaxLen, iLen;
int iChars, iStride;
unsigned char *s;
unsigned short usFG = (usFGColor >> 8) | ((usFGColor & -1)<< 8);
unsigned short usBG = (usBGColor >> 8) | ((usBGColor & -1)<< 8);
unsigned short *usD;
int cx;
uint8_t *pFont;

    if (iFontSize != FONT_SMALL && iFontSize != FONT_NORMAL)
        return -1; // invalid size
    cx = (iFontSize == FONT_NORMAL) ? 8:6;
    pFont = (iFontSize == FONT_NORMAL) ? (uint8_t *)ucFont : (uint8_t *)ucSmallFont;
    iLen = strlen(szMsg);
	if (iLen <=0) return -1; // can't use this function
        iMaxLen = (iOrientation == LCD_ORIENTATION_NATIVE) ? iWidth : iHeight;

    if ((cx*iLen) + x > iMaxLen) iLen = (iMaxLen - x)/cx; // can't display it all
    if (iOrientation == LCD_ORIENTATION_ROTATED) // draw rotated
    {
        iChars = 0;
        for (i=0; i<iLen; i++)
        {
            s = &pFont[((unsigned char)szMsg[i]-32) * cx];
            usD = (unsigned short *)&ucTXBuf[iChars*cx*16];
            for (k=0; k<cx; k++) // for each scanline
            {
                uint8_t ucMask = 0x80;
                for (j=0; j<8; j++)
                {
                    if (s[k] & ucMask)
                        *usD++ = usFG;
                    else
                        *usD++ = usBG;
                    ucMask >>= 1;
                } // for j
            } // for k
            iChars++;
            if (iChars == 32) // need to write it
            {
#ifdef ESP32_DMA
                spilcdWaitDMA();
                spilcdSetPosition(x, y, cx*iChars, 8, 1);
                spilcdWriteDataDMA(iChars*cx*16);
#else
                spilcdSetPosition(x, y, cx*iChars, 8, 1);
                myspiWrite(ucTXBuf, iChars*cx*16, MODE_DATA, 1);
#endif
                x += iChars*cx;
                iChars = 0;
            }
        } // for i
        if (iChars)
        {
#ifdef ESP32_DMA
            spilcdWaitDMA();
            spilcdSetPosition(x, y, cx*iChars, 8, 1);
            spilcdWriteDataDMA(iChars*cx*16);
#else
            spilcdSetPosition(x, y, cx*iChars, 8, 1);
            myspiWrite(ucTXBuf, iChars*cx*16, MODE_DATA, 1);
#endif
        }
    } // landscape
    else // portrait orientation
    {
        if (iLen > 32) iLen = 32;
        iStride = iLen * cx*2;
        for (i=0; i<iLen; i++)
        {
            s = &pFont[((unsigned char)szMsg[i]-32) * cx];
            uint8_t ucMask = 1;
            for (k=0; k<8; k++) // for each scanline
            {
                usD = (unsigned short *)&ucTXBuf[(k*iStride) + (i * cx*2)];
                for (j=0; j<cx; j++)
                {
                    if (s[j] & ucMask)
                        *usD++ = usFG;
                    else
                        *usD++ = usBG;
                } // for j
                ucMask <<= 1;
            } // for k
        } // normal orientation
        // write the data in one shot
#ifdef ESP32_DMA
        spilcdWaitDMA();
        spilcdSetPosition(x, y, cx*iLen, 8, 1);
        spilcdWriteDataDMA(iLen*cx*16);
#else
        spilcdSetPosition(x, y, cx*iLen, 8, 1);
        myspiWrite(ucTXBuf, iLen*cx*16, MODE_DATA, 1);
#endif
    } // portrait orientation
	return 0;
} /* spilcdWriteStringFast() */
#endif // !__AVR__

//
// Draw a string of small (8x8) or large (16x32) characters
// At the given col+row
//
int spilcdWriteString(int x, int y, char *szMsg, int usFGColor, int usBGColor, int iFontSize, int bRender)
{
int i, j, k, iMaxLen, iLen;
#ifndef __AVR__
int l;
#endif
unsigned char *s;
unsigned short usFG = (usFGColor >> 8) | (usFGColor << 8);
unsigned short usBG = (usBGColor >> 8) | (usBGColor << 8);
uint16_t usPitch = iScreenPitch/2;


	iLen = strlen(szMsg);
	iMaxLen = (iOrientation == LCD_ORIENTATION_NATIVE) ? iWidth : iHeight;
    if (usBGColor == -1) bRender = 0; // transparent text doesn't get written to the display
#ifndef __AVR__
	if (iFontSize == FONT_LARGE) // draw 16x32 font
	{
		if (iLen*16 + x > iMaxLen) iLen = (iMaxLen - x) / 16;
		if (iLen < 0) return -1;
		for (i=0; i<iLen; i++)
		{
			uint16_t *usD, *usTemp = (uint16_t *)ucRXBuf;
            uint8_t ucMask;
			s = (uint8_t *)&ucBigFont[((unsigned char)szMsg[i]-32)*64];
			usD = &usTemp[0];
            if (usBGColor == -1) // transparent text is not rendered to the dispaly
                bRender = 0;
			if (iOrientation == LCD_ORIENTATION_ROTATED) // rotated
			{
				spilcdSetPosition(x+(i*16), y,16,32, bRender);
                for (j=0; j<16; j++) // horizontal
                {
                    if (usBGColor == -1) // transparent text
                    {
                        usD = (uint16_t *)&pBackBuffer[iOffset + (j*iScreenPitch)];
                        for (k=31; k>=0; k--) // vertical
                        {
                            ucMask = 1 << (k & 7);
                            if (s[j+((k>>3)<<4)] & ucMask)
                                *usD = usFG;
                            usD++;
                        } // for j
                    }
                    else
                    {
                        for (k=31; k>=0; k--) // vertical
                        {
                            ucMask = 1 << (k & 7);
                            if (s[j+((k>>3)<<4)] & ucMask)
                                *usD++ = usFG;
                            else
                                *usD++ = usBG;
                        } // for j
                    }
                } // for j
			}
			else // not rotated
			{ // portrait
				spilcdSetPosition(x+(i*16), y,16,32, bRender);
                for (l=0; l<4; l++) // 4 sets of 8 rows
                {
                    uint8_t ucMask = 1;
                    for (k=0; k<8; k++) // for each scanline
                    { // left half
                        if (usBGColor == -1) // transparent text
                        {
                            uint16_t *d = (uint16_t *)&pBackBuffer[iOffset + ((l*8+k)*iScreenPitch)];
                            for (j=0; j<16; j++)
                            {
                                if (s[j] & ucMask)
                                    *d = usFG;
                                d++;
                            } // for j
                        }
                        else
                        {
                            for (j=0; j<16; j++)
                            {
                                if (s[j] & ucMask)
                                    *usD++ = usFG;
                                else
                                    *usD++ = usBG;
                            } // for j
                        }
                        ucMask <<= 1;
                    } // for each scanline
                    s += 16;
				} // for each set of 8 scanlines
			} // portrait mode
            if (usBGColor != -1) // don't write anything if we're doing transparent text
                myspiWrite((unsigned char *)usTemp, 1024, MODE_DATA, bRender);
		} // for each character
	}
#endif // !__AVR__
    if (iFontSize == FONT_NORMAL || iFontSize == FONT_SMALL) // draw the 6x8 or 8x8 font
	{
		uint16_t *usD, *usTemp = (uint16_t *)ucRXBuf;
        int cx;
        uint8_t c, *pFont;

        cx = (iFontSize == FONT_NORMAL) ? 8:6;
        pFont = (iFontSize == FONT_NORMAL) ? (uint8_t *)ucFont : (uint8_t *)ucSmallFont;
		if ((cx*iLen) + x > iMaxLen) iLen = (iMaxLen - x)/cx; // can't display it all
		if (iLen < 0)return -1;

		for (i=0; i<iLen; i++)
		{
			s = &pFont[((unsigned char)szMsg[i]-32) * cx];
			usD = &usTemp[0];
			if (iOrientation == LCD_ORIENTATION_ROTATED) // draw rotated
			{
				spilcdSetPosition(x+(i*cx), y, cx, 8, bRender);
				for (k=0; k<cx; k++) // for each scanline
				{
                    uint8_t ucMask = 0x80;
					c = pgm_read_byte(&s[k]);
                    if (usBGColor == -1) // transparent text
                    {
                        usD = (uint16_t *)&pBackBuffer[iOffset + (k * iScreenPitch)];
                        for (j=0; j<8; j++)
                        {
                            if (c & ucMask)
                                *usD = usFG;
                            usD++;
                            ucMask >>= 1;
                        } // for j
                    }
                    else
                    {
                        for (j=0; j<8; j++)
                        {
                            if (c & ucMask)
                                *usD++ = usFG;
                            else
                                *usD++ = usBG;
                            ucMask >>= 1;
                        } // for j
                    }
				} // for k
			}
			else // portrait orientation
			{
				spilcdSetPosition(x+(i*cx), y, cx, 8, bRender);
                uint8_t ucMask = 1;
				for (k=0; k<8; k++) // for each scanline
				{
                    if (usBGColor == -1) // transparent text
                    {
                        usD = (uint16_t *)&pBackBuffer[iOffset + (k * iScreenPitch)];
                        for (j=0; j<cx; j++)
                        {
                            if (pgm_read_byte(&s[j]) & ucMask)
                                *usD = usFG;
                            usD++;
                        } // for j
                    }
                    else // regular text
                    {
                        for (j=0; j<cx; j++)
                        {
                            if (pgm_read_byte(&s[j]) & ucMask)
                                *usD++ = usFG;
                            else
                                *usD++ = usBG;
                        } // for j
                    }
                    ucMask <<= 1;
				} // for k
			} // normal orientation
		// write the data in one shot
            if (usBGColor != -1) // don't write anything if we're doing transparent text
                myspiWrite((unsigned char *)usTemp, cx*16, MODE_DATA, bRender);
		}	
	} // 6x8 and 8x8
    if (iFontSize == FONT_STRETCHED) // 8x8 stretched to 16x16
    {
        uint16_t *usD, *usTemp = (uint16_t *)ucRXBuf;
        uint8_t c;
        
        if ((16*iLen) + x > iMaxLen) iLen = (iMaxLen - x)/16; // can't display it all
        if (iLen < 0)return -1;
        
        for (i=0; i<iLen; i++)
        {
            s = (uint8_t *)&ucFont[((unsigned char)szMsg[i]-32) * 8];
            usD = &usTemp[0];
            if (iOrientation == LCD_ORIENTATION_ROTATED) // draw rotated
            {
                spilcdSetPosition(x+(i*16), y, 16, 16, bRender);
                for (k=0; k<8; k++) // for each scanline
                {
                    uint8_t ucMask = 0x80;
                    c = pgm_read_byte(&s[k]);
                    if (usBGColor == -1) // transparent text
                    {
                        usD = (uint16_t *)&pBackBuffer[iOffset + (k * 2 * iScreenPitch)];
                        for (j=0; j<8; j++)
                        {
                            if (c & ucMask) // write 2x2 pixels
                            {
                                usD[0] = usD[1] = usD[usPitch] = usD[usPitch+1] = usFG;
                            }
                            usD += 2;
                            ucMask >>= 1;
                        } // for j
                    }
                    else
                    {
                        for (j=0; j<8; j++)
                        {
                            if (c & ucMask) // write 2x2 pixels
                            {
                                usD[0] = usD[1] = usD[16] = usD[17] = usFG;
                            }
                            else
                                usD[0] = usD[1] = usD[16] = usD[17] = usBG;
                            usD += 2;
                            ucMask >>= 1;
                        } // for j
                    }
                    usD += 16; // skip next scanline since we already drew it
                } // for k
            }
            else // portrait orientation
            {
                spilcdSetPosition(x+(i*16), y, 16, 16, bRender);
                uint8_t ucMask = 1;
                for (k=0; k<8; k++) // for each scanline
                {
                    if (usBGColor == -1) // transparent text
                    {
                        usD = (uint16_t *)&pBackBuffer[iOffset + (k*2*iScreenPitch)];
                        for (j=0; j<8; j++)
                        {
                            if (pgm_read_byte(&s[j]) & ucMask)
                                usD[0] = usD[1] = usD[usPitch] = usD[usPitch+1] = usFG;
                            usD += 2;
                        } // for j
                    }
                    else // regular text drawing
                    {
                        for (j=0; j<8; j++)
                        {
                            if (pgm_read_byte(&s[j]) & ucMask)
                                usD[0] = usD[1] = usD[16] = usD[17] = usFG;
                            else
                                usD[0] = usD[1] = usD[16] = usD[17] = usBG;
                            usD += 2;
                        } // for j
                    }
                    usD += 16; // skip over scanline we already drew
                    ucMask <<= 1;
                } // for k
            } // normal orientation
            // write the data in one shot
            if (usBGColor != -1) // don't write anything if we're doing transparent text
                myspiWrite((unsigned char *)usTemp, 512, MODE_DATA, bRender);
        }

    } // FONT_STRETCHED
	return 0;
} /* spilcdWriteString() */
//
// For drawing ellipses, a circle is drawn and the x and y pixels are scaled by a 16-bit integer fraction
// This function draws a single pixel and scales its position based on the x/y fraction of the ellipse
//
void DrawScaledPixel(int32_t iCX, int32_t iCY, int32_t x, int32_t y, int32_t iXFrac, int32_t iYFrac, unsigned short usColor, int bRender)
{
    uint8_t ucBuf[2];
    if (iXFrac != 0x10000) x = (x * iXFrac) >> 16;
    if (iYFrac != 0x10000) y = (y * iYFrac) >> 16;
    x += iCX; y += iCY;
    if (x < 0 || x >= iCurrentWidth || y < 0 || y >= iCurrentHeight)
        return; // off the screen
    ucBuf[0] = (uint8_t)(usColor >> 8);
    ucBuf[1] = (uint8_t)usColor;
    spilcdSetPosition(x, y, 1, 1, bRender);
    myspiWrite(ucBuf, 2, MODE_DATA, bRender);
} /* DrawScaledPixel() */
void DrawScaledLine(int32_t iCX, int32_t iCY, int32_t x, int32_t y, int32_t iXFrac, int32_t iYFrac, uint16_t *pBuf, int bRender)
{
    int32_t iLen, x2;
    if (iXFrac != 0x10000) x = (x * iXFrac) >> 16;
    if (iYFrac != 0x10000) y = (y * iYFrac) >> 16;
    iLen = x * 2;
    x = iCX - x; y += iCY;
    x2 = x + iLen;
    if (y < 0 || y >= iCurrentHeight)
        return; // completely off the screen
    if (x < 0) x = 0;
    if (x2 >= iCurrentWidth) x2 = iCurrentWidth-1;
    iLen = x2 - x + 1; // new length
    spilcdSetPosition(x, y, iLen, 1, bRender);
#ifdef ESP32_DMA 
    myspiWrite((uint8_t*)pBuf, iLen*2, MODE_DATA, bRender);
#else
    // need to refresh the output data each time
    {
    int i;
    unsigned short us = pBuf[0];
      for (i=1; i<iLen; i++)
        pBuf[i] = us;
    }
    myspiWrite((uint8_t*)&pBuf[1], iLen*2, MODE_DATA, bRender);
#endif
} /* DrawScaledLine() */
//
// Draw the 8 pixels around the Bresenham circle
// (scaled to make an ellipse)
//
void BresenhamCircle(int32_t iCX, int32_t iCY, int32_t x, int32_t y, int32_t iXFrac, int32_t iYFrac, uint16_t iColor, uint16_t *pFill, int bRender)
{
    if (pFill != NULL) // draw a filled ellipse
    {
        static int prev_y = -1;
        // for a filled ellipse, draw 4 lines instead of 8 pixels
        DrawScaledLine(iCX, iCY, y, x, iXFrac, iYFrac, pFill, bRender);
        DrawScaledLine(iCX, iCY, y, -x, iXFrac, iYFrac, pFill, bRender);
        if (y != prev_y) {
            DrawScaledLine(iCX, iCY, x, y, iXFrac, iYFrac, pFill, bRender);
            DrawScaledLine(iCX, iCY, x, -y, iXFrac, iYFrac, pFill, bRender);
            prev_y = y;
        }
    }
    else // draw 8 pixels around the edges
    {
        DrawScaledPixel(iCX, iCY, x, y, iXFrac, iYFrac, iColor, bRender);
        DrawScaledPixel(iCX, iCY, -x, y, iXFrac, iYFrac, iColor, bRender);
        DrawScaledPixel(iCX, iCY, x, -y, iXFrac, iYFrac, iColor, bRender);
        DrawScaledPixel(iCX, iCY, -x, -y, iXFrac, iYFrac, iColor, bRender);
        DrawScaledPixel(iCX, iCY, y, x, iXFrac, iYFrac, iColor, bRender);
        DrawScaledPixel(iCX, iCY, -y, x, iXFrac, iYFrac, iColor, bRender);
        DrawScaledPixel(iCX, iCY, y, -x, iXFrac, iYFrac, iColor, bRender);
        DrawScaledPixel(iCX, iCY, -y, -x, iXFrac, iYFrac, iColor, bRender);
    }
} /* BresenhamCircle() */

void spilcdEllipse(int32_t iCenterX, int32_t iCenterY, int32_t iRadiusX, int32_t iRadiusY, unsigned short usColor, int bFilled, int bRender)
{
    int32_t iRadius, iXFrac, iYFrac;
    int32_t iDelta, x, y;
    uint16_t us, *pus, *usTemp = (uint16_t *)ucRXBuf; // up to 320 pixels wide
    
    if (iRadiusX > iRadiusY) // use X as the primary radius
    {
        iRadius = iRadiusX;
        iXFrac = 65536;
        iYFrac = (iRadiusY * 65536) / iRadiusX;
    }
    else
    {
        iRadius = iRadiusY;
        iXFrac = (iRadiusX * 65536) / iRadiusY;
        iYFrac = 65536;
    }
    // set up a buffer with the widest possible run of pixels to dump in 1 shot
    if (bFilled)
    {
        us = (usColor >> 8) | (usColor << 8); // swap byte order
        y = iRadius*2;
        if (y > 320) y = 320; // max size
#ifdef ESP32_DMA
        for (x=0; x<y; x++)
        {
            usTemp[x] = us;
        }
#else
	usTemp[0] = us; // otherwise just set the first one to the color
#endif
        pus = usTemp;
    }
    else
    {
        pus = NULL;
    }
    iDelta = 3 - (2 * iRadius);
    x = 0; y = iRadius;
    while (x < y)
    {
        BresenhamCircle(iCenterX, iCenterY, x, y, iXFrac, iYFrac, usColor, pus, bRender);
        x++;
        if (iDelta < 0)
        {
            iDelta += (4*x) + 6;
        }
        else
        {
            iDelta += 4 * (x-y) + 10;
            y--;
        }
    }

} /* spilcdEllipse() */

//
// Set the (software) orientation of the display
// The hardware is permanently oriented in 240x320 portrait mode
// The library can draw characters/tiles rotated 90
// degrees if set into landscape mode
//
int spilcdSetOrientation(int iOrient)
{
	if (iOrient != LCD_ORIENTATION_NATIVE && iOrient != LCD_ORIENTATION_ROTATED)
		return -1;
	iOrientation = iOrient; // nothing else needed to do
	iCurrentWidth = (iOrientation == LCD_ORIENTATION_NATIVE) ? iWidth : iHeight;
	iCurrentHeight = (iOrientation == LCD_ORIENTATION_NATIVE) ? iHeight : iWidth;
	return 0;
} /* spilcdSetOrientation() */

//
// Fill the frame buffer with a single color
//
int spilcdFill(unsigned short usData, int bRender)
{
int i, cx, tx, x, y;
int iOldOrient;
uint16_t *u16Temp = (uint16_t *)ucRXBuf;

    // make sure we're in landscape mode to use the correct coordinates
    iOldOrient = iOrientation;
    iOrientation = LCD_ORIENTATION_NATIVE;
    spilcdScrollReset();
    spilcdSetPosition(0,0,iWidth,iHeight, bRender);
    usData = (usData >> 8) | (usData << 8); // swap hi/lo byte for LCD
    // fit within our temp buffer
    cx = 1; tx = iWidth;
    if (iWidth > 160)
    {
       cx = 2; tx = iWidth/2;
    }
    for (y=0; y<iHeight; y++)
    {
       for (x=0; x<cx; x++)
       {
// have to do this every time because the buffer gets overrun (no half-duplex mode in Arduino SPI library)
            for (i=0; i<tx; i++)
                u16Temp[i] = usData;
            myspiWrite((uint8_t *)u16Temp, tx*2, MODE_DATA, bRender); // fill with data byte
       } // for x
    } // for y
    iOrientation = iOldOrient;
    return 0;
} /* spilcdFill() */
//
// Draw a 16x16 tile as 16x13 (with priority to non-black pixels)
// This is for drawing a 224x288 image onto a 320x240 display in landscape
//
int spilcdDrawRetroTile(int x, int y, unsigned char *pTile, int iPitch, int bRender)
{
    unsigned char *ucTemp = ucRXBuf;
    int i, j, iPitch16;
    uint16_t *s, *d, u16A, u16B;
    
    // scale y coordinate for shrinking
    y = (y * 13)/16;
    iPitch16 = iPitch/2;
    for (j=0; j<16; j++) // 16 destination columns
    {
        s = (uint16_t *)&pTile[j * 2];
        d = (uint16_t *)&ucTemp[j*26];
        for (i=0; i<16; i++) // 13 actual source rows
        {
            if (i == 0 || i == 5 || i == 10) // combined pixels
            {
                u16A = s[(15-i)*iPitch16];
                u16B = s[(14-i)*iPitch16];
                if (u16A == 0)
                    *d++ = __builtin_bswap16(u16B);
                else
                    *d++ = __builtin_bswap16(u16A);
                i++; // advance count since we merged 2 lines
            }
            else // just copy
            {
                *d++ = __builtin_bswap16(s[(15-i)*iPitch16]);
            }
        } // for i
    } // for j
    spilcdSetPosition(x, y, 16, 13, bRender);
    if (((x + iScrollOffset) % iHeight) > iHeight-16) // need to write in 2 parts since it won't wrap
    {
        int iStart = (iHeight - ((x+iScrollOffset) % iHeight));
        myspiWrite(ucTemp, iStart*26, MODE_DATA, bRender); // first N lines
        spilcdSetPosition(x+iStart, y, 16-iStart, 13, bRender);
        myspiWrite(&ucTemp[iStart*26], 416-(iStart*26), MODE_DATA, bRender);
    }
    else // can write in one shot
    {
        myspiWrite(ucTemp, 416, MODE_DATA, bRender);
    }
    return 0;
    
} /* spilcdDrawRetroTile() */

//
// Draw a 16x16 tile as 16x14 (with pixel averaging)
// This is for drawing 160x144 video games onto a 160x128 display
// It is assumed that the display is set to LANDSCAPE orientation
//
int spilcdDrawSmallTile(int x, int y, unsigned char *pTile, int iPitch, int bRender)
{
    unsigned char *ucTemp = ucRXBuf;
    int i, j, iPitch32;
    uint16_t *d;
    uint32_t *s;
    uint32_t u32A, u32B, u32a, u32b, u32C, u32D;
    uint32_t u32Magic = 0xf7def7de;
    uint32_t u32Mask = 0xffff;
    
    // scale y coordinate for shrinking
    y = (y * 7)/8;
    iPitch32 = iPitch/4;
    for (j=0; j<16; j+=2) // 16 source lines (2 at a time)
    {
        s = (uint32_t *)&pTile[j * 2];
        d = (uint16_t *)&ucTemp[j*28];
        for (i=0; i<16; i+=2) // 16 source columns (2 at a time)
        {
            u32A = s[(15-i)*iPitch32]; // read A+C
            u32B = s[(14-i)*iPitch32]; // read B+D
            u32C = u32A >> 16;
            u32D = u32B >> 16;
            u32A &= u32Mask;
            u32B &= u32Mask;
            if (i == 0 || i == 8) // pixel average a pair
            {
                u32a = (u32A & u32Magic) >> 1;
                u32a += ((u32B & u32Magic) >> 1);
                u32b = (u32C & u32Magic) >> 1;
                u32b += ((u32D & u32Magic) >> 1);
                d[0] = __builtin_bswap16(u32a);
                d[14] = __builtin_bswap16(u32b);
                d++;
            }
            else
            {
                d[0] = __builtin_bswap16(u32A);
                d[1] = __builtin_bswap16(u32B);
                d[14] = __builtin_bswap16(u32C);
                d[15] = __builtin_bswap16(u32D);
                d += 2;
            }
        } // for i
    } // for j
    spilcdSetPosition(x, y, 16, 14, bRender);
    if (((x + iScrollOffset) % iHeight) > iHeight-16) // need to write in 2 parts since it won't wrap
    {
        int iStart = (iHeight - ((x+iScrollOffset) % iHeight));
        myspiWrite(ucTemp, iStart*28, MODE_DATA, bRender); // first N lines
        spilcdSetPosition(x+iStart, y, 16-iStart, 14, bRender);
        myspiWrite(&ucTemp[iStart*28], 448-(iStart*28), MODE_DATA, bRender);
    }
    else // can write in one shot
    {
        myspiWrite(ucTemp, 448, MODE_DATA, bRender);
    }
    return 0;
} /* spilcdDrawSmallTile() */
//
// Draw a 16x16 RGB565 tile scaled to 32x24
// The main purpose of this function is for GameBoy emulation
// Since the original display is 160x144, this function allows it to be
// stretched 100x50% larger (320x216). Not a perfect fit for 320x240, but better
// Each group of 2x2 pixels becomes a group of 4x3 pixels by averaging the pixels
//
// +-+-+ becomes +-+-+-+-+
// |A|B|         |A|A|B|B|
// +-+-+         +-+-+-+-+
// |C|D|         |a|a|b|b| a = A avg. B, b = B avg. D
// +-+-+         +-+-+-+-+
//               |C|C|D|D|
//               +-+-+-+-+
//
// The x/y coordinates will be scaled 2x in the X direction and 1.5x in the Y
// It is assumed that the display is set to ROTATED orientation
//
int spilcdDrawScaledTile(int x, int y, int cx, int cy, unsigned char *pTile, int iPitch, int bRender)
{
    int i, j, iPitch32;
    uint16_t *d;
    uint32_t *s;
    uint32_t u32A, u32B, u32a, u32b, u32C, u32D;
    uint32_t u32Magic = 0xf7def7de;
    uint32_t u32Mask = 0xffff;
    
    // scale coordinates for stretching
    x = x * 2;
    y = (y * 3)/2;
    iPitch32 = iPitch/4;
    for (j=0; j<cx; j+=2) // source lines (2 at a time)
    {
        s = (uint32_t *)&pTile[j * 2];
        d = (uint16_t *)&ucRXBuf[j*cy*6];
        for (i=0; i<cy; i+=2) // source columns (2 at a time)
        {
            u32A = s[(cy-1-i)*iPitch32];
            u32B = s[(cy-2-i)*iPitch32];
            u32C = u32A >> 16;
            u32D = u32B >> 16;
            u32A &= u32Mask;
            u32B &= u32Mask;
            u32a = (u32A & u32Magic) >> 1;
            u32a += ((u32B & u32Magic) >> 1);
            u32b = (u32C & u32Magic) >> 1;
            u32b += ((u32D & u32Magic) >> 1);
            d[0] = d[(cy*3)/2] = __builtin_bswap16(u32A); // swap byte order
            d[1] = d[((cy*3)/2)+1] = __builtin_bswap16(u32a);
            d[2] = d[((cy*3)/2)+2] = __builtin_bswap16(u32B);
            d[cy*3] = d[(cy*9)/2] = __builtin_bswap16(u32C);
            d[(cy*3)+1] = d[((cy*9)/2)+1] = __builtin_bswap16(u32b);
            d[(cy*3)+2] = d[((cy*9)/2)+2] = __builtin_bswap16(u32D);
            d += 3;
        } // for i
    } // for j
    spilcdSetPosition(x, y, cx*2, (cy*3)/2, bRender);
    if (((x + iScrollOffset) % iHeight) > iHeight-(cx*2)) // need to write in 2 parts since it won't wrap
    {
        int iStart = (iHeight - ((x+iScrollOffset) % iHeight));
        myspiWrite(ucRXBuf, iStart*cx*3, MODE_DATA, bRender); // first N lines
        spilcdSetPosition(x+iStart, y, (cx*2)-iStart, (cy*3)/2, bRender);
        myspiWrite(&ucRXBuf[iStart*cx*3], (cx*cy*6)-(iStart*cx*3), MODE_DATA, bRender);
    }
    else // can write in one shot
    {
        myspiWrite(ucRXBuf, (cx*cy*6), MODE_DATA, bRender);
    }
    return 0;
} /* spilcdDrawScaledTile() */
//
// Draw a 24x24 RGB565 tile scaled to 40x40
// The main purpose of this function is for GameBoy emulation
// Since the original display is 160x144, this function allows it to be
// stretched 166% larger (266x240). Not a perfect fit for 320x240, but better
// Each group of 3x3 pixels becomes a group of 5x5 pixels by averaging the pixels
//
// +-+-+-+ becomes +----+----+----+----+----+
// |A|B|C|         |A   |ab  |B   |bc  |C   |
// +-+-+-+         +----+----+----+----+----+
// |D|E|F|         |ad  |abde|be  |becf|cf  |
// +-+-+-+         +----+----+----+----+----+
// |G|H|I|         |D   |de  |E   |ef  |F   |
// +-+-+-+         +----+----+----+----+----+
//                 |dg  |dgeh|eh  |ehfi|fi  |
//                 +----+----+----+----+----+
//                 |G   |gh  |H   |hi  |I   |
//                 +----+----+----+----+----+
//
// The x/y coordinates will be scaled as well
//
int spilcdDraw53Tile(int x, int y, int cx, int cy, unsigned char *pTile, int iPitch, int bRender)
{
    int i, j, iPitch16;
    uint16_t *s, *d;
    uint16_t u32A, u32B, u32C, u32D, u32E, u32F;
    uint16_t t1, t2, u32ab, u32bc, u32de, u32ef, u32ad, u32be, u32cf;
    uint16_t u32Magic = 0xf7de;
    int bFlipped;
    
    bFlipped = (iWidth < 320); // rotated display
    
    // scale coordinates for stretching
    x = (x * 5)/3;
    y = (y * 5)/3;
    iPitch16 = iPitch/2;
    if (cx < 24 || cy < 24)
        memset(ucTXBuf, 0, 40*40*2);
    for (j=0; j<cy/3; j++) // 8 blocks of 3 lines
    {
        s = (uint16_t *)&pTile[j*3*iPitch];
        if (bFlipped)
            d = (uint16_t *)&ucTXBuf[(35-(j*5))*2];
        else
            d = (uint16_t *)&ucTXBuf[j*40*5*2];
        for (i=0; i<cx-2; i+=3) // source columns (3 at a time)
        {
            u32A = s[i];
            u32B = s[i+1];
            u32C = s[i+2];
            u32D = s[i+iPitch16];
            u32E = s[i+iPitch16+1];
            u32F = s[i+iPitch16 + 2];
            u32bc = u32ab = (u32B & u32Magic) >> 1;
            u32ab += ((u32A & u32Magic) >> 1);
            u32bc += (u32C & u32Magic) >> 1;
            u32de = u32ef = ((u32E & u32Magic) >> 1);
            u32de += ((u32D & u32Magic) >> 1);
            u32ef += ((u32F & u32Magic) >> 1);
            u32ad = ((u32A & u32Magic) >> 1) + ((u32D & u32Magic) >> 1);
            u32be = ((u32B & u32Magic) >> 1) + ((u32E & u32Magic) >> 1);
            u32cf = ((u32C & u32Magic) >> 1) + ((u32F & u32Magic) >> 1);
            // first row
            if (bFlipped)
            {
                d[4] = __builtin_bswap16(u32A); // swap byte order
                d[44] = __builtin_bswap16(u32ab);
                d[84] = __builtin_bswap16(u32B);
                d[124] = __builtin_bswap16(u32bc);
                d[164] = __builtin_bswap16(u32C);
            }
            else
            {
                d[0] = __builtin_bswap16(u32A); // swap byte order
                d[1] = __builtin_bswap16(u32ab);
                d[2] = __builtin_bswap16(u32B);
                d[3] = __builtin_bswap16(u32bc);
                d[4] = __builtin_bswap16(u32C);
            }
            // second row
            t1 = ((u32ab & u32Magic) >> 1) + ((u32de & u32Magic) >> 1);
            t2 = ((u32be & u32Magic) >> 1) + ((u32cf & u32Magic) >> 1);
            if (bFlipped)
            {
                d[3] = __builtin_bswap16(u32ad);
                d[43] = __builtin_bswap16(t1);
                d[83] = __builtin_bswap16(u32be);
                d[123] = __builtin_bswap16(t2);
                d[163] = __builtin_bswap16(u32cf);
            }
            else
            {
                d[40] = __builtin_bswap16(u32ad);
                d[41] = __builtin_bswap16(t1);
                d[42] = __builtin_bswap16(u32be);
                d[43] = __builtin_bswap16(t2);
                d[44] = __builtin_bswap16(u32cf);
            }
            // third row
            if (bFlipped)
            {
                d[2] = __builtin_bswap16(u32D);
                d[42] = __builtin_bswap16(u32de);
                d[82] = __builtin_bswap16(u32E);
                d[122] = __builtin_bswap16(u32ef);
                d[162] = __builtin_bswap16(u32F);
            }
            else
            {
                d[80] = __builtin_bswap16(u32D);
                d[81] = __builtin_bswap16(u32de);
                d[82] = __builtin_bswap16(u32E);
                d[83] = __builtin_bswap16(u32ef);
                d[84] = __builtin_bswap16(u32F);
            }
            // fourth row
            u32A = s[i+iPitch16*2];
            u32B = s[i+iPitch16*2 + 1];
            u32C = s[i+iPitch16*2 + 2];
            u32bc = u32ab = (u32B & u32Magic) >> 1;
            u32ab += ((u32A & u32Magic) >> 1);
            u32bc += (u32C & u32Magic) >> 1;
            u32ad = ((u32A & u32Magic) >> 1) + ((u32D & u32Magic) >> 1);
            u32be = ((u32B & u32Magic) >> 1) + ((u32E & u32Magic) >> 1);
            u32cf = ((u32C & u32Magic) >> 1) + ((u32F & u32Magic) >> 1);
            t1 = ((u32ab & u32Magic) >> 1) + ((u32de & u32Magic) >> 1);
            t2 = ((u32be & u32Magic) >> 1) + ((u32cf & u32Magic) >> 1);
            if (bFlipped)
            {
                d[1] = __builtin_bswap16(u32ad);
                d[41] = __builtin_bswap16(t1);
                d[81] = __builtin_bswap16(u32be);
                d[121] = __builtin_bswap16(t2);
                d[161] = __builtin_bswap16(u32cf);
            }
            else
            {
                d[120] = __builtin_bswap16(u32ad);
                d[121] = __builtin_bswap16(t1);
                d[122] = __builtin_bswap16(u32be);
                d[123] = __builtin_bswap16(t2);
                d[124] = __builtin_bswap16(u32cf);
            }
            // fifth row
            if (bFlipped)
            {
                d[0] = __builtin_bswap16(u32A);
                d[40] = __builtin_bswap16(u32ab);
                d[80] = __builtin_bswap16(u32B);
                d[120] = __builtin_bswap16(u32bc);
                d[160] = __builtin_bswap16(u32C);
                d += 200;
            }
            else
            {
                d[160] = __builtin_bswap16(u32A);
                d[161] = __builtin_bswap16(u32ab);
                d[162] = __builtin_bswap16(u32B);
                d[163] = __builtin_bswap16(u32bc);
                d[164] = __builtin_bswap16(u32C);
                d += 5;
            }
        } // for i
    } // for j
#ifdef ESP32_DMA
    spilcdWaitDMA();
    spilcdSetPosition(x, y, 40, 40, bRender);
    spilcdWriteDataDMA(40*40*2);
#else
    spilcdSetPosition(x, y, 40, 40, bRender);
    myspiWrite(ucTXBuf, 40*40*2, MODE_DATA, bRender);
#endif
    return 0;
} /* spilcdDraw53Tile() */
//
// Draw a 16x16 RGB656 tile with select rows/columns removed
// the mask contains 1 bit for every column/row that should be drawn
// For example, to skip the first 2 columns, the mask value would be 0xfffc
//
int spilcdDrawMaskedTile(int x, int y, unsigned char *pTile, int iPitch, int iColMask, int iRowMask, int bRender)
{
    unsigned char *ucTemp = ucRXBuf; // fix the byte order first to write it more quickly
    int i, j;
    unsigned char *s, *d;
    int iNumCols, iNumRows, iTotalSize;
    
    iNumCols = __builtin_popcount(iColMask);
    iNumRows = __builtin_popcount(iRowMask);
    iTotalSize = iNumCols * iNumRows * 2;
    
    if (iOrientation == LCD_ORIENTATION_ROTATED) // need to rotate the data
    {
        // First convert to big-endian order
        d = ucTemp;
        for (j=0; j<16; j++)
        {
            if ((iColMask & (1<<j)) == 0) continue; // skip row
            s = &pTile[j*2];
            for (i=0; i<16; i++)
            {
                if ((iRowMask & (1<<i)) == 0) continue; // skip column
                d[1] = s[(15-i)*iPitch];
                d[0] = s[((15-i)*iPitch)+1]; // swap byte order (MSB first)
                d += 2;
            } // for i;
        } // for j
        spilcdSetPosition(x, y, iNumCols, iNumRows, bRender);
        if (((x + iScrollOffset) % iHeight) > iHeight-iNumRows) // need to write in 2 parts since it won't wrap
        {
            int iStart = (iHeight - ((x+iScrollOffset) % iHeight));
            myspiWrite(ucTemp, iStart*iNumRows*2, MODE_DATA, bRender); // first N lines
            spilcdSetPosition(x+iStart, y, iNumRows-iStart, iNumCols, bRender);
            myspiWrite(&ucTemp[iStart*iNumRows*2], iTotalSize-(iStart*iNumRows*2), MODE_DATA, bRender);
        }
        else // can write in one shot
        {
            myspiWrite(ucTemp, iTotalSize, MODE_DATA, bRender);
        }
    }
    else // native orientation
    {
        uint16_t *s16 = (uint16_t *)s;
        uint16_t u16, *d16 = (uint16_t *)d;
        int iMask;
        
        // First convert to big-endian order
        d16 = (uint16_t *)ucTemp;
        for (j=0; j<16; j++)
        {
            if ((iRowMask & (1<<j)) == 0) continue; // skip row
            s16 = (uint16_t *)&pTile[j*iPitch];
            iMask = iColMask;
            for (i=0; i<16; i++)
            {
                u16 = *s16++;
                if (iMask & 1)
                {
                    *d16++ = __builtin_bswap16(u16);
                }
                iMask >>= 1;
            } // for i;
        } // for j
        spilcdSetPosition(x, y, iNumCols, iNumRows, bRender);
        if (((y + iScrollOffset) % iHeight) > iHeight-iNumRows) // need to write in 2 parts since it won't wrap
        {
            int iStart = (iHeight - ((y+iScrollOffset) % iHeight));
            myspiWrite(ucTemp, iStart*iNumCols*2, MODE_DATA, bRender); // first N lines
            spilcdSetPosition(x, y+iStart, iNumCols, iNumRows-iStart, bRender);
            myspiWrite(&ucTemp[iStart*iNumCols*2], iTotalSize-(iStart*iNumCols*2), MODE_DATA, bRender);
        }
        else // can write in one shot
        {
            myspiWrite(ucTemp, iTotalSize, MODE_DATA, bRender);
        }
    } // portrait orientation
    return 0;
} /* spilcdDrawMaskedTile() */

//
// Draw a NxN RGB565 tile
// This reverses the pixel byte order and sets a memory "window"
// of pixels so that the write can occur in one shot
//
int spilcdDrawTile(int x, int y, int iTileWidth, int iTileHeight, unsigned char *pTile, int iPitch, int bRender)
{
    int i, j;
    uint32_t ul32;
    unsigned char *s, *d;
    
    if (iTileWidth*iTileHeight > 2048)
        return -1; // tile must fit in 4k SPI block size
    
    if (iOrientation == LCD_ORIENTATION_ROTATED) // need to rotate the data
    {
        // First convert to big-endian order
        d = ucTXBuf;
        for (j=0; j<iTileWidth; j++)
        {
            s = &pTile[j*2];
            s += (iTileHeight-2)*iPitch;
            for (i=0; i<iTileHeight; i+=2)
            {
                // combine the 2 pixels into a single write for better memory performance
                ul32 = __builtin_bswap16(*(uint16_t *)&s[iPitch]);
                ul32 |= ((uint32_t)__builtin_bswap16(*(uint16_t *)s) << 16); // swap byte order (MSB first)
                *(uint32_t *)d = ul32;
                d += 4;
                s -= iPitch*2;
            } // for i;
        } // for j
        spilcdSetPosition(x, y, iTileWidth, iTileHeight, bRender);
        if (((x + iScrollOffset) % iHeight) > iHeight-iTileWidth) // need to write in 2 parts since it won't wrap
        {
            int iStart = (iHeight - ((x+iScrollOffset) % iHeight));
            myspiWrite(ucTXBuf, iStart*iTileHeight*2, MODE_DATA, bRender); // first N lines
            spilcdSetPosition(x+iStart, y, iTileWidth-iStart, iTileWidth, bRender);
            myspiWrite(&ucTXBuf[iStart*iTileHeight*2], (iTileWidth*iTileHeight*2)-(iStart*iTileHeight*2), MODE_DATA, bRender);
        }
        else // can write in one shot
        {
            myspiWrite(ucTXBuf, iTileWidth*iTileHeight*2, MODE_DATA, bRender);
        }
    }
    else // native orientation
    {
        uint16_t *s16, *d16;
        // First convert to big-endian order
        d16 = (uint16_t *)ucTXBuf;
        for (j=0; j<iTileHeight; j++)
        {
            s16 = (uint16_t*)&pTile[j*iPitch];
            for (i=0; i<iTileWidth; i++)
            {
                *d16++ = __builtin_bswap16(*s16++);
            } // for i;
        } // for j
        spilcdSetPosition(x, y, iTileWidth, iTileHeight, bRender);
        if (((y + iScrollOffset) % iHeight) > iHeight-iTileHeight) // need to write in 2 parts since it won't wrap
        {
            int iStart = (iHeight - ((y+iScrollOffset) % iHeight));
            myspiWrite(ucTXBuf, iStart*iTileWidth*2, MODE_DATA, bRender); // first N lines
            spilcdSetPosition(x, y+iStart, iTileWidth, iTileHeight-iStart, bRender);
            myspiWrite(&ucTXBuf[iStart*iTileWidth*2], (iTileWidth*iTileHeight*2)-(iStart*iTileWidth*2), MODE_DATA, bRender);
        }
        else // can write in one shot
        {
            myspiWrite(ucTXBuf, iTileWidth*iTileHeight*2, MODE_DATA, bRender);
        }
    } // portrait orientation
    return 0;
} /* spilcdDrawTile() */
//
// Draw a NxN RGB565 tile
// This reverses the pixel byte order and sets a memory "window"
// of pixels so that the write can occur in one shot
// Scales the tile by 150% (for GameBoy/GameGear)
//
int spilcdDrawTile150(int x, int y, int iTileWidth, int iTileHeight, unsigned char *pTile, int iPitch, int bRender)
{
    int i, j, iPitch32, iLocalPitch;
    uint32_t ul32A, ul32B, ul32Avg, ul32Avg2;
    uint16_t u16Avg, u16Avg2;
    uint32_t u32Magic = 0xf7def7de;
    uint16_t u16Magic = 0xf7de;
    uint16_t *d16;
    uint32_t *s32;
    
    if (iTileWidth*iTileHeight > 1365)
        return -1; // tile must fit in 4k SPI block size
    
    iPitch32 = iPitch / 4;
    if (iOrientation == LCD_ORIENTATION_ROTATED) // need to rotate the data
    {
    iLocalPitch = (iTileHeight * 3)/2; // offset to next output line
    for (j=0; j<iTileHeight; j+=2)
    {
        d16 = (uint16_t *)&ucRXBuf[(j*3)]; //+(iLocalPitch * 2 * (iDestWidth-1))];
        s32 = (uint32_t*)&pTile[(iTileHeight-2-j)*iPitch];
        for (i=0; i<iTileWidth; i+=2) // turn 2x2 pixels into 3x3
        {
            ul32A = s32[0];
            ul32B = s32[iPitch32]; // get 2x2 pixels
            // top row
            ul32Avg = ((ul32A & u32Magic) >> 1);
            ul32Avg2 = ((ul32B & u32Magic) >> 1);
            u16Avg = (uint16_t)(ul32Avg + (ul32Avg >> 16)); // average the 2 pixels
            d16[2] = __builtin_bswap16((uint16_t)ul32A); // first pixel
            d16[2+iLocalPitch] = __builtin_bswap16(u16Avg); // middle (new) pixel
            d16[2+(iLocalPitch*2)] = __builtin_bswap16((uint16_t)(ul32A >> 16)); // 3rd pixel
            u16Avg2 = (uint16_t)(ul32Avg2 + (ul32Avg2 >> 16)); // bottom line averaged pixel
            d16[1] = __builtin_bswap16((uint16_t)(ul32Avg + ul32Avg2)); // vertical average
            d16[1+(iLocalPitch*2)] = __builtin_bswap16((uint16_t)((ul32Avg + ul32Avg2)>>16)); // vertical average
            d16[0] = __builtin_bswap16((uint16_t)ul32B); // last line 1st
            d16[0+iLocalPitch] = __builtin_bswap16(u16Avg2); // middle pixel
            d16[0+(iLocalPitch*2)] = __builtin_bswap16((uint16_t)(ul32B >> 16)); // 3rd pixel
            u16Avg = (u16Avg & u16Magic) >> 1;
            u16Avg2 = (u16Avg2 & u16Magic) >> 1;
            d16[1+iLocalPitch] = __builtin_bswap16(u16Avg + u16Avg2); // middle pixel
            d16 += (3*iLocalPitch);
            s32 += 1;
        } // for i;
    } // for j
    } // rotated 90
    else // normal orientation
    {
    iLocalPitch = (iTileWidth * 3)/2; // offset to next output line
    d16 = (uint16_t *)ucRXBuf;
    for (j=0; j<iTileHeight; j+=2)
    {
        s32 = (uint32_t*)&pTile[j*iPitch];
        for (i=0; i<iTileWidth; i+=2) // turn 2x2 pixels into 3x3
        {
            ul32A = s32[0];
            ul32B = s32[iPitch32]; // get 2x2 pixels
            // top row
            ul32Avg = ((ul32A & u32Magic) >> 1);
            ul32Avg2 = ((ul32B & u32Magic) >> 1);
            u16Avg = (uint16_t)(ul32Avg + (ul32Avg >> 16)); // average the 2 pixels
            d16[0] = __builtin_bswap16((uint16_t)ul32A); // first pixel
            d16[1] = __builtin_bswap16(u16Avg); // middle (new) pixel
            d16[2] = __builtin_bswap16((uint16_t)(ul32A >> 16)); // 3rd pixel
            u16Avg2 = (uint16_t)(ul32Avg2 + (ul32Avg2 >> 16)); // bottom line averaged pixel
            d16[iLocalPitch] = __builtin_bswap16((uint16_t)(ul32Avg + ul32Avg2)); // vertical average
            d16[iLocalPitch+2] = __builtin_bswap16((uint16_t)((ul32Avg + ul32Avg2)>>16)); // vertical average
            d16[iLocalPitch*2] = __builtin_bswap16((uint16_t)ul32B); // last line 1st
            d16[iLocalPitch*2+1] = __builtin_bswap16(u16Avg2); // middle pixel
            d16[iLocalPitch*2+2] = __builtin_bswap16((uint16_t)(ul32B >> 16)); // 3rd pixel
            u16Avg = (u16Avg & u16Magic) >> 1;
            u16Avg2 = (u16Avg2 & u16Magic) >> 1;
            d16[iLocalPitch+1] = __builtin_bswap16(u16Avg + u16Avg2); // middle pixel
            d16 += 3;
            s32 += 1;
        } // for i;
        d16 += iLocalPitch*2; // skip lines we already output
    } // for j
    } // normal orientation
    spilcdSetPosition((x*3)/2, (y*3)/2, (iTileWidth*3)/2, (iTileHeight*3)/2, bRender);
    myspiWrite(ucRXBuf, (iTileWidth*iTileHeight*9)/2, MODE_DATA, bRender);
    return 0;
} /* spilcdDrawTile150() */

//
// Draw a line between 2 points using Bresenham's algorithm
// An optimized version of the algorithm where each continuous run of pixels is written in a
// single shot to reduce the total number of SPI transactions. Perfectly vertical or horizontal
// lines are the most extreme version of this condition and will write the data in a single
// operation.
//
void spilcdDrawLine(int x1, int y1, int x2, int y2, unsigned short usColor, int bRender)
{
    int temp;
    int dx = x2 - x1;
    int dy = y2 - y1;
    int i, error;
    int xinc, yinc;
    int iLen, x, y;
    uint16_t *usTemp = (uint16_t *)ucRXBuf, us;

    if (x1 < 0 || x2 < 0 || y1 < 0 || y2 < 0 || x1 >= iCurrentWidth || x2 >= iCurrentWidth || y1 >= iCurrentHeight || y2 >= iCurrentHeight)
        return;
    us = (usColor >> 8) | (usColor << 8); // byte swap for LCD byte order

    if(abs(dx) > abs(dy)) {
        // X major case
        if(x2 < x1) {
            dx = -dx;
            temp = x1;
            x1 = x2;
            x2 = temp;
            temp = y1;
            y1 = y2;
            y2 = temp;
        }
#ifdef ESP32_DMA
        for (x=0; x<dx+1; x++) // prepare color data for max length line
            usTemp[x] = us;
#endif
//        spilcdSetPosition(x1, y1, dx+1, 1); // set the starting position in both X and Y
        y = y1;
        dy = (y2 - y1);
        error = dx >> 1;
        yinc = 1;
        if (dy < 0)
        {
            dy = -dy;
            yinc = -1;
        }
        for(x = x1; x1 <= x2; x1++) {
            error -= dy;
            if (error < 0) // y needs to change, write existing pixels
            {
                error += dx;
		iLen = (x1-x+1);
                spilcdSetPosition(x, y, iLen, 1, bRender);
#ifndef ESP32_DMA
	        for (i=0; i<iLen; i++) // prepare color data for max length line
                   usTemp[i] = us;
#endif
                myspiWrite((uint8_t*)usTemp, iLen*2, MODE_DATA, bRender); // write the row we changed
                y += yinc;
//                spilcdSetPosY(y, 1); // update the y position only
                x = x1+1; // we've already written the pixel at x1
            }
        } // for x1
        if (x != x1) // some data needs to be written
        {
	    iLen = (x1-x+1);
#ifndef ESP32_DMA
            for (i=0; i<iLen; i++) // prepare color data for max length line
               usTemp[i] = us;
#endif
            spilcdSetPosition(x, y, iLen, 1, bRender);
            myspiWrite((uint8_t*)usTemp, iLen*2, MODE_DATA, bRender); // write the row we changed
        }
    }
    else {
        // Y major case
        if(y1 > y2) {
            dy = -dy;
            temp = x1;
            x1 = x2;
            x2 = temp;
            temp = y1;
            y1 = y2;
            y2 = temp;
        }
#ifdef ESP32_DMA
        for (x=0; x<dy+1; x++) // prepare color data for max length line
            usTemp[x] = us;
#endif
//        spilcdSetPosition(x1, y1, 1, dy+1); // set the starting position in both X and Y
        dx = (x2 - x1);
        error = dy >> 1;
        xinc = 1;
        if (dx < 0)
        {
            dx = -dx;
            xinc = -1;
        }
        x = x1;
        for(y = y1; y1 <= y2; y1++) {
            error -= dx;
            if (error < 0) { // x needs to change, write any pixels we traversed
                error += dy;
                iLen = y1-y+1;
#ifndef ESP32_DMA
      		for (i=0; i<iLen; i++) // prepare color data for max length line
       		    usTemp[i] = us;
#endif
                spilcdSetPosition(x, y, 1, iLen, bRender);
                myspiWrite((uint8_t*)usTemp, iLen*2, MODE_DATA, bRender); // write the row we changed
                x += xinc;
//                spilcdSetPosX(x, 1); // update the x position only
                y = y1+1; // we've already written the pixel at y1
            }
        } // for y
        if (y != y1) // write the last byte we modified if it changed
        {
	    iLen = y1-y+1;
#ifndef ESP32_DMA
            for (i=0; i<iLen; i++) // prepare color data for max length line
               usTemp[i] = us;
#endif
            spilcdSetPosition(x, y, 1, iLen, bRender);
            myspiWrite((uint8_t*)usTemp, iLen*2, MODE_DATA, bRender); // write the row we changed
        }
    } // y major case
} /* spilcdDrawLine() */
//
// Decompress one line of 8-bit RLE data
//
unsigned char * DecodeRLE8(unsigned char *s, int iWidth, uint16_t *d, uint16_t *usPalette)
{
unsigned char c, ucRepeat, ucCount, ucColor;
long l;

   ucRepeat = 0;
   ucCount = 0;

   while (iWidth > 0)
   {
      if (ucCount) // some non-repeating bytes to deal with
      {  
         while (ucCount && iWidth > 0)
         {  
            ucCount--;
            iWidth--; 
            ucColor = *s++;
            *d++ = usPalette[ucColor];
         } 
         l = (long)s;
         if (l & 1) s++; // compressed data pointer must always be even
      }
      if (ucRepeat == 0 && iWidth > 0) // get a new repeat code or command byte
      {
         ucRepeat = *s++;
         if (ucRepeat == 0) // command code
         {
            c = *s++;
            switch (c)
            {
               case 0: // end of line
                 break; // we already deal with this
               case 1: // end of bitmap
                 break; // we already deal with this
               case 2: // move
                 c = *s++; // debug - delta X
                 d += c; iWidth -= c;
                 c = *s++; // debug - delta Y
                 break;
               default: // uncompressed data
                 ucCount = c;
                 break;
            } // switch on command byte
         }
         else
         {
            ucColor = *s++; // get the new colors
         }     
      }
      while (ucRepeat && iWidth > 0)
      {
         ucRepeat--;
         *d++ = usPalette[ucColor];
         iWidth--;
      } // while decoding the current line
   } // while pixels on the current line to draw
   return s;
} /* DecodeRLE8() */

//
// Decompress one line of 4-bit RLE data
//
unsigned char * DecodeRLE4(unsigned char *s, int iWidth, uint16_t *d, uint16_t *usPalette)
{
unsigned char c, ucOdd, ucRepeat, ucCount, ucColor, uc1, uc2;
long l;

   ucRepeat = 0;
   ucCount = 0;

   while (iWidth > 0)
   {
      if (ucCount) // some non-repeating bytes to deal with
      {
         while (ucCount && iWidth > 0)
         {
            ucCount--;
            iWidth--;
            ucColor = *s++;
            uc1 = ucColor >> 4; uc2 = ucColor & 0xf;
            *d++ = usPalette[uc1];
            if (ucCount && iWidth)
            {
               *d++ = usPalette[uc2];
               ucCount--;
               iWidth--;
            }
         }
         l = (long)s;
         if (l & 1) s++; // compressed data pointer must always be even
      }
      if (ucRepeat == 0 && iWidth > 0) // get a new repeat code or command byte
      {
         ucRepeat = *s++;
         if (ucRepeat == 0) // command code
         {
            c = *s++;
            switch (c)
            {
               case 0: // end of line
                 break; // we already deal with this
               case 1: // end of bitmap
                 break; // we already deal with this
               case 2: // move
                 c = *s++; // debug - delta X
                 d += c; iWidth -= c;
                 c = *s++; // debug - delta Y
                 break;
               default: // uncompressed data
                 ucCount = c;
                 break;
            } // switch on command byte
         }
         else
         {
            ucOdd = 0; // start on an even source pixel
            ucColor = *s++; // get the new colors
            uc1 = ucColor >> 4; uc2 = ucColor & 0xf;
         }     
      }
      while (ucRepeat && iWidth > 0)
      {
         ucRepeat--;
         *d++ = (ucOdd) ? usPalette[uc2] : usPalette[uc1]; 
         ucOdd = !ucOdd;
         iWidth--;
      } // while decoding the current line
   } // while pixels on the current line to draw
   return s;
} /* DecodeRLE4() */

//
// Draw a 4, 8 or 16-bit Windows uncompressed bitmap onto the display
// Pass the pointer to the beginning of the BMP file
// Optionally stretch to 2x size
// Optimized for drawing to the backbuffer. The transparent color index is only used
// when drawinng to the back buffer. Set it to -1 to disable
// returns -1 for error, 0 for success
//
int spilcdDrawBMP(uint8_t *pBMP, int iDestX, int iDestY, int bStretch, int iTransparent, int bRender)
{
    int iOffBits, iPitch;
    uint16_t usPalette[256];
    uint8_t *pCompressed;
    uint8_t ucCompression;
    int16_t cx, cy, bpp, y; // offset to bitmap data
    int j, x;
    uint16_t *pus, us, *d, *usTemp = (uint16_t *)ucRXBuf; // process a line at a time
    uint8_t bFlipped = false;
    
    if (pBMP[0] != 'B' || pBMP[1] != 'M') // must start with 'BM'
        return -1; // not a BMP file
    cx = pBMP[18] | pBMP[19]<<8;
    cy = pBMP[22] | pBMP[23]<<8;
    ucCompression = pBMP[30]; // 0 = uncompressed, 1/2/4 = RLE compressed
    if (ucCompression > 4) // unsupported feature
        return -1;
    if (cy > 0) // BMP is flipped vertically (typical)
        bFlipped = true;
    else
        cy = -cy;
    bpp = pBMP[28] | pBMP[29]<<8;
    if (bpp != 16 && bpp != 4 && bpp != 8) // must be 4/8/16 bits per pixel
        return -1;
    if (iDestX + cx > iCurrentWidth || iDestX < 0 || cx < 0)
        return -1; // invalid
    if (iDestY + cy > iCurrentHeight || iDestY < 0 || cy < 0)
        return -1;
    if (iTransparent != -1) // transparent drawing can only happen on the back buffer
        bRender = 0;
    iOffBits = pBMP[10] | pBMP[11]<<8;
    iPitch = (cx * bpp) >> 3; // bytes per line
    iPitch = (iPitch + 3) & 0xfffc; // must be dword aligned
    // Get the palette as RGB565 values (if there is one)
    if (bpp == 4 || bpp == 8)
    {
        uint16_t r, g, b, us;
        int iOff, iColors;
        iColors = pBMP[46]; // colors used BMP field
        if (iColors == 0 || iColors > (1<<bpp))
            iColors = (1 << bpp); // full palette
        iOff = iOffBits - (4 * iColors); // start of color palette
        for (x=0; x<iColors; x++)
        {
            b = pBMP[iOff++];
            g = pBMP[iOff++];
            r = pBMP[iOff++];
            iOff++; // skip extra byte
            r >>= 3;
            us = (r  << 11);
            g >>= 2;
            us |= (g << 5);
            us |= (b >> 3);
            usPalette[x] = (us >> 8) | (us << 8); // swap byte order for writing to the display
        }
    }
    if (ucCompression) // need to do it differently for RLE compressed
    { 
    uint16_t *d = (uint16_t *)ucRXBuf;
    int y, iStartY, iEndY, iDeltaY;
   
       pCompressed = &pBMP[iOffBits]; // start of compressed data
       if (bFlipped)
       {  
          iStartY = iDestY + cy - 1;
          iEndY = iDestY - 1;
          iDeltaY = -1;
       }
       else
       {  
          iStartY = iDestY;
          iEndY = iDestY + cy;
          iDeltaY = 1;
       }
       for (y=iStartY; y!= iEndY; y += iDeltaY)
       {  
          spilcdSetPosition(iDestX, y, cx, 1, bRender);
          if (bpp == 4)
             pCompressed = DecodeRLE4(pCompressed, cx, d, usPalette);
          else
             pCompressed = DecodeRLE8(pCompressed, cx, d, usPalette);
          spilcdWriteDataBlock((uint8_t *)d, cx*2, bRender);
       } 
       return 0;
    } // RLE compressed

    if (bFlipped)
    {
        iOffBits += (cy-1) * iPitch; // start from bottom
        iPitch = -iPitch;
    }

    // Handle the 2 LCD orientations. The LCD memory is always the same, so I need to write
    // the pixels differently when the display is rotated
    if (iOrientation == LCD_ORIENTATION_ROTATED)
    {
        if (bStretch)
        {
            int iUSPitch = iPitch >> 1; // modify to index shorts
            spilcdSetPosition(iDestX, iDestY, cx*2, cy*2, bRender);
            for (x=0; x<cx; x++)
            {
                pus = (uint16_t *)&pBMP[iOffBits + x*2]; // source line
                for (j=0; j<2; j++) // for systems without half-duplex, we need to prepare the data for each write
                {
                    if (bRender)
                        d = usTemp;
                    else
                        d = (uint16_t *)&pBackBuffer[iOffset + (((x * 2) + j) * iScreenPitch)];
                    if (bpp == 16)
                    {
                        if (iTransparent == -1) // no transparency
                        {
                            for (y=0; y<cy; y++)
                            {
                                us = pus[y*iUSPitch];
                                d[(cy-1-y)*2] = d[(cy-1-y)*2 + 1] = (us >> 8) | (us << 8); // swap byte order
                            } // for y
                        }
                        else
                        {
                            for (y=0; y<cy; y++)
                            {
                                us = pus[y*iUSPitch];
                                if (us != (uint16_t)iTransparent)
                                    d[(cy-1-y)*2] = d[(cy-1-y)*2 + 1] = (us >> 8) | (us << 8); // swap byte order
                            } // for y
                        }
                    }
                    else if (bpp == 8)
                    {
                        uint8_t uc, *s = (uint8_t *)pus;
                        if (iTransparent == -1) // no transparency
                        {
                            for (y=0; y<cy; y++)
                            {
                                uc = s[y*iPitch];
                                us = usPalette[uc];
                                d[(cy-1-y)*2] = d[(cy-1-y)*2 + 1] = us;
                            } // for y
                        }
                        else
                        {
                            for (y=0; y<cy; y++)
                            {
                                uc = s[y*iPitch];
                                if (uc != (uint8_t)iTransparent)
                                {
                                    us = usPalette[uc];
                                    d[(cy-1-y)*2] = d[(cy-1-y)*2 + 1] = us;
                                }
                            } // for y
                        }
                    }
                    else // 4 bpp
                    {
                        uint8_t uc, *s = (uint8_t *)pus;
                        if (iTransparent == -1) // no transparency
                        {
                            for (y=0; y<cy; y++)
                            {
                                uc = s[y*iPitch];
                                if (x & 1)
                                    us = usPalette[uc >> 4];
                                else
                                    us = usPalette[uc & 0xf];
                                d[(cy-1-y)*2] = d[(cy-1-y)*2 + 1] = us;
                            } // for y
                        }
                        else
                        {
                            for (y=0; y<cy; y++)
                            {
                                uint8_t pix;
                                uc = s[y*iPitch];
                                if (x & 1)
                                    pix = uc >> 4;
                                else
                                    pix = uc & 0xf;
                                if (pix != (uint8_t)iTransparent)
                                    d[(cy-1-y)*2] = d[(cy-1-y)*2 + 1] = usPalette[pix];
                            } // for y
                        }
                    }
                    if (bRender)
                        spilcdWriteDataBlock((uint8_t *)usTemp, cy*4, bRender); // write the same line twice
                } // for j
            } // for x
        } // 2:1
        else // 1:1
        {
            int iUSPitch = iPitch >> 1; // modify to index shorts
            spilcdSetPosition(iDestX, iDestY, cx, cy, bRender);
            for (x=0; x<cx; x++)
            {
                pus = (uint16_t *)&pBMP[iOffBits + ((x*bpp)>>3)];
                if (bRender)
                    d = usTemp;
                else
                    d = (uint16_t *)&pBackBuffer[iOffset + (x*iScreenPitch)];
                if (bpp == 16)
                {
                    if (iTransparent == -1) // no transparency
                    {
                        for (y=0; y<cy; y++)
                        {
                            us = pus[0];
                            d[cy-1-y] = (us >> 8) | (us << 8); // swap byte order
                            pus += iUSPitch;
                        }
                    }
                    else
                    {
                        for (y=0; y<cy; y++)
                        {
                            us = pus[0];
                            if (us != (uint16_t)iTransparent)
                                d[cy-1-y] = (us >> 8) | (us << 8); // swap byte order
                            pus += iUSPitch;
                        }
                    }
                } else if (bpp == 8)
                {
                    uint8_t uc, *s = (uint8_t *)pus;
                    if (iTransparent == -1) // no transparency
                    {
                        for (y=0; y<cy; y++)
                        {
                            uc = s[0];
                            d[cy-1-y] = usPalette[uc];
                            s += iPitch;
                        }
                    }
                    else
                    {
                        for (y=0; y<cy; y++)
                        {
                            uc = s[0];
                            if (uc != (uint8_t)iTransparent)
                                d[cy-1-y] = usPalette[uc];
                            s += iPitch;
                        }
                    }
                }
                else // 4 bpp
                {
                    uint8_t uc, *s = (uint8_t *)pus;
                    if (iTransparent == -1) // no transparency
                    {
                        for (y=0; y<cy; y++)
                        {
                            uc = s[0];
                            if (x & 1)
                                d[cy-1-y] = usPalette[uc & 0xf];
                            else
                                d[cy-1-y] = usPalette[uc >> 4];
                            s += iPitch;
                        }
                    }
                    else
                    {
                        for (y=0; y<cy; y++)
                        {
                            uc = s[0];
                            if (x & 1)
                            {
                                if ((uc & 0xf) != (uint8_t)iTransparent)
                                    d[cy-1-y] = usPalette[uc & 0xf];
                            }
                            else
                            {
                                if ((uc >> 4) != (uint8_t)iTransparent)
                                d[cy-1-y] = usPalette[uc >> 4];
                            }
                            s += iPitch;
                        }
                    }
                }
                if (bRender)
                    spilcdWriteDataBlock((uint8_t *)usTemp, cy*2, bRender);
            } // for x
        } // 1:1    } // rotated 90
    }
    else // non-rotated
    {
        if (bStretch)
        {
            spilcdSetPosition(iDestX, iDestY, cx*2, cy*2, bRender);
            for (y=0; y<cy; y++)
            {
                pus = (uint16_t *)&pBMP[iOffBits + (y * iPitch)]; // source line
                for (j=0; j<2; j++) // for systems without half-duplex, we need to prepare the data for each write
                {
                    if (bRender)
                        d = usTemp;
                    else
                        d = (uint16_t *)&pBackBuffer[iOffset + (y*iScreenPitch)];
                    if (bpp == 16)
                    {
                        if (iTransparent == -1) // no transparency
                        {
                            for (x=0; x<cx; x++)
                            {
                                us = pus[x];
                                d[0] = d[1] = (us >> 8) | (us << 8); // swap byte order
                                d += 2;
                            } // for x
                        }
                        else
                        {
                            for (x=0; x<cx; x++)
                            {
                                us = pus[x];
                                if (us != (uint16_t)iTransparent)
                                    d[0] = d[1] = (us >> 8) | (us << 8); // swap byte order
                                d += 2;
                            } // for x
                        }
                    }
                    else if (bpp == 8)
                    {
                        uint8_t *s = (uint8_t *)pus;
                        if (iTransparent == -1) // no transparency
                        {
                            for (x=0; x<cx; x++)
                            {
                                d[0] = d[1] = usPalette[*s++];
                                d += 2;
                            }
                        }
                        else
                        {
                            for (x=0; x<cx; x++)
                            {
                                uint8_t uc = *s++;
                                if (uc != (uint8_t)iTransparent)
                                    d[0] = d[1] = usPalette[uc];
                                d += 2;
                            }
                        }
                    }
                    else // 4 bpp
                    {
                        uint8_t uc, *s = (uint8_t *)pus;
                        if (iTransparent == -1) // no transparency
                        {
                            for (x=0; x<cx; x+=2)
                            {
                                uc = *s++;
                                d[0] = d[1] = usPalette[uc >> 4];
                                d[2] = d[3] = usPalette[uc & 0xf];
                                d += 4;
                            }
                        }
                        else
                        {
                            for (x=0; x<cx; x+=2)
                            {
                                uc = *s++;
                                if ((uc >> 4) != (uint8_t)iTransparent)
                                    d[0] = d[1] = usPalette[uc >> 4];
                                if ((uc & 0xf) != (uint8_t)iTransparent)
                                    d[2] = d[3] = usPalette[uc & 0xf];
                                d += 4;
                            }
                        }
                    }
                    if (bRender)
                        spilcdWriteDataBlock((uint8_t *)usTemp, cx*4, bRender); // write the same line twice
                } // for j
            } // for y
        } // 2:1
        else // 1:1
        {
            spilcdSetPosition(iDestX, iDestY, cx, cy, bRender);
            for (y=0; y<cy; y++)
            {
                pus = (uint16_t *)&pBMP[iOffBits + (y * iPitch)]; // source line
                if (bpp == 16)
                {
                    if (bRender)
                        d = usTemp;
                    else
                        d = (uint16_t *)&pBackBuffer[iOffset + (y * iScreenPitch)];
                    if (iTransparent == -1) // no transparency
                    {
                        for (x=0; x<cx; x++)
                        {
                           us = *pus++;
                           *d++ = (us >> 8) | (us << 8); // swap byte order
                        }
                    }
                    else // skip transparent pixels
                    {
                        for (x=0; x<cx; x++)
                        {
                            us = *pus++;
                            if (us != (uint16_t)iTransparent)
                             d[0] = (us >> 8) | (us << 8); // swap byte order
                            d++;
                        }
                    }
                }
                else if (bpp == 8)
                {
                    uint8_t uc, *s = (uint8_t *)pus;
                    if (bRender)
                        d = usTemp;
                    else
                        d = (uint16_t *)&pBackBuffer[iOffset + (y*iScreenPitch)];
                    if (iTransparent == -1) // no transparency
                    {
                        for (x=0; x<cx; x++)
                        {
                            *d++ = usPalette[*s++];
                        }
                    }
                    else
                    {
                        for (x=0; x<cx; x++)
                        {
                            uc = *s++;
                            if (uc != iTransparent)
                                d[0] = usPalette[*s++];
                            d++;
                        }
                    }
                }
                else // 4 bpp
                {
                    uint8_t uc, *s = (uint8_t *)pus;
                    if (bRender)
                        d = usTemp;
                    else // write to the correct spot directly to save time
                        d = (uint16_t *)&pBackBuffer[iOffset + (y*iScreenPitch)];
                    if (iTransparent == -1) // no transparency
                    {
                        for (x=0; x<cx; x+=2)
                        {
                            uc = *s++;
                            *d++ = usPalette[uc >> 4];
                            *d++ = usPalette[uc & 0xf];
                        }
                    }
                    else // check transparent color
                    {
                        for (x=0; x<cx; x+=2)
                        {
                            uc = *s++;
                            if ((uc >> 4) != iTransparent)
                               d[0] = usPalette[uc >> 4];
                            if ((uc & 0xf) != iTransparent)
                               d[1] = usPalette[uc & 0xf];
                            d += 2;
                        }
                    }
                }
                if (bRender)
                    spilcdWriteDataBlock((uint8_t *)usTemp, cx*2, bRender);
            } // for y
        } // 1:1
    } // non-rotated
    return 0;
} /* spilcdDrawBMP() */

#ifndef __AVR__
//
// Returns the current backbuffer address
//
uint16_t * spilcdGetBuffer(void)
{
    return (uint16_t *)pBackBuffer;
}
//
// Allocate the back buffer for delayed rendering operations
// returns -1 for failure, 0 for success
//
int spilcdAllocBackbuffer(void)
{
    if (pBackBuffer != NULL) // already allocated
        return -1;
    iScreenPitch = iWidth * 2;
    pBackBuffer = (uint8_t *)malloc(iScreenPitch * iHeight);
    if (pBackBuffer == NULL) // no memory
        return -1;
    memset(pBackBuffer, 0, iScreenPitch * iHeight);
    iOffset = 0; // starting offset
    iMaxOffset = iScreenPitch * iHeight; // can't write past this point
    iWindowX = iWindowY = 0; // current window = whole display
    iWindowCX = iWidth;
    iWindowCY = iHeight;
    return 0;
}
//
// Free the back buffer
//
void spilcdFreeBackbuffer(void)
{
    if (pBackBuffer)
    {
        free(pBackBuffer);
        pBackBuffer = NULL;
    }
}
//
// Rotate a 1-bpp mask image around a given center point
// valid angles are 0-359
//
void spilcdRotateBitmap(uint8_t *pSrc, uint8_t *pDest, int iBpp, int iWidth, int iHeight, int iPitch, int iCenterX, int iCenterY, int iAngle)
{
int32_t i, x, y;
int16_t pre_sin[512], pre_cos[512], *pSin, *pCos;
int32_t tx, ty, sa, ca;
uint8_t *s, *d, uc, ucMask;
uint16_t *uss, *usd;

    if (pSrc == NULL || pDest == NULL || iWidth < 1 || iHeight < 1 || iPitch < 1 || iAngle < 0 || iAngle > 359 || iCenterX < 0 || iCenterX >= iWidth || iCenterY < 0 || iCenterY >= iHeight || (iBpp != 1 && iBpp != 16))
        return;
    // since we're rotating from dest back to source, reverse the angle
    iAngle = 360 - iAngle;
    if (iAngle == 360) // just copy src to dest
    {
        memcpy(pDest, pSrc, iHeight * iPitch);
        return;
    }
    // Create a quicker lookup table for sin/cos pre-multiplied at the given angle
    sa = (int32_t)i16SineTable[iAngle]; // sine of given angle
    ca = (int32_t)i16SineTable[iAngle+90]; // cosine of given angle
    for (i=-256; i<256; i++) // create the pre-calc tables
    {
        pre_sin[i+256] = (sa * i) >> 15; // sin * x
        pre_cos[i+256] = (ca * i) >> 15;
    }
    pSin = &pre_sin[256]; pCos = &pre_cos[256]; // point to 0 points in tables
    for (y=0; y<iHeight; y++)
    {
        int16_t siny = pSin[y-iCenterY];
        int16_t cosy = pCos[y-iCenterY];
        d = &pDest[y * iPitch];
        usd = (uint16_t *)d;
        ucMask = 0x80;
        uc = 0;
        for (x=0; x<iWidth; x++)
        {
            // Rotate from the destination pixel back to the source to not have gaps
            // x' = cos*x - sin*y, y' = sin*x + cos*y
            tx = iCenterX + pCos[x-iCenterX] - siny;
            ty = iCenterY + pSin[x-iCenterX] + cosy;
            if (iBpp == 1)
            {
                if (tx > 0 && ty > 0 && tx < iWidth && ty < iHeight) // check source pixel
                {
                    s = &pSrc[(ty*iPitch)+(tx>>3)];
                    if (s[0] & (0x80 >> (tx & 7)))
                        uc |= ucMask; // set destination pixel
                }
                ucMask >>= 1;
                if (ucMask == 0) // write the byte into the destination bitmap
                {
                    ucMask = 0x80;
                    *d++ = uc;
                    uc = 0;
                }
            }
            else // 16-bpp
            {
                if (tx > 0 && ty > 0 && tx < iWidth && ty < iHeight) // check source pixel
                {
                    uss = (uint16_t *)&pSrc[(ty*iPitch)+(tx*2)];
                    *usd++ = uss[0]; // copy the pixel
                }
            }
        }
        if (iBpp == 1 && ucMask != 0x80) // store partial byte
            *d++ = uc;
    } // for y
} /* spilcdRotateMask() */
#endif // !__AVR__
