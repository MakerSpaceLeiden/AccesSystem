#ifndef _H_OTA
#define _H_OTA

#include <ArduinoOTA.h>
#include <ACBase.h>
#include "Display/Display.h"
#include "Display/Deck.h"
#include "MachineState.h"

#ifndef OTA_PORT
#define OTA_PORT (3232)
#endif

class OTA: public ACBase
{
  public:
    OTA(const char * password);
    virtual const char * name() { return "OTA"; };

    void loop();
    void begin();
    void report(JsonObject & report);
    // void status(JsonObject & report);
    const char * passwdType();
  protected:
    const char * _ota_password_hash = NULL;
    char _ota_masked_password_hash[10] = "not-set";
};

class OTAWithDisplay: public OTA
{
  public:
    OTAWithDisplay(const char * password, Display *d, const char * hostname);
    virtual const char * name() { return "OTAWithDisplay"; };

    typedef std::function<bool(void)> THandlerFunction_ota_ok;
    void setOTAOK(THandlerFunction_ota_ok fn) { _ota_ok_cb = fn; };

    typedef std::function<void(void)> THandlerFunction_wipe_secrets;
    void setPreOTASecretWiper(THandlerFunction_wipe_secrets fn) { _pre_secrets_cb = fn; };

    void begin();
private:
    Display * _display;
    const char * _hostname;
    bool _otaOK = true;
    THandlerFunction_ota_ok _ota_ok_cb = NULL;
    THandlerFunction_wipe_secrets _pre_secrets_cb = NULL;
    friend class OTADeck;
};

class OTADeck: public Deck {
public:
    OTADeck(ACNodeBase * node, OTAWithDisplay * ota) : Deck(node), _ota(ota) {};
    virtual void render_pane(bool refresh);
private:
    const OTAWithDisplay * _ota;
};
#endif
