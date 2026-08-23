#if 0
#ifndef _H_DISPLAY_TFT
#define _H_DISPLAY_TFT

#include "REST/ACRestNode.h"

// 1.77 160(RGB)x128 Board  labeling v.s pining
// 1  GND
// 2  VCC   3V3
// 3  SCK   CLK
// 4  SDA   MOSI
// 5  RES   RESET
// 6  RS    A0 / bank select / DC
// 7  CS    SS
// 8 LEDA - wired to 3V3
// 9 LEDA - already wired to pin 8 on the board.
//
#define TFT_BL              4
#define TFT_BACKLIGHT_ON    HIGH

#define LED_1               23 // CANCELand left red light
#define LED_2               22 // OK and right red light
#define BUTTON_1            32 // CANCEL and LEFT button
#define BUTTON_2            33 // OK and right button
#define BOARD_V3_SENSE      35 // hard wired to GND on board V3

#define RFID_SCLK           16 // shared with screen
#define RFID_MOSI            5 // shared with screen
#define RFID_MISO           13 // shared with screen
#define RFID_CS             25  // ok
#define RFID_RESET          17  // shared with screen
#define RFID_IRQ             3

#define TFT_ROTATION 3

// undef if you do not want the screensaver
#define SCREENSAVER_TIMEOUT (15 * 60 * 1000 /* 15 mins */)

void setupTFT();
void setTFTPower(bool onoff);


void updateDisplay_startProgressBar(const char *str);
void updateDisplay_progressBar(float p);

void updateDisplay_progressText(const char * str);
void displayForceShowErrorModal(const char * str, const char * substr);
void displayForceShowModal(const char * str, const char * substr);
void displayForceShow(const char * str, const char * substr);
void updateDisplay_warningText(const char * str);

void updateClock(bool force);

#endif
#endif
