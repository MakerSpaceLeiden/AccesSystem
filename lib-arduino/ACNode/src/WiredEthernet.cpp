#include <ACBaseNode.h>
#include <lwip/netdb.h>

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

static WiFiEvent_t cur;
static bool ev = false;

void WiFiEvent(WiFiEvent_t event)
{
    cur = event; 
    ev = true;
};

void WiFiEventLoop() {
    if (!ev)
         return;
    ev = false;

    switch (cur) {
        case EV(WIFI_READY):
            Debug.println("WiFi Ready");
            break;
        case EV(WIFI_STA_START):
        case EV(ETH_START):
            Debug.println("WiFi/ETH Started");
            ETH.setHostname(_acnodebase->moi);
            break;
        case EV(WIFI_STA_CONNECTED):
        case EV(ETH_CONNECTED):
            Debug.println("WiFi/ETH Connected");
            break;
        case EV(WIFI_STA_GOT_IP):
            Log.printf("WiFi MAC: %s, IPv4: %s\n", 
            	WiFi.macAddress().c_str(),
	        WiFi.localIP().toString().c_str());
            _connected = true;
            break;
        case EV(ETH_GOT_IP):
            Log.printf("ETH MAC: %s, IPv4: %s%s, %d Mbps ",
            	ETH.macAddress().c_str(),
		ETH.localIP().toString().c_str(),
                ETH.fullDuplex() ? ", FULL_DUPLEX" : "",
            	ETH.linkSpeed());
            Log.printf(" GW: %s DNS: ",
            	ETH.gatewayIP().toString().c_str());
	    for(int i = 0; i < 16; i++) {
		IPAddress ip = ETH.dnsIP(i);
		if (ip != IPAddress(INADDR_ANY))
		      Log.printf("%s ",ip.toString().c_str());
	    };
	    Log.println("");
            _connected = true;
            break;
        case EV(WIFI_STA_DISCONNECTED):
        case EV(ETH_DISCONNECTED):
            Log.println("Wifi/ETH Disconnected");
            _connected = false;
            break;
        case EV(WIFI_STA_STOP):
        case EV(ETH_STOP):
            Debug.println("Wifi/ETH Stopped");
            _connected = false;
            break;
        default:
            Debug.printf("Wifi/ETH unexpected event %d (ignored)\n", cur);
            break;
    }
}
#endif
