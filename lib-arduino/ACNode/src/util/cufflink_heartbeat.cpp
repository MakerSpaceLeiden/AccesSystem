#include <Arduino.h>

#include "util/cufflink_heartbeat.h"

// Returns a heart beat like PWM value sequence
//
// Source and credits: https://github.com/adafruit/Adafruit_iCufflinks/tree/master -
// Copy of the first generating on/off button of apple macs.
//
// Intentionally using the beat rather than time - so we can visually see slowdown
// unless we're called very fast.
//
unsigned char hearthbeat()
{
    static const unsigned char cufflink_beat[] = {
        1, 1, 2, 3, 5, 8, 11, 15, 20, 25, 30, 36, 43, 49, 56, 64, 72, 80, 88, 97, 105,
        114, 123, 132, 141, 150, 158, 167, 175, 183, 191, 199, 206, 212, 219, 225, 230,
        235, 240, 244, 247, 250, 252, 253, 254, 255, 254, 253, 252, 250, 247, 244, 240,
        235, 230, 225, 219, 212, 206, 199, 191, 183, 175, 167, 158, 150, 141, 132, 123,
        114, 105, 97, 88, 80, 72, 64, 56, 49, 43, 36, 30, 25, 20, 15, 11, 8, 5, 3, 2, 1,
        0
    };
    static unsigned int i = 50; // Start bright.
    static unsigned long lst = 0;
    
    if (millis() - lst > 5) {
        i++;
        lst = millis();
    };
    
    // List is 0 terminated.
    if (cufflink_beat[i] == 0)
        i = 0;
    
    return cufflink_beat[i]/2;
};

