 /* "Oxygen Manipulation Machine" 
 *  
 * Coded for ARDUINO MEGA 2560 with Adafruit Data Logging Shield (w/ SD card, Real Time Clock (RTC), and LCD output)
 *  
 * Measures dissolved oxygen through an Atlas Scientific DO circuit and probe, compares averaged DO to planned DO and 
 * allows the solenoid to open to allow oxygenated water into tank if DO is too low. Logs data to SD card and displays 
 * average and planned DO on LCD. Communication with DO circuit in I2C. 
 * 
 * Data logged to SD: date and time via the RTC, the instantaneous DO, a running average of the last few DO readings, 
 * the "planned" DO for that time and whether or not the valve was opened.
 * 
 * Before uploading, check that the correct planned pattern is active in check_DO function. The pushbutton must be  
 * pressed once to generate a new SD file, then again to begin oxygen manipulation.
 * 
 * Created          By: Kara Gadeken
 *                  Date: 28 Jul 2018
 * 
 * Last Modified    By: Kara Gadeken
 *                  Date: 08 Jan 2021          
 */
//----------------------------------------------------- LIBRARIES ---------------------------------------------------
#include <Time.h>   
#include <TimeLib.h>            
#include <SoftwareSerial.h>
#include <SPI.h>
#include <SD.h>                         //NOTE: some libraries may need to be installed; error "no such file or 
#include <Wire.h>                          //directory" indicates missing library
#include <RTClib.h> 
#include <math.h>
#include <stdlib.h>
#include <LiquidCrystal_PCF8574.h> 

//----------------------------------------------- OBJECTS AND ADDRESSES ---------------------------------------------
#define circuit1 97                     //defines address of the probe
unsigned long wait = 55000UL;           //time between loops (need 'UL', otherwise arduino can't count that high)
char realDO[20];                        //create char w/ 20 bytes to hold DO value recieved from the circuit
const int chipSelect = 10;              //defines pin 10 as pin that arduino uses to enable and disable SD                                
File logfile;                           //creates file
char filename[] = "Logger00.csv";       //names file will be writing to
RTC_PCF8523 rtc;                        //names version of RTClib being used
String valvestate = "";                 //string to hold an indicator of whether the solenoid was opened or not
LiquidCrystal_PCF8574 lcd(0x3F);        //names and adresses LCD

const int FLOWpin = 8;                  //defines pin 8 as pin that sends a signal to the relay
const int Button = 2;                   //defines pin 2 as connected to pushbutton that triggers start of void loop
const int LED_SD = 3;                   //defines pin 3 as an LED (pin 3 is connected to built-in LED on SD card)

//objects to hold current time values in check_DO function
float now_hr; 
float now_min;
float now_minDEC;
float now_time;
float now_plannedDO;

float time_max;

//AvgCalc function objects (From "https://www.arduino.cc/en/Tutorial/Smoothing)
int j = 0;                     //counter for number of loops, used in "if" statement in fix_DO() function so DO  
                                 //manipulation doesn't start until several measurements have been taken and averaged
float fl_realDO;               //float to hold DO value converted from realDO (can't do math using char datatype)
const int numReadings = 5;     //number of readings that will be saved and averaged
float readings[numReadings];   //float array to hold readings that will be averaged (comes from fl_realDO generated in each loop)
int readIndex = 0;             //the index of the current reading (set to 0 for initial reading)
float total = 0;               //float to hold the running total of all readings, to be divided by numReading
float avg;                     //float to hold the calculated average of the past few readings, to be compared to plannedDO

//------------------------------------------------------- SETUP -----------------------------------------------------
void setup() {
  Serial.begin(9600);          //opens serial communication
  Wire.begin();                //start I2C, default SDA/SCL on MEGA are pins 4 and 5
  pinMode(Button, INPUT);      //sets up button used to start and restart code
  pinMode(LED_SD, OUTPUT);     //sets up onboard SD LED for signaling
  
  while(!Serial){              //wait for serial port to connect before proceeding
   ;
  }
//------- RTC Setup -------- (https://learn.adafruit.com/adafruit-data-logger-shield/using-the-real-time-clock)
  rtc.begin();                                  //starting the real time clock...
  if(!rtc.begin()){                             //checking to see if rtc started...
    Serial.println("Couldn't find RTC");        //nope, didn't start, OR...
    while(1);
  }
  else if(!rtc.initialized()){
    Serial.println("RTC NOT running");       //isn't running right
  }
  else {
    Serial.println("RTC is running");
  }

// rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));   //Uncomment this line to adjust the time; if RTC is wrong, this will set date 
                                                       //and time to current as of script compilation, according to the computer's 
                                                       //clock (SAVE SCRIPT BEFORE SETTING TIME)
 
    while(!digitalRead(Button)){}     //pauses code and waits until button has been pushed before creating new SD file...this is a
                                        //useful "checkpoint" so that if this is the uploaded script when the system is powered up but
                                        //you still need to calibrate the probe, it won't uselessly make a new empty file each time
   
    digitalWrite(LED_SD, HIGH);       //signal with onboard SD LED that button was pushed and it's about to make a new SD file
    delay(1000);                          
    digitalWrite(LED_SD, LOW);           
    delay(500);                  
    digitalWrite(LED_SD, HIGH);
    delay(1000);
    digitalWrite(LED_SD, LOW);


  //--------- SD Card Setup -------- (https://learn.adafruit.com/adafruit-data-logger-shield/using-the-sd-card)
  Serial.print("Initializing SD card...");    
    if(!SD.begin(chipSelect)){                          //checks that communication with SD card is working...
      Serial.println("SD failed, or not present");      //nope, didn't work, OR...
    }
    else {
      Serial.println("SD initialized");                 //yay, it worked!
    }
  
  Serial.print("Creating new file...");
  for (byte i = 0; i < 100; i++) {                      //check files on the SD card, counting up from LOGGER00, LOGGER01, etc.
    filename[6] = i/10 + '0';  
    filename[7] = i%10 + '0';
    if (!SD.exists(filename)) {                         //only open a new file if one with that name doesn't exist already
      logfile = SD.open(filename, FILE_WRITE); 
    break;     
    }
  }  

  logfile = SD.open(filename, FILE_WRITE);           //try opening newly created and named file
    if (!logfile) {                                  //logfile wouldn't open, OR...
      Serial.println("Couldn't create file");
    }
    else {
      Serial.print("Logging to: ");                  //Logfile was sucessfully opened, print name of new file
      Serial.println(filename);
      String headerString = "year,month,day,hour,minute,second,DO_real,DO_avg,DO_planned,valvestate";    //create column headers
      logfile.println(headerString);                                                          //add headers as first line in new file
      logfile.close();
    }     
  //--------- Other Setup -----------
  pinMode(FLOWpin,OUTPUT);
  digitalWrite(FLOWpin,LOW);                //sets relay initial condition as off   

  lcd.begin(16,2);                          //sets up LCD screen
  lcd.setBacklight(255);
  lcd.clear();
  lcd.setCursor(0,0);

  for (int thisReading = 0; thisReading < numReadings; thisReading++) {   //initializes all values in readings array to 0
    readings[thisReading] = 0;
    }
    Serial.println("-----------------------------------------------");    //dashed line to delineate the setup from the loop in serial monitor (purely aestetic)
    while(!digitalRead(Button)){}  //pauses code at end of setup and waits until button has been pushed before starting void loop                         
    
}                                                   

//------------------------------------------------------ LOOP -----------------------------------------------------
void loop() {
  String dataString = "";                       //create an empty data string to fill with data that will be logged to SD

                                                //void functions (below)
  execute_circuit(circuit1);                      //go get DO value from probe, at address circuit1
  check_DO();                                     //calculate what the DO should be at that time
  AvgCalc_DO();                                   //calculate smoothing average
  fix_DO();                                       //compare and try to adjust DO
  
  DateTime now = rtc.now();                     //calls the object "now" with the date&time from the RTC at that moment
  dataString += now.year();                     //appends year to dataString
  dataString += ",";                            //adds comma so file has comma separated values
  dataString += now.month();
  dataString += ","; 
  dataString += now.day();
  dataString += ","; 
  dataString += now.hour();
  dataString += ","; 
  dataString += now.minute();
  dataString += ","; 
  dataString += now.second();
  dataString += ","; 
  dataString += realDO;                           //append value from circuit to dataString
  dataString += ","; 
  dataString += avg;                              //append averaged DO to dataString
  dataString += ",";
  dataString += now_plannedDO;                    //append plannedDO to dataString
  dataString += ",";
  dataString += valvestate;                       //append valvestate to dataString

  logfile = SD.open(filename, FILE_WRITE);        //open file
    if (!logfile) {
      Serial.println("Error opening file");       //file didn't open, so print an 'oops' message
    }
  logfile.println(dataString);                    //write the contents of the string to next line in the file
  logfile.close();

    Serial.print("(");                   //also print date, time, real DO, avg DO and planned DO to the serial monitor
    Serial.print(now.year(), DEC);
    Serial.print('/');
    Serial.print(now.month(), DEC);
    Serial.print('/');
    Serial.print(now.day(), DEC);
    Serial.print(") ");
    Serial.print(now.hour(), DEC);
    Serial.print(':');
    Serial.print(now.minute(), DEC);
    Serial.print(':');
    Serial.println(now.second(), DEC);
    
    Serial.print("nowDO = ");
    Serial.print(realDO);
    Serial.print(' ');
    Serial.print("Avg = ");
    Serial.print(avg);
    Serial.print(' ');
    Serial.print("plannedDO = ");
    Serial.println(now_plannedDO);                
    Serial.println();

  LCD_print();

  delay(wait);           //wait a bit before running loop again, give time for O2 water to mix and sensor to respond
}



//------------------------------------------------- RETRIEVE DO VALUE ---------------------------------------------
void execute_circuit(byte circuit) {                  
  byte i = 0;                         //set byte counter to 0
  Wire.beginTransmission(circuit);    //call circuit
    delay(20);
    Wire.write('r');                  //ask for value
    Wire.endTransmission();           //end communication
    delay(1500);                      //wait a bit
  Wire.requestFrom(circuit,20,1);     //call circuit and ask for 20 bytes of data
    delay(20);
    char c=Wire.read();               //read 1st byte the circuit sends separately, since this is the response code
    while(Wire.available()){          
      byte in_char=Wire.read();       //read bytes being sent from circuit
      realDO[i]=in_char;              //load into data array
      i+=1;                           //and for each byte add 1 to our element counter
      if(in_char==0){                 //if encounter a null command (ASCII 0)
        Wire.endTransmission();       //end I2C communication
        break;                        //and end the while loop
      }
     }
     j+=1;               //add one to measurement counter
 }
//------------------------------------------------- FIND PLANNED DO ----------------------------------------------
void check_DO() {
  DateTime now = rtc.now();          //calls the object "now" with the DandT information from the RTC at that moment
  now_hr = now.hour();               //populates hour float point
  now_min = now.minute();            //populates minute float point
  now_minDEC = now_min/60;           //convert minutes into decimal proportion of an hour
  now_time = now_hr + now_minDEC;    //populates decimal elapsed time from midnight

//planned DO for current time (make sure ONLY ONE now_plannedDO is uncommented at a time, or it will overwrite!)
  //COMMENT/UNCOMMENT TO SWITCH PATTERN, OR MAKE YOUR OWN
    now_plannedDO = 7.00;         //SUSTAINED HIGH
    //now_plannedDO = 2.00;        //SUSTAINED LOW
    //now_plannedDO = 5.00;

  //COMMENT/UNCOMMENT NEXT TWO LINES TOGETHER FOR DIEL CYCLE 
    //time_max = 0;    //time in decimal hours that you want the diel pattern to be at max DO (ex: 5:30 PM would be 17.5)
    //now_plannedDO = (2*cos((now_time-time_max)/3.8)) + 5;  //DIEL PATTERN (max 7 mg/L, min 3 mg/L)
}   
//--------------------------------------------- CALCULATE SMOOTHING AVERAGE ------------------------------------------------
void AvgCalc_DO() {
  fl_realDO = atof(realDO);              //first convert "realDO" to a floating-point number, b/c can't compare a char to a float
  total = total - readings[readIndex];   //subtract last reading from total
  readings[readIndex] = fl_realDO;       //plug new reading in to array
  Serial.print("Readings = ");
  Serial.print(readings[0]);
  Serial.print(" ");
  Serial.print(readings[1]);
  Serial.print(" ");
  Serial.print(readings[2]);
  Serial.print(" ");
  Serial.print(readings[3]);
  Serial.print(" ");
  Serial.println(readings[4]);
  total = total + readings[readIndex];   //add reading to total
  readIndex = readIndex + 1;             //advance to next position in array
  if(readIndex >= numReadings) {         // if we're at the end of the array...
    readIndex = 0;                       //...wrap back around to the beginning
  }
  avg = total/numReadings;               //calculate the average
}
//-------------------------------------------------- MODULATE FLOW ---------------------------------------------------------
void fix_DO() {  
  if (avg<now_plannedDO && j>=numReadings) {   //if avg DO over last few readings is less than planned DO AND it is not one of the first few readings
  digitalWrite(FLOWpin, HIGH);        //Open solenoid
  delay(5000);                       //Keep open
  digitalWrite(FLOWpin, LOW);         //Close solenoid
  delay(10);
  valvestate = "y";
  delay(10);
  }
  else {                        //if avg DO is greater than planned DO, wait a bit before proceeding (so each loop is about the same duration)
  valvestate = "n";
    delay(5000);
  }
}
//-------------------------------------------------- PRINT TO LCD -------------------------------------------------
void LCD_print(){
  lcd.setCursor(0,0);
  lcd.print("Avg:");
  lcd.print(avg);
  lcd.setCursor(0,1);
  lcd.print("Plan:");
  lcd.print(now_plannedDO);
}
