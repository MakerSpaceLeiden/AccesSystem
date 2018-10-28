Dependencies
  git clone https://github.com/zhouhan0126/WebServer-esp32.git
  git clone https://github.com/zhouhan0126/DNSServer---esp32.git

Use:
	git clone https://github.com/zhouhan0126/WIFIMANAGER-ESP32.git
with PR#
	https://github.com/zhouhan0126/WIFIMANAGER-ESP32/pull/21
or
 	git@github.com:dirkx/WIFIMANAGER-ESP32.git

For debugging without a serial port (especially as you need to be isolated from the ground due to the PoE being at -48 volt and the unit having no galvanic separation); two options:

1-	syslog -- tcpdump the stream to the default gateway

2-	telnet - to port 23. Easy script to capture things early:

		while true; do nc 1.2.3.4 23; done

3-	UDP stream; Include something like

	  SyslogStream syslogStream = SyslogStream();
	  syslogStream.setDestination(192.172.8.123); // my laptops IP address.
	  syslogStream.setPort(1234);
	  syslogStream.setRaw(true);

	  Debug.addPrintStream(std::make_shared<SyslogStream>(syslogStream));
	  Log.addPrintStream(std::make_shared<SyslogStream>(syslogStream));

	And then use something like

		nc -lku 1234

	to listen to the 'serial'. 
