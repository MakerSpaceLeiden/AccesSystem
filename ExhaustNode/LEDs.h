#pragma once

typedef enum { LED_OFF, LED_FLASH, LED_SLOW, LED_FAST, LED_ON, NEVERSET } LEDstate;

void setGreenLED(int state);
void setOrangeLED(int state);
void setRedLED(int state);

