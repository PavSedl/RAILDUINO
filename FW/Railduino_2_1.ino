/*
    Copyright (C) 2019  Ing. Pavel Sedlacek
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

const char hwVer[] = "2.1"; // Statická hodnota pro hardware verzi
const char fwVer[] = "25082025"; // Statická hodnota pro firmware verzi

// Documentation content stored in PROGMEM, split into smaller chunks
const char docsContentHeader[] PROGMEM = R"=====(
<!DOCTYPE HTML>
<html>
<head>
    <title>Railduino Protocol Documentation</title>
    <style>
        body { font-family: Arial, sans-serif; font-size: 14px; margin: 20px; }
        .container { width: 1000px; margin: 0 auto; position: relative; }
        h2, h3 { font-family: Arial, sans-serif; }
        pre { background: #f4f4f4; padding: 10px; border: 1px solid #ccc; white-space: pre-wrap; }
    </style>
</head>
<body>
    <div class='container'>
    <h2>Railduino Firmware Documentation</h2>
)=====";

const char docsContentUDPSyntax1[] PROGMEM = R"=====(
    <h3>UDP</h3>
    <pre>
signals:
  DS18B20 1wire sensor packet:    rail1 1w 2864fc3008082 25.44
  DS2438 1wire sensor packet:     rail1 1w 2612c3102004f 25.44 1.23 0.12
  digital input state:            rail1 di1 1
  analog input state:             rail1 ai1 1020
)=====";

const char docsContentUDPSyntax2[] PROGMEM = R"=====(
commands:
  relay on command:               rail1 ro12 on
  relay off command:              rail1 ro5 off
  high side switch on command:    rail1 ho2 on
  high side switch off command:   rail1 ho4 off
  high side switch PWM command:   rail1 ho1_pwm 180
  low side switch on command:     rail1 lo1 on
  low side switch off command:    rail1 lo2 off
  low side switch PWM command:    rail1 lo1_pwm 180
  analog output command:          rail1 ao1 180
  reset command:                  rail1 rst
default scan cycles:
  1wire cycle:                    30000 ms
  analog input cycle:             10000 ms
    </pre>
)=====";

const char docsContentModbus[] PROGMEM = R"=====(
    <h3>MODBUS</h3>
    <pre>
MODBUS TCP commands: FC1 - FC16
MODBUS RTU commands: FC3, FC6, FC16

Registers are 2 bytes (16 bits) - only lowest significant byte (LSB) is used
    
)=====";

const char docsContentRegisterMap[] PROGMEM = R"=====(
MODBUS Register Map

register number (2 bytes)  description
0 - bits 0-7               relay outputs 1-8
1 - bits 16-19             relay outputs 9-12
2 - bits 32-39             digital outputs HSS 1-4, LSS 1-4
3 - LSB byte               HSS PWM value 1 (0-255)
4 - LSB byte               HSS PWM value 2 (0-255)
5 - LSB byte               HSS PWM value 3 (0-255)
6 - LSB byte               HSS PWM value 4 (0-255)
7 - LSB byte               LSS PWM value 1 (0-255)
8 - LSB byte               LSS PWM value 2 (0-255)
9 - LSB byte               LSS PWM value 3 (0-255)
10 - LSB byte              LSS value 4 (0 or 255)
11 - LSB byte              analog output 1 (0-255)
12 - LSB byte              analog output 2 (0-255)
13 - bits 208-215          digital inputs 1-8
14 - bits 216-223          digital inputs 9-16
15 - bits 224-231          digital inputs 17-24
16 - LSB byte              analog input 1 (0-1023)
17 - LSB byte              analog input 2 (0-1023)
18 - bit 288               reset 
19 - LSB byte              1st DS2438 Temp (value multiplied by 100)
20 - LSB byte              1st DS2438 Vad (value multiplied by 100)
21 - LSB byte              1st DS2438 Vsens (value multiplied by 100)
-                         
46 - LSB byte              DS2438 values (up to 10 sensors)
47-57                      DS18B20 Temperature (up to 10 sensors) (value multiplied by 100)
</pre>
)=====";

const char docsContentFooter[] PROGMEM = R"=====(
    <h3>Additional Notes</h3>
    <pre>
Combination of 1wire sensors must be up to 10pcs maximum (DS18B20 or DS2438)
Using RS485 the UDP syntax must have \n symbol at the end of the command line
    </pre>
</body>
</html>
)=====";

// Libraries for communication and device control
#include <SimpleModbusSlave.h>
#include <OneWire.h>
#include <DS2438.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <SPI.h>
#include <MgsModbus.h>
#include <avr/wdt.h>
#include <EEPROM.h>

// Ethernet configuration
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0}; // Device MAC address
unsigned int listenPort = 44444; // Port for receiving UDP messages
unsigned int sendPort = 55554;   // Port for sending UDP messages
unsigned int remPort = 55555;    // Remote port for sending messages
unsigned int httpPort = 80;      // Port for HTTP webserver
IPAddress dhcpServer; // DHCP server IP address (gateway)
IPAddress sendIpAddress(255, 255, 255, 255); // Broadcast IP address for sending
EthernetClient testClient; // Client pro TCP test připojení

#define EEPROM_INITFLAG 0
#define EEPROM_SERIALLOCK 1
#define EEPROM_DEBUGON 2
#define EEPROM_PULSEON 3
#define EEPROM_GATEWAYON 4
#define EEPROM_UDPCONTROLON 5
#define EEPROM_1WIRECYCLE 10
#define EEPROM_ANAINCYCLE 15
#define EEPROM_PULSECYCLE 20
#define EEPROM_LANCYCLE 25
#define EEPROM_BAUDRATE 30
#define EEPROM_SERIALNUMBER 50
#define EEPROM_DESCRIPTION 91
#define EEPROM_ALIAS_START 200
#define EEPROM_ALIAS_RELAYS EEPROM_ALIAS_START
#define EEPROM_ALIAS_HSS (EEPROM_ALIAS_RELAYS + (numOfRelays * 41))
#define EEPROM_ALIAS_LSS (EEPROM_ALIAS_HSS + (numOfHSSwitches * 41))
#define EEPROM_ALIAS_DIGINPUTS (EEPROM_ALIAS_LSS + (numOfLSSwitches * 41))
#define EEPROM_ALIAS_ANA_INPUTS (EEPROM_ALIAS_DIGINPUTS + (numOfDigInputs * 41))
#define EEPROM_ALIAS_ANA_OUTS (EEPROM_ALIAS_ANA_INPUTS + (numOfAnaInputs * 41))
#define EEPROM_ALIAS_DS2438 (EEPROM_ALIAS_ANA_OUTS + (numOfAnaOuts * 41))
#define EEPROM_ALIAS_DS18B20 (EEPROM_ALIAS_DS2438 + (maxSensors * 41))


// MODBUS register definitions
#define relOut1Byte 0   // Relay outputs 1-8
#define relOut2Byte 1   // Relay outputs 9-12
#define hssLssByte 2    // HSS and LSS outputs
#define hssPWM1Byte 3   // PWM for HSS1
#define hssPWM2Byte 4   // PWM for HSS2
#define hssPWM3Byte 5   // PWM for HSS3
#define hssPWM4Byte 6   // PWM for HSS4
#define lssPWM1Byte 7   // PWM for LSS1
#define lssPWM2Byte 8   // PWM for LSS2
#define lssPWM3Byte 9   // PWM for LSS3
#define lssPWM4Byte 10  // PWM for LSS4
#define anaOut1Byte 11  // Analog output 1 
#define anaOut2Byte 12  // Analog output 2 
#define digInp1Byte 13  // Digital inputs 1-8 
#define digInp2Byte 14  // Digital inputs 9-16 
#define digInp3Byte 15  // Digital inputs 17-24 
#define anaInp1Byte 16  // Analog input 1 
#define anaInp2Byte 17  // Analog input 2 
#define resetByte 18  // Service register 
#define oneWireTempByte 19  // DS2438 temperature 
#define oneWireVadByte 20   // DS2438 Vad voltage
#define oneWireVsensByte 21 // DS2438 Vsens voltage
#define oneWireDS18B20Byte 47 // DS18B20 temperature

// Buffers for receiving and sending UDP messages
#define inputPacketBufferSize UDP_TX_PACKET_MAX_SIZE
char inputPacketBuffer[UDP_TX_PACKET_MAX_SIZE];
#define outputPacketBufferSize 100
char outputPacketBuffer[outputPacketBufferSize];

// UDP and HTTP communication instances
EthernetUDP udpRecv; // UDP receiver
EthernetUDP udpSend; // UDP sender
EthernetServer server(httpPort); // HTTP server on port 80

// Timing cycles for various operations
unsigned long oneWireCycle = 30000;   // Cycle for 1-Wire sensors (ms)
unsigned long oneWireSubCycle = 2000; // Sub-cycle for 1-Wire (ms)
unsigned long anaInputCycle = 10000;  // Cycle for analog inputs (ms)
unsigned long checkInterval = 10000; // Connection check interval (ms)
unsigned long pulseSendCycle = 10000; // Cycle for sending pulse counts (ms)
unsigned long baudRate = 115200; // RS485 communication baud rate

int boardAddress = 0; // Board address (from DIP switches)
int serial3TxDelay = 10; // Delay for RS485 transmission
int serial3TimeOut = 20; // Timeout for serial communication
int resetPin = 4; // Pin for Ethernet module reset

bool ethernetOK = false;
bool dipSwitchEthOn = false;
bool dhcpSuccess = false; // Flag for successful DHCP IP acquisition
bool serialLocked = false;
bool debugEnabled = false;
bool pulseOn = false;
bool gatewayEnabled = false;
bool useUDPctrl = true;
int pulse1 = 0, pulse2 = 0, pulse3 = 0; // Pulse counters for pins 21, 20, 19
int sentPulse1 = 0, sentPulse2 = 0, sentPulse3 = 0;

#define statusLedTimeOn 50   // Status LED on time in milliseconds
#define statusLedTimeOff 950 // Status LED off time in milliseconds
#define commLedTimeOn 50     // Communication LED on time in milliseconds
#define commLedTimeOff 50    // Communication LED off time in milliseconds
#define debouncingTime 5     // Debouncing time for inputs in milliseconds
#define serial3TxControl 16  // Pin for RS485 transmission control
#define numOfDipSwitchPins 6 // Number of DIP switch pins
#define numOfLedPins 2       // Number of LED pins
#define numOfDigInputs 24    // Number of digital inputs
#define numOfRelays 12       // Number of relays
#define numOfHSSwitches 4    // Number of high-side switches
#define numOfLSSwitches 4    // Number of low-side switches
#define numOfAnaOuts 2       // Number of analog outputs
#define numOfAnaInputs 2     // Number of analog inputs
#define maxSensors 10        // Maximum number of sensors

// Pin configuration for relays, HSS, LSS, analog I/O, and LEDs
int relayPins[numOfRelays] = {37, 35, 33, 31, 29, 27, 39, 41, 43, 45, 47, 49}; // Relay pins
int HSSwitchPins[numOfHSSwitches] = {5, 6, 7, 8}; // High-Side switch pins
int LSSwitchPins[numOfLSSwitches] = {9, 11, 12, 18}; // Low-Side switch pins
int anaOutPins[numOfAnaOuts] = {3, 2}; // Analog output pins
float anaOutsVoltage[numOfAnaOuts];
int analogPins[numOfAnaInputs] = {64, 63}; // Analog input pins
float analogStatus[numOfAnaInputs]; // Analog input status
float anaInputsVoltage[numOfAnaInputs];
int inputPins[numOfDigInputs] = {34, 32, 30, 28, 26, 24, 22, 25, 23, 21, 20, 19, 36, 38, 40, 42, 44, 46, 48, 69, 68, 67, 66, 65}; // Digital input pins
int inputStatus[numOfDigInputs]; // Current digital input status
int inputStatusNew[numOfDigInputs]; // New digital input status for debouncing
int inputChangeTimestamp[numOfDigInputs]; // Timestamp for input state changes
int dipSwitchPins[numOfDipSwitchPins] = {57, 56, 55, 54, 58, 59}; // DIP switch pins
int ledPins[numOfLedPins] = {13, 17}; // LED pins

char serialNumber[41];
char description[41];
char tempAlias[41]; // Dočasný buffer pro aliasy

// List of allowed Czech diacritical characters in UTF-8 (two-byte sequences)
static const unsigned char czech_chars[][2] = {{0xC3,0xA1},{0xC4,0x8D},{0xC4,0x8F},{0xC3,0xA9},{0xC4,0x9B},{0xC3,0xAD},{0xC5,0x88},{0xC3,0xB3},{0xC5,0x99},{0xC5,0xA1},{0xC5,0xA5},{0xC3,0xBA},{0xC5,0xAF},{0xC3,0xBD},{0xC5,0xBE},{0xC3,0x81},{0xC4,0x8C},{0xC4,0x8E},{0xC3,0x89},{0xC4,0x9A},{0xC3,0x8D},{0xC5,0x87},{0xC3,0x93},{0xC5,0x98},{0xC5,0xA0},{0xC5,0xA4},{0xC3,0x9A},{0xC5,0xAE},{0xC3,0x9D},{0xC5,0xBD}}; 

char cmdBuffer[64]; // Buffer pro příkazy
String boardAddressStr; // String representation of board address
char boardAddressRailStr[10]; // Prefix for messages (e.g., "rail1")
const char railStr[] PROGMEM = "rail";
const char digInputStr[] PROGMEM = "di";
const char anaInputStr[] PROGMEM = "ai";
const char relayStr[] PROGMEM = "ro";
const char HSSwitchStr[] PROGMEM = "ho";
const char LSSwitchStr[] PROGMEM = "lo";
const char anaOutStr[] PROGMEM = "ao";
const char rstStr[] PROGMEM = "rst";

// Helper function to print PROGMEM strings safely
void printProgmemString(EthernetClient& client, const char* progmemStr) { while (true) { char c = pgm_read_byte(progmemStr++); if (c == 0) break; client.write(c); } }

// Serial debug definition
void dbg(const String& msg) { if (debugEnabled) Serial.print(msg); }
void dbgln(const String& msg) { if (debugEnabled) Serial.println(msg); }
void dbgln(const char* msg) { if (debugEnabled) Serial.println(msg); }

// Timer class definition
class Timer { private: unsigned long timestampLastHitMs, sleepTimeMs; public: boolean isOver(); void sleep(unsigned long sleepTimeMs); };
boolean Timer::isOver() { if (millis() - timestampLastHitMs < sleepTimeMs) return false; timestampLastHitMs = millis(); return true; }
void Timer::sleep(unsigned long sleepTimeMs) { this->sleepTimeMs = sleepTimeMs; timestampLastHitMs = millis(); }

// Timer instances for various tasks
Timer statusLedTimerOn; // Timer for status LED on state
Timer statusLedTimerOff; // Timer for status LED off state
Timer checkEthernetTimer; // Timer for periodic check of LAN connection
Timer oneWireTimer; // Timer for 1-Wire cycle
Timer oneWireSubTimer; // Timer for 1-Wire sub-cycle
Timer analogTimer; // Timer for reading analog inputs
Timer pulseTimer; // Timer for pulse sending cycle

// MODBUS communication instance
MgsModbus Mb;

// 1-Wire communication instance
OneWire ds(62); // 1-Wire bus on pin 62
byte oneWireData[12]; // Data from 1-Wire sensors
byte oneWireAddr[8]; // Address of 1-Wire sensor

// 1-Wire sensor configuration
byte resolution = 11; // Reading stage and sensor resolution
byte sensors[maxSensors][8], DS2438count, DS18B20count; // Sensor address arrays
byte sensors2438[maxSensors][8], sensors18B20[maxSensors][8]; // DS2438 and DS18B20 addresses
DS2438 ds2438(&ds); // Instance for DS2438 sensors

// Function to reset the device
void (* resetFunc) (void) = 0; 

// Detects W5100/W5200 Ethernet shield, returns true if version is 0x02 (W5200) or 0x03 (W5100)
bool ethShieldDetected() { 
    pinMode(53, OUTPUT); digitalWrite(53, HIGH); 
    pinMode(10, OUTPUT); digitalWrite(10, HIGH); 
    pinMode(4, OUTPUT); digitalWrite(4, HIGH); 
    SPI.begin(); digitalWrite(10, LOW); 
    SPI.transfer(0x00); SPI.transfer(0x1F); 
    byte version = SPI.transfer(0x00); 
    digitalWrite(10, HIGH); SPI.end(); 
    return (version == 0x02 || version == 0x03); 
}

// Resets Ethernet module, attempts DHCP, triggers watchdog if failed
void resetEthernetModule() { 
    pinMode(resetPin, OUTPUT); 
    digitalWrite(resetPin, LOW); 
    delay(50); 
    digitalWrite(resetPin, HIGH); 
    delay(200); 
    unsigned long startTime = millis(); 
    bool dhcpAttempt = false; 
    while (millis() - startTime < 5000) { 
        if (Ethernet.begin(mac) != 0 && Ethernet.localIP()[0] != 0) { 
            dhcpAttempt = true; 
            break; 
        } 
        delay(100); 
    } 
    dhcpSuccess = dhcpAttempt ? true : false; 
    if (!dhcpAttempt) { 
        dbgln("Ethernet reset failed. Triggering watchdog reset..."); 
        while (true) {} 
    } 
}

// Check the Ethernet module
void checkEthernet() {
    udpRecv.stop();
    udpSend.stop();
    bool connected = testClient.connected() || testClient.connect(dhcpServer, 53);
    if (!connected) {
        dbgln(F("Ethernet connection failed, resetting."));
        dhcpSuccess = false;
        testClient.stop();
        resetEthernetModule();
        return;
    } 
    testClient.stop();
    if (useUDPctrl) {
        udpRecv.begin(listenPort);
    }
    udpSend.begin(sendPort);
}

// Parse parameter value from HTTP request
String parseParam(const String& request, const String& param, int& startIdx, int& endIdx) {
    startIdx = request.indexOf(param) + param.length();
    endIdx = request.indexOf("&", startIdx);
    if (endIdx == -1) endIdx = request.indexOf(" HTTP");
    String value = request.substring(startIdx, endIdx);
    value.trim();
    return value.length() > 30 ? value.substring(0, 30) : value;
}

// Handle HTTP requests for the webserver
void handleWebServer() {
    EthernetClient client = server.available();
    if (!client) return;

    String request = "";
    boolean currentLineIsBlank = true;
    unsigned long startTime = millis();
    while (client.connected() && (millis() - startTime < 500)) {
        if (client.available()) {
            char c = client.read();
            if (request.length() < 512) request += c;
            if (c == '\n' && currentLineIsBlank) {
                // Recognize request by URL
                if (request.indexOf("GET /status") != -1) {
                    client.println(F("HTTP/1.1 200 OK\nContent-Type: application/json\nConnection: close\n"));

                    // Update 1-Wire sensor data if cycle time has elapsed
                    static unsigned long lastOneWireUpdate = 0;
                    static float ds2438Temps[10], ds2438Vads[10], ds2438Vsens[10], ds18b20Temps[10];
                    if (millis() - lastOneWireUpdate >= oneWireCycle) {
                        for (int i = 0; i < DS2438count; i++) {
                            ds2438.update(sensors2438[i]);
                            ds2438Temps[i] = ds2438.getTemperature();
                            ds2438Vads[i] = ds2438.getVoltage(DS2438_CHA);
                            ds2438Vsens[i] = ds2438.getVoltage(DS2438_CHB);
                        }
                        for (int i = 0; i < DS18B20count; i++) ds18b20Temps[i] = isnan(dsreadtemp(ds, sensors18B20[i])) ? 0.0 : dsreadtemp(ds, sensors18B20[i]);
                        lastOneWireUpdate = millis();
                    }

                    // Calculate voltages for analog inputs and outputs
                    for (int i = 0; i < numOfAnaInputs; i++) anaInputsVoltage[i] = (analogStatus[i] / 1024.0) * 10.0;
                    for (int i = 0; i < numOfAnaOuts; i++) anaOutsVoltage[i] = (Mb.MbData[anaOut1Byte + i] / 255.0) * 10.0;

                    // Start JSON output
                    client.print(F("{"));

                    // Print relay states
                    client.print(F("\"relays\":[")); 
                    for (int i = 0; i < numOfRelays; i++) {
                        client.print(bitRead(Mb.MbData[i < 8 ? relOut1Byte : relOut2Byte], i % 8));
                        client.print(i < numOfRelays - 1 ? F(",") : F("],"));
                    }

                    // Print high-side switch states
                    client.print(F("\"hss\":[")); 
                    for (int i = 0; i < numOfHSSwitches; i++) {
                        client.print(bitRead(Mb.MbData[hssLssByte], i));
                        client.print(i < numOfHSSwitches - 1 ? F(",") : F("],"));
                    }

                    // Print low-side switch states
                    client.print(F("\"lss\":[")); 
                    for (int i = 0; i < numOfLSSwitches; i++) {
                        client.print(bitRead(Mb.MbData[hssLssByte], i + numOfHSSwitches));
                        client.print(i < numOfLSSwitches - 1 ? F(",") : F("],"));
                    }

                    // Print high-side switch PWM values
                    client.print(F("\"hssPWM\":[")); 
                    for (int i = 0; i < numOfHSSwitches; i++) {
                        client.print(Mb.MbData[hssPWM1Byte + i]);
                        client.print(i < numOfHSSwitches - 1 ? F(",") : F("],"));
                    }

                    // Print low-side switch PWM values
                    client.print(F("\"lssPWM\":[")); 
                    for (int i = 0; i < numOfLSSwitches; i++) {
                        client.print(Mb.MbData[lssPWM1Byte + i]);
                        client.print(i < numOfLSSwitches - 1 ? F(",") : F("],"));
                    }

                    // Print analog output values
                    client.print(F("\"anaOuts\":[")); 
                    for (int i = 0; i < numOfAnaOuts; i++) {
                        client.print(Mb.MbData[anaOut1Byte + i]);
                        client.print(i < numOfAnaOuts - 1 ? F(",") : F("],"));
                    }

                    // Print digital input states (inverted due to negated logic)
                    client.print(F("\"digInputs\":[")); 
                    for (int i = 0; i < numOfDigInputs; i++) {
                        client.print(1 - inputStatus[i]);
                        client.print(i < numOfDigInputs - 1 ? F(",") : F("],"));
                    }

                    // Print analog input values
                    client.print(F("\"anaInputs\":[")); 
                    for (int i = 0; i < numOfAnaInputs; i++) {
                        client.print(analogStatus[i], 2);
                        client.print(i < numOfAnaInputs - 1 ? F(",") : F("],"));
                    }

                    // Print DS2438 sensor data
                    client.print(F("\"ds2438\":[")); 
                    if (DS2438count > 0) {
                        for (int i = 0; i < DS2438count; i++) {
                            client.print(F("{\"sn\":\"")); client.print(oneWireAddressToString(sensors2438[i])); 
                            client.print(F("\",\"temp\":")); client.print(ds2438Temps[i], 2);
                            client.print(F(",\"vad\":")); client.print(ds2438Vads[i], 2);
                            client.print(F(",\"vsens\":")); client.print(ds2438Vsens[i], 2);
                            client.print(F("}")); client.print(i < DS2438count - 1 ? F(",") : F("],"));
                        }
                    } else {
                        client.print(F("],"));   
                    }
                    // Print DS18B20 sensor data
                    client.print(F("\"ds18b20\":[")); 
                    if (DS18B20count > 0) {
                        for (int i = 0; i < DS18B20count; i++) {
                            client.print(F("{\"sn\":\"")); client.print(oneWireAddressToString(sensors18B20[i])); 
                            client.print(F("\",\"temp\":")); client.print(ds18b20Temps[i], 2);
                            client.print(F("}")); client.print(i < DS18B20count - 1 ? F(",") : F("],"));
                        }
                    } else {
                        client.print(F("],"));   
                    }
                    // Print pulse counts and configuration values
                    client.print(F("\"pulseCounts\":[")); client.print(sentPulse1); client.print(F(",")); client.print(sentPulse2); client.print(F(",")); client.print(sentPulse3); client.print(F("],"));
                    client.print(F("\"oneWireCycle\":")); client.print(oneWireCycle); client.print(F(","));
                    client.print(F("\"anaInputCycle\":")); client.print(anaInputCycle); client.print(F(","));
                    client.print(F("\"useUDPctrl\":")); client.print(useUDPctrl ? 1 : 0); client.print(F(","));
                    client.print(F("\"pulseOn\":")); client.print(pulseOn ? 1 : 0); client.print(F(","));
                    client.print(F("\"pulseSendCycle\":")); client.print(pulseSendCycle); client.print(F(","));
                    client.print(F("\"gatewayEnabled\":")); client.print(gatewayEnabled ? 1 : 0); client.print(F(","));
                    client.print(F("\"checkInterval\":")); client.print(checkInterval); client.print(F(","));
                    client.print(F("\"debugEnabled\":")); client.print(debugEnabled ? 1 : 0); client.print(F(","));
                    client.print(F("\"serialNumber\":\"")); client.print(urlDecode(String(serialNumber))); client.print(F("\","));
                    client.print(F("\"serialLocked\":")); client.print(serialLocked ? 1 : 0); client.print(F(","));
                    client.print(F("\"description\":\"")); client.print(urlDecode(String(description))); client.print(F("\","));
                    client.print(F("\"baudRate\":")); client.print(baudRate); client.print(F(","));

                    // Print analog input voltages
                    client.print(F("\"anaInputsVoltage\":[")); 
                    for (int i = 0; i < numOfAnaInputs; i++) {
                        client.print(anaInputsVoltage[i], 2);
                        client.print(i < numOfAnaInputs - 1 ? F(",") : F("],"));
                    }

                    // Print analog output voltages
                    client.print(F("\"anaOutsVoltage\":[")); 
                    for (int i = 0; i < numOfAnaOuts; i++) {
                        client.print(anaOutsVoltage[i], 2);
                        client.print(i < numOfAnaOuts - 1 ? F(",") : F("],"));
                    }

                    // Print relay aliases
                    client.print(F("\"alias_relays\":[")); 
                    for (int i = 0; i < numOfRelays; i++) {
                        EEPROM.get(EEPROM_ALIAS_RELAYS + i * 41, tempAlias);
                        client.print(F("\"")); client.print(urlDecode(tempAlias)); client.print(F("\""));
                        client.print(i < numOfRelays - 1 ? F(",") : F("],"));
                    }

                    // Print high-side switch aliases
                    client.print(F("\"alias_hss\":[")); 
                    for (int i = 0; i < numOfHSSwitches; i++) {
                        EEPROM.get(EEPROM_ALIAS_HSS + i * 41, tempAlias);
                        client.print(F("\"")); client.print(urlDecode(tempAlias)); client.print(F("\""));
                        client.print(i < numOfHSSwitches - 1 ? F(",") : F("],"));
                    }

                    // Print low-side switch aliases
                    client.print(F("\"alias_lss\":[")); 
                    for (int i = 0; i < numOfLSSwitches; i++) {
                        EEPROM.get(EEPROM_ALIAS_LSS + i * 41, tempAlias);
                        client.print(F("\"")); client.print(urlDecode(tempAlias)); client.print(F("\""));
                        client.print(i < numOfLSSwitches - 1 ? F(",") : F("],"));
                    }

                    // Print digital input aliases
                    client.print(F("\"alias_digInputs\":[")); 
                    for (int i = 0; i < numOfDigInputs; i++) {
                        EEPROM.get(EEPROM_ALIAS_DIGINPUTS + i * 41, tempAlias);
                        client.print(F("\"")); client.print(urlDecode(tempAlias)); client.print(F("\""));
                        client.print(i < numOfDigInputs - 1 ? F(",") : F("],"));
                    }

                    // Print analog input aliases
                    client.print(F("\"alias_anaInputs\":[")); 
                    for (int i = 0; i < numOfAnaInputs; i++) {
                        EEPROM.get(EEPROM_ALIAS_ANA_INPUTS + i * 41, tempAlias);
                        client.print(F("\"")); client.print(urlDecode(tempAlias)); client.print(F("\""));
                        client.print(i < numOfAnaInputs - 1 ? F(",") : F("],"));
                    }

                    // Print analog output aliases
                    client.print(F("\"alias_anaOuts\":[")); 
                    for (int i = 0; i < numOfAnaOuts; i++) {
                        EEPROM.get(EEPROM_ALIAS_ANA_OUTS + i * 41, tempAlias);
                        client.print(F("\"")); client.print(urlDecode(tempAlias)); client.print(F("\""));
                        client.print(i < numOfAnaOuts - 1 ? F(",") : F("],"));
                    }

                    // Print DS2438 aliases
                    client.print(F("\"alias_ds2438\":[")); 
                    if (DS2438count > 0) {
                        for (int i = 0; i < DS2438count; i++) {
                            EEPROM.get(EEPROM_ALIAS_DS2438 + i * 41, tempAlias);
                            client.print(F("\"")); client.print(urlDecode(tempAlias)); client.print(F("\""));
                            client.print(i < DS2438count - 1 ? F(",") : F("],"));
                        }
                    } else {
                        client.print(F("],"));   
                    }
                    // Print DS18B20 aliases
                    client.print(F("\"alias_ds18b20\":[")); 
                    if (DS18B20count > 0) {
                        for (int i = 0; i < DS18B20count; i++) {
                            EEPROM.get(EEPROM_ALIAS_DS18B20 + i * 41, tempAlias);
                            client.print(F("\"")); client.print(urlDecode(tempAlias)); client.print(F("\""));
                            client.print(i < DS18B20count - 1 ? F(",") : F("]"));
                        }
                    } else {
                        client.print(F("]"));   
                    }
                    // Close JSON object
                    client.println(F("}"));
                } else if (request.indexOf("GET /reboot") != -1) {
                    client.println(F("HTTP/1.1 200 OK"));
                    client.println(F("Content-Type: text/html"));
                    client.println(F("Connection: close"));
                    client.println();
                    client.println(F("<!DOCTYPE HTML>"));
                    client.println(F("<html><body>"));
                    client.println(F("<h3>Rebooting...</h3>"));
                    client.println(F("<script>setTimeout(function(){window.location.href='/';}, 5000);</script>"));
                    client.println(F("</body></html>"));
                    delay(100); 
                    resetFunc(); 
                } else if (request.indexOf("GET /info") != -1) {
                    // Handle /info endpoint: Serves the firmware documentation page
                    client.println(F("HTTP/1.1 200 OK"));
                    client.println(F("Content-Type: text/html"));
                    client.println(F("Connection: close"));
                    client.println();
                    // Print PROGMEM chunks using helper function to avoid buffer issues
                    printProgmemString(client, docsContentHeader);
                    printProgmemString(client, docsContentUDPSyntax1);
                    printProgmemString(client, docsContentUDPSyntax2);
                    printProgmemString(client, docsContentModbus);
                    printProgmemString(client, docsContentRegisterMap);
                    printProgmemString(client, docsContentFooter);
                    client.println(F("</div>"));
                    client.println(F("</body></html>"));
                } else if (request.indexOf("GET /command?") != -1) {
                    client.println(F("HTTP/1.1 200 OK"));
                    client.println(F("Content-Type: text/plain"));
                    client.println(F("Connection: close"));
                    client.println();
                    client.println(F("OK"));

                    int startIdx, endIdx;
                    String value;

                    // Update serial number
                    if (request.indexOf(F("serialNumber=")) != -1) {
                        value = parseParam(request, F("serialNumber="), startIdx, endIdx);
                        if (startIdx != -1 && isValidAlias(value.c_str())) {
                            value.toCharArray(serialNumber, 41);
                            EEPROM.put(EEPROM_SERIALNUMBER, serialNumber);
                            dbg(F("Serial number updated: ")); dbgln(value);
                        }
                    }

                    // Update description
                    if (request.indexOf(F("description=")) != -1) {
                        value = parseParam(request, F("description="), startIdx, endIdx);
                        if (startIdx != -1 && isValidAlias(value.c_str())) {
                            value.toCharArray(description, 41);
                            EEPROM.put(EEPROM_DESCRIPTION, description);
                            dbg(F("Description updated: ")); dbgln(value);
                        }
                    }

                    // Update 1-Wire cycle
                    if (request.indexOf(F("oneWireCycle=")) != -1) {
                        value = parseParam(request, F("oneWireCycle="), startIdx, endIdx);
                        if (startIdx != -1) {
                            unsigned long val = value.toInt();
                            if (val >= 1000 && val <= 60000) {
                                oneWireCycle = val;
                                oneWireTimer.sleep(oneWireCycle);
                                EEPROM.put(EEPROM_1WIRECYCLE, oneWireCycle);
                                dbg(F("oneWireCycle updated: ")); dbgln(String(val));
                            }
                        }
                    }

                    // Update analog input cycle
                    if (request.indexOf(F("anaInputCycle=")) != -1) {
                        value = parseParam(request, F("anaInputCycle="), startIdx, endIdx);
                        if (startIdx != -1) {
                            unsigned long val = value.toInt();
                            if (val >= 1000 && val <= 60000) {
                                anaInputCycle = val;
                                analogTimer.sleep(anaInputCycle);
                                EEPROM.put(EEPROM_ANAINCYCLE, anaInputCycle);
                                dbg(F("anaInputCycle updated: ")); dbgln(String(val));
                            }
                        }
                    }

                    // Update pulse send cycle
                    if (request.indexOf(F("pulseSendCycle=")) != -1) {
                        value = parseParam(request, F("pulseSendCycle="), startIdx, endIdx);
                        if (startIdx != -1) {
                            unsigned long val = value.toInt();
                            if (val >= 1000 && val <= 60000) {
                                pulseSendCycle = val;
                                EEPROM.put(EEPROM_PULSECYCLE, pulseSendCycle);
                                if (pulseOn) pulseTimer.sleep(pulseSendCycle);
                                dbg(F("pulseSendCycle updated: ")); dbgln(String(val));
                            }
                        }
                    }

                    // Update check interval
                    if (request.indexOf(F("checkInterval=")) != -1) {
                        value = parseParam(request, F("checkInterval="), startIdx, endIdx);
                        if (startIdx != -1) {
                            unsigned long val = value.toInt();
                            if (val >= 1000 && val <= 60000) {
                                checkInterval = val;
                                checkEthernetTimer.sleep(checkInterval);
                                EEPROM.put(EEPROM_LANCYCLE, checkInterval);
                                dbg(F("checkInterval updated: ")); dbgln(String(val));
                            }
                        }
                    }

                    // Update baudRate
                    if (request.indexOf(F("baudRate=")) != -1) {
                        value = parseParam(request, F("baudRate="), startIdx, endIdx);
                        if (startIdx != -1) {
                            long val = value.toInt();
                            if (val >= 100 && val <= 115200) {
                                baudRate = val;
                                EEPROM.put(EEPROM_BAUDRATE, baudRate);
                                Serial3.begin(baudRate);
                                dbg(F("RS485 Baud rate updated: ")); dbgln(String(baudRate));
                            }
                        }
                    }

                    // Update pulse sensing state
                    if (request.indexOf(F("pulseOn=")) != -1) {
                        value = parseParam(request, F("pulseOn="), startIdx, endIdx);
                        if (startIdx != -1) {
                            bool val = value.toInt() == 1;
                            if (val != pulseOn) {
                                pulseOn = val;
                                EEPROM.put(EEPROM_PULSEON, pulseOn);
                                if (pulseOn) {
                                    attachInterrupt(digitalPinToInterrupt(21), [](){pulse1++;}, FALLING);
                                    attachInterrupt(digitalPinToInterrupt(20), [](){pulse2++;}, FALLING);
                                    attachInterrupt(digitalPinToInterrupt(19), [](){pulse3++;}, FALLING);
                                    pulseTimer.sleep(pulseSendCycle);
                                } else {
                                    detachInterrupt(digitalPinToInterrupt(21));
                                    detachInterrupt(digitalPinToInterrupt(20));
                                    detachInterrupt(digitalPinToInterrupt(19));
                                }
                                dbg(F("pulseOn updated: ")); dbgln(String(pulseOn));
                            }
                        }
                    }

                    // Update debug enable state
                    if (request.indexOf(F("debugEnabled=")) != -1) {
                        value = parseParam(request, F("debugEnabled="), startIdx, endIdx);
                        if (startIdx != -1) {
                            bool val = value.toInt() == 1;
                            if (val != debugEnabled) {
                                debugEnabled = val;
                                EEPROM.put(EEPROM_DEBUGON, debugEnabled);
                                dbg(F("debugEnabled updated: ")); dbgln(String(debugEnabled));
                            }
                        }
                    }

                    // Update gateway enabled
                    if (request.indexOf(F("gatewayEnabled=")) != -1) {
                        value = parseParam(request, F("gatewayEnabled="), startIdx, endIdx);
                        if (startIdx != -1) {
                            bool newValue = (value == "1");
                            gatewayEnabled = newValue;
                            EEPROM.put(EEPROM_GATEWAYON, gatewayEnabled);
                            dbg(F("Gateway enabled updated: ")); dbgln(String(gatewayEnabled));
                        }
                    }

                    // Update UDP control enable state
                    if (request.indexOf(F("useUDPctrl=")) != -1) {
                        value = parseParam(request, F("useUDPctrl="), startIdx, endIdx);
                        if (startIdx != -1) {
                            bool newValue = (value == "1");
                            if (newValue != useUDPctrl) {
                                useUDPctrl = newValue;
                                EEPROM.put(EEPROM_UDPCONTROLON, useUDPctrl);
                                if (useUDPctrl) {
                                    udpRecv.begin(listenPort);
                                } else {
                                    udpRecv.stop(); // Uvolní socket
                                }
                                dbg(F("Outputs control protocol changed: ")); dbgln(String(useUDPctrl));
                            }
                        }
                    }

                    // Update relay aliases
                    if (request.indexOf(F("alias_relay_")) != -1) {
                        for (int i = 0; i < numOfRelays; i++) {
                            String aliasParam = "alias_relay_" + String(i + 1) + "=";
                            if (request.indexOf(aliasParam) != -1) {
                                value = parseParam(request, aliasParam, startIdx, endIdx);
                                if (startIdx != -1 && isValidAlias(value.c_str())) {
                                    value.toCharArray(tempAlias, 41);
                                    EEPROM.put(EEPROM_ALIAS_RELAYS + (i * 41), tempAlias);
                                    dbg(F("alias_relay_")); dbg(String(i + 1)); dbg(F(" updated: ")); dbgln(value);
                                }
                            }
                        }
                    }

                    // Update high-side switch aliases
                    if (request.indexOf(F("alias_hss_")) != -1) {
                        for (int i = 0; i < numOfHSSwitches; i++) {
                            String aliasParam = "alias_hss_" + String(i + 1) + "=";
                            if (request.indexOf(aliasParam) != -1) {
                                value = parseParam(request, aliasParam, startIdx, endIdx);
                                if (startIdx != -1 && isValidAlias(value.c_str())) {
                                    value.toCharArray(tempAlias, 41);
                                    EEPROM.put(EEPROM_ALIAS_HSS + (i * 41), tempAlias);
                                    dbg(F("alias_hss_")); dbg(String(i + 1)); dbg(F(" updated: ")); dbgln(value);
                                }
                            }
                        }
                    }

                    // Update low-side switch aliases
                    if (request.indexOf(F("alias_lss_")) != -1) {
                        for (int i = 0; i < numOfLSSwitches; i++) {
                            String aliasParam = "alias_lss_" + String(i + 1) + "=";
                            if (request.indexOf(aliasParam) != -1) {
                                value = parseParam(request, aliasParam, startIdx, endIdx);
                                if (startIdx != -1 && isValidAlias(value.c_str())) {
                                    value.toCharArray(tempAlias, 41);
                                    EEPROM.put(EEPROM_ALIAS_LSS + (i * 41), tempAlias);
                                    dbg(F("alias_lss_")); dbg(String(i + 1)); dbg(F(" updated: ")); dbgln(value);
                                }
                            }
                        }
                    }

                    // Update digital input aliases
                    if (request.indexOf(F("alias_digInput_")) != -1) {
                        for (int i = 0; i < numOfDigInputs; i++) {
                            String aliasParam = "alias_digInput_" + String(i + 1) + "=";
                            if (request.indexOf(aliasParam) != -1) {
                                value = parseParam(request, aliasParam, startIdx, endIdx);
                                if (startIdx != -1 && isValidAlias(value.c_str())) {
                                    value.toCharArray(tempAlias, 41);
                                    EEPROM.put(EEPROM_ALIAS_DIGINPUTS + (i * 41), tempAlias);
                                    dbg(F("alias_digInput_")); dbg(String(i + 1)); dbg(F(" updated: ")); dbgln(value);
                                }
                            }
                        }
                    }

                    // Update analog input aliases
                    if (request.indexOf(F("alias_anaInput_")) != -1) {
                        for (int i = 0; i < numOfAnaInputs; i++) {
                            String aliasParam = "alias_anaInput_" + String(i + 1) + "=";
                            if (request.indexOf(aliasParam) != -1) {
                                value = parseParam(request, aliasParam, startIdx, endIdx);
                                if (startIdx != -1 && isValidAlias(value.c_str())) {
                                    value.toCharArray(tempAlias, 41);
                                    EEPROM.put(EEPROM_ALIAS_ANA_INPUTS + (i * 41), tempAlias);
                                    dbg(F("alias_anaInput_")); dbg(String(i + 1)); dbg(F(" updated: ")); dbgln(value);
                                }
                            }
                        }
                    }

                    // Update analog output aliases
                    if (request.indexOf(F("alias_anaOut_")) != -1) {
                        for (int i = 0; i < numOfAnaOuts; i++) {
                            String aliasParam = "alias_anaOut_" + String(i + 1) + "=";
                            if (request.indexOf(aliasParam) != -1) {
                                value = parseParam(request, aliasParam, startIdx, endIdx);
                                if (startIdx != -1 && isValidAlias(value.c_str())) {
                                    value.toCharArray(tempAlias, 41);
                                    EEPROM.put(EEPROM_ALIAS_ANA_OUTS + (i * 41), tempAlias);
                                    dbg(F("alias_anaOut_")); dbg(String(i + 1)); dbg(F(" updated: ")); dbgln(value);
                                }
                            }
                        }
                    }

                    // Update DS2438 aliases
                    if (request.indexOf(F("alias_ds2438_")) != -1) {
                        for (int i = 0; i < DS2438count; i++) {
                            String aliasParam = "alias_ds2438_" + String(i + 1) + "=";
                            if (request.indexOf(aliasParam) != -1) {
                                value = parseParam(request, aliasParam, startIdx, endIdx);
                                if (startIdx != -1 && isValidAlias(value.c_str())) {
                                    value.toCharArray(tempAlias, 41);
                                    EEPROM.put(EEPROM_ALIAS_DS2438 + (i * 41), tempAlias);
                                    dbg(F("alias_ds2438_")); dbg(String(i + 1)); dbg(F(" updated: ")); dbgln(value);
                                }
                            }
                        }
                    }

                    // Update DS18B20 aliases
                    if (request.indexOf(F("alias_ds18b20_")) != -1) {
                        for (int i = 0; i < DS18B20count; i++) {
                            String aliasParam = "alias_ds18b20_" + String(i + 1) + "=";
                            if (request.indexOf(aliasParam) != -1) {
                                value = parseParam(request, aliasParam, startIdx, endIdx);
                                if (startIdx != -1 && isValidAlias(value.c_str())) {
                                    value.toCharArray(tempAlias, 41);
                                    EEPROM.put(EEPROM_ALIAS_DS18B20 + (i * 41), tempAlias);
                                    dbg(F("alias_ds18b20_")); dbg(String(i + 1)); dbg(F(" updated: ")); dbgln(value);
                                }
                            }
                        }
                    }

                    // Update relay states
                    if (request.indexOf("relay") != -1) {
                        for (int i = 0; i < numOfRelays; i++) {
                            String relayParam = "relay" + String(i + 1) + "=";
                            if (request.indexOf(relayParam) != -1) {
                                value = parseParam(request, relayParam, startIdx, endIdx);
                                int val = value.toInt();
                                if (val >= 0 && val <= 1) {
                                    bitWrite(Mb.MbData[i < 8 ? relOut1Byte : relOut2Byte], i % 8, val);
                                    setRelay(i, val);
                                    dbg(F("Relay ")); dbg(String(i + 1)); dbg(F(" updated: ")); dbgln(String(val));
                                }
                            }
                        }
                    }

                    // Update high-side switch states and PWM
                    if (request.indexOf("hss") != -1) {
                        for (int i = 0; i < numOfHSSwitches; i++) {
                            String hssParam = "hss" + String(i + 1) + "=";
                            if (request.indexOf(hssParam) != -1) {
                                value = parseParam(request, hssParam, startIdx, endIdx);
                                int val = value.toInt();
                                if (val >= 0 && val <= 255) {
                                    Mb.MbData[hssPWM1Byte + i] = val;
                                    bitWrite(Mb.MbData[hssLssByte], i, (val > 0) ? 1 : 0);
                                    setHSSwitch(i, val);
                                    dbg(F("HSS ")); dbg(String(i + 1)); dbg(F(" updated: ")); dbgln(String(val));
                                }
                            }
                        }
                    }

                    // Update low-side switch states and PWM
                    if (request.indexOf("lss") != -1) {
                        for (int i = 0; i < numOfLSSwitches; i++) {
                            String lssParam = "lss" + String(i + 1) + "=";
                            if (request.indexOf(lssParam) != -1) {
                                value = parseParam(request, lssParam, startIdx, endIdx);
                                int val = value.toInt();
                                if (val >= 0 && val <= 255) {
                                    Mb.MbData[lssPWM1Byte + i] = val;
                                    bitWrite(Mb.MbData[hssLssByte], i + numOfHSSwitches, (val > 0) ? 1 : 0);
                                    setLSSwitch(i, val);
                                    dbg(F("LSS ")); dbg(String(i + 1)); dbg(F(" updated: ")); dbgln(String(val));
                                }
                            }
                        }
                    }

                    // Update analog output values
                    if (request.indexOf("anaOut") != -1) {
                        for (int i = 0; i < numOfAnaOuts; i++) {
                            String anaOutParam = "anaOut" + String(i + 1) + "=";
                            if (request.indexOf(anaOutParam) != -1) {
                                value = parseParam(request, anaOutParam, startIdx, endIdx);
                                int val = value.toInt();
                                if (val >= 0 && val <= 255) {
                                    Mb.MbData[anaOut1Byte + i] = val;
                                    setAnaOut(i, val);
                                    dbg(F("Analog Output ")); dbg(String(i + 1)); dbg(F(" updated: ")); dbgln(String(val));
                                }
                            }
                        }
                    }

                    // Trigger reset
                    if (request.indexOf(F("reset=1")) != -1) resetFunc();
                } else {
                    // Main page with HTML and AJAX
                    if (request.indexOf(F("GET / ")) != -1 || request.indexOf(F("GET /?")) != -1) {
                        // Update serial lock state
                        if (request.indexOf(F("?lock=")) != -1) {
                            int startIdx = request.indexOf(F("?lock=")) + 6;
                            int endIdx = request.indexOf(F(" HTTP"), startIdx);
                            serialLocked = (request.substring(startIdx, endIdx).toInt() == 1);
                            EEPROM.put(EEPROM_SERIALLOCK, serialLocked);
                        }

                        // HTTP headers with UTF-8 charset
                        client.println(F("HTTP/1.1 200 OK"));
                        client.println(F("Content-Type: text/html; charset=UTF-8"));
                        client.println(F("Connection: close"));
                        client.println();
                        // HTML head with UTF-8 charset and styles
                        client.println(F("<!DOCTYPE HTML><html><head><meta charset=\"UTF-8\"><title>Railduino Control</title><style>"
                                        "body{font-family:Arial,sans-serif;font-size:14px;position:relative;}"
                                        ".container{width:1000px;margin:0 auto;position:relative;}"
                                        "h1,h2,h3{font-family:Arial,sans-serif;color:#444;}"
                                        "table.inner{border-collapse:collapse;width:100%;background-color:#f8f9fa;}"
                                        "table.outer{border:1px solid #ccc;width:100%;border-radius:8px;overflow:hidden;box-shadow:0 4px 8px rgba(0,0,0,0.1);background-color:#f8f9fa;padding:0 0 10px 15px}"
                                        "td{vertical-align:middle;font-size:14px;padding:2px;}"
                                        "input,select{font-size:14px;color:#555;padding:3px;border:1px solid #ccc;border-radius:4px;box-shadow:inset 0 1px 2px rgba(0,0,0,0.1);transition:all 0.2s ease;}"
                                        "input:focus,select:focus{border-color:#80bdff;outline:none;box-shadow:0 0 5px rgba(0,123,255,0.5);}"
                                        "input:hover,select:hover{border-color:#999;}"
                                        ".reset-button{position:absolute;top:0px;right:0px;}"
                                        ".reset-button input{padding:5px;}"
                                        "tr{height:26px;}"
                                        "table.inner td:first-child{width:100px;}"
                                        "table.inner td:first-child:hover{cursor: pointer;}"
                                        "table.inner td:nth-child(2){width:80px;}"
                                        "table.basic-info-table td:first-child{width:180px;}"
                                        "table.other-settings-table td:first-child{width:200px;}"
                                        "table.onewire-table td:first-child{width:80px;font-size:13px;}"
                                        "table.onewire-table td:nth-child(2){width:130px;font-size:13px;}"
                                        "table.onewire-table td:nth-child(3),table.onewire-table td:nth-child(4),table.onewire-table td:nth-child(5){width:40px;font-size:13px;}"
                                        "table.onewire-table input{width:120px;}"
                                        ".no-border{border:none;}"
                                        "</style>"));

                        // JavaScript for updating UI
                        client.println(F("<script>"));
                        client.println(F("let firstUpdate=true;let activePWMField=null;let pendingCommands={};let updateIntervalId=null;"));
                        client.println(F("function updateStatus(){"));
                        client.println(F("fetch('/status').then(response => response.json()).then(data => {"));
                        // Update relays
                        client.print(F("for(let i=0;i<")); client.print(numOfRelays); client.println(F(";i++){"
                                        "let relay=document.getElementById('relay'+(i+1));if(relay)relay.checked=data.relays[i];}"));
                        // Update HSS
                        client.print(F("for(let i=0;i<")); client.print(numOfHSSwitches); client.println(F(";i++){"
                                        "let hss=document.getElementById('hss'+(i+1));let hssPWM=document.getElementById('hssPWM'+(i+1));"
                                        "if(hss&&data.hss&&data.hss[i]!==undefined&&!pendingCommands['hss'+(i+1)])hss.checked=data.hss[i]===1;"
                                        "if(hssPWM&&data.hssPWM&&data.hssPWM[i]!==undefined&&window.activePWMField!=='hssPWM'+(i+1))hssPWM.value=data.hssPWM[i]||0;}"));
                        // Update LSS
                        client.print(F("for(let i=0;i<")); client.print(numOfLSSwitches); client.println(F(";i++){"
                                        "let lss=document.getElementById('lss'+(i+1));let lssPWM=document.getElementById('lssPWM'+(i+1));"
                                        "if(lss&&data.lss&&data.lss[i]!==undefined&&!pendingCommands['lss'+(i+1)])lss.checked=data.lss[i]===1;"
                                        "if(lssPWM&&data.lssPWM&&data.lssPWM[i]!==undefined&&window.activePWMField!=='lssPWM'+(i+1))lssPWM.value=data.lssPWM[i]||0;}"));
                        // Update digital inputs
                        client.println(F("for(let i=0;i<data.digInputs.length;i++){"
                                        "let statusElem=document.getElementById('di_status'+(i+1));"
                                        "if(statusElem){let statusText=data.digInputs[i];"
                                        "if(data.pulseOn&&(i===9||i===10||i===11))statusText+=' (count: '+data.pulseCounts[i-9]+')';"
                                        "statusElem.textContent=statusText;}}"));
                        // Update analog inputs
                        client.println(F("for(let i=0;i<data.anaInputs.length;i++){"
                                        "let statusElem=document.getElementById('ai_status'+(i+1));"
                                        "if(statusElem)statusElem.textContent=data.anaInputs[i]+' ('+data.anaInputsVoltage[i].toFixed(1)+'V)';}"));
                        // Update analog outputs
                        client.print(F("for(let i=0;i<")); client.print(numOfAnaOuts); client.println(F(";i++){"
                                        "let anaOut=document.getElementById('anaOut_'+(i+1));if(anaOut)anaOut.value=data.anaOuts[i]||0;"
                                        "let anaOutVoltage=document.getElementById('anaOutVoltage'+(i+1));"
                                        "if(anaOutVoltage)anaOutVoltage.textContent='('+data.anaOutsVoltage[i].toFixed(1)+'V)';}"));
                        // Update DS2438 sensors
                        client.println(F("for(let i=0;i<data.ds2438.length;i++){"
                                        "let snElem=document.getElementById('ds2438_sn'+(i+1));"
                                        "let tempElem=document.getElementById('ds2438_temp'+(i+1));"
                                        "let vadElem=document.getElementById('ds2438_vad'+(i+1));"
                                        "let vsensElem=document.getElementById('ds2438_vsens'+(i+1));"
                                        "if(snElem&&tempElem&&vadElem&&vsensElem){"
                                        "snElem.textContent=data.ds2438[i].sn;"
                                        "tempElem.textContent=data.ds2438[i].temp+'C';"
                                        "vadElem.textContent=data.ds2438[i].vad+'V';"
                                        "vsensElem.textContent=data.ds2438[i].vsens+'V';}}"));
                        // Update DS18B20 sensors
                        client.println(F("for(let i=0;i<data.ds18b20.length;i++){"
                                        "let snElem=document.getElementById('ds18b20_sn'+(i+1));"
                                        "let tempElem=document.getElementById('ds18b20_temp'+(i+1));"
                                        "if(snElem&&tempElem){snElem.textContent=data.ds18b20[i].sn;tempElem.textContent=data.ds18b20[i].temp+'C';}}"));
                        // Initial UI update
                        client.println(F("if(firstUpdate){"
                                        "let oneWireCycle=document.getElementById('oneWireCycle');if(oneWireCycle)oneWireCycle.value=data.oneWireCycle||30000;"
                                        "let anaInputCycle=document.getElementById('anaInputCycle');if(anaInputCycle)anaInputCycle.value=data.anaInputCycle||10000;"
                                        "let useUDPctrl=document.getElementById('useUDPctrl');if(useUDPctrl)useUDPctrl.value=data.useUDPctrl.toString();"
                                        "let pulseOn=document.getElementById('pulseOn');if(pulseOn)pulseOn.value=data.pulseOn.toString();"
                                        "let gatewayEnabled=document.getElementById('gatewayEnabled');if(gatewayEnabled)gatewayEnabled.value=data.gatewayEnabled.toString();"
                                        "let pulseSendCycle=document.getElementById('pulseSendCycle');if(pulseSendCycle)pulseSendCycle.value=data.pulseSendCycle||10000;"
                                        "let checkInterval=document.getElementById('checkInterval');if(checkInterval)checkInterval.value=data.checkInterval||10000;"
                                        "let baudRate=document.getElementById('baudRate');if(baudRate)baudRate.value=data.baudRate||115200;"
                                        "if(updateIntervalId)clearInterval(updateIntervalId);updateIntervalId=setInterval(updateStatus,2000);"));
                        client.print(F("for(let i=0;i<")); client.print(numOfAnaOuts); client.println(F(";i++){"
                                        "let anaOut=document.getElementById('anaOut_'+(i+1));if(anaOut)anaOut.value=data.anaOuts[i]||0;"
                                        "let anaOutVoltage=document.getElementById('anaOutVoltage'+(i+1));"
                                        "if(anaOutVoltage)anaOutVoltage.textContent='('+data.anaOutsVoltage[i].toFixed(1)+'V)';}"));
                        client.print(F("for(let i=0;i<")); client.print(numOfRelays); client.println(F(";i++){"
                                        "let aliasInput=document.getElementById('alias_relay_'+(i+1));if(aliasInput)aliasInput.value=data.alias_relays[i]||'';}"));
                        client.print(F("for(let i=0;i<")); client.print(numOfHSSwitches); client.println(F(";i++){"
                                        "let aliasInput=document.getElementById('alias_hss_'+(i+1));if(aliasInput)aliasInput.value=data.alias_hss[i]||'';}"));
                        client.print(F("for(let i=0;i<")); client.print(numOfLSSwitches); client.println(F(";i++){"
                                        "let aliasInput=document.getElementById('alias_lss_'+(i+1));if(aliasInput)aliasInput.value=data.alias_lss[i]||'';}"));
                        client.print(F("for(let i=0;i<")); client.print(numOfAnaOuts); client.println(F(";i++){"
                                        "let aliasInput=document.getElementById('alias_ao_'+(i+1));if(aliasInput)aliasInput.value=data.alias_anaOuts[i]||'';}"));
                        client.println(F("for(let i=0;i<data.alias_ds2438.length;i++){"
                                        "let aliasInput=document.getElementById('alias_ds2438_'+(i+1));if(aliasInput)aliasInput.value=data.alias_ds2438[i]||'';}"
                                        "for(let i=0;i<data.alias_ds18b20.length;i++){"
                                        "let aliasInput=document.getElementById('alias_ds18b20_'+(i+1));if(aliasInput)aliasInput.value=data.alias_ds18b20[i]||'';}"
                                        "let debugEnabled=document.getElementById('debugEnabled');if(debugEnabled)debugEnabled.value=data.debugEnabled.toString();"
                                        "let serialNumberInput=document.getElementById('serialNumber');if(serialNumberInput)serialNumberInput.value=data.serialNumber||'';"
                                        "if(serialNumberInput){if(data.serialLocked)serialNumberInput.classList.add('no-border');"
                                        "else serialNumberInput.classList.remove('no-border');}"
                                        "firstUpdate=false;}"
                                        "});}"));
                        client.println(F("function sendCommand(param,value){"
                                        "if(param.startsWith('hss')||param.startsWith('lss'))pendingCommands[param]=true;"
                                        "fetch('/command?'+param+'='+value).then(response=>response.text()).then(data=>{"
                                        "if(param.startsWith('hss')||param.startsWith('lss'))delete pendingCommands[param];"
                                        "updateStatus();});}"
                                        "document.addEventListener('DOMContentLoaded',updateStatus);"));
                        client.println(F("</script></head>"));

                        // HTML body
                        client.println(F("<body><div class='container'>"
                                        "<div class='reset-button'>"
                                        "<input type='button' onclick=\"window.location.href='/info'\" value='Protocol Docs'>&nbsp;"
                                        "<input type='button' onclick=\"window.open('https://github.com/PavSedl/RAILDUINO', '_blank')\" value='Github'>&nbsp;"
                                        "<input type='button' onclick=\"if(confirm('The module will reboot. Continue?')) window.location.href='/reboot'\" value='Save&Reboot'>"
                                        "</div><h2>Railduino Control Panel</h2>"));

                        // First row: Basic Info and Settings
                        client.println(F("<div style='margin-bottom:10px;'><table class='outer'><tr><td width='50%'>"
                                        "<h3>Basic Information</h3><table class='basic-info-table'>"
                                        "<tr><td>DIP Switch Address</td><td>")); client.print(boardAddress); client.println(F("</td></tr>"
                                        "<tr><td>IP Address (DHCP)</td><td>")); client.print(Ethernet.localIP()); client.println(F("</td></tr>"
                                        "<tr><td>Gateway</td><td>")); client.print(Ethernet.gatewayIP()); client.println(F("</td></tr>"
                                        "<tr><td>MAC Address</td><td>"));
                        for (int i = 0; i < 6; i++) {
                            if (mac[i] < 16) client.print(F("0"));
                            client.print(mac[i], HEX);
                            if (i < 5) client.print(F(":"));
                        }
                        client.print(F("</td></tr><tr><td>Description</td><td><input type='text' id='description' maxlength='30' value='")); 
                        client.print(urlDecode(String(description))); 
                        client.print(F("' onchange='sendCommand(\"description\",this.value)'></td></tr><tr><td>Serial Number</td><td><input type='text' id='serialNumber' maxlength='30' value='"));
                        client.print(urlDecode(String(serialNumber)));
                        client.print(F("' onchange='sendCommand(\"serialNumber\",this.value)'></td></tr><tr><td>Hardware version</td><td>"));
                        client.print(hwVer);
                        client.print(F("</td></tr><tr><td>Firmware version</td><td>"));
                        client.print(fwVer);
                        client.print(F("</td></tr><tr><td></td><td><tr><td></td><td></td></tr>"));
                        client.print(F("</td></tr></table></td><td width='50%'><h3>Other settings</h3><table class='other-settings-table'><tr><td>1-Wire Cycle</td><td><input type='number' id='oneWireCycle' min='5000' max='600000' style='width:80px;' value='"));
                        client.print(String(oneWireCycle));
                        client.print(F("' onchange='sendCommand(\"oneWireCycle\",this.value)'> ms</td></tr><tr><td>Analog Input Cycle</td><td><input type='number' id='anaInputCycle' min='2000' max='600000' style='width:80px;' value='"));
                        client.print(String(anaInputCycle));
                        client.print(F("' onchange='sendCommand(\"anaInputCycle\",this.value)'> ms</td></tr><tr><td>Pulses Send Cycle</td><td><input type='number' id='pulseSendCycle' min='2000' max='600000' style='width:80px;' value='"));
                        client.print(String(pulseSendCycle));
                        client.print(F("' onchange='sendCommand(\"pulseSendCycle\",this.value)'> ms</td></tr><tr><td>Ping DHCP Cycle</td><td><input type='number' id='checkInterval' min='2000' max='600000' style='width:80px;' value='"));
                        client.print(String(checkInterval));
                        client.print(F("' onchange='sendCommand(\"checkInterval\",this.value)'> ms</td></tr><tr><td>RS485 Baud Rate</td><td><input type='number' id='baudRate' style='width:80px;' value='"));
                        client.print(String(baudRate));
                        client.print(F("' onchange='sendCommand(\"baudRate\",this.value)'> Bd</td></tr>"));
                        client.print(F("<tr><td>Outputs control protocol</td><td><select id='useUDPctrl' onchange='sendCommand(\"useUDPctrl\",this.value)'>"));
                        client.print(F("<option value='0' "));
                        if (!useUDPctrl) client.print(F("selected"));
                        client.print(F(">Modbus TCP</option><option value='1' "));
                        if (useUDPctrl) client.print(F("selected"));
                        client.print(F(">UDP</option></select></td></tr><tr><td>Pulses Sensing (DI 10,11,12)</td><td><select id='pulseOn' onchange='sendCommand(\"pulseOn\",this.value)'>"));
                        client.print(F("<option value='0' "));
                        if (!pulseOn) client.print(F("selected"));
                        client.print(F(">Off</option><option value='1' "));
                        if (pulseOn) client.print(F("selected"));
                        client.print(F(">On</option></select></td></tr><tr><td>LAN-RS485 Gateway</td><td><select id='gatewayEnabled' onchange='sendCommand(\"gatewayEnabled\",this.value)'>"));
                        client.print(F("<option value='0' "));
                        if (!gatewayEnabled) client.print(F("selected"));
                        client.print(F(">Off</option><option value='1' "));
                        if (gatewayEnabled) client.print(F("selected"));
                        client.print(F(">On</option></select></td></tr><tr><td>Serial Debug (115200 Bd)</td><td><select id='debugEnabled' onchange='sendCommand(\"debugEnabled\",this.value)'>"));
                        client.print(F("<option value='0' "));
                        if (!debugEnabled) client.print(F("selected"));
                        client.print(F(">Off</option><option value='1' "));
                        if (debugEnabled) client.print(F("selected"));
                        client.print(F(">On</option></select></td></tr></table></td></tr></table></div>"));

                        // Second row: Relay Status
                        client.println(F("<div style='margin-bottom:10px;'><table class='outer'><tr><td width='50%'>"
                                        "<h3>Relay Status</h3><table class='inner'>"));
                        for (int i = 0; i < numOfRelays; i++) {
                            client.print(F("<tr><td title=\"Modbus Register "));
                            if (i < 8) {
                                client.print(F("0, Bit "));
                                client.print(i); // Bity 0-7 pro relé 1-8
                            } else {
                                client.print(F("1, Bit "));
                                client.print(16 + (i - 8)); // Bity 16-19 pro relé 9-12
                            }
                            client.print(F("\nUDP: 'rail"));
                            client.print(boardAddress);
                            client.print(F(" ro"));
                            client.print(i + 1);
                            client.print(F(" on'\nUDP: 'rail"));
                            client.print(boardAddress);
                            client.print(F(" ro"));
                            client.print(i + 1);
                            client.print(F(" off'\">Relay "));
                            client.print(i + 1);
                            client.print(F("</td><td>"
                                        "<input type='checkbox' id='relay"));
                            client.print(i + 1);
                            client.print(F("' name='relay"));
                            client.print(i + 1);
                            client.print(F("' onchange='sendCommand(\"relay"));
                            client.print(i + 1);
                            client.print(F("\",this.checked?1:0)'"));
                            if (bitRead(Mb.MbData[i < 8 ? relOut1Byte : relOut2Byte], i % 8)) client.print(F(" checked"));
                            client.print(F("></td><td><input type='text' maxlength='30' id='alias_relay_"));
                            client.print(i + 1);
                            client.print(F("' name='alias_relay_"));
                            client.print(i + 1);
                            client.print(F("' value='"));
                            EEPROM.get(EEPROM_ALIAS_RELAYS + i * 41, tempAlias);
                            client.print(urlDecode(tempAlias));
                            client.print(F("' onchange='sendCommand(\"alias_relay_"));
                            client.print(i + 1);
                            client.print(F("\",encodeURIComponent(this.value))'>"));
                            client.println(F("</td></tr>"));
                        }
                        client.println(F("</table></td><td width='50%'>"));

                        // Second row: HSS and LSS Status
                        client.println(F("<table class='inner'><tr><td><h3>High-Side Switches Status (0-255 = 0-V+)</h3><table class='inner'>"));
                        for (int i = 0; i < numOfHSSwitches; i++) {
                            client.print(F("<tr><td title=\"Modbus reg 2 - bit "));
                            client.print(32 + i); // Bity 32-35 pro HSS 1-4
                            client.print(F("\nUDP: 'rail"));
                            client.print(boardAddress);
                            client.print(F(" ho"));
                            client.print(i + 1);
                            client.print(F(" on'\nUDP: 'rail"));
                            client.print(boardAddress);
                            client.print(F(" ho"));
                            client.print(i + 1);
                            client.print(F(" off'\nUDP: 'rail"));
                            client.print(boardAddress);
                            client.print(F(" ho"));
                            client.print(i + 1);
                            client.print(F("_pwm VALUE'\">HSS "));
                            client.print(i + 1);
                            client.print(F("</td><td>"
                                        "<input type='checkbox' id='hss"));
                            client.print(i + 1);
                            client.print(F("' name='hss"));
                            client.print(i + 1);
                            client.print(F("' onchange='sendCommand(\"hss"));
                            client.print(i + 1);
                            client.print(F("\",this.checked?255:0)'"));
                            if (bitRead(Mb.MbData[hssLssByte], i)) client.print(F(" checked"));
                            client.print(F("></td><td><input type='number' id='hssPWM"));
                            client.print(i + 1);
                            client.print(F("' name='hssPWM"));
                            client.print(i + 1);
                            client.print(F("' min='0' max='255' value='"));
                            client.print(Mb.MbData[hssPWM1Byte + i]);
                            client.print(F("' onchange='sendCommand(\"hss"));
                            client.print(i + 1);
                            client.print(F("\",this.value)'>"));
                            client.print(F("</td><td><input type='text' maxlength='30' id='alias_hss_"));
                            client.print(i + 1);
                            client.print(F("' name='alias_hss_"));
                            client.print(i + 1);
                            client.print(F("' value='"));
                            EEPROM.get(EEPROM_ALIAS_HSS + i * 41, tempAlias);
                            client.print(urlDecode(tempAlias));
                            client.print(F("' onchange='sendCommand(\"alias_hss_"));
                            client.print(i + 1);
                            client.print(F("\",encodeURIComponent(this.value))'>"));
                            client.println(F("</td></tr>"));
                        }
                        client.println(F("</table></td></tr><tr><td><h3>Low-Side Switches Status (0-255 = 0-V+)</h3><table class='inner'>"));
                        for (int i = 0; i < numOfLSSwitches; i++) {
                            client.print(F("<tr><td title=\"Modbus reg 2 - bit "));
                            client.print(36 + i); // Bity 36-39 pro LSS 1-4
                            client.print(F("\nUDP: 'rail"));
                            client.print(boardAddress);
                            client.print(F(" lo"));
                            client.print(i + 1);
                            client.print(F(" on'\nUDP: 'rail"));
                            client.print(boardAddress);
                            client.print(F(" lo"));
                            client.print(i + 1);
                            client.print(F(" off'"));
                            if (i != 3) { // PWM není podporováno pro LSS4
                                client.print(F("\nUDP: 'rail"));
                                client.print(boardAddress);
                                client.print(F(" lo"));
                                client.print(i + 1);
                                client.print(F("_pwm VALUE'"));
                            }
                            client.print(F("\">LSS "));
                            client.print(i + 1);
                            client.print(F("</td><td>"
                                        "<input type='checkbox' id='lss"));
                            client.print(i + 1);
                            client.print(F("' name='lss"));
                            client.print(i + 1);
                            client.print(F("' onchange='sendCommand(\"lss"));
                            client.print(i + 1);
                            client.print(F("\",this.checked?255:0)'"));
                            if (bitRead(Mb.MbData[hssLssByte], i + numOfHSSwitches)) client.print(F(" checked"));
                            client.print(F("></td><td>"));
                            if (i != 3) {
                                client.print(F("<input type='number' id='lssPWM"));
                                client.print(i + 1);
                                client.print(F("' name='lssPWM"));
                                client.print(i + 1);
                                client.print(F("' min='0' max='255' value='"));
                                client.print(Mb.MbData[lssPWM1Byte + i]);
                                client.print(F("' onchange='sendCommand(\"lss"));
                                client.print(i + 1);
                                client.print(F("\",this.value)'>"));
                            } else {
                                client.print(F("-"));
                            }
                            client.print(F("</td><td><input type='text' maxlength='30' id='alias_lss_"));
                            client.print(i + 1);
                            client.print(F("' name='alias_lss_"));
                            client.print(i + 1);
                            client.print(F("' value='"));
                            EEPROM.get(EEPROM_ALIAS_LSS + i * 41, tempAlias);
                            client.print(urlDecode(tempAlias));
                            client.print(F("' onchange='sendCommand(\"alias_lss_"));
                            client.print(i + 1);
                            client.print(F("\",encodeURIComponent(this.value))'>"));
                            client.println(F("</td></tr>"));
                        }
                        client.println(F("</table></td></tr></table></td></tr></table></div>"));

                        // Third row: Digital Inputs
                        client.println(F("<div style='margin-bottom:10px;'><table class='outer'><tr>"
                                        "<td width='50%'><h3 class='dig-inputs-title'>Digital Inputs 1-12</h3><table class='inner' id='digInputsTable1'><tbody>"));
                        for (int i = 0; i < 12; i++) {
                            client.print(F("<tr><td title=\"Modbus reg "));
                            if (i < 8) {
                                client.print(F("13 - bit "));
                                client.print(208 + i); // Bity 208-215 pro DI 1-8
                            } else {
                                client.print(F("14 - bit "));
                                client.print(216 + (i - 8)); // Bity 216-223 pro DI 9-12
                            }
                            client.print(F("\nUDP: 'rail"));
                            client.print(boardAddress);
                            client.print(F(" di"));
                            client.print(i + 1);
                            client.print(F(" VALUE'\">DI "));
                            client.print(i + 1);
                            client.print(F("</td><td id='di_status"));
                            client.print(i + 1);
                            client.print(F("'>"));
                            client.print(1 - inputStatus[i]);
                            client.print(F("</td><td><input type='text' maxlength='30' id='alias_digInput_"));
                            client.print(i + 1);
                            client.print(F("' name='alias_digInput_"));
                            client.print(i + 1);
                            client.print(F("' value='"));
                            EEPROM.get(EEPROM_ALIAS_DIGINPUTS + i * 41, tempAlias);
                            client.print(urlDecode(tempAlias));
                            client.print(F("' onchange='sendCommand(\"alias_digInput_"));
                            client.print(i + 1);
                            client.print(F("\",encodeURIComponent(this.value))'>"));
                            client.println(F("</td></tr>"));
                        }
                        client.println(F("</tbody></table></td><td width='50%'><h3 class='dig-inputs-title'>Digital Inputs 13-24</h3><table class='inner' id='digInputsTable2'><tbody>"));
                        for (int i = 12; i < 24; i++) {
                            client.print(F("<tr><td title=\"Modbus reg "));
                            if (i < 16) {
                                client.print(F("14 - bit "));
                                client.print(216 + (i - 8)); // Bity 216-223 pro DI 9-16
                            } else {
                                client.print(F("15 - bit "));
                                client.print(224 + (i - 16)); // Bity 224-231 pro DI 17-24
                            }
                            client.print(F("\nUDP: 'rail"));
                            client.print(boardAddress);
                            client.print(F(" di"));
                            client.print(i + 1);
                            client.print(F(" VALUE'\">DI "));
                            client.print(i + 1);
                            client.print(F("</td><td id='di_status"));
                            client.print(i + 1);
                            client.print(F("'>"));
                            client.print(1 - inputStatus[i]);
                            client.print(F("</td><td><input type='text' maxlength='30' id='alias_digInput_"));
                            client.print(i + 1);
                            client.print(F("' name='alias_digInput_"));
                            client.print(i + 1);
                            client.print(F("' value='"));
                            EEPROM.get(EEPROM_ALIAS_DIGINPUTS + i * 41, tempAlias);
                            client.print(urlDecode(tempAlias));
                            client.print(F("' onchange='sendCommand(\"alias_digInput_"));
                            client.print(i + 1);
                            client.print(F("\",encodeURIComponent(this.value))'>"));
                            client.println(F("</td></tr>"));
                        }
                        client.println(F("</tbody></table></td></tr></table></div>"));

                        // Fourth row: Analog Inputs and Outputs
                        client.println(F("<div style='margin-bottom:10px;'><table class='outer'><tr><td width='50%'>"
                                        "<h3>Analog Inputs (0-1024 = 0-10V)</h3><table class='inner' id='anaInputsTable'><tbody>"));
                        for (int i = 0; i < numOfAnaInputs; i++) {
                            client.print(F("<tr><td title=\"Modbus reg "));
                            client.print(16 + i); // Registry 16-17 pro AI 1-2
                            client.print(F("\nUDP: 'rail"));
                            client.print(boardAddress);
                            client.print(F(" ai"));
                            client.print(i + 1);
                            client.print(F(" VALUE'\">AI "));
                            client.print(i + 1);
                            client.print(F("</td><td id='ai_status"));
                            client.print(i + 1);
                            client.print(F("'>"));
                            client.print(analogStatus[i], 2);
                            client.print(F("</td><td>"
                                        "<input type='text' maxlength='30' id='alias_anaInput_"));
                            client.print(i + 1);
                            client.print(F("' name='alias_anaInput_"));
                            client.print(i + 1);
                            client.print(F("' value='"));
                            EEPROM.get(EEPROM_ALIAS_ANA_INPUTS + i * 41, tempAlias);
                            client.print(urlDecode(tempAlias));
                            client.print(F("' onchange='sendCommand(\"alias_anaInput_"));
                            client.print(i + 1);
                            client.print(F("\",encodeURIComponent(this.value))'>"));
                            client.println(F("</td></tr>"));
                        }
                        client.println(F("</tbody></table></td><td width='50%'><h3>Analog Outputs (0-255 = 0-10V)</h3><table class='inner'>"));
                        for (int i = 0; i < numOfAnaOuts; i++) {
                            client.print(F("<tr><td title=\"Modbus reg "));
                            client.print(11 + i); // Registry 11-12 pro AO 1-2
                            client.print(F("\nUDP: 'rail"));
                            client.print(boardAddress);
                            client.print(F(" ao"));
                            client.print(i + 1);
                            client.print(F(" VALUE'\">AO "));
                            client.print(i + 1);
                            client.print(F("</td><td>"
                                        "<input type='number' id='anaOut"));
                            client.print(i + 1);
                            client.print(F("' name='anaOut"));
                            client.print(i + 1);
                            client.print(F("' min='0' max='255' value='"));
                            client.print(Mb.MbData[anaOut1Byte + i]);
                            client.print(F("' onchange='sendCommand(\"anaOut"));
                            client.print(i + 1);
                            client.print(F("\",this.value)'>"));
                            client.print(F("</td><td id='anaOutVoltage"));
                            client.print(i + 1);
                            client.print(F("'>("));
                            client.print(anaOutsVoltage[i], 1);
                            client.print(F("V)</td><td>"
                                        "<input type='text' maxlength='30' id='alias_anaOut_"));
                            client.print(i + 1);
                            client.print(F("' name='alias_anaOut_"));
                            client.print(i + 1);
                            client.print(F("' value='"));
                            EEPROM.get(EEPROM_ALIAS_ANA_OUTS + i * 41, tempAlias);
                            client.print(urlDecode(tempAlias));
                            client.print(F("' onchange='sendCommand(\"alias_anaOut_"));
                            client.print(i + 1);
                            client.print(F("\",encodeURIComponent(this.value))'>"));
                            client.println(F("</td></tr>"));
                        }
                        client.println(F("</table></td></tr></table></div>"));

                        // Fifth row: 1-Wire Sensors
                        client.println(F("<div style='margin-bottom:10px;'><table class='outer'><tr><td width='50%'>"
                                        "<h3>1-Wire Sensors DS2438</h3><table class='onewire-table' id='ds2438Table'><tbody>"));
                        for (int i = 0; i < DS2438count; i++) {
                            client.print(F("<tr><td title=\"Modbus reg "));
                            if (i == 0) {
                                client.print(F("19-21 (Temp, Vad, Vsens)")); // První DS2438 senzor
                            } else {
                                client.print(F("46 (Sensor "));
                                client.print(i + 1);
                                client.print(F(")")); // Další senzory v registru 46
                            }
                            client.print(F("\nUDP: 'rail"));
                            client.print(boardAddress);
                            client.print(F(" 1w SERIAL_NUMBER Temp Vad Vsens'\">DS2438-"));
                            client.print(i + 1);
                            client.print(F("</td><td id='ds2438_sn"));
                            client.print(i + 1);
                            client.print(F("'>"));
                            client.print(oneWireAddressToString(sensors2438[i]));
                            client.print(F("</td><td id='ds2438_temp"));
                            client.print(i + 1);
                            client.print(F("'></td><td id='ds2438_vad"));
                            client.print(i + 1);
                            client.print(F("'></td><td id='ds2438_vsens"));
                            client.print(i + 1);
                            client.print(F("'></td><td><input type='text' maxlength='30' id='alias_ds2438_"));
                            client.print(i + 1);
                            client.print(F("' name='alias_ds2438_"));
                            client.print(i + 1);
                            client.print(F("' value='"));
                            EEPROM.get(EEPROM_ALIAS_DS2438 + i * 41, tempAlias);
                            client.print(urlDecode(tempAlias));
                            client.print(F("' onchange='sendCommand(\"alias_ds2438_"));
                            client.print(i + 1);
                            client.print(F("\",encodeURIComponent(this.value))'>"));
                            client.println(F("</td></tr>"));
                        }
                        client.println(F("</tbody></table></td><td width='50%'><h3>1-Wire Sensors DS18B20</h3><table class='onewire-table' id='ds18b20Table'><tbody>"));
                        for (int i = 0; i < DS18B20count; i++) {
                            client.print(F("<tr><td title=\"Modbus reg "));
                            client.print(47 + i); // Registry 47-57 pro DS18B20
                            client.print(F("\nUDP: 'rail"));
                            client.print(boardAddress);
                            client.print(F(" 1w SERIAL_NUMBER Temp'\">DS18B20-"));
                            client.print(i + 1);
                            client.print(F("</td><td id='ds18b20_sn"));
                            client.print(i + 1);
                            client.print(F("'>"));
                            client.print(oneWireAddressToString(sensors18B20[i]));
                            client.print(F("</td><td id='ds18b20_temp"));
                            client.print(i + 1);
                            client.print(F("'></td><td><input type='text' maxlength='30' id='alias_ds18b20_"));
                            client.print(i + 1);
                            client.print(F("' name='alias_ds18b20_"));
                            client.print(i + 1);
                            client.print(F("' value='"));
                            EEPROM.get(EEPROM_ALIAS_DS18B20 + i * 41, tempAlias);
                            client.print(urlDecode(tempAlias));
                            client.print(F("' onchange='sendCommand(\"alias_ds18b20_"));
                            client.print(i + 1);
                            client.print(F("\",encodeURIComponent(this.value))'>"));
                            client.println(F("</td></tr>"));
                        }
                        client.println(F("</tbody></table></td></tr></table></div></div></body></html>"));
                    } else {
                        client.println(F("HTTP/1.1 404 Not Found"));
                        client.println(F("Content-Type: text/plain"));
                        client.println(F("Connection: close"));
                        client.println();
                        client.println(F("404 Not Found"));
                    }
                }
                break;
            }
            if (c == '\n') {
                currentLineIsBlank = true;
            } else if (c != '\r') {
                currentLineIsBlank = false;
            }
        }
    }
    client.stop();
}


// Initialize the device
void setup() {

    // Generic pin and command initialization
    auto initPins = [](int count, const int* pins) {
        for (int i = 0; i < count; i++) {
            pinMode(pins[i], OUTPUT);
            digitalWrite(pins[i], LOW);
        }
    };

    // Initialize pins
    for (int i = 0; i < numOfDigInputs; i++) {
        pinMode(inputPins[i], INPUT);
        inputStatus[i] = 1;
        inputStatusNew[i] = 0;
    }
    initPins(numOfRelays, relayPins);
    initPins(numOfHSSwitches, HSSwitchPins);
    initPins(numOfLSSwitches, LSSwitchPins);
    initPins(numOfAnaOuts, anaOutPins);
    for (int i = 0; i < numOfAnaInputs; i++) pinMode(analogPins[i], INPUT);
    for (int i = 0; i < numOfLedPins; i++) pinMode(ledPins[i], OUTPUT);

     // Initialize EEPROM variables with validation
    auto initEEPROM = [](int addr, bool& var, bool defaultVal) {
        EEPROM.get(addr, var);
        if (var != 0 && var != 1) {
            var = defaultVal;
            EEPROM.put(addr, var);
        }
    };
    auto initEEPROMUL = [](int addr, unsigned long& var, unsigned long defaultVal) {
        EEPROM.get(addr, var);
        if (var == 0 || var == 0xFFFFFFFF) { // Check for invalid values
            var = defaultVal;
            EEPROM.put(addr, var);
        }
    };
    
    // Set up timers
    statusLedTimerOn.sleep(statusLedTimeOn);
    statusLedTimerOff.sleep(statusLedTimeOff);
    oneWireTimer.sleep(oneWireCycle);
    pulseTimer.sleep(pulseSendCycle);
    checkEthernetTimer.sleep(checkInterval);

    initEEPROM(EEPROM_SERIALLOCK, serialLocked, false);
    initEEPROM(EEPROM_DEBUGON, debugEnabled, false);
    initEEPROM(EEPROM_PULSEON, pulseOn, false);
    initEEPROM(EEPROM_GATEWAYON, gatewayEnabled, false);
    initEEPROMUL(EEPROM_ANAINCYCLE, anaInputCycle, 10000UL);
    initEEPROMUL(EEPROM_LANCYCLE, checkInterval, 10000UL);
    initEEPROMUL(EEPROM_BAUDRATE, baudRate, 115200UL);

    // Initialize string arrays from EEPROM
    auto initStringArray = [](int count, int addr) {
        for (int i = 0; i < count; i++) {
            EEPROM.get(addr, tempAlias);
            int len = strnlen(tempAlias, 41);
            for (int j = len; j < 41; j++) tempAlias[j] = 0;
            if (!isValidAlias(tempAlias)) {
                tempAlias[0] = '\0';
                EEPROM.put(addr, tempAlias);
            }
            addr += 41;
        }
    };

    // Initialize single string from EEPROM
    auto initEEPROMString = [](int addr, char* var) {
        char temp[41];
        EEPROM.get(addr, temp);
        int len = strnlen(temp, 41);
        for (int j = len; j < 41; j++) temp[j] = 0; // Vynuluje garbage za řetězcem
        if (isValidAlias(temp)) {
            strcpy(var, temp);
        } else {
            var[0] = '\0';
            EEPROM.put(addr, var);
        }
    };

    initEEPROMString(EEPROM_SERIALNUMBER, serialNumber);
    initEEPROMString(EEPROM_DESCRIPTION, description);
    initStringArray(numOfRelays, EEPROM_ALIAS_RELAYS);
    initStringArray(numOfHSSwitches, EEPROM_ALIAS_HSS);
    initStringArray(numOfLSSwitches, EEPROM_ALIAS_LSS);
    initStringArray(numOfDigInputs, EEPROM_ALIAS_DIGINPUTS);
    initStringArray(numOfAnaInputs, EEPROM_ALIAS_ANA_INPUTS);
    initStringArray(numOfAnaOuts, EEPROM_ALIAS_ANA_OUTS);
    initStringArray(maxSensors, EEPROM_ALIAS_DS2438);
    initStringArray(maxSensors, EEPROM_ALIAS_DS18B20);

    // Initialize EEPROM if not already done
    byte initFlag;

    EEPROM.get(EEPROM_SERIALLOCK, serialLocked);
    EEPROM.get(EEPROM_DEBUGON, debugEnabled);
    EEPROM.get(EEPROM_PULSEON, pulseOn);
    EEPROM.get(EEPROM_GATEWAYON, gatewayEnabled);
    EEPROM.get(EEPROM_1WIRECYCLE, oneWireCycle);
    EEPROM.get(EEPROM_ANAINCYCLE, anaInputCycle);
    EEPROM.get(EEPROM_PULSECYCLE, pulseSendCycle);
    EEPROM.get(EEPROM_LANCYCLE, checkInterval);
    EEPROM.get(EEPROM_BAUDRATE, baudRate);
    EEPROM.get(EEPROM_DESCRIPTION, description);
    EEPROM.get(EEPROM_SERIALNUMBER, serialNumber);
    EEPROM.get(EEPROM_INITFLAG, initFlag);
    EEPROM.get(EEPROM_UDPCONTROLON, useUDPctrl);

    if (initFlag != 0xAA) {
        char emptyAlias[41] = "";
        EEPROM.put(EEPROM_SERIALLOCK, false);
        EEPROM.put(EEPROM_DEBUGON, false);
        EEPROM.put(EEPROM_PULSEON, false);
        EEPROM.put(EEPROM_GATEWAYON, false);
        for (int i = 0; i < numOfRelays; i++) EEPROM.put(EEPROM_ALIAS_RELAYS + i * 41, emptyAlias);
        for (int i = 0; i < numOfHSSwitches; i++) EEPROM.put(EEPROM_ALIAS_HSS + i * 41, emptyAlias);
        for (int i = 0; i < numOfLSSwitches; i++) EEPROM.put(EEPROM_ALIAS_LSS + i * 41, emptyAlias);
        for (int i = 0; i < numOfDigInputs; i++) EEPROM.put(EEPROM_ALIAS_DIGINPUTS + i * 41, emptyAlias);
        for (int i = 0; i < numOfAnaInputs; i++) EEPROM.put(EEPROM_ALIAS_ANA_INPUTS + i * 41, emptyAlias);
        for (int i = 0; i < numOfAnaOuts; i++) EEPROM.put(EEPROM_ALIAS_ANA_OUTS + i * 41, emptyAlias);
        for (int i = 0; i < maxSensors; i++) EEPROM.put(EEPROM_ALIAS_DS2438 + i * 41, emptyAlias);
        for (int i = 0; i < maxSensors; i++) EEPROM.put(EEPROM_ALIAS_DS18B20 + i * 41, emptyAlias);
        EEPROM.put(EEPROM_DESCRIPTION, emptyAlias);
        EEPROM.put(EEPROM_SERIALNUMBER, emptyAlias);
        EEPROM.put(EEPROM_LANCYCLE, 10000UL);
        EEPROM.put(EEPROM_1WIRECYCLE, 30000UL);
        EEPROM.put(EEPROM_ANAINCYCLE, 10000UL);
        EEPROM.put(EEPROM_PULSECYCLE, 10000UL);
        EEPROM.put(EEPROM_BAUDRATE, 115200UL);
        initFlag = 0xAA;
        EEPROM.put(EEPROM_INITFLAG, initFlag);
        dbg(F("EEPROM initialized with default values"));
    }

    // Attach interrupts for pulse counting
    if (pulseOn) {
        attachInterrupt(digitalPinToInterrupt(21), [](){pulse1++;}, FALLING);
        attachInterrupt(digitalPinToInterrupt(20), [](){pulse2++;}, FALLING);
        attachInterrupt(digitalPinToInterrupt(19), [](){pulse3++;}, FALLING);
    }

    Serial.begin(115200);
    
    dbgln(""); // Použije dbgln(const char*)
    char temp[40];
    snprintf(temp, sizeof(temp), "Railduino hardware version: %s", hwVer);
    dbgln(temp); // Použije dbgln(const char*)
    snprintf(temp, sizeof(temp), "Railduino firmware version: %s", fwVer);
    dbgln(temp); // Použije dbgln(const char*)

    // Read board address from DIP switches
    for (int i = 0; i < 4; i++) {
        pinMode(dipSwitchPins[i], INPUT);
        if (!digitalRead(dipSwitchPins[i])) boardAddress |= (1 << i);
    }
    boardAddressStr = String(boardAddress);
    strcpy_P(boardAddressRailStr, railStr); // Načte "rail" z PROGMEM
    strcat(boardAddressRailStr, boardAddressStr.c_str()); // Přidá číslo adresy

    // Initialize Ethernet
    ethernetOK = ethShieldDetected();
    dbgln(ethernetOK ? "Ethernet shield detected" : "Ethernet shield NOT detected");

    pinMode(dipSwitchPins[4], INPUT);
    if (!digitalRead(dipSwitchPins[4])) { dipSwitchEthOn = 1; dbgln("Ethernet enabled");
    } else { dipSwitchEthOn = 0; dbgln("Ethernet disabled"); }

    if (ethernetOK && dipSwitchEthOn) {
        mac[5] = 0xED + boardAddress;
        while (!Ethernet.begin(mac)) {
            dbgln("Attempting to obtain IP address...");
            digitalWrite(ledPins[0], HIGH);
            delay(1000);
        }
        if (useUDPctrl) {
            udpRecv.begin(listenPort);
        }
        udpSend.begin(sendPort);
        server.begin();
        dbgln("IP address: " + ipToString(Ethernet.localIP()));
        dhcpServer = Ethernet.gatewayIP();
        dbgln("Gateway (DHCP): " + ipToString(dhcpServer));
        digitalWrite(ledPins[0], LOW);
    }

    // Initialize MODBUS and RS485
    memset(Mb.MbData, 0, sizeof(Mb.MbData));
    modbus_configure(&Serial3, baudRate, SERIAL_8N1, boardAddress, serial3TxControl, sizeof(Mb.MbData), Mb.MbData);
    Serial3.begin(baudRate);
    Serial3.setTimeout(serial3TimeOut);
    pinMode(serial3TxControl, OUTPUT);
    digitalWrite(serial3TxControl, LOW);

    // Final initializations
    dbgln("Physical address: " + boardAddressStr);
    lookUpSensors();
    wdt_enable(WDTO_8S);
}

// Main loop
void loop() {
    wdt_reset(); // Reset watchdog timer

    // Handle webserver requests if Ethernet is enabled
    if (ethernetOK && dipSwitchEthOn) {
        handleWebServer();
        if (!useUDPctrl) {
            Mb.MbsRun(); 
        }
        IPrenew();
        if (checkEthernetTimer.isOver()) {
            checkEthernet();
            checkEthernetTimer.sleep(checkInterval);
        }
    }
    
    // Process RS485 packets
    if (!gatewayEnabled) {
        modbus_update();    // using Modbus RTU
    } else {
        processRS485ToLAN(); // using plain RS485 packets
    }

    // Read digital inputs
    readDigInputs();

    // Read analog inputs
    readAnaInputs();

    // Process incoming commands
    processCommands();

    // Process 1-Wire sensors
    processOnewire();

    // Update status LED
    statusLed();



    // Send pulse packet if pulse sensing is enabled
    if (pulseOn) {sendPulsePacket();}
}

bool isValidAlias(const char* alias) { 
    int len = strlen(alias), char_count = 0; // Check alias length and count characters
    if (len > 30) return false; 
    for (int i = 0; i < len; char_count++) { 
        if (alias[i] >= 32 && alias[i] <= 126) { i++; if (char_count >= 30) return false; continue; } // Validate ASCII
        if (i + 1 >= len) return false; // Ensure enough bytes for UTF-8
        bool valid = false; 
        for (size_t j = 0; j < sizeof(czech_chars) / sizeof(czech_chars[0]); j++) 
            if (alias[i] == czech_chars[j][0] && alias[i + 1] == czech_chars[j][1]) { valid = true; i += 2; break; } // Validate Czech UTF-8
        if (!valid || char_count >= 30) return false; 
    } 
    return true; // Returns true if alias contains only valid ASCII or Czech UTF-8 characters
}

String urlDecode(String input) { 
    String result = ""; 
    for (size_t i = 0; i < input.length(); i++) 
        if (input[i] == '%') { if (i + 2 < input.length()) { result += (char)strtol(input.substring(i + 1, i + 3).c_str(), NULL, 16); i += 2; } } 
        else result += input[i] == '+' ? ' ' : input[i]; // Decode %XX and + to space, copy others
    return result; // Returns URL-decoded string
}

void sendPulsePacket() {
    if (!pulseTimer.isOver()) {
        return;
    }
    pulseTimer.sleep(pulseSendCycle);
    sentPulse1 = pulse1;
    sentPulse2 = pulse2;
    sentPulse3 = pulse3;
    char prefix[5];
    strncpy_P(prefix, digInputStr, sizeof(prefix));
    prefix[sizeof(prefix) - 1] = '\0';
    sendMsg(String(prefix) + "10 " + String(sentPulse1));
    sendMsg(String(prefix) + "11 " + String(sentPulse2));
    sendMsg(String(prefix) + "12 " + String(sentPulse3));
    pulse1 = 0;
    pulse2 = 0;
    pulse3 = 0;
}

// Maintain Ethernet connection
void IPrenew() {
    byte rtnVal = Ethernet.maintain();
    switch(rtnVal) {
        case 1: dbgln(F("DHCP renew fail")); break;
        case 2: dbgln(F("DHCP renew ok")); break;
        case 3: dbgln(F("DHCP rebind fail")); break;
        case 4: dbgln(F("DHCP rebind ok")); break; 
    }
}

// Print IP address
String ipToString(IPAddress ip) {
    return String(ip[0]) + "." + ip[1] + "." + ip[2] + "." + ip[3];
}

// Control status LED
void statusLed() {
    if (statusLedTimerOff.isOver()) { 
        statusLedTimerOn.sleep(statusLedTimeOn);
        statusLedTimerOff.sleep(statusLedTimeOff);
        digitalWrite(ledPins[0], HIGH);  
    }  
    if (statusLedTimerOn.isOver()) { 
        digitalWrite(ledPins[0], LOW); 
    } 
}

// Convert 1-Wire address to string
String oneWireAddressToString(byte addr[]) {
    String s = "";
    for (int i = 0; i < 8; i++) {
        if(addr[i] < 0x10) {s += '0';}
        s += String(addr[i], HEX);
    }
    return s;
}

// Search for 1-Wire sensors
void lookUpSensors() {
    byte j=0, k=0, l=0, m=0;
    while ((j <= maxSensors) && (ds.search(sensors[j]))) {
        if (OneWire::crc8(sensors[j], 7) == sensors[j][7]) {
            if (sensors[j][0] == 38) {
                for (l=0; l<8; l++) { sensors2438[k][l]=sensors[j][l]; }  
                k++;  
            } else {
                for (l=0; l<8; l++) { sensors18B20[m][l]=sensors[j][l]; }
                m++;
                dssetresolution(ds, sensors[j], resolution);
            }
        }
        j++;
    }
    DS2438count = k;
    DS18B20count = m;
    dbg("1-wire sensors found: ");
    dbgln(String(k+m));
}

// Set resolution for DS18B20 sensors
void dssetresolution(OneWire ow, byte addr[8], byte resolution) {
    byte resbyte = 0x1F;
    if (resolution == 12) { resbyte = 0x7F; }
    else if (resolution == 11) { resbyte = 0x5F; }
    else if (resolution == 10) { resbyte = 0x3F; }
    ow.reset();
    ow.select(addr);
    ow.write(0x4E); // Write scratchpad
    ow.write(0); // Alarm trigger high
    ow.write(0); // Alarm trigger low
    ow.write(resbyte); // Configuration
    ow.write(0x48); // Copy scratchpad
}

// Send convert command to DS18B20 sensors
void dsconvertcommand(OneWire ow, byte addr[8]) {
    ow.reset();
    ow.select(addr);
    ow.write(0x44, 1); // Start temperature conversion
}

// Read temperature from DS18B20 sensor
float dsreadtemp(OneWire ow, byte addr[8]) {
    int i;
    byte data[12];
    float celsius;
    ow.reset();
    ow.select(addr);
    ow.write(0xBE); // Read scratchpad
    for (i = 0; i < 9; i++) { 
        data[i] = ow.read();
    }
    int16_t TReading = (data[1] << 8) | data[0];  
    celsius = 0.0625 * TReading;
    return celsius;
}

// Process 1-Wire sensors
void processOnewire() {
    static byte oneWireState = 0;
    static byte oneWireCnt = 0;
    switch(oneWireState) {
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
            if (oneWireCnt < DS2438count) {          
                ds2438.begin();
                ds2438.update(sensors2438[oneWireCnt]);
                if (!ds2438.isError()) {
                    Mb.MbData[oneWireTempByte + (oneWireCnt*3)] = ds2438.getTemperature()*100; 
                    Mb.MbData[oneWireVadByte + (oneWireCnt*3)] = ds2438.getVoltage(DS2438_CHA)*100; 
                    Mb.MbData[oneWireVsensByte + (oneWireCnt*3)] = ds2438.getVoltage(DS2438_CHB)*100; 
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
            if (oneWireCnt < DS18B20count) {  
                dsconvertcommand(ds, sensors18B20[oneWireCnt]);            
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
            if (oneWireCnt < DS18B20count) {  
                Mb.MbData[oneWireDS18B20Byte + oneWireCnt] = dsreadtemp(ds, sensors18B20[oneWireCnt])*100; 
                sendMsg("1w " + oneWireAddressToString(sensors18B20[oneWireCnt]) + " " + String(dsreadtemp(ds, sensors18B20[oneWireCnt]), 2));
                oneWireCnt++;
            } else {
                oneWireState = 0;
            }
            break;
    }
}

// Read digital inputs with debouncing
void readDigInputs() {
    int timestamp = millis();
    for (int i = 0; i < numOfDigInputs; i++) {      
        int oldValue = inputStatus[i];
        int newValue = inputStatusNew[i];
        int curValue = digitalRead(inputPins[i]);
        if (pulseOn && (i == 9 || i == 10 || i == 11)) { curValue = 1; }
        int byteNo = i/8;
        int bitPos = i - (byteNo*8);
        if (oldValue != newValue) {
            if (newValue != curValue) {
                inputStatusNew[i] = curValue;
            } else if (timestamp - inputChangeTimestamp[i] > debouncingTime) {
                inputStatus[i] = newValue;
                if (!newValue) {
                    bitWrite(Mb.MbData[digInp1Byte + byteNo], bitPos, 1);
                    sendInputOn(i + 1);
                } else {
                    bitWrite(Mb.MbData[digInp1Byte + byteNo], bitPos, 0);         
                    sendInputOff(i + 1);  
                }
            }
        } else {
            if (oldValue != curValue) {
                inputStatusNew[i] = curValue;
                inputChangeTimestamp[i] = timestamp;
            }
        }
    }
}

// Read analog inputs
void readAnaInputs() {
    if (!analogTimer.isOver()) {
        return;
    }
    analogTimer.sleep(anaInputCycle);
    for (int i = 0; i < numOfAnaInputs; i++) {
        int pin = analogPins[i];
        float value = analogRead(pin); // Read analog value
        float oldValue = analogStatus[i];
        analogStatus[i] = value;
        if (value != oldValue) {
            Mb.MbData[anaInp1Byte + i] = (int)value;
            sendAnaInput(i+1, Mb.MbData[anaInp1Byte + i]);
        }
    } 
}

void sendInputOn(int input) {
    char prefix[5];
    strncpy_P(prefix, digInputStr, sizeof(prefix));
    prefix[sizeof(prefix) - 1] = '\0';
    sendMsg(String(prefix) + String(input, DEC) + " 1");
}

void sendInputOff(int input) {
    char prefix[5];
    strncpy_P(prefix, digInputStr, sizeof(prefix));
    prefix[sizeof(prefix) - 1] = '\0';
    sendMsg(String(prefix) + String(input, DEC) + " 0");
}

void sendAnaInput(int input, float value) {
    char prefix[5];
    strncpy_P(prefix, anaInputStr, sizeof(prefix));
    prefix[sizeof(prefix) - 1] = '\0';
    sendMsg(String(prefix) + String(input, DEC) + " " + String(value, 2));
}

// Send a message via UDP
void sendMsg(String message) {
    char railPrefix[5];
    strncpy_P(railPrefix, railStr, sizeof(railPrefix));
    railPrefix[sizeof(railPrefix) - 1] = '\0';
    message = String(railPrefix) + boardAddressStr + " " + message;
    message.toCharArray(outputPacketBuffer, outputPacketBufferSize);
    digitalWrite(ledPins[1], HIGH);
    if (ethernetOK) {
        udpSend.beginPacket(sendIpAddress, remPort);
        udpSend.write(outputPacketBuffer, message.length());
        udpSend.endPacket();
    }
    digitalWrite(ledPins[1], LOW);
    dbg("Sending packet: ");
    dbgln(message);
}

// Set relay state
void setRelay(int relay, int value) {
    if (relay >= numOfRelays) {
        return;
    }
    digitalWrite(relayPins[relay], value);
}

// Set High-Side switch state or PWM
void setHSSwitch(int hsswitch, int value) {
    if (hsswitch >= numOfHSSwitches) {
        return;
    }
    analogWrite(HSSwitchPins[hsswitch], value); 
}

// Set Low-Side switch state or PWM
void setLSSwitch(int lsswitch, int value) {
    if (lsswitch >= numOfLSSwitches) {
        return;
    }
    if (lsswitch == 3) {
        if (value > 0) { digitalWrite(LSSwitchPins[lsswitch], 1); 
        } else { digitalWrite(LSSwitchPins[lsswitch], 0); }
    } else {
        analogWrite(LSSwitchPins[lsswitch], value); // Nastavit PWM
    }
}

// Set analog output value
void setAnaOut(int pwm, int value) {
    if (pwm >= numOfAnaOuts) {
        return;
    }
    analogWrite(anaOutPins[pwm], value);
}

// Receive and parse incoming packet
boolean receivePacket(String *cmd) {
    if (ethernetOK && useUDPctrl) {
        int packetSize = udpRecv.parsePacket();
        if (packetSize) {
            memset(inputPacketBuffer, 0, sizeof(inputPacketBuffer));
            udpRecv.read(inputPacketBuffer, inputPacketBufferSize);
            *cmd = String(inputPacketBuffer);
           if (strncmp(cmd->c_str(), boardAddressRailStr, strlen(boardAddressRailStr)) == 0) {
                *cmd = cmd->substring(strlen(boardAddressRailStr));
                cmd->trim();
                return true;
            }
        }
    }
    return false;
}

void processRS485ToLAN() {
    static String rs485Buffer = "";
    while (Serial3.available()) {
        char c = Serial3.read();
        rs485Buffer += c;
        if (c == '\n') {
            rs485Buffer.trim();
            if (rs485Buffer.length() > 0) {
                dbg("Received from RS485: ");
                dbgln(rs485Buffer);
                bool isLocal = strncmp(rs485Buffer.c_str(), boardAddressRailStr, strlen(boardAddressRailStr)) == 0;
                String cmd = isLocal ? rs485Buffer.substring(strlen(boardAddressRailStr)) : rs485Buffer;
                cmd.trim();
                dbgln("Processed RS485 command: " + cmd);

                if (isLocal) {
                    digitalWrite(ledPins[1], HIGH);  
                    byte byteNo, bitPos;
                    char prefix[5];
                    strncpy_P(prefix, relayStr, sizeof(prefix));
                    prefix[sizeof(prefix) - 1] = '\0';
                    if (strncmp(cmd.c_str(), prefix, strlen(prefix)) == 0) {
                        for (int i = 0; i < numOfRelays; i++) {
                            byteNo = (i < 8) ? relOut1Byte : relOut2Byte;
                            bitPos = (i < 8) ? i : i-8;
                            sprintf(cmdBuffer, "ro%d on", i + 1);
                            if (cmd == cmdBuffer) {
                                bitWrite(Mb.MbData[byteNo], bitPos, 1);
                            }
                            sprintf(cmdBuffer, "ro%d off", i + 1);
                            if (cmd == cmdBuffer) {
                                bitWrite(Mb.MbData[byteNo], bitPos, 0);
                            }
                        }
                    } else {
                        strncpy_P(prefix, HSSwitchStr, sizeof(prefix));
                        prefix[sizeof(prefix) - 1] = '\0';
                        if (strncmp(cmd.c_str(), prefix, strlen(prefix)) == 0) {
                            for (int i = 0; i < numOfHSSwitches; i++) {
                                sprintf(cmdBuffer, "ho%d on", i + 1);
                                if (cmd == cmdBuffer) {
                                    Mb.MbData[hssPWM1Byte + i] = 255;
                                    bitWrite(Mb.MbData[hssLssByte], i, 1);
                                    setHSSwitch(i, 255);
                                }
                                sprintf(cmdBuffer, "ho%d off", i + 1);
                                if (cmd == cmdBuffer) {
                                    Mb.MbData[hssPWM1Byte + i] = 0;
                                    bitWrite(Mb.MbData[hssLssByte], i, 0);
                                    setHSSwitch(i, 0);
                                }
                                sprintf(cmdBuffer, "ho%d_pwm ", i + 1);
                                if (cmd.startsWith(cmdBuffer)) {
                                    String pwmValue = cmd.substring(strlen(cmdBuffer));
                                    int value = pwmValue.toInt();
                                    if (value >= 0 && value <= 255) {
                                        Mb.MbData[hssPWM1Byte + i] = value;
                                        bitWrite(Mb.MbData[hssLssByte], i, (value > 0) ? 1 : 0);
                                        setHSSwitch(i, value);
                                    }
                                }
                            }
                        } else {
                            strncpy_P(prefix, LSSwitchStr, sizeof(prefix));
                            prefix[sizeof(prefix) - 1] = '\0';
                            if (strncmp(cmd.c_str(), prefix, strlen(prefix)) == 0) {
                                for (int i = 0; i < numOfLSSwitches; i++) {
                                    sprintf(cmdBuffer, "lo%d on", i + 1);
                                    if (cmd == cmdBuffer) {
                                        Mb.MbData[lssPWM1Byte + i] = 255;
                                        bitWrite(Mb.MbData[hssLssByte], i + numOfHSSwitches, 1);
                                        setLSSwitch(i, 255);
                                    }
                                    sprintf(cmdBuffer, "lo%d off", i + 1);
                                    if (cmd == cmdBuffer) {
                                        Mb.MbData[lssPWM1Byte + i] = 0;
                                        bitWrite(Mb.MbData[hssLssByte], i + numOfHSSwitches, 0);
                                        setLSSwitch(i, 0);
                                    }
                                    sprintf(cmdBuffer, "lo%d_pwm ", i + 1);
                                    if (cmd.startsWith(cmdBuffer)) {
                                        String pwmValue = cmd.substring(strlen(cmdBuffer));
                                        int value = pwmValue.toInt();
                                        if (value >= 0 && value <= 255 && i != 3) {
                                            Mb.MbData[lssPWM1Byte + i] = value;
                                            bitWrite(Mb.MbData[hssLssByte], i + numOfHSSwitches, (value > 0) ? 1 : 0);
                                            setLSSwitch(i, value);
                                        }
                                    }
                                }
                            } else {
                                strncpy_P(prefix, anaOutStr, sizeof(prefix));
                                prefix[sizeof(prefix) - 1] = '\0';
                                if (strncmp(cmd.c_str(), prefix, strlen(prefix)) == 0) {
                                    for (int i = 0; i < numOfAnaOuts; i++) {
                                        sprintf(cmdBuffer, "ao%d ", i + 1);
                                        if (cmd.substring(0, strlen(cmdBuffer)) == cmdBuffer) {
                                            String anaOutValue = cmd.substring(strlen(cmdBuffer));
                                            Mb.MbData[anaOut1Byte + i] = anaOutValue.toInt();
                                        }
                                    }
                                } else {
                                    strncpy_P(prefix, rstStr, sizeof(prefix));
                                    prefix[sizeof(prefix) - 1] = '\0';
                                    if (cmd == prefix) {
                                        bitWrite(Mb.MbData[resetByte], 0, 1);
                                    }
                                }
                            }
                        }
                    }
                }

                if (ethernetOK) {
                    udpSend.beginPacket(sendIpAddress, remPort);
                    udpSend.print(rs485Buffer);
                    udpSend.endPacket();
                    dbg("Forwarded to LAN: ");
                    dbgln(rs485Buffer);
                }
                digitalWrite(ledPins[1], LOW);
                rs485Buffer = "";
            }
        }
    }
}

// Process incoming commands
void processCommands() {
    byte byteNo, bitPos;
    for (int i = 0; i < numOfRelays; i++) {
        if (i < 8) {
            setRelay(i, bitRead(Mb.MbData[relOut1Byte], i));
        } else {
            setRelay(i, bitRead(Mb.MbData[relOut2Byte], i-8));
        }
    }
    for (int i = 0; i < numOfHSSwitches; i++) {
        int bitState = bitRead(Mb.MbData[hssLssByte], i);
        if (bitState == 1 && Mb.MbData[hssPWM1Byte + i] == 0) {
            Mb.MbData[hssPWM1Byte + i] = 255;
        } else if (bitState == 0) {
            Mb.MbData[hssPWM1Byte + i] = 0;
        }
        int value = (bitState == 1) ? Mb.MbData[hssPWM1Byte + i] : 0;
        setHSSwitch(i, value);
    }
    for (int i = 0; i < numOfLSSwitches; i++) {
        int bitState = bitRead(Mb.MbData[hssLssByte], i + numOfHSSwitches);
        if (bitState == 1 && Mb.MbData[lssPWM1Byte + i] == 0) {
            Mb.MbData[lssPWM1Byte + i] = 255;
        } else if (bitState == 0) {
            Mb.MbData[lssPWM1Byte + i] = 0;
        }
        int value = (bitState == 1) ? Mb.MbData[lssPWM1Byte + i] : 0;
        setLSSwitch(i, value);
    }
    for (int i = 0; i < numOfAnaOuts; i++) {
        setAnaOut(i, Mb.MbData[anaOut1Byte+i]);
    }
    if (bitRead(Mb.MbData[resetByte], 0)) {
        resetFunc();
    }

    if (useUDPctrl) {
        String cmd, originalCmd; // Dočasně zachováno, upravíme později
        if (receivePacket(&cmd)) {
            originalCmd = cmd;
            dbg("Received packet: ");
            dbgln(cmd);
            digitalWrite(ledPins[1], HIGH);

            char prefix[5];
            strncpy_P(prefix, relayStr, sizeof(prefix));
            prefix[sizeof(prefix) - 1] = '\0';
            if (strncmp(cmd.c_str(), prefix, strlen(prefix)) == 0) {
                for (int i = 0; i < numOfRelays; i++) {
                    byteNo = (i < 8) ? relOut1Byte : relOut2Byte;
                    bitPos = (i < 8) ? i : i - 8;
                    sprintf(cmdBuffer, "ro%d on", i + 1);
                    if (cmd == cmdBuffer) {
                        bitWrite(Mb.MbData[byteNo], bitPos, 1);
                    }
                    sprintf(cmdBuffer, "ro%d off", i + 1);
                    if (cmd == cmdBuffer) {
                        bitWrite(Mb.MbData[byteNo], bitPos, 0);
                    }
                }
            } else {
                strncpy_P(prefix, HSSwitchStr, sizeof(prefix));
                prefix[sizeof(prefix) - 1] = '\0';
                if (strncmp(cmd.c_str(), prefix, strlen(prefix)) == 0) {
                    for (int i = 0; i < numOfHSSwitches; i++) {
                        sprintf(cmdBuffer, "ho%d on", i + 1);
                        if (cmd == cmdBuffer) {
                            Mb.MbData[hssPWM1Byte + i] = 255;
                            bitWrite(Mb.MbData[hssLssByte], i, 1);
                            setHSSwitch(i, 255);
                        }
                        sprintf(cmdBuffer, "ho%d off", i + 1);
                        if (cmd == cmdBuffer) {
                            Mb.MbData[hssPWM1Byte + i] = 0;
                            bitWrite(Mb.MbData[hssLssByte], i, 0);
                            setHSSwitch(i, 0);
                        }
                        sprintf(cmdBuffer, "ho%d_pwm ", i + 1);
                        if (cmd.startsWith(cmdBuffer)) {
                            String pwmValue = cmd.substring(strlen(cmdBuffer));
                            int value = pwmValue.toInt();
                            if (value >= 0 && value <= 255) {
                                Mb.MbData[hssPWM1Byte + i] = value;
                                bitWrite(Mb.MbData[hssLssByte], i, (value > 0) ? 1 : 0);
                                setHSSwitch(i, value);
                            }
                        }
                    }
                } else {
                    strncpy_P(prefix, LSSwitchStr, sizeof(prefix));
                    prefix[sizeof(prefix) - 1] = '\0';
                    if (strncmp(cmd.c_str(), prefix, strlen(prefix)) == 0) {
                        for (int i = 0; i < numOfLSSwitches; i++) {
                            sprintf(cmdBuffer, "lo%d on", i + 1);
                            if (cmd == cmdBuffer) {
                                Mb.MbData[lssPWM1Byte + i] = 255;
                                bitWrite(Mb.MbData[hssLssByte], i + numOfHSSwitches, 1);
                                setLSSwitch(i, 255);
                            }
                            sprintf(cmdBuffer, "lo%d off", i + 1);
                            if (cmd == cmdBuffer) {
                                Mb.MbData[lssPWM1Byte + i] = 0;
                                bitWrite(Mb.MbData[hssLssByte], i + numOfHSSwitches, 0);
                                setLSSwitch(i, 0);
                            }
                            sprintf(cmdBuffer, "lo%d_pwm ", i + 1);
                            if (cmd.startsWith(cmdBuffer)) {
                                String pwmValue = cmd.substring(strlen(cmdBuffer));
                                int value = pwmValue.toInt();
                                if (value >= 0 && value <= 255 && i != 3) {
                                    Mb.MbData[lssPWM1Byte + i] = value;
                                    bitWrite(Mb.MbData[hssLssByte], i + numOfHSSwitches, (value > 0) ? 1 : 0);
                                    setLSSwitch(i, value);
                                }
                            }
                        }
                    } else {
                        strncpy_P(prefix, anaOutStr, sizeof(prefix));
                        prefix[sizeof(prefix) - 1] = '\0';
                        if (strncmp(cmd.c_str(), prefix, strlen(prefix)) == 0) {
                            for (int i = 0; i < numOfAnaOuts; i++) {
                                sprintf(cmdBuffer, "ao%d ", i + 1);
                                if (cmd.substring(0, strlen(cmdBuffer)) == cmdBuffer) {
                                    String anaOutValue = cmd.substring(strlen(cmdBuffer));
                                    Mb.MbData[anaOut1Byte + i] = anaOutValue.toInt();
                                }
                            }
                        } else {
                            strncpy_P(prefix, rstStr, sizeof(prefix));
                            prefix[sizeof(prefix) - 1] = '\0';
                            if (cmd == prefix) {
                                bitWrite(Mb.MbData[resetByte], 0, 1);
                            }
                        }
                    }
                }
            }

            if (gatewayEnabled) {
                digitalWrite(serial3TxControl, HIGH);
                Serial3.print(originalCmd + "\n");
                delay(serial3TxDelay);
                digitalWrite(serial3TxControl, LOW);
                dbg("Forwarded to RS485: ");
                dbgln(originalCmd);
            }

            digitalWrite(ledPins[1], LOW);
        }
    }
}