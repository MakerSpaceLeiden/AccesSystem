#include <RFID.h>

volatile bool cardScannedIrqSeen = false;
static void readCard() { cardScannedIrqSeen = true; }

void RFID::registerCallback(unsigned char irqpin) {
  if (irqpin == 255)  
    return;

  pinMode(irqpin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(irqpin), readCard, FALLING);
  _irqMode = true;
};


void RFID::processAndRateLimitCard(unsigned char * buff, size_t len) {
       char tag[RFID_MAX_TAG_LEN * 4 + 2] = { 0 };
       for (int i = 0; i < len; i++) {
           char buff[5];
           snprintf(buff, sizeof(buff), "%s%d", i ? "-" : "", buff[i]);
	   size_t left = sizeof(tag) - strlen(tag) -1;
	   if (left > 0) strncat(tag, buff, left);
       };

       // Limit the rate of reporting. Unless it is a new tag.
       //
       if (strncmp(lasttag, tag, sizeof(lasttag)) || millis() - lastswipe > 3000) {
    	      lastswipe = millis();
	      strncpy(lasttag, tag, sizeof(lasttag));

	      if (!_swipe_cb || (_swipe_cb(lasttag) != ACNode::CMD_CLAIMED)) {
 	      	   // Simple approval request; default is to 'energise' the contactor on 'machine'.
		   Log.println("Requesting approval");
	           _acnode->request_approval(lasttag);
	      } else {
		   Debug.println( _swipe_cb ? "internal rq used " : "callback claimed" );
	      };
       };
}

void RFID::report(JsonObject& report) {
	report["rfid_scans"] = _scan;
	report["rfid_misses"] = _miss;
        report["rfid"] = name();
}

