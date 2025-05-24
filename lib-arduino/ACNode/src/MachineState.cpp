#include <stddef.h>
#include <functional>

#include <ACBaseNode.h>
#include <ACBase.h>
#include <MachineState.h>

MachineState::state_t * MachineState::_initState(uint8_t state, MachineState::state_t dflt) {
    return _initState(state, &dflt);
}

MachineState::state_t * MachineState::_initState(uint8_t state,
                                                 const char * label, LED::led_state_t ledState, time_t timeout, machinestate_t nextstate)
{
    state_t s = {
        .label = label,
        .ledState = ledState,
        .maxTimeInMilliSeconds = timeout,
        .failStateOnTimeout = nextstate,
        .timeoutTransitions = 0,
        .autoReportCycle = 0,
        .safeForOTA = true,
        .onLoopCBs = {},
        .onChangeCBs = {},
        .onTimeoutCBs = {},
        // Internal Bookkeeping
        .timeInState = 0,
        .stateCnt = 0,
    };
    return _initState(state, &s);
}

void MachineState::defineState(uint8_t state,
                               const char * label,
                               LED::led_state_t ledState,
                               time_t timeout,
                               machinestate_t nextstate,
                               unsigned long timeoutTransitions,
                               unsigned long autoReportCycle,
                               std::list<THandlerFunction_OnLoopCB> onLoopCBs,
                               std::list<THandlerFunction_OnChangeCB> onChangeCBs,
                               std::list<THandlerFunction_OnTimeoutCB> onTimeoutCBs
                               ) {
    if (state >=254 || _state2stateStruct[state]) {
        Log.printf("BUG -- inpossible state (%d:%s)\n", state, label);
        return;
    };
    state_t aState = {
        .label = label,
        .ledState = ledState,
        .maxTimeInMilliSeconds = timeout,
        .failStateOnTimeout = nextstate,
        .timeoutTransitions = timeoutTransitions,
        .autoReportCycle = autoReportCycle,
        .onLoopCBs = onLoopCBs,
        .onChangeCBs = onChangeCBs,
        .onTimeoutCBs = onTimeoutCBs,
        // internal bookkeeping
        .timeInState = 0,
        .stateCnt = 0
    };
    _initState(state , &aState);
}


MachineState::machinestate_t MachineState::addState(const char * label, machinestate_t nextstate) {
    return addState((state_t) {
        .label = label,
        .ledState = LED::LED_ERROR,
        .maxTimeInMilliSeconds = 5 * 1000,
        .failStateOnTimeout = nextstate,
        .timeoutTransitions = 0,
        .autoReportCycle = 0,
        .onLoopCBs = {},
        .onChangeCBs = {},
        .onTimeoutCBs = {},
    });
}

MachineState::machinestate_t MachineState::addState(const char * label, time_t timeout, machinestate_t nextstate) {
    return addState((state_t) {
        .label = label,
        .ledState = LED::LED_ERROR,
        .maxTimeInMilliSeconds = timeout,
        .failStateOnTimeout = nextstate,
        .timeoutTransitions = 0,
        .autoReportCycle = 0,
        .onLoopCBs = {},
        .onChangeCBs = {},
        .onTimeoutCBs = {},
    });
}

MachineState::machinestate_t MachineState::addState(const char * label, LED::led_state_t ledState, time_t timeout, machinestate_t nextstate, bool isSafeForOTA) {
    state_t s = {
        .label = label,
        .ledState = ledState,
        .maxTimeInMilliSeconds = timeout,
        .failStateOnTimeout = nextstate,
        .timeoutTransitions = 0,
        .autoReportCycle = 0,
        .safeForOTA = isSafeForOTA,
        .onLoopCBs = {},
        .onChangeCBs = {},
        .onTimeoutCBs = {},
    };
    return addState(s);
}

MachineState::machinestate_t MachineState::addState(state_t aState) {
    for (uint8_t i = 0; i < 255; i++)
        if (_state2stateStruct[i] == NULL) {
            _initState(i, &aState);
            return i;
        };
    Log.println("BUG -- More than 254 active states ?");
    return 255;
};


MachineState::state_t * MachineState::_initState(uint8_t state, MachineState::state_t * dflt) {
    // state_t *s = heap_caps_malloc(sizeof(state_t), MALLOC_CAP_32BIT | MALLOC_CAP_SPIRAM);
    state_t * s = (state_t *) malloc(sizeof(state_t));
    if (!s)
        return NULL;
    
    if (dflt)
        memcpy(s,dflt,sizeof(state_t)); // *s = *dflt;
    else
        memset(s,0,sizeof(s)); // *s = {};
    
    if (s->label) s->label = strdup(s->label);
    
    _state2stateStruct[state] = s;
    // Serial.printf("State: %d - %p - %s\n", state, _state2stateStruct[state], s->label ? s->label : "????");
    return s;
}

const char * MachineState::label()  {
    return label(machinestate);
}

bool MachineState::safeForOTA() { return _state2stateStruct[machinestate]->safeForOTA;};

const char * MachineState::label(uint8_t state)  {
    if (_state2stateStruct[state] && _state2stateStruct[state] ->label)
        return _state2stateStruct[state]->label;
    return "no-label-set";
};

MachineState::machinestate_t MachineState::state() {
    return machinestate;
}

void MachineState::setState(machinestate_t s) {
    Log.printf("MachineState:setState; %s -> %s\n", label(machinestate), label(s));
    newstate = s;
    if (_led) _led->set(ledState());
}

void MachineState::operator=(machinestate_t s) {
    setState(s);
}

void MachineState::addOnLoopCallback(uint8_t state, THandlerFunction_OnLoopCB onLoopCB) {
    state_t *s = _state2stateStruct[state];
    if (s == NULL) s = _initState(state, NULL);
    s->onLoopCBs.push_back(onLoopCB);
}

void MachineState::addOnChangeCallback(uint8_t state, THandlerFunction_OnChangeCB onChangeCB) {
    state_t *s = _state2stateStruct[state];
    if (s == NULL)
        s = _initState(state, NULL);
    s->onChangeCBs.push_back(onChangeCB);
};

void MachineState::addOnTimeoutCallback(uint8_t state, THandlerFunction_OnTimeoutCB onTimeoutCB) {
    state_t *s = _state2stateStruct[state];
    if (s == NULL)
        s = _initState(state, NULL);
    s->onTimeoutCBs.push_back(onTimeoutCB);
};

MachineState::MachineState(LED * led) {
    _led = led;
    for(int i = 0; i < 256; i++)
        _state2stateStruct[i] = NULL;
    
    _initState(WAITINGFORCARD,"Waiting for card",     LED::LED_IDLE,         NEVER, WAITINGFORCARD );
    _initState(REBOOT, 	"Rebooting",            LED::LED_ERROR,   120 * 1000, REBOOT         );
    _initState(BOOTING, 	"Booting",              LED::LED_ERROR,   120 * 1000, REBOOT         );
    _initState(OUTOFORDER, 	"Out of order",         LED::LED_ERROR,   120 * 1000, REBOOT         );
    _initState(TRANSIENTERROR,"Transient Error",      LED::LED_ERROR,     5 * 1000, WAITINGFORCARD );
    _initState(NOCONN, 	"No network",           LED::LED_FLASH,        NEVER, NOCONN         );
    _initState(CHECKINGCARD, 	"Checking card...",     LED::LED_IDLE,     10 * 1000, WAITINGFORCARD );
    _initState(REJECTED, 	"Card rejected",        LED::LED_ERROR,     2 * 1000, WAITINGFORCARD );
    _initState(ALL_STATES, 	"<default>",            LED::LED_IDLE,         NEVER, ALL_STATES     );
    
    laststate = OUTOFORDER;
    newstate = machinestate = BOOTING;
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
    machinestate = newstate;
    
    if (_state2stateStruct[machinestate] == NULL) {
        Log.printf("State %d reached - which is undefind. ignoring.", machinestate);
        return;
    };
    
    if (laststate != machinestate) {
        Debug.printf("Changed from state <%s> to state <%s>\n", label(laststate), label(machinestate));
        
        std::list<THandlerFunction_OnChangeCB> cbs;
        
        cbs = _state2stateStruct[machinestate]->onChangeCBs;
        for (auto it = cbs.begin(); it!=cbs.end(); ++it)
            (*it)(laststate, machinestate);
        
        cbs = _state2stateStruct[ALL_STATES]->onChangeCBs;
        for (auto it = cbs.begin(); it!=cbs.end(); ++it)
            (*it)(laststate, machinestate);
    
        if (_state2stateStruct[laststate]) {
            _state2stateStruct[laststate]->timeInState += (millis() - laststatechange) / 1000;
            _state2stateStruct[laststate]->stateCnt ++;
        };
        if (_led) _led->set(ledState());

        laststate = machinestate;
        laststatechange = millis();
        return;
    };
    
    if (_state2stateStruct[machinestate]->maxTimeInMilliSeconds != NEVER &&
        (millis() - laststatechange > _state2stateStruct[machinestate]->maxTimeInMilliSeconds))
    {
        _state2stateStruct[machinestate]->timeoutTransitions++;
        
        std::list<THandlerFunction_OnTimeoutCB> cbs;
        
        cbs = _state2stateStruct[laststate]->onTimeoutCBs;
        for (auto it = cbs.begin(); it!=cbs.end(); ++it)
            (*it)(machinestate);

        cbs = _state2stateStruct[ALL_STATES]->onTimeoutCBs;
        for (auto it = cbs.begin(); it!=cbs.end(); ++it)
            (*it)(machinestate);
        
        laststate = machinestate;
        newstate = _state2stateStruct[machinestate]->failStateOnTimeout;
        
        Debug.printf("Time-out (%f seconds); will transition from %d<%s> to %d<%s>\n",
                     _state2stateStruct[laststate]->maxTimeInMilliSeconds/1000.,
                     laststate, label(laststate),
                     newstate, label(newstate));
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


