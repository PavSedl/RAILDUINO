/*
    Copyright (C) 2017  Ing. Pavel Sedlacek, Dusan Zatkovsky, Milan Brejl
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
     DS18B20 1wire sensor packet:    rail1 1w 2864fc3008082 25.44
     DS2438 1wire sensor packet:     rail1 1w 2612c3102004f 25.44 1.23 0.12
     digital input state:            rail1 di1 1
     analog input state:             rail1 ai1 520
   commands:
     relay on command:               rail1 do12 on
     relay off command:              rail1 do5 off
     high side switch on command:    rail1 ho2 on    
     high side switch off command:   rail1 ho4 off
     low side switch on command:     rail1 lo1 on    
     low side switch off command:    rail1 lo2 off
     analog output command:          rail1 ao1 180
     status command:                 rail1 stat10
     reset command:                  rail1 rst
   default scan cycles:
     1wire cycle:                    30000 ms
     analog input cycle:             10000 ms
     heart beat cycle(only RS485):   60000 ms

   RS485 syntax must have \n symbol at the end of the command line
     
*/

#define dbg(x) Serial.print(x);
//#define dbg(x) ;
#define dbgln(x) Serial.println(x);
//#define dbgln(x) ;

#define ver 2.1

#include <OneWire.h>
#include <DS2438.h>
#include <Ethernet.h>
#include <EthernetUdp.h>

byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0};
unsigned int listenPort = 44444;
unsigned int sendPort = 55554;
unsigned int loxonePort = 55555;
IPAddress listenIpAddress;
IPAddress sendIpAddress(255, 255, 255, 255);

#define inputPacketBufferSize UDP_TX_PACKET_MAX_SIZE
char inputPacketBuffer[UDP_TX_PACKET_MAX_SIZE];
#define outputPacketBufferSize 100
char outputPacketBuffer[outputPacketBufferSize];

EthernetUDP udpRecv;
EthernetUDP udpSend;

#define oneWireCycle 30000
#define oneWireSubCycle 5000
#define anaInputCycle 10000
#define heartBeatCycle 60000
#define statusLedTimeOn 50
#define statusLedTimeOff 990
#define commLedTimeOn 50
#define commLedTimeOff 50
#define debouncingTime 5
#define serialTxControl 16
#define numOfRelays 12
int relayPins[numOfRelays] = {37, 35, 33, 31, 29, 27, 39, 41, 43, 45, 47, 49};
#define numOfHSSwitches 4
int HSSwitchPins[numOfHSSwitches] = {5, 6, 7, 8};
#define numOfLSSwitches 4
int LSSwitchPins[numOfLSSwitches] = {9, 11, 12, 13};
#define numOfAnaOuts 2
int anaOutPins[numOfAnaOuts] = {3, 2};
#define numOfAnaInputs 2
int analogPins[numOfAnaInputs] = {64, 63};
float analogStatus[numOfAnaInputs];
#define numOfDigInputs 24
int inputPins[numOfDigInputs] = {34, 32, 30, 28, 26, 24, 22, 25, 23, 21, 20, 19, 36, 38, 40, 42, 44, 46, 48, 69, 68, 67, 66, 65};
int inputStatus[numOfDigInputs];
int inputStatusNew[numOfDigInputs];
int inputChangeTimestamp[numOfDigInputs];
#define numOfDipSwitchPins 6
int dipSwitchPins[numOfDipSwitchPins] = {57, 56, 55, 54, 58, 59};
#define numOfLedPins 2
int ledPins[numOfLedPins] = {18, 17};
int boardAddress = 0;
int ethOn = 0;
long baudRates[2] = {19200, 115200};
long selectedBaudRate = 115200;
bool ticTac = 0;

String boardAddressStr;
String boardAddressRailStr;
String railStr = "rail";
String digInputStr = "di";
String anaInputStr = "ai";
String relayStr = "ro";
String HSSwitchStr = "ho";
String LSSwitchStr = "lo";
String anaOutStr = "ao";
String digStatStr = "stat";
String rstStr = "rst";
String heartBeatStr = "hb";
String relayOnCommands[numOfRelays];
String relayOffCommands[numOfRelays];
String HSSwitchOnCommands[numOfHSSwitches];
String HSSwitchOffCommands[numOfHSSwitches];
String LSSwitchOnCommands[numOfLSSwitches];
String LSSwitchOffCommands[numOfLSSwitches];

String digStatCommand[numOfDigInputs];
String anaOutCommand[numOfAnaOuts];

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

OneWire ds(54);
byte oneWireData[12];
byte oneWireAddr[8];

#define maxSensors 10
byte readstage = 0, resolution = 11;
byte sensors[maxSensors][8], DS2438count, DS18B20count;
byte sensors2438[maxSensors][8], sensors18B20[maxSensors][8];
DS2438 ds2438(&ds);


void setup() {

    Serial.begin(9600);  

    dbg("Railduino firmware version: ");
    dbgln(ver);
    
    for (int i = 0; i < numOfDigInputs; i++) {
        pinMode(inputPins[i], INPUT);
        inputStatus[i] = 1;
        inputStatusNew[i] = 0;
        digStatCommand[i] = digStatStr + String(i + 1, DEC);
    }
    
    for (int i = 0; i < numOfRelays; i++) {
        pinMode(relayPins[i], OUTPUT);
        relayOnCommands[i] = relayStr + String(i + 1, DEC) + " on";
        relayOffCommands[i] = relayStr + String(i + 1, DEC) + " off";
        setRelay(i, 0);
    }

    for (int i = 0; i < numOfHSSwitches; i++) {
        pinMode(HSSwitchPins[i], OUTPUT);
        HSSwitchOnCommands[i] = HSSwitchStr + String(i + 1, DEC) + " on";
        HSSwitchOffCommands[i] = HSSwitchStr + String(i + 1, DEC) + " off";
        setHSSwitch(i, 0);
    }

    for (int i = 0; i < numOfLSSwitches; i++) {
        pinMode(LSSwitchPins[i], OUTPUT);
        LSSwitchOnCommands[i] = LSSwitchStr + String(i + 1, DEC) + " on";
        LSSwitchOffCommands[i] = LSSwitchStr + String(i + 1, DEC) + " off";
        setLSSwitch(i, 0);
    }
   
    for (int i = 0; i < numOfAnaOuts; i++) {
        pinMode(anaOutPins[i], OUTPUT);
        anaOutCommand[i] = anaOutStr + String(i + 1, DEC);
        setAnaOut(i, 0);
    }

    for (int i = 0; i < numOfAnaInputs; i++) {
        pinMode(analogPins[i], INPUT);
    }

    for (int i = 0; i < numOfLedPins; i++) {
        pinMode(ledPins[i], OUTPUT);
    }

    analogTimer.sleep(anaInputCycle);
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
    dbg("RS485 speed: ");
    if (!digitalRead(dipSwitchPins[5]))  { selectedBaudRate = baudRates[0];} else { selectedBaudRate = baudRates[1];}
    dbg(selectedBaudRate);
    dbgln(" Bd");
            
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

    Serial3.begin(selectedBaudRate);
    if (selectedBaudRate == baudRates[1]) { Serial3.setTimeout(50);} else {Serial3.setTimeout(500);}
    pinMode(serialTxControl, OUTPUT);
    digitalWrite(serialTxControl, 0);

    dbg("Address: ");
    dbgln(boardAddressStr);
    
    lookUpSensors(); 

}

void loop() {
  
    readDigInputs();

    readAnaInputs();

    processCommands();
    
    processOnewire();

    statusLed();
    
    if (!ethOn) { heartBeat();} else { IPrenew(); }
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
    if (statusLedTimerOn.isOver()) { digitalWrite(ledPins[0],LOW); } 
}

String oneWireAddressToString(byte addr[]) {
    String s = "";
    for (int i = 0; i < 8; i++) {
        s += String(addr[i], HEX);
    }
    return s;
}

void lookUpSensors(){
  byte j=0, k=0, l=0, m=0;
  while ((j <= maxSensors) && (ds.search(sensors[j]))){
     if (!OneWire::crc8(sensors[j], 7) != sensors[j][7]){
        if (sensors[j][0] == 38){
           for (l=0;l<8;l++){ sensors2438[k][l]=sensors[j][l]; }  
           k++;  
        } else {
           for (l=0;l<8;l++){ sensors18B20[m][l]=sensors[j][l]; }
           m++;
           dssetresolution(ds,sensors[j],resolution);
        }
     }
     j++;
  }
  DS2438count = k;
  DS18B20count = m;
  dbg("1-wire sensors found: ");
  dbgln(k+m);
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
      if ((oneWireCnt < DS2438count)){          
         ds2438.begin();
         ds2438.update(sensors2438[oneWireCnt]);
         if (!ds2438.isError()) {
            sendMsg("1w " + oneWireAddressToString(sensors2438[oneWireCnt]) + " " + String(ds2438.getTemperature(), 2) + " " + String(ds2438.getVoltage(DS2438_CHA), 2) + " " + String(ds2438.getVoltage(DS2438_CHB), 2));
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
      if ((oneWireCnt < DS18B20count)){  
         dsconvertcommand(ds,sensors18B20[oneWireCnt]);            
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
      if ((oneWireCnt < DS18B20count)){  
         sendMsg("1w " + oneWireAddressToString(sensors18B20[oneWireCnt]) + " " + String(dsreadtemp(ds,sensors18B20[oneWireCnt]), 2));
         oneWireCnt++;
      } else {
        oneWireState = 0;
      }
      break;
   }
}


void readDigInputs() {

    int timestamp = millis();
    for (int i = 0; i < numOfDigInputs; i++) {      
       int oldValue = inputStatus[i];
       int newValue = inputStatusNew[i];
       int curValue = digitalRead(inputPins[i]);

       if (oldValue != newValue) {
          if(newValue != curValue) {
             inputStatusNew[i] = curValue;
          } else if(timestamp - inputChangeTimestamp[i] > debouncingTime) {
             inputStatus[i] = newValue;
             if(!newValue) {
                sendInputOn(i + 1);               
             } else {
                sendInputOff(i + 1);           
             }
          }
          
       } else {
          if(oldValue != curValue) {
             inputStatusNew[i] = curValue;
             inputChangeTimestamp[i] = timestamp;
          }
       }
    }
}

void readAnaInputs() {
    
    if (!analogTimer.isOver()) {
      return;
    }
    
    analogTimer.sleep(anaInputCycle);
    for (int i = 0; i < numOfAnaInputs; i++) {
        int pin = analogPins[i];
        float value = analogRead(pin) * (10.0 / 1023.0);
        float oldValue = analogStatus[i];
        analogStatus[i] = value;
        if (value != oldValue) {
            sendAnaInput(i+1,value);
        }
    } 
}

void sendInputOn(int input) {
    sendMsg(digInputStr + String(input, DEC) + " 1");
}

void sendInputOff(int input) {
    sendMsg(digInputStr + String(input, DEC) + " 0");
}

void sendAnaInput(int input, float value) {
    sendMsg(anaInputStr + String(input, DEC) + " " + String(value, 2));
}

void sendMsg(String message) {
    message = railStr + boardAddressStr + " " + message;
    message.toCharArray(outputPacketBuffer, outputPacketBufferSize);
   
    digitalWrite(ledPins[1],HIGH);
    
    if (ethOn) {
      udpSend.beginPacket(sendIpAddress, loxonePort);
      udpSend.write(outputPacketBuffer, message.length());
      udpSend.endPacket();
    }

    digitalWrite(serialTxControl, HIGH);     
    Serial3.print(message + "\n");
    delay(10);    
    digitalWrite(serialTxControl, LOW);
        
    digitalWrite(ledPins[1],LOW);
    
    dbg("Sending packet: ");
    dbgln(message);
}

void setRelay(int relay, int value) {
    if (relay > numOfRelays) {
        return;
    }
    dbgln("Writing to relay " + String(relay+1) + " value " + String(value));
    digitalWrite(relayPins[relay], value);
}

void setHSSwitch(int hsswitch, int value) {
    if (hsswitch > numOfHSSwitches) {
        return;
    }
    dbgln("Writing to high side switch" + String(hsswitch+1) + " value " + String(value));
    digitalWrite(HSSwitchPins[hsswitch], value);
}

void setLSSwitch(int lsswitch, int value) {
    if (lsswitch > numOfLSSwitches) {
        return;
    }
    dbgln("Writing to low side switch" + String(lsswitch+1) + " value " + String(value));
    digitalWrite(LSSwitchPins[lsswitch], value);
}

void setAnaOut(int pwm, int value) {
    if (pwm > numOfAnaOuts) {
        return;
    }
    dbgln("Writing to analog output " + String(pwm+1) + " value " + String(value));
    analogWrite(anaOutPins[pwm], value);
}

boolean receivePacket(String *cmd) {

    while (Serial3.available() > 0) {    
      *cmd = Serial3.readStringUntil('\n'); 
      if (cmd->startsWith(boardAddressRailStr)) {
         cmd->replace(boardAddressRailStr, "");
         cmd->trim();
         return true;
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
        if (cmd.startsWith(relayStr)) {
            for (int i = 0; i < numOfRelays; i++) {
                if (cmd == relayOnCommands[i]) {
                    setRelay(i, 1);
                } else if (cmd == relayOffCommands[i]) {
                    setRelay(i, 0);
                }
            }
        } else if (cmd.startsWith(HSSwitchStr)) {
            for (int i = 0; i < numOfHSSwitches; i++) {
                if (cmd == HSSwitchOnCommands[i]) {
                    setHSSwitch(i, 1);
                } else if (cmd == HSSwitchOffCommands[i]) {
                    setHSSwitch(i, 0);
                }
            }
        } else if (cmd.startsWith(LSSwitchStr)) {
            for (int i = 0; i < numOfLSSwitches; i++) {
                if (cmd == LSSwitchOnCommands[i]) {
                    setLSSwitch(i, 1);
                } else if (cmd == LSSwitchOffCommands[i]) {
                    setLSSwitch(i, 0);
                }
            }
        } else if (cmd.startsWith(anaOutStr)) {
            String anaOutValue = cmd.substring(anaOutStr.length() + 2);
            for (int i = 0; i < numOfAnaOuts; i++) {
                if (cmd.substring(0,anaOutStr.length()+1) == anaOutCommand[i]) {
                    setAnaOut(i, anaOutValue.toInt());
                } 
            }
        } else if (cmd.startsWith(digStatStr)) {
            for (int i = 0; i < numOfDigInputs; i++) {
                if (cmd.substring(0,digStatStr.length()+2) == digStatCommand[i]) {
                      if(!inputStatus[i]) {
                        sendInputOn(i + 1);
                      } else {
                        sendInputOff(i + 1);
                      }
                } 
            }
        } else if (cmd.startsWith(rstStr)) {
            resetFunc();
        }
        digitalWrite(ledPins[1],LOW);
      }
 }



