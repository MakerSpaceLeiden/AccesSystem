#ifndef _H_DISPLAY_TFT
#define _H_DISPLAY_TFT

#include "global.h"
#include "pins_tft177.h" // 1.77" boards
// #include "pins_ttgo.h" // TTGO unit with own buttons; no LEDs.

// undef if you do not want the screensaver
#define SCREENSAVER_TIMEOUT (15 * 60 * 1000 /* 15 mins */)

void setupTFT();

void updateDisplay(state_t md);

void updateDisplay_startProgressBar(const char *str);
void updateDisplay_progressBar(float p);

void updateDisplay_progressText(const char * str);
void displayForceShowErrorModal(const char * str, const char * substr);
void displayForceShowModal(const char * str, const char * substr);
void displayForceShow(const char * str, const char * substr);
void updateDisplay_warningText(const char * str);

void updateClock(bool force);

void setTFTPower(bool onoff);
#endif
