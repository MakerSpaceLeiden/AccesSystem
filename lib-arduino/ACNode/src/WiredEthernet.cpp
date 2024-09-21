#include <ACBaseNode.h>

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
            Debug.println("Wifi Ready");
            break;
        case EV(WIFI_STA_START):
        case EV(ETH_START):
            Log.println("Wifi/ETH Started");
            ETH.setHostname(_acnodebase->moi);
            break;
        case EV(WIFI_STA_CONNECTED):
        case EV(ETH_CONNECTED):
            Log.println("Wifi/ETH Connected");
            break;
        case EV(WIFI_STA_GOT_IP):
            Log.print("Wifi MAC: ");
            Log.print(WiFi.macAddress());
            Log.print(", IPv4: ");
            Log.println(WiFi.localIP());
            _connected = true;
            break;
        case EV(ETH_GOT_IP):
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
        case EV(WIFI_STA_DISCONNECTED):
        case EV(ETH_DISCONNECTED):
            Log.println("Wifi/ETH Disconnected");
            _connected = false;
            break;
        case EV(WIFI_STA_STOP):
        case EV(ETH_STOP):
            Log.println("Wifi/ETH Stopped");
            _connected = false;
            break;
        default:
            Log.printf("Wifi/ETH unexpected event %d (ignored)\n", event);
            break;
    }
}
#endif
