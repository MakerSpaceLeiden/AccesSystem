#include "WhiteNodev108.h"
#include "msl-logo.h"

WhiteNodev108::WhiteNodev108(const char * machine, bool wired, acnode_proto_t proto)
: ACNode(machine, wired, proto) {
    // Non standard pins for i2c.
    Wire.begin(I2C_SDA, I2C_SCL);
    
    // All nodes have a build-in RFID reader; so fine to hardcode this.
    //
    _reader = new RFID_MFRC522(&Wire, RFID_ADDR, RFID_RESET, -1); // RFID_IRQ);
    addHandler(_reader);
};

void WhiteNodev108::begin(bool hasScreen) {
    _hasScreen = hasScreen;
    if (_hasScreen) {
        _display = new Adafruit_SH1106G(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, SCREEN_RESET);
        _display->setRotation(2); // for purple/white boards - OLED is upside down.
        _display->begin(SCREEN_Address, true);
        _display->clearDisplay();
        _display->drawBitmap(
                             (SCREEN_WIDTH-msl_logo_width)/2,
                             (SCREEN_HEIGHT-msl_logo_height)/2,
                             msl_logo,msl_logo_width,msl_logo_height,SH110X_WHITE);
        _display->display();
        _pageState = -2;
    };
    
    
    ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_RTL8201, ETH_CLOCK_GPIO17_OUT);
    
    ACNode::begin(BOARD_NG);
    updateDisplay("","MORE", true);
}

void WhiteNodev108::setDisplayScreensaver(bool on) {
    if (!_hasScreen) return;
    _display->oled_command(on ? SH110X_DISPLAYOFF : SH110X_DISPLAYON);
}

void WhiteNodev108::updateDisplay( String left, String right, bool rebuildFull) {
    if (!_hasScreen) return;
    if (rebuildFull || _pageState != -1) {
        _display->clearDisplay();
        _display->setTextSize(1);
        _display->setTextColor(SH110X_WHITE);
        int i = SCREEN_WIDTH - 6 * strlen(machine);
        _display->setCursor(i>0 ? i/2 : 0, 0);
        _display->println(machine);
        _display->setFont(NULL); // Fairly large 5x7 font
        
        if (left.length() || right.length()) {
            _display->setTextColor(SH110X_BLACK);
            _display->drawFastHLine(0,SCREEN_HEIGHT-8*3-1,SCREEN_WIDTH,SH110X_WHITE);
            _display->drawFastHLine(0,SCREEN_HEIGHT-8*2+3,SCREEN_WIDTH,SH110X_WHITE);
        };
        
        if (left.length()) {
            _display->fillRect(0, SCREEN_HEIGHT-8*3+1, 60, 9, SH110X_WHITE);
            _display->setCursor(1,SCREEN_HEIGHT-8*3+2);
            _display->println(left);
        };
        
        if (right.length()) {
            _display->fillRect(SCREEN_WIDTH-60,  SCREEN_HEIGHT-8*3+1, 60, 9, SH110X_WHITE);
            _display->setCursor(SCREEN_WIDTH-right.length()*6,SCREEN_HEIGHT-8*3+2);
            _display->println(right);
        };
        _pageState = -1;
    };
    _display->display();
};

void WhiteNodev108::updateDisplayStateMsg(String msg, int line) {
    if (!_hasScreen) return;
    int y = 16+line*12;
    _display->fillRect(0, y, SCREEN_WIDTH, 12, SH110X_BLACK);
    int i = SCREEN_WIDTH - 6 * msg.length();
    _display->setCursor(i > 0 ? i/2 : 0, y);
    _display->setTextColor(SH110X_WHITE);
    _display->print(msg);
    
    _display->display();
}

void WhiteNodev108::updateInfoDisplay(int page) {
    if (_pageState != page) {
        _display->clearDisplay();
        _display->setTextSize(1);
        _display->setTextColor(SH110X_WHITE);
        _display->setCursor(0, 0);
        _display->println("    -- INPUTS --");
        _display->setFont(NULL); // Fairly large 5x7 font
    };
    
    for (int i = 0; i < 6; i++) {
        state_t * s = &states[i];
        int x =  2 + (i / 3)   * SCREEN_WIDTH / 2;
        int y = 16 + (i % 3) *  (SCREEN_HEIGHT - 16) / 3;
        int val;

#if 0
        if (s->tpe == INPUT_ANALOG)
            val = analogRead(s->pin) > 500 ? 1 : 0;
        else
#endif
            val = !digitalRead(s->pin); // they are all pullup style
        
        if (_pageState != page) {
            _display->drawRect(x, y, 10, 10, SH110X_WHITE);
            _display->setCursor(x + 12 , y + 1);
            _display->print(s->label);
            // pinMode(s->pin, s->tpe);
            s->lst = val;
        };
        
        _display->fillRect(x + 2, y + 2, 10 - 4, 10 - 4, val ? SH110X_WHITE : SH110X_BLACK);
        
        if (val != s->lst) {
            s->lst = val;
        }
    };
    _display->display();
    _pageState = page;
}

void WhiteNodev108::onSwipe(RFID::THandlerFunction_SwipeCB fn) { 
    _reader->onSwipe(fn);
};

void WhiteNodev108::loop() {
    ACNode::loop();
    if (_pageState == 0)
        updateInfoDisplay(0);
}

