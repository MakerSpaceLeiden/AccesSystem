#include <TLog.h>
#include <Wire.h>

void scan_i2c()
{
  Log.println ();
  Log.println ("I2C scan:");
  byte count = 0;

  for (byte i = 8; i < 128; i++)
  {
    Wire.beginTransmission (i);          // Begin I2C transmission Address (i)
    if (Wire.endTransmission () == 0)  // Receive 0 = success (ACK response)
    {
      Log.print ("   ");
      Log.print (i, DEC);
      Log.print (" (0x");
      Log.print (i, HEX);
      Log.print  (")");
      switch(i) {
	case 0x24: Log.print(" PN542 RFID chip"); break;
	case 0x28: Log.print(" MFRC522 RFID chip"); break;
	case 0x20: Log.print(" MCP2xx io expander"); break;
	case 0x58: Log.print(" AW9523 io expander"); break;
	case 0x50: Log.print(" EEProm"); break;
        default:   Log.print(" Unknown identifier"); break;
      };
      Log.println();
      count++;
    }
  }
  Log.print ("Found a total of ");
  Log.print (count, DEC);
  Log.println (" i2c device(s).");
}

