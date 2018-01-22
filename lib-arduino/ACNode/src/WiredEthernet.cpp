#include <ACNode.h>

// Be sure to define these prior to any inclusion
// of ETH.h or anything that may include ETH.h - as
// the latter will otherwise define these values itself.
// Upon which below is ignored and will give you an odd 
// silent hang.

#define ETH_PHY_ADDR      (1)
#define ETH_PHY_MDC       (23)
#define ETH_PHY_MDIO      (18)
#define ETH_PHY_POWER     (17)
#define ETH_PHY_TYPE      (ETH_PHY_LAN8720)

#include <ETH.h>

static bool _connected = false;

bool eth_connected () {
	return _connected;
}

void WiFiEvent(WiFiEvent_t event)
{
  switch (event) {
    case SYSTEM_EVENT_ETH_START:
      Log.println("ETH Started");
      //set eth hostname here
      ETH.setHostname("esp32-ethernet");
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

void eth_setup()
{
  WiFi.onEvent(WiFiEvent);

  Debug.println("Starting wired ethernet.");
  ETH.begin();
  Debug.println("Wired ethernet started.");
}
