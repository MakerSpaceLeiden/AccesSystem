#pragma once
typedef enum { LED_OFF, LED_FLASH, LED_SLOW, LED_FAST, LED_ON, NEVERSET } LEDstate;


// Note: we're using an int rather than above LEDstate -- as the latter
// is not liked by Arduino its magic 'header detect' logic (1.5).
//
void setGreenLED(int state);
void setOrangeLED(int state);
