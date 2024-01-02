#ifndef _H_WHITE108
#define _H_WHITE108

// https://wiki.makerspaceleiden.nl/mediawiki/index.php/Powernode_1.1
//
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <ButtonDebounce.h>

// White / 1.08
const uint8_t LED_INDICATOR = 12;
const uint8_t OUT0 = 16;
const uint8_t OUT1 = 04;
const uint8_t BUTT0 = 14;
const uint8_t BUTT1 = 13;
const uint8_t OPTO0 = 34;
const uint8_t OPTO1 = 35;
const uint8_t CURR0 = 36; // SENSOR_VN
const uint8_t CURR1 = 37; // SENSOR_VP
const uint8_t BUZZER = 2;

const uint8_t RFID_ADDR = 0x28;
const uint8_t RFID_RESET = 32;
const uint8_t RFID_IRQ = 33;

const uint8_t I2C_SDA = 05; // 21 is the default
const uint8_t I2C_SCL = 15; // 22 is the default

// Oled desplay - type SH1106G via i2c. Not always wired up.
//
const uint8_t SCREEN_Address = 0x3c;
const uint16_t SCREEN_WIDTH = 128; // OLED display width, in pixels
const uint16_t SCREEN_HEIGHT = 64; // OLED display height, in pixels
const uint8_t SCREEN_RESET = -1;     //  Not wired up
#define SCREEN_RESET -1   //  Not wired up

#define MENU_BUTTON (BUTT0)
#define OFF_BUTTON (BUTT1)
#define WHEN_PRESSED (ONLOW)  // pullup, active low buttons

// Needed for the screen
#include <MachineState.h>

#include <RFID_MFRC522.h>
#include "ACNode.h"

// Extra, hardware specific states
extern MachineState::machinestate_t FAULTED, SCREENSAVER, INFODISPLAY, POWERED;

// Global; as we have just one of them; and we have some plain C functions as calbacks.
extern Adafruit_SH1106G * _display;

class WhiteNodev108 : public ACNode {
public:
    WhiteNodev108(const char * machine, const char * ssid, const char * ssid_passwd, acnode_proto_t proto = PROTO_SIG2);
    WhiteNodev108(const char * machine = NULL, bool wired = true, acnode_proto_t proto = PROTO_SIG2);
    void setOTAPasswordHash(const char * ota_md5);
    void begin(bool hasScreen = true);
    void loop();
    void updateDisplay(String left, String right, bool rebuildFull = false);
    void updateDisplayStateMsg(String msg,int line = 0);
    void updateDisplayProgressbar(unsigned int percentage, bool rebuildFull = false);
    void setDisplayScreensaver(bool on);
    void onSwipe(RFID::THandlerFunction_SwipeCB fn);
    
    typedef enum { PAGE_NORMAL= 0, PAGE_QR, PAGE_LOG_QR, PAGE_INFO, PAGE_SNTP, PAGE_MQTT, PAGE_BUTT, PAGE_LAST} page_t;
    void updateInfoDisplay(page_t page = PAGE_QR);
    
    typedef std::function<void(const int)> ButtonCallback;
    void setOffCallback(ButtonCallback callback,int mode = CHANGE) {
        _offCallBack = callback;
        _offCallBackMode = mode;
    };
    void setMenuCallback(ButtonCallback callback,int mode = CHANGE) {
        _menuCallBack = callback;
        _menuCallBackMode = mode;
    }
    MachineState machinestate;
    
    void setOnChangeCallback(MachineState::machinestate_t state, MachineState::THandlerFunction_OnChangeCB onChangeCB) {
        _onChangeState = state;
        _onChangeCB =onChangeCB;
    }
    
    void buzzer(bool onOff);
    void buzzerOk();
    void buzzerErr();
    
private:
    // reader build into the board - so only one type; and it is hardcoded.
    //
    RFID_MFRC522 * _reader;
    bool _hasScreen;
    page_t _pageState;

    bool _otaOK = true;
    
    ButtonDebounce *offButton, *menuButton;
    ButtonCallback _offCallBack, _menuCallBack = NULL;
    int _offCallBackMode, _menuCallBackMode;
    
    MachineState::THandlerFunction_OnChangeCB _onChangeCB;
    MachineState::machinestate_t _onChangeState;
    
    RFID::THandlerFunction_SwipeCB _swipeCB;
    
    LED errorLed = LED(LED_INDICATOR);
    
    const unsigned long CARD_CHECK_WAIT = 3;              // wait up to 3 seconds for a card to be checked.
    const unsigned long MAX_IDLE_TIME = 45 * 60;          // auto power off the machine after 45 minutes of no use.
    const unsigned long SHOW_COUNTDOWN_TIME_AFTER = 10 * 60;  // Only start showing above idle to off countdown after 10 minutes of no use.
    const unsigned long SCREENSAVER_DELAY = 20 * 60;      // power off the screen after some period of no swipe/interaction.
    
    unsigned long manual_poweroff = 0;
    unsigned long idle_poweroff = 0;
    unsigned long errors = 0;
    
    void pop();
    
    void report(JsonObject & out);
    
    typedef struct state {
        uint8_t pin; const char * label; int lst; int tpe;
    } state_t;
    state_t states[6] = {
        { BUTT0, "YES/nxt", 1, INPUT_PULLUP },
        { BUTT1, "NO/back", 1, INPUT_PULLUP },
        { CURR0, "Curr 1" , 1, INPUT },
        { CURR1, "Curr 2", 1, INPUT },
        { OPTO0, "Opto 1", 1, INPUT  },
        { OPTO1, "Opto 2", 1, INPUT },
    };
};
#endif
