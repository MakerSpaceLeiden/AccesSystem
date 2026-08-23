#pragma once

// Not all our ACNodes have the same UI capabilities (one or two LEDs, differnent colours, Buzzer).
// Below states are used by the various libraries (MQTT, OTA, RFID) - and it is left to the main
// hardware dependent node code to implement a signalStateToUser() with some appropriate signaling.
//
typedef enum { 
    STATE_OFF,                // not responsive
    STATE_ERROR,              // error; device is inoperable; user cannot do anything - device is trying to sort itself out.
    STATE_ERROR_HANG,         // error; device is inoperable; user cannot do anything (but powercycle, etc).
    STATE_BOOTING,            // pre-interaction not ready state (as oposed to STATE_NORMAL_PLEASE_WAIT).
    STATE_CONFIG,             // in configuration state (e.g. OTA) - waiting for the user to interact with it.
    STATE_UPDATE,             // being updated - user should not do anything.
    STATE_NORMAL_READY,       // device can be interacted with it
    STATE_NORMAL_ACTIVATED,   // device can be interacted with - it is 'on' or doing its thing.
    STATE_NORMAL_PLEASE_WAIT, // device is doing something - cannot be interacted with.    
  } signalState_t;

extern void signalStateToUser(signalState_t newState);

