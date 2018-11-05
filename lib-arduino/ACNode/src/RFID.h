#ifndef _H_RFID
#define _H_RFID

#include <stddef.h>
#include <functional>

#include <ACNode-private.h>
#include <ACBase.h>
#include <MFRC522.h>

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

class RFID : public ACBase {
  public:
    const char * name() { return "RFID"; }
    
    RFID(const byte sspin = RFID_SELECT_PIN, const byte rstpin = RFID_RESET_PIN, const byte irqpin = RFID_IRQ_PIN, 
	 const byte spiclk = RFID_CLK_PIN, const byte spimiso = RFID_MISO_PIN, const byte spimosi = RFID_MOSI_PIN
    );

    void begin();
    void loop();

    typedef std::function<ACBase::cmd_result_t(const char *)> THandlerFunction_SwipeCB;

    RFID& onSwipe(THandlerFunction_SwipeCB fn) 
	{ _swipe_cb = fn; return *this; };
  
  private:
    bool _irqMode = false;
    MFRC522_SPI * _mfrc522 = NULL;
    THandlerFunction_SwipeCB _swipe_cb = NULL;

    char lasttag[MAX_TAG_LEN * 4];      // Up to a 3 digit byte and a dash or terminating \0. */
    unsigned long lastswipe;
};
#endif
