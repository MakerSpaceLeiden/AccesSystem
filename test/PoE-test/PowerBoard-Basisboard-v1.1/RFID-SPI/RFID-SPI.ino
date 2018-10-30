
#include <SPI.h>
#include <MFRC522.h>

// https://wiki.makerspaceleiden.nl/mediawiki/index.php/Powernode_1.1
// SPI based RFID reader

#define RFID_MOSI_PIN   (13)
#define RFID_MISO_PIN   (12)
#define RFID_CLK_PIN    (14)
#define RFID_SELECT_PIN (15)
#define RFID_RESET_PIN  (32)

// Comment out the enxt line to work in `polling' mode.
//
#define RFID_IRQ_PIN   (33)

MFRC522 reader = MFRC522(RFID_SELECT_PIN, RFID_RESET_PIN);

volatile bool bNewInt = false;
void readCard() {
  bNewInt = true;
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n\n");
  Serial.println("Booted: " __FILE__ " " __DATE__ " " __TIME__ );

  SPI.begin(RFID_CLK_PIN, RFID_MISO_PIN, RFID_MOSI_PIN);

  Serial.println("Init reader");

  reader.PCD_Init();   // Init MFRC522
  reader.PCD_DumpVersionToSerial();  // Show details of PCD - MFRC522 Card Reader details

#ifdef RFID_IRQ_PIN
  pinMode(RFID_IRQ_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(RFID_IRQ_PIN), readCard, FALLING);

  reader.PCD_WriteRegister(reader.ComIEnReg, 0xA0 /* RQ irq */);
  bNewInt = false; //interrupt flag
#endif

  Serial.println("Scanning");
}


void loop() {
  static unsigned long last = 0;
  if (millis() - last > 2000) {
    last = millis();
    Serial.println("scanning.");
#ifdef RFID_IRQ_PIN
    // kick off a background scan. again.
    reader.PCD_WriteRegister(reader.FIFODataReg, reader.PICC_CMD_REQA);
    reader.PCD_WriteRegister(reader.CommandReg, reader.PCD_Transceive);
    reader.PCD_WriteRegister(reader.BitFramingReg, 0x87);
#endif
  }
  
#ifdef RFID_IRQ_PIN
  if (!bNewInt)
    return;
#else
  // Look for new cards
  if ( ! reader.PICC_IsNewCardPresent()) {
    return;
  }
#endif

  // Select one of the cards
  if ( ! reader.PICC_ReadCardSerial()) {
    return;
  }

  // Dump debug info about the card; PICC_HaltA() is automatically called
  reader.PICC_DumpToSerial(&(reader.uid));

#ifdef RFID_IRQ_PIN
  // clear the interupt and re-arm the reader.
  reader.PCD_WriteRegister(reader.ComIrqReg, 0x7F);
  reader.PICC_HaltA();
  bNewInt = false;
#endif
}
