#include <ACNode-private.h>

#ifdef ESP32
static bool _connected = false;

bool eth_connected () {
	return _connected;
}

void WiFiEvent(WiFiEvent_t event)
{
  switch (event) {
    case SYSTEM_EVENT_WIFI_READY:
      break;
    case SYSTEM_EVENT_STA_START:
      Log.println("Wifi Started");
      break;
    case SYSTEM_EVENT_ETH_START:
      Log.println("ETH Started");
      ETH.setHostname(_acnode->moi);
      break;
    case SYSTEM_EVENT_ETH_CONNECTED:
      Log.println("ETH Connected");
      break;
    case SYSTEM_EVENT_ETH_GOT_IP:
    case SYSTEM_EVENT_STA_GOT_IP: // added - we expected above - seem to get this one.
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
    case SYSTEM_EVENT_ETH_DISCONNECTED:
      Log.println("ETH Disconnected");
      _connected = false;
      break;
    case SYSTEM_EVENT_ETH_STOP:
      Log.println("ETH Stopped");
      _connected = false;
      break;
    default:
      Log.printf("ETH unknown event %d (ignored)\n", event);
      break;
  }
}
#endif
