#include <stddef.h>
#include <functional>

#include <ACNode-private.h>
#include <ACBase.h>



class MachineState : public ACBase {
  public:
    typedef uint8_t machinestates_t;

    static const machinestates_t ALL_STATES = 255;
    static const time_t NEVER = 0;

    typedef std::function<void(machinestates_t currentState)> THandlerFunction_OnLoopCB;
    typedef std::function<void(machinestates_t oldState, machinestates_t newState)> THandlerFunction_OnChangeCB; // in & out
    typedef std::function<void(machinestates_t currentState)> THandlerFunction_OnTimeoutCB;

    static const machinestates_t BOOTING = 1,                  /* Startup state */
                                 OUTOFORDER = 2,               /* device not functional.  */
                                 REBOOT = 3,                   /* forcefull reboot  */
                                 TRANSIENTERROR = 4,           /* hopefully goes away level error  */
                                 NOCONN = 5,                   /* sort of fairly hopless (though we can cache RFIDs!)  */
                                 WAITINGFORCARD = 6,           /* waiting for card. */
                                 CHECKINGCARD = 7;             /* checkeing card. with server */

  private:
    unsigned long laststatechange, lastReport;
    typedef struct {
      const char * label;                   /* name of this state */
      LED::led_state_t ledState;            /* flashing pattern for the aartLED. Zie ook https://wiki.makerspaceleiden.nl/mediawiki/index.php/Powernode_1.1. */
      time_t maxTimeInMilliSeconds;         /* how long we can stay in this state before we timeout. */
      machinestates_t failStateOnTimeout;   /* what state we transition to on timeout. */
      unsigned long timeInState;
      unsigned long timeoutTransitions;
      unsigned long autoReportCycle;

      THandlerFunction_OnLoopCB onLoopCB;
      THandlerFunction_OnChangeCB onChangeCB;
      THandlerFunction_OnTimeoutCB onTimeoutCB;
    } state_t;
    state_t * _state2stateStruct[256];

    typedef  uint8_t machinestate_t;
    machinestate_t machinestate, laststate;

    state_t * _initState(uint8_t state, state_t dflt);
    state_t * _initState(uint8_t state, state_t * dflt);

  public:
    const char * label();
    const char * label(uint8_t label);
    MachineState();

    machinestates_t state();

    void operator=(machinestates_t s);
    void setState(machinestates_t s);

    void setOnLoopCallback(uint8_t state, THandlerFunction_OnLoopCB onLoopCB);

    void setOnChangeCallback(uint8_t state, THandlerFunction_OnChangeCB onChangeCB);

    void setOnTimeoutCallback(uint8_t state, THandlerFunction_OnTimeoutCB onTimeoutCB);

    uint8_t addState(state_t aState);

    uint8_t addState(const char * label, machinestate_t nextstate);

    uint8_t addState(const char * label, time_t timeout, machinestate_t nextstate);

    uint8_t addState(const char * label, LED::led_state_t ledState, 
		time_t timeout, machinestate_t nextstate);

    // ACBase - standard handlers.
    //
    void begin();
    void report(JsonObject& report);
    void loop();
};
