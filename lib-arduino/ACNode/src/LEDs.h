#pragma once

typedef enum { LED_OFF, LED_FLASH, LED_SLOW, LED_FAST, LED_ON, NEVERSET } LEDstate;
void setLed(int state, const byte pin = AART_LED);

