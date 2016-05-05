# 3phase Grinder AC Node


## Key components:

1.	One relay to interact with the interlock/start stop of the motor relay.
1.	One optocoupler to confirm the energising of the motor relay.
1.	One optocoupler to detect the operator switch setting on front of the grinder.
1. 	One LED for feedback.

Schematics at http://www.digikey.com/schemeit/#301j or in the grinder-schema.png file.

## To reprogram

Reprogramming requires physical access; the (current) flash is too small (or the firmware too bulky) to allow for OTA reprogramming. 

Changes to the password, wifi network and wifi password also require reprogramming *(hint: there is ample room in flash to add a 'emergency config' web interface and AP mode)*.

Required: 

1. 3v3 TTL serial with GND, RxD and TxD. Any normal FTDI or PL23xx will do - provided it is **3v3**.
1. A recent Arduino install (> 1.5)
1. matching http://arduino.esp8266.com/stable/package_esp8266com_index.json in the board manager).

Either remove the AC power and power the device with 3v3 from the Serial/USB adaptor; or leave the power on - and do **NOT** connected the 3v3. 

You can also remove the entire ESP8266 board; and do the programming in vitro at your desk. In that case you will need to power it with 3v3 from the USB/Serial dongle.

The EPS8266 is brought into serial firmware upload mode by short circuiting the red LED; or place a jumper on the 2 pin header/connector of the LED. Press the reset button (near the two capacitors and the 2 pin jumper) or powercycle as needed.

