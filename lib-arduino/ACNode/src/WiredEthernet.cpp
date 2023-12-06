#include <ACNode-private.h>

#ifdef ESP32
#include <ETH.h>

static bool _connected = false;

// The Enums have all changed names; they got an Arduino prefix
// and were renamed a little.
//
#if ESP_ARDUINO_VERSION_MAJOR >= 2
#define EV(x) ARDUINO_EVENT_ ## x
#else
#define EV(x) SYSTEM_EVENT_
#endif

bool eth_connected () {
	return _connected;
}

void WiFiEvent(WiFiEvent_t event)
{
  switch (event) {
    case EV(WIFI_READY):
      break;
    case EV(WIFI_STA_START):
      Log.println("Wifi Started");
      break;
    case EV(ETH_START):
      Log.println("ETH Started");
      ETH.setHostname(_acnode->moi);
      break;
    case EV(ETH_CONNECTED):
      Log.println("ETH Connected");
      break;
    case EV(ETH_GOT_IP):
    case EV(WIFI_STA_GOT_IP): // added - we expected above - seem to get this one.
      Log.print("ETH MAC: ");
      Log.print(ETH.macAddress());
      Log.print(", IPv4: ");
      Log.print(ETH.localIP());
      if (ETH.fullDuplex()) {
        Log.print(", FULL_DUPLEX");
      }
      Log.print(", ");
      Log.print(ETH.linkSpeed());
      Log.println("Mbps");

      _connected = true;
      break;
    case EV(ETH_DISCONNECTED):
      Log.println("ETH Disconnected");
      _connected = false;
      break;
    case EV(ETH_STOP):
      Log.println("ETH Stopped");
      _connected = false;
      break;
    default:
      Log.printf("ETH unknown event %d (ignored)\n", event);
      break;
  }
}
#endif
