#define _H_WHITE108

// https://wiki.makerspaceleiden.nl/mediawiki/index.php/Powernode_1.1
//
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include <ButtonDebounce.h>

// White / 1.08

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
    const uint8_t LED_INDICATOR, 
		  OUT0, OUT1, BUTT0, BUTT1, OPTO0, OPTO1, 
 		  CURR0, CURR1, 
		  BUZZER, 
		  STEP_DIR, STEP_STEP, STEP_SLP,
		  RFID_ADDR, RFID_RESET, RFID_IRGQ, I2C_SDA, I2C_SCL, 
		  SCREEN_Address, SCREEN_WIDTH, SCREEN_HEIGHT, SCREEN_RESET;

    void CONSTS() {
	ACNode::CONSTS();

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

        STEP_DIR = OUT2;
        STEP_STEP = OUT1;
        STEP_SLP = BUT1;

        RFID_ADDR = 0x28;
        RFID_RESET = 32;
        RFID_IRQ = 33;

        I2C_SDA = 05; // 21 is the default
        I2C_SCL = 15; // 22 is the default
     
        // Oled desplay - type SH1106G via i2c. Not always wired up.
        //
        SCREEN_Address = 0x3c;
        SCREEN_WIDTH = 128; // OLED display width, in pixels
        SCREEN_HEIGHT = 64; // OLED display height, in pixels
        SCREEN_RESET = -1;     //  Not wired up

        static const states _s[] = {
        	{ BUTT0, "YES/nxt", 1, INPUT_PULLUP },
        	{ BUTT1, "NO/back", 1, INPUT_PULLUP },
        	{ CURR0, "Curr 1" , 1, INPUT },
        	{ CURR1, "Curr 2", 1, INPUT },
        	{ OPTO0, "Opto 1", 1, INPUT  },
        	{ OPTO1, "Opto 2", 1, INPUT },
	        NULL
	};
        states = (state *) &_s;
    };

    typedef std::function<void(const int)> ButtonCallback;
    MachineState machinestate;

    WhiteNodev108(const char * machine, const char * ssid, const char * ssid_passwd, acnode_proto_t proto = PROTO_SIG2);
    WhiteNodev108(const char * machine = NULL, bool wired = true, acnode_proto_t proto = PROTO_SIG2);

    void setOTAPasswordHash(const char * ota_md5);
    void begin(bool hasScreen = true);
    void loop();

    void setDisplayScreensaver(bool on);
    void onSwipe(RFID::THandlerFunction_SwipeCB fn);
    
    typedef enum { PAGE_NORMAL= 0, PAGE_QR, PAGE_LOG_QR, PAGE_INFO, PAGE_SNTP, PAGE_MQTT, PAGE_BUTT, PAGE_LAST} page_t;
    void updateInfoDisplay(page_t page = PAGE_QR);
    void updateDisplay(String left, String right, bool rebuildFull = false);
    void updateDisplayStateMsg(String msg,int line = 0);
    void updateDisplayProgressbar(unsigned int percentage, bool rebuildFull = false);

    void setOffCallback(ButtonCallback callback,int mode = CHANGE);
    void setMenuCallback(ButtonCallback callback,int mode = CHANGE);
    
    void setOnChangeCallback(MachineState::machinestate_t state, MachineState::THandlerFunction_OnChangeCB onChangeCB);

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
    
    LED * errorLed;
    
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
    state_t * states;

};

class BlackNodev111 : public WhiteNode108 {
public:
    // Some extra IO
    uint8_t OPTO3, OPTO4,
            LEDA, LEDB, LEDC, LEDD, LEDE,
            IOA, IOB, IOC, IOD, IOE;

    void CONSTS() {
	WhiteNode108::CONSTS();

        // AW5293 reset and interrupt pins are wired up. 
        AW_RST = 35; // Was OPTO1
        AW_INT = 35; // Was OPTO2

        // Rewired to their own pins (mostly shared with strapping
        // pins as it known that the A4988 has no pull up/downs on
        // these pins. They are straight inputs.
        //
        STEP_DIR = 0; // Was OUT2
        STEP_STEP = 2; // Was OUT1
        STEP_SLP = 12; // Was BUT1

        // Moved from ESP32 to AW gated IO
        LED_INDICATOR = PIN_HPIO_AW5293 | (8+6); // P1_6 on the AW5293
        BUZZER = PIN_HPIO_AW5293 | (8+7); // P1_7 on the AW5293

        // Moved from ESP32 to AW gated IO
        OPTO1 = PIN_HPIO_AW5293 | (0+4); // P0_4 
        OPTO2 = PIN_HPIO_AW5293 | (0+3); // P0_3

        // Two extra LEDs, introduced in v1.11
        OPTO3 = PIN_HPIO_AW5293 | (0+2); // P0_2
        OPTO4 = PIN_HPIO_AW5293 | (0+1); // P0_0

        // Extra LEDs on the front, introduced in v1.11
        LEDA = PIN_HPIO_AW5293 | (8+0); // P1_0
        LEDB = PIN_HPIO_AW5293 | (8+1); // P1_1
        LEDC = PIN_HPIO_AW5293 | (8+2); // P1_2
        LEDD = PIN_HPIO_AW5293 | (8+3); // P1_3
        LEDE = PIN_HPIO_AW5293 | (0+0); // P0_0

        // Extra connector intruduced with v1.11
        IOA = PIN_HPIO_AW5293 | (0+5); // P0_5
        IOB = PIN_HPIO_AW5293 | (0+6); // P0_6
        IOC = PIN_HPIO_AW5293 | (0+7); // P0_7
        IOD = PIN_HPIO_AW5293 | (8+4); // P1_4
        IOE = PIN_HPIO_AW5293 | (8+5); // P1_5

       static const states _s[] = {
        	{ BUTT0, "YES/nxt", 1, INPUT_PULLUP },
        	{ BUTT1, "NO/back", 1, INPUT_PULLUP },
        	{ BUTT3, "MENU", 1, INPUT_PULLUP },
        	{ CURR0, "Curr 1" , 1, INPUT },
        	{ OPTO0, "Opto 1", 1, INPUT  },
        	{ OPTO1, "Opto 2", 1, INPUT },
        	{ OPTO2, "Opto 3", 1, INPUT },
        	{ OPTO3, "Opto 4", 1, INPUT },
		NULL
       };
       states = (state *) &_s;
    };

    // From 1.11 nodes have an internal overwrite switch/jumper. When setting it
    // using this method - the main loop will monitor for this switch or jumper
    // to be used as a bypass.
    void setMonitoredOutput(uint8_t num, bool val);

    private:
        int8_t expectOut1 = -1;
        int8_t expectOut2 = -1;
};
#endif
