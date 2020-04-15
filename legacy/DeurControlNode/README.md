Node that is ONLY an actuator; i.e. it control something; but
does not contain a RFID reader (or any other sensors that talk
*to* the master).

The key isuse here is that, as this type of node never emits
a requet, of ensuring that reply is reasonably hard. For this
reason we change & telltthe master our nonce early and often:

-	each time we (re)join a channel.

-	each time we act on a message from the master.

-	at regular (e.g. every 5 minute) intervals.

HARDWARE

The use of the '--offline' command argument allows for testing
without the relevant hardware.

The hardware specific code is in open_door() en init_gpio(). The

