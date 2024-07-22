 /*
 - APDS gesture check was stalling loop -- Because the include statement was malformed? TBD
 - Using digitalWrite to turn relay on and off worked when using PORTB |= (1 << pin); did not... why?
 */



/*
CAN incoming Frame data structure: 8 byte array
byte 0: current state of the lamp (On = 0x01, Off = 0x00)
byte 1: command state of the lamp (On = 0xFF, Off = 0x11, no change = 0x00)
byte 2:
byte 3:
byte 4:
byte 5:
byte 6:
byte 7:
*/

/*
TODO
add functionality for manual button to toggle the lamp, send updated state back to primary node after that action
Class lamp set/reset need to be momentary actions  

Change lamp state back to bool from uint8_t now that you know that it was not the problem
*/



#include <avr/io.h>
#include <ACAN2515.h>
#include <SPI.h>
#include <Adafruit_APDS9960.h>

#define CAN_BITRATE 125000 //CAN Bus bitrate in bps
#define PIN_SET_RELAY 4 //PD4 //Pin used to turn the lamp on and off via the S/R latching relay
#define PIN_RESET_RELAY 3 //PD3
#define PIN_SET_BUTTON 8//PB0 //Pin used for physical button that will tell the MCU to activate pin_set_relay
#define PIN_RESET_BUTTON 7//PD7
#define PIN_HEARTBEAT 6


class Lamp {
  private:
    uint8_t _lampIsOn = 0;
  public:
    //Function turnOn and turnOff are the setters for the lamp state variable
    void turnOn(){
      //Pulse the set pin to close the relay
      //PORTB &= !(1 << PIN_RESET_RELAY); //Always need to make sure that both set and reset are never active at the same time, 
      digitalWrite(PIN_RESET_RELAY, LOW);
      delay(10);
      //PORTB |= (1 << PIN_SET_RELAY);
      digitalWrite(PIN_SET_RELAY, HIGH);
      _lampIsOn = 1;
      delay(10);
      //PORTB &= !(1 << PIN_SET_RELAY);
      digitalWrite(PIN_SET_RELAY, LOW);
      delay(10);
    }
    void turnOff(){
      //Pulse the reset pin to open the relay
      //PORTB &= !(1 << PIN_SET_RELAY);
      digitalWrite(PIN_SET_RELAY, LOW);
      delay(10);
      //PORTB |= (1 << PIN_RESET_RELAY);
      digitalWrite(PIN_RESET_RELAY, HIGH);
      _lampIsOn = 0;
      delay(10);
      //PORTB &= !(1 << PIN_RESET_RELAY);
      digitalWrite(PIN_RESET_RELAY, LOW);
      delay(10);
    }
    uint8_t getLampState(){
      return _lampIsOn;
    }
};



//Constants
static const uint16_t LAMP_NODE_ID = 0x010;
static const uint16_t APDS_I2C_ADDRESS = 0x39; //I2C pins: SDA: (PC4, A4, PYS27 APDS STEMMAQT Blue), SCL: (PC5, A5, PYS28, APDS STEMMAQT Yellow)

static const byte MCP2515_CS  = 10 ;// PB2 // CS input of MCP2515
static const byte MCP2515_INT =  2; //PD2 (INT0) // INT output of MCP2515

static const uint32_t QUARTZ_FREQUENCY = 8UL * 1000UL * 1000UL ; // 8 MHz Quartz frquency of MCP2515
static const uint16_t NODE_ID = 0x010; //Used in the identifier field, lower has higher priority in arbitration

ACAN2515 can (MCP2515_CS, SPI, MCP2515_INT) ; // MCP2515 Driver object
Adafruit_APDS9960 apds; //APDS9960 gesture and proximity sensor object

//Acceptance masks and filters
//static const ACAN2515Mask rxm0 = standard2515Mask(0x7FF, 0xFF, 0xFF); //This mask just lets everything through for now
//static const ACAN2515AcceptanceFilter filters[] = {
//  standard2515Filter(0x005, 0, 0) // This filter will only accept commands from the main node (ID = 0x005), and not check either of the first two bytes of data? (or is it 2 filters for the first byte?)
//};

//Instatiate the lamp object
Lamp lamp1;

void setup() {
  SPI.begin();
  Serial.begin(115200);
  while(!Serial){
    delay(1);
  }

  pinMode(PIN_HEARTBEAT, OUTPUT);

  //Configure ACAN2515
  ACAN2515Settings settings (QUARTZ_FREQUENCY, CAN_BITRATE);
//  const uint16_t errorCode = can.begin (settings, [] { can.isr (); }, rxm0, filters, 1);
  const uint16_t errorCode = can.begin(settings, [] {can.isr();});
  if(errorCode != 0){
    Serial.print("Configuration error 0x");
    Serial.println(errorCode, HEX);
  }
  else{
    Serial.println("CAN Init Complete");
  }

  //Set the data direction register for the S/R relay pins (output pins)
  // DDRD |= (1 << PIN_SET_RELAY) | (1 << PIN_RESET_RELAY);
  
  pinMode(PIN_SET_RELAY, OUTPUT);
  pinMode(PIN_RESET_RELAY, OUTPUT);

  digitalWrite(PIN_SET_RELAY, LOW);
  digitalWrite(PIN_RESET_RELAY, LOW);

  //Set the data direction register for the manual S/R buttons (input pins)
  // DDRC &= !(1 << PIN_SET_BUTTON);
  // DDRC &= !(1 << PIN_RESET_BUTTON);

  pinMode(PIN_SET_BUTTON, INPUT);
  pinMode(PIN_RESET_BUTTON, INPUT);


  // //Initialize the ADPS sensor
  // if(!apds.begin()){
  //   Serial.print("apds not init");
  // }
  // else{
  //   Serial.println("apds successful init");
  // }

  // apds.enableProximity(true);
  // apds.enableGesture(true);

  Serial.println("INIT Complete");

}



//~~~~~~~~~~~~~~~~~MAIN LOOP~~~~~~~~~~~~~~~~~
void loop() {

  //The main loop in this iteration of the controller will wait for a CAN frame to come in from the MCP2515, and then execute the instructions contained therein 
  if (can.available()){
    receiveCANFrame();
    Serial.println("Frame received in loop");
  }
  // else{
  //   Serial.println("CAN not available");
  // }
 
  // //Check the APDS gesture sensor
  // uint8_t gesture = apds.readGesture();
  // //Serial.println("Finish Read Gesture");
  // if(gesture == APDS9960_DOWN){
  //   //Turn lamp off
  //   Serial.println("Read down gesture");
  //   lamp1.turnOff();
  //   sendLampStateFrame();
  // }

  // if(gesture == APDS9960_UP){
  //   //Turn lamp on
  //   Serial.println("Read up (on) gesture");
  //   lamp1.turnOn();
  //   sendLampStateFrame();
  //}

  if(digitalRead(PIN_SET_BUTTON)){//(PINC & (1 << PIN_SET_BUTTON)){
    //Turn lamp on
    Serial.println("Physical set button input registered");
    lamp1.turnOn();
    sendLampStateFrame();
  }
  if(digitalRead(PIN_RESET_BUTTON)){//(PINC & (1 << PIN_RESET_BUTTON)){
    //Turn off lamp
    Serial.println("Physical reset button input registerd");
    lamp1.turnOff();
    sendLampStateFrame();
  }

}

//Function to send out a CAN frame
//When using the sendCANFrame function, always pass an 8 byte array to messageData, or else the gremlins will get in
bool sendCANFrame(uint16_t messageID, uint8_t messageLen, uint8_t messageData[8]){
  CANMessage message;
  message.id = messageID;
  message.len = messageLen;

  for(int i = 0; i < messageLen; i++){
    message.data[i] = messageData[i];
  }

  const bool ok = can.tryToSend(message);
  if(ok){
    Serial.println("Frame sent");
    return true;
  }
  else{
    Serial.println("Frame send failure");
    return false;
  }


}

// Function to deal with an incoming CAN frame 
void receiveCANFrame(){
  CANMessage incomingFrame;
  can.receive(incomingFrame);
  Serial.println("Frame Received");

  uint8_t dataLength = incomingFrame.len;
  uint8_t incomingFrameData[8];
  for(int i = 0; i < dataLength; i++){
    incomingFrameData[i] = incomingFrame.data[i];
  }

  //Handle the command byte and turn the lamp on or off

  if(incomingFrameData[1] == 0xFF){ //Lamp on command
    if(lamp1.getLampState() == false){
      //Lamp is off and needs to be turned on
      lamp1.turnOn();
      Serial.println("Lamp has been turned on");
    }
    else{
      //Lamp is already on
      lamp1.turnOn();
      Serial.println("Lamp is already on");
    }
  }
  else if(incomingFrameData[1] == 0x11){ //Lamp off command
    if(lamp1.getLampState() == true){
      //Lamp is on and needs to be turned off
      lamp1.turnOff();
      Serial.println("Lamp has been turned off");
    }
    else{
      //Lamp is already off
      lamp1.turnOff();
      Serial.println("Lamp is already off");
    }
  }

  //Send the new state of the lamp back to the primary node
  sendLampStateFrame();

}

void sendLampStateFrame(){
  uint8_t state = lamp1.getLampState();
  uint8_t stateArr[8] = {state, 0, 0, 0, 0, 0, 0, 0};
  sendCANFrame(NODE_ID, 1, stateArr);
}


  //else{ //This is the condition for an error frame,
  //  Serial.println("Frame Error");
  //}


