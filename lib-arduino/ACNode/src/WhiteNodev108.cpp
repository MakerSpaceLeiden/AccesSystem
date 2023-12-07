#include "WhiteNodev108.h"
#include "msl-logo.h"

void WhiteNodev108::begin(bool hasScreen) {

        // Non standard pins for i2c.
        Wire.begin(I2C_SDA, I2C_SCL);

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

        };

        // All nodes have a build-in RFID reader; so fine to hardcode this.
        //
        _reader = new RFID_MFRC522(&Wire, RFID_ADDR, RFID_RESET, RFID_IRQ);
        addHandler(_reader);

        ETH.begin(ETH_PHY_ADDR, ETH_PHY_POWER, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_RTL8201, ETH_CLOCK_GPIO17_OUT);

        ACNode::begin(BOARD_NG);
	updateDisplay("","MORE", true);
}

void WhiteNodev108::updateDisplay( String left, String right, bool rebuildFull) {
	if (!_hasScreen) return;
        if (rebuildFull) {
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

	};
  	_display->display();
};

void WhiteNodev108::updateDisplayStateMsg(String msg) {
    _display->fillRect(0, 16, SCREEN_WIDTH, 16, SH110X_BLACK);
    int i = SCREEN_WIDTH - 6 * msg.length();
    _display->setCursor(i > 0 ? i/2 : 0, 16);
    _display->setTextColor(SH110X_WHITE);
    _display->print(msg);

    _display->display();
}

void WhiteNodev108::loop() {
        ACNode::loop();
}

