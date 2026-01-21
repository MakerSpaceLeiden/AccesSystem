#include "reset.h"

// Source https://docs.espressif.com/projects/arduino-esp32/en/latest/api/reset_reason.html
//
#if CONFIG_IDF_TARGET_ESP32  // ESP32/PICO-D4
#include "esp32/rom/rtc.h"
#elif CONFIG_IDF_TARGET_ESP32S2
#include "esp32s2/rom/rtc.h"
#elif CONFIG_IDF_TARGET_ESP32C2
#include "esp32c2/rom/rtc.h"
#elif CONFIG_IDF_TARGET_ESP32C3
#include "esp32c3/rom/rtc.h"
#elif CONFIG_IDF_TARGET_ESP32S3
#include "esp32s3/rom/rtc.h"
#elif CONFIG_IDF_TARGET_ESP32C6
#include "esp32c6/rom/rtc.h"
#elif CONFIG_IDF_TARGET_ESP32H2
#include "esp32h2/rom/rtc.h"
#elif CONFIG_IDF_TARGET_ESP32P4
#include "esp32p4/rom/rtc.h"
#elif CONFIG_IDF_TARGET_ESP32C5
#include "esp32c5/rom/rtc.h"
#elif CONFIG_IDF_TARGET_ESP32C61
#include "esp32c61/rom/rtc.h"
#else
// #error Target CONFIG_IDF_TARGET is not supported
#include "esp32/rom/rtc.h"
#endif

const char * reset_reason(uint8_t core) {
  switch (rtc_get_reset_reason(core)) {
    case 1:  return "Vbat power on reset"; break;
    case 3:  return "Software reset digital core"; break;
    case 4:  return "Legacy watch dog reset digital core"; break;
    case 5:  return "Deep Sleep reset digital core"; break;
    case 6:  return "Reset by SLC module, reset digital core"; break;
    case 7:  return "Timer Group0 Watch dog reset digital core"; break;
    case 8:  return "Timer Group1 Watch dog reset digital core"; break;
    case 9:  return "RTC Watch dog Reset digital core"; break;
    case 10: return "Instrusion tested to reset CPU"; break;
    case 11: return "Time Group reset CPU"; break;
    case 12: return "Software reset CPU"; break;
    case 13: return "RTC Watch dog Reset CPU"; break;
    case 14: return "for APP CPU, reset by PRO CPU"; break;
    case 15: return "Reset when the vdd voltage is not stable"; break;
    case 16: return "RTC Watch dog reset digital core and rtc module"; break;
    default: break;
  }
  return "Reset reason unknown";
}


