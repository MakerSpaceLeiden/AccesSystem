// Simple 'tee' class - that sends all 'serial' port data also to the TelnetSerial and/or MQTT bus -
// to the 'log' topic if such is possible/enabled.
//
// XXX should refactor in a generic buffered 'add a Stream' class and then
// make the various destinations classes in their own right you can 'add' to the T.
//
//
#include <ACNode-private.h>
#include "TelnetSerialStream.h"

size_t TelnetSerialStream::write(uint8_t c) {
	if (!_acnode->isConnected())
		return 1;
  	if (!_server)
		return 1;
	for (int i = 0; i < MAX_SERIAL_TELNET_CLIENTS; i++) {
      		if (_serverClients[i] && _serverClients[i].connected()) {
		        _serverClients[i].write(c);
      		};
    	};
    	return 1;
}

void TelnetSerialStream::begin() {
  if (_server != NULL) 
	return;
  _server = new WiFiServer(_telnetPort);
  _server->begin();

  Log.printf("Opened serial telnet on %s:%d\n", _acnode->localIP().toString().c_str(), _telnetPort);
};

void TelnetSerialStream::stop() {
  if (!_server)
	return;
  for (int i = 0; i < MAX_SERIAL_TELNET_CLIENTS; i++) {
    if (_serverClients[i]) {
      _serverClients[i].println("Connection closed from remote end");
      _serverClients[i].stop();
    }
  }
  _server->stop();
}

void TelnetSerialStream::loop() {
  if (!_acnode->isConnected()) 
	return;

  if (!_server)
	return;

  if (_server->hasClient()) {
    int i;
    for (i = 0; i < MAX_SERIAL_TELNET_CLIENTS; i++) {
      if (!_serverClients[i] || !_serverClients[i].connected()) {
        Log.print(_serverClients[i].remoteIP()); Log.println(" Connected.");
        if (_serverClients[i]) 
		_serverClients[i].stop();
        _serverClients[i] = _server->available();
	if (_acnode && _acnode->moi)
          _serverClients[i].print(_acnode->moi);
        _serverClients[i].print(" Serial connected ");
        _serverClients[i].println(WiFi.macAddress());
        break;
      };
    };
    if (i >= MAX_SERIAL_TELNET_CLIENTS) {
      //no free/disconnected spot so reject
      Log.println("Too many log/telnet clients. rejecting.");
      _server->available().stop();
    }
  }

#if 0
  //check clients for data
  for (int i = 0; i < MAX_SERIAL_TELNET_CLIENTS; i++) {
    if (_serverClients[i] && _serverClients[i].connected()) {
      if (_serverClients[i].available()) {
        while (_serverClients[i].available()) {
		// we could buffer the chars entered and pass
		// them to some callback.
	};
    } else {
      if (_serverClients[i]) {
        Log.print(_serverClients[i].remoteIP()); 
	Log.println(" closed the conenction.");
        _serverClients[i].stop();
      }
    }
  };
 }
#endif

}
