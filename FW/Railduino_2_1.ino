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
IPAddress listenIpAddress;        // IP address for receiving
IPAddress sendIpAddress(255, 255, 255, 255); // Broadcast IP address for sending
EthernetClient testClient; // Client pro TCP test připojení

#define EEPROM_ONEWIRE 0
#define EEPROM_ANAINPUT 4
#define EEPROM_PULSEON 8
#define EEPROM_PULSECYCLE 12
#define EEPROM_DEBUG 16
#define EEPROM_SERIALNUMBER 20
#define EEPROM_SERIALLOCK 40
#define EEPROM_INIT_FLAG 41
#define EEPROM_ALIAS_START 42
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
unsigned long oneWireSubCycle = 5000; // Sub-cycle for 1-Wire (ms)
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
String digStatStr = "stat"; // Prefix for status messages
String rstStr = "rst"; // Prefix for reset command
String relayOnCommands[numOfRelays]; // Commands for turning relays on
String relayOffCommands[numOfRelays]; // Commands for turning relays off
String HSSwitchOnCommands[numOfHSSwitches]; // Commands for turning HSS on
String HSSwitchOffCommands[numOfHSSwitches]; // Commands for turning HSS off
String LSSwitchOnCommands[numOfLSSwitches]; // Commands for turning LSS on
String LSSwitchOffCommands[numOfLSSwitches]; // Commands for turning LSS off
String HSSwitchPWMCommands[numOfHSSwitches]; // Příkazy pro PWM HSS, např. "ho1_pwm"
String LSSwitchPWMCommands[numOfLSSwitches]; // Příkazy pro PWM LSS, např. "lo1_pwm"
String digStatCommand[numOfDigInputs]; // Commands for digital input status
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
byte readstage = 0, resolution = 11; // Reading stage and sensor resolution
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
        dbgln("[" + String(millis()) + "] Ethernet reset failed. Triggering watchdog reset...");
        dhcpSuccess = false;
        while (true) {} // Infinite loop to trigger watchdog reset
    } 
}

// Check the Ethernet module
void checkEthernet() {
    if (!dhcpSuccess) return;

    IPAddress currentIP = Ethernet.localIP();
    if (currentIP[0] == 0 && currentIP[1] == 0 && currentIP[2] == 0 && currentIP[3] == 0) {
        dhcpSuccess = false;
        resetEthernetModule();
        return;
    }

    udpRecv.stop();
    udpSend.stop();

    unsigned long startTime = millis();
    bool connected = false;
    if (!testClient.connected()) {
        while (millis() - startTime < 100) {
            if (testClient.connect(dhcpServer, 53)) {
                connected = true;
                break;
            }
            delay(10);
        }
    } else {
        connected = true;
    }
    if (!connected) {
        dhcpSuccess = false;
        testClient.stop();
        resetEthernetModule();
        return;
    }
    testClient.stop();

    udpRecv.begin(listenPort);
    udpSend.begin(sendPort);
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
                    // Return JSON with register states
                    client.println(F("HTTP/1.1 200 OK"));
                    client.println(F("Content-Type: application/json"));
                    client.println(F("Connection: close"));
                    client.println();

                    // Add caching for 1-wire sensors to update based on oneWireCycle
                    static unsigned long lastOneWireUpdate = 0;
                    const int MAX_1WIRE_SENSORS = 10; // Assuming maximum of 10 sensors for each type
                    static float ds2438Temps[MAX_1WIRE_SENSORS];
                    static float ds2438Vads[MAX_1WIRE_SENSORS];
                    static float ds2438Vsens[MAX_1WIRE_SENSORS];
                    static float ds18b20Temps[MAX_1WIRE_SENSORS];

                    if (millis() - lastOneWireUpdate >= oneWireCycle) {
                        for (int i = 0; i < DS2438count; i++) {
                            ds2438.update(sensors2438[i]);
                            ds2438Temps[i] = ds2438.getTemperature();
                            ds2438Vads[i] = ds2438.getVoltage(DS2438_CHA);
                            ds2438Vsens[i] = ds2438.getVoltage(DS2438_CHB);
                        }
                        for (int i = 0; i < DS18B20count; i++) {
                            float temp = dsreadtemp(ds, sensors18B20[i]);
                            if (isnan(temp)) temp = 0.0; // Ensure valid value
                            ds18b20Temps[i] = temp;
                        }
                        lastOneWireUpdate = millis();
                    }

                    // Výpočet napětí pro analog inputs
                    for (int i = 0; i < numOfAnaInputs; i++) {
                        anaInputsVoltage[i] = (analogStatus[i] / 1024.0) * 10.0;
                    }
                    // Výpočet napětí pro analog outputs
                    for (int i = 0; i < numOfAnaOuts; i++) {
                        anaOutsVoltage[i] = (Mb.MbData[anaOut1Byte + i] / 255.0) * 10.0;
                    }
                    // Send JSON in chunks to prevent memory overflow
                    client.print(F("{"));
                    client.print(F("\"relays\": ["));
                    for (int i = 0; i < numOfRelays; i++) {
                        byte byteNo = (i < 8) ? relOut1Byte : relOut2Byte;
                        byte bitPos = (i < 8) ? i : i - 8;
                        client.print(bitRead(Mb.MbData[byteNo], bitPos));
                        if (i < numOfRelays - 1) client.print(F(","));
                    }
                    client.print(F("],"));
                    client.print(F("\"hss\": ["));
                    for (int i = 0; i < numOfHSSwitches; i++) {
                        int bitValue = bitRead(Mb.MbData[hssLssByte], i);
                        client.print(bitValue);
                        if (i < numOfHSSwitches - 1) client.print(F(","));
                    }
                    client.print(F("],"));
                    client.print(F("\"lss\": ["));
                    for (int i = 0; i < numOfLSSwitches; i++) {
                        int bitValue = bitRead(Mb.MbData[hssLssByte], i + numOfHSSwitches);
                        client.print(bitValue);
                        if (i < numOfLSSwitches - 1) client.print(F(","));
                    }
                    client.print(F("],"));
                    client.print(F("\"hssPWM\": ["));
                    for (int i = 0; i < numOfHSSwitches; i++) {
                        client.print(Mb.MbData[hssPWM1Byte + i]);
                        if (i < numOfHSSwitches - 1) client.print(F(","));
                    }
                    client.print(F("],"));
                    client.print(F("\"lssPWM\": ["));
                    for (int i = 0; i < numOfLSSwitches; i++) {
                        client.print(Mb.MbData[lssPWM1Byte + i]);
                        if (i < numOfLSSwitches - 1) client.print(F(","));
                    }
                    client.print(F("],"));
                    client.print(F("\"anaOuts\": ["));
                    for (int i = 0; i < numOfAnaOuts; i++) {
                        client.print(Mb.MbData[anaOut1Byte + i]);
                        if (i < numOfAnaOuts - 1) client.print(F(","));
                    }
                    client.print(F("],"));
                    client.print(F("\"digInputs\": ["));
                    for (int i = 0; i < numOfDigInputs; i++) {
                        client.print(1 - inputStatus[i]);  // Inverted due to negated logic of digital inputs
                        if (i < numOfDigInputs - 1) client.print(F(","));
                    }
                    client.print(F("],"));
                    client.print(F("\"anaInputs\": ["));
                    for (int i = 0; i < numOfAnaInputs; i++) {
                        client.print(analogStatus[i], 2);
                        if (i < numOfAnaInputs - 1) client.print(F(","));
                    }
                    client.print(F("],"));
                    client.print(F("\"ds2438\": ["));
                    for (int i = 0; i < DS2438count; i++) {
                        client.print(F("{\"sn\":\""));
                        client.print(oneWireAddressToString(sensors2438[i]));
                        client.print(F("\",\"temp\":"));
                        client.print(ds2438Temps[i], 2);
                        client.print(F(",\"vad\":"));
                        client.print(ds2438Vads[i], 2);
                        client.print(F(",\"vsens\":"));
                        client.print(ds2438Vsens[i], 2);
                        client.print(F("}"));
                        if (i < DS2438count - 1) client.print(F(","));
                    }
                    client.print(F("],"));
                    client.print(F("\"ds18b20\": ["));
                    for (int i = 0; i < DS18B20count; i++) {
                        client.print(F("{\"sn\":\""));
                        client.print(oneWireAddressToString(sensors18B20[i]));
                        client.print(F("\",\"temp\":"));
                        client.print(ds18b20Temps[i], 2);
                        client.print(F("}"));
                        if (i < DS18B20count - 1) client.print(F(","));
                    }
                    client.print(F("]"));
                    client.print(F(",\"pulseCounts\": ["));
                    client.print(pulse1);
                    client.print(F(","));
                    client.print(pulse2);
                    client.print(F(","));
                    client.print(pulse3);
                    client.print(F("]"));
                    client.print(F(",\"oneWireCycle\":"));
                    client.print(oneWireCycle);
                    client.print(F(",\"anaInputCycle\":"));
                    client.print(anaInputCycle);
                    client.print(F(",\"pulseOn\":"));
                    client.print(pulseOn ? 1 : 0);
                    client.print(F(",\"pulseSendCycle\":"));
                    client.print(pulseSendCycle);
                    client.print(F(",\"debugEnabled\":"));
                    client.print(debugEnabled ? 1 : 0);
                    client.print(F(",\"serialNumber\":\""));
                    client.print(serialNumber);
                    client.print(F("\""));
                    client.print(F(",\"serialLocked\":"));
                    client.print(serialLocked ? 1 : 0);
                    client.print(F(",\"anaInputsVoltage\": ["));
                    for (int i = 0; i < numOfAnaInputs; i++) {
                        client.print(anaInputsVoltage[i], 2);
                        if (i < numOfAnaInputs - 1) client.print(F(","));
                    }
                    client.print(F("]"));
                    client.print(F(",\"anaOutsVoltage\": ["));
                    for (int i = 0; i < numOfAnaOuts; i++) {
                        client.print(anaOutsVoltage[i], 2);
                        if (i < numOfAnaOuts - 1) client.print(F(","));
                    }
                    client.print(F("]"));

                    client.print(F(",\"alias_relays\": ["));
                    for (int i = 0; i < numOfRelays; i++) {
                        client.print(F("\""));
                        client.print(aliasRelays[i]);
                        client.print(F("\""));
                        if (i < numOfRelays - 1) client.print(F(","));
                    }
                    client.print(F("]"));
                    client.print(F(",\"alias_hss\": ["));
                    for (int i = 0; i < numOfHSSwitches; i++) {
                        client.print(F("\""));
                        client.print(aliasHSS[i]);
                        client.print(F("\""));
                        if (i < numOfHSSwitches - 1) client.print(F(","));
                    }
                    client.print(F("]"));
                    client.print(F(",\"alias_lss\": ["));
                    for (int i = 0; i < numOfLSSwitches; i++) {
                        client.print(F("\""));
                        client.print(aliasLSS[i]);
                        client.print(F("\""));
                        if (i < numOfLSSwitches - 1) client.print(F(","));
                    }
                    client.print(F("]"));
                    client.print(F(",\"alias_digInputs\": ["));
                    for (int i = 0; i < numOfDigInputs; i++) {
                        client.print(F("\""));
                        client.print(aliasDigInputs[i]);
                        client.print(F("\""));
                        if (i < numOfDigInputs - 1) client.print(F(","));
                    }
                    client.print(F("]"));
                    client.print(F(",\"alias_anaInputs\": ["));
                    for (int i = 0; i < numOfAnaInputs; i++) {
                        client.print(F("\""));
                        client.print(aliasAnaInputs[i]);
                        client.print(F("\""));
                        if (i < numOfAnaInputs - 1) client.print(F(","));
                    }
                    client.print(F("]"));
                    client.print(F(",\"alias_anaOuts\": ["));
                    for (int i = 0; i < numOfAnaOuts; i++) {
                        client.print(F("\""));
                        client.print(aliasAnaOuts[i]);
                        client.print(F("\""));
                        if (i < numOfAnaOuts - 1) client.print(F(","));
                    }
                    client.print(F("]"));
                    client.print(F(",\"alias_ds2438\": ["));
                    for (int i = 0; i < DS2438count; i++) {
                        client.print(F("\""));
                        client.print(aliasDS2438[i]);
                        client.print(F("\""));
                        if (i < DS2438count - 1) client.print(F(","));
                    }
                    client.print(F("]"));
                    client.print(F(",\"alias_ds18b20\": ["));
                    for (int i = 0; i < DS18B20count; i++) {
                        client.print(F("\""));
                        client.print(aliasDS18B20[i]);
                        client.print(F("\""));
                        if (i < DS18B20count - 1) client.print(F(","));
                    }
                    client.print(F("]"));
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
                    // Process commands
                    if (request.indexOf("serialNumber=") != -1) {
                        int startIdx = request.indexOf(F("serialNumber=")) + 13;
                        int endIdx = request.indexOf(F("&"), startIdx);
                        if (endIdx == -1) endIdx = request.indexOf(F(" HTTP"));
                        String value = request.substring(startIdx, endIdx);
                        value = value.substring(0, 20);
                        value.toCharArray(serialNumber, 21);
                        EEPROM.put(EEPROM_SERIALNUMBER, serialNumber);
                        dbg(F("Serial Number set to ")); dbgln(value);
                    }                 
                    if (request.indexOf("relay") != -1) {
                        for (int i = 0; i < numOfRelays; i++) {
                            String relayParam = "relay" + String(i + 1) + "=";
                            if (request.indexOf(relayParam) != -1) {
                                int startIdx = request.indexOf(relayParam) + relayParam.length();
                                int endIdx = request.indexOf("&", startIdx);
                                if (endIdx == -1) endIdx = request.indexOf(" HTTP");
                                int value = request.substring(startIdx, endIdx).toInt();
                                byte byteNo = (i < 8) ? relOut1Byte : relOut2Byte;
                                byte bitPos = (i < 8) ? i : i - 8;
                                bitWrite(Mb.MbData[byteNo], bitPos, value);
                                setRelay(i, value);
                                dbg(F("Relay "));
                                dbg(String(i + 1));
                                dbg(F(" set to "));
                                dbgln(String(value));
                            }
                        }
                    }
                    if (request.indexOf("hss") != -1) {
                        for (int i = 0; i < numOfHSSwitches; i++) {
                            String hssParam = "hss" + String(i + 1) + "=";
                            if (request.indexOf(hssParam) != -1) {
                                int startIdx = request.indexOf(hssParam) + hssParam.length();
                                int endIdx = request.indexOf("&", startIdx);
                                if (endIdx == -1) endIdx = request.indexOf(" HTTP");
                                int value = request.substring(startIdx, endIdx).toInt();
                                if (value >= 0 && value <= 255) {
                                    Mb.MbData[hssPWM1Byte + i] = value;
                                    bitWrite(Mb.MbData[hssLssByte], i, (value > 0) ? 1 : 0);
                                    setHSSwitch(i, value);
                                    dbg(F("HSS "));
                                    dbg(String(i + 1));
                                    dbg(F(" set to "));
                                    dbg(String(value));
                                    dbg(F(" reg "));
                                    dbg(String(hssPWM1Byte + i));
                                    dbg(F(" bit "));
                                    dbgln(String(bitRead(Mb.MbData[hssLssByte], i)));
                                }
                            }
                        }
                    }
                    if (request.indexOf("lss") != -1) {
                        for (int i = 0; i < numOfLSSwitches; i++) {
                            String lssParam = "lss" + String(i + 1) + "=";
                            if (request.indexOf(lssParam) != -1) {
                                int startIdx = request.indexOf(lssParam) + lssParam.length();
                                int endIdx = request.indexOf("&", startIdx);
                                if (endIdx == -1) endIdx = request.indexOf(" HTTP");
                                int value = request.substring(startIdx, endIdx).toInt();
                                if (value >= 0 && value <= 255) {
                                    Mb.MbData[lssPWM1Byte + i] = value;
                                    bitWrite(Mb.MbData[hssLssByte], i + numOfHSSwitches, (value > 0) ? 1 : 0);
                                    setLSSwitch(i, value);
                                    dbg(F("LSS "));
                                    dbg(String(i + 1));
                                    dbg(F(" set to "));
                                    dbg(String(value));
                                    dbg(F(" reg "));
                                    dbg(String(lssPWM1Byte + i));
                                    dbg(F(" bit "));
                                    dbgln(String(bitRead(Mb.MbData[hssLssByte], i + numOfHSSwitches)));
                                }
                            }
                        }
                    }
                    if (request.indexOf("anaOut") != -1) {
                        for (int i = 0; i < numOfAnaOuts; i++) {
                            String anaOutParam = "anaOut" + String(i + 1) + "=";
                            if (request.indexOf(anaOutParam) != -1) {
                                int startIdx = request.indexOf(anaOutParam) + anaOutParam.length();
                                int endIdx = request.indexOf("&", startIdx);
                                if (endIdx == -1) endIdx = request.indexOf(" HTTP");
                                int value = request.substring(startIdx, endIdx).toInt();
                                if (value > 255) value = 255;
                                if (value < 0) value = 0;
                                Mb.MbData[anaOut1Byte + i] = value;
                                setAnaOut(i, value);
                                dbg(F("Analog Output "));
                                dbg(String(i + 1));
                                dbg(F(" set to "));
                                dbgln(String(value));
                            }
                        }
                    }
                    if (request.indexOf("oneWireCycle=") != -1) {
                        int startIdx = request.indexOf(F("oneWireCycle=")) + 13;
                        int endIdx = request.indexOf(F("&"), startIdx);
                        if (endIdx == -1) endIdx = request.indexOf(F(" HTTP"));
                        oneWireCycle = request.substring(startIdx, endIdx).toInt();
                        if (oneWireCycle < 1000) oneWireCycle = 1000;
                        oneWireTimer.sleep(oneWireCycle);
                        EEPROM.put(EEPROM_ONEWIRE, oneWireCycle);
                        dbg(F("Updated oneWireCycle: "));
                        dbgln(String(oneWireCycle));
                    }
                    if (request.indexOf("anaInputCycle=") != -1) {
                        int startIdx = request.indexOf(F("anaInputCycle=")) + 14;
                        int endIdx = request.indexOf(F("&"), startIdx);
                        if (endIdx == -1) endIdx = request.indexOf(F(" HTTP"));
                        anaInputCycle = request.substring(startIdx, endIdx).toInt();
                        if (anaInputCycle < 1000) anaInputCycle = 1000;
                        analogTimer.sleep(anaInputCycle);
                        EEPROM.put(EEPROM_ANAINPUT, anaInputCycle);
                        dbg(F("Updated anaInputCycle: "));
                        dbgln(String(anaInputCycle));
                    }
                    if (request.indexOf("pulseOn=") != -1) {
                        int startIdx = request.indexOf(F("pulseOn=")) + 8;
                        int endIdx = request.indexOf(F("&"), startIdx);
                        if (endIdx == -1) endIdx = request.indexOf(F(" HTTP"));
                        int value = request.substring(startIdx, endIdx).toInt();
                        pulseOn = (value == 1);
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
                        dbg(F("Updated pulseOn: "));
                        dbgln(String(pulseOn));
                    }
                    if (request.indexOf("pulseSendCycle=") != -1) {
                        int startIdx = request.indexOf(F("pulseSendCycle=")) + 15;
                        int endIdx = request.indexOf(F("&"), startIdx);
                        if (endIdx == -1) endIdx = request.indexOf(F(" HTTP"));
                        pulseSendCycle = request.substring(startIdx, endIdx).toInt();
                        if (pulseSendCycle < 1000) pulseSendCycle = 1000;
                        EEPROM.put(EEPROM_PULSECYCLE, pulseSendCycle);
                        if (pulseOn) {
                            pulseTimer.sleep(pulseSendCycle);
                        }
                        dbg(F("Updated pulseSendCycle: "));
                        dbgln(String(pulseSendCycle));
                    }

                    if (request.indexOf("debugEnabled=") != -1) {
                        int startIdx = request.indexOf(F("debugEnabled=")) + 13;
                        int endIdx = request.indexOf(F("&"), startIdx);
                        if (endIdx == -1) endIdx = request.indexOf(F(" HTTP"));
                        int value = request.substring(startIdx, endIdx).toInt();
                        debugEnabled = (value == 1);
                        EEPROM.put(EEPROM_DEBUG, debugEnabled);
                        if (debugEnabled) {
                            dbgln("Debug enabled at baud rate: " + String(baudRate));
                        } else {
                            dbgln("Debug disabled.");  // Poslední výpis před vypnutím
                        }
                        dbg(F("Updated debugEnabled: "));
                        dbgln(String(debugEnabled));
                    } 
                    if (request.indexOf("reset=1") != -1) {
                        resetFunc();
                        dbg(F("Reset function called"));
                    }
                    // Update aliasů
                    for (int i = 0; i < numOfRelays; i++) {
                        String aliasParam = "alias_relay" + String(i + 1) + "=";
                        if (request.indexOf(aliasParam) != -1) {
                            int startIdx = request.indexOf(aliasParam) + aliasParam.length();
                            int endIdx = request.indexOf("&", startIdx);
                            if (endIdx == -1) endIdx = request.indexOf(" HTTP");
                            String value = request.substring(startIdx, endIdx);
                            value = value.substring(0, 20);  // Omezení na 20 znaků
                            value.toCharArray(aliasRelays[i], 21);
                            EEPROM.put(EEPROM_ALIAS_RELAYS + (i * 21), aliasRelays[i]);
                            dbg(F("Alias relay ")); dbg(String(i + 1)); dbg(F(" set to ")); dbgln(String(value));
                        }
                    }
                    for (int i = 0; i < numOfHSSwitches; i++) {
                        String aliasParam = "alias_hss" + String(i + 1) + "=";
                        if (request.indexOf(aliasParam) != -1) {
                            int startIdx = request.indexOf(aliasParam) + aliasParam.length();
                            int endIdx = request.indexOf("&", startIdx);
                            if (endIdx == -1) endIdx = request.indexOf(" HTTP");
                            String value = request.substring(startIdx, endIdx);
                            value = value.substring(0, 20);
                            value.toCharArray(aliasHSS[i], 21);
                            EEPROM.put(EEPROM_ALIAS_HSS + (i * 21), aliasHSS[i]);
                            dbg(F("Alias HSS ")); dbg(String(i + 1)); dbg(F(" set to ")); dbgln(String(value));
                        }
                    }
                    for (int i = 0; i < numOfLSSwitches; i++) {
                        String aliasParam = "alias_lss" + String(i + 1) + "=";
                        if (request.indexOf(aliasParam) != -1) {
                            int startIdx = request.indexOf(aliasParam) + aliasParam.length();
                            int endIdx = request.indexOf("&", startIdx);
                            if (endIdx == -1) endIdx = request.indexOf(" HTTP");
                            String value = request.substring(startIdx, endIdx);
                            value = value.substring(0, 20);
                            value.toCharArray(aliasLSS[i], 21);
                            EEPROM.put(EEPROM_ALIAS_LSS + (i * 21), aliasLSS[i]);
                            dbg(F("Alias LSS ")); dbg(String(i + 1)); dbg(F(" set to ")); dbgln(String(value));
                        }
                    }
                    for (int i = 0; i < numOfDigInputs; i++) {
                        String aliasParam = "alias_di" + String(i + 1) + "=";
                        if (request.indexOf(aliasParam) != -1) {
                            int startIdx = request.indexOf(aliasParam) + aliasParam.length();
                            int endIdx = request.indexOf("&", startIdx);
                            if (endIdx == -1) endIdx = request.indexOf(" HTTP");
                            String value = request.substring(startIdx, endIdx);
                            value = value.substring(0, 20);
                            value.toCharArray(aliasDigInputs[i], 21);
                            EEPROM.put(EEPROM_ALIAS_DIGINPUTS + (i * 21), aliasDigInputs[i]);
                            dbg(F("Alias DI ")); dbg(String(i + 1)); dbg(F(" set to ")); dbgln(String(value));
                        }
                    }
                    for (int i = 0; i < numOfAnaInputs; i++) {
                        String aliasParam = "alias_ai" + String(i + 1) + "=";
                        if (request.indexOf(aliasParam) != -1) {
                            int startIdx = request.indexOf(aliasParam) + aliasParam.length();
                            int endIdx = request.indexOf("&", startIdx);
                            if (endIdx == -1) endIdx = request.indexOf(" HTTP");
                            String value = request.substring(startIdx, endIdx);
                            value = value.substring(0, 20);
                            value.toCharArray(aliasAnaInputs[i], 21);
                            EEPROM.put(EEPROM_ALIAS_ANA_INPUTS + (i * 21), aliasAnaInputs[i]);
                            dbg(F("Alias AI ")); dbg(String(i + 1)); dbg(F(" set to ")); dbgln(String(value));
                        }
                    }
                    for (int i = 0; i < numOfAnaOuts; i++) {
                        String aliasParam = "alias_ao" + String(i + 1) + "=";
                        if (request.indexOf(aliasParam) != -1) {
                            int startIdx = request.indexOf(aliasParam) + aliasParam.length();
                            int endIdx = request.indexOf("&", startIdx);
                            if (endIdx == -1) endIdx = request.indexOf(" HTTP");
                            String value = request.substring(startIdx, endIdx);
                            value = value.substring(0, 20);
                            value.toCharArray(aliasAnaOuts[i], 21);
                            EEPROM.put(EEPROM_ALIAS_ANA_OUTS + (i * 21), aliasAnaOuts[i]);
                            dbg(F("Alias AO ")); dbg(String(i + 1)); dbg(F(" set to ")); dbgln(String(value));
                        }
                    }
                    for (int i = 0; i < DS2438count; i++) {
                        String aliasParam = "alias_ds2438_" + String(i + 1) + "=";
                        if (request.indexOf(aliasParam) != -1) {
                            int startIdx = request.indexOf(aliasParam) + aliasParam.length();
                            int endIdx = request.indexOf("&", startIdx);
                            if (endIdx == -1) endIdx = request.indexOf(" HTTP");
                            String value = request.substring(startIdx, endIdx);
                            value = value.substring(0, 20);
                            value.toCharArray(aliasDS2438[i], 21);
                            EEPROM.put(EEPROM_ALIAS_DS2438 + (i * 21), aliasDS2438[i]);
                            dbg(F("Alias DS2438 ")); dbg(String(i + 1)); dbg(F(" set to ")); dbgln(String(value));
                        }
                    }
                    for (int i = 0; i < DS18B20count; i++) {
                        String aliasParam = "alias_ds18b20_" + String(i + 1) + "=";
                        if (request.indexOf(aliasParam) != -1) {
                            int startIdx = request.indexOf(aliasParam) + aliasParam.length();
                            int endIdx = request.indexOf("&", startIdx);
                            if (endIdx == -1) endIdx = request.indexOf(" HTTP");
                            String value = request.substring(startIdx, endIdx);
                            value = value.substring(0, 20);
                            value.toCharArray(aliasDS18B20[i], 21);
                            EEPROM.put(EEPROM_ALIAS_DS18B20 + (i * 21), aliasDS18B20[i]);
                            dbg(F("Alias DS18B20 ")); dbg(String(i + 1)); dbg(F(" set to ")); dbgln(String(value));
                        }
                    }
                    
                    client.println(F("HTTP/1.1 200 OK"));
                    client.println(F("Content-Type: text/plain"));
                    client.println(F("Connection: close"));
                    client.println();
                    client.println(F("OK"));
                } else {
                    // Main page with HTML and AJAX
                    if (request.indexOf(F("GET / ")) != -1 || request.indexOf(F("GET /?")) != -1) {
                        if (request.indexOf("?lock=") != -1) {
                            int startIdx = request.indexOf(F("?lock=")) + 6;
                            int endIdx = request.indexOf(F(" HTTP"), startIdx);
                            int value = request.substring(startIdx, endIdx).toInt();
                            serialLocked = (value == 1);
                            EEPROM.put(EEPROM_SERIALLOCK, serialLocked);
                            dbg(F("Serial Number lock set to ")); dbgln(String(serialLocked));
                        }

                        // Send HTML page
                        client.println(F("HTTP/1.1 200 OK"));
                        client.println(F("Content-Type: text/html"));
                        client.println(F("Connection: close"));
                        client.println();
                        client.println(F("<!DOCTYPE HTML>"));
                        client.println(F("<html>"));
                        client.println(F("<head><title>Railduino Control</title>"));
                        client.println(F("<style>"));
                        client.println(F("body { font-family: Arial, sans-serif; font-size: 14px; position: relative; }"));
                        client.println(F("h1, h2, h3 { font-family: Arial, sans-serif; }"));
                        client.println(F("table.inner { border-collapse: collapse; width: 100%; }"));
                        client.println(F("table.outer { border: 1px solid #ccc; width: 100%;  }"));
                        client.println(F("td { vertical-align: middle; font-size: 14px; padding: 2px; }"));
                        client.println(F("input, select { font-size: 14px; padding: 2px; }"));
                        client.println(F(".reset-button { position: absolute; top: 0px; right: 0px; }"));
                        client.println(F(".reset-button input { padding: 5px; }"));
                        client.println(F("table.inner tr { height: 25px; }"));
                        client.println(F("table.inner td:first-child { width: 50px; }"));  // Název sloupec (konzistentní pro všechny tabulky)
                        client.println(F("table.inner td:nth-child(2) { width: 50px; }"));  // Stav/select/number sloupec v tabulkách s 3 sloupci
                        client.println(F("table.inner td:nth-child(3) { width: 50px; }"));  // Alias sloupec
                        client.println(F("table.inner td:nth-child(4) { width: 100px; }"));
                        client.println(F(".ethernet-table td:first-child, .cycle-table td:first-child { width: 100px; }"));  // Hodnota zarovnaná se součtem stav + alias
                        client.println(F(".onewire-table td:nth-child(1) { width: 100px; }"));  // Sensor
                        client.println(F(".onewire-table td:nth-child(2) { width: 120px; }"));  // SN (širší pro dlouhé adresy)
                        client.println(F(".onewire-table td:nth-child(3) { width: 50px; }"));   // Temp (C)
                        client.println(F(".onewire-table td:nth-child(4) { width: 50px; }"));   // Vad (V) - jen pro DS2438
                        client.println(F(".onewire-table td:nth-child(5) { width: 50px; }"));   // Vsens (V) - jen pro DS2438
                        client.println(F(".onewire-table td:nth-child(6) { width: 30px; }"));  // Alias (širší pro popis)
                        client.println(F(".no-border { border: none; }"));
                        client.println(F("</style>"));
                        client.println(F("<script>"));
                        client.println(F("let firstUpdate = true;"));
                        client.println(F("function updateStatus() {"));
                        client.println(F("  fetch('/status?ts=' + new Date().getTime()).then(response => response.json()).then(data => {"));
                        client.println(F("    console.log('Received data:', data);"));
                        client.println(F("    try {"));
                        client.println(F("      // Update Relays"));
                        client.print(F("      for (let i = 0; i < "));
                        client.print(numOfRelays);
                        client.println(F("; i++) {"));
                        client.println(F("        let relay = document.getElementById('relay' + (i+1));"));
                        client.println(F("        if (relay) relay.value = data.relays[i] || 0;"));
                        client.println(F("      }"));
                        client.println(F("      // Update HSS and LSS"));
                        client.print(F("      for (let i = 0; i < "));
                        client.print(numOfHSSwitches);
                        client.println(F("; i++) {"));
                        client.println(F("        let hss = document.getElementById('hss' + (i+1));"));
                        client.println(F("        if (hss && data.hss && data.hss[i] !== undefined) {"));
                        client.println(F("          let value = (data.hss[i] === 1) ? '255' : '0';"));
                        client.println(F("          hss.value = value;"));
                        client.println(F("          console.log('HSS ' + (i+1) + ' selectbox set to ' + value + ' (raw bit: ' + data.hss[i] + ')');"));
                        client.println(F("          if (hss.value !== value) console.error('HSS ' + (i+1) + ' value not set correctly');"));
                        client.println(F("        }"));
                        client.println(F("        if (firstUpdate) {"));
                        client.println(F("          let hssPWM = document.getElementById('hssPWM' + (i+1));"));
                        client.println(F("          if (hssPWM && data.hssPWM && data.hssPWM[i] !== undefined) hssPWM.value = data.hssPWM[i] || 0;"));
                        client.println(F("        }"));
                        client.println(F("      }"));
                        client.print(F("      for (let i = 0; i < "));
                        client.print(numOfLSSwitches);
                        client.println(F("; i++) {"));
                        client.println(F("        let lss = document.getElementById('lss' + (i+1));"));
                        client.println(F("        if (lss && data.lss && data.lss[i] !== undefined) {"));
                        client.println(F("          let value = (data.lss[i] === 1) ? '255' : '0';"));
                        client.println(F("          lss.value = value;"));
                        client.println(F("          console.log('LSS ' + (i+1) + ' selectbox set to ' + value + ' (raw bit: ' + data.lss[i] + ')');"));
                        client.println(F("          if (lss.value !== value) console.error('LSS ' + (i+1) + ' value not set correctly');"));
                        client.println(F("        }"));
                        client.println(F("        if (firstUpdate) {"));
                        client.println(F("          let lssPWM = document.getElementById('lssPWM' + (i+1));"));
                        client.println(F("          if (lssPWM && data.lssPWM && data.lssPWM[i] !== undefined) lssPWM.value = data.lssPWM[i] || 0;"));
                        client.println(F("          if (i === 3 && lssPWM) lssPWM.disabled = true;"));
                        client.println(F("        }"));
                        client.println(F("      }"));
                        client.println(F("      // Update Digital Inputs"));
                        client.println(F("      for (let i = 0; i < data.digInputs.length; i++) {"));
                        client.println(F("        let statusElem = document.getElementById('di_status' + (i+1));"));
                        client.println(F("        if (statusElem) {"));
                        client.println(F("          let statusText = data.digInputs[i];"));
                        client.println(F("          if (data.pulseOn && (i === 9 || i === 10 || i === 11)) {"));
                        client.println(F("            statusText += ' (count: ' + data.pulseCounts[i-9] + ')';"));
                        client.println(F("          }"));
                        client.println(F("          statusElem.textContent = statusText;"));
                        client.println(F("        } else { console.error('No di_status' + (i+1) + ' element found'); }"));
                        client.println(F("      }"));
                        client.println(F("      // Analog Inputs"));
                        client.println(F("      for (let i = 0; i < data.anaInputs.length; i++) {"));
                        client.println(F("        let statusElem = document.getElementById('ai_status' + (i+1));"));
                        client.println(F("        if (statusElem) {"));
                        client.println(F("          statusElem.textContent = data.anaInputs[i] + ' (' + data.anaInputsVoltage[i].toFixed(1) + 'V)';"));
                        client.println(F("        } else { console.error('No ai_status' + (i+1) + ' element found'); }"));
                        client.println(F("      }"));
                        client.println(F("      // Update DS2438 Sensors"));
                        client.println(F("      for (let i = 0; i < data.ds2438.length; i++) {"));
                        client.println(F("        let snElem = document.getElementById('ds2438_sn' + (i+1));"));
                        client.println(F("        let tempElem = document.getElementById('ds2438_temp' + (i+1));"));
                        client.println(F("        let vadElem = document.getElementById('ds2438_vad' + (i+1));"));
                        client.println(F("        let vsensElem = document.getElementById('ds2438_vsens' + (i+1));"));
                        client.println(F("        if (snElem && tempElem && vadElem && vsensElem) {"));
                        client.println(F("          snElem.textContent = data.ds2438[i].sn;"));
                        client.println(F("          tempElem.textContent = data.ds2438[i].temp + ' C';"));
                        client.println(F("          vadElem.textContent = data.ds2438[i].vad + ' V';"));
                        client.println(F("          vsensElem.textContent = data.ds2438[i].vsens + ' V';"));
                        client.println(F("        } else { console.error('DS2438 element(s) missing for index ' + (i+1)); }"));
                        client.println(F("      }"));
                        client.println(F("      // Update DS18B20 Sensors"));
                        client.println(F("      for (let i = 0; i < data.ds18b20.length; i++) {"));
                        client.println(F("        let snElem = document.getElementById('ds18b20_sn' + (i+1));"));
                        client.println(F("        let tempElem = document.getElementById('ds18b20_temp' + (i+1));"));
                        client.println(F("        if (snElem && tempElem) {"));
                        client.println(F("          snElem.textContent = data.ds18b20[i].sn;"));
                        client.println(F("          tempElem.textContent = data.ds18b20[i].temp + ' C';"));
                        client.println(F("        } else { console.error('DS18B20 element(s) missing for index ' + (i+1)); }"));
                        client.println(F("      }"));
                        client.println(F("      if (firstUpdate) {"));
                        client.println(F("        let oneWireCycle = document.getElementById('oneWireCycle');"));
                        client.println(F("        let anaInputCycle = document.getElementById('anaInputCycle');"));
                        client.println(F("        let pulseOn = document.getElementById('pulseOn');"));
                        client.println(F("        let pulseSendCycle = document.getElementById('pulseSendCycle');"));
                        client.println(F("        if (oneWireCycle) oneWireCycle.value = data.oneWireCycle || 1000;"));
                        client.println(F("        else console.error('No oneWireCycle element found');"));
                        client.println(F("        if (anaInputCycle) anaInputCycle.value = data.anaInputCycle || 1000;"));
                        client.println(F("        else console.error('No anaInputCycle element found');"));
                        client.println(F("        if (pulseOn) pulseOn.value = data.pulseOn.toString();"));
                        client.println(F("        else console.error('No pulseOn element found');"));
                        client.println(F("        if (pulseSendCycle) pulseSendCycle.value = data.pulseSendCycle || 2000;"));
                        client.println(F("        else console.error('No pulseSendCycle element found');"));
                        client.print(F("        for (let i = 0; i < "));
                        client.print(numOfAnaOuts);
                        client.println(F("; i++) {"));
                        client.println(F("          let anaOut = document.getElementById('anaOut' + (i+1));"));
                        client.println(F("          if (anaOut) anaOut.value = data.anaOuts[i] || 0;"));
                        client.println(F("          let anaOutVoltage = document.getElementById('anaOutVoltage' + (i+1));"));
                        client.println(F("          if (anaOutVoltage) anaOutVoltage.textContent = '(' + data.anaOutsVoltage[i].toFixed(1) + 'V)';"));
                        client.println(F("        }"));
                        client.println(F("        // Nastavení aliasů pro relays"));
                        client.print(F("        for (let i = 0; i < "));
                        client.print(numOfRelays);
                        client.println(F("; i++) {"));
                        client.println(F("          let aliasInput = document.getElementById('alias_relay' + (i+1));"));
                        client.println(F("          if (aliasInput) aliasInput.value = data.alias_relays[i] || '';"));
                        client.println(F("        }"));
                        client.println(F("        // Pro HSS"));
                        client.print(F("        for (let i = 0; i < "));
                        client.print(numOfHSSwitches);
                        client.println(F("; i++) {"));
                        client.println(F("          let aliasInput = document.getElementById('alias_hss' + (i+1));"));
                        client.println(F("          if (aliasInput) aliasInput.value = data.alias_hss[i] || '';"));
                        client.println(F("        }"));
                        client.println(F("        // Pro LSS"));
                        client.print(F("        for (let i = 0; i < "));
                        client.print(numOfLSSwitches);
                        client.println(F("; i++) {"));
                        client.println(F("          let aliasInput = document.getElementById('alias_lss' + (i+1));"));
                        client.println(F("          if (aliasInput) aliasInput.value = data.alias_lss[i] || '';"));
                        client.println(F("        }"));
                        client.println(F("        // Pro analog outputs"));
                        client.print(F("        for (let i = 0; i < "));
                        client.print(numOfAnaOuts);
                        client.println(F("; i++) {"));
                        client.println(F("          let aliasInput = document.getElementById('alias_ao' + (i+1));"));
                        client.println(F("          if (aliasInput) aliasInput.value = data.alias_anaOuts[i] || '';"));
                        client.println(F("        }"));
                       client.println(F("        // Pro DS2438"));
                        client.println(F("        for (let i = 0; i < data.alias_ds2438.length; i++) {"));
                        client.println(F("          let aliasInput = document.getElementById('alias_ds2438_' + (i+1));"));
                        client.println(F("          if (aliasInput) aliasInput.value = data.alias_ds2438[i] || '';"));
                        client.println(F("        }"));
                        client.println(F("        // Pro DS18B20"));
                        client.println(F("        for (let i = 0; i < data.alias_ds18b20.length; i++) {"));
                        client.println(F("          let aliasInput = document.getElementById('alias_ds18b20_' + (i+1));"));
                        client.println(F("          if (aliasInput) aliasInput.value = data.alias_ds18b20[i] || '';"));
                        client.println(F("        }"));
                        client.println(F("        let debugEnabled = document.getElementById('debugEnabled');"));
                        client.println(F("        if (debugEnabled) debugEnabled.value = data.debugEnabled.toString();"));
                        client.println(F("        else console.error('No debugEnabled element found');"));
                        client.println(F("        // Pro Digital Inputs"));
                        client.println(F("        for (let i = 0; i < data.alias_digInputs.length; i++) {"));
                        client.println(F("          let aliasInput = document.getElementById('alias_di' + (i+1));"));
                        client.println(F("          if (aliasInput) aliasInput.value = data.alias_digInputs[i] || '';"));
                        client.println(F("        }"));
                        client.println(F("        // Pro Analog Inputs"));
                        client.println(F("        for (let i = 0; i < data.alias_anaInputs.length; i++) {"));
                        client.println(F("          let aliasInput = document.getElementById('alias_ai' + (i+1));"));
                        client.println(F("          if (aliasInput) aliasInput.value = data.alias_anaInputs[i] || '';"));
                        client.println(F("        }"));
                        client.println(F("        let serialNumberInput = document.getElementById('serialNumber');"));
                        client.println(F("        if (serialNumberInput) serialNumberInput.value = data.serialNumber || '';"));
                        client.println(F("        else console.error('No serialNumber element found');"));
                        client.println(F("        if (serialNumberInput) serialNumberInput.readOnly = data.serialLocked;"));
                        client.println(F("        if (serialNumberInput) {"));
                        client.println(F("          if (data.serialLocked) {"));
                        client.println(F("            serialNumberInput.classList.add('no-border');"));
                        client.println(F("          } else {"));
                        client.println(F("            serialNumberInput.classList.remove('no-border');"));
                        client.println(F("          }"));
                        client.println(F("        }"));
                        client.println(F("        firstUpdate = false;"));
                        client.println(F("      }"));
                        client.println(F("    } catch (e) { console.error('Error in updateStatus:', e); }"));
                        client.println(F("  }).catch(err => console.error('Fetch error:', err));"));
                        client.println(F("}"));
                        client.println(F("function sendCommand(param, value) {"));
                        client.println(F("  fetch('/command?' + param + '=' + value).then(response => response.text()).then(data => {"));
                        client.println(F("    console.log('Command response: ' + data);"));
                        client.println(F("    updateStatus();"));
                        client.println(F("  }).catch(err => console.error('Command fetch error:', err));"));
                        client.println(F("}"));
                        client.println(F("setInterval(updateStatus, 1000);"));
                        client.println(F("document.addEventListener(\"DOMContentLoaded\", updateStatus);"));
                        client.println(F("</script>"));
                        client.println(F("</head>"));
                        client.println(F("<body>"));
                        client.println(F("<div class='reset-button'>"));
                        client.println(F("<input type='button' onclick=\"window.location.href='/info'\" value='View Docs'>"));
                        client.println(F("<input type='button' value='Reboot' onclick='if(confirm(\"Po stisku tlačítka reset dojde k resetu modulu. Pokračovat?\")) sendCommand(\"reset\", 1)'>"));
                        client.println(F("</div>"));
                        client.println(F("<h2>Railduino Control Panel</h2>"));

                        // First row: Ethernet Info and Cycle Times side by side
                        client.println(F("<div style='border: 1px solid #ccc; margin-bottom: 10px;'>"));
                        client.println(F("<table class='outer'>"));
                        client.println(F("<tr>"));
                        client.println(F("<td width='50%'>"));
                        client.println(F("<h3>Basic Information</h3>"));
                        client.println(F("<table class='inner'>"));
                        client.println(F("<tr><td>DIP Switch Address</td><td>"));
                        client.print(boardAddress);
                        client.println(F("</td></tr>"));
                        client.println(F("<tr><td>IP Address (DHCP)</td><td>"));
                        client.print(Ethernet.localIP());
                        client.println(F("</td></tr>"));
                        client.println(F("<tr><td>Gateway</td><td>"));
                        client.print(Ethernet.gatewayIP());
                        client.println(F("</td></tr>"));
                        client.println(F("<tr><td>MAC Address</td><td>"));
                        for (int i = 0; i < 6; i++) {
                            if (mac[i] < 16) client.print(F("0"));
                            client.print(mac[i], HEX);
                            if (i < 5) client.print(F(":"));
                        }
                        client.println(F("</td></tr>"));
                        client.println(F("<tr><td>Serial Number</td><td><input type='text' id='serialNumber' maxlength='20' onchange='sendCommand(\"serialNumber\", this.value)' placeholder='..serial number' padding=0></td></tr>"));
                        client.println(F("<tr><td>Verze Hardware</td><td>"));
                        client.print(ver,1);
                        client.println(F("</td></tr>"));
                        client.println(F("</table>"));
                        client.println(F("</td>"));
                        client.println(F("<td width='50%'>"));
                        client.println(F("<h3>Other settings</h3>"));
                        client.println(F("<table class='inner'>"));
                        client.println(F("<tr><td>1-Wire Cycle</td><td><input type='number' id='oneWireCycle' min='1000' style='width: 100px;' onchange='sendCommand(\"oneWireCycle\", this.value)'> ms</td></tr>"));
                        client.println(F("<tr><td>Analog Input Cycle</td><td><input type='number' id='anaInputCycle' min='1000' style='width: 100px;' onchange='sendCommand(\"anaInputCycle\", this.value)'> ms</td></tr>"));
                        client.println(F("<tr><td>Pulses Send Cycle</td><td><input type='number' id='pulseSendCycle' min='1000' style='width: 100px;' onchange='sendCommand(\"pulseSendCycle\", this.value)'> ms</td></tr>"));
                        client.println(F("<tr><td>Pulses Sensing (DI 10,11,12)</td><td><select id='pulseOn' onchange='sendCommand(\"pulseOn\", this.value)'><option value='0' selected>Off</option><option value='1'>On</option></select></td></tr>"));
                        client.println(F("<tr><td>Serial Debug Output (115200 Bd)</td><td><select id='debugEnabled' onchange='sendCommand(\"debugEnabled\", this.value)'><option value='0' selected>Off</option><option value='1'>On</option></select></td></tr>"));
                        client.println(F("</table>"));
                        client.println(F("</td>"));
                        client.println(F("</tr>"));
                        client.println(F("</table>"));
                        client.println(F("</div>"));

                        // Second row: Relay Status and HSS/LSS side by side
                        client.println(F("<div style='border: 1px solid #ccc; margin-bottom: 10px;'>"));
                        client.println(F("<table class='outer'>"));
                        client.println(F("<tr>"));
                        client.println(F("<td width='50%'>"));
                        client.println(F("<h3>Relay Status</h3>"));
                        client.println(F("<table class='inner'>"));
                        for (int i = 0; i < numOfRelays; i++) {
                            client.print(F("<tr><td>Relay "));
                            client.print(i + 1);
                            client.print(F("</td><td><input type=\"checkbox\" id=\"relay"));
                            client.print(i + 1);
                            client.print(F("\" onchange=\"fetch('/command?relay"));
                            client.print(i + 1);
                            client.print(F("=' + (this.checked ? 1 : 0))\""));
                            byte byteNo = (i < 8) ? relOut1Byte : relOut2Byte;
                            byte bitPos = (i < 8) ? i : i - 8;
                            if (bitRead(Mb.MbData[byteNo], bitPos)) {
                                client.print(F(" checked"));
                            }
                            client.print(F("></td><td><input type=\"text\" name=\"alias_relay"));
                            client.print(i + 1);
                            client.print(F("\" placeholder=\"...description...\" value=\""));
                            client.print(aliasRelays[i]);
                            client.print(F("\"></td></tr>"));
                        }
                        client.println(F("</table>"));
                        client.println(F("</td>"));
                        client.println(F("<td width='50%'>"));
                        client.println(F("<table class='inner'>"));
                        client.println(F("<tr><td>"));
                        client.println(F("<h3>High-Side Switches Status (0-255 = 0-V+)</h3>"));
                        client.println(F("<table class='inner'>"));
                        for (int i = 0; i < numOfHSSwitches; i++) {
                            client.print(F("<tr><td>HSS "));
                            client.print(i + 1);
                            client.print(F("</td><td><input type=\"checkbox\" id=\"hss"));
                            client.print(i + 1);
                            client.print(F("\" onchange=\"fetch('/command?hss"));
                            client.print(i + 1);
                            client.print(F("=' + (this.checked ? 1 : 0)); document.getElementById('hssPWM"));
                            client.print(i + 1);
                            client.print(F("').value = this.checked ? 255 : 0;\"></td>"));
                            if (bitRead(Mb.MbData[hssLssByte], i)) {
                                client.print(F(" checked"));
                            }
                            client.print(F("<td><input type='number' id='hssPWM"));
                            client.print(i + 1);
                            client.print(F("' min='0' max='255' style='width: 50px;' onchange='sendCommand(\"hss"));
                            client.print(i + 1);
                            client.print(F("\", this.value)'></td>"));
                            client.print(F("<td><input type='text' id='alias_hss"));
                            client.print(i + 1);
                            client.print(F("' maxlength='20' onchange='sendCommand(\"alias_hss"));
                            client.print(i + 1);
                            client.print(F("\", this.value)' placeholder='...description...'></td></tr>"));
                        }
                        client.println(F("</table>"));
                        client.println(F("</td></tr>"));
                        client.println(F("<tr><td>"));
                        client.println(F("<h3>Low-Side Switches Status (0-255 = 0-V+)</h3>"));
                        client.println(F("<table class='inner'>"));
                        for (int i = 0; i < numOfLSSwitches; i++) {
                            client.print(F("<tr><td>LSS "));
                            client.print(i + 1);
                            client.print(F("</td><td><input type=\"checkbox\" id=\"lss"));
                            client.print(i + 1);
                            client.print(F("\" onchange=\"fetch('/command?lss"));
                            client.print(i + 1);
                            client.print(F("=' + (this.checked ? 1 : 0)); document.getElementById('lssPWM"));
                            client.print(i + 1);
                            client.print(F("').value = this.checked ? 255 : 0;\"></td>"));
                            if (bitRead(Mb.MbData[hssLssByte], i + numOfHSSwitches)) {
                                client.print(F(" checked"));
                            }
                            client.print(F("<td><input type='number' id='lssPWM"));
                            client.print(i + 1);
                            client.print(F("' min='0' max='255' style='width: 50px;' onchange='sendCommand(\"lss"));
                            client.print(i + 1);
                            client.print(F("\", this.value)'"));
                            if (i == 3) client.print(F(" disabled")); // Zakázat PWM pro LSS4 (pin 18)
                            client.print(F("></td>"));
                            client.print(F("<td><input type='text' id='alias_lss"));
                            client.print(i + 1);
                            client.print(F("' maxlength='20' onchange='sendCommand(\"alias_lss"));
                            client.print(i + 1);
                            client.print(F("\", this.value)' placeholder='...description...'></td></tr>"));
                        }
                        client.println(F("</table>"));
                        client.println(F("</td></tr>"));
                        client.println(F("</table>"));
                        client.println(F("</td>"));
                        client.println(F("</tr>"));
                        client.println(F("</table>"));
                        client.println(F("</div>"));

                        // Third row: Digital Inputs (2x12) side by side
                        client.println(F("<div style='border: 1px solid #ccc; margin-bottom: 10px;'>"));
                        client.println(F("<table class='outer'>"));
                        client.println(F("<tr>"));
                        client.println(F("<td width='50%'>"));
                        client.println(F("<h3 class='dig-inputs-title'>Digital Inputs 1-12</h3>"));
                        client.println(F("<table class='inner' id='digInputsTable1'>"));
                        client.println(F("<tbody>"));
                        for (int i = 0; i < 12; i++) {
                            client.print(F("<tr><td>DI"));
                            client.print(i + 1);
                            client.print(F("</td><td id='di_status"));
                            client.print(i + 1);
                            client.print(F("'></td><td><input type='text' id='alias_di"));
                            client.print(i + 1);
                            client.print(F("' maxlength='20' placeholder='...description...' onchange='sendCommand(\"alias_di"));
                            client.print(i + 1);
                            client.print(F("\", this.value)'></td></tr>"));
                        }
                        client.println(F("</tbody>"));
                        client.println(F("</table>"));
                        client.println(F("</td>"));
                        client.println(F("<td width='50%'>"));
                        client.println(F("<h3 class='dig-inputs-title'>Digital Inputs 13-24</h3>"));
                        client.println(F("<table class='inner' id='digInputsTable2'>"));
                        client.println(F("<tbody>"));
                        for (int i = 12; i < 24; i++) {
                            client.print(F("<tr><td>DI"));
                            client.print(i + 1);
                            client.print(F("</td><td id='di_status"));
                            client.print(i + 1);
                            client.print(F("'></td><td><input type='text' id='alias_di"));
                            client.print(i + 1);
                            client.print(F("' maxlength='20' placeholder='...description...' onchange='sendCommand(\"alias_di"));
                            client.print(i + 1);
                            client.print(F("\", this.value)'></td></tr>"));
                        }
                        client.println(F("</tbody>"));
                        client.println(F("</table>"));
                        client.println(F("</td>"));
                        client.println(F("</tr>"));
                        client.println(F("</table>"));
                        client.println(F("</div>"));

                        // Fourth row: Analog Inputs and Outputs side by side
                        client.println(F("<div style='border: 1px solid #ccc; margin-bottom: 10px;'>"));
                        client.println(F("<table class='outer'>"));
                        client.println(F("<tr>"));
                        client.println(F("<td width='50%'>"));
                        client.println(F("<h3>Analog Inputs (0-1024 = 0-10V)</h3>"));
                        client.println(F("<table class='inner' id='anaInputsTable'>"));
                        client.println(F("<tbody>"));
                        for (int i = 0; i < numOfAnaInputs; i++) {
                            client.print(F("<tr><td>AI"));
                            client.print(i + 1);
                            client.print(F("</td><td id='ai_status"));
                            client.print(i + 1);
                            client.print(F("'></td><td><input type='text' id='alias_ai"));
                            client.print(i + 1);
                            client.print(F("' maxlength='20' placeholder='...description...' onchange='sendCommand(\"alias_ai"));
                            client.print(i + 1);
                            client.print(F("\", this.value)'></td></tr>"));
                        }
                        client.println(F("</tbody>"));
                        client.println(F("</table>"));
                        client.println(F("</td>"));
                        client.println(F("<td width='50%'>"));
                        client.println(F("<h3>Analog Outputs (0-255 = 0-10V)</h3>"));
                        client.println(F("<table class='inner'>"));
                        for (int i = 0; i < numOfAnaOuts; i++) {
                            client.print(F("<tr><td>AO"));
                            client.print(i + 1);
                            client.print(F("</td><td><input type='number' id='anaOut"));
                            client.print(i + 1);
                            client.print(F("' min='0' max='255' onchange='sendCommand(\"anaOut"));
                            client.print(i + 1);
                            client.print(F("\", this.value)'></td>"));
                            client.print(F("<td id='anaOutVoltage"));
                            client.print(i + 1);
                            client.print(F("'></td>"));  // Nový sloupec pro voltage
                            client.print(F("<td><input type='text' id='alias_ao"));
                            client.print(i + 1);
                            client.print(F("' maxlength='20' placeholder='...description...' onchange='sendCommand(\"alias_ao"));
                            client.print(i + 1);
                            client.print(F("\", this.value)'></td></tr>"));
                        }
                        client.println(F("</table>"));
                        client.println(F("</td>"));
                        client.println(F("</tr>"));
                        client.println(F("</table>"));
                        client.println(F("</div>"));

                        // Fifth row: 1-Wire Sensors DS2438 and DS18B20 side by side
                        client.println(F("<div style='border: 1px solid #ccc; margin-bottom: 10px;'>"));
                        client.println(F("<table class='outer'>"));
                        client.println(F("<tr>"));
                        client.println(F("<td width='50%'>"));
                        client.println(F("<h3>1-Wire Sensors DS2438</h3>"));
                        client.println(F("<table class='inner onewire-table' id='ds2438Table'>"));
                        client.println(F("<tbody>"));
                        for (int i = 0; i < DS2438count; i++) {
                            client.print(F("<tr><td>DS2438-"));
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
                            client.print(F("'></td><td><input type='text' id='alias_ds2438_"));
                            client.print(i + 1);
                            client.print(F("' maxlength='20' placeholder='...description...' onchange='sendCommand(\"alias_ds2438_"));
                            client.print(i + 1);
                            client.print(F("\", this.value)' value='"));
                            client.print(aliasDS2438[i]);
                            client.print(F("'></td></tr>"));
                        }
                        client.println(F("</tbody>"));
                        client.println(F("</table>"));
                        client.println(F("</td>"));
                        client.println(F("<td width='50%'>"));
                        client.println(F("<h3>1-Wire Sensors DS18B20</h3>"));
                        client.println(F("<table class='inner onewire-table' id='ds18b20Table'>"));
                        client.println(F("<tbody>"));
                        for (int i = 0; i < DS18B20count; i++) {
                            client.print(F("<tr><td>DS18B20-"));
                            client.print(i + 1);
                            client.print(F("</td><td id='ds18b20_sn"));
                            client.print(i + 1);
                            client.print(F("'>"));
                            client.print(oneWireAddressToString(sensors18B20[i]));
                            client.print(F("</td><td id='ds18b20_temp"));
                            client.print(i + 1);
                            client.print(F("'></td><td><input type='text' id='alias_ds18b20_"));
                            client.print(i + 1);
                            client.print(F("' maxlength='20' placeholder='...description...' onchange='sendCommand(\"alias_ds18b20_"));
                            client.print(i + 1);
                            client.print(F("\", this.value)' value='"));
                            client.print(aliasDS18B20[i]);
                            client.print(F("'></td></tr>"));
                        }
                        client.println(F("</tbody>"));
                        client.println(F("</table>"));
                        client.println(F("</td>"));
                        client.println(F("</tr>"));
                        client.println(F("</table>"));
                        client.println(F("</div>"));

                        client.println(F("</body></html>"));
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
        digStatCommand[i] = digInputStr + String(i + 1, DEC); // Command for input status
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
        setHSSwitch(i, 0); // Turn off HSS
    }

    // Initialize Low-Side switches
    for (int i = 0; i < numOfLSSwitches; i++) {
        pinMode(LSSwitchPins[i], OUTPUT);
        LSSwitchOnCommands[i] = LSSwitchStr + String(i + 1, DEC) + " on"; // Command for turning on
        LSSwitchOffCommands[i] = LSSwitchStr + String(i + 1, DEC) + " off"; // Command for turning off
        setLSSwitch(i, 0); // Turn off LSS
    }

    for (int i = 0; i < numOfHSSwitches; i++) {
        HSSwitchPWMCommands[i] = HSSwitchStr + String(i + 1, DEC) + "_pwm"; // Např. "ho1_pwm"
        }
    
    for (int i = 0; i < numOfLSSwitches; i++) {
        LSSwitchPWMCommands[i] = LSSwitchStr + String(i + 1, DEC) + "_pwm"; // Např. "lo1_pwm"
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

    EEPROM.get(EEPROM_SERIALNUMBER, serialNumber);
    serialNumber[20] = '\0';
    if (!isValidAlias(serialNumber)) {
        strcpy(serialNumber, "");
    }

    EEPROM.get(EEPROM_SERIALLOCK, serialLocked);
    if (serialLocked != 0 && serialLocked != 1) {
        serialLocked = false;
        EEPROM.put(EEPROM_SERIALLOCK, serialLocked);
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
        oneWireCycle = 1000; // Výchozí hodnota
    }
    oneWireTimer.sleep(oneWireCycle);

    EEPROM.get(EEPROM_ANAINPUT, anaInputCycle);
    if (anaInputCycle < 1000 || anaInputCycle > 60000) {
        anaInputCycle = 1000; // Výchozí hodnota
    }
    analogTimer.sleep(anaInputCycle);

    EEPROM.get(EEPROM_PULSEON, pulseOn);
    if (pulseOn != 0 && pulseOn != 1) {
        pulseOn = false;
        EEPROM.put(EEPROM_PULSEON, pulseOn);
    }
    EEPROM.get(EEPROM_PULSECYCLE, pulseSendCycle);
    if (pulseSendCycle < 1000 || pulseSendCycle > 60000) {
        pulseSendCycle = 2000; // Default value
    }

    // Načtení aliasů z EEPROM s kontrolou platnosti
    for (int i = 0; i < numOfRelays; i++) {
        EEPROM.get(EEPROM_ALIAS_RELAYS + (i * 21), aliasRelays[i]);
        aliasRelays[i][20] = '\0';  // Vždy ukončit null terminátorem
        if (!isValidAlias(aliasRelays[i])) {
            strcpy(aliasRelays[i], "");  // Neplatné: Nastav na prázdné
        }
    }
    // Podobně pro ostatní sekce (HSS, LSS, DigInputs, AnaInputs, AnaOuts) – opakujte smyčku s příslušnými proměnnými a adresami
    for (int i = 0; i < numOfHSSwitches; i++) {
        EEPROM.get(EEPROM_ALIAS_HSS + (i * 21), aliasHSS[i]);
        aliasHSS[i][20] = '\0';
        if (!isValidAlias(aliasHSS[i])) {
            strcpy(aliasHSS[i], "");
        }
    }
    for (int i = 0; i < numOfLSSwitches; i++) {
        EEPROM.get(EEPROM_ALIAS_LSS + (i * 21), aliasLSS[i]);
        aliasLSS[i][20] = '\0';
        if (!isValidAlias(aliasLSS[i])) {
            strcpy(aliasLSS[i], "");
        }
    }
    for (int i = 0; i < numOfDigInputs; i++) {
        EEPROM.get(EEPROM_ALIAS_DIGINPUTS + (i * 21), aliasDigInputs[i]);
        aliasDigInputs[i][20] = '\0';
        if (!isValidAlias(aliasDigInputs[i])) {
            strcpy(aliasDigInputs[i], "");
        }
    }
    for (int i = 0; i < numOfAnaInputs; i++) {
        EEPROM.get(EEPROM_ALIAS_ANA_INPUTS + (i * 21), aliasAnaInputs[i]);
        aliasAnaInputs[i][20] = '\0';
        if (!isValidAlias(aliasAnaInputs[i])) {
            strcpy(aliasAnaInputs[i], "");
        }
    }
    for (int i = 0; i < numOfAnaOuts; i++) {
        EEPROM.get(EEPROM_ALIAS_ANA_OUTS + (i * 21), aliasAnaOuts[i]);
        aliasAnaOuts[i][20] = '\0';
        if (!isValidAlias(aliasAnaOuts[i])) {
            strcpy(aliasAnaOuts[i], "");
        }
    }

    for (int i = 0; i < maxSensors; i++) {
        EEPROM.get(EEPROM_ALIAS_DS2438 + (i * 21), aliasDS2438[i]);
        aliasDS2438[i][20] = '\0';
        if (!isValidAlias(aliasDS2438[i])) {
            strcpy(aliasDS2438[i], "");
        }
    }
    for (int i = 0; i < maxSensors; i++) {
        EEPROM.get(EEPROM_ALIAS_DS18B20 + (i * 21), aliasDS18B20[i]);
        aliasDS18B20[i][20] = '\0';
        if (!isValidAlias(aliasDS18B20[i])) {
            strcpy(aliasDS18B20[i], "");
        }
    }

    // Inicializační flag pro aliasy (hodnota 0xAA znamená inicializováno)
    byte initFlag = 0;
    EEPROM.get(EEPROM_INIT_FLAG, initFlag);

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
        // Nastav flag na inicializováno
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
    if (len > 20) return false;
    for (int j = 0; j < len; j++) {
        if (alias[j] < 32 || alias[j] > 126) {  // Neprintable znaky
            return false;
        }
    }
    return true;
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
        if (pulseOn && (i == 9 || i == 10 || i == 11)) {
    curValue = 1;
}
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
            Mb.MbData[i+8] = (int)value;
            sendAnaInput(i+1, Mb.MbData[i+8]);
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