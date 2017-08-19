// Basic test of RFID reader using IRQ's as to
// make it easier to mix this with MQTT at some point.
//

#include <SPI.h>
#include <MFRC522.h>

#define RFID_SS     D0 // GPIO16 in ESP nomencalture
#define RFID_IRQ    D1 // GPIO5 
#define RFID_RESET  -1 // not connected

// SCK, MISO, and MOSI on thier normal D5,6,7 / GPIO 2,14,13

MFRC522 mfrc522(RFID_SS, RFID_RESET);

volatile boolean bNewInt = false;

void setup() {
  Serial.begin(9600);
  Serial.println("Starting build " __FILE__ "/" __DATE__ "/" __TIME__);

  SPI.begin();
  mfrc522.PCD_Init();

  pinMode(RFID_IRQ, INPUT_PULLUP);
  mfrc522.PCD_WriteRegister(mfrc522.ComIEnReg, 0xA0 /* rx irq */);
  attachInterrupt(digitalPinToInterrupt(RFID_IRQ), readCard, FALLING);

  Serial.println("Start scanning");
}

/**
   MFRC522 interrupt serving routine
*/
void readCard() {
  bNewInt = true;
}

/*
   The function sending to the MFRC522 the needed commands to activate the reception
*/
void activateRec(MFRC522 mfrc522) {
  mfrc522.PCD_WriteRegister(mfrc522.FIFODataReg, mfrc522.PICC_CMD_REQA);
  mfrc522.PCD_WriteRegister(mfrc522.CommandReg, mfrc522.PCD_Transceive);
  mfrc522.PCD_WriteRegister(mfrc522.BitFramingReg, 0x87);
}

/*
   The function to clear the pending interrupt bits after interrupt serving routine
*/
void clearInt(MFRC522 mfrc522) {
  mfrc522.PCD_WriteRegister(mfrc522.ComIrqReg, 0x7F);
}
void loop() {
  if (bNewInt) {
    if (mfrc522.PICC_ReadCardSerial() && mfrc522.uid.size > 0) {
      Serial.print(F("Card UID:"));
      for (int i = 0; i < mfrc522.uid.size; i++) {
        if (i) Serial.print("-");
        Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : " ");
        Serial.print(mfrc522.uid.uidByte[i], HEX);
      };
      Serial.println();

      // Reset the internal buffer of the MFRC522 library; to avoid
      // spurious data/prints.
      //
      mfrc522.uid.size = 0;
    } else {
      Serial.println("Interrupt (but we ignored it -- no data.");
    }
    bNewInt = false;
    clearInt(mfrc522);
  }

  // Apperently "The receiving block needs regular retriggering (tell the tag it should transmit??)"
  //
  static unsigned long lastActTime = 0;
  if (millis() - lastActTime > 1500) {
    activateRec(mfrc522);
    lastActTime = millis();
  }

  return;
}

