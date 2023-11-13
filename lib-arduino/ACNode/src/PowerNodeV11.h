#include "ACNode.h"
#ifndef _H_POWERNODEV11
#define _H_POWERNODEV11

// https://wiki.makerspaceleiden.nl/mediawiki/index.php/Powernode_1.1
//
#include <WiredEthernet.h>

#ifndef CURRENT_GPIO
#define CURRENT_GPIO    (36)  // Analog in - current
#endif

#ifndef RELAY_GPIO
#define RELAY_GPIO      ( 5)  // output
#endif

#ifndef TRIAC_GPIO
#define TRIAC_GPIO      ( 4)  // output
#endif

#ifndef AART_LED
#define AART_LED        (16)  // superfluous indicator LED.
#endif

#ifndef SW1_BUTTON
#define SW1_BUTTON      (02)
#endif

#ifndef SW2_BUTTON
#define SW2_BUTTON      (39)
#endif

#ifndef OPTO1
#define OPTO1           (34)
#endif

#ifndef OPTO2
#define OPTO2           (35)
#endif

// SPI based RFID reader
#ifndef RFID_MOSI_PIN
#define RFID_MOSI_PIN   (13)
#endif

#ifndef RFID_MISO_PIN
#define RFID_MISO_PIN   (12)
#endif

#ifndef RFID_CLK_PIN
#define RFID_CLK_PIN    (14)
#endif

#ifndef RFID_SELECT_PIN
#define RFID_SELECT_PIN (15)
#endif

#ifndef RFID_RESET_PIN
#define RFID_RESET_PIN  (32)
#endif

#ifndef RFID_IRQ_PIN
#define RFID_IRQ_PIN    (33) // Set to -1 to switch to polling mode; 33 to use IRQs
#endif

class PowerNodeV11: public ACNode {
  public: 
	void begin();
};
#endif
