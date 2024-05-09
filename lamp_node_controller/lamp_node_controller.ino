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
*/



#include <avr/io.h>
#include <ACAN2515.h>
#include <SPI.h>

#define CAN_BITRATE 125000 //CAN Bus bitrate in bps
#define PIN_TOGGLELAMP PD6 //Pin used to turn the lamp on and off

static const uint16_t LAMP_NODE_ID = 0x010;

class Lamp {
  private:
    uint8_t _lampIsOn = 0;
  public:
    //Function turnOn and turnOff are the setters for the lamp state variable
    void turnOn(){
      PORTD |= (1 << PIN_TOGGLELAMP);
      _lampIsOn = 1;
    }
    void turnOff(){
      PORTD &= !(1 << PIN_TOGGLELAMP);
      _lampIsOn = 0;
    }
    uint8_t getLampState(){
      return _lampIsOn;
    }
};



//Pin Assignment
static const byte MCP2515_CS  = 10 ; // CS input of MCP2515
static const byte MCP2515_INT =  2 ; // INT output of MCP2515

static const uint32_t QUARTZ_FREQUENCY = 8UL * 1000UL * 1000UL ; // 8 MHz Quartz frquency of MCP2515
static const uint16_t NODE_ID = 0x010; //Used in the identifier field, lower has higher priority in arbitration

ACAN2515 can (MCP2515_CS, SPI, MCP2515_INT) ; // MCP2515 Driver object

//Acceptance masks and filters
//static const ACAN2515Mask rxm0 = standard2515Mask(0x7FF, 0xFF, 0xFF); //This mask just lets everything through for now
//static const ACAN2515AcceptanceFilter filters[] = {
//  standard2515Filter(0x005, 0, 0) // This filter will only accept commands from the main node (ID = 0x005), and not check either of the first two bytes of data? (or is it 2 filters for the first byte?)
//};

//Instatiate the lamp object
Lamp lamp1;

void setup() {
  SPI.begin();
  Serial.begin(9600);
  while(!Serial){
    delay(1);
  }

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

  //Set the data direction register for the lamp pin (output pin)
  DDRD |= (1 << PIN_TOGGLELAMP);
}



//~~~~~~~~~~~~~~~~~MAIN LOOP~~~~~~~~~~~~~~~~~
void loop() {  
  //The main loop in this iteration of the controller will wait for a CAN frame to come in from the MCP2515, and then execute the instructions contained therein 
  if (can.available()){
    receiveCANFrame();
  }
  // else{
  //   Serial.println("CAN not available");
  // }
  // delay(3000);

  //Test heartbeat
  //delay(2000);
  //uint8_t test[2] = {0x00, 0x12};
  //sendCANFrame(LAMP_NODE_ID, 2, test);


  //There will be a way to handle error frames
  //


}

//Function to send out a CAN frame
//When using the sendCANFrame function, always pass an 8 byte array to messageData, or else the gremlins will get in
bool sendCANFrame(uint16_t messageID, uint8_t messageLen, uint8_t messageData[8]){
  CANMessage message;
  message.id = messageID;
  message.len = messageLen;
  Serial.println(messageData[0]);
  Serial.println(messageData[1]);
  Serial.println(messageData[2]);
  Serial.println(messageData[3]);
  Serial.println(messageData[4]);
  Serial.println(messageData[5]);
  Serial.println(messageData[6]);
  Serial.println(messageData[7]);

  for(int i = 0; i < messageLen; i++){
    message.data[i] = messageData[i];
  }
  Serial.println(message.data[0]);

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
      //Lamp is already on, nothing needs to be done
      Serial.println("Lamp is already on, command ignored");
    }
  }
  else if(incomingFrameData[1] == 0x11){ //Lamp off command
    if(lamp1.getLampState() == true){
      //Lamp is on and needs to be turned off
      lamp1.turnOff();
      Serial.println("Lamp has been turned off");
    }
    else{
      //Lamp is already off, nothing needs to be done
      Serial.println("Lamp is already off, command ignored");
    }
  }

  //Send the new state of the lamp back to the primary node
  uint8_t lampState = lamp1.getLampState();

  uint8_t lampStateArray[8] = {lampState, 0, 0, 0, 0, 0, 0, 0};
  
  Serial.print("Current lamp state (1 = on, 0 = off): ");
  Serial.println(lampState);

  sendCANFrame(NODE_ID, 1, lampStateArray);



}


  //else{ //This is the condition for an error frame,
  //  Serial.println("Frame Error");
  //}

