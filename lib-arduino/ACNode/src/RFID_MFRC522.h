#ifndef _H_RFID_MRC522
#define _H_RFID_MRC522

#include <stddef.h>
#include <functional>

#include <MFRC522.h>
#include "RFID.h"

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

class RFID_MFRC522 : public RFID {
  public:
    const char * name() { return "RFID-MFRC522"; }
    
    RFID_MFRC522(const byte sspin = RFID_SELECT_PIN, const byte rstpin = RFID_RESET_PIN, const byte irqpin = RFID_IRQ_PIN, 
	 const byte spiclk = RFID_CLK_PIN, const byte spimiso = RFID_MISO_PIN, const byte spimosi = RFID_MOSI_PIN
    );
    RFID_MFRC522(TwoWire *i2cBus, const byte i2caddr, const byte rstpin = RFID_RESET_PIN, const byte irqpin = RFID_IRQ_PIN);
    ~RFID_MFRC522();

    void begin();
    void loop();

  private:
    MFRC522_SPI * _spiDevice;
    MFRC522_I2C * _i2cDevice;
    MFRC522 * _mfrc522;
};
#endif
