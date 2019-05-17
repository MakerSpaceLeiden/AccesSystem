#include <stddef.h>
#include <functional>

#include <ACNode-private.h>
#include <ACBase.h>
#include <MachineState.h>

    MachineState::state_t * MachineState::_initState(uint8_t state, MachineState::state_t dflt) {
      return _initState(state, &dflt);
    }

    MachineState::state_t * MachineState::_initState(uint8_t state, MachineState::state_t * dflt) {
      // state_t *s = heap_caps_malloc(sizeof(state_t), MALLOC_CAP_32BIT | MALLOC_CAP_SPIRAM);

      state_t * s = (state_t *) malloc(sizeof(state_t));

      if (dflt)
        bcopy(dflt, s, sizeof(state_t));
      else
        bzero(s, sizeof(state_t));

      _state2stateStruct[state] = s;
      return s;
    }

    const char * MachineState::label()  {
      if (_state2stateStruct[machinestate])
        return _state2stateStruct[machinestate]->label;
      return "none";
    }

    MachineState::machinestates_t MachineState::state() {
      return machinestate;
    }

    void MachineState::setState(machinestates_t s) {
      machinestate = s;
    }

    void MachineState::operator=(machinestates_t s) {
      Debug.printf("State change %d -> %d\n", machinestate, s);
      machinestate = s;
    }

    void MachineState::setOnLoopCallback(uint8_t state, THandlerFunction_OnLoopCB onLoopCB) {
      state_t *s = _state2stateStruct[state];
      if (s == NULL) s = _initState(state, NULL);
      s->onLoopCB = onLoopCB;
    }

    void MachineState::setOnChangeCallback(uint8_t state, THandlerFunction_OnChangeCB onChangeCB) {
      state_t *s = _state2stateStruct[state];
      if (s == NULL) s = _initState(state, NULL);
      s->onChangeCB = onChangeCB;
    };

    void MachineState::setOnTimeoutCallback(uint8_t state, THandlerFunction_OnTimeoutCB onTimeoutCB) {
      state_t *s = _state2stateStruct[state];
      if (s == NULL) s = _initState(state, NULL);
      s->onTimeoutCB = onTimeoutCB;
    };


    uint8_t MachineState::addState(const char * label, machinestate_t nextstate) {
      return addState((state_t) {
        label, LED::LED_ERROR, 5 * 10000, nextstate, 0, 0, 0, NULL, NULL, NULL
      });
    }

    uint8_t MachineState::addState(const char * label, time_t timeout, machinestate_t nextstate) {
      return addState((state_t) {
        label, LED::LED_ERROR, timeout, nextstate, 0, 0, 0, NULL, NULL, NULL
      });
    }

    uint8_t MachineState::addState(const char * label, LED::led_state_t ledState, time_t timeout, machinestate_t nextstate) {
      return addState((state_t) {
        label, ledState, timeout, nextstate, 0, 0, 0, NULL, NULL, NULL
      });
    }

    MachineState::MachineState() {
      _initState(WAITINGFORCARD,
      (state_t) {
        "Waiting for card",     LED::LED_IDLE,                 NEVER, WAITINGFORCARD , 0,             0, 0,
        NULL, NULL, NULL
      });
      _initState(REBOOT,
      (state_t) {
        "Rebooting",            LED::LED_ERROR,           120 * 1000, REBOOT,         0,             0, 0,
        NULL, NULL, NULL
      });
      _initState(BOOTING,
      (state_t) {
        "Booting",              LED::LED_ERROR,           120 * 1000, REBOOT,         0 ,            0, 0,
        NULL, NULL, NULL
      });
      _initState(OUTOFORDER,
      (state_t) {
        "Out of order",         LED::LED_ERROR,           120 * 1000, REBOOT,         5 * 60 * 1000, 0, 0,
        NULL, NULL, NULL
      });
      _initState(TRANSIENTERROR,
      (state_t) {
        "Transient Error",      LED::LED_ERROR,             5 * 1000, WAITINGFORCARD, 5 * 60 * 1000, 0, 0,
        NULL, NULL, NULL
      });
      _initState(NOCONN,
      (state_t) {
        "No network",           LED::LED_FLASH,                NEVER, NOCONN,         0,             0, 0,
        NULL, NULL, NULL
      });
      _initState(CHECKINGCARD,
      (state_t) {
        "Checking card...",             LED::LED_IDLE,             10 * 1000, WAITINGFORCARD, 0,             0, 0,
        NULL, NULL, NULL
      });

      laststate = OUTOFORDER;
      machinestate = BOOTING;
    };

    // ACBase - standard handlers.
    //
    void MachineState::begin() {
      // register reboot 'late' -- so we know we're through as much init complexity
      // and surprises as possible.
      //
      setOnLoopCallback(REBOOT, [](MachineState::machinestates_t s) -> void {
        _acnode->delayedReboot();
      });
    };

    void MachineState::report(JsonObject& report) {
      report["state"] = label();
    }

    void MachineState::loop()
    {
      if (laststate != machinestate) {
        Debug.printf("Changed from state <%s> to state <%s>\n",
                     _state2stateStruct[laststate]->label, _state2stateStruct[machinestate]->label);

        if (_state2stateStruct[machinestate]->onChangeCB)
          _state2stateStruct[machinestate]->onChangeCB(laststate, machinestate);
        else if (_state2stateStruct[ALL_STATES]->onChangeCB)
          _state2stateStruct[ALL_STATES]->onChangeCB(laststate, machinestate);

        _state2stateStruct[laststate]->timeInState += (millis() - laststatechange) / 1000;
        laststate = machinestate;
        laststatechange = millis();
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

        Log.printf("Time-out; transition from <%s> to <%s>\n",
                   _state2stateStruct[laststate]->label, _state2stateStruct[machinestate]->label);
        return;
      };

      if (_state2stateStruct[machinestate]->autoReportCycle && \
          millis() - laststatechange > _state2stateStruct[machinestate]->autoReportCycle && \
          millis() - lastReport > _state2stateStruct[machinestate]->autoReportCycle)
      {
        Log.printf("State: %s now for %lu seconds", _state2stateStruct[laststate]->label, (millis() - laststatechange) / 1000);
        lastReport = millis();
      };

      if (_state2stateStruct[machinestate]->onLoopCB)
        _state2stateStruct[machinestate]->onLoopCB(machinestate);
      else if (_state2stateStruct[ALL_STATES]->onLoopCB)
        _state2stateStruct[ALL_STATES]->onLoopCB(machinestate);
    };

    uint8_t MachineState::addState(state_t aState) {
      for (uint8_t i = 1; i < 255; i++)
        if (_state2stateStruct[i] == NULL) {
          _initState(i, &aState);
          return i;
        };
      Debug.println("BUG -- More than 254 active states ?");
      return 255;
    };
