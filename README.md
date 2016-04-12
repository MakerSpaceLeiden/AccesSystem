Software for the Access Control Nodes (ACNode) at the
Makerspace Leiden.

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
	Simple RFID reader
