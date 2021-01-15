#include <SoftwareSerial.h> //Software serial library
#include <Wire.h>   //I2C library, for calling and bossing around secondary devices

#define circuit1 97      //set addresses of DO circuits

int circuit = circuit1;

char PC_data[20];         //20 byte character array to hold incoming data from the PC
byte from_PC = 0;         //counter for bytes from PC
char c = 0;            //holds I2C response code
char data[20];         //20 byte character array to hold incoming data from the circuit
byte i = 0;               //counter for DO_data array
byte in_char;             //1 byte buffer to store inbound bytes from the circuit
int time_=1800;           //delay to give circuit time to take a reading

void setup() {
Serial.begin(9600);         //set hardware baud rate to 9600 (for communication between arduino and PC)
Wire.begin();               //initiate I2C library
}

void loop() {
  if(Serial.available() > 0) {                          //if data is being held in the serial buffer (from the PC)
   from_PC = Serial.readBytesUntil(13, PC_data, 20);    //read those bytes (which are counted by from_PC) into PC_data array, until <CR>
   PC_data[from_PC] = 0;                                //stop the buffer from transmitting leftovers or garbage

  Wire.beginTransmission(circuit);                      //call the circuit
  Wire.write(PC_data);                                   //transmit the command sent through the serial port
  Wire.endTransmission();                                  //end I2C data transmission

  delay(time_);                                       //wait for the curcuit to perform the command

  Wire.requestFrom(circuit, 20, 1);                    //call the circuit and request 20 bytes
  delay(20);
  char c=Wire.read();                         //first byte is the response code (read separately) to tell us if we got a read from the circuit

 if(PC_data[0] == 'c') {
   switch (c) {                   //switch case based on what the response code is
      case 1:                         //decimal 1
        Serial.println("Success");    //means the command was successful
        break;                        //exits the switch case

      case 2:                         //decimal 2
        Serial.println("Failed");     //means the command has failed
        break;                        //exits the switch case

      case 254:                      //decimal 254
        Serial.println("Pending");   //means the command has not yet been finished calculating
        break;                       //exits the switch case

      case 255:                      //decimal 255
        Serial.println("No Data");   //means there is no further data to send
        break;                       //exits the switch case
    }
 }

  while(Wire.available()){                   //if there are bytes to recieve...
    in_char=Wire.read();                        //recieve a reading and it break up into a form that can be read by serial/SD
    data[i]=in_char;                         //load the data into the array
    i+=1;                                       //and add one to our array element counter
    if(in_char==0){                             //if we see that we have been sent a null command (ASCII 0)
      i=0;                                      //reset the counter to 0
      Wire.endTransmission();                   //and close i2c communication
  Serial.println(data);                     //print data
   break;                                      //end while loop
      }
    }
  }
}
