#pragma once

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

#ifndef ADAFRUIT_GFX_DEGREE_SYMBOL
#define ADAFRUIT_GFX_DEGREE_SYMBOL (247)
#endif

#include "ACBase.h"
#include "ACBaseNode.h"


class Display : public Adafruit_SH1106G {
private:
    typedef Adafruit_SH1106G super;
    const unsigned short SCREEN_WIDTH, SCREEN_HEIGHT;
public:
    Display(uint16_t w, uint16_t h, TwoWire *twi = &Wire,
                   int16_t rst_pin = -1, uint32_t preclk = 400000,
                   uint32_t postclk = 100000)
                   : Adafruit_SH1106G(w,h,twi,rst_pin,preclk,postclk),
                       SCREEN_WIDTH(w), SCREEN_HEIGHT(h) {};
                   
    void begin(uint8_t SCREEN_Address, bool reset = true);

    void setDisplayScreensaver(bool on);
    
    void updateDisplay(const char * title, String left, String right, bool rebuildFull = false);
    void updateDisplayStateMsg(String msg,int line = 0);
    void updateDisplayProgressbar(unsigned int percentage, bool rebuildFull = false);

    void drawCentredBitmap(const unsigned char * bitmap, unsigned short w, unsigned short h, unsigned char col);
    void print_centred(char * title, bool titlelines = true);
    void print_centered_QR(char * titleOrNull, char * url);
};


