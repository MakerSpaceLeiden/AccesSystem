// Be sure to define these prior to any inclusion
// of ETH.h or anything that may include ETH.h - as
// the latter will otherwise define these values itself.
// Upon which below is ignored and will give you an odd 
// silent hang.

#ifdef  ESP32

#define ETH_PHY_ADDR      (1)
#define ETH_PHY_MDC       (23)
#define ETH_PHY_MDIO      (18)
#define ETH_PHY_POWER     (17)
#define ETH_PHY_TYPE      (ETH_PHY_LAN8720)

#include <ETH.h>

static bool eth_connected = false;

void WiFiEvent(WiFiEvent_t event)
{
  switch (event) {
    case SYSTEM_EVENT_ETH_START:
      Serial.println("ETH Started");
      //set eth hostname here
      ETH.setHostname("esp32-ethernet");
      break;
    case SYSTEM_EVENT_ETH_CONNECTED:
      Serial.println("ETH Connected");
      break;
    case SYSTEM_EVENT_ETH_GOT_IP:
    case SYSTEM_EVENT_STA_GOT_IP: // added - we expected above - seem to get this one.
      Serial.print("ETH MAC: ");
      Serial.print(ETH.macAddress());
      Serial.print(", IPv4: ");
      Serial.print(ETH.localIP());
      if (ETH.fullDuplex()) {
        Serial.print(", FULL_DUPLEX");
      }
      Serial.print(", ");
      Serial.print(ETH.linkSpeed());
      Serial.println("Mbps");
      eth_connected = true;
      break;
    case SYSTEM_EVENT_ETH_DISCONNECTED:
      Serial.println("ETH Disconnected");
      eth_connected = false;
      break;
    case SYSTEM_EVENT_ETH_STOP:
      Serial.println("ETH Stopped");
      eth_connected = false;
      break;
    default:
      Serial.printf("ETH unknown event %d (ignored)\n", event);
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
#endif
