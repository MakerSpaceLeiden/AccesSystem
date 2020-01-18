**Install software environment for ACNode development**

The instruction below is meant for a Windows 10 PC.

The following elements are part of the development environment:

- MQTT client (+ optional MQQT server/broker);
- Git server and git client;
- Arduino software

MQTT client and server

See for an overview of a number of clients: [https://www.hivemq.com/blog/seven-best-mqtt-client-tools/](https://www.hivemq.com/blog/seven-best-mqtt-client-tools/)

**MQTT server/broker:**

E.g. mosquitto:

Information: [https://mosquitto.org/](https://mosquitto.org/)

Download: [https://mosquitto.org/download/](https://mosquitto.org/download/)

This server/broker will be installed as a service on the Windows 10 PC.

Installation of Mosquitto server:

Download Mosquitto from [**https://mosquitto.org/download**](https://mosquitto.org/download)

Double click on the downloaded executable to install the Mosquitto service.

**Install MQTT Clients:**

1. 1)MQTT.fx

Information: [**https://mqttfx.jensd.de/**](https://mqttfx.jensd.de/)

Download ( **64 bit** Windows): [**http://www.jensd.de/apps/mqttfx/1.7.1/mqttfx-1.7.1-windows-x64.exe**](http://www.jensd.de/apps/mqttfx/1.7.1/mqttfx-1.7.1-windows-x64.exe)

Download ( **32 bit** Windows): [**http://www.jensd.de/apps/mqttfx/1.7.1/mqttfx-1.7.1-windows.exe**](http://www.jensd.de/apps/mqttfx/1.7.1/mqttfx-1.7.1-windows.exe)

Double click on the downloaded executable to install this client.

Press Start and look in the list for MQTT.fx to start this program.

**Installation of GIT:**

Information: [**https://git-scm.com/**](https://git-scm.com/)

Download: [**https://git-scm.com/download/win**](https://git-scm.com/download/win)

**Git Client,** Git-cola:

Informatie: [**https://git-cola.github.io**](https://git-cola.github.io)

Download: [**https://github.com/git-cola/git-cola/releases/download/v3.5/git-cola-3.5.windows.zip**](https://github.com/git-cola/git-cola/releases/download/v3.5/git-cola-3.5.windows.zip)

Unpack the downloaded ZIP file and double click dubbelklik the \*.exe. Accept all windows message and install the software.





**Arduino:**

Information: [**https://www.arduino.cc/**](https://www.arduino.cc/)

Download: [**https://www.arduino.cc/download\_handler.php**](https://www.arduino.cc/download_handler.php)

After de Arduino software is installed, Arduino must be configured before it can be used to develop software for the nodes of the Makerspace.

**Olimex ESP32-PoE board:**

Install in Arduino the software for the Olimex ESP32-PoE board:

See e.g. the instructions on: [**https://randomnerdtutorials.com/installing-the-esp32-board-in-arduino-ide-windows-instructions/**](https://randomnerdtutorials.com/installing-the-esp32-board-in-arduino-ide-windows-instructions/)

In short after starting the Arduino IDE software:

1. In the menu of the Arduino software click **File | Preferences**
2. In the Preference window shown add the following next to text: Additional Boards Manager URLs:

**https://dl.espressif.com/dl/package\_esp32\_index.json,** [**http://arduino.esp8266.com/stable/package\_esp8266com\_index.json**](http://arduino.esp8266.com/stable/package_esp8266com_index.json)

1. Option: Also enable Display line numbers
2. Press the **OK** button
3. In the menu of the Arduino software select **Tools | Board: …** and click on **Boards Manager** in the menu popping up. Scroll to the bottom in the list shown or add esp32 to the search input box, until you see esp32. Click the Install button for the esp32 and close the window as soon as the software is installed
4. Select **Tools | Board: …** again but now scroll down in the list shown and select **OLIMEX ESP32‑PoE**

**Install libraries needed**

Use the Library Manager (Menu: **Tools | Manage Libraries…** ) of the Arduino ontwikkel to install the following libraries:

- **Adafruit GFX Library by Adafruit (versie 1.5.7)**
- **Adafruit SSD1306 by Adafruit (versie 1.3.0)**
- **ArduinoJson by Benoit Blanchon (versie 6.12.0)**
- **PubSubClient by Nick O&#39;Leary (versie 2.7.0)**
- **AccelStepper by Mike McCauley, (versie 1.59.0)**

To speed up your search, add the name of the library, in the input box next to: **Filter your search…** Next press **Install**.

**Tip:** To edit certain source files it might be handy to use a plain text editor tool e.g. Notepad++.

Information: [https://notepad-plus-plus.org/](https://notepad-plus-plus.org/)

Download: [https://notepad-plus-plus.org/downloads/](https://notepad-plus-plus.org/downloads/)



Notepad++ or another plain tekst editor is needed e.g. to edit PubSubClient.h after the library PubSubClientis installed. Open the file D **ocumenten\Arduino\libraries\PubSubClient\src\PubSubClient.h** e.g. in Notepad++

Change the value next to   **MQTT\_MAX\_PACKET\_SIZE** from **128** to **1290** , as shown below:

**#define MQTT\_MAX\_PACKET\_SIZE 1290**

Save this file and close Notepad++

The following libraries cannot be installed with the Arduino Library manager. To install these libraries:

Open each of the following urls&#39;s in a webbrowser, click in the window shown on the **Clone or download** button and then on the **Download ZIP** button, save the ZIP file in a directory of your own choice. Copy the content of all these ZIP files to the Windows directory **Documenten\Arduino\libraries**.

[**https://github.com/dirkx/OptocouplerDebouncer**](https://github.com/dirkx/OptocouplerDebouncer)

[**https://github.com/dirkx/CurrentTransformer**](https://github.com/dirkx/CurrentTransformer)

[**https://github.com/dirkx/ButtonDebounce**](https://github.com/dirkx/ButtonDebounce)

[**https://github.com/dirkx/base64\_arduino.git**](https://github.com/dirkx/base64_arduino.git)

[**https://github.com/dirkx/rfid.git**](https://github.com/dirkx/rfid.git)

[**https://github.com/prphntm63/WIFIMANAGER-ESP32**](https://github.com/prphntm63/WIFIMANAGER-ESP32)

Do the same fort the following link, but unpack the ZIP file e.g. in the directory where the ZIP file is downloaded to.

[**https://github.com/dirkx/AccesSystem**](https://github.com/dirkx/AccesSystem) ** **

Open in Windows explorer the directory in the unpacked ZIP file:   **……\AccesSystem-master\lib-arduino\**

Copy sub directory **ACNode** to the Windows directory **Documenten\Arduino\libraries**.

Do the samen for the link [**https://github.com/rweather/arduinolibs.git**](https://github.com/rweather/arduinolibs.git), also unpack this ZIP file and go to the directory **…..\arduinolibs-master\libraries\**. Copy the subdirectories **Crypto** and **CryptoLegacy** to the Windows directory **Documenten\Arduino\libraries**.

Open in the Arduino  development software de **SampleNode** sketch:

Click left in the top of the Arduino window on the menu item **File** , then: **Examples | ACNode | SampleNode**

 A new Arduino window is opened. with SampleNode sketch. Use File | Save As… to save this sketch as **SampleNode**.

The Arduino software is ready to compile the sketch and download it in the ESP32.

**Tip:** default the Serial monitor tool (Menu: **Tools | Serial Monitor** ) is configured with a baudrate of 9600, while the SampleNode sketch expects a baudrate of 115200. To do this you first have to connect the ESP32 to the PC and set the correct serial port (Menu: **Tools | Port** ).

= Other notes 

If needed - create a file such as

		.../platform.local.txt 

and include a statement such as

               compiler.cpp.extra_flags=-imacros/Users/dirkx/.local-config.h

to include any #define's to override the default ones. E.g. for the
WIFI and OTA passwords used.

= Debugging

For debugging without a serial port (especially as you need to be isolated from the ground due to the PoE being at -48 volt and the unit having no galvanic separation); four options:

0-	listen to MQTT (mosquitto_sub -t 'log/#').

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
