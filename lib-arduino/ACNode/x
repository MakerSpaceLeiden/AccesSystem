If needed - create a file such as

		.../hardware/espressif/.../platform.local.txt 

and include a statement such as

               compiler.cpp.extra_flags=-imacros/Users/dirkx/.local-config.h

to include any #define's to override the default ones. E.g. for the
WIFI and OTA passwords used.

The *DEVELOPMENT* version of the Espressif-IDE is required; version 1.00
	seems to produce broken code (FunctionalInterrupts.o functionality
	seems AWOL; it hangs on an IRQ or produces StackOverflow errors).

Dependencies - from the standard Library manager:
	PubSubCLient Nick O'Leary 2.7.0
	ArduinoJson Benoit Blanchon, 5.13.3 (not the 6-beta!)

To be added manually in to .../Arduino/library
	https://github.com/dirkx/CurrentTransformer 	(awaiting pull request upstream)
	https://github.com/maykon/ButtonDebounce.git
	https://github.com/dirkx/base64_arduino.git	(assuming pull request upstream)
	https://github.com/dirkx/rfid.git (assuming you need both spi & i2c support)

The crypto libraries are packaged differently. Fetch:
	https://github.com/rweather/arduinolibs.git 

and then  for
	
0.1.x versions
	just move the dirctory Crypto up into .../Arduino/Library,
0.2.x versions
	move the directories  'Crypto' and `CryptoLegacy' up into .../Arduino/Library

Note that this later version of this library may contain some ESP32 optimisations that
are not quite tested/conflict with the compiler.

In order to build above on case-insenstive and case-sensitive systems; create a set
of 'unqiue' header signatures:

	cd Crypto
	mkdir CryptoLib	# we can't use Crypto - already in use by WPA supplicant.
	 for i in *.h; do ln  $i CryptoLib/$i; done

For test/as-is versions of the older code:
	https://github.com/zhouhan0126/WebServer-esp32.git
	https://github.com/zhouhan0126/DNSServer---esp32.git
i
You will have to edit PubSubClient.h and change the Packet size to something like a  1000.

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
