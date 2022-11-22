/*
    Copyright (C) 2021  Ing. Pavel Sedlacek
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


/*
   UDP syntax:
   signals:
     DS18B20 1wire sensor packet:    rail1 1w2 2864fc3008082 25.44
     DS2438 1wire sensor packet:     rail1 1w1 2612c3102004f 25.44 1.23 0.12
   commands:
     rgb led command :               rail1 led2 rgb 255000127    // lowest byte is red color 0-255
     dim led command:                rail1 led5 dim 255           // dimming function 0-255
     blk led command:                rail1 led1 blk 255           // blink function 0-255
     analog output:                  rail1 ao2 255                // 0-255 = 0-10V
     reset command:                  rail1 rst                    
   default scan cycles:
     1wire cycle:                    30000 ms
     heart beat cycle(only RS485):   60000 ms

   MODBUS TCP commands: FC1 - FC16
   MODBUS RTU commands: FC3, FC6, FC16
   
   MODBUS register map (1 register = 2 bytes = 16 bits)
          register number            description
          0                          rgb output 1 - red color
          1                          rgb output 1 - green color
          2                          rgb output 1 - blue color
          3                          rgb output 1 - blink cycle value
          4                          rgb output 1 - brightness value
          ...
          85                         rgb output 16 - red color
          86                         rgb output 16 - green color
          87                         rgb output 16 - blue color
          88                         rgb output 16 - blink cycle value
          89                         rgb output 16 - brightness value
          
          90                         analog output 1 - pwm value  0-255
          ...
          101                        analog output 12 - pwm value  0-255
          
          110                        1wire ch.1 - U1WTVSL 1 Temp (value multiplied by 100)
          111                        1wire ch.1 - U1WTVSL 1 Hum (value multiplied by 100)
          112                        1wire ch.1 - U1WTVSL 1 Light (value multiplied by 100)
          110                        1wire ch.1 - U1WTVSL 2 Temp (value multiplied by 100)
          111                        1wire ch.1 - U1WTVSL 2 Hum (value multiplied by 100)
          112                        1wire ch.1 - U1WTVSL 2 Light (value multiplied by 100)
          ...
          140                        1wire ch.2 - U1WTVSL 1 Temp (value multiplied by 100)
          141                        1wire ch.2 - U1WTVSL 1 Hum (value multiplied by 100)
          142                        1wire ch.2 - U1WTVSL 1 Light (value multiplied by 100)
          140                        1wire ch.2 - U1WTVSL 2 Temp (value multiplied by 100)
          141                        1wire ch.2 - U1WTVSL 2 Hum (value multiplied by 100)
          142                        1wire ch.2 - U1WTVSL 2 Light (value multiplied by 100)
          ...
          
          170                         DS18B20 values (up to 10 sensors) 
   
   Combination of 1wire sensors must be up to 10pcs maximum
   
   using RS485 the UDP syntax must have \n symbol at the end of the command line
     
*/

//#define dbg(x) Serial.print(x);
#define dbg(x) ;
//#define dbgln(x) Serial.println(x);
#define dbgln(x) ;

#define ver 1.1

#include <SimpleModbusSlave.h>
#include <OneWire.h>
#include <DS2438.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <MgsModbus.h>
#include <FastLED.h>

byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0};
unsigned int listenPort = 44444;
unsigned int sendPort = 55554;
unsigned int remPort = 55555;
IPAddress listenIpAddress;
IPAddress sendIpAddress(255, 255, 255, 255);

#define rgbOutFirstByte 0
#define anaOutFirstByte 90
#define oneWireTempByte 110
#define oneWireVadByte 111
#define oneWireVsensByte 112
#define oneWireDS18B20Byte 170
#define UDP_TX_PACKET_MAX_SIZE 48
#define inputPacketBufferSize UDP_TX_PACKET_MAX_SIZE
char inputPacketBuffer[UDP_TX_PACKET_MAX_SIZE];
#define outputPacketBufferSize 100
char outputPacketBuffer[outputPacketBufferSize];

EthernetUDP udpRecv;
EthernetUDP udpSend;

#define oneWireCycle 60000
#define oneWireSubCycle 5000
#define heartBeatCycle 60000
#define statusLedTimeOn 50
#define statusLedTimeOff 990
#define blinkTimePer 500
#define commLedTimeOn 50
#define commLedTimeOff 50
#define debouncingTime 5
#define serial3TxControl 16

#define NUM_LEDS 1
#define numOfLEDControllers 16
byte ledRGBValue[numOfLEDControllers][3] = {{0, 0, 0},{0, 0, 0},{0, 0, 0},{0, 0, 0},{0, 0, 0},{0, 0, 0},{0, 0, 0},{0, 0, 0},{0, 0, 0},{0, 0, 0},{0, 0, 0},{0, 0, 0},{0, 0, 0},{0, 0, 0},{0, 0, 0},{0, 0, 0}};
float ledBlkCycValue[numOfLEDControllers] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
byte ledDimValue[numOfLEDControllers] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
CRGB leds[NUM_LEDS];

#define numOfAnaOuts 12
int anaOutPins[numOfAnaOuts] = {44, 45, 46, 2, 3, 5, 6, 7, 8, 9, 11, 12};
#define numOfDipSwitchPins 7
int dipSwitchPins[numOfDipSwitchPins] = {57, 58, 59, 62, 56, 55, 54};
#define numOfLedPins 2
int ledPins[numOfLedPins] = {13, 17};
int boardAddress = 0;
int ethOn = 0;
int rtuOn = 0;
long baudRate = 115200;
int serial3TxDelay = 10;
int serial3TimeOut = 20;
bool ticTac = 0;

String boardAddressStr;
String boardAddressRailStr;
String railStr = "rail";
String ledStr = "led";
#define numOfLedCmds 3
String ledCmdsStr[numOfLedCmds] = {"rgb","dim","blk"};
String anaOutStr = "ao";
String rstStr = "rst";
String heartBeatStr = "hb";
String anaOutCommand[numOfAnaOuts];
String ledCommand[numOfLEDControllers];

class Timer {
  private:
    unsigned long timestampLastHitMs;
    unsigned long sleepTimeMs;
  public:
    boolean isOver();
    void sleep(unsigned long sleepTimeMs);
};

boolean Timer::isOver() {
    if (millis() - timestampLastHitMs < sleepTimeMs) {
        return false;
    }
    timestampLastHitMs = millis();
    return true;
}

void Timer::sleep(unsigned long sleepTimeMs) {
    this->sleepTimeMs = sleepTimeMs;
    timestampLastHitMs = millis();
}

Timer statusLedTimerOn;
Timer statusLedTimerOff;
Timer oneWireTimer;
Timer oneWireSubTimer;
Timer analogTimer;
Timer heartBeatTimer;

MgsModbus Mb;

OneWire ds[2] = {64, 63};
DS2438 ds2438[2] = {&ds[0], &ds[1]};
byte oneWireData[12];
byte oneWireAddr[8];

#define maxSensors 10
byte readstage = 0, resolution = 11;
byte sensors[maxSensors][8], DS2438count[2], DS18B20count[2];
byte sensors2438[2][maxSensors][8], sensors18B20[2][maxSensors][8];
float tempDS2438_1, tempDS2438_2;
float voltDS2438_1, voltDS2438_2;
float currDS2438_1, currDS2438_2;

void setup() {

    memset(Mb.MbData, 0, sizeof(Mb.MbData));
    
    Serial.begin(9600);  
   
    dbg("Railduino firmware version: ");
    dbgln(ver);
    
    for (int i = 0; i < numOfLEDControllers; i++) {
        ledCommand[i] = ledStr + String(i + 1, DEC);
     }

    FastLED.addLeds<WS2811,29,RGB>(leds, NUM_LEDS);
    FastLED.addLeds<WS2811,27,RGB>(leds, NUM_LEDS);
    FastLED.addLeds<WS2811,25,RGB>(leds, NUM_LEDS);
    FastLED.addLeds<WS2811,23,RGB>(leds, NUM_LEDS);
    FastLED.addLeds<WS2811,21,RGB>(leds, NUM_LEDS);
    FastLED.addLeds<WS2811,20,RGB>(leds, NUM_LEDS);
    FastLED.addLeds<WS2811,19,RGB>(leds, NUM_LEDS);
    FastLED.addLeds<WS2811,18,RGB>(leds, NUM_LEDS);
    FastLED.addLeds<WS2811,47,RGB>(leds, NUM_LEDS);
    FastLED.addLeds<WS2811,48,RGB>(leds, NUM_LEDS);
    FastLED.addLeds<WS2811,49,RGB>(leds, NUM_LEDS);
    FastLED.addLeds<WS2811,69,RGB>(leds, NUM_LEDS);
    FastLED.addLeds<WS2811,68,RGB>(leds, NUM_LEDS);
    FastLED.addLeds<WS2811,67,RGB>(leds, NUM_LEDS);
    FastLED.addLeds<WS2811,66,RGB>(leds, NUM_LEDS);
    FastLED.addLeds<WS2811,65,RGB>(leds, NUM_LEDS);

    for (int i = 0; i < numOfLEDControllers; i++) {
        Mb.MbData[rgbOutFirstByte + i * 5 + 3] = ledDimValue[i];
    }

    for (int i = 0; i < numOfAnaOuts; i++) {
        pinMode(anaOutPins[i], OUTPUT);
        anaOutCommand[i] = anaOutStr + String(i + 1, DEC);
    }

    for (int i = 0; i < numOfLedPins; i++) {
        pinMode(ledPins[i], OUTPUT);
    }

    statusLedTimerOn.sleep(statusLedTimeOn);
    statusLedTimerOff.sleep(statusLedTimeOff);
    heartBeatTimer.sleep(heartBeatCycle);
 
    for (int i = 0; i < 4; i++) {
        pinMode(dipSwitchPins[i], INPUT);
        if (!digitalRead(dipSwitchPins[i])) { boardAddress |= (1 << i); }
    }

    pinMode(dipSwitchPins[4], INPUT);
    if (!digitalRead(dipSwitchPins[4]))  { ethOn = 1; dbgln("Ethernet ON");} else { ethOn = 0; dbgln("Ethernet OFF");}

    pinMode(dipSwitchPins[5], INPUT); 
    if (!digitalRead(dipSwitchPins[5]))  { rtuOn = 1; dbgln("485 RTU ON ");} else { rtuOn = 0; dbgln("485 RTU OFF");}
    
    dbg(baudRate);
    dbg(" Bd, Tx Delay: ");
    dbg(serial3TxDelay);
    dbg(" ms, Timeout: ");
    dbg(serial3TimeOut);
    dbgln(" ms");
            
    boardAddressStr = String(boardAddress);  
    boardAddressRailStr = railStr + String(boardAddress);

    if (ethOn) {
      mac[5] = (0xED + boardAddress);
      listenIpAddress = IPAddress(192, 168, 150, 150 + boardAddress);
      if (Ethernet.begin(mac) == 0)
      {
        dbgln("Failed to configure Ethernet using DHCP, using Static Mode");
        Ethernet.begin(mac, listenIpAddress);
      }
         
      udpRecv.begin(listenPort);
      udpSend.begin(sendPort);

      dbg("IP: ");
      printIPAddress();
      dbgln();
    }




    Serial3.begin(baudRate);
    Serial3.setTimeout(serial3TimeOut);
    pinMode(serial3TxControl, OUTPUT);
    digitalWrite(serial3TxControl, 0);

    if (rtuOn) {
      modbus_configure(&Serial3, baudRate, SERIAL_8N1, boardAddress, serial3TxControl, sizeof(Mb.MbData), Mb.MbData); 
    }
    
    dbg("Address: ");
    dbgln(boardAddressStr);

    lookUpSensors();
}

void loop() {
    
    processCommands();
    
    processOnewire();

    statusLed();

    setLedsShow();

    setAnaOut();
       
    if (ethOn) {Mb.MbsRun(); IPrenew();}

    if (rtuOn) {modbus_update(); heartBeat();}   

}

void (* resetFunc) (void) = 0; 

void IPrenew()
{
  byte rtnVal = Ethernet.maintain();
  switch(rtnVal) {
       case 1: dbgln(F("DHCP renew fail"));        
               break;
       case 2: dbgln(F("DHCP renew ok"));        
               break;
       case 3: dbgln(F("DHCP rebind fail"));        
               break;
       case 4: dbgln(F("DHCP rebind ok"));        
               break; 
  }
}

void printIPAddress()
{
  for (byte thisByte = 0; thisByte < 4; thisByte++) {
    dbg(Ethernet.localIP()[thisByte]);
    dbg(".");
  }
}

void heartBeat() {
    if (!heartBeatTimer.isOver()) { 
      return;
    }  
    
    heartBeatTimer.sleep(heartBeatCycle);
    if (ticTac) {
      sendMsg(heartBeatStr + " 1");
      ticTac = 0;
    } else {
      sendMsg(heartBeatStr + " 0");
      ticTac = 1;
    }   
}

void statusLed() {
    if (statusLedTimerOff.isOver()) { 
       statusLedTimerOn.sleep(statusLedTimeOn);
       statusLedTimerOff.sleep(statusLedTimeOff);
       digitalWrite(ledPins[0],HIGH);  
    }  
    if (statusLedTimerOn.isOver()) { digitalWrite(ledPins[0],LOW);} 
}

String oneWireAddressToString(byte addr[]) {
    String s = "";
    for (int i = 0; i < 8; i++) {
        s += String(addr[i], HEX);
    }
    return s;
}

void lookUpSensors(){
  for (int i = 0; i < 2; i++) {
    byte j=0, k=0, l=0, m=0;
    while ((j <= maxSensors) && (ds[i].search(sensors[j]))){
      if (!OneWire::crc8(sensors[j], 7) != sensors[j][7]){
          if (sensors[j][0] == 38){
            for (l=0;l<8;l++){ sensors2438[i][k][l]=sensors[j][l]; }  
            k++;  
          } else {
            for (l=0;l<8;l++){ sensors18B20[i][m][l]=sensors[j][l]; }
            m++;
            dssetresolution(ds[i],sensors[j],resolution);
          }
      }
      j++;
    }
    DS2438count[i] = k;
    DS18B20count[i] = m;
    dbgln("1-wire bus " + String(i+1) + " DS2438 sensors found: " + String(DS2438count[i]) + ", DS18B20 sensors found: " + String(DS18B20count[i]));
  }
}

void dssetresolution(OneWire ow, byte addr[8], byte resolution) {
  byte resbyte = 0x1F;
  if (resolution == 12){ resbyte = 0x7F; }
  else if (resolution == 11) { resbyte = 0x5F; }
  else if (resolution == 10) { resbyte = 0x3F; }

  ow.reset();
  ow.select(addr);
  ow.write(0x4E);         
  ow.write(0);            
  ow.write(0);            
  ow.write(resbyte);      
  ow.write(0x48);         
}

void dsconvertcommand(OneWire ow, byte addr[8]){
  ow.reset();
  ow.select(addr);
  ow.write(0x44,1);       
}

float dsreadtemp(OneWire ow, byte addr[8]) {
  int i;
  byte data[12];
  float celsius;
  ow.reset();
  ow.select(addr);
  ow.write(0xBE);         
  for ( i = 0; i < 9; i++) { 
    data[i] = ow.read();
  }

  int16_t TReading = (data[1] << 8) | data[0];  
  celsius = 0.0625 * TReading;
  return celsius;
}


void processOnewire() {
  
   static byte oneWireState = 0;
   static byte oneWireCnt = 0;
     
   switch(oneWireState)
   {
   case 0:
     if (!oneWireTimer.isOver()) {
        return;
     }
     oneWireTimer.sleep(oneWireCycle);   
     oneWireSubTimer.sleep(oneWireSubCycle);   
     
     oneWireCnt = 0;
     oneWireState++;
     break;
   case 1:
     if (!oneWireSubTimer.isOver()) {
       return;
     }
       if ((oneWireCnt < DS2438count[0])){          
          ds2438[0].begin();
          ds2438[0].update(sensors2438[0][oneWireCnt]);
           if (!ds2438[0].isError()) {
             tempDS2438_1 = ds2438[0].getTemperature();
             voltDS2438_1 = ds2438[0].getVoltage(DS2438_CHA);
             currDS2438_1 = ds2438[0].getVoltage(DS2438_CHB);
             Mb.MbData[oneWireTempByte + (oneWireCnt*3)] = tempDS2438_1*100; 
             Mb.MbData[oneWireVadByte + (oneWireCnt*3)] = voltDS2438_1*100; 
             Mb.MbData[oneWireVsensByte + (oneWireCnt*3)] = currDS2438_1*100; 
             sendMsg("1w1 " + oneWireAddressToString(sensors2438[0][oneWireCnt]) + " " + String(tempDS2438_1, 2) + " " + String(voltDS2438_1, 2) + " " + String(currDS2438_1, 3));
          }
        oneWireCnt++;
        } else {
        oneWireCnt = 0;
        oneWireState++;
        }
     break;
   case 2:
     if (!oneWireSubTimer.isOver()) {
       return;
     }
       if ((oneWireCnt < DS2438count[1])){          
          ds2438[1].begin();
          ds2438[1].update(sensors2438[1][oneWireCnt]);
          if (!ds2438[1].isError()) {
             tempDS2438_2 = ds2438[1].getTemperature();
             voltDS2438_2 = ds2438[1].getVoltage(DS2438_CHA);
             currDS2438_2 = ds2438[1].getVoltage(DS2438_CHB);
             Mb.MbData[oneWireTempByte + maxSensors*3 + (oneWireCnt*3)] = tempDS2438_2*100; 
             Mb.MbData[oneWireVadByte + maxSensors*3 + (oneWireCnt*3)] = voltDS2438_2*100; 
             Mb.MbData[oneWireVsensByte + maxSensors*3 +(oneWireCnt*3)] = currDS2438_2*100; 
             sendMsg("1w2 " + oneWireAddressToString(sensors2438[1][oneWireCnt]) + " " + String(tempDS2438_2, 2) + " " + String(voltDS2438_2, 2) + " " + String(currDS2438_2, 3));
          }
        oneWireCnt++;
        } else {
        oneWireCnt = 0;
        oneWireState++;
        }
     break;
   case 3:
     if (!oneWireSubTimer.isOver()) {
        return;
     }
      if ((oneWireCnt < DS18B20count[0])){  
        dsconvertcommand(ds[0],sensors18B20[0][oneWireCnt]);            
        oneWireCnt++;
      } else {
       oneWireCnt = 0;
       oneWireState++;
      }
     break;
   case 4:
     if (!oneWireSubTimer.isOver()) {
        return;
     }
      if ((oneWireCnt < DS18B20count[0])){  
           Mb.MbData[oneWireDS18B20Byte + oneWireCnt] = dsreadtemp(ds[0],sensors18B20[0][oneWireCnt])*100; 
           sendMsg("1w1 " + oneWireAddressToString(sensors18B20[0][oneWireCnt]) + " " + String(dsreadtemp(ds[0],sensors18B20[0][oneWireCnt]), 2));
           oneWireCnt++;
      } else {
        oneWireState = 0;
      }
     break;
   case 5:
     if (!oneWireSubTimer.isOver()) {
        return;
     }
      if ((oneWireCnt < DS18B20count[1])){  
        dsconvertcommand(ds[1],sensors18B20[1][oneWireCnt]);            
        oneWireCnt++;
      } else {
       oneWireCnt = 0;
       oneWireState++;
      }
     break;
   case 6:
     if (!oneWireSubTimer.isOver()) {
        return;
     }
      if ((oneWireCnt < DS18B20count[1])){  
           Mb.MbData[oneWireDS18B20Byte + maxSensors + oneWireCnt] = dsreadtemp(ds[1],sensors18B20[1][oneWireCnt])*100; 
           sendMsg("1w2 " + oneWireAddressToString(sensors18B20[1][oneWireCnt]) + " " + String(dsreadtemp(ds[1],sensors18B20[1][oneWireCnt]), 2));
         oneWireCnt++;
      } else {
        oneWireState = 0;
      }
    break;
  }
}

void sendMsg(String message) {
    message = railStr + boardAddressStr + " " + message;
    message.toCharArray(outputPacketBuffer, outputPacketBufferSize);
   
    digitalWrite(ledPins[1],HIGH);
    
    if (ethOn) {
      udpSend.beginPacket(sendIpAddress, remPort);
      udpSend.write(outputPacketBuffer, message.length());
      udpSend.endPacket();
    }

    if (!rtuOn) {
    digitalWrite(serial3TxControl, HIGH);     
    Serial3.print(message + "\n");
    delay(serial3TxDelay);    
    digitalWrite(serial3TxControl, LOW);
    }
     
    digitalWrite(ledPins[1],LOW);
    
    dbg("Sending packet: ");
    dbgln(message);
}


void setLedsShow() {
    for (int i = 0; i < numOfLEDControllers; i++) {
       fill_solid(leds, 1,  CRGB( Mb.MbData[rgbOutFirstByte + i*5], Mb.MbData[rgbOutFirstByte + i*5 + 1], Mb.MbData[rgbOutFirstByte + i*5 + 2]));
       if (Mb.MbData[rgbOutFirstByte + i * 5 + 4] > 0) { 
          FastLED[i].showLeds((exp(sin(millis()/(float)Mb.MbData[rgbOutFirstByte + i * 5 + 4]*PI)) - 0.36787944) * 108.0 * Mb.MbData[rgbOutFirstByte + i * 5 + 3] / 255);
       } else {
          FastLED[i].showLeds(Mb.MbData[rgbOutFirstByte + i * 5 + 3]);
       }
    }
}

void setAnaOut() {
    for (int i = 0; i < numOfAnaOuts; i++) {
       analogWrite(anaOutPins[i], Mb.MbData[anaOutFirstByte + i]);
    }
   
}

boolean receivePacket(String *cmd) {

    if (!rtuOn) {
      while (Serial3.available() > 0) {    
        *cmd = Serial3.readStringUntil('\n'); 
        if (cmd->startsWith(boardAddressRailStr)) {
          cmd->replace(boardAddressRailStr, "");
          cmd->trim();
          return true;
        }   
      }   
    }
    
    if (ethOn) {
      int packetSize = udpRecv.parsePacket();
      if (packetSize) {
        memset(inputPacketBuffer, 0, sizeof(inputPacketBuffer));
        udpRecv.read(inputPacketBuffer, inputPacketBufferSize);
        *cmd = String(inputPacketBuffer);
        if (cmd->startsWith(boardAddressRailStr)) {
            cmd->replace(boardAddressRailStr, "");
            cmd->trim();
            return true;
        }
      }
    }
   
    return false;
}


void processCommands() {
    String cmd;
     
 
    if (receivePacket(&cmd)) {
        dbg("Received packet: ")
        dbgln(cmd);
        digitalWrite(ledPins[1],HIGH);
        if (cmd.startsWith(ledStr)) {
            String ledControllerStr = cmd.substring(0,ledStr.length() + 2);
            ledControllerStr.trim();
            for (int i = 0; i < numOfLEDControllers; i++) {
               if (ledControllerStr == ledCommand[i]) {
                String ledCmdStr = cmd.substring(ledStr.length() + 2, ledStr.length() + 6);
                ledCmdStr.trim();
                if (ledCmdStr == ledCmdsStr[0]) {   // rgb command
                  String ledRGBValue = cmd.substring(ledStr.length() + 6, ledStr.length() + 16);
                  ledRGBValue.trim();
                  byte blue = ledRGBValue.toInt() / 1000000;
                  byte green = (ledRGBValue.toInt() % 1000000) / 1000;
                  byte red = (ledRGBValue.toInt() % 1000000) % 1000;  
                  Mb.MbData[rgbOutFirstByte + i * 5] = red;
                  Mb.MbData[rgbOutFirstByte + i * 5 + 1] = green;
                  Mb.MbData[rgbOutFirstByte + i * 5 + 2] = blue;
                  dbgln("Writing to LED output " + String(i + 1) + " RGB value " + String(red) + " " + String(green) + " " + String(blue));
                 } else if (ledCmdStr == ledCmdsStr[1]) {  // dim command
                  String ledDimVal = cmd.substring(ledStr.length() + 6, ledStr.length() + 10);
                  ledDimVal.trim();
                  Mb.MbData[rgbOutFirstByte + i * 5 + 3] = ledDimVal.toInt();
                  dbgln("Writing to LED output " + String(i + 1) + " DIM value " + String(Mb.MbData[rgbOutFirstByte + i * 5 + 3]));
                } else if (ledCmdStr == ledCmdsStr[2]) {  // blink command
                  String ledBlkVal = cmd.substring(ledStr.length() + 6, ledStr.length() + 10);
                  ledBlkVal.trim();
                  Mb.MbData[rgbOutFirstByte + i * 5 + 4] = ledBlkVal.toInt();
                  dbgln("Writing to LED output " + String(i + 1) + " BLINK cycle value " + String(Mb.MbData[rgbOutFirstByte + i * 5 + 4]) + " ms");
                } 
               } 
            }
        } else if (cmd.startsWith(anaOutStr)) {
            String anaOutValue = cmd.substring(anaOutStr.length() + 2);
            anaOutValue.trim();
            for (int i = 0; i < numOfAnaOuts; i++) {
                String anaOutCmd = cmd.substring(0,anaOutStr.length() + 2);
                anaOutCmd.trim();
                if (anaOutCmd == anaOutCommand[i]) {
                    Mb.MbData[anaOutFirstByte + i] = anaOutValue.toInt();
                    dbgln("Writing to analog output " + String(i+1) + " value " + String(anaOutValue.toInt()));
                } 
            }
        } 
        digitalWrite(ledPins[1],LOW);
      }
 }
