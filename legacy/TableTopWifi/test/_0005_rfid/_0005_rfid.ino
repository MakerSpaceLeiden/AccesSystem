// Basic test of RFID reader -- most minimal.
//
#include <SPI.h>
#include <MFRC522.h>

#define RFID_SS     D0 // GPIO16 in ESP nomencalture
#define RFID_IRQ    D1 // GPIO5 
#define RFID_RESET  -1 // not connected

// SCK, MISO, and MOSI on thier normal D5,6,7 / GPIO 2,14,13

MFRC522 mfrc522(RFID_SS, RFID_RESET);

void setup() {
  Serial.begin(9600);
  Serial.println("Starting build " __FILE__ "/" __DATE__ "/" __TIME__);

  SPI.begin();      // Init SPI bus
  mfrc522.PCD_Init();   // Init MFRC522
}


void loop() {
  if ( ! mfrc522.PICC_IsNewCardPresent()) {
    return;
  }

  // Select one of the cards
  if ( ! mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  // Dump debug info about the card; PICC_HaltA() is automatically called
  mfrc522.PICC_DumpToSerial(&(mfrc522.uid));
}

