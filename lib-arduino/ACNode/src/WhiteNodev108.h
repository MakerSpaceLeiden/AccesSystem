// https://wiki.makerspaceleiden.nl/mediawiki/index.php/PowerNode_White
//
#ifndef _H_WHITE108
#define _H_WHITE108

#include <Wire.h>
#include "ExpandedGPIO.h"
#include "util/IODebounce.h"
#include "Display/Deck.h"
#include "Display/Display.h"
#include "Display/DeckController.h"

// White / 1.08

#ifndef MENU_BUTTON
#define MENU_BUTTON (BUTT0)
#endif

#ifndef OFF_BUTTON
#define OFF_BUTTON (BUTT1)
#endif

#define WHEN_PRESSED (ONLOW)  // pullup, active low buttons

// Needed for the screen
#include <MachineState.h>

#include <RFID/RFID_MFRC522.h>
#include "REST/ACRestNode.h"

// Extra, hardware specific states
extern MachineState::machinestate_t FAULTED, SCREENSAVER, INFODISPLAY, POWERED;

// Oled desplay - type SH1106G via i2c. Not always wired up.
//
static const uint8_t SCREEN_Address = 0x3c;
static const uint8_t SCREEN_WIDTH = 128; // OLED display width, in pixels
static const uint8_t SCREEN_HEIGHT = 64; // OLED display height, in pixels
static const uint8_t SCREEN_RESET = -1;     //  Not wired up

// Global; as we have just one of them; and we have some plain C functions as callbacks.
// When set to NULL; no display is wired up.
//
extern Display * _display;

// const uint8_t RFID_ADDR = 0x28;
// const uint8_t RFID_RESET = 32;
// const uint8_t RFID_IRQ = 33;

// const uint8_t I2C_SDA = 05; // 21 is the default
// const uint8_t I2C_SCL = 15; // 22 is the default

typedef struct iostate {
    uint8_t pin; const char * label; int lst; int tpe;
} iostate_t;

class WhiteNodev108 : public ACNodeRest {
private:
    typedef ACNodeRest super;
public:
    uint8_t LED_INDICATOR,
    OUT0, OUT1, BUTT0, BUTT1, OPTO0, OPTO1,
    CURR0, CURR1,
    BUZZER,
    STEP_DIR, STEP_STEP, STEP_SLP,
    RFID_ADDR, RFID_RESET, RFID_IRQ, I2C_SDA, I2C_SCL;
 
    void CONSTS() {
        Wire.setPins(I2C_SDA, I2C_SCL);
        
//        super::CONSTS();
                
        LED_INDICATOR = 12;
        OUT0 = 16;
        OUT1 = 04;
        BUTT0 = 14;
        BUTT1 = 13;
        OPTO0 = 34;
        OPTO1 = 35;
        CURR0 = 36; // SENSOR_VN
        CURR1 = 37; // SENSOR_VP
        BUZZER = 2;
        
        STEP_DIR = OUT1;
        STEP_STEP = OUT0;
        STEP_SLP = BUTT1;
        
        RFID_ADDR = 0x28;
        RFID_RESET = 32;
        RFID_IRQ = 33;
        
        I2C_SDA = 05; // 21 is the default
        I2C_SCL = 15; // 22 is the default
        
        static iostate_t s[ ]= {
            { BUTT0, "YES/nxt", 1, INPUT_PULLUP },
            { BUTT1, "NO/back", 1, INPUT_PULLUP },
            { CURR0, "Curr 1" , 1, INPUT },
            { OPTO0, "Opto 1", 1, INPUT  },
            { OPTO1, "Opto 2", 1, INPUT },
            { 255, NULL },
        };
        iostates = s;
        
        _ota_md5 = NULL;
    };
    virtual const char * name() { return "WhiteNodev108"; }
    typedef std::function<bool(const int)> ButtonCallback;
    
    WhiteNodev108(const char * machine, const char * ssid, const char * ssid_passwd, acnode_proto_t proto = PROTO_REST);
    WhiteNodev108(const char * machine = NULL, bool wired = true, acnode_proto_t proto = PROTO_REST);
    
    void setOTAPasswordHash(const char * ota_md5) { _ota_md5 = ota_md5; };
    void begin();
    void loop();
    
    void onSwipe(RFID::THandlerFunction_SwipeCB fn);
        
    void updateDisplay(String left, String right, bool rebuildFull = false);
    void updateDisplayStateMsg(String msg,int line = 0);
    
    void setOffCallback(ButtonCallback callback,int mode = CHANGE);
    void setMenuCallback(ButtonCallback callback,int mode = CHANGE);
    
    void setOnChangeCallback(MachineState::machinestate_t state, MachineState::THandlerFunction_OnChangeCB onChangeCB);
    void setIdleCallback(MachineState::machinestate_t state, MachineState::THandlerFunction_OnChangeCB onIdleCB);

    void buzzer(bool onOff);
    void buzzerOk();
    void buzzerErr();

    void setNodeDeck(Deck * deck);
    void addDeck(Deck * deck);
    Deck * currentDeck() { return _deskCtrl->current(); };
    
protected:
    LED * errorLed = NULL;
    void pop();
    iostate_t * iostates;

private:
    // reader build into the board - so only one type; and it is hardcoded.
    //
    RFID_MFRC522 * _reader;
    DeckController *_deskCtrl;
    ApprovalDeck *approvalDeck;
    FirmwareDeck *firmwareDeck;

    IODebounce *offButton, *menuButton;
    ButtonCallback _offCallBack, _menuCallBack = NULL;
    int _offCallBackMode, _menuCallBackMode;
    
    MachineState::THandlerFunction_OnChangeCB _onChangeCB;
    MachineState::machinestate_t _onChangeState;
    
    RFID::THandlerFunction_SwipeCB _swipeCB;
    
    const unsigned long CARD_CHECK_WAIT = 3;              // wait up to 3 seconds for a card to be checked.
    const unsigned long MAX_IDLE_TIME = 45 * 60;          // auto power off the machine after 45 minutes of no use.
    const unsigned long SHOW_COUNTDOWN_TIME_AFTER = 10 * 60;  // Only start showing above idle to off countdown after 10 minutes of no use.
    const unsigned long SCREENSAVER_DELAY = 20 * 60;      // power off the screen after some period of no swipe/interaction.
    
    unsigned long manual_poweroff = 0;
    unsigned long idle_poweroff = 0;
    unsigned long errors = 0;
    const char * _lasterrmsg = NULL;
    
    const char * _ota_md5;
    unsigned long _last_buzz = 0;
#if 0
    const uint8_t * leds() {
        static const uint8_t tmp[] = { BUZZER, LED_INDICATOR, 255};
        return tmp;
    };
#endif
    
    void report(JsonObject & out);
};


class ButtonsDeck: public Deck {
public:
    ButtonsDeck(ACNodeBase * node, iostate_t * states) : Deck(node), iostates(states)  {};
    virtual void render_pane(bool refresh);
private:
    iostate_t * iostates;
};
#endif
