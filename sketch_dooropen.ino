
/*
  RFID Door Lock
 10/13/2009
 Brett Martin
 www.pcmofo.com
 
 System for controlling access by reading 125khz RFID cards and compairing the ID tags to stored EEPROM values
 Master RFID card used to program new cards into the system. 
 
 ID-12/ID-12 RFID reader
 
 Using sparkfun breakout board
 [1] GND (Ground)
 [2] /RST (Reset Reader) Connect to +5vDC     
 [3] ANT (NC)  
 [4] ANT (NC)      
 [5] CP  (NC) (Card Present)     
 [6] NC
 [7] FS (Format select) (ground this for ASCII)
 [8] D1 (NC)
 [9] D0 (TTL Serial) to arduino RX             
 [10] Buzzer/LED (Card read indicator) (Connect to transistor + buzzer/led to indicate card was read)
 [11] 5v+ ( +5vDC power) 
 
 On Arduino....
 [3] (Digital OUT) to Blue LED+
 [4] (Digital OUT) to Red LED+
 [5] (Digital OUT) to Green LED+
 [6] (Digital OUT) to lock relay+ 
 [RX0] (Serial IN) to [9] D0 TTL serial out on ID-20
 
 
 // Based on code by BARRAGAN <http://people.interaction-ivrea.it/h.barragan> 
 // and code from HC Gilje - http://hcgilje.wordpress.com/resources/rfid_id12_tagreader/
 // Modified for Arudino by djmatic0
 // Modified for ID-12 and checksum by Martijn The - http://www.martijnthe.nl/
 //
 // Use the drawings from HC Gilje to wire up the ID-12.
 // Remark: disconnect the rx serial wire to the ID-12 when uploading the sketch
 */

#include <EEPROM.h> // Needed to write to EEPROM storage

#define powerPin    2
#define failPin     3
#define passPin     4
#define doorPin     5

boolean programMode = false;
boolean match = false;

byte storedCard[6];  // Stores an ID read from EEPROM
byte readCard[6];    // Sotres an ID read from the RFID reader
byte checksum = 0;   // Stores the checksum to verify the ID 

#include <SPI.h>
#include <Ethernet.h>

// Enter a MAC address for your controller below.
// Newer Ethernet shields have a MAC address printed on a sticker on the shield
byte mac[] = {  
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };
IPAddress server(192, 168, 0, 101); // Google

// Initialize the Ethernet client library
// with the IP address and port of the server 
// that you want to connect to (port 80 is default for HTTP):
EthernetClient client;

void setup() 
{
  //  for (int i = 0; i < 512; i++) // Uncoment to wipe the EEPROM
  //   EEPROM.write(i, 0);
  pinMode(powerPin, OUTPUT);      // Connected to Blue on tri-color LED to indicate reader is ready
  pinMode(passPin, OUTPUT);       // Connected to Green on tri-color LED to indicate user is valid
  pinMode(failPin, OUTPUT);       // Connected to Green on tri-color LED to indicate user is NOT valid or read failed
  pinMode(doorPin, OUTPUT);       // Connected to relay to activate the door lock
  Serial.begin(9600);             // Connect to the serial port

  digitalWrite(failPin, HIGH);
  digitalWrite(passPin, HIGH);
  digitalWrite(powerPin, HIGH);
  Serial.println("Starting...");

  setupEth();

  normalModeOn();
}

void setupEth()
{
  Serial.println("Setting up ethernet...");  
  // start the Ethernet connection:
  if (Ethernet.begin(mac) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
  }
  Serial.println("Initializing done, waiting...");
  // give the Ethernet shield a second to initialize:
  delay(1000);  
  Serial.println("Started connection, ready for use");
}

void loop () 
{

  // if there are incoming bytes available 
  // from the server, read them and print them:
  if (client.available()) {
    char c = client.read();
    Serial.print(c);
  }


  byte val = 0;       // Temp variable to hold the current byte

  normalModeOn();     // Normal mode, blue Power LED is on, all others are off

  if ( programMode)   // Program mode to add a new ID card
  {
    programModeOn();  // Program Mode cycles through RGB waiting to read a new card

    if(Serial.available() > 0)  // Waits for something to come on the serial line
    {
      if((val = Serial.read()) == 2)  // First Byte should be 2, STX byte 
      {  
        getID();                      // Get the ID, sets readCard = to the read ID
        if ( !isMaster(readCard) )    // Check to see if it is the master programing card
        {
          writeID(readCard);          // If not, write the card to the EEPROM sotrage
          programMode = false;        // Turn off programing mode
          checksum = 0;               // Make sure the checksum is empty
        }
      }
    }
  }
  // Normal Operation...
  else 
  {
    if(Serial.available() > 0)          // If the serial port is available and sending data...
    {
      if((val = Serial.read()) == 2)    // First Byte should be 2, STX byte 
      {                  
        getID();                        // Get the ID, sets readCard = to the read ID
        byte bytesread = 0;

        for ( int i = 0; i < 5; i++ )         // Loop 5 times
        {
          if  ( readCard[i] < 16 )              // Print out 0 if < 16 to prepend output
            Serial.print("0");

          Serial.print(readCard[i], HEX);     // Print out the hex value read in
          Serial.print(" ");
        }
        Serial.println();
        Serial.print("Checksum: ");
        Serial.print(readCard[5], HEX);       // Checksum read from the card

        if ( readCard[5] == checksum )        // See if the 5th BYTE (the checksum) read in from the reader
        {                                     // matches the checksum caculated 
          checksum = 0;                       // If so, we can empty the variable storing the calculated checksum
          //Serial.println(" passed");  
          //Serial.println();
          if ( isMaster( readCard ) )         // Check to see if the card is the master programing card
          {
            programMode = true;               // If so, enable programing mode
          }
          else
          {
            if ( findID(readCard) )           // If not, see if the card is in the EEPROM
            {
              openDoor(5);                    // If it is, open the door lock
            }
            else
            {
              failed();                       // If not, show that the ID was not valid
            }
          }
        }
        else                                  // If the checksum failed
        {                                     // Print out the checksum

          Serial.println(" error");
          Serial.println();
          Serial.print("[");
          Serial.print(readCard[5], HEX);
          Serial.print("] != [");
          Serial.print(checksum, HEX);
          Serial.print("] ");

        }
      }
    }
  }
}

// If the serial port is ready and we received the STX BYTE (2) then this function is called 
// to get the 4 BYTE ID + 1 BYTE checksum. The ID+checksum is stored in readCard[6]
// Bytes 0-4 are the 5 ID bytes, byte 5 is the checksum
void getID()
{
  byte bytesread = 0;
  byte i = 0;
  byte val = 0;
  byte tempbyte = 0;

  // 5 HEX Byte code is actually 10 ASCII Bytes. 
  while ( bytesread < 12 ) // Read 10 digit code + 2 digit checksum
  {                        
    if( Serial.available() > 0)   // Check to make sure data is coming on the serial line
    { 
      val = Serial.read();        // Store the current ASCII byte in val

      if((val == 0x0D)||(val == 0x0A)||(val == 0x03)||(val == 0x02)) 
      {                           // If header or stop bytes before the 10 digit reading
        break;                    // Stop reading                                 
      }

      if ( (val >= '0' ) && ( val <= '9' ) )    // Do Ascii/Hex conversion
      {
        val = val - '0';
      } 
      else if ( ( val >= 'A' ) && ( val <= 'F' ) ) 
      {
        val = 10 + val - 'A';
      }

      if ( bytesread & 1 == 1 )      // Every two ASCII charactors = 1 BYTE in HEX format
      {
        // Make some space for this hex-digit by
        // shifting the previous hex-digit with 4 bits to the left:
        readCard[bytesread >> 1] = (val | (tempbyte << 4));

        if ( bytesread >> 1 != 5 )                // If we're at the checksum byte,
        {
          checksum ^= readCard[bytesread >> 1];   // Calculate the checksum using XOR
        };
      } 
      else                                        // If it is the first HEX charactor
      {
        tempbyte = val;                           // Store the HEX in a temp variable
      };
      bytesread++;                                // Increment the counter to keep track
    } 
  } 
  bytesread = 0;
}


// Read an ID from EEPROM and save it to the storedCard[6] array
void readID( int number )  // Number = position in EEPROM to get the 5 Bytes from 
{
  int start = (number * 5 ) - 4;  // Figure out starting position
  //Serial.print("Start: ");
  //Serial.print(start);
  //Serial.print("\n\n");

  for ( int i = 0; i < 5; i++ )  // Loop 5 times to get the 5 Bytes
  {
    storedCard[i] = EEPROM.read(start+i);  // Assign values read from EEPROM to array
    /*
     Serial.print("Read [");
     Serial.print(start+i);
     Serial.print("] [");
     Serial.print(storedCard[i], HEX);
     Serial.print("] \n");
     */
  }
}

// Write an array to the EEPROM in the next available slot
void writeID( byte a[] )
{
  if ( !findID( a ) )          // Before we write to the EEPROM, check to see if we have seen this card before!
  {
    int num = EEPROM.read(0);  // Get the numer of used spaces, position 0 stores the number of ID cards
    /*
    Serial.print("Num: ");
     Serial.print(num);
     Serial.print(" \n");
     */
    int start = ( num * 5 ) + 1;   // Figure out where the next slot starts

    num++;                         // Increment the counter by one
    EEPROM.write( 0, num );        // Write the new count to the counter

    for ( int j = 0; j < 5; j++ )  // Loop 5 times
    {
      EEPROM.write( start+j, a[j] );  // Write the array values to EEPROM in the right position
      /*
      Serial.print("W[");
       Serial.print(start+j);
       Serial.print("] Value [");
       Serial.print(a[j], HEX);
       Serial.print("] \n");
       */
    }
    successWrite();
  }
  else
  {
    failedWrite();
  }
}

// Check two arrays of bytes to see if they are exact matches
boolean checkTwo ( byte a[], byte b[] )
{
  if ( a[0] != NULL )             // Make sure there is something in the array first
    match = true;                 // Assume they match at first

  for ( int k = 0;  k < 5; k++ )  // Loop 5 times
  {
    /*
    Serial.print("[");
     Serial.print(k);
     Serial.print("] ReadCard [");
     Serial.print(a[k], HEX);
     Serial.print("] StoredCard [");
     Serial.print(b[k], HEX);
     Serial.print("] \n");
     */
    if ( a[k] != b[k] )           // IF a != b then set match = false, one fails, all fail
      match = false;
  }
  if ( match )                    // Check to see if if match is still true
  {
    //Serial.print("Strings Match! \n");  
    return true;                  // Return true
  }
  else {
    //Serial.print("Strings do not match \n"); 
    return false;                 // Return false
  }
}

// Looks in the EEPROM to try to match any of the EEPROM ID's with the passed ID
boolean findID( byte find[] )
{
  int count = EEPROM.read(0);             // Read the first Byte of EEPROM that
  // Serial.print("Count: ");                // stores the number of ID's in EEPROM
  // Serial.print(count);
  //Serial.print("\n");
  for ( int i = 1; i <= count; i++ )      // Loop once for each EEPROM entry
  {
    readID(i);                            // Read an ID from EEPROM, it is stored in storedCard[6]
    if( checkTwo( find, storedCard ) )    // Check to see if the storedCard read from EEPROM 
    {                                     // is the same as the find[] ID card passed
      //Serial.print("We have a matched card!!! \n");
      return true;
      break;                              // Stop looking we found it
    }
    else                                  // If not, return false
    {
      //Serial.print("No Match here.... \n");
    }

  }
  return false;
}

// Opens door and turns on the green LED for setDelay seconds
void openDoor( int setDelay )
{
  setDelay *= 1000; // Sets delay in seconds

  digitalWrite(powerPin, LOW);  // Turn off blue LED
  digitalWrite(failPin, LOW);   // Turn off red LED
  digitalWrite(passPin, HIGH);  // Turn on green LED
  digitalWrite(doorPin, HIGH);  // Unlock door!

  delay(setDelay); // Hold door lock open for 5 seconds

  digitalWrite(doorPin, LOW); // Relock door
  digitalWrite(passPin, LOW); // Turn off green LED
}

// Flashes Red LED if failed login
void failed()
{
  digitalWrite(passPin, LOW);   // Make sure green LED is off
  digitalWrite(powerPin, LOW);  // Make sure blue LED is off

  // Blink red fail LED 3 times to indicate failed key
  digitalWrite(failPin, HIGH);  // Turn on red LED
  delay(500);
  digitalWrite(failPin, LOW);   // Turn off red LED

  digitalWrite(failPin, HIGH);  // Turn on red LED
  delay(500);
  digitalWrite(failPin, LOW);   // Turn off red LED

  digitalWrite(failPin, HIGH);  // Turn on red LED
  delay(500);
  digitalWrite(failPin, LOW);   // Turn off red LED
}

// Check to see if the ID passed is the master programing card
boolean isMaster( byte test[] ) 
{
  byte bytesread = 0;
  byte i = 0;                // Example card, replace with one of yours you want to be the master
  // 84 00 34 B6 F6 
  byte val[10] = {
    '8','4','0','0','3','4','B','6','F','6'     };  
  byte master[6];
  byte checksum = 0;
  byte tempbyte = 0;
  bytesread = 0; 

  for ( i = 0; i < 10; i++ )  // First we need to convert the array above into a 5 HEX BYTE array
  {
    if ( (val[i] >= '0' ) && ( val[i] <= '9' ) )   // Convert one char to HEX
    {
      val[i] = val[i] - '0';
    } 
    else if ( (val[i] >= 'A' ) && ( val[i] <= 'F' ) ) 
    {
      val[i] = 10 + val[i] - 'A';
    }

    if (bytesread & 1 == 1) // Every two hex-digits, add byte to code:
    {
      // make some space for this hex-digit by
      // shifting the previous hex-digit with 4 bits to the left:
      master[bytesread >> 1] = (val[i] | (tempbyte << 4));

      if (bytesread >> 1 != 5)                 // If we're at the checksum byte,
      {
        checksum ^= master[bytesread >> 1];      // Calculate the checksum... (XOR)
      };
    } 
    else 
    {
      tempbyte = val[i];                       // Store the first hex digit first...
    };
    bytesread++;         
  }

  if ( checkTwo( test, master ) )                // Check to see if the master = the test ID
    return true;
  else
    return false;
}

// Controls LED's for Normal mode, Blue on, all others off
void normalModeOn()
{
  digitalWrite(powerPin, HIGH);    // Power pin ON and ready to read card
  digitalWrite(passPin, LOW);      // Make sure Green LED is off
  digitalWrite(failPin, LOW);      // Make sure Red LED is off
  digitalWrite(doorPin, LOW);      // Make sure Door is Locked 
}

// Controls LED's for program mode, cycles through RGB
void programModeOn()
{
  digitalWrite(powerPin, LOW);  // Make sure blue LED is off
  digitalWrite(failPin, LOW);   // Make sure blue LED is off
  digitalWrite(passPin, HIGH);  // Make sure green LED is on
  delay(200);
  digitalWrite(powerPin, LOW);  // Make sure blue LED is off
  digitalWrite(failPin, HIGH);  // Make sure blue LED is on
  digitalWrite(passPin, LOW);   // Make sure green LED is off
  delay(200);
  digitalWrite(powerPin, HIGH); // Make sure blue LED is on
  digitalWrite(failPin, LOW);   // Make sure blue LED is off
  digitalWrite(passPin, LOW);   // Make sure green LED is off
  delay(200);
}

// Flashes the green LED 3 times to indicate a successful write to EEPROM
void successWrite()
{
  digitalWrite(powerPin, LOW); // Make sure blue LED is off
  digitalWrite(failPin, LOW);  // Make sure blue LED is off
  digitalWrite(passPin, HIGH); // Make sure green LED is on
  delay(200);
  digitalWrite(passPin, LOW);  // Make sure green LED is off
  delay(200);
  digitalWrite(passPin, HIGH); // Make sure green LED is on
  delay(200);
  digitalWrite(passPin, LOW);  // Make sure green LED is off
  delay(200);
  digitalWrite(passPin, HIGH); // Make sure green LED is on
  delay(200);
}

// Flashes the red LED 3 times to indicate a failed write to EEPROM
void failedWrite()
{
  digitalWrite(powerPin, LOW); // Make sure blue LED is off
  digitalWrite(failPin, HIGH); // Make sure red LED is on
  digitalWrite(passPin, LOW);  // Make sure green LED is off
  delay(200);
  digitalWrite(failPin, LOW);  // Make sure red LED is off
  delay(200);
  digitalWrite(failPin, HIGH); // Make sure red LED is on
  delay(200);
  digitalWrite(failPin, LOW);  // Make sure red LED is off
  delay(200);
  digitalWrite(failPin, HIGH); // Make sure red LED is on
  delay(200); 
}

/*
ATmega168 with Arduino Bootloader $4.95
 http://www.sparkfun.com/commerce/product_info.php?products_id=8846
 
 Crystal 16MHz $1.50
 http://www.sparkfun.com/commerce/product_info.php?products_id=536
 
 Capacitor Ceramic 22pF $0.25 (x2)
 http://www.sparkfun.com/commerce/product_info.php?products_id=8571
 
 Capacitor Ceramic 0.1uF $0.25
 http://www.sparkfun.com/commerce/product_info.php?products_id=8375
 
 Resistor 10k Ohm 1/6th Watt PTH $0.25
 http://www.sparkfun.com/commerce/product_info.php?products_id=8374
 
 Mini Push Button Switch $0.35
 http://www.sparkfun.com/commerce/product_info.php?products_id=97
 
 Triple Output LED RGB - Diffused $1.95
 http://www.sparkfun.com/commerce/product_info.php?products_id=9264
 
 RFID Reader ID-12 $29.95
 http://www.sparkfun.com/commerce/product_info.php?products_id=8419
 
 RFID Reader ID-20 $34.95
 http://www.sparkfun.com/commerce/product_info.php?products_id=8628
 
 RFID Reader Breakout $0.95
 http://www.sparkfun.com/commerce/product_info.php?products_id=8423
 
 Break Away Headers - Straight $2.50
 http://www.sparkfun.com/commerce/product_info.php?products_id=116
 
 RFID Tag - 125kHz $1.95
 http://www.sparkfun.com/commerce/product_info.php?products_id=8310
 
 Door Fail Secure access control Electric Strike v5 NO $17.50 (kawamall, bay)
 http://cgi.ebay.com/Door-Fail-Secure-access-control-Electric-Strike-v5-NO_W0QQitemZ350230332833QQcmdZViewItemQQptZLH_DefaultDomain_0?hash=item518b5ac5a1#ht_3787wt_1167
 */


