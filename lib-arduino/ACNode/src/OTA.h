#ifndef _H_OTA
#define _H_OTA

#include <ArduinoOTA.h>
#include <ACBase.h>
#include "Display/Display.h"
#include "Display/Deck.h"
#include "Machinestate.h"

#ifndef OTA_PORT
#define OTA_PORT (3232)
#endif

class OTA: public ACBase
{
  public:
    virtual const char * name() { return "OTA"; };
    OTA(const char * password);
    void loop();
    void begin();
    void report(JsonObject& report);
  protected:
	const char * _ota_password_hash;
};

class OTAWithDisplay: public ACBase
{
  public:
    OTAWithDisplay(const char * password, Display *d, const char * hostname);
    virtual const char * name() { return "OTAwithDisplay"; };

    typedef std::function<bool(void)> THandlerFunction_ota_ok;
    void setOTAOK(THandlerFunction_ota_ok fn) { _ota_ok_cb = fn; };

    typedef std::function<void(void)> THandlerFunction_wipe_secrets;
    void setPreOTASecretWiper(THandlerFunction_wipe_secrets fn) { _pre_secrets_cb = fn; };

    void loop();
    void begin();
    void report(JsonObject& report);
    
protected:
    const char * _ota_password_hash, * _hostname;
    Display * _display;
    bool _otaOK = true;
    THandlerFunction_ota_ok _ota_ok_cb = NULL;
    THandlerFunction_wipe_secrets _pre_secrets_cb = NULL;
    friend class OTADeck;
};

class OTADeck: public Deck {
public:
    OTADeck(ACNodeBase * node, OTAWithDisplay * ota) : Deck(node), _ota(ota)  {};
    virtual void render_pane(bool refresh);
private:
    const OTAWithDisplay * _ota;
};
#endif


