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

// Documentation content stored in PROGMEM, split into smaller chunks
const char docsContentHeader[] PROGMEM = R"=====(
<!DOCTYPE HTML>
<html>
<head>
    <title>Railduino Documentation</title>
    <style>
        body { font-family: Arial, sans-serif; font-size: 14px; margin: 20px; }
        h2, h3 { font-family: Arial, sans-serif; }
        pre { background: #f4f4f4; padding: 10px; border: 1px solid #ccc; white-space: pre-wrap; }
        a { color: #0066cc; text-decoration: none; }
        a:hover { text-decoration: underline; }
    </style>
</head>
<body>
    <h2>Railduino Firmware Documentation</h2>
    <p><a href='/'>Back to Control Panel</a></p>
)=====";

const char docsContentUDPSyntax1[] PROGMEM = R"=====(
    <h3>UDP Syntax</h3>
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
)=====";

const char docsContentModbus[] PROGMEM = R"=====(
    <h3>MODBUS Commands</h3>
    <pre>
MODBUS TCP commands: FC1 - FC16
MODBUS RTU commands: FC3, FC6, FC16
    </pre>
)=====";

const char docsContentRegisterMap[] PROGMEM = R"=====(
    MODBUS Register Map
register number            description
0                          relay outputs 1-8
1                          relay outputs 9-12
2                          digital outputs HSS 1-4, LSS 1-4
3                          HSS PWM value 1 (0-255)
4                          HSS PWM value 2 (0-255)
5                          HSS PWM value 3 (0-255)
6                          HSS PWM value 4 (0-255)
7                          LSS PWM value 1 (0-255)
8                          LSS PWM value 2 (0-255)
9                          LSS PWM value 3 (0-255)
10                         LSS PWM value 4 (0-255)
11                         analog output 1
12                         analog output 2
13                         digital inputs 1-8
14                         digital inputs 9-16
15                         digital inputs 17-24
16                         analog input 1           0-1023
17                         analog input 2           0-1023
18                         service - reset (bit 0)
19                         1st DS2438 Temp (value multiplied by 100)
20                         1st DS2438 Vad (value multiplied by 100)
21                         1st DS2438 Vsens (value multiplied by 100)
-                         
46                         DS2438 values (up to 10 sensors)
47-57                      DS18B20 Temperature (up to 10 sensors) (value multiplied by 100)
)=====";

const char docsContentFooter[] PROGMEM = R"=====(
    <h3>Additional Notes</h3>
    <pre>
Combination of 1wire sensors must be up to 10pcs maximum (DS18B20 or DS2438)
Using RS485 the UDP syntax must have \n symbol at the end of the command line
    </pre>
    <p><a href='/'>Back to Control Panel</a></p>
</body>
</html>
)=====";

// Hardware version
#define ver 2.1

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
unsigned int httpPort = 2560;      // Port for HTTP webserver
IPAddress dhcpServer; // DHCP server IP address (gateway)
IPAddress sendIpAddress(255, 255, 255, 255); // Broadcast IP address for sending
EthernetClient testClient; // Client pro TCP test připojení

#define EEPROM_ONEWIRE 0
#define EEPROM_ANAINPUT 4
#define EEPROM_PULSEON 8
#define EEPROM_PULSECYCLE 12
#define EEPROM_CHECKINTERVAL 16 
#define EEPROM_DEBUG 20
#define EEPROM_SERIALNUMBER 24 // Posunuto
#define EEPROM_SERIALLOCK 44 // Posunuto
#define EEPROM_DESCRIPTION 45
#define EEPROM_INIT_FLAG 65 // Posunuto
#define EEPROM_ALIAS_START 66 // Posunuto
#define EEPROM_ALIAS_RELAYS EEPROM_ALIAS_START  // 12 × 21 = 252 bajtů
#define EEPROM_ALIAS_HSS (EEPROM_ALIAS_RELAYS + (numOfRelays * 21))  // 4 × 21 = 84
#define EEPROM_ALIAS_LSS (EEPROM_ALIAS_HSS + (numOfHSSwitches * 21))  // 4 × 21 = 84
#define EEPROM_ALIAS_DIGINPUTS (EEPROM_ALIAS_LSS + (numOfLSSwitches * 21))  // 24 × 21 = 504
#define EEPROM_ALIAS_ANA_INPUTS (EEPROM_ALIAS_DIGINPUTS + (numOfDigInputs * 21))  // 2 × 21 = 42
#define EEPROM_ALIAS_ANA_OUTS (EEPROM_ALIAS_ANA_INPUTS + (numOfAnaInputs * 21))  // 2 × 21 = 42
#define EEPROM_ALIAS_DS2438 (EEPROM_ALIAS_ANA_OUTS + (numOfAnaOuts * 21))  // 10 × 21 = 210 bajtů
#define EEPROM_ALIAS_DS18B20 (EEPROM_ALIAS_DS2438 + (maxSensors * 21))     // Dalších 10 × 21 = 210 bajtů

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
#define anaOut1Byte 11  // Analog output 1 (posunuto)
#define anaOut2Byte 12  // Analog output 2 (posunuto)
#define digInp1Byte 13  // Digital inputs 1-8 (posunuto)
#define digInp2Byte 14  // Digital inputs 9-16 (posunuto)
#define digInp3Byte 15  // Digital inputs 17-24 (posunuto)
#define anaInp1Byte 16  // Analog input 1 (posunuto)
#define anaInp2Byte 17  // Analog input 2 (posunuto)
#define serviceByte 18  // Service register (reset, posunuto)
#define oneWireTempByte 19  // DS2438 temperature (posunuto)
#define oneWireVadByte 20   // DS2438 Vad voltage (posunuto)
#define oneWireVsensByte 21 // DS2438 Vsens voltage (posunuto)
#define oneWireDS18B20Byte 47 // DS18B20 temperature (posunuto)

bool debugEnabled = false;
bool serialLocked = false;

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
unsigned long lastCheckTime = 0; // Last connection check time
unsigned long checkInterval = 10000; // Connection check interval (ms)
unsigned long pulseSendCycle = 2000; // Cycle for sending pulse counts (ms)

bool pulseOn = false; // Flag for pulse sensing activation (default off)
int pulse1 = 0, pulse2 = 0, pulse3 = 0; // Pulse counters for pins 21, 20, 19
#define statusLedTimeOn 50   // Status LED on time (ms)
#define statusLedTimeOff 950 // Status LED off time (ms)
#define commLedTimeOn 50     // Communication LED on time (ms)
#define commLedTimeOff 50    // Communication LED off time (ms)
#define debouncingTime 5     // Debouncing time for inputs (ms)
#define serial3TxControl 16  // Pin for RS485 transmission control
#define numOfDipSwitchPins 6
#define numOfLedPins 2
#define numOfDigInputs 24
#define numOfRelays 12
#define numOfHSSwitches 4
#define numOfLSSwitches 4
#define numOfAnaOuts 2
#define numOfAnaInputs 2
#define maxSensors 10 // Maximum number of sensors

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

// Aliasy pro vstupy/výstupy (max 20 znaků + null terminator)
char aliasRelays[numOfRelays][21];
char aliasHSS[numOfHSSwitches][21];
char aliasLSS[numOfLSSwitches][21];
char aliasDigInputs[numOfDigInputs][21];
char aliasAnaInputs[numOfAnaInputs][21];
char aliasAnaOuts[numOfAnaOuts][21];
char aliasDS2438[maxSensors][21];
char aliasDS18B20[maxSensors][21];

int boardAddress = 0; // Board address (from DIP switches)
bool ethernetOK;
long baudRate = 115200; // Serial communication baud rate
int serial3TxDelay = 10; // Delay for RS485 transmission
int serial3TimeOut = 20; // Timeout for serial communication
int resetPin = 4; // Pin for Ethernet module reset
bool dhcpSuccess = false; // Flag for successful DHCP IP acquisition
char serialNumber[21];
char description[21];

// Strings for message formatting
String boardAddressStr; // String representation of board address
String boardAddressRailStr; // Prefix for messages (e.g., "rail1")
String railStr = "rail"; // Base prefix for messages
String digInputStr = "di"; // Prefix for digital inputs
String anaInputStr = "ai"; // Prefix for analog inputs
String relayStr = "ro"; // Prefix for relays
String HSSwitchStr = "ho"; // Prefix for High-Side switches
String LSSwitchStr = "lo"; // Prefix for Low-Side switches
String anaOutStr = "ao"; // Prefix for analog outputs
String rstStr = "rst"; // Prefix for reset command
String relayOnCommands[numOfRelays]; // Commands for turning relays on
String relayOffCommands[numOfRelays]; // Commands for turning relays off
String HSSwitchOnCommands[numOfHSSwitches]; // Commands for turning HSS on
String HSSwitchOffCommands[numOfHSSwitches]; // Commands for turning HSS off
String LSSwitchOnCommands[numOfLSSwitches]; // Commands for turning LSS on
String LSSwitchOffCommands[numOfLSSwitches]; // Commands for turning LSS off
String HSSwitchPWMCommands[numOfHSSwitches]; // Příkazy pro PWM HSS, např. "ho1_pwm"
String LSSwitchPWMCommands[numOfLSSwitches]; // Příkazy pro PWM LSS, např. "lo1_pwm"
String anaOutCommand[numOfAnaOuts]; // Commands for analog outputs


// Helper function to print PROGMEM strings safely
void printProgmemString(EthernetClient& client, const char* progmemStr) {
    while (true) {
        char c = pgm_read_byte(progmemStr++);
        if (c == 0) break;
        client.write(c);
    }
}


void dbg(const String& msg) {
    if (debugEnabled) {
        Serial.print(msg);
    }
}

void dbgln(const String& msg) {
    if (debugEnabled) {
        Serial.println(msg);
    }
}

// Timer class for managing time-based operations
class Timer {
  private:
    unsigned long timestampLastHitMs; // Time of last trigger
    unsigned long sleepTimeMs; // Sleep duration (ms)
  public:
    boolean isOver(); // Checks if the timer has expired
    void sleep(unsigned long sleepTimeMs); // Sets a new sleep duration
};

// Check if the timer has expired
boolean Timer::isOver() {
    if (millis() - timestampLastHitMs < sleepTimeMs) {
        return false;
    }
    timestampLastHitMs = millis();
    return true;
}

// Set a new sleep duration for the timer
void Timer::sleep(unsigned long sleepTimeMs) {
    this->sleepTimeMs = sleepTimeMs;
    timestampLastHitMs = millis();
}

// Timer instances for various tasks
Timer statusLedTimerOn; // Timer for status LED on state
Timer statusLedTimerOff; // Timer for status LED off state
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

// Funkce pro detekci Ethernet shieldu W5100
bool ethShieldDetected() {
    pinMode(53, OUTPUT);  // SPI Master pin pro Mega
    digitalWrite(53, HIGH);
    pinMode(10, OUTPUT);  // SS pro W5200/W5100
    digitalWrite(10, HIGH);
    pinMode(4, OUTPUT);   // SS pro SD kartu (pokud je na shieldu)
    digitalWrite(4, HIGH);  // Deaktivace SD

    // Ruční detekce přes SPI
    SPI.begin();
    digitalWrite(10, LOW);  // Vybrat W5200/W5100
    SPI.transfer(0x00);     // Vysoký bajt adresy VERSIONR (0x001F)
    SPI.transfer(0x1F);     // Nízký bajt adresy VERSIONR
    byte version = SPI.transfer(0x00);  // Číst hodnotu registru
    digitalWrite(10, HIGH);  // Ukončit výběr
    SPI.end();
    return (version == 0x02 || version == 0x03);  // 0x02 pro W5200, 0x03 pro W5100
}

// Reset the Ethernet module
void resetEthernetModule() {
    dbgln("[" + String(millis()) + "] Resetting Ethernet module...");
    pinMode(resetPin, OUTPUT);
    digitalWrite(resetPin, LOW);
    delay(50); // Short reset pulse
    digitalWrite(resetPin, HIGH);
    delay(200); // Recovery time
    
    unsigned long startTime = millis();
    bool dhcpAttempt = false;
    while (millis() - startTime < 100) { // 500ms timeout for DHCP
        if (Ethernet.begin(mac) != 0 && Ethernet.localIP()[0] != 0) {
            dhcpAttempt = true;
            break;
        }
        delay(10); // Short delay to prevent blocking
    }
    
    if (dhcpAttempt) {
        dhcpSuccess = true;
    }

    if (!dhcpAttempt) {
        dbgln("Ethernet reset failed. Triggering watchdog reset...");
        dhcpSuccess = false;
        while (true) {} // Infinite loop to trigger watchdog reset
    } 
}

// Check the Ethernet module
void checkEthernet() {
    if (!dhcpSuccess) return;
    IPAddress currentIP = Ethernet.localIP();
    if (currentIP[0] == 0 && currentIP[1] == 0 && currentIP[2] == 0 && currentIP[3] == 0) {
        if (debugEnabled) dbgln(F("Ethernet connection lost, resetting."));
        dhcpSuccess = false;
        resetEthernetModule();
        return;
    }
    udpRecv.stop();
    udpSend.stop();
    bool connected = testClient.connected() || testClient.connect(dhcpServer, 53);
    if (!connected) {
        if (debugEnabled) dbgln(F("Ethernet connection failed, resetting."));
        dhcpSuccess = false;
        testClient.stop();
        resetEthernetModule();
        return;
    }
    testClient.stop();
    udpRecv.begin(listenPort);
    udpSend.begin(sendPort);
}

// Parse parameter value from HTTP request
String parseParam(const String& request, const String& param, int& startIdx, int& endIdx) {
    startIdx = request.indexOf(param) + param.length();
    endIdx = request.indexOf("&", startIdx);
    if (endIdx == -1) endIdx = request.indexOf(" HTTP");
    String value = request.substring(startIdx, endIdx);
    value.trim();
    return value.length() > 20 ? value.substring(0, 20) : value;
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
                    // Send HTTP headers
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
                    for (int i = 0; i < DS2438count; i++) {
                        client.print(F("{\"sn\":\"")); client.print(oneWireAddressToString(sensors2438[i])); 
                        client.print(F("\",\"temp\":")); client.print(ds2438Temps[i], 2);
                        client.print(F(",\"vad\":")); client.print(ds2438Vads[i], 2);
                        client.print(F(",\"vsens\":")); client.print(ds2438Vsens[i], 2);
                        client.print(F("}")); client.print(i < DS2438count - 1 ? F(",") : F("],"));
                    }

                    // Print DS18B20 sensor data
                    client.print(F("\"ds18b20\":[")); 
                    for (int i = 0; i < DS18B20count; i++) {
                        client.print(F("{\"sn\":\"")); client.print(oneWireAddressToString(sensors18B20[i])); 
                        client.print(F("\",\"temp\":")); client.print(ds18b20Temps[i], 2);
                        client.print(F("}")); client.print(i < DS18B20count - 1 ? F(",") : F("],"));
                    }

                    // Print pulse counts and configuration values
                    client.print(F("\"pulseCounts\":[")); client.print(pulse1); client.print(F(",")); client.print(pulse2); client.print(F(",")); client.print(pulse3); client.print(F("],"));
                    client.print(F("\"oneWireCycle\":")); client.print(oneWireCycle); client.print(F(","));
                    client.print(F("\"anaInputCycle\":")); client.print(anaInputCycle); client.print(F(","));
                    client.print(F("\"pulseOn\":")); client.print(pulseOn ? 1 : 0); client.print(F(","));
                    client.print(F("\"pulseSendCycle\":")); client.print(pulseSendCycle); client.print(F(","));
                    client.print(F("\"checkInterval\":")); client.print(checkInterval); client.print(F(","));
                    client.print(F("\"debugEnabled\":")); client.print(debugEnabled ? 1 : 0); client.print(F(","));
                    client.print(F("\"serialNumber\":\"")); client.print(serialNumber); client.print(F("\","));
                    client.print(F("\"serialLocked\":")); client.print(serialLocked ? 1 : 0); client.print(F(","));
                    client.print(F("\"description\":\"")); client.print(urlDecode(String(description))); client.print(F("\","));

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
                        client.print(F("\"")); client.print(urlDecode(String(aliasRelays[i]))); client.print(F("\""));
                        client.print(i < numOfRelays - 1 ? F(",") : F("],"));
                    }

                    // Print high-side switch aliases
                    client.print(F("\"alias_hss\":[")); 
                    for (int i = 0; i < numOfHSSwitches; i++) {
                        client.print(F("\"")); client.print(urlDecode(String(aliasHSS[i]))); client.print(F("\""));
                        client.print(i < numOfHSSwitches - 1 ? F(",") : F("],"));
                    }

                    // Print low-side switch aliases
                    client.print(F("\"alias_lss\":[")); 
                    for (int i = 0; i < numOfLSSwitches; i++) {
                        client.print(F("\"")); client.print(urlDecode(String(aliasLSS[i]))); client.print(F("\""));
                        client.print(i < numOfLSSwitches - 1 ? F(",") : F("],"));
                    }

                    // Print digital input aliases
                    client.print(F("\"alias_digInputs\":[")); 
                    for (int i = 0; i < numOfDigInputs; i++) {
                        client.print(F("\"")); client.print(urlDecode(String(aliasDigInputs[i]))); client.print(F("\""));
                        client.print(i < numOfDigInputs - 1 ? F(",") : F("],"));
                    }

                    // Print analog input aliases
                    client.print(F("\"alias_anaInputs\":[")); 
                    for (int i = 0; i < numOfAnaInputs; i++) {
                        client.print(F("\"")); client.print(urlDecode(String(aliasAnaInputs[i]))); client.print(F("\""));
                        client.print(i < numOfAnaInputs - 1 ? F(",") : F("],"));
                    }

                    // Print analog output aliases
                    client.print(F("\"alias_anaOuts\":[")); 
                    for (int i = 0; i < numOfAnaOuts; i++) {
                        client.print(F("\"")); client.print(urlDecode(String(aliasAnaOuts[i]))); client.print(F("\""));
                        client.print(i < numOfAnaOuts - 1 ? F(",") : F("],"));
                    }

                    // Print DS2438 aliases
                    client.print(F("\"alias_ds2438\":[")); 
                    for (int i = 0; i < DS2438count; i++) {
                        client.print(F("\"")); client.print(urlDecode(String(aliasDS2438[i]))); client.print(F("\""));
                        client.print(i < DS2438count - 1 ? F(",") : F("],"));
                    }

                    // Print DS18B20 aliases
                    client.print(F("\"alias_ds18b20\":[")); 
                    for (int i = 0; i < DS18B20count; i++) {
                        client.print(F("\"")); client.print(urlDecode(String(aliasDS18B20[i]))); client.print(F("\""));
                        client.print(i < DS18B20count - 1 ? F(",") : F("]"));
                    }

                    // Close JSON object
                    client.println(F("}"));
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
                } else if (request.indexOf("GET /command?") != -1) {
                    // Send HTTP response
                    client.println(F("HTTP/1.1 200 OK\nContent-Type: text/plain\nConnection: close\n\nOK"));

                    int startIdx, endIdx;
                    String value;

                    // Update serial number
                    if (request.indexOf(F("serialNumber=")) != -1) {
                        value = parseParam(request, F("serialNumber="), startIdx, endIdx);
                        value.toCharArray(serialNumber, 21);
                        EEPROM.put(EEPROM_SERIALNUMBER, serialNumber);
                    }

                    // Update description
                    if (request.indexOf(F("description=")) != -1) {
                        value = parseParam(request, F("description="), startIdx, endIdx);
                        strcpy(description, value.length() > 0 && isValidAlias(value.c_str()) ? value.c_str() : "");
                        EEPROM.put(EEPROM_DESCRIPTION, description);
                    }

                    // Update relay states
                    if (request.indexOf("relay") != -1) {
                        for (int i = 0; i < numOfRelays; i++) {
                            String relayParam = "relay" + String(i + 1) + "=";
                            if (request.indexOf(relayParam) != -1) {
                                value = parseParam(request, relayParam, startIdx, endIdx);
                                int val = value.toInt();
                                bitWrite(Mb.MbData[i < 8 ? relOut1Byte : relOut2Byte], i % 8, val);
                                setRelay(i, val);
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
                                    bitWrite(Mb.MbData[hssLssByte], i, val > 0 ? 1 : 0);
                                    Mb.MbData[hssPWM1Byte + i] = (val == 1 || val == 0) ? (val ? 255 : 0) : val;
                                    setHSSwitch(i, Mb.MbData[hssPWM1Byte + i]);
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
                                    bitWrite(Mb.MbData[hssLssByte], i + numOfHSSwitches, val > 0 ? 1 : 0);
                                    if (i != 3) Mb.MbData[lssPWM1Byte + i] = (val == 1 || val == 0) ? (val ? 255 : 0) : val;
                                    setLSSwitch(i, i != 3 ? Mb.MbData[lssPWM1Byte + i] : val);
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
                                int val = constrain(value.toInt(), 0, 255);
                                Mb.MbData[anaOut1Byte + i] = val;
                                setAnaOut(i, val);
                            }
                        }
                    }

                    // Update 1-Wire cycle
                    if (request.indexOf(F("oneWireCycle=")) != -1) {
                        value = parseParam(request, F("oneWireCycle="), startIdx, endIdx);
                        oneWireCycle = max(value.toInt(), 5000);
                        oneWireTimer.sleep(oneWireCycle);
                        EEPROM.put(EEPROM_ONEWIRE, oneWireCycle);
                    }

                    // Update analog input cycle
                    if (request.indexOf(F("anaInputCycle=")) != -1) {
                        value = parseParam(request, F("anaInputCycle="), startIdx, endIdx);
                        anaInputCycle = max(value.toInt(), 1000);
                        analogTimer.sleep(anaInputCycle);
                        EEPROM.put(EEPROM_ANAINPUT, anaInputCycle);
                    }

                    // Update pulse sensing state
                    if (request.indexOf(F("pulseOn=")) != -1) {
                        value = parseParam(request, F("pulseOn="), startIdx, endIdx);
                        pulseOn = (value.toInt() == 1);
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
                    }

                    // Update pulse send cycle
                    if (request.indexOf(F("pulseSendCycle=")) != -1) {
                        value = parseParam(request, F("pulseSendCycle="), startIdx, endIdx);
                        pulseSendCycle = max(value.toInt(), 1000);
                        EEPROM.put(EEPROM_PULSECYCLE, pulseSendCycle);
                        if (pulseOn) pulseTimer.sleep(pulseSendCycle);
                    }

                    // Update check interval
                    if (request.indexOf(F("checkInterval=")) != -1) {
                        value = parseParam(request, F("checkInterval="), startIdx, endIdx);
                        long val = value.toInt();
                        checkInterval = (val >= 1000 && val <= 60000) ? val : 10000;
                        EEPROM.put(EEPROM_CHECKINTERVAL, checkInterval);
                    }

                    // Update debug enable state
                    if (request.indexOf(F("debugEnabled=")) != -1) {
                        value = parseParam(request, F("debugEnabled="), startIdx, endIdx);
                        debugEnabled = (value.toInt() == 1);
                        EEPROM.put(EEPROM_DEBUG, debugEnabled);
                        if (debugEnabled) dbgln("Debug enabled at baud rate: " + String(baudRate));
                        else dbgln(F("Debug disabled."));
                    }

                    // Trigger reset
                    if (request.indexOf(F("reset=1")) != -1) resetFunc();

                    // Update relay aliases
                    if (request.indexOf(F("alias_relay")) != -1) {
                        for (int i = 0; i < numOfRelays; i++) {
                            String aliasParam = "alias_relay" + String(i + 1) + "=";
                            if (request.indexOf(aliasParam) != -1) {
                                value = parseParam(request, aliasParam, startIdx, endIdx);
                                strcpy(aliasRelays[i], value.length() > 0 && isValidAlias(value.c_str()) ? value.c_str() : "");
                                EEPROM.put(EEPROM_ALIAS_RELAYS + (i * 21), aliasRelays[i]);
                            }
                        }
                    }

                    // Update high-side switch aliases
                    if (request.indexOf(F("alias_hss")) != -1) {
                        for (int i = 0; i < numOfHSSwitches; i++) {
                            String aliasParam = "alias_hss" + String(i + 1) + "=";
                            if (request.indexOf(aliasParam) != -1) {
                                value = parseParam(request, aliasParam, startIdx, endIdx);
                                value.toCharArray(aliasHSS[i], 21);
                                EEPROM.put(EEPROM_ALIAS_HSS + (i * 21), aliasHSS[i]);
                            }
                        }
                    }

                    // Update low-side switch aliases
                    if (request.indexOf(F("alias_lss")) != -1) {
                        for (int i = 0; i < numOfLSSwitches; i++) {
                            String aliasParam = "alias_lss" + String(i + 1) + "=";
                            if (request.indexOf(aliasParam) != -1) {
                                value = parseParam(request, aliasParam, startIdx, endIdx);
                                value.toCharArray(aliasLSS[i], 21);
                                EEPROM.put(EEPROM_ALIAS_LSS + (i * 21), aliasLSS[i]);
                            }
                        }
                    }

                    // Update digital input aliases
                    if (request.indexOf(F("alias_digInputs")) != -1) {
                        for (int i = 0; i < numOfDigInputs; i++) {
                            String aliasParam = "alias_digInputs" + String(i + 1) + "=";
                            if (request.indexOf(aliasParam) != -1) {
                                value = parseParam(request, aliasParam, startIdx, endIdx);
                                memset(aliasDigInputs[i], 0, 21);
                                strcpy(aliasDigInputs[i], value.length() > 0 && isValidAlias(value.c_str()) ? value.c_str() : "");
                                EEPROM.put(EEPROM_ALIAS_DIGINPUTS + (i * 21), aliasDigInputs[i]);
                            }
                        }
                    }

                    // Update analog input aliases
                    if (request.indexOf(F("alias_anaInputs")) != -1) {
                        for (int i = 0; i < numOfAnaInputs; i++) {
                            String aliasParam = "alias_anaInputs" + String(i + 1) + "=";
                            if (request.indexOf(aliasParam) != -1) {
                                value = parseParam(request, aliasParam, startIdx, endIdx);
                                memset(aliasAnaInputs[i], 0, 21);
                                strcpy(aliasAnaInputs[i], value.length() > 0 && isValidAlias(value.c_str()) ? value.c_str() : "");
                                EEPROM.put(EEPROM_ALIAS_ANA_INPUTS + (i * 21), aliasAnaInputs[i]);
                            }
                        }
                    }

                    // Update analog output aliases
                    if (request.indexOf(F("alias_anaOuts")) != -1) {
                        for (int i = 0; i < numOfAnaOuts; i++) {
                            String aliasParam = "alias_anaOuts" + String(i + 1) + "=";
                            if (request.indexOf(aliasParam) != -1) {
                                value = parseParam(request, aliasParam, startIdx, endIdx);
                                memset(aliasAnaOuts[i], 0, 21);
                                strcpy(aliasAnaOuts[i], value.length() > 0 && isValidAlias(value.c_str()) ? value.c_str() : "");
                                EEPROM.put(EEPROM_ALIAS_ANA_OUTS + (i * 21), aliasAnaOuts[i]);
                            }
                        }
                    }

                    // Update DS2438 aliases
                    if (request.indexOf(F("alias_ds2438")) != -1) {
                        for (int i = 0; i < DS2438count; i++) {
                            String aliasParam = "alias_ds2438" + String(i + 1) + "=";
                            if (request.indexOf(aliasParam) != -1) {
                                value = parseParam(request, aliasParam, startIdx, endIdx);
                                memset(aliasDS2438[i], 0, 21);
                                strcpy(aliasDS2438[i], value.length() > 0 && isValidAlias(value.c_str()) ? value.c_str() : "");
                                EEPROM.put(EEPROM_ALIAS_DS2438 + (i * 21), aliasDS2438[i]);
                            }
                        }
                    }

                    // Update DS18B20 aliases
                    if (request.indexOf(F("alias_ds18b20")) != -1) {
                        for (int i = 0; i < DS18B20count; i++) {
                            String aliasParam = "alias_ds18b20" + String(i + 1) + "=";
                            if (request.indexOf(aliasParam) != -1) {
                                value = parseParam(request, aliasParam, startIdx, endIdx);
                                memset(aliasDS18B20[i], 0, 21);
                                strcpy(aliasDS18B20[i], value.length() > 0 && isValidAlias(value.c_str()) ? value.c_str() : "");
                                EEPROM.put(EEPROM_ALIAS_DS18B20 + (i * 21), aliasDS18B20[i]);
                            }
                        }
                    }
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

                        // Send HTTP headers
                        client.println(F("HTTP/1.1 200 OK\nContent-Type: text/html\nConnection: close\n"));

                        // HTML head and styles
                        client.println(F("<!DOCTYPE HTML><html><head><title>Railduino Control</title><style>"
                                        "body{font-family:Arial,sans-serif;font-size:14px;position:relative;}"
                                        ".container{width:1100px;margin:0 auto;position:relative;}"
                                        "h1,h2,h3{font-family:Arial,sans-serif;}"
                                        "table.inner{border-collapse:collapse;width:100%;}"
                                        "table.outer{border:1px solid #ccc;width:100%;}"
                                        "td{vertical-align:middle;font-size:14px;padding:2px;}"
                                        "input,select{font-size:14px;padding:2px;}"
                                        ".reset-button{position:absolute;top:0px;right:0px;}"
                                        ".reset-button input{padding:5px;}"
                                        "tr{height:26px;}"
                                        "table.inner td:first-child{width:100px;}"
                                        "table.inner td:nth-child(2){width:80px;}"
                                        "table.basic-info-table td:first-child{width:180px;}"
                                        "table.other-settings-table td:first-child{width:200px;}"
                                        "table.onewire-table td:first-child{width:80px;}"
                                        "table.onewire-table td:nth-child(2){width:130px;}"
                                        "table.onewire-table td:nth-child(3),table.onewire-table td:nth-child(4),table.onewire-table td:nth-child(5){width:40px;}"
                                        ".no-border{border:none;}</style>"));

                        // JavaScript for updating UI
                        client.println(F("<script>"));
                        client.println(F("let firstUpdate=true;let activePWMField=null;let pendingCommands={};let updateIntervalId=null;"));
                        client.println(F("function updateStatus(){"));
                        client.println(F("fetch('/status?ts='+new Date().getTime()).then(response=>response.json()).then(data=>{"));
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
                                        "let oneWireCycle=document.getElementById('oneWireCycle');if(oneWireCycle)oneWireCycle.value=data.oneWireCycle||5000;"
                                        "let anaInputCycle=document.getElementById('anaInputCycle');if(anaInputCycle)anaInputCycle.value=data.anaInputCycle||1000;"
                                        "let pulseOn=document.getElementById('pulseOn');if(pulseOn)pulseOn.value=data.pulseOn.toString();"
                                        "let pulseSendCycle=document.getElementById('pulseSendCycle');if(pulseSendCycle)pulseSendCycle.value=data.pulseSendCycle||2000;"
                                        "let checkInterval=document.getElementById('checkInterval');if(checkInterval){"
                                        "checkInterval.value=data.checkInterval||1000;"
                                        "if(updateIntervalId)clearInterval(updateIntervalId);"
                                        "updateIntervalId=setInterval(updateStatus,data.checkInterval||1000);}"));
                        client.print(F("for(let i=0;i<")); client.print(numOfAnaOuts); client.println(F(";i++){"
                                        "let anaOut=document.getElementById('anaOut'+(i+1));if(anaOut)anaOut.value=data.anaOuts[i]||0;"
                                        "let anaOutVoltage=document.getElementById('anaOutVoltage'+(i+1));"
                                        "if(anaOutVoltage)anaOutVoltage.textContent='('+data.anaOutsVoltage[i].toFixed(1)+'V)';}"));
                        client.print(F("for(let i=0;i<")); client.print(numOfRelays); client.println(F(";i++){"
                                        "let aliasInput=document.getElementById('alias_relay'+(i+1));if(aliasInput)aliasInput.value=data.alias_relays[i]||'';}"));
                        client.print(F("for(let i=0;i<")); client.print(numOfHSSwitches); client.println(F(";i++){"
                                        "let aliasInput=document.getElementById('alias_hss'+(i+1));if(aliasInput)aliasInput.value=data.alias_hss[i]||'';}"));
                        client.print(F("for(let i=0;i<")); client.print(numOfLSSwitches); client.println(F(";i++){"
                                        "let aliasInput=document.getElementById('alias_lss'+(i+1));if(aliasInput)aliasInput.value=data.alias_lss[i]||'';}"));
                        client.print(F("for(let i=0;i<")); client.print(numOfAnaOuts); client.println(F(";i++){"
                                        "let aliasInput=document.getElementById('alias_ao'+(i+1));if(aliasInput)aliasInput.value=data.alias_anaOuts[i]||'';}"));
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
                                        "<input type='button' onclick=\"window.location.href='/info'\" value='View Docs'>"
                                        "<input type='button' value='Reboot' onclick='if(confirm(\"Po stisku tlačítka reset dojde k resetu modulu. Pokračovat?\")) sendCommand(\"reset\",1)'>"
                                        "</div><h2>Railduino ")); client.print(ver, 1); client.println(F(" Control Panel</h2>"));

                        // First row: Basic Info and Settings
                        client.println(F("<div style='border:1px solid #ccc;margin-bottom:10px;'><table class='outer'><tr><td width='50%'>"
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
                        client.println(F("</td></tr>"
                                        "<tr><td>Description</td><td><input type='text' maxlength='20' value='")); 
                        client.print(urlDecode(String(description))); 
                        client.println(F("' onchange='fetch(\"/command?description=\"+encodeURIComponent(this.value))'></td></tr>"
                                        "<tr><td>Serial Number</td><td><input type='text' id='serialNumber' maxlength='20' onchange='sendCommand(\"serialNumber\",this.value)'></td></tr>"
                                        "</table></td><td width='50%'>"
                                        "<h3>Other settings</h3><table class='other-settings-table'>"
                                        "<tr><td>1-Wire Cycle</td><td><input type='number' id='oneWireCycle' min='5000' style='width:80px;' onchange='sendCommand(\"oneWireCycle\",this.value)'> ms</td></tr>"
                                        "<tr><td>Analog Input Cycle</td><td><input type='number' id='anaInputCycle' min='2000' style='width:80px;' onchange='sendCommand(\"anaInputCycle\",this.value)'> ms</td></tr>"
                                        "<tr><td>Pulses Send Cycle</td><td><input type='number' id='pulseSendCycle' min='2000' style='width:80px;' onchange='sendCommand(\"pulseSendCycle\",this.value)'> ms</td></tr>"
                                        "<tr><td>Ping DHCP Cycle</td><td><input type='number' id='checkInterval' min='1000' max='60000' style='width:80px;' value=")); 
                        client.print(checkInterval); 
                        client.println(F(" onchange='sendCommand(\"checkInterval\",this.value)'> ms</td></tr>"
                                        "<tr><td>Pulses Sensing (DI 10,11,12)</td><td><select id='pulseOn' onchange='sendCommand(\"pulseOn\",this.value)'>"
                                        "<option value='0' selected>Off</option><option value='1'>On</option></select></td></tr>"
                                        "<tr><td>Serial Debug (115200 Bd)</td><td><select id='debugEnabled' onchange='sendCommand(\"debugEnabled\",this.value)'>"
                                        "<option value='0' selected>Off</option><option value='1'>On</option></select></td></tr>"
                                        "</table></td></tr></table></div>"));

                        // Second row: Relay Status
                        client.println(F("<div style='border:1px solid #ccc;margin-bottom:10px;'><table class='outer'><tr><td width='50%'>"
                                        "<h3>Relay Status</h3><table class='inner'>"));
                        for (int i = 0; i < numOfRelays; i++) {
                            client.print(F("<tr><td>Relay ")); client.print(i + 1); client.print(F("</td><td>"
                                          "<input type='checkbox' id='relay")); client.print(i + 1); 
                            client.print(F("' name='relay")); client.print(i + 1); client.print(F("' onchange='sendCommand(\"relay")); 
                            client.print(i + 1); client.print(F("\",this.checked?1:0)'")); 
                            if (bitRead(Mb.MbData[i < 8 ? relOut1Byte : relOut2Byte], i % 8)) client.print(F(" checked"));
                            client.print(F("></td><td><input type='text' id='alias_relay")); client.print(i + 1); 
                            client.print(F("' name='alias_relay")); client.print(i + 1); client.print(F("' value='")); 
                            client.print(urlDecode(String(aliasRelays[i]))); 
                            client.print(F("' onchange='sendCommand(\"alias_relay")); client.print(i + 1); 
                            client.print(F("\",encodeURIComponent(this.value))'>"));
                            client.println(F("</td></tr>"));
                        }
                        client.println(F("</table></td><td width='50%'>"));

                        // Second row: HSS and LSS Status
                        client.println(F("<table class='inner'><tr><td><h3>High-Side Switches Status (0-255 = 0-V+)</h3><table class='inner'>"));
                        for (int i = 0; i < numOfHSSwitches; i++) {
                            client.print(F("<tr><td>HSS ")); client.print(i + 1); client.print(F("</td><td>"
                                          "<input type='checkbox' id='hss")); client.print(i + 1); 
                            client.print(F("' name='hss")); client.print(i + 1); client.print(F("' onchange='sendCommand(\"hss")); 
                            client.print(i + 1); client.print(F("\",this.checked?1:0)'")); 
                            if (bitRead(Mb.MbData[hssLssByte], i)) client.print(F(" checked"));
                            client.print(F("></td><td><input type='number' id='hssPWM")); client.print(i + 1); 
                            client.print(F("' name='hssPWM")); client.print(i + 1); client.print(F("' min='0' max='255' value='")); 
                            client.print(Mb.MbData[hssPWM1Byte + i]); client.print(F("' onchange='sendCommand(\"hss")); 
                            client.print(i + 1); client.print(F("\",this.value)'>"));
                            client.print(F("</td><td><input type='text' id='alias_hss")); client.print(i + 1); 
                            client.print(F("' name='alias_hss")); client.print(i + 1); client.print(F("' value='")); 
                            client.print(urlDecode(String(aliasHSS[i]))); client.print(F("' onchange='sendCommand(\"alias_hss")); 
                            client.print(i + 1); client.print(F("\",encodeURIComponent(this.value))'>"));
                            client.println(F("</td></tr>"));
                        }
                        client.println(F("</table></td></tr><tr><td><h3>Low-Side Switches Status (0-255 = 0-V+)</h3><table class='inner'>"));
                        for (int i = 0; i < numOfLSSwitches; i++) {
                            client.print(F("<tr><td>LSS ")); client.print(i + 1); client.print(F("</td><td>"
                                          "<input type='checkbox' id='lss")); client.print(i + 1); 
                            client.print(F("' name='lss")); client.print(i + 1); client.print(F("' onchange='sendCommand(\"lss")); 
                            client.print(i + 1); client.print(F("\",this.checked?1:0)'")); 
                            if (bitRead(Mb.MbData[hssLssByte], i + numOfHSSwitches)) client.print(F(" checked"));
                            client.print(F("></td><td>"));
                            if (i != 3) {
                                client.print(F("<input type='number' id='lssPWM")); client.print(i + 1); 
                                client.print(F("' name='lssPWM")); client.print(i + 1); client.print(F("' min='0' max='255' value='")); 
                                client.print(Mb.MbData[lssPWM1Byte + i]); client.print(F("' onchange='sendCommand(\"lss")); 
                                client.print(i + 1); client.print(F("\",this.value)'>"));
                            } else {
                                client.print(F("-"));
                            }
                            client.print(F("</td><td><input type='text' id='alias_lss")); client.print(i + 1); 
                            client.print(F("' name='alias_lss")); client.print(i + 1); client.print(F("' value='")); 
                            client.print(urlDecode(String(aliasLSS[i]))); client.print(F("' onchange='sendCommand(\"alias_lss")); 
                            client.print(i + 1); client.print(F("\",encodeURIComponent(this.value))'>"));
                            client.println(F("</td></tr>"));
                        }
                        client.println(F("</table></td></tr></table></td></tr></table></div>"));

                        // Third row: Digital Inputs
                        client.println(F("<div style='border:1px solid #ccc;margin-bottom:10px;'><table class='outer'><tr>"
                                        "<td width='50%'><h3 class='dig-inputs-title'>Digital Inputs 1-12</h3><table class='inner' id='digInputsTable1'><tbody>"));
                        for (int i = 0; i < 12; i++) {
                            client.print(F("<tr><td>DI ")); client.print(i + 1); client.print(F("</td><td id='di_status")); 
                            client.print(i + 1); client.print(F("'>")); client.print(1 - inputStatus[i]); client.print(F("</td><td>"
                                          "<input type='text' id='alias_digInputs")); client.print(i + 1); 
                            client.print(F("' name='alias_digInputs")); client.print(i + 1); client.print(F("' value='")); 
                            client.print(urlDecode(String(aliasDigInputs[i]))); client.print(F("' onchange='sendCommand(\"alias_digInputs")); 
                            client.print(i + 1); client.print(F("\",encodeURIComponent(this.value))'>"));
                            client.println(F("</td></tr>"));
                        }
                        client.println(F("</tbody></table></td><td width='50%'><h3 class='dig-inputs-title'>Digital Inputs 13-24</h3><table class='inner' id='digInputsTable2'><tbody>"));
                        for (int i = 12; i < 24; i++) {
                            client.print(F("<tr><td>DI ")); client.print(i + 1); client.print(F("</td><td id='di_status")); 
                            client.print(i + 1); client.print(F("'>")); client.print(1 - inputStatus[i]); client.print(F("</td><td>"
                                          "<input type='text' id='alias_digInputs")); client.print(i + 1); 
                            client.print(F("' name='alias_digInputs")); client.print(i + 1); client.print(F("' value='")); 
                            client.print(urlDecode(String(aliasDigInputs[i]))); client.print(F("' onchange='sendCommand(\"alias_digInputs")); 
                            client.print(i + 1); client.print(F("\",encodeURIComponent(this.value))'>"));
                            client.println(F("</td></tr>"));
                        }
                        client.println(F("</tbody></table></td></tr></table></div>"));

                        // Fourth row: Analog Inputs and Outputs
                        client.println(F("<div style='border:1px solid #ccc;margin-bottom:10px;'><table class='outer'><tr><td width='50%'>"
                                        "<h3>Analog Inputs (0-1024 = 0-10V)</h3><table class='inner' id='anaInputsTable'><tbody>"));
                        for (int i = 0; i < numOfAnaInputs; i++) {
                            client.print(F("<tr><td>AI ")); client.print(i + 1); client.print(F("</td><td id='ai_status")); 
                            client.print(i + 1); client.print(F("'>")); client.print(analogStatus[i], 2); client.print(F("</td><td>"
                                          "<input type='text' id='alias_anaInputs")); client.print(i + 1); 
                            client.print(F("' name='alias_anaInputs")); client.print(i + 1); client.print(F("' value='")); 
                            client.print(urlDecode(String(aliasAnaInputs[i]))); client.print(F("' onchange='sendCommand(\"alias_anaInputs")); 
                            client.print(i + 1); client.print(F("\",encodeURIComponent(this.value))'>"));
                            client.println(F("</td></tr>"));
                        }
                        client.println(F("</tbody></table></td><td width='50%'><h3>Analog Outputs (0-255 = 0-10V)</h3><table class='inner'>"));
                        for (int i = 0; i < numOfAnaOuts; i++) {
                            client.print(F("<tr><td>AO ")); client.print(i + 1); client.print(F("</td><td>"
                                          "<input type='number' id='anaOut")); client.print(i + 1); 
                            client.print(F("' name='anaOut")); client.print(i + 1); client.print(F("' min='0' max='255' value='")); 
                            client.print(Mb.MbData[anaOut1Byte + i]); client.print(F("' onchange='sendCommand(\"anaOut")); 
                            client.print(i + 1); client.print(F("\",this.value)'>"));
                            client.print(F("</td><td id='anaOutVoltage")); client.print(i + 1); client.print(F("'>(")); 
                            client.print(anaOutsVoltage[i], 1); client.print(F("V)</td><td>"
                                          "<input type='text' id='alias_anaOuts")); client.print(i + 1); 
                            client.print(F("' name='alias_anaOuts")); client.print(i + 1); client.print(F("' value='")); 
                            client.print(urlDecode(String(aliasAnaOuts[i]))); client.print(F("' onchange='sendCommand(\"alias_anaOuts")); 
                            client.print(i + 1); client.print(F("\",encodeURIComponent(this.value))'>"));
                            client.println(F("</td></tr>"));
                        }
                        client.println(F("</table></td></tr></table></div>"));

                        // Fifth row: 1-Wire Sensors
                        client.println(F("<div style='border:1px solid #ccc;margin-bottom:10px;'><table class='outer'><tr><td width='50%'>"
                                        "<h3>1-Wire Sensors DS2438</h3><table class='onewire-table' id='ds2438Table'><tbody>"));
                        for (int i = 0; i < DS2438count; i++) {
                            client.print(F("<tr><td>DS2438-")); client.print(i + 1); client.print(F("</td><td id='ds2438_sn")); 
                            client.print(i + 1); client.print(F("'>")); client.print(oneWireAddressToString(sensors2438[i])); 
                            client.print(F("</td><td id='ds2438_temp")); client.print(i + 1); client.print(F("'></td><td id='ds2438_vad")); 
                            client.print(i + 1); client.print(F("'></td><td id='ds2438_vsens")); client.print(i + 1); 
                            client.print(F("'></td><td><input type='text' id='alias_ds2438_")); client.print(i + 1); 
                            client.print(F("' name='alias_ds2438")); client.print(i + 1); client.print(F("' value='")); 
                            client.print(urlDecode(String(aliasDS2438[i]))); client.print(F("' onchange='sendCommand(\"alias_ds2438")); 
                            client.print(i + 1); client.print(F("\",encodeURIComponent(this.value))'>"));
                            client.println(F("</td></tr>"));
                        }
                        client.println(F("</tbody></table></td><td width='50%'><h3>1-Wire Sensors DS18B20</h3><table class='onewire-table' id='ds18b20Table'><tbody>"));
                        for (int i = 0; i < DS18B20count; i++) {
                            client.print(F("<tr><td>DS18B20-")); client.print(i + 1); client.print(F("</td><td id='ds18b20_sn")); 
                            client.print(i + 1); client.print(F("'>")); client.print(oneWireAddressToString(sensors18B20[i])); 
                            client.print(F("</td><td id='ds18b20_temp")); client.print(i + 1); 
                            client.print(F("'></td><td><input type='text' id='alias_ds18b20_")); client.print(i + 1); 
                            client.print(F("' name='alias_ds18b20")); client.print(i + 1); client.print(F("' value='")); 
                            client.print(urlDecode(String(aliasDS18B20[i]))); client.print(F("' onchange='sendCommand(\"alias_ds18b20")); 
                            client.print(i + 1); client.print(F("\",encodeURIComponent(this.value))'>"));
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
    Serial.begin(115200); // Initialize serial communication
    dbg("Railduino firmware version: ");
    dbgln(String(ver));

    // Initialize digital inputs
    for (int i = 0; i < numOfDigInputs; i++) {
        pinMode(inputPins[i], INPUT);
        inputStatus[i] = 1; // Default state
        inputStatusNew[i] = 0; // New state for debouncing
    }

    // Initialize relays
    for (int i = 0; i < numOfRelays; i++) {
        pinMode(relayPins[i], OUTPUT);
        relayOnCommands[i] = relayStr + String(i + 1, DEC) + " on"; // Command for turning on
        relayOffCommands[i] = relayStr + String(i + 1, DEC) + " off"; // Command for turning off
        setRelay(i, 0); // Turn off relay
    }

    // Initialize High-Side switches
    for (int i = 0; i < numOfHSSwitches; i++) {
        pinMode(HSSwitchPins[i], OUTPUT);
        HSSwitchOnCommands[i] = HSSwitchStr + String(i + 1, DEC) + " on"; // Command for turning on
        HSSwitchOffCommands[i] = HSSwitchStr + String(i + 1, DEC) + " off"; // Command for turning off
        HSSwitchPWMCommands[i] = HSSwitchStr + String(i + 1, DEC) + "_pwm"; // Např. "ho1_pwm"
        setHSSwitch(i, 0); // Turn off HSS
    }

    // Initialize Low-Side switches
    for (int i = 0; i < numOfLSSwitches; i++) {
        pinMode(LSSwitchPins[i], OUTPUT);
        LSSwitchOnCommands[i] = LSSwitchStr + String(i + 1, DEC) + " on"; // Command for turning on
        LSSwitchOffCommands[i] = LSSwitchStr + String(i + 1, DEC) + " off"; // Command for turning off
        LSSwitchPWMCommands[i] = LSSwitchStr + String(i + 1, DEC) + "_pwm"; // Např. "lo1_pwm"
        setLSSwitch(i, 0); // Turn off LSS
    }

    // Initialize analog outputs
    for (int i = 0; i < numOfAnaOuts; i++) {
        pinMode(anaOutPins[i], OUTPUT);
        anaOutCommand[i] = anaOutStr + String(i + 1, DEC); // Command for analog output
        setAnaOut(i, 0); // Set to zero
    }

    // Initialize analog inputs
    for (int i = 0; i < numOfAnaInputs; i++) {
        pinMode(analogPins[i], INPUT);
    }

    // Initialize LEDs
    for (int i = 0; i < numOfLedPins; i++) {
        pinMode(ledPins[i], OUTPUT);
    }

    // Set up timers
    statusLedTimerOn.sleep(statusLedTimeOn);
    statusLedTimerOff.sleep(statusLedTimeOff);

    EEPROM.get(EEPROM_SERIALLOCK, serialLocked);
    if (serialLocked != 0 && serialLocked != 1) {
        serialLocked = false;
        EEPROM.put(EEPROM_SERIALLOCK, serialLocked);
    }

    EEPROM.get(EEPROM_SERIALNUMBER, serialNumber);
    serialNumber[20] = '\0';
    if (!isValidAlias(serialNumber)) {
        strcpy(serialNumber, "");
        EEPROM.put(EEPROM_SERIALNUMBER, serialNumber);
    }

    EEPROM.get(EEPROM_DESCRIPTION, description);
    description[20] = '\0';
    if (!isValidAlias(description)) {
        strcpy(description, "");
        EEPROM.put(EEPROM_DESCRIPTION, description);
    }

    EEPROM.get(EEPROM_DEBUG, debugEnabled);
    if (debugEnabled != 0 && debugEnabled != 1) {
        debugEnabled = false; // Default fallback
        EEPROM.put(EEPROM_DEBUG, debugEnabled);
    }
    if (debugEnabled) {
        dbgln("Debug enabled at baud rate: " + String(baudRate));
    }

    EEPROM.get(EEPROM_ONEWIRE, oneWireCycle);
    if (oneWireCycle < 1000 || oneWireCycle > 60000) { 
        oneWireCycle = 30000;
        EEPROM.put(EEPROM_ONEWIRE, oneWireCycle);
    }
    oneWireTimer.sleep(oneWireCycle);

    EEPROM.get(EEPROM_ANAINPUT, anaInputCycle);
    if (anaInputCycle < 1000 || anaInputCycle > 60000) {
        anaInputCycle = 10000;
        EEPROM.put(EEPROM_ANAINPUT, anaInputCycle);
    }

    EEPROM.get(EEPROM_PULSEON, pulseOn);
    if (pulseOn != 0 && pulseOn != 1) {
        pulseOn = false;
        EEPROM.put(EEPROM_PULSEON, pulseOn);
    }

    EEPROM.get(EEPROM_PULSECYCLE, pulseSendCycle);
    if (pulseSendCycle < 1000 || pulseSendCycle > 60000) {
        pulseSendCycle = 2000;
        EEPROM.put(EEPROM_PULSECYCLE, pulseSendCycle);
    }

    EEPROM.get(EEPROM_CHECKINTERVAL, checkInterval);
    if (debugEnabled) {
        dbg(F("Načtená hodnota checkInterval z EEPROM: "));
        dbgln(String(checkInterval));
    }
    if (checkInterval < 1000 || checkInterval > 60000 || checkInterval == 0xFFFFFFFF) {
        checkInterval = 10000; // Výchozí hodnota 10000 ms
        EEPROM.put(EEPROM_CHECKINTERVAL, checkInterval);
        if (debugEnabled) {
            dbg(F("Inicializace checkInterval na: "));
            dbgln(String(checkInterval));
        }
    }

    for (int i = 0; i < numOfRelays; i++) {
        EEPROM.get(EEPROM_ALIAS_RELAYS + (i * 21), aliasRelays[i]);
        aliasRelays[i][20] = '\0';
        if (!isValidAlias(aliasRelays[i])) {
            strcpy(aliasRelays[i], "");
            EEPROM.put(EEPROM_ALIAS_RELAYS + (i * 21), aliasRelays[i]);
        }
    }
    for (int i = 0; i < numOfHSSwitches; i++) {
        EEPROM.get(EEPROM_ALIAS_HSS + (i * 21), aliasHSS[i]);
        aliasHSS[i][20] = '\0';
        if (!isValidAlias(aliasHSS[i])) {
            strcpy(aliasHSS[i], "");
            EEPROM.put(EEPROM_ALIAS_HSS + (i * 21), aliasHSS[i]);
        }
    }
    for (int i = 0; i < numOfLSSwitches; i++) {
        EEPROM.get(EEPROM_ALIAS_LSS + (i * 21), aliasLSS[i]);
        aliasLSS[i][20] = '\0';
        if (!isValidAlias(aliasLSS[i])) {
            strcpy(aliasLSS[i], "");
            EEPROM.put(EEPROM_ALIAS_LSS + (i * 21), aliasLSS[i]);
        }
    }
    for (int i = 0; i < numOfDigInputs; i++) {
        EEPROM.get(EEPROM_ALIAS_DIGINPUTS + (i * 21), aliasDigInputs[i]);
        aliasDigInputs[i][20] = '\0';
        if (!isValidAlias(aliasDigInputs[i])) {
            strcpy(aliasDigInputs[i], "");
            EEPROM.put(EEPROM_ALIAS_DIGINPUTS + (i * 21), aliasDigInputs[i]);
        }
    }
    for (int i = 0; i < numOfAnaInputs; i++) {
        EEPROM.get(EEPROM_ALIAS_ANA_INPUTS + (i * 21), aliasAnaInputs[i]);
        aliasAnaInputs[i][20] = '\0';
        if (!isValidAlias(aliasAnaInputs[i])) {
            strcpy(aliasAnaInputs[i], "");
            EEPROM.put(EEPROM_ALIAS_ANA_INPUTS + (i * 21), aliasAnaInputs[i]);
        }
    }
    for (int i = 0; i < numOfAnaOuts; i++) {
        EEPROM.get(EEPROM_ALIAS_ANA_OUTS + (i * 21), aliasAnaOuts[i]);
        aliasAnaOuts[i][20] = '\0';
        if (!isValidAlias(aliasAnaOuts[i])) {
            strcpy(aliasAnaOuts[i], "");
            EEPROM.put(EEPROM_ALIAS_ANA_OUTS + (i * 21), aliasAnaOuts[i]);
        }
    }
    for (int i = 0; i < maxSensors; i++) {
        EEPROM.get(EEPROM_ALIAS_DS2438 + (i * 21), aliasDS2438[i]);
        aliasDS2438[i][20] = '\0';
        if (!isValidAlias(aliasDS2438[i])) {
            strcpy(aliasDS2438[i], "");
            EEPROM.put(EEPROM_ALIAS_DS2438 + (i * 21), aliasDS2438[i]);
        }
    }
    for (int i = 0; i < maxSensors; i++) {
        EEPROM.get(EEPROM_ALIAS_DS18B20 + (i * 21), aliasDS18B20[i]);
        aliasDS18B20[i][20] = '\0';
        if (!isValidAlias(aliasDS18B20[i])) {
            strcpy(aliasDS18B20[i], "");
            EEPROM.put(EEPROM_ALIAS_DS18B20 + (i * 21), aliasDS18B20[i]);
        }
    }

    // Inicializační flag pro aliasy (hodnota 0xAA znamená inicializováno)
    byte initFlag = 0;
    EEPROM.get(EEPROM_INIT_FLAG, initFlag);
    dbgln("EEPROM init flag: " + String(initFlag, HEX)); // Debug výpis

    if (initFlag != 0xAA) {
        // První spuštění: Inicializuj všechny aliasy na prázdné stringy
        dbgln("Inicializace EEPROM aliasů na prázdné hodnoty...");
        char emptyAlias[21] = "";  // Prázdný string s null terminátorem
        
        EEPROM.put(EEPROM_SERIALLOCK, false);

        for (int i = 0; i < numOfRelays; i++) {
            EEPROM.put(EEPROM_ALIAS_RELAYS + (i * 21), emptyAlias);
        }
        for (int i = 0; i < numOfHSSwitches; i++) {
            EEPROM.put(EEPROM_ALIAS_HSS + (i * 21), emptyAlias);
        }
        for (int i = 0; i < numOfLSSwitches; i++) {
            EEPROM.put(EEPROM_ALIAS_LSS + (i * 21), emptyAlias);
        }
        for (int i = 0; i < numOfDigInputs; i++) {
            EEPROM.put(EEPROM_ALIAS_DIGINPUTS + (i * 21), emptyAlias);
        }
        for (int i = 0; i < numOfAnaInputs; i++) {
            EEPROM.put(EEPROM_ALIAS_ANA_INPUTS + (i * 21), emptyAlias);
        }
        for (int i = 0; i < numOfAnaOuts; i++) {
            EEPROM.put(EEPROM_ALIAS_ANA_OUTS + (i * 21), emptyAlias);
        }
        for (int i = 0; i < maxSensors; i++) {
            EEPROM.put(EEPROM_ALIAS_DS2438 + (i * 21), emptyAlias);
        }
        for (int i = 0; i < maxSensors; i++) {
            EEPROM.put(EEPROM_ALIAS_DS18B20 + (i * 21), emptyAlias);
        }
        
        EEPROM.put(EEPROM_DESCRIPTION, emptyAlias);
        EEPROM.put(EEPROM_CHECKINTERVAL, (unsigned long)10000); // Inicializace checkInterval na 10000 ms
        if (debugEnabled) {
            dbgln(F("Inicializace EEPROM_CHECKINTERVAL na 10000 ms při prvním spuštění."));
        }
        initFlag = 0xAA;
        EEPROM.put(EEPROM_INIT_FLAG, initFlag);
    }

    if (pulseOn) {
        attachInterrupt(digitalPinToInterrupt(21), [](){pulse1++;}, FALLING);
        attachInterrupt(digitalPinToInterrupt(20), [](){pulse2++;}, FALLING);
        attachInterrupt(digitalPinToInterrupt(19), [](){pulse3++;}, FALLING);
        pulseTimer.sleep(pulseSendCycle);
    }

    // Read board address from DIP switches
    for (int i = 0; i < 4; i++) {
        pinMode(dipSwitchPins[i], INPUT);
        if (!digitalRead(dipSwitchPins[i])) { boardAddress |= (1 << i); }
    }

    // Nová logika na základě detekce ethernet shieldu:
    if (ethShieldDetected()) { ethernetOK = true; dbgln("Ethernet shield detekován.");
    } else { ethernetOK = false; dbgln("Ethernet shield NENÍ detekován.");    }
 
       // Print communication parameters
    dbg(String(baudRate));
    dbg(" Bd, Tx Delay: ");
    dbg(String(serial3TxDelay));
    dbg(" ms, Timeout: ");
    dbg(String(serial3TimeOut));
    dbgln(" ms");

    // Set board address strings
    boardAddressStr = String(boardAddress); 
    boardAddressRailStr = railStr + String(boardAddress);

    // Initialize Ethernet if enabled
    if (ethernetOK) {
        mac[5] = (0xED + boardAddress); // Adjust MAC address based on board address
        while (!dhcpSuccess) {
            dbgln("Attempting to obtain IP address...");
            digitalWrite(ledPins[0], HIGH);
            if (Ethernet.begin(mac) != 0) {
                dhcpSuccess = true;
            } else {
                delay(1000);
            }
            digitalWrite(ledPins[0], LOW);
        }
        udpRecv.begin(listenPort); // Start UDP receiver
        udpSend.begin(sendPort);   // Start UDP sender
        server.begin(); // Start HTTP server
        dbg("IP address: ");
        printIPAddress(Ethernet.localIP()); // Print local IP
        dbgln("");
        dhcpServer = Ethernet.gatewayIP(); // Store gateway IP
        dbg("Gateway (DHCP server): ");
        printIPAddress(dhcpServer); // Print gateway IP
        dbgln("");
    }

    // Initialize MODBUS data
    memset(Mb.MbData, 0, sizeof(Mb.MbData));
    modbus_configure(&Serial3, baudRate, SERIAL_8N1, boardAddress, serial3TxControl, sizeof(Mb.MbData), Mb.MbData); 

    // Initialize RS485 communication
    Serial3.begin(baudRate);
    Serial3.setTimeout(serial3TimeOut);
    pinMode(serial3TxControl, OUTPUT);
    digitalWrite(serial3TxControl, 0);

    // Print board address
    dbg("Physical address: ");
    dbgln(boardAddressStr);

    // Search for 1-Wire sensors
    lookUpSensors(); 

    // Enable watchdog timer (8 seconds)
    wdt_enable(WDTO_8S);
}

// Main loop
void loop() {
    wdt_reset(); // Reset watchdog timer

    // Check Ethernet connection periodically
    unsigned long currentTime = millis();
    if (currentTime - lastCheckTime >= checkInterval) {
        lastCheckTime = currentTime;
        checkEthernet();
    }

    // Handle webserver requests if Ethernet is enabled
    if (ethernetOK) {
        handleWebServer();
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

    // Run MODBUS and Ethernet maintenance
    if (ethernetOK) {Mb.MbsRun(); IPrenew();}

    // Update MODBUS if RS485 is enabled
    modbus_update();

    // Send pulse packet if pulse sensing is enabled
    if (pulseOn) {
        sendPulsePacket();
    }
}

bool isValidAlias(const char* alias) {
    int len = strlen(alias);
    if (len > 20) return false; // Max 20 znaků + null terminátor
    for (int i = 0; i < len; i++) {
        // Povolit ASCII 32–126 (včetně mezery) a základní UTF-8 diakritiku (např. 128–255)
        if (alias[i] < 32 || (alias[i] > 126 && alias[i] < 128)) {
            return false; // Zakázat neprintable znaky mimo UTF-8 diakritiku
        }
    }
    return true;
}

String urlDecode(String input) {
    String result = "";
    for (size_t i = 0; i < input.length(); i++) {
        if (input[i] == '%') {
            if (i + 2 < input.length()) {
                String hex = input.substring(i + 1, i + 3);
                char decoded = (char) strtol(hex.c_str(), NULL, 16);
                result += decoded;
                i += 2;
            }
        } else if (input[i] == '+') {
            result += ' ';
        } else {
            result += input[i];
        }
    }
    return result;
}

// Send pulse counts as digital input messages
void sendPulsePacket() {
    if (!pulseTimer.isOver()) {
        return;
    }
    pulseTimer.sleep(pulseSendCycle);
    sendMsg("di10 " + String(pulse1));
    sendMsg("di11 " + String(pulse2));
    sendMsg("di12 " + String(pulse3));
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
void printIPAddress(IPAddress ip) {
    for (byte thisByte = 0; thisByte < 4; thisByte++) {
        dbg(String(ip[thisByte]));
        if (thisByte < 3) dbg(".");
    }
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
                    bitWrite(Mb.MbData[byteNo+5], bitPos, 1);
                    sendInputOn(i + 1);
                } else {
                    bitWrite(Mb.MbData[byteNo+5], bitPos, 0);         
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

// Send digital input on message
void sendInputOn(int input) {
    sendMsg(digInputStr + String(input, DEC) + " 1");
}

// Send digital input off message
void sendInputOff(int input) {
    sendMsg(digInputStr + String(input, DEC) + " 0");
}

// Send analog input message
void sendAnaInput(int input, float value) {
    sendMsg(anaInputStr + String(input, DEC) + " " + String(value, 2));
}

// Send a message via UDP
void sendMsg(String message) {
    message = railStr + boardAddressStr + " " + message;
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
    if (value == 0 || value == 1) {
        digitalWrite(HSSwitchPins[hsswitch], value); // Binární zapnutí/vypnutí
    } else if (value >= 2 && value <= 255) {
        analogWrite(HSSwitchPins[hsswitch], value); // Nastavit PWM
    }
}

// Set Low-Side switch state or PWM
void setLSSwitch(int lsswitch, int value) {
    if (lsswitch >= numOfLSSwitches) {
        return;
    }
    if (lsswitch == 3 && value >= 2 && value <= 255) { // Pin 18 (LSS 4) nepodporuje PWM
        dbg(F("Warning: LSS 4 (pin 18) does not support PWM, ignoring value "));
        dbgln(String(value));
        return;
    }
    if (value == 0 || value == 1) {
        digitalWrite(LSSwitchPins[lsswitch], value); // Binární zapnutí/vypnutí
    } else if (value >= 2 && value <= 255) {
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
    if (ethernetOK) {
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

// Process incoming commands
void processCommands() {
    String cmd;
    byte byteNo, bitPos;
    for (int i = 0; i < numOfRelays; i++) {
        if (i < 8) {
            setRelay(i, bitRead(Mb.MbData[relOut1Byte], i));
        } else {
            setRelay(i, bitRead(Mb.MbData[relOut2Byte], i-8));
        }
    }
    for (int i = 0; i < numOfHSSwitches; i++) {
        setHSSwitch(i, bitRead(Mb.MbData[hssLssByte], i));
    }
    for (int i = 0; i < numOfLSSwitches; i++) {
        setLSSwitch(i, bitRead(Mb.MbData[hssLssByte], i + numOfHSSwitches));
    }
    for (int i = 0; i < numOfAnaOuts; i++) {
        setAnaOut(i, Mb.MbData[anaOut1Byte+i]);
    }
    if (bitRead(Mb.MbData[serviceByte], 0)) {
        resetFunc();
    }
    if (receivePacket(&cmd)) {
        dbg("Received packet: ");
        dbgln(cmd);
        digitalWrite(ledPins[1], HIGH);
        if (cmd.startsWith(relayStr)) {
            for (int i = 0; i < numOfRelays; i++) {
                if (i < 8) { byteNo = relOut1Byte; bitPos = i; } else { byteNo = relOut2Byte; bitPos = i-8; }
                if (cmd == relayOnCommands[i]) {
                    bitWrite(Mb.MbData[byteNo], bitPos, 1);
                } else if (cmd == relayOffCommands[i]) {
                    bitWrite(Mb.MbData[byteNo], bitPos, 0);
                }
            }
       } else if (cmd.startsWith(HSSwitchStr)) {
            for (int i = 0; i < numOfHSSwitches; i++) {
                if (cmd == HSSwitchOnCommands[i]) {
                    Mb.MbData[hssPWM1Byte + i] = 255; // Nastavit PWM na 255 pro ON
                    bitWrite(Mb.MbData[hssLssByte], i, 1);
                    setHSSwitch(i, 255);
                    dbg(F("HSS "));
                    dbg(String(i + 1));
                    dbgln(F(" ON"));
                } else if (cmd == HSSwitchOffCommands[i]) {
                    Mb.MbData[hssPWM1Byte + i] = 0; // Nastavit PWM na 0 pro OFF
                    bitWrite(Mb.MbData[hssLssByte], i, 0);
                    setHSSwitch(i, 0);
                    dbg(F("HSS "));
                    dbg(String(i + 1));
                    dbgln(F(" OFF"));
                } else if (cmd.startsWith(HSSwitchPWMCommands[i])) {
                    String pwmValue = cmd.substring(HSSwitchPWMCommands[i].length() + 1);
                    int value = pwmValue.toInt();
                    if (value >= 0 && value <= 255) {
                        Mb.MbData[hssPWM1Byte + i] = value; // Ukládat do individuálního registru
                        bitWrite(Mb.MbData[hssLssByte], i, (value > 0) ? 1 : 0);
                        setHSSwitch(i, value);
                        dbg(F("HSS PWM "));
                        dbg(String(i + 1));
                        dbg(F(" set to "));
                        dbgln(String(value));
                    }
                }
            }
        } else if (cmd.startsWith(LSSwitchStr)) {
            for (int i = 0; i < numOfLSSwitches; i++) {
                if (cmd == LSSwitchOnCommands[i]) {
                    Mb.MbData[lssPWM1Byte + i] = 255;
                    bitWrite(Mb.MbData[hssLssByte], i + numOfHSSwitches, 1);
                    setLSSwitch(i, 255);
                    dbg(F("LSS "));
                    dbg(String(i + 1));
                    dbgln(F(" ON"));
                } else if (cmd == LSSwitchOffCommands[i]) {
                    Mb.MbData[lssPWM1Byte + i] = 0;
                    bitWrite(Mb.MbData[hssLssByte], i + numOfHSSwitches, 0);
                    setLSSwitch(i, 0);
                    dbg(F("LSS "));
                    dbg(String(i + 1));
                    dbgln(F(" OFF"));
                } else if (cmd.startsWith(LSSwitchPWMCommands[i])) {
                    String pwmValue = cmd.substring(LSSwitchPWMCommands[i].length() + 1);
                    int value = pwmValue.toInt();
                    if (value >= 0 && value <= 255) {
                        Mb.MbData[lssPWM1Byte + i] = value;
                        bitWrite(Mb.MbData[hssLssByte], i + numOfHSSwitches, (value > 0) ? 1 : 0);
                        setLSSwitch(i, value);
                        dbg(F("LSS PWM "));
                        dbg(String(i + 1));
                        dbg(F(" set to "));
                        dbgln(String(value));
                    }
                }
            }
        } else if (cmd.startsWith(anaOutStr)) {
            String anaOutValue = cmd.substring(anaOutStr.length() + 2);
            for (int i = 0; i < numOfAnaOuts; i++) {
                if (cmd.substring(0, anaOutStr.length()+1) == anaOutCommand[i]) {
                    Mb.MbData[anaOut1Byte+i] = anaOutValue.toInt();
                } 
            }
        } else if (cmd.startsWith(rstStr)) {
            bitWrite(Mb.MbData[serviceByte], 0, 1);
        }
        digitalWrite(ledPins[1], LOW);
    }
}