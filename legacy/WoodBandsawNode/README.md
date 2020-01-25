# ESP8266 

ESP8266 based node - should fit in a 256Kb or bigger ESP; from 1Mbyte OTA will work.

## Design principles

0.	Worst csae the ACNode should not make things unsafer.

1.	Mess as little as possible with existing safety features and
	interlocks as possible. 

2.	Make it easy to understand and fix - so select low learning
	curve technology & limit choises to things like standard
	Arduino code.

3.	Keep it cheap and cheerful.

## Safety

The solution to 0 & 1 is that we are using the existing safety 'magnetics';
and have merely added the ACNode as a 'must be interlock' to the existing
interlocks (is operator switch 'off'; is there no thermal/power overload).

## Residual issue

1. A shortcut on the board at the high-side of the sense wires could
foil the magnetics its interlock. Possible solution would be to
move the resistors of the optocoupler to the magnetics side.

2. The relay coil has been given a 0.1uF/100ohm snubber circuit.

## (re)programming the node

There is no OTA support (the flash is too small). 

Open the unit and locate the ESP8266 board. It is white.

Connect a **3v3** serial/usb dongle to the board; GND, TX and RX only when powered; or when the device is disconnected - including 3v3 power. The RX and TX of the board need to go to the TX and RX of the dongle (i.e. crossed).

There is a small 2 pin header and rectangular push-button just off the ESP8266 board - between antenna and the edge of the main board.

Ensure that these two pins of the header near the reset are bridged - this puts the unit in programming mode. 
Then press the reset button just next to it; and reflash from the Arduino IDE.

Note that the program pin doubles as the overload pin - so needs the jumper to be removed for normal operations (otherwise the device will go into error node - thinking the current-overload switch has tripped).

## Sensors and actuators:

* **Relay 1** - Controls the magnetic latch relay of the 4 phase control box; in
          series with the thermal/current interlock.

* **Relay 2** - Switches (just) the L1 of a power socket for the dust control unit.

* **Sense 1** - senses if there is a voltage on the magnetic latch provided through
          the opertor on/off-switch at the front. 

* **Sense 2** - sense if the relay is energized.

* **Switch 1** - senses if the over-current protection has been actviated.

* **Tagsense**  - SPI0.0 connected MFRC522 reader. Reset and IRQ are not used.yy

## Electroncis:

* **RELAYs**	Simple signal relays (230VAC, 10A), DC 5 volt coil with diode snubber. 
	Powered by a TUN with a 3v3 GPIO 'high'; with passive pulldown.

* **Sense**	Optocouplers with a few 100k; directly wired to mains; no bridge, single
	LED diode (so only signal about 1/4 of the 50hz period). Safe side
	wired to chargebank with diode.

* **LED**	Two LEDs, red and green. Wired to a single pin. Hz will let them
	both glow softly; pulling the pin down lights red, pulling it up
	lights green.

The sense pins are charge banks with a diode; pull down to ground to discharge
the capacitor prior to a measurement. Keep into account that you need at least
a full 50Hz cycles to be sure to see something.

## Possible machine states:

state   | pwr | description
--------|-----|--------------
safe	| Off |  magnetics can not be turned on.
energ	| Off | magnetics can be turned on. Machine will not start until operator switch also to on.
power	| Off  |magnetics are on. Machine will start when operator switch is set to on.
run	    | On   | Running, machine will switch off when operator switch is set to off.
err	    |Off   | something amis; e.g. operator switch to 'on' while magnetics are off.

Mapping to Possible states Relay 1 and Sense 1/2:

Relay 	| Sense 1| Sense 2 | state | description
--------|--------|---------|-------|------	
off	    |   off		| off	|	err	 | Magnetics off, operator switch on. blocked.
off	    |	on		| off	|	safe | Magnetics off, operator switch off. blocked.
off	    |	off		| on	|	errr | Magnetics off, blocked. Someone pressed the start button recently.
off	    |	on		| on	|	err	 | Magnetics off, blocked. Someone is prolly pressing the 'start' button.
on	    |	off		| off	|	err	  | Magnetics off, operator switch on. blocked.
on	    |	on		| off	|	energ |	Magnetics off, operator switch off. Magnetics can be operated.
on	    |	on		| on	|	power | Magnetics on, operator switch off. Can be started.
on	    |	off		| off	|	run	  | Magnetics on, operator switch on. Running.


## Wiring
 
Current wire colours - from left to right

### Block 1 - 2 screw terminals

colour | volt   | description
-------|------|------------
Blue	|N	| Neutral for PSU board. Neutral for the relays.
Brown	| L	| Phase for PSU board (fused).
##  Block 2 - 6 screw terminals

colour | volt | description
-------|------|------------
Red	   | dry | Dry contact; overcurrent sense. Wired to 97
White	| dry	| Dry contact; overcurrent sense. Wired to 98
Green	| S1	| Operator interlock/switch sense; Wired; to 17.
Yellow	| S2	| Interlock. Relay; mesures relay energized. Wired to 18.	
Black 	| L	| switched phase for dust control
Black	| L	| switched phase for magnetics. Wired to A1 (relay)
