#pragma once

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "Adafruit_NoOLED.h"
#include <ESPAsyncWebServer.h>
#include <TLog.h>

#include "ACBase.h"
#include "ACBaseNode.h"
#include "Display/fonts.h"


#ifndef ADAFRUIT_GFX_DEGREE_SYMBOL
// #define ADAFRUIT_GFX_DEGREE_SYMBOL (247)
#define ADAFRUIT_GFX_DEGREE_SYMBOL (0x5e) // ^
#endif

class Display : public Adafruit_SH1106G {
private:
    typedef Adafruit_SH1106G super;
    bool _headless = false;
public:
    Display(uint16_t w, uint16_t h, TwoWire *twi = &Wire,
                   int16_t rst_pin = -1, uint32_t preclk = 400000,
                   uint32_t postclk = 100000)
                   : Adafruit_SH1106G(w,h,twi,rst_pin,preclk,postclk),
                       SCREEN_WIDTH(w), SCREEN_HEIGHT(h) {};
                   
    bool begin(uint8_t SCREEN_Address, bool reset = true, const char * bootmsg = NULL, bool headless = false);
    void setWebResponder(const char * urlPrefix, AsyncWebServer * _webServer, bool raw = false);

    void setDisplayScreensaver(bool on);
    
    void updateDisplay(const char * title, const char *left, const char * right, bool rebuildFull = false);
    void updateDisplayStateMsg(const char * msg,int line = 0);
    void updateDisplayProgressbar(unsigned int percentage, bool rebuildFull = false);

    void drawCentredBitmap(const unsigned char * bitmap, unsigned short w, unsigned short h, unsigned char col);

    void print_centred(const char * title, bool titlelines = true);
    void print_centered_QR(const char * titleOrNull, char * url);
    void printCmdBar(const char * left, const char * right);
    
    uint16_t widthOfString(const char * str);

    const unsigned short SCREEN_WIDTH, SCREEN_HEIGHT;
    inline void display(void) {
	if (!_headless) super::display();
    };
};

extern Display * _display;
