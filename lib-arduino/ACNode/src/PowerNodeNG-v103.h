#define CHECK_NFC_READER_AVAILABLE_TIME_WINDOW  (10000) // in ms  
#define GPIOPORT_I2C_RECOVER_SWITCH             (15)       

#define OPTO_COUPLER_INPUT1                     (32)
#define OPTO_COUPLER_INPUT2                     (33)
#define OPTO_COUPLER_INPUT3                     (36)

#define CURRENT_SAMPLES_PER_SEC                 (500) // one sample each 2 ms
#define CURRENT_THRESHOLD                       (0.15)  // Irms ~ 4A, with current transformer 1:1000 and resistor of 36 ohm

#define BLINK_NOT_CONNECTED                     (1000)  // blink on/of every 1000 ms
#define BLINK_ERROR                             (300) // blink on/of every 300 ms
#define BLINK_CHECKING_RFIDCARD                 (600) // blink on/of every 600 ms

