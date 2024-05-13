/*
Changes since last commit:
-- ConfigLCD now just stores the pointers to the page objects and uses structure dereference to use the page methods
-- LCD Enter input and analog data editing works now -- the config LCD is minimally functional, but it works
-- Current issues: analog data input does not use the full range of the pot for some unknown reason
   - when you modify the field data to a value with fewer digits it does not draw over the old least significant digits until you cycle through the pages -simple fix
*/

/* NOTES
  Weekdays start at sunday=weekday() = 1
  
  Lamp command structure
  byte 0: current state of the lamp (On = 0x01, Off = 0x00)
  byte 1: command state of the lamp (On = 0xFF, Off = 0x11, no change = 0x00)
  byte 2: reserved
  byte 3: reserved
  byte 4: reserved
  byte 5: reserved
  byte 6: reserved
  byte 7: reserved
*/

/*TODO - project
  Test lamp node comtroller on bare ATmega328p
  Relay control schematic
  Build lamp node circuit
  Manual input at lamp node - gesture sensor to activate lamp and send state change over canbus to primary
    Code and circuit
  
  Future:
    Give config LCD its own controller and either have it talk on the CAN bus or have an SPI or I2C connection to the primary node (pros/cons of each approach?)

*/

/* TODO - -primary node controller program
  Send debug messages from secondary nodes over canbus to be Serial.print ed on the main node? message page on lcd?
  Config LCD pages
    Set flag islongworkweek, master enable/diable of all lamp alarms
    Manually activate lamp from menu
    Change alarm times/ schedule? -- low priority
    View current time on RTC -- low priority
  
  Break out all class definitions to separate file, write header file



*/

#include <ACAN_T4.h>
#include <TimeLib.h>
#include <TimeAlarms.h>
//When using the TimeAlarms library, use Alarm.delay() instead of the normal delay() function, also need to do Alarm.delay(0) regularly to check if an alarm needs to be activated
#include <Wire.h>
#include <LiquidCrystal_I2C.h>


/*
$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
Definitions, Constants, Variables in the Global Scope, and Other Sundry Items That Just Need to be Before the Class Definitions.
$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
*/


#define CAN_BITRATE 125000 //CAN bus bitrate in bps
#define BAUD_RATE 115200 //Serial baud rate for communication with PC

#define TIME_HEADER  "T"   // Header tag for serial time sync message
#define TIME_REQUEST  7    // ASCII bell character requests a time sync message 

//Input pins
#define ANALOG_LCD_PIN A13
#define SCROLL_LCD_PIN 2
#define ENTER_LCD_PIN 3

static const uint16_t PRIMARY_NODE_ID = 0x005; //Node id of the primary node, value is low so that it has higer priority in arbitration (controls most time sensitive commands)
static const uint16_t LAMP_NODE_ID = 0x010;

//Initialize the LCD
LiquidCrystal_I2C lcd(0x3F, 20, 4);


//Polluting the global scope
bool isLongWorkWeek = false; //Long week work days are Sun, M, Th, F. Short week (false) is Tu, W, Sat
uint8_t lampState = 0; //The true lampState is stored in the lamp object on the local node, and is sent back to the primary node after a state change and stored here



/*
$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
Class Definitions
TODO: Write header file and move lcd class definitions to their own file
$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
*/

 

//Naming note: configLCD refers to the LCD that will be used by the end user (me)  to configure various values
//Ideas for how the config LCD will work:
//Array of page objects that you cycle through
//Move cursor to line to interact with that data (if applicable)

class ConfigLCDPage {
  private:
    static const uint8_t PAGE_TITLE_LEN = 17;
    //static const uint8_t FIELD_TITLE_LEN 12; //not currently used, maybe should be used to enforce max length?
    uint8_t _pageNumber;
    String _pageTitle = "NULLTITLE";
    String _fieldTitleArr[3] = {"", "", ""};
    uint8_t _fieldDataStartArr[3] = {0, 0, 0}; //This is the first column on each line after the title that is valid to print data
    int16_t _fieldData16[3] = {0, 0, 0};
    int16_t _fieldData16_min[3] = {0, 0, 0};
    int16_t _fieldData16_max[3] = {0, 0, 0};
    //String _fieldDataStr[3];
    uint8_t _userCursorLine = 0; //Track the line that the user has moved the cursor to, reference this to update values in response to input
    bool _isEditable = true; //Flag to lock edit functionality on pages with read-only data (eg clock display) 


  public:
    //ConfigLCDPage(uint8_t pageNumber); //Constructor

    uint16_t tempData = 0; //Value to hold the temporary fieldData value while it is being edited and before it is confirmed

    void setPageTitle(String pagetitle){
      _pageTitle = pagetitle;
    }

    //Let the ConfigLCD class use this to automatically assign the page number
    void setPageNumber(uint8_t pageNumber){
      _pageNumber = pageNumber;
    }

    void setFieldTitle(String fieldtitle, uint8_t row){
      //Add the title to the array of titles
      _fieldTitleArr[row-1] = fieldtitle;
      //Add 1 space for padding after the field title
      _fieldDataStartArr[row-1] = (1 + fieldtitle.length());
    }

    //Set 16 bit values to the data fields
    //minData and maxData are the ends of the range of valid values for fieldData
    //TODO: function overloading if need be for strings
    bool initFieldData(int8_t fieldNumber, int16_t fieldData, int16_t minData, int16_t maxData){
      if((fieldNumber < 1) || (fieldNumber > 3)){
        Serial.println("Invalid field number");
        return false;
      }
      if((fieldData < minData) || (fieldData > maxData)){
        Serial.print("Error fieldData for fieldNumber:");
        Serial.println(fieldNumber);
        Serial.println("fieldData is out of the range that YOU provided");
        return false;
      }

      _fieldData16[fieldNumber-1] = fieldData;
      _fieldData16_min[fieldNumber-1] = minData;
      _fieldData16_max[fieldNumber-1] = maxData;

      return true;
    }

    //Updates the field data value without changing the range min/max bounds
    bool setFieldData(int8_t fieldNumber, int16_t fieldData){
      //First check that the data field has been properly initialized
      if(_fieldData16[fieldNumber - 1] == 0 && _fieldData16_min[fieldNumber - 1] == 0 && _fieldData16_max[fieldNumber - 1] == 0){
        Serial.print("Please use initFieldData and specify the data range before updating the data value");
        return false;
      }
      //Check if the provided data is valid in the given range
      if((fieldData < _fieldData16_min[fieldNumber - 1]) || (fieldData > _fieldData16_max[fieldNumber - 1])){
        Serial.print("setFieldData: Data value out of bounds for field ");
        Serial.println(fieldNumber);
        return false;
      }
      //Update the data value
      else{
        _fieldData16[fieldNumber - 1] = fieldData;
        return true;
      }

    }

    //Returns the fieldData for a specified fieldNumber
    uint16_t getFieldData(uint8_t fieldNumber){
      return _fieldData16[fieldNumber - 1];
    }

    uint16_t getFieldDataMin(uint8_t fieldNumber){
      return _fieldData16_min[fieldNumber - 1];
    }

    uint16_t getFieldDataMax(uint8_t fieldNumber){
      return _fieldData16_max[fieldNumber - 1];
    }

    //Returns the line the the user has moved the cursor to
    uint8_t getUserCursorLine(){
      return _userCursorLine;
    }

    //Move the cursor down 1 line of the LCD (wraps around from bottom to top), meant to be triggered by user input
    void moveCursor(){
      if(_userCursorLine == 3){ //Loop around case
        _userCursorLine = 0;
        lcd.setCursor(19, _userCursorLine);
      }
      else if((_userCursorLine >= 0) && (_userCursorLine < 3)){ //Valid case
        _userCursorLine++;
        lcd.setCursor(19, _userCursorLine);
      }
      else if ((_userCursorLine >= 4) || (_userCursorLine < 0)){ //Invalid Case
        Serial.println("ConfigLCDPage.moveCursor: _userCursorLine was out of bounds");
        _userCursorLine = 0;
        lcd.setCursor(19, _userCursorLine);
      }
    }

    //Prints all 3 of the fieldData values to the LCD -- use only with drawPage, no protection for userCursorLine state
    void printFieldData(){
      for(int i = 0; i < 3; i++){
        //Set the cursor to the first safe column to write data without overwriting the field titles
        lcd.setCursor(_fieldDataStartArr[i], i+1);
        lcd.print(_fieldData16[i]);
      }
    }

    //Re-print just one of the data fields, for use when the user is setting a value with the analog input
    void printFieldDatum(uint8_t fieldNumber){
      //Set the cursor to the first safe column to write data without overwriting the field titles
      lcd.setCursor(_fieldDataStartArr[fieldNumber - 1], fieldNumber);
      lcd.print(_fieldData16[fieldNumber - 1]);

      lcd.setCursor(18, _userCursorLine); //Move the cursor back to where it was before this function was called
    }

    //Prints the tempData, for use while it is being adjusted
    void printTempFieldData(uint8_t fieldNumber){
      lcd.setCursor(_fieldDataStartArr[fieldNumber - 1], fieldNumber);
      lcd.print(tempData);
      lcd.setCursor(18, _userCursorLine);
    }

    //Synthesize all of the provided page data and display it on the LCD
    void drawPage(){
      //Clear whatever is currently being displayed
      lcd.clear();
      _userCursorLine = 0; //lcd.clear resets the cursor position
      //Print page title and page number in the first line
      lcd.setCursor(0,0);
      lcd.print(_pageTitle);
      lcd.setCursor(PAGE_TITLE_LEN,0);
      lcd.print("P");
      lcd.setCursor(18,0);
      lcd.print(_pageNumber);

      //Print the field titles
      for(int i = 0; i < 3; i++){
        lcd.setCursor(0, i+1);
        lcd.print(_fieldTitleArr[i]);
      }

      //Print the field data
      printFieldData();

      //Place the cursor right before the page number, as a default position
      lcd.setCursor(17,0);
      _userCursorLine = 0;

    }

};

//The ConfigLCD class will be the object representation of the real-life configLCD
//Its primary purpose is to organize and manage the ConfigLCDPage objects, and handle user input
//Functionaly for ancillary LCD functions (cursor blink, backlight) could also be included
class ConfigLCD {
  private:
    static const uint8_t  _TOTALPAGES = 3; //The total number of pages that can be stored, change if more are needed
    ConfigLCDPage* _pages_p[_TOTALPAGES];
    //ConfigLCDPage _pages[_TOTALPAGES]; //This will store the address of all the ConfigLCDPage objects to make it easier to iterate through the pages
    uint8_t _pageCount = 0; //Total number of pages stored in the _pages_p array
    uint8_t _currentPage = 1; //Stores the page number of the currently displayed configLCD page
    uint8_t _currentEditLine = 0; //Stores the line with data that is being editing, and needs to be updated on the LCD while the edit is in progress
    bool _currentlyEditingData = false;

  public:
    //Put the LCD setup/initialization in the constructor?
    //ConfigLCD(address, lines, columns){
    //  backlight, init    
    //}


    //This is quickly becoming indecipherable, stop the bleeding asap
    bool addPage(ConfigLCDPage *page_p){
      if(_pageCount >= _TOTALPAGES){
        Serial.println("ConfigLCD: page limit reached, cannot add new page");
        return false;
      }
      _pages_p[_pageCount] = page_p;
      //_pages[_pageCount] = *page_p; //Here i go dereferencing again

      //Page numbers are not 0-indexed, so the display page number is always 1 higher than the index
      _pageCount++;
      _pages_p[_pageCount - 1]->setPageNumber(_pageCount);

      Serial.println("Page added");
      return true;
    }

    //It feels like there should be some provision for deleting pages but I don't know where I would need that so I'll just leave this comment instead
    //void deletePage(uint8_t pageNumber){}

    //Display the next page
    bool nextPage(){
      if ((_currentPage < 1) || (_currentPage > _TOTALPAGES)){ //Case for if somehow this data is invalid
        Serial.println("ConfigLCD: _currentPage has invalid value (out of range)");
        //Reset to page 1
        _currentPage = 1;
        _pages_p[_currentPage - 1]->drawPage();
        return false;
      }

      if(_currentPage == _TOTALPAGES){ //Case for being on the last page, want to loop around to the beginning
        _currentPage = 1;
        _pages_p[_currentPage - 1]->drawPage();
        return true;
      }

      if(_currentPage < _TOTALPAGES){ //Increment the page
        _currentPage++;
        _pages_p[_currentPage - 1]->drawPage();
        return true;
      }
      else{
        Serial.println("this should not be possible");
        return false;
      }
    }

    //This feels like this isn't the best way to go about doing this, like it seems inefficient but whatever
    uint16_t computeAnalogDataValue(){
      //check user cursor line
      //uint8_t cursorLine = _pages_p[_currentPage - 1].getUserCursorLine();

      //get the min/max for the field data stored on that line
      uint16_t dataMin = _pages_p[_currentPage - 1]->getFieldDataMin(_currentEditLine);
      uint16_t dataMax = _pages_p[_currentPage - 1]->getFieldDataMax(_currentEditLine);

      //get the analog input value (0-1023)
      uint16_t rawInput = analogRead(ANALOG_LCD_PIN);

      //map the analog input value to the valid fieldData range
      uint16_t mappedOutput = uint16_t(map(rawInput, 0, 1023, dataMin, dataMax));

      Serial.println(mappedOutput);

      return mappedOutput;
    }


    void inputActionEnter(){
      //static bool currentlyEditingData = false;
      //static uint8_t currentlyEditingLine = 0; //You can't edit line 0, so that will be the "null" value for this variable
      Serial.println("Enter input registered");
      uint8_t cursorLine = _pages_p[_currentPage - 1]->getUserCursorLine();

      //If the cursor is the title line, go to the next page
      if (cursorLine == 0) {
        nextPage();
      }

      //If the cursor is on a data line, mark that line as the line currently being edited
      else if ((cursorLine > 0) && (cursorLine <= 3)){ //If the cursor is on a fieldData line
        if ((_currentlyEditingData == false) && (_currentEditLine == 0)){
          _currentlyEditingData = true;
          _currentEditLine = cursorLine;

          //from here we need to compute the analog data value, continuously update it on the LCD, but only update the fieldData variable upon a second enter press
        }
        //Confirmation of the altered data
        else if ((_currentlyEditingData == true) && (_currentEditLine == cursorLine)){
          _pages_p[_currentPage - 1]->setFieldData(_currentEditLine, _pages_p[_currentPage - 1]->tempData); //Write the temp data to the actual fieldData variable
          
          //Reset Flags
          _currentlyEditingData = false;
          _currentEditLine = 0;
          _pages_p[_currentPage - 1]->tempData = 0;
        }
      }

      //When you press the enter button, you might want to do the following:
      //Go to the next page (cursor is on line 0)
      //Edit a data field (cursor is on line 1-3)
      //Confirm edited data (cursor is on line 1-3, already selected the line)
    }

    void inputActionScroll(){
      //When you press the scroll button, you might want to do the following:
      //Move the cursor down to the next line
      //Escape from editing a fieldData value (by moving the cursor down to the next line)
      Serial.println("Scroll input registered");

      if((_currentlyEditingData == true)){
        _currentlyEditingData = false;
        _pages_p[_currentPage - 1]->printFieldDatum(_currentEditLine); //Reset the displayed data to the stored value
        _currentEditLine = 0;
      }

      _pages_p[_currentPage - 1]->moveCursor();
    }

    //Method to update a data value that is being edited. This method should be placed in the main loop and needs to be check every loop.
    bool updateFieldDataDisplayWhileEditing(){
      if (_currentlyEditingData == false){
        return false;
      }
      if (_currentEditLine == 0){
        return false;
      }

      if ((_currentEditLine > 0) && (_currentEditLine <= 3)){
        Serial.println("checking analog value");
        _pages_p[_currentPage - 1]->tempData = computeAnalogDataValue();
        _pages_p[_currentPage - 1]->printTempFieldData(_currentEditLine);
      }

      return true;


    }


};

/*
$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
Setup
$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
*/

//Object Declaration that need to be in the global scope, and after the class definitions (I know i could move the class definition above to the other section that I have for global variables but then I would not be motivated to do the better solution which is to have those definitions in a separate file so instead of taking 200+ lines in the main file and making me scroll past them every time I just want to get to the setup function, let alone and CAN stuff or what have you, it would be
//a single include statement)
//Seriously though right now the setup function starts on line 362 and I'm not even done with the ConfigLCD class yet, kinda silly
//Initialize the pages on the configLCD
ConfigLCDPage page1;
ConfigLCDPage page2;
ConfigLCDPage page3;
//Instantiate the configuration LCD display object
ConfigLCD mainDisplay;


void setup() {
  //open serial port
  Serial.begin(BAUD_RATE);
  while(!Serial){
    ;
  }
  Serial.println("Serial Init Complete");

  //set up can bus
  ACAN_T4_Settings settings(CAN_BITRATE);
  const uint32_t errorCode = ACAN_T4::can1.begin(settings);

  if(errorCode == 0){
    Serial.println("can1 Init Complete");
  }
  else{
    Serial.print("Error can1: 0x");
    Serial.println(errorCode, HEX);
  }

  //RTC Setup
  setSyncProvider(requestSync);  //set function to call when sync required
  Serial.println("Waiting for sync message");

  //Init RTC
  while(1){
    syncRTC();
    if (now() > 1700000000){ //Nov 14 2023 unix timestamp, if it greater than this it hasn't reset completely
    //whether or not it's correct is left as an exercise to the reader
      Serial.println("RTC Init Complete");
      break;
    } 
  }

  //Define data direction for config LCD input pins
  pinMode(ANALOG_LCD_PIN, OUTPUT);
  pinMode(SCROLL_LCD_PIN, OUTPUT);
  pinMode(ENTER_LCD_PIN, OUTPUT);

  lcd.init();
  lcd.backlight();





  page1.setPageTitle("Big Long Title");
  page1.setPageNumber(1);
  page1.setFieldTitle("hello:", 1);
  page1.initFieldData(1, 69, 0, 100);
  page1.setFieldTitle("more data:", 2);
  page1.initFieldData(2, 100, 99, 101);
  page1.setFieldTitle("toodaloo:", 3);
  page1.initFieldData(3, 30000, 1, 31000);
  //page1.drawPage();

  page2.setPageTitle("page 2");
  page3.setPageTitle("page3");

  //Pages need to be added after they are configured, probably need to use pointers in the methods to set data in ConfigLCDPage in order to update later?
  mainDisplay.addPage(&page1);
  mainDisplay.addPage(&page2);
  mainDisplay.addPage(&page3);


  //lcd.setCursor(18,0);
  //lcd.cursor_on();
  lcd.blink(); 


  //Initialize alarms, only set alarms after the RTC is initialized with the correct time
  //Set the time alarm to switch the long or short work week flag on Saturdays at 11:59:59 pm
  Alarm.alarmRepeat(dowSaturday, 23, 59, 59, toggleisLongWorkWeekFlag);
  //Set an alarm at the beginning of every day to schedule that day's events every day at 12:01:00 am
  Alarm.alarmRepeat(0, 1, 0, alarmScheduler);

  //Delete this
  debugAlarms();

}

/*
$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
LCD Object Setup (In the global scope)
$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
*/






//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//MAIN LOOP
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~



//the node controller will send back the state of the lamp whenver it changes
bool timerFlag = true;
static const uint8_t digitalInputLockoutTime = 200; //Used to reject repeated digital inputs, in milliseconds
uint32_t lockoutScrollTime = millis();
uint32_t lockoutEnterTime = millis();

void loop() {
  
  
  uint32_t loopStartTime = micros();

  //Check the state of each digital input
  uint8_t startScrollInputState = digitalRead(SCROLL_LCD_PIN);
  uint8_t startEnterInputState = digitalRead(ENTER_LCD_PIN);


  //Check if a RTC sync message is waiting in the serial buffer
  syncRTC();

  //Check the CAN bus message buffer to see if there is anything to do.
  checkCANMessageBuffer();

  //Config LCD Interaction
  //Move the cursor around, when you press enter, get the analog input and map it to that line's data range, constantly update the display
  //You can scroll away and the changes are discarded, ie value is not written to the fieldData variable, display the original value
  //When you press enter, the new value is stored in the fieldData variable and the dispaly is updated
  //TODO move this functionality to a dedicated controller for the LCD

  //This needs to be called every loop so the data on the screen will update live if it is being edited
  mainDisplay.updateFieldDataDisplayWhileEditing();

  if(digitalRead(SCROLL_LCD_PIN)){
    if((millis() - lockoutScrollTime) < digitalInputLockoutTime){
      //Serial.println("Scroll input locked");
      ;
    }
    else{
      lockoutScrollTime = millis();
      //Serial.println("Scroll input seen in main loop");
      mainDisplay.inputActionScroll();
    }
  }

  if(digitalRead(ENTER_LCD_PIN)){
    if((millis() - lockoutEnterTime) < digitalInputLockoutTime){
      //Serial.println("Enter input locked");
      ;
    }
    else{
      lockoutEnterTime = millis();
      //Serial.println("Enter input seen in main loop");
      mainDisplay.inputActionEnter();
    }
  }
  
  //This Alarm.delay(0) (or any other value for the argument) is required for the alarms to trigger.
  //The conditions for the alarms are only checked during an Alarm.delay()
  Alarm.delay(0);

  if(timerFlag){
    Serial.print("Main loop time: ");
    Serial.println(micros() - loopStartTime);
    timerFlag = false;
  }



}



//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//END OF MAIN LOOP
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~



/*
$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
Lamp Functions
$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
*/

void toggleisLongWorkWeekFlag(){
  isLongWorkWeek = !isLongWorkWeek;
}

void alarmScheduler(){
  int day = weekday();

  //use the enum dowWeekday format to make this more readable
  if(isLongWorkWeek){
    if(day == 1 || day == 2 || day == 5 || day == 6){
      lampAlarmSchedule();
    }
  }
  else if(!isLongWorkWeek){
    if(day == 3 || day == 4 || day == 7){
      lampAlarmSchedule();
    }
  }

  Serial.println("Alarm set");
  digitalClockDisplay();
}

void lampAlarmSchedule(){
  Alarm.alarmOnce(5, 5, 0, turnOnLamp); //Send command to turn on lamp at 5:05 am
  Alarm.alarmOnce(6, 20, 0, turnOffLamp); //Send command to turn off lamp at 6:20 am

}

//FOR DEBUGGING PURPOSES ONLY
void debugAlarms(){
  //DEBUG ONLY
  Alarm.alarmOnce(20, 3, 30, turnOnLamp);
  Alarm.alarmOnce(20, 4, 30, turnOffLamp);
}

//Sends a CAN bus message to the lamp node controller to command it to the desired state.
//After this occurs, the lamp node controller will echo back the state of the lamp on the can bus
void turnOnLamp(){
  //Call the sendCANMessage function with the data as argument
  uint8_t messageData[8] = {lampState, 0xFF};
  sendCANFrame(PRIMARY_NODE_ID, 2, messageData);
  
  Serial.println("Command issued to turn lamp ON");
  digitalClockDisplay();
}

void turnOffLamp(){
  //Call the sendCANMessage function with the data as argument
  uint8_t messageData[8] = {lampState, 0x11};
  sendCANFrame(PRIMARY_NODE_ID, 2, messageData);
  
  Serial.println("Command issued to turn lamp OFF");
  digitalClockDisplay();
}



/*
$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
CAN Land
$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
*/



bool sendCANFrame(uint16_t messageID, uint8_t messageLen, uint8_t messageData[8]){
  //Return true/false is succesful?
  if(messageID > 0x7FF){
    Serial.println("Message ID out of range");
  }
  CANMessage message;
  message.id = messageID;
  message.len = messageLen;

  for(int i = 0; i < messageLen; i++){
    message.data[i] = messageData[i];
  }

  const bool ok = ACAN_T4::can1.tryToSend(message);
  if(ok){
    Serial.println("Frame sent");
    return true;
  }
  else{
    Serial.println("Frame send failure");
    return false;
  }
}

bool receiveCANFrame(){
  CANMessage incomingFrame; //Instantiate the CANMessage object 
  uint8_t incomingFrameData[8]; //Array to store the up to 8 data bytes in a CAN frame
  ACAN_T4::can1.receive(incomingFrame); //Store the next message in the CAN1 controller buffer in the CANMessage object
  
  //Extract the data bytes from the message and store in the local array
  for(int i = 0; i < incomingFrame.len; i++){
    incomingFrameData[i] = incomingFrame.data[i];
  }

  //Condition for recieving a frame from the lamp node
  if(incomingFrame.id == LAMP_NODE_ID){
    Serial.println("Frame from lamp node received");
    lampState = incomingFrameData[0]; //Store the on/off state of the lamp as reported by the lamp node into a local variable
  
    //Debug frame info
    Serial.print("ID: ");
    Serial.println(incomingFrame.id);
    Serial.print("Len: ");
    Serial.println(incomingFrame.len);
    Serial.println("Data: ");  
    Serial.println(incomingFrame.data[0]);
    Serial.println(incomingFrame.data[1]);
    Serial.println(incomingFrame.data[2]);
    Serial.println(incomingFrame.data[3]);
    Serial.println(incomingFrame.data[4]);
    Serial.println(incomingFrame.data[5]);
    Serial.println(incomingFrame.data[6]);
    Serial.println(incomingFrame.data[7]);

    Serial.print("Lamp State: ");
    Serial.println(lampState);


    return true;
  }

  return false;
}

//Read the CAN message buffer and call receiveCANFrame is there is a message waiting
void checkCANMessageBuffer(){
  if(ACAN_T4::can1.available()){
    bool ok = receiveCANFrame();
    if(ok){
      Serial.println("Frame reception success");
    }
    else{
      Serial.println("Frame reception error");
    }
  }
}



/*
$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
Real Time Clock & Timekeeping Functions
$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$$
*/



void syncRTC(){
  if (Serial.available()) {
    processSyncMessage();
    digitalClockDisplay();
  }
}

void processSyncMessage() {
  unsigned long pctime;
  const unsigned long DEFAULT_TIME = 1357041600; // Jan 1 2013

  if(Serial.find(TIME_HEADER)) {
     pctime = Serial.parseInt();
     if( pctime >= DEFAULT_TIME) { // check the integer is a valid time (greater than Jan 1 2013)
       setTime(pctime); // Sync Arduino clock to the time received on the serial port
     }
  }
}

time_t requestSync()
{
  Serial.write(TIME_REQUEST);  
  return 0; // the time will be sent later in response to serial mesg
}

//Serial colock output
void digitalClockDisplay(){
  // digital clock display of the time
  Serial.print(hour());
  printDigits(minute());
  printDigits(second());
  Serial.print(" ");
  Serial.print(day());
  Serial.print(" ");
  Serial.print(month());
  Serial.print(" ");
  Serial.print(year()); 
  Serial.println(); 
}

void printDigits(int digits){
  // utility function for digital clock display: prints preceding colon and leading 0
  Serial.print(":");
  if(digits < 10)
    Serial.print('0');
  Serial.print(digits);
}