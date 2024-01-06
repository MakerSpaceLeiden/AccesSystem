#include <stddef.h>
#include <functional>

#include <ACNode-private.h>
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
      		.onLoopCB = nullptr,
      		.onChangeCB = nullptr,
      		.onTimeoutCB = nullptr,
		// Internal Bookkeeping
      		.timeInState = 0,
		.stateCnt = 0,
    	};
	return _initState(state, &s);
      }

    MachineState::state_t * MachineState::_initState(uint8_t state, MachineState::state_t * dflt) {
      // state_t *s = heap_caps_malloc(sizeof(state_t), MALLOC_CAP_32BIT | MALLOC_CAP_SPIRAM);
      state_t * s = (state_t *) malloc(sizeof(state_t));
      if (!s) return NULL;

      if (dflt) 
        memcpy(s,dflt,sizeof(state_t)); // *s = *dflt;
      else
        memset(s,0,sizeof(s)); // *s = {};

      _state2stateStruct[state] = s;
      return s;
    }

    const char * MachineState::label()  {
      return label(machinestate);
    }

    const char * MachineState::label(uint8_t state)  {
      if (_state2stateStruct[state] && _state2stateStruct[state] ->label)
        return _state2stateStruct[state]->label;
      return "no-label-set";
    };

    MachineState::machinestate_t MachineState::state() {
      return machinestate;
    }

    void MachineState::setState(machinestate_t s) {
      machinestate = s;
      if (_led) _led->set(ledState());
    }

    void MachineState::operator=(machinestate_t s) {
      machinestate = s;
    }

    void MachineState::setOnLoopCallback(uint8_t state, THandlerFunction_OnLoopCB onLoopCB) {
      state_t *s = _state2stateStruct[state];
      if (s == NULL) s = _initState(state, NULL);
      s->onLoopCB = onLoopCB;
    }

    void MachineState::setOnChangeCallback(uint8_t state, THandlerFunction_OnChangeCB onChangeCB) {
      state_t *s = _state2stateStruct[state];
      if (s == NULL) 
		s = _initState(state, NULL);
      s->onChangeCB = onChangeCB;
    };

    void MachineState::setOnTimeoutCallback(uint8_t state, THandlerFunction_OnTimeoutCB onTimeoutCB) {
      state_t *s = _state2stateStruct[state];
      if (s == NULL) 
		s = _initState(state, NULL);
      s->onTimeoutCB = onTimeoutCB;
    };


    MachineState::machinestate_t MachineState::addState(const char * label, machinestate_t nextstate) {
      return addState((state_t) {
		.label = label,
		.ledState = LED::LED_ERROR,
      		.maxTimeInMilliSeconds = 5 * 1000,
      		.failStateOnTimeout = nextstate,
      		.timeoutTransitions = 0,
      		.autoReportCycle = 0,
      		.onLoopCB = nullptr,
      		.onChangeCB = nullptr,
      		.onTimeoutCB = nullptr,
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
      		.onLoopCB = nullptr,
      		.onChangeCB = nullptr,
      		.onTimeoutCB = nullptr,
      });
    }

    MachineState::machinestate_t MachineState::addState(const char * label, LED::led_state_t ledState, time_t timeout, machinestate_t nextstate) {
	state_t s = {
		.label = label,
		.ledState = ledState,
      		.maxTimeInMilliSeconds = timeout,
      		.failStateOnTimeout = nextstate,
      		.timeoutTransitions = 0,
      		.autoReportCycle = 0,
      		.onLoopCB = nullptr,
      		.onChangeCB = nullptr,
      		.onTimeoutCB = nullptr,
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
      machinestate = BOOTING;
    };

    // ACBase - standard handlers.
    //
    void MachineState::begin() {
      // register reboot 'late' -- so we know we're through as much init complexity
      // and surprises as possible.
      //
      setOnLoopCallback(REBOOT, [](MachineState::machinestate_t s) -> void {
        _acnode->delayedReboot();
      });
      if (_led) _led->set(ledState());
    };

    void MachineState::report(JsonObject& report) {
      report["state"] = label();
      JsonObject tis = report.createNestedObject("seconds_in_state");
      for(int i = 0; i <= 255;i ++)
	if (_state2stateStruct[i])
		tis[ _state2stateStruct[i]->label ] = _state2stateStruct[i]->timeInState + ((i == machinestate) ? (millis() - laststatechange) : 0)/1000;
    }

    void MachineState::loop()
    {
      if (_state2stateStruct[machinestate] == NULL) {
	Log.printf("State %d reached - which us undefind. ignoring.", machinestate);
        return;
      };

      if (laststate != machinestate) {
        Debug.printf("Changed from state <%s> to state <%s>\n", label(laststate), label(machinestate));

        if (_state2stateStruct[machinestate]->onChangeCB)
          _state2stateStruct[machinestate]->onChangeCB(laststate, machinestate);
        else if (_state2stateStruct[ALL_STATES]->onChangeCB)
          _state2stateStruct[ALL_STATES]->onChangeCB(laststate, machinestate);

        if (_state2stateStruct[laststate]) {
        	_state2stateStruct[laststate]->timeInState += (millis() - laststatechange) / 1000;
		_state2stateStruct[laststate]->stateCnt ++;
	};
        laststate = machinestate;
        laststatechange = millis();
        if (_led) _led->set(ledState());
        return;
      };

      if (_state2stateStruct[machinestate]->maxTimeInMilliSeconds != NEVER &&
          (millis() - laststatechange > _state2stateStruct[machinestate]->maxTimeInMilliSeconds))
      {
        _state2stateStruct[machinestate]->timeoutTransitions++;

        if (_state2stateStruct[laststate]->onTimeoutCB)
          _state2stateStruct[laststate]->onTimeoutCB(machinestate);
        else if (_state2stateStruct[ALL_STATES]->onTimeoutCB)
          _state2stateStruct[ALL_STATES]->onTimeoutCB(machinestate);

        laststate = machinestate;
        machinestate = _state2stateStruct[machinestate]->failStateOnTimeout;

        Log.printf("Time-out (%f seconds); transition from %d<%s> to %d<%s>\n",
		_state2stateStruct[laststate]->maxTimeInMilliSeconds/1000.,
		laststate, label(laststate), 
		machinestate, label(machinestate));
        return;
      };

      if (_state2stateStruct[machinestate]->autoReportCycle && \
          millis() - laststatechange > _state2stateStruct[machinestate]->autoReportCycle && \
          millis() - lastReport > _state2stateStruct[machinestate]->autoReportCycle)
      {
        Log.printf("State: %s now for %lu seconds", label(laststate), (millis() - laststatechange) / 1000);
        lastReport = millis();
      };

      if (_state2stateStruct[machinestate]->onLoopCB)
        _state2stateStruct[machinestate]->onLoopCB(machinestate);
      else if (_state2stateStruct[ALL_STATES]->onLoopCB)
        _state2stateStruct[ALL_STATES]->onLoopCB(machinestate);
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

    void MachineState::defineState(uint8_t state,
                const char * label,
                LED::led_state_t ledState,
                time_t timeout,
                machinestate_t nextstate,
                unsigned long timeoutTransitions,
                unsigned long autoReportCycle,
                THandlerFunction_OnLoopCB onLoopCB,
                THandlerFunction_OnChangeCB onChangeCB,
                THandlerFunction_OnTimeoutCB onTimeoutCB
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
                .onLoopCB = onLoopCB,
                .onChangeCB = onChangeCB,
                .onTimeoutCB = onTimeoutCB,
		// internal bookkeeping
                .timeInState = 0,
		.stateCnt = 0
	};
        _initState(state , &aState);
    }

