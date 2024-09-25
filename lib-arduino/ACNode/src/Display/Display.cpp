#include <qrcode.h> // Part of the ESP32 package

#include "Display/Display.h"
#include "Display/msl-logo.h"

void Display::begin(uint8_t SCREEN_Address, bool reset, const char * bootmsg) {
    if (!super::begin(SCREEN_Address,reset)) {
        Log.println("Could not initialize the LCD/OLED screen.");
        return;
    }
    // Should we capture that the screen actually works; and make
    // the methods condition on a 'workie' variable ?
    clearDisplay();
    drawCentredBitmap(msl_logo,msl_logo_width,msl_logo_height,SH110X_WHITE);
    if (bootmsg) {
        setCursor(0,0);
        setFont(FONT_SMALL);
        setTextSize(1);
        setTextColor(SH110X_WHITE);
        print_centred((char*)bootmsg, false);
    };
    display();
}

void Display::drawCentredBitmap(const unsigned char * bitmap, unsigned short w, unsigned short h, unsigned char col) {
    drawBitmap((SCREEN_WIDTH-w)/2,(SCREEN_HEIGHT-h)/2,bitmap,w,h,col);
}

void Display::setDisplayScreensaver(bool on) {
    oled_command(on ? SH110X_DISPLAYOFF : SH110X_DISPLAYON);
}

#define getBBX(str,w,h) uint16_t w,h; { int16_t x,y; getTextBounds(str,0,0,&x,&y,&w,&h); }

void Display::updateDisplay(const char * title, String left, String right, bool rebuildFull) {
    if (rebuildFull) {
        clearDisplay();
        setTextSize(1);
        setFont(FONT_LARGE);
        setTextColor(SH110X_WHITE);

        getBBX(title,w,h);
        setCursor((SCREEN_WIDTH - w)/2,h);
        println(title);

        setFont(FONT_MEDIUM);        
        if (left.length() || right.length()) {
            const uint16_t WBOX = 60;
            setTextColor(SH110X_BLACK);
            drawFastHLine(0,SCREEN_HEIGHT-8-4,SCREEN_WIDTH,SH110X_WHITE);
            drawFastHLine(0,SCREEN_HEIGHT-1,SCREEN_WIDTH,SH110X_WHITE);
            
            if (left.length()) {
                getBBX(left,w,h);
                fillRect(0, SCREEN_HEIGHT-8-2, WBOX, 8, SH110X_WHITE);
                setCursor((WBOX-w)/2,SCREEN_HEIGHT-h+3);  // We assume these to be always uppercase; hence the h+3
                println(left);
            };
            
            if (right.length()) {
                getBBX(right,w,h);
                fillRect(SCREEN_WIDTH-WBOX,  SCREEN_HEIGHT-8-2, 60, 8, SH110X_WHITE);
                setCursor(SCREEN_WIDTH-WBOX+(WBOX-w)/2,SCREEN_HEIGHT-h+3); // We assume these to be always uppercase; hence the h+3
                println(right);
            };
        };
        // Uncommet for layout checks
        // drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SH110X_WHITE);
        display();
    };
};



void Display::updateDisplayProgressbar(unsigned int percentage, bool rebuildFull) {
    int y = SCREEN_HEIGHT-16;
    int l = (SCREEN_WIDTH-4)*percentage / 100.;
    
    if (rebuildFull){
        fillRect(0, y, SCREEN_WIDTH, 20, SH110X_BLACK);
        drawRect(0, y, SCREEN_WIDTH, 12, SH110X_WHITE);
    };
    
    fillRect(0+2, y+2, l, 12-4, SH110X_WHITE);
    display();
}

void Display::updateDisplayStateMsg(String msg, int line) {
    int16_t x,y;
    uint16_t w,h;
    getTextBounds(msg,0,0,&x,&y,&w,&h);

    y = 16+line*12;
    fillRect(0, y, SCREEN_WIDTH, 11, SH110X_BLACK);
    
    int i = SCREEN_WIDTH - 6 * msg.length();
    setCursor(i > 0 ? i/2 : 0, y);
    setTextColor(SH110X_WHITE);
    print(msg);
    
    display();
}

void Display::print_centred(char * title, bool titlelines) {
    int16_t x,y;
    uint16_t w,h;
    int16_t cy = getCursorY();
    getTextBounds(title,0,0,&x,&y,&w,&h);
    if (titlelines) {
        int16_t l = (SCREEN_WIDTH-w)/2 - 2;
        int16_t r = (SCREEN_WIDTH+w)/2 + 2 + 2;
        
        if (l>2)
            drawFastHLine(2,cy + h / 2, l-4, SH110X_WHITE);
        
        if (r<SCREEN_WIDTH-2)
            drawFastHLine(r+2,cy + h / 2, SCREEN_WIDTH-r - 4 , SH110X_WHITE);
        
    };
    setCursor((SCREEN_WIDTH-w)/2, cy);
    print(title);
    print("\n");
};

static Display * _d;
void Display::print_centered_QR(char * titleOrNull, char * url) {
    _d = this;
    esp_qrcode_config_t qrc = {
        .display_func = ([](esp_qrcode_handle_t qrcode)->void{
            int s = esp_qrcode_get_size(qrcode);
            int p = 1;
            while ((s*(p+1) <= _d->SCREEN_WIDTH) && (s*(p+1) <= (_d->SCREEN_HEIGHT))) p++;
            int ox = (_d->SCREEN_WIDTH - p*s)/2;
            // We cannot pass anything to this lambda; as it maps to C, rather than c++.
            // So we use the state of the cursor to dected an empty title.
            //
            int oy = _d->getCursorY() ? (_d->SCREEN_HEIGHT - p*s -1) : (_d->SCREEN_HEIGHT - p*s)/2;
            for (int y = 0; y < s; y++)
                for (int x = 0; x < s; x++)
                    if (p == 1)
                        _d->drawPixel(ox+p*x,oy+p*y, esp_qrcode_get_module(qrcode, x, y) ? SH110X_WHITE : SH110X_BLACK);
                    else
                        _d->fillRect(ox+p*x,oy+p*y,p,p,esp_qrcode_get_module(qrcode, x, y) ? SH110X_WHITE : SH110X_BLACK);
        }),
            .max_qrcode_version = 40,
            .qrcode_ecc_level = 1,
    };
    // Make sure above getCursorY() returns zero if there is no title.
    setCursor(0, 0);
    if (titleOrNull) {
        print_centred(titleOrNull);
    };
    esp_qrcode_generate(&qrc,url);
    Log.printf("Showing QR %s%s with text: <%s>\n", 
               titleOrNull ? titleOrNull : "",
               titleOrNull ? "" : " ", 
               url);
}
