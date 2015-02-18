// Temperature Monitor
// Measures the temperature and sends it over the XBee network.
// Puts the Arduino and XBee to sleep after each send to conserve
// battery power. On vs off time is set with the defines below.
//
// For documentation see desert-home.com 
//
// Don't forget to change the version number (below includes)
//#include <Time.h>
//#include <TimeAlarms.h>
#include <SoftwareSerial.h>
#include <avr/wdt.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include <XBee.h>
#include <JeeLib.h> // Low power functions library is here
 
char verNum[] = "Version 1";
char t[10], v[10], pt[10];  // To store the strings for temp and pressure

// The awake and sleep times
#define AWAKETIME 10000 
#define SLEEPTIME 50000

// These are the XBee control pins
#define xbeeRxPin 4
#define xbeeTxPin 5
#define xbeeCTS 6         // Clear to send
#define xbeeSleepReq 7    // Set low to wake it up
#define AWAKE LOW         // I can never rememmber it, so
#define SLEEP HIGH        // I made these defines 

// The various XBee messages and such
SoftwareSerial xbeeSerial = SoftwareSerial(xbeeRxPin, xbeeTxPin);
XBee xbee = XBee();
ZBRxResponse xbrx = ZBRxResponse();  // for holding the ascii traffic
AtCommandRequest atRequest = AtCommandRequest();
AtCommandResponse atResponse = AtCommandResponse();
ZBTxStatusResponse txStatus = ZBTxStatusResponse(); // to look at the tx response packet
ModemStatusResponse modemStatus = ModemStatusResponse();
char deviceName [21];  // read fromm the XBee that is attached
int deviceFirmware;
XBeeAddress64 Broadcast = XBeeAddress64(0x00000000, 0x0000ffff);
XBeeAddress64 RaspberryPi = XBeeAddress64(0x0013a200, 0x406f7f8C);// House Controller address, so I can remember it
XBeeAddress64 Destination;  // This will hold the XBee address that the code currently sends to
XBeeAddress64 ThisDevice;   // will fill in with the local XBee address
XBeeAddress64 Controller;   // will fill in with the default destination address read from the XBee

char requestBuffer [50]; // for requests coming from the serial port
int requestIdx;

char Dbuf[100]; // general purpose buffer
unsigned long savedmillis = 0; // Use this to time the sleep cycles

// Need this for the watchdog timer wake up operation
ISR(WDT_vect) { Sleepy::watchdogEvent(); } // Setup the watchdog

void(* resetFunc) (void) = 0; //declare reset function @ address 0

void setup() {
  Serial.begin(9600);          //talk to it port
  Serial.print(F("Room Temperature Sensor "));
  Serial.print(verNum);
  Serial.println(F(" Init..."));

  pinMode(13, OUTPUT);         // turn off the on board led
  digitalWrite(13, LOW);       // because it uses power too
  analogReference(INTERNAL);   // hoping to improve the temp reading
  pinMode(xbeeCTS, INPUT);
  pinMode(xbeeSleepReq, OUTPUT);
  digitalWrite(xbeeSleepReq, AWAKE); // To make sure the XBee is awake
  xbeeSerial.begin(9600);            // This starts the xbee up
  xbee.setSerial(xbeeSerial);        // XBee library will use Software Serial port
  Destination = Broadcast;           // set XBee destination address to Broadcast for now
  // Now, get the unit name from the XBee, this will be sent in 
  // all reports.  While we're there, get some other stuff as well.
  xbeeSerial.listen();  // so SoftwareSerial pays attention to the XBee
  getDeviceParameters();
  Serial.print(F("This is device "));
  Serial.println(deviceName);
  Serial.print(F("Device Firmware version "));
  Serial.println(deviceFirmware,HEX);
  Serial.print(F("This Device Address is 0x"));
  print32Bits(ThisDevice.getMsb());
  Serial.print(F(" 0x"));
  print32Bits(ThisDevice.getLsb());
  Serial.println();
  Serial.print(F("Default Destination Device Address is 0x"));
  print32Bits(Controller.getMsb());
  Serial.print(F(" 0x"));
  print32Bits(Controller.getLsb());
  Serial.println();
  Destination = Controller; // Setup to send to house controller
  savedmillis = millis();
  Serial.println(F("Setup Complete"));
}

boolean firsttime = true;

void loop(){
  if(firsttime == true){  // keep from running out of memory!!
    showMem();
    firsttime = false;
  }
  /* Since this may have piled up a bunch of messages while
     sleeping, it could take a while
  */
  // check for incoming XBee traffic
  while (xbeeSerial.available() > 0)
    checkXbee();
  
  // This allows emulation of an incoming XBee command from the serial console
  // It's not a command interpreter, it's limited to the same command string as
  // the XBee.  Can be used for testing
  if(Serial.available()){  // 
    char c = Serial.read();
    if(requestIdx < sizeof(requestBuffer)){
      requestBuffer[requestIdx] = c;
      requestIdx++;
    }
    else{  //the buffer filled up with garbage
      memset(requestBuffer,0,sizeof(requestBuffer));  //start off empty
      requestIdx = 0; // and at the beginning
      c = 0; // clear away the just-read character
    }
    if (c == '\r'){
      requestBuffer[requestIdx] = '\0';
//      Serial.println(requestBuffer);
      // now do something with the request string
      processXbee(requestBuffer,strlen(requestBuffer));
      //start collecting the next request string
      memset(requestBuffer,0,sizeof(requestBuffer));  //start off empty
      requestIdx = 0; // and at the beginning
    }
  }
  if (millis() - savedmillis > AWAKETIME){
    Serial.print("was awake for ");
    Serial.println(millis() - savedmillis);
    delay(100); // delay to allow the characters to get out
    savedmillis = millis();
    unsigned long timeSlept = 0;
    while (timeSlept < SLEEPTIME){
      digitalWrite(xbeeSleepReq, SLEEP); // put the XBee to sleep
      Sleepy::loseSomeTime((unsigned int)1000);
      timeSlept = (millis() - savedmillis);
      digitalWrite(xbeeSleepReq, AWAKE); // wake that boy up now
    }
    Serial.print("was asleep for ");
    Serial.println(millis() - savedmillis);
    savedmillis = millis();
    sendStatusXbee();
  }
}