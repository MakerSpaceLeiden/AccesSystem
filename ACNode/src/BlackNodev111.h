#pragma once

#ifndef YES_BUTTON
#define YES_BUTTON (BUTT2)
#endif

#include "WhiteNodev108.h"
#include "LEDAW.h"
#include "Display/DeckController.h"


class BlackNodev111 : public WhiteNodev108 {
private:
    typedef WhiteNodev108 super;
public:
    BlackNodev111(const char * machine, const char * ssid, const char * ssid_passwd, acnode_proto_t proto = PROTO_SIG2);
    BlackNodev111(const char * machine = NULL, bool wired = true, acnode_proto_t proto = PROTO_SIG2);
    BlackNodev111() { Serial.println("Destroy WhiteNode - should never happen"); };

    virtual const char * name() { return "BlackNodev111"; }
    
    // Some extra IO
    uint8_t OPTO2, OPTO3, BUTT2,
    LEDA, LEDB, LEDC, LEDD, LEDE,
    IOA, IOB, IOC, IOD, IOE;
    
    // AW9523 reset and interrupt pins are wired up.
    //
    static const uint8_t AW_RST = 35; // Was opto 1
    static const uint8_t AW_INT = 36; // Was opto 2
    
    void CONSTS() {
        // super::CONSTS();
        
        // Rewired to their own pins (mostly shared with strapping
        // pins as it known that the A4988 has no pull up/downs on
        // these pins. They are straight inputs.
        //
        STEP_DIR = 0; // Was OUT2
        STEP_STEP = 2; // Was OUT1
        STEP_SLP = 12; // Was BUT1
        
        // Moved from ESP32 to AW gated IO (PIN_HPIO_AW9523==2<<6= 128
        LED_INDICATOR = PIN_HPIO_AW9523 | (8+6); // P1_6 on the AW9523
        BUZZER = PIN_HPIO_AW9523 | (8+7);        // P1_7 on the AW9523
        
        // Moved from ESP32 to AW gated IO
        OPTO0 = PIN_HPIO_AW9523 | (0+4); // P0_4
        OPTO1 = PIN_HPIO_AW9523 | (0+3); // P0_3
        
        // Two extra opto couplers, introduced in v1.11
        OPTO2 = PIN_HPIO_AW9523 | (0+1); // P0_1
        OPTO3 = PIN_HPIO_AW9523 | (0+2); // P0_2
        
        // Extra LEDs on the front, introduced in v1.11
        LEDA = PIN_HPIO_AW9523 | (8+0); // P1_0 -- checked on blue board
        LEDB = PIN_HPIO_AW9523 | (8+2); // P1_2 -- checked on blue board
        LEDC = PIN_HPIO_AW9523 | (8+1); // P1_1 -- checked on blue board
        LEDD = PIN_HPIO_AW9523 | (8+3); // P1_3
        LEDE = PIN_HPIO_AW9523 | (0+0); // P0_0 -- checked on black & blue board
        
        // Extra connector intruduced with v1.11
        IOA = PIN_HPIO_AW9523 | (0+5); // P0_5
        IOB = PIN_HPIO_AW9523 | (0+6); // P0_6
        IOC = PIN_HPIO_AW9523 | (0+7); // P0_7
        IOD = PIN_HPIO_AW9523 | (8+4); // P1_4
        IOE = PIN_HPIO_AW9523 | (8+5); // P1_5
        
        BUTT2 = 0; // Labeled MENU on the PCB -- not yet tested.

        static iostate_t s[] = {
            { BUTT0, "YES/nxt", 1, INPUT_PULLUP },
            { BUTT1, "NO/back", 1, INPUT_PULLUP },
            { BUTT2, "MENU", 1, INPUT_PULLUP },
            { CURR0, "Curr 1" , 1, INPUT },
            { OPTO0, "Opto 1", 1, INPUT  },
            { OPTO1, "Opto 2", 1, INPUT },
            { OPTO2, "Opto 3", 1, INPUT },
            { OPTO3, "Opto 4", 1, INPUT },
            { 255, NULL, 0, 0 },
        };
        iostates = s;
    };
    void begin(bool hasDisplay = true);
    void pop();
    void loop();
    
    // Newer nodes have an extra button.
    //
    void setYesCallback(ButtonCallback callback,int mode = CHANGE);
    void setHeartbeat(bool on) { _hearthBeat = on; };
private:
    IODebounce *yesButton;
    ButtonCallback _yesCallBack;
    int _yesCallBackMode;
    bool _hearthBeat = true;

#if 0
    const uint8_t * leds() {
        static const uint8_t tmp[] = { BUZZER, LED_INDICATOR, LEDA, LEDB, LEDC, LEDD, LEDE, 255};
        return tmp;
    };
#endif
};
