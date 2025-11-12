#include <stddef.h>
#include <functional>

#include <ACBaseNode.h>
#include <ACBase.h>
#include <MachineState.h>

#include <assert.h>

#define X { Serial.printf("%s:%d - %s\n",__FILE__,__LINE__,__PRETTY_FUNCTION__); }

MachineState::machinestate_t MachineState::defState(machinestate_t i, const char * label, LED::led_state_t ledState, time_t timeout, machinestate_t nextstate, bool isSafeForOTA, bool backgroundTaskOk) {
    assert(_state2stateStruct[i]==NULL);

    _state2stateStruct[i] = new AMState();
    _state2stateStruct[i]->label = label;
    _state2stateStruct[i]->ledState= ledState;
    _state2stateStruct[i]->maxTimeInMilliSeconds = timeout;
    _state2stateStruct[i]->failStateOnTimeout = nextstate;
    _state2stateStruct[i]->safeForOTA = isSafeForOTA;
    _state2stateStruct[i]->backgroundTaskOk = backgroundTaskOk;

    return i;
};

MachineState::machinestate_t MachineState::addState(const char * label, machinestate_t nextstate) {
    return addState(label, LED::LED_ERROR,5 * 1000, nextstate, false);
}

MachineState::machinestate_t MachineState::addState(const char * label, time_t timeout, machinestate_t nextstate) {
    return addState(label, LED::LED_ERROR,timeout, nextstate, false);
}

MachineState::machinestate_t MachineState::addState(const char * label, LED::led_state_t ledState, time_t timeout, machinestate_t nextstate, bool isSafeForOTA,  bool backgroundTaskOk) {
    for (uint8_t i = 0; i < 255; i++)
        if (_state2stateStruct[i]==NULL)
	     return defState(i,label,ledState,timeout,nextstate,isSafeForOTA,backgroundTaskOk);
    assert(NULL == "BUG -- More than 254 active states ?");
    return 255;
};

const char * MachineState::label()  {
    return label(machinestate);
}

bool MachineState::safeForOTA() { return _state2stateStruct[machinestate]->safeForOTA;};
bool MachineState::backgroundTaskOk() { return _state2stateStruct[machinestate]->backgroundTaskOk;};
bool MachineState::isStable() { return lastloopstate == machinestate; };

const char * MachineState::label(uint8_t state)  {
    if (_state2stateStruct[state] && _state2stateStruct[state] ->label)
        return _state2stateStruct[state]->label;
    return "no-label-set";
};

MachineState::machinestate_t MachineState::state() {
    return machinestate;
}

void MachineState::setState(machinestate_t newstate) {
    Log.printf("MachineState:setState; %s(%d) -> %s(%d)\n", label(machinestate), machinestate, label(newstate), newstate);

    if (machinestate == newstate && machinestate != BOOTING) {
	Log.println("*BUG* no change in state; ignored.");
	return;
    };

    laststate = machinestate;
    machinestate = newstate;

    if (_led) 
	_led->set(ledState());
    
    if (_state2stateStruct[laststate]) {
        _state2stateStruct[laststate]->timeInState += (millis() - laststatechange) / 1000;
        _state2stateStruct[laststate]->stateCnt ++;
    };
    laststatechange = millis();

    if (_state2stateStruct[machinestate] == NULL) {
        Log.printf("State %d reached - which is undefind. ignoring.", machinestate);
        return;
    };
    
    std::list<THandlerFunction_OnChangeCB> cbs;

    cbs = _state2stateStruct[machinestate]->onChangeCBs;
    for (auto it = cbs.begin(); it!=cbs.end(); ++it) {
        (*it)(laststate, machinestate);
    };
 
    cbs = _state2stateStruct[ALL_STATES]->onChangeCBs;
    for (auto it = cbs.begin(); it!=cbs.end(); ++it)
        (*it)(laststate, machinestate);
}

void MachineState::operator=(machinestate_t s) {
    setState(s);
}

void MachineState::addOnLoopCallback(uint8_t state, THandlerFunction_OnLoopCB onLoopCB) {
    AMState *s = _state2stateStruct[state];
    assert(s);
    s->onLoopCBs.push_back(onLoopCB);
}

void MachineState::addOnChangeCallback(uint8_t state, THandlerFunction_OnChangeCB onChangeCB) {
    AMState *s = _state2stateStruct[state];
    if (s) s->onChangeCBs.push_back(onChangeCB);
};

void MachineState::addOnTimeoutCallback(uint8_t state, THandlerFunction_OnTimeoutCB onTimeoutCB) {
    AMState *s = _state2stateStruct[state];
    assert(s);
    s->onTimeoutCBs.push_back(onTimeoutCB);
};

MachineState::MachineState(LED * led) {
    _led = led;
    for(int i = 0; i < 256; i++)
        _state2stateStruct[i] = NULL;
    
    defState(WAITINGFORCARD,"Waiting for card",     LED::LED_IDLE,         NEVER, WAITINGFORCARD, true, true );
    defState(REBOOT, 	"Rebooting",            LED::LED_ERROR,   120 * 1000, REBOOT         );
    defState(BOOTING, 	"Booting",              LED::LED_ERROR,   120 * 1000, REBOOT         );
    defState(OUTOFORDER, 	"Out of order",         LED::LED_ERROR,   120 * 1000, REBOOT, true, true );
    defState(TRANSIENTERROR,"Transient Error",      LED::LED_ERROR,     5 * 1000, WAITINGFORCARD, true, false );
    defState(NOCONN, 	"No network",           LED::LED_FLASH,        NEVER, NOCONN, true, true);
    defState(CHECKINGCARD, 	"Checking card...",     LED::LED_IDLE,     10 * 1000, WAITINGFORCARD, true, true );
    defState(REJECTED, 	"Sorry!",        LED::LED_ERROR,     2 * 1000, WAITINGFORCARD, false, true );
    defState(ALL_STATES, 	"<default>",            LED::LED_IDLE,         NEVER, ALL_STATES     );
    
    laststate = OUTOFORDER;
    machinestate = BOOTING;
};

// ACBase - standard handlers.
//
void MachineState::begin() {
    // register reboot 'late' -- so we know we're through as much init complexity
    // and surprises as possible.
    //
    addOnLoopCallback(REBOOT, [](MachineState::machinestate_t s) -> void {
        _acnodebase->delayedReboot();
    });
    if (_led) _led->set(ledState());
    // Debug.println(__PRETTY_FUNCTION__);
};

void MachineState::report(JsonObject& report) {
    report["state"] = label();
    JsonObject tis = report["seconds_in_state"].add<JsonObject>();
    for(int i = 0; i <= 255;i ++)
        if (_state2stateStruct[i])
            tis[ _state2stateStruct[i]->label ] = _state2stateStruct[i]->timeInState + ((i == machinestate) ? (millis() - laststatechange) : 0)/1000;
}

void MachineState::loop()
{
    // Debug.println(__PRETTY_FUNCTION__);
    if (_state2stateStruct[machinestate]->maxTimeInMilliSeconds != NEVER &&
        (millis() - laststatechange > _state2stateStruct[machinestate]->maxTimeInMilliSeconds))
    {
        _state2stateStruct[machinestate]->timeoutTransitions++;

        machinestate_t newstate = _state2stateStruct[machinestate]->failStateOnTimeout; 

        Debug.printf("Time-out (%f seconds); will transition from %d<%s> to %d<%s>\n",
                     _state2stateStruct[machinestate]->maxTimeInMilliSeconds/1000.,
                     machinestate, label(machinestate),
                     newstate, label(newstate)
	);
        
        std::list<THandlerFunction_OnTimeoutCB> cbs;
        cbs = _state2stateStruct[laststate]->onTimeoutCBs;
        for (auto it = cbs.begin(); it!=cbs.end(); ++it)
            (*it)(newstate);

        cbs = _state2stateStruct[ALL_STATES]->onTimeoutCBs;
        for (auto it = cbs.begin(); it!=cbs.end(); ++it)
            (*it)(newstate);
       
        setState(newstate);
        return;
    };
    
    if (_state2stateStruct[machinestate]->autoReportCycle && \
        millis() - laststatechange > _state2stateStruct[machinestate]->autoReportCycle && \
        millis() - lastReport > _state2stateStruct[machinestate]->autoReportCycle)
    {
        Debug.printf("State: %s now for %lu seconds", label(laststate), (millis() - laststatechange) / 1000);
        lastReport = millis();
    };
    
    std::list<THandlerFunction_OnLoopCB> cbs;

    cbs = _state2stateStruct[machinestate]->onLoopCBs;
    for (auto it = cbs.begin(); it!=cbs.end(); ++it)
        (*it)(machinestate);

    cbs = _state2stateStruct[ALL_STATES]->onLoopCBs;
    for (auto it = cbs.begin(); it!=cbs.end(); ++it)
        (*it)(machinestate);
 
    lastloopstate = machinestate;
};

time_t MachineState::secondsInThisState() {
    unsigned long d = millis() - laststatechange;
    return d / 1000;
};

time_t MachineState::secondsLeftInThisState() {
    if (_state2stateStruct[machinestate]->maxTimeInMilliSeconds == NEVER)
        return 0;
    
    unsigned long d = millis() - laststatechange;
    d = _state2stateStruct[machinestate]->maxTimeInMilliSeconds - d;
    return d / 1000;
};

String MachineState::timeLeftInThisState() {
    char buff[48];
    time_t d = secondsLeftInThisState();
    if (d == 0) return "";
    int s = d % 60; d /= 60;
    int m = d % 60; d /= 60;
    int h = d;
    if (h) snprintf(buff,sizeof(buff),"%02d:%02d",h,m);
    else if (m > 5) snprintf(buff,sizeof(buff),"%02d:%02d",m,s);
    else snprintf(buff,sizeof(buff),"%d",m*60+s);
    return String(buff);
};


