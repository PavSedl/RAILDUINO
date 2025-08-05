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
     digital input connected:        rail1 di1 1
     digital input disconnected:     rail1 di1 0
     analog input state:             rail1 ai1 520
   commands:
     relay on command:               rail1 do12 on
     relay off command:              rail1 do5 off
     pwm output command:             rail1 pwm1 255
     status command:                 rail1 stat10
     reset command:                  rail1 rst
   default scan cycles:
     1wire cycle:                    30000 ms
     analog input cycle:             30000 ms
     heart beat cycle(only RS485):   60000 ms

   RS485 syntax must have \n symbol at the end of the command line
     
*/

//define dbg(x) Serial.println(x);
#define dbg(x);
// UDP = ethOn 1   485 = ethOn 0
#define ethOn 1


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
#define anaInputCycle 30000
#define heartBeatCycle 60000
#define statusLedTimeOn 50
#define statusLedTimeOff 990
#define debouncingTime 5
#define serialTxControl 8
#define serialTxDelay 10
#define numOfRelays 12
int relayPins[numOfRelays] = {39, 41, 43, 45, 47, 49, 23, 25, 27, 29, 31, 33};
#define numOfPwms 4
int pwmPins[numOfPwms] = {11, 13, 12, 7};
#define numOfAnaInputs 3
int analogPins[numOfAnaInputs] = {58, 59, 62};
int analogStatus[numOfAnaInputs];
#define numOfDigInputs 24
int inputPins[numOfDigInputs] = {36, 34, 48, 46, 69, 68, 67, 66, 44, 42, 40, 38, 6, 5, 3, 2, 14, 15, 16, 17, 24, 26, 28, 30};
int inputStatus[numOfDigInputs];
int inputStatusNew[numOfDigInputs];
int inputChangeTimestamp[numOfDigInputs];
#define numOfDipSwitchPins 4
int dipSwitchPins[numOfDipSwitchPins] = {54, 55, 56, 57};
int statusLedPin = 32;
int boardAddress = 0;
bool ticTac = 0;

String boardAddressStr;
String boardAddressRailStr;
String railStr = "rail";
String digInputStr = "di";
String anaInputStr = "ai";
String relayStr = "do";
String pwmStr = "pwm";
String digStatStr = "stat";
String rstStr = "rst";
String heartBeatStr = "hb";
String relayOnCommands[numOfRelays];
String relayOffCommands[numOfRelays];
String digStatCommand[numOfDigInputs];
String pwmCommand[numOfPwms];

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

OneWire ds(9);
byte oneWireData[12];
byte oneWireAddr[8];

#define maxSensors 5
byte readstage = 0, resolution = 11;
byte sensors[maxSensors][8], DS2438count, DS18B20count;
byte sensors2438[maxSensors][8], sensors18B20[maxSensors][8];
DS2438 ds2438(&ds);


void setup() {

    Serial.begin(9600);
    Serial1.begin(115200);
    Serial1.setTimeout(50);

    for (int i = 0; i < numOfDigInputs; i++) {
        pinMode(inputPins[i], INPUT_PULLUP);
        inputStatus[i] = 1;
        inputStatusNew[i] = 0;
        digStatCommand[i] = digStatStr + String(i + 1, DEC);
    }

    pinMode(3,INPUT_PULLUP);
    
    for (int i = 0; i < numOfRelays; i++) {
        pinMode(relayPins[i], OUTPUT);
        relayOnCommands[i] = relayStr + String(i + 1, DEC) + " on";
        relayOffCommands[i] = relayStr + String(i + 1, DEC) + " off";
        setRelay(i, 0);
    }
    
    for (int i = 0; i < numOfPwms; i++) {
        pinMode(pwmPins[i], OUTPUT);
        pwmCommand[i] = pwmStr + String(i + 1, DEC);
        setPWM(i, 0);
    }

    for (int i = 0; i < numOfAnaInputs; i++) {
        pinMode(analogPins[i], INPUT);
    }

    analogTimer.sleep(anaInputCycle);
    heartBeatTimer.sleep(heartBeatCycle);
    
    pinMode(statusLedPin, OUTPUT); 
    statusLedTimerOn.sleep(statusLedTimeOn);
    statusLedTimerOff.sleep(statusLedTimeOff);

    for (int i = 0; i < numOfDipSwitchPins; i++) {
        pinMode(dipSwitchPins[i], INPUT);
        if (!digitalRead(dipSwitchPins[i])) { boardAddress |= (1 << i); }
    }
    boardAddressStr = String(boardAddress);  
    boardAddressRailStr = railStr + String(boardAddress);

    if (ethOn) {
      mac[5] = (0xED + boardAddress);
      listenIpAddress = IPAddress(192, 168, 150, 150 + boardAddress);
      if (Ethernet.begin(mac) == 0)
      {
        dbg("Failed to configure Ethernet using DHCP, using Static Mode");
        Ethernet.begin(mac, listenIpAddress);
      }
         
      udpRecv.begin(listenPort);
      udpSend.begin(sendPort);

      dbg("IP: ");
      printIPAddress();
    }

    pinMode(serialTxControl, OUTPUT);
    digitalWrite(serialTxControl, 0);

    dbg("Railduino address: ");
    dbg(boardAddressStr);
    
    lookUpSensors(); 

}

void loop() {
  
    readDigInputs();

    readAnaInputs();

    processCommands();

    processOnewire();

    statusLed();
    
    if (!ethOn) { heartBeat();}
}

void (* resetFunc) (void) = 0; 

void printIPAddress()
{
  for (byte thisByte = 0; thisByte < 4; thisByte++) {
    dbg(Ethernet.localIP()[thisByte]);
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
       digitalWrite(statusLedPin,HIGH);  
    }  
    if (statusLedTimerOn.isOver()) { digitalWrite(statusLedPin,LOW); } 
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
   
   //dbg("Processing onewire - oneWireState: " +  String(oneWireState) + ", oneWireCnt: " +  String(oneWireCnt));

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
            sendMsg("1w " + oneWireAddressToString(sensors2438[oneWireCnt]) + " " + String(ds2438.getTemperature(), 2) + " " + String(ds2438.getVoltage(DS2438_CHA), 2) + " " + String(ds2438.getVoltage(DS2438_CHB), 3));
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
     
       if(oldValue != newValue) {
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
        float value = analogRead(pin);
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

    if (ethOn) {
      udpSend.beginPacket(sendIpAddress, loxonePort);
      udpSend.write(outputPacketBuffer, message.length());
      udpSend.endPacket();
    }
    
    digitalWrite(serialTxControl, 1);     
    Serial1.print(message + "\n");
    delay(serialTxDelay);    
    digitalWrite(serialTxControl, 0);
        
    dbg(message);
}

void setRelay(int relay, int value) {
    if (relay > numOfRelays) {
        return;
    }
    dbg("Writing to relay " + String(relay+1) + " value " + String(value));
    digitalWrite(relayPins[relay], value);
}

void setPWM(int pwm, int value) {
    if (pwm > numOfPwms) {
        return;
    }
    dbg("Writing to PWM output " + String(pwm+1) + " value " + String(value));
    analogWrite(pwmPins[pwm], value);
}

boolean receivePacket(String *cmd) {

    while (Serial1.available() > 0) {    
      *cmd = Serial1.readStringUntil('\n'); 
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
        dbg(cmd);
        if (cmd.startsWith(relayStr)) {
            for (int i = 0; i < numOfRelays; i++) {
                if (cmd == relayOnCommands[i]) {
                    setRelay(i, 1);
                } else if (cmd == relayOffCommands[i]) {
                    setRelay(i, 0);
                }
            }
        } else if (cmd.startsWith(pwmStr)) {
            String pwmvalue = cmd.substring(pwmStr.length() + 2);
            for (int i = 0; i < numOfPwms; i++) {
                if (cmd.substring(0,pwmStr.length()+1) == pwmCommand[i]) {
                    setPWM(i, pwmvalue.toInt());
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
    }
}
