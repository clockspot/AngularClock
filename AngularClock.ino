
/***************************************************

  This program is designed for the Wicked Device Angular Clock v1 

  For more information and to buy go to:

  ----> http://shop.wickeddevice.com/product/angular-clock-kit/


  Written by Ken Rother for Wicked Device LLC.

  MIT license, all text above must be included in any redistribution

 ****************************************************/
#define VERSION "V1.0"

#include <TimeLib.h>
#include <Wire.h>
#include <EEPROM.h>
#include <Encoder.h>
#include <MCP7941x.h>

MCP7941x rtc=MCP7941x();

#define ENCODER_DO_NOT_USE_INTERRUPTS
#define ENCA 3
#define ENCB 2

Encoder myEnc(ENCA,ENCB);

// Map Meters to Arduino Pins Meter1 is on the left of clock
#define METER1 9 //purple wire
#define METER2 6 //blue wire
#define METER3 5 //orange wire

// Associate METER1, METER2, METER3 with output functions -- SECONDS,MINUTES,HOURS,TEMPERATURE,HOURS2,HOURS3 
// (HOURS2,HOURS2 are for multitimezone clock)
#define HOURS METER1
#define MINUTES METER2
#define SECONDS METER3

#define TOTAL_METERS 10

//The min and max control values sent to control the needle on the meter.
#define MIN_ANALOG 0
#define MAX_ANALOG 255 //roughly 5VDC, max deflection

#define HOURS_SCALE 24 //12 or 24

//Calibration points for each meter, in integer meter values (values displayed on the meter scale).
//First point should be the meter's lowest integer value, if it's nonzero.
//(If it's zero, it's optional – include only if you need to calibrate it to a nonzero control value for some reason.)
//Last value should be the meter's highest integer value. This helps in case deflection at CV 255 doesn't reach the end of the scale.
//The lowest (if nonzero) and highest calibration points also define the meter's range until calibrated.
//Make sure the memory locations (see CALMEMLOC_) account for the number of calibration points defined here.
const int CALPTS = 7; //http://forum.arduino.cc/index.php?topic=41009.0
const int CALPTS_HOURS[CALPTS] = {1,5,10,15,20,22,23};
const int CALPTS_MINUTES[CALPTS] = {1,5,15,35,57,58,59};
const int CALPTS_SECONDS[CALPTS] = {1,5,15,35,57,58,59};
const int* getCalPts(int meterID){ //TODO: set this up programmatically either CALPTS[meterID] or what
	switch(meterID){
		case HOURS: return CALPTS_HOURS; break;
		case MINUTES: return CALPTS_MINUTES; break;
		case SECONDS: default: return CALPTS_SECONDS; break;
	}
}

// LED Pins
#define RED 11
#define GREEN 10

// Time delays
#define SECONDS_1 1000 //ms
#define FLASH_DELAY 200 //ms

#define LED_STARTUP_FLASHES 1

int meterCtrlVal[TOTAL_METERS] = {0,}; //Holds the current control value of each meter

tmElements_t tm;

void log(char *msg){
  //Serial.print(tm.Day, DEC);
  //Serial.print(F(":"));
  //Serial.print(tm.Month, DEC);
  //Serial.print(F(":"));
  //Serial.print(tm.Year+2000, DEC);
  //Serial.print(F("  "));
  Serial.print(tm.Hour, DEC);
  Serial.print(F(":"));
  Serial.print(tm.Minute, DEC);
  Serial.print(F(":"));
  Serial.print(tm.Second, DEC);
  Serial.print(F("  "));
  Serial.print(msg);
  Serial.println();
}

// Sweep meter over full range "count" times
void sweepMeters(int count){
  int value;
  int i;
  
  for(i=0;i<count;i++){
    
    for(value = MIN_ANALOG; value <=MAX_ANALOG; value++){
      analogWrite(METER1,value);
      analogWrite(METER2,value);
      analogWrite(METER3,value);
      delay(5);
    }
    for(value = MAX_ANALOG; value > MIN_ANALOG; value--){
      analogWrite(METER1,value);
      analogWrite(METER2,value);
      analogWrite(METER3,value);
      delay(5);
    }
  }
  analogWrite(METER1,0);
  analogWrite(METER2,0);
  analogWrite(METER3,0); 
}

void setup () {
  int i;

  Serial.begin(115200);
  Serial.print(F("Wicked Device Angular Clock "));
  Serial.println(VERSION);
  Serial.println("");
  Serial.println(F("Type any key and <return> to enter meter adjustment mode\n"));
 
  pinMode(RED,OUTPUT);
  pinMode(GREEN,OUTPUT);

  for(int i=0; i < LED_STARTUP_FLASHES; i++){ // Flash Leds
    digitalWrite(RED,HIGH);
    digitalWrite(GREEN,LOW);
    delay(FLASH_DELAY);
    digitalWrite(RED,LOW);
    digitalWrite(GREEN,HIGH);
    delay(FLASH_DELAY);
  }
  digitalWrite(RED,LOW); // Turn off leds
  digitalWrite(GREEN,LOW);
  
  sweepMeters(1);
  
  pinMode(ENCA,INPUT);
  pinMode(ENCB,INPUT);
  digitalWrite(ENCA,HIGH); // set pullups
  digitalWrite(ENCB,HIGH);

  Wire.begin();
  
  delay(SECONDS_1);
  
  rtc.getDateTime(&tm.Second,&tm.Minute,&tm.Hour,&tm.Wday,&tm.Day,&tm.Month,&tm.Year);
  
// If RTC does not have a valid time (year == 1) or TimeGood Flag is false, because reset was hit while time was beening adjusted
// Restore RTC to time saved in EEPROM. This will either be the time before adjustment started or time store at board setup
  if(!readTimeGood() || tm.Year==1){ 
    restoreTime();
    rtc.setDateTime(tm.Second,tm.Minute,tm.Hour,1,tm.Day,tm.Month,tm.Year);
    setTimeGood(true);
  }
  
  //clearCalibrations();
  
  //If there's a serial input during setup, let's do some stuff
  if(Serial.available()){
	  calibrateMeter(HOURS);
	  calibrateMeter(MINUTES);
	  calibrateMeter(SECONDS);
  }
  log("Setup Complete");
  
  //Confirm that the calibration points are saved and recalled correctly in EEPROM
  	Serial.println();
	Serial.println();
	int j;
	int memLoc;
	Serial.print(F("Hour calibration points (mv/ml/cv): ")); //meter value, memory location, control value
	memLoc = getCalMemLoc(HOURS);
	for(j=0; j<CALPTS; j++) {
		Serial.print(CALPTS_HOURS[j]);
		Serial.print(F("/"));
		Serial.print(memLoc+j);
		Serial.print(F("/"));
		Serial.print(EEPROM.read(memLoc+j));
		if(j==CALPTS-1) Serial.println(); else Serial.print(F(", "));
	}
	Serial.print(F("Minute calibration points (mv/ml/cv): ")); //meter value, memory location, control value
	memLoc = getCalMemLoc(MINUTES);
	for(j=0; j<CALPTS; j++) {
		Serial.print(CALPTS_MINUTES[j]);
		Serial.print(F("/"));
		Serial.print(memLoc+j);
		Serial.print(F("/"));
		Serial.print(EEPROM.read(memLoc+j));
		if(j==CALPTS-1) Serial.println(); else Serial.print(F(", "));
	}
	Serial.print(F("Second calibration points (mv/ml/cv): ")); //meter value, memory location, control value
	memLoc = getCalMemLoc(SECONDS);
	for(j=0; j<CALPTS; j++) {
		Serial.print(CALPTS_SECONDS[j]);
		Serial.print(F("/"));
		Serial.print(memLoc+j);
		Serial.print(F("/"));
		Serial.print(EEPROM.read(memLoc+j));
		if(j==CALPTS-1) Serial.println(); else Serial.print(F(", "));
	}
}

int encoderValue = 0;
int oldEncoderValue = 0;
int oldSec = 0;
int lastSec = 0;
long adjustStart=0;
#define ADJUST_WAIT 10000

void loop () {
  static time_t adjustTime;
  
  long newPos = myEnc.read();
  if (newPos != encoderValue) { // encoder was turned
    encoderValue = newPos;
    encoderValue = -encoderValue; // flip encoder sign
  }   
  
  rtc.getDateTime(&tm.Second,&tm.Minute,&tm.Hour,&tm.Wday,&tm.Day,&tm.Month,&tm.Year);
  
    
  if(tm.Second != oldSec){ // only do stuff once per second
    //log("New Second");

    if(encoderValue != oldEncoderValue){
      if(adjustStart == 0){ // Is this the start of a time adjustment
        saveTime();         // If so save current RTC time in EEPROM 
        setTimeGood(false); // Indicate current RTC time is no longer good
      }
      
      adjustStart = millis();
      //Serial.print(F("Encoder non zero ="));
      //Serial.println(encoderValue);
      if(encoderValue > oldEncoderValue){
        digitalWrite(GREEN,HIGH);
        digitalWrite(RED,LOW); 
      }
      else{
         digitalWrite(RED,HIGH);
         digitalWrite(GREEN,LOW);
      }
      adjustTime = makeTime(tm); // Convert current tm structure into seconds since epoch
      lastSec = tm.Second; 
      //Serial.print(adjustTime);
      adjustTime = adjustTime + (encoderValue - oldEncoderValue) * 15;
      oldEncoderValue = encoderValue;
      //Serial.print(F(" - "));
      //Serial.println(adjustTime);
      breakTime(adjustTime,tm);
      tm.Second = lastSec;
      log("After adjust");
      rtc.setDateTime(tm.Second,tm.Minute,tm.Hour,1,tm.Day,tm.Month,tm.Year);
   }
   else{
      digitalWrite(GREEN,LOW);
      digitalWrite(RED,LOW);
   }

   if((adjustStart != 0) && ((millis()-adjustStart) > ADJUST_WAIT)){
      setTimeGood(true);
      adjustStart=0;
   }
   
   log("Set Meters");
 
// do not show seconds if time adjustment in progress, 
// seconds bounce around too much
   if(encoderValue == oldEncoderValue){
       //setMeter(SECONDS,map(tm.Second,0,59,0,255));
	   setMeter(SECONDS,applyCalibration(SECONDS,tm.Second));
   }
 
       
   //setMeter(MINUTES,map(tm.Minute,0,59,0,255)+getOffset(MINUTES,tm.Minute));
   setMeter(MINUTES,applyCalibration(MINUTES,tm.Minute));

  
     int temp_hours;
     temp_hours =  tm.Hour;
	 //24h clock: don't drop 12 hours in the afternoon, and double the map scale
     if(temp_hours >  11 && HOURS_SCALE==12)
       temp_hours = temp_hours - 12;
     //setMeter(HOURS,map(temp_hours*60+(tm.Minute/2),0,60*HOURS_SCALE,0,255)+getOffset(HOURS,temp_hours));
	 setMeter(HOURS,applyCalibration(HOURS,temp_hours));


    //Serial.println(F("--- End of Meter update loop"));
    oldSec=tm.Second;
  }  
}

/* 
  when returning a meter to 0, if deflection is >25%, motion is ugly and loud 
  so keep track of position of each meter and if being set to 0 and 
  current defelection is >25% move meter down to 0 slowly
*/

#define MINSOFT 64    // 25% anything > let down slowly
#define METER_DEC 5   //step amount to move meter down
#define METER_WAIT 20 // time (ms) between each down step

void setMeter(int meter, int value){
  int meterTemp;
  
  //Serial.print(F("Set Meter "));
  //Serial.print(meter);
  //Serial.print(" to ");
  //Serial.println(value); 
  
  if((value == 0) and (meterCtrlVal[meter] >= MINSOFT)){
    for(meterTemp=meterCtrlVal[meter]; meterTemp > 0; meterTemp -= METER_DEC){
      analogWrite(meter,meterTemp);
      delay(METER_WAIT);
    }
  }
  
  if(value > 255) value = 255;
  
  analogWrite(meter,value);
  
  meterCtrlVal[meter] = value;
}

// //Ballistics control
// //#define BALL_VAL 64    //Control value changes greater than this (either direction) will trigger ballistics
// //With each step, the needle will move toward ctrlVal by BALL_PRC % plus BALL_BIT
// #define BALL_PRC 10
// #define BALL_BIT 2
// #define BALL_DELAY 100 //time (ms) between each step
//
// void setMeter(int meterID, int ctrlVal){
// 	//Wraps analogWrite with some limiting and ballistics control
//
// 	if(ctrlVal<0) ctrlVal=0; //does this even happen?
// 	if(ctrlVal>255) ctrlVal=255;
//
// 	int movingVal = meterCtrlVal[meterID];
// 	bool movingDir = (ctrlVal >= meterCtrlVal[meterID] ? true : false); //true for up, false for down
//
// 	for(int i=1; i<=10; i++) { //Don't take any more than 10 steps
// 		//Move movingVal toward ctrlVal by BALL_PRC % plus BALL_BIT
// 		movingVal = map(BALL_PRC,0,100,movingVal+((movingDir?1:-1)*BALL_BIT),ctrlVal);
// 		//If we went too far, go straight to ctrlVal and call it quits
// 		if((movingDir && movingVal > ctrlVal) || (!movingDir && movingVal < ctrlVal)) movingVal = ctrlVal;
// 		//If out of range, or this is the last chance saloon, let's call it quits here too
// 		if(movingVal<0 || movingVal>255 || i==10) movingVal == ctrlVal;
// 		Serial.print(F("......Setting meter "));
// 		Serial.print(meterID);
// 		Serial.print(F(" to "));
// 		Serial.print(movingVal);
// 		Serial.print(F(" > "));
// 		Serial.print(ctrlVal);
// 		Serial.println();
// 		analogWrite(meterID,movingVal); meterCtrlVal[meterID] = movingVal;
// 		if(meterCtrlVal[meterID] == ctrlVal) break; //all done
// 	}
// }


// EEPROM Memory Locations used to store last know good time
#define YEAR_MEM 10
#define MONTH_MEM 11
#define DAY_MEM 12
#define WDAY_MEM 13
#define HOUR_MEM 14
#define MINUTE_MEM 15
#define SECOND_MEM 16

// Flag indicating EEPROM time is valid
#define TIME_GOOD 20

/*
  EEPROM routines to handle meter offsets
  Offsets are used for beter needle postioning on Hour and Minute clock faces
*/
// Memory locations for meter calibration. They hold control values that correspond to the meter values in the CALPTS_ arrays.
const int CALMEMLOC_HOURS = 30; //thru 36
const int CALMEMLOC_MINUTES = 37; //thru 43
const int CALMEMLOC_SECONDS = 44; //thru 50
const int getCalMemLoc(int meterID){ //TODO: set this up programmatically either CALMEMLOC[meterID] or what
	switch(meterID){
		case HOURS: return CALMEMLOC_HOURS; break;
		case MINUTES: return CALMEMLOC_MINUTES; break;
		case SECONDS: default: return CALMEMLOC_SECONDS; break;
	}
}

// Flag indicating meter calibrations have been initialized
#define CALIBRATIONS_GOOD 22

void calibrateMeter(char meterID) {
	int calPtID; //Calibration point index
	int meterVal; //Display value on the meter scale
	int ctrlValStart, ctrlVal; //Analog value sent to control the meter
	int encValFirst, encValPrev, encValNow; //Track changes to the encoder
	const int* calPts = getCalPts(meterID);
	for(calPtID=0; calPtID<CALPTS; calPtID++) {
		//Get the meter value for this calibration point
		meterVal = calPts[calPtID];
		//Set a starting control value for this calibration point.
		//If present in EEPROM, use that. Otherwise, calculate one, assuming meter range is 0 to highest calibration point.
		if(EEPROM.read(CALIBRATIONS_GOOD)!=0x55) ctrlValStart = getCalibration(meterID,calPtID);
		else ctrlValStart = map(meterVal,0,calPts[CALPTS-1],MIN_ANALOG,MAX_ANALOG);
		ctrlVal = ctrlValStart; //in case the knob isn't moved
	    setMeter(meterID,ctrlValStart);
		encValFirst = -myEnc.read(); //What's the encoder set to?
		//encValPrev = -myEnc.read();
	    //Send user prompt
		//NTS: Keep literal strings in F() to keep them in PROGMEM Flash, not copied to SRAM
		//NTS: Also kept separated out because strings need to be given an initial value before concatenated
		Serial.print(F("Adjust"));
		//Serial.print(meterDesc);
		Serial.print(F(" meter to "));
		Serial.print(meterVal);
		Serial.print(F(" using knob, then type any key + <return>. Current control value is "));
		Serial.print(ctrlValStart);
		if(EEPROM.read(CALIBRATIONS_GOOD)==0x55) Serial.print(F(" (inferred)"));
		Serial.println();
		
	    // Adjust offset until serial character is received
	    while(Serial.available())
	        Serial.read();
		while(Serial.available() == 0){
			encValNow = -myEnc.read();
			if (encValPrev != encValNow) { // encoder was turned
				encValPrev = encValNow;
				ctrlVal = ctrlValStart+(encValPrev-encValFirst);
				Serial.print(F("...encVal "));
				Serial.print(encValPrev);
				Serial.print(F(", ctrlVal "));
				Serial.print(ctrlVal);
				Serial.print(F(", encValFirst "));
				Serial.print(encValFirst);
				
				//In case calibrations have taken us out of range, artifically adjust encValFirst to compensate
				if(ctrlVal>255) {
					encValFirst += ctrlVal-255; ctrlVal = 255;
					Serial.print(F("...corrected to ctrlVal "));
					Serial.print(ctrlVal);
					Serial.print(F(", encValFirst "));
					Serial.print(encValFirst);
				}
				if(ctrlVal<0) { encValFirst += ctrlVal; ctrlVal = 0; }
				
				Serial.println();
				setMeter(meterID,ctrlVal);
			}
			delay(5);
	    }
  
	    //Update offset storage
	    setCalibration(meterID,calPtID,ctrlVal);
		Serial.print(F("Meter value "));
	    Serial.print(meterVal);
	    Serial.print(F(" is set to control value "));
	    //Serial.println((int)getOffset(meter,mpos));
		Serial.print(ctrlVal);
		Serial.println();
		Serial.println();
	} //end for each calibration point
} //end calibrateMeter

void setCalibration(int meterID, int calPtID, int ctrlVal){
	const int memloc = getCalMemLoc(meterID);
	EEPROM.write(memloc+calPtID, ctrlVal);
	EEPROM.write(CALIBRATIONS_GOOD,0x56); //do we need? TODO
}
int getCalibration(int meterID, int calPtID){
	if(EEPROM.read(CALIBRATIONS_GOOD) == 0x55) return 0;
	const int memloc = getCalMemLoc(meterID);
	return EEPROM.read(memloc+calPtID);
}
void clearCalibrations(){
    if(EEPROM.read(CALIBRATIONS_GOOD) == 0x55) return;
	clearCalibration(CALMEMLOC_HOURS);
	clearCalibration(CALMEMLOC_MINUTES);
	clearCalibration(CALMEMLOC_SECONDS);
	EEPROM.write(CALIBRATIONS_GOOD,0x55);
}
void clearCalibration(int calMemLoc){
	int i; for(i=0; i<4; i++) EEPROM.write(calMemLoc+i, 0);
}

int applyCalibration(int meterID, int meterVal){
	//Given a meter ID and a desired display value, return control value with meter calibration accounted for
	int calPtID, ctrlVal;
	const int* calPts = getCalPts(meterID);
	
	if(EEPROM.read(CALIBRATIONS_GOOD)==0x55) { //no good calibration
		//Use a flat linear conversion. Assume range is 0 to highest calibration point.
		return map(meterVal,0,calPts[CALPTS-1],MIN_ANALOG,MAX_ANALOG);
	} else { //good calibrations
		const int calMemLoc = getCalMemLoc(meterID);
		for(calPtID=0; calPtID<CALPTS; calPtID++) { //Which calibration range does meterVal fall into?
			if(meterVal < calPts[calPtID] || calPtID==CALPTS-1) {
				//The second half of that condition is in case meterVal >= the last calibration point.
				//In that case, it uses the scale of the previous range.
				//If meterVal < the first calibration point, we'll map using a lower bound of 0 (thus why 0 as a cal pt is optional)
				ctrlVal = map(meterVal,(calPtID==0?0:calPts[calPtID-1]),calPts[calPtID],(calPtID==0?0:EEPROM.read(calMemLoc+calPtID-1)),EEPROM.read(calMemLoc+calPtID));
				break;
			}
		}
	}
	return ctrlVal;
} //end applyCalibration

/*
  EEPROM Routines used to hold time during adjustment process
  When Adjustment starts current time is stored in EEPROM
  If processor is reset within 10 seconds of time adjustment clock
  will restore time from EEPROM and save to RTC
*/

void setTimeGood(boolean state){
  EEPROM.write(TIME_GOOD,state);
}

boolean readTimeGood(){
  return(EEPROM.read(TIME_GOOD));
}

// Save time in current tm structure to EEPROM
void saveTime(){
  EEPROM.write(YEAR_MEM,tm.Year-30);
  EEPROM.write(MONTH_MEM,tm.Month);
  EEPROM.write(DAY_MEM,tm.Day);
  EEPROM.write(DAY_MEM,tm.Wday);
  EEPROM.write(HOUR_MEM,tm.Hour);
  EEPROM.write(MINUTE_MEM,tm.Minute);
  EEPROM.write(SECOND_MEM,tm.Second);
  
}

// Restore time from EEPROM into tm structure
void restoreTime(){
  tm.Year=EEPROM.read(YEAR_MEM);
  tm.Month=EEPROM.read(MONTH_MEM);
  tm.Day=EEPROM.read(DAY_MEM);
  tm.Wday=EEPROM.read(WDAY_MEM);
  tm.Hour=EEPROM.read(HOUR_MEM);
  tm.Minute=EEPROM.read(MINUTE_MEM);
  tm.Second=EEPROM.read(SECOND_MEM);
}

