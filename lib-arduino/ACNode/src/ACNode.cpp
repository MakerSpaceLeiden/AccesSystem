// Simple 'tee' class - that sends all 'serial' port data also to the Syslog and/or MQTT bus - 
// to the 'log' topic if such is possible/enabled.
//
// XXX should refactor in a generic buffered 'add a Stream' class and then
// make the various destinations classes in their own right you can 'add' to the T.
//
//
#include <ACNode.h>
#include <ACBase.h>


const char ACNODE_CAPS[] = 
#ifdef OTA
              " ota"
#endif
#ifdef CONFIGAP
              " configAP"
#endif
#ifdef WIRED_ETHERNET
              " ethernet"
#else
              " wifi"
#endif
#ifdef DEBUG
              " debug"
#endif
#ifdef DEBUG_ALIVE
              " fast-alive-beat"
#endif
#ifdef HASRFID
              " rfid-reader"
#endif
#ifdef SIG1
              " sig1"
#endif
#ifdef SIG2
              " sig2"
#endif
	;

// Sort of a fake singleton to overcome callback
// limits in MQTT callback and elsewhere.
//
ACNode &_acnode;

ACNode::ACNode(bool wired) : 
	_ssid(NULL), _ssid_passwd(NULL), _wired(wired) 
{
	_acnode = *this;
}

ACNode::ACNode(const char * ssid , const char * ssid_passwd ) :
	_ssid(ssid), _ssid_passwd(ssid_passwd), _wired(false) 
{
	_acnode = *this;
}

void ACNode::set_debugAlive(bool debug) { _debug_alive = debug; }

bool ACNode::isConnected() {
  if (_wired)
	return eth_connected();
  return (WiFi.status() == WL_CONNECTED);
};

void ACNode::begin() {

  if (_debug)
  	debugFlash();

  if (_wired) {
    Debug.println("starting up ethernet");
    eth_setup();
  };

#ifdef CONFIGAP
  configBegin();

  // Go into Config AP mode if the orange button is pressed
  // just post powerup -- or if we have an issue loading the
  // config.
  //
  static int debounce = 0;
  while (digitalRead(PUSHBUTTON) == 0 && debounce < 5) {
    debounce++;
    delay(5);
  };
  if (debounce >= 5 || configLoad() == 0)  {
    configPortal();
  }
#endif

  if (_wired) {
	Debug.println("starting up wifi");
  	WiFi.mode(WIFI_STA);
  };
  if (_ssid) {
  	WiFi.begin(_ssid, _ssid_passwd);
  } else {
  	WiFiManager wifiManager;
  	wifiManager.autoConnect();
  };

  const int del = 10; // seconds.

  // Try up to del seconds to get a WiFi connection; and if that fails; reboot
  // with a bit of a delay.
  //
  unsigned long start = millis();
  while (!isConnected() && (millis() - start < del * 1000)) {
    delay(100);
  };

  if (!_wired && !isConnected()) {
    Log.printf("No connection after %d seconds (ssid=%s). Going into config portal (debug mode);.\n", del, WiFi.SSID().c_str());
    // configPortal();
    Log.printf("No connection after %d seconds (ssid=%s). Rebooting.\n", del, WiFi.SSID().c_str());
    setOrangeLED(LED_FAST);
    Log.println("Rebooting...");
    delay(1000);
    ESP.restart();
  }
  if(_ssid)
	  Log.printf("Wifi connected to <%s>\n", WiFi.SSID().c_str());

  Log.print("IP address: ");
  Log.println(WiFi.localIP());

  WiFiClient _espClient = WiFiClient();
  _client = PubSubClient(_espClient);

  configureMQTT();


  if (_debug)
  	debugListFS("/");

  std::list<ACBase>::iterator it;
  for (it =_handlers.begin(); it!=_handlers.end(); ++it)
	it->begin();
}

void ACNode::addHandler(ACBase &handler) {
  _handlers.insert (_handlers.end(), handler);
}

void ACNode::addSecurityHandler(ACSecurityHandler &handler) {
  _security_handlers.insert (_security_handlers.end(), handler);

  // Some handlers need a begin or loop maintenance cycle.
  addHandler(handler);
}

void ACNode::loop() {
  // XX to hook into a callback of the ethernet/wifi
  // once we figure out how we can get this from the wifi.
  //
  static bool lastconnectedstate = false;
  bool connectedstate = isConnected();
  if (lastconnectedstate != connectedstate) {
	if (connectedstate)
		_connect_callback();
        else 
		_disconnect_callback();
	lastconnectedstate = connectedstate;
  };
  // Keepting time is a bit messy; the millis() wrap around and
  // the SPI access to the reader seems to mess with the millis().
  // So we revert to doing 'our own'.
  //
  static unsigned long last_loop = 0;
  if (millis() - last_loop >= 1000) {
    unsigned long secs = (millis() - last_loop + 499) / 1000;
    beatCounter += secs;
    last_loop = millis();
  }

  mqttLoop();

  if (_debug_alive) {
    static unsigned long last_beat = 0;
    if (millis() - last_beat > 3000 && _client.connected()) {
      send(NULL, "ping");
      last_beat = millis();
    }
  }

  std::list<ACBase>::iterator it;
  for (it=_handlers.begin(); it!=_handlers.end(); ++it)
	it->loop();

#if 0
  if (_debug) {
  // Emit the state of the node very 5 seconds or so.
  static int last_debug = 0, last_debug_state = -1;
  if (millis() - last_debug > 5000 || last_debug_state != machinestate) {
    Log.print("State: ");
    Log.print(machinestateName[machinestate]);
    Log.print(" Wifi= ");
    Log.print(WiFi.status());
    Log.print(WiFi.status() == WL_CONNECTED ? "(connected)" : "");
    Log.print(" MQTT=<");
    Log.print(state2str(client.state()));
    Log.print(">");

    Log.print(" Button="); Log.print(digitalRead(PUSHBUTTON)  ? "not-pressed" : "PRESSed");
    Log.print(" Relay="); Log.print(digitalRead(RELAY)  ? "ON" : "off");
    Log.println(".");

    last_debug = millis();
    last_debug_state = machinestate;
  }
};
#endif
}
