Software for the Access Control Nodes (ACNode) at the
Makerspace Leiden.

RFID cards are coded as decimal numbers separated by a dash. So if the card contains

    unsigned char uid[] = { 13, 70, 0, 33, 19 }; // In 'read from card' order.

then the seralisation into an ascii string wil be '13-17-0-33-19'.

* Master

  The master node and a sample database. Can be ran out of the box with

       ./master.py --debug --dbfile sample-keydb.txt 

* SimpleDeurNode

  A simple door RFID reader and stepper-motor lock combination. Can be tested/ran with the command

      ./acnode-deur.py --offline --debug 1-1

  The --offline command removes the hardware from the loop (see the top of the file for the GPIO settings); the '1-1' is a 'fake' card; see the above sample-keydb.txt

* DeurControlNode

  A node that has only the stepper engine to open the door connected. Run with:

      ./acnode-deur.py --offline --debug

* DeurRFIDNode

  A node that has just a reader attached. 

  Test with the command (after starting above DeurControlNode.  

      ./acnode-rfid.py --offline --debug 1-1

* KrachtstroomACNode
      
  A more complex node - which can control multiple tools.

* Drumbeat

  A test tool - just broadcast the timestamps.

* lib

  Library of shared files:

 * lib/ACNode.py

  Base class with the protocol and assorted sundry

 * SensorACNode.py
 * ActuatorACNode.py
 * DrumbeatNode.py

   Classes on top of the ACNode that add the protocol bits for a sensor, actuator and the timestamps.

 * OfflineModeACNode.py

   Provides the '--offline' functionality to allow testing on laptops (e.g. not require the specific RasberryPI & assorted hardware).

 * RfidReaderNode.py

   Simple RFID reader; can also be ran 'stand alone' to test the RFID reader; e.g. with

         $ ./RfidReaderNode
         Detected card: 23-9-12-4-32
         Detected card: 3-71-5-21-81
         ctrl-C
         $

 * lib/Drivers
  * Mosfet.py
  * Stepper.py

   Directory with hardware/wiring specific drivers. These can be ran from the command line as well. e.g. use:

      ./Stepper.py 

   to activate the stepper to open the door.


Currentl dependencyies for a `pip install' are:

	wheel setuptools 

followed by (as otherwise dependencies can go funny):

	daemon setproctitle configargparse python-axolotl_curve25519 ed25519 python-axolotl paho-mqtt pycrypto

Pip can generally be installed with

	pkg install pip
	port install pip
	apt install python-pip

etc. On some platforms you will have to also install the headers; e.g.

	apt install python3-dev

or similar.

	

