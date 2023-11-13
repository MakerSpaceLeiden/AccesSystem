#include "PowerNodeV11.h"

void PowerNodeV11::begin() {
   pinMode(RELAY_GPIO, OUTPUT);
   digitalWrite(RELAY_GPIO, LOW);

   ACNode::begin();
}
