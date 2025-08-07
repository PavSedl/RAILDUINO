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
  low side switch on command:     rail1 lo1 on
  low side switch off command:    rail1 lo2 off
  low side switch pwm command:    rail1 lo1_pwm 180
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
2                          low side switch outputs 1-4
3                          LSS PWM value 1 (0-255)
4                          LSS PWM value 2 (0-255)
5                          LSS PWM value 3 (0-255)
6                          LSS PWM value 4 (0-255)
7                          digital inputs 1-8
8                          digital inputs 9-16
9                          digital inputs 17-24
10                         analog input 1           0-1023
11                         analog input 2           0-1023
12                         analog input 3           0-1023
13                         service - reset (bit 0)
14                         1st DS2438 Temp (value multiplied by 100)
15                         1st DS2438 Vad (value multiplied by 100)
16                         1st DS2438 Vsens (value multiplied by 100)
-                         
39                         DS2438 values (up to 10 sensors)
40-50                      DS18B20 Temperature (up to 10 sensors) (value multiplied by 100)
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
#define ver 1.3

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
#define EEPROM_WEBUPDATE 16 // Nová adresa pro webUpdateCycle
#define EEPROM_DEBUG 20 // Posunuto kvůli webUpdateCycle
#define EEPROM_SERIALNUMBER 24 // Posunuto
#define EEPROM_SERIALLOCK 44 // Posunuto
#define EEPROM_INIT_FLAG 45 // Posunuto
#define EEPROM_ALIAS_START 46 // Posunuto
#define EEPROM_ALIAS_RELAYS EEPROM_ALIAS_START  // 12 × 21 = 252 bajtů
#define EEPROM_ALIAS_LSS (EEPROM_ALIAS_RELAYS + (numOfRelays * 21))  // 4 × 21 = 84
#define EEPROM_ALIAS_DIGINPUTS (EEPROM_ALIAS_LSS + (numOfLSSwitches * 21))  // 24 × 21 = 504
#define EEPROM_ALIAS_ANA_INPUTS (EEPROM_ALIAS_DIGINPUTS + (numOfDigInputs * 21))  // 2 × 21 = 42
#define EEPROM_ALIAS_DS2438 (EEPROM_ALIAS_ANA_INPUTS + (numOfAnaInputs * 21))  // 10 × 21 = 210 bajtů
#define EEPROM_ALIAS_DS18B20 (EEPROM_ALIAS_DS2438 + (maxSensors * 21))     // Dalších 10 × 21 = 210 bajtů

// MODBUS register definitions
#define relOut1Byte 0   // Relay outputs 1-8
#define relOut2Byte 1   // Relay outputs 9-12
#define lssByte 2       // LSS outputs (on/off, bits 0-3)
#define lssPWM1Byte 3   // PWM for LSS1
#define lssPWM2Byte 4   // PWM for LSS2
#define lssPWM3Byte 5   // PWM for LSS3
#define lssPWM4Byte 6   // PWM for LSS4
#define digInp1Byte 7   // Digital inputs 1-8
#define digInp2Byte 8   // Digital inputs 9-16
#define digInp3Byte 9   // Digital inputs 17-24
#define anaInp1Byte 10  // Analog input 1
#define anaInp2Byte 11  // Analog input 2
#define anaInp3Byte 12  // Analog input 3 (new for version 1.3)
#define serviceByte 13  // Service register (reset)
#define oneWireTempByte 14  // DS2438 temperature
#define oneWireVadByte 15   // DS2438 Vad voltage
#define oneWireVsensByte 16 // DS2438 Vsens voltage
#define oneWireDS18B20Byte 40 // DS18B20 temperature
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
unsigned long webUpdateCycle = 2000;  // Nový cyklus pro aktualizaci webserveru (ms, výchozí 1000)
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
#define serial3TxControl 8  // Pin for RS485 transmission control
#define numOfDipSwitchPins 4
#define numOfLedPins 1
#define numOfDigInputs 24
#define numOfRelays 12
#define numOfLSSwitches 4  // LSS nahrazují analogové výstupy
#define numOfAnaInputs 3
#define maxSensors 10      // Maximum number of sensors

// Pin configuration for relays, LSS, analog inputs, and LEDs
int relayPins[numOfRelays] = {39, 41, 43, 45, 47, 49, 23, 25, 27, 29, 31, 33}; // Relay pins
int LSSwitchPins[numOfLSSwitches] = {11, 13, 12, 7}; // LSS pins
float lssVoltage[numOfLSSwitches]; // Voltage array for LSS
int analogPins[numOfAnaInputs] = {58, 59, 62}; // Analog input pins
float analogStatus[numOfAnaInputs]; // Analog input status
float anaInputsVoltage[numOfAnaInputs];
int inputPins[numOfDigInputs] = {36, 34, 48, 46, 69, 68, 67, 66, 44, 42, 40, 38, 6, 5, 3, 2, 14, 15, 16, 17, 24, 26, 28, 30}; // Digital input pins
int inputStatus[numOfDigInputs]; // Current digital input status
int inputStatusNew[numOfDigInputs]; // New digital input status for debouncing
int inputChangeTimestamp[numOfDigInputs]; // Timestamp for input state changes
int dipSwitchPins[numOfDipSwitchPins] = {54, 55, 56, 57}; // DIP switch pins
int ledPins[numOfLedPins] = {32}; // LED pins

// Aliasy pro vstupy/výstupy (max 20 znaků + null terminator)
char aliasRelays[numOfRelays][21];
char aliasLSS[numOfLSSwitches][21];
char aliasDigInputs[numOfDigInputs][21];
char aliasAnaInputs[numOfAnaInputs][21];
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
String LSSwitchStr = "lo"; // Prefix for Low-Side switches
String rstStr = "rst"; // Prefix for reset command
String relayOnCommands[numOfRelays]; // Commands for turning relays on
String relayOffCommands[numOfRelays]; // Commands for turning relays off
String LSSwitchOnCommands[numOfLSSwitches]; // Commands for turning LSS on
String LSSwitchOffCommands[numOfLSSwitches]; // Commands for turning LSS off
String LSSwitchPWMCommands[numOfLSSwitches]; // Příkazy pro PWM LSS, např. "lo1_pwm"

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
OneWire ds(9); // 1-Wire bus on pin 62
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
                    // Výpočet napětí pro LSS
                    for (int i = 0; i < numOfLSSwitches; i++) {
                        lssVoltage[i] = (Mb.MbData[lssPWM1Byte + i] / 255.0) * 10.0;
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
                    client.print(F("\"lss\": ["));
                    for (int i = 0; i < numOfLSSwitches; i++) {
                        int bitValue = bitRead(Mb.MbData[lssByte], i);
                        client.print(bitValue);
                        if (i < numOfLSSwitches - 1) client.print(F(","));
                    }
                    client.print(F("],"));
                    client.print(F("\"lssPWM\": ["));
                    for (int i = 0; i < numOfLSSwitches; i++) {
                        client.print(Mb.MbData[lssPWM1Byte + i]);
                        if (i < numOfLSSwitches - 1) client.print(F(","));
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
                    client.print(F(",\"webUpdateCycle\":"));
                    client.print(webUpdateCycle);
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
                    client.print(F(",\"lssVoltage\": ["));
                    for (int i = 0; i < numOfLSSwitches; i++) {
                        client.print(lssVoltage[i], 2);
                        if (i < numOfLSSwitches - 1) client.print(F(","));
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
                    if (request.indexOf("lss") != -1) {
                        for (int i = 0; i < numOfLSSwitches; i++) {
                            String lssParam = "lss" + String(i + 1) + "=";
                            if (request.indexOf(lssParam) != -1) {
                                int startIdx = request.indexOf(lssParam) + lssParam.length();
                                int endIdx = request.indexOf("&", startIdx);
                                if (endIdx == -1) endIdx = request.indexOf(" HTTP");
                                int value = request.substring(startIdx, endIdx).toInt();
                                if (value >= 0 && value <= 255) {
                                    bitWrite(Mb.MbData[lssByte], i, (value > 0) ? 1 : 0);
                                    Mb.MbData[lssPWM1Byte + i] = (value == 1 || value == 0) ? (value == 1 ? 255 : 0) : value;
                                    setLSSwitch(i, Mb.MbData[lssPWM1Byte + i]);
                                    dbg(F("LSS "));
                                    dbg(String(i + 1));
                                    dbg(F(" set to "));
                                    dbgln(String(Mb.MbData[lssPWM1Byte + i]));
                                }
                            }
                        }
                    }
                    if (request.indexOf("oneWireCycle=") != -1) {
                        int startIdx = request.indexOf(F("oneWireCycle=")) + 13;
                        int endIdx = request.indexOf(F("&"), startIdx);
                        if (endIdx == -1) endIdx = request.indexOf(F(" HTTP"));
                        oneWireCycle = request.substring(startIdx, endIdx).toInt();
                        if (oneWireCycle < 5000) oneWireCycle = 5000;
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
                            attachInterrupt(digitalPinToInterrupt(15), [](){pulse1++;}, FALLING);
                            attachInterrupt(digitalPinToInterrupt(16), [](){pulse2++;}, FALLING);
                            attachInterrupt(digitalPinToInterrupt(17), [](){pulse3++;}, FALLING);
                            pulseTimer.sleep(pulseSendCycle);
                        } else {
                            detachInterrupt(digitalPinToInterrupt(15));
                            detachInterrupt(digitalPinToInterrupt(16));
                            detachInterrupt(digitalPinToInterrupt(17));
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
                    if (request.indexOf("webUpdateCycle=") != -1) {
                        int startIdx = request.indexOf(F("webUpdateCycle=")) + 15;
                        int endIdx = request.indexOf(F("&"), startIdx);
                        if (endIdx == -1) endIdx = request.indexOf(F(" HTTP"));
                        webUpdateCycle = request.substring(startIdx, endIdx).toInt();
                        if (webUpdateCycle < 500) webUpdateCycle = 500;
                        if (webUpdateCycle > 10000) webUpdateCycle = 10000;
                        EEPROM.put(EEPROM_WEBUPDATE, webUpdateCycle);
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
                        client.println(F("<head>"));
                        client.println(F("<title>Railduino Control Panel</title>"));
                        client.println(F("<meta charset=\"UTF-8\">"));
                        client.println(F("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">"));
                        client.println(F("<style>"));
                        client.println(F("body { font-family: Arial, sans-serif; margin: 20px; }"));
                        client.println(F("table { border-collapse: collapse; width: 100%; }"));
                        client.println(F("th, td { border: 1px solid black; padding: 8px; text-align: left; }"));
                        client.println(F("th { background-color: #f2f2f2; }"));
                        client.println(F(".container { display: flex; flex-wrap: wrap; }"));
                        client.println(F(".column { flex: 1; min-width: 300px; margin: 10px; }"));
                        client.println(F("</style>"));
                        client.println(F("</head>"));
                        client.println(F("<body>"));
                        client.println(F("<h1>Railduino Control Panel</h1>"));

                        // First row: Ethernet Info and Cycle Times side by side
                        client.println(F("<div class=\"container\">"));
                        client.println(F("<div class=\"column\">"));
                        client.println(F("<h2>Basic Information</h2>"));
                        client.println(F("<table>"));
                        client.println(F("<tr><th>DIP Switch Address</th><td>"));
                        client.print(boardAddress);
                        client.println(F("</td></tr>"));
                        client.println(F("<tr><th>IP Address (DHCP)</th><td>"));
                        client.print(Ethernet.localIP());
                        client.println(F("</td></tr>"));
                        client.println(F("<tr><th>Gateway</th><td>"));
                        client.print(Ethernet.gatewayIP());
                        client.println(F("</td></tr>"));
                        client.println(F("<tr><th>MAC Address</th><td>"));
                        for (int i = 0; i < 6; i++) {
                            if (mac[i] < 16) client.print(F("0"));
                            client.print(mac[i], HEX);
                            if (i < 5) client.print(F(":"));
                        }
                        client.println(F("</td></tr>"));
                        client.println(F("<tr><th>Serial Number</th><td>"));
                        client.println(F("<input type=\"text\" id=\"serialNumber\" value=\""));
                        client.print(serialNumber);
                        client.println(F("\">"));
                        client.println(F("<input type=\"checkbox\" id=\"serialLock\" "));
                        if (serialLocked) client.println(F("checked"));
                        client.println(F("> Lock</td></tr>"));
                        client.println(F("<tr><th>Verze Hardware</th><td>"));
                        client.print(ver, 1);
                        client.println(F("</td></tr>"));
                        client.println(F("</table>"));
                        client.println(F("</div>"));
                        client.println(F("<div class=\"column\">"));
                        client.println(F("<h2>Other settings</h2>"));
                        client.println(F("<table>"));
                        client.println(F("<tr><th>1-Wire Cycle</th><td><input type=\"number\" id=\"oneWireCycle\" value=\""));
                        client.print(oneWireCycle);
                        client.println(F("\"> ms</td></tr>"));
                        client.println(F("<tr><th>Analog Input Cycle</th><td><input type=\"number\" id=\"anaInputCycle\" value=\""));
                        client.print(anaInputCycle);
                        client.println(F("\"> ms</td></tr>"));
                        client.println(F("<tr><th>Pulses Send Cycle</th><td><input type=\"number\" id=\"pulseSendCycle\" value=\""));
                        client.print(pulseSendCycle);
                        client.println(F("\"> ms</td></tr>"));
                        client.println(F("<tr><th>Pulses Sensing (DI18, DI19, DI20)</th><td>"));
                        client.println(F("<input type=\"radio\" name=\"pulseOn\" id=\"pulseOnOff\" value=\"0\" "));
                        if (!pulseOn) client.println(F("checked"));
                        client.println(F("> Off"));
                        client.println(F("<input type=\"radio\" name=\"pulseOn\" id=\"pulseOnOn\" value=\"1\" "));
                        if (pulseOn) client.println(F("checked"));
                        client.println(F("> On</td></tr>"));
                        client.println(F("<tr><th>Serial Debug (115200 Bd)</th><td>"));
                        client.println(F("<input type=\"radio\" name=\"debugEnabled\" id=\"debugEnabledOff\" value=\"0\" "));
                        if (!debugEnabled) client.println(F("checked"));
                        client.println(F("> Off"));
                        client.println(F("<input type=\"radio\" name=\"debugEnabled\" id=\"debugEnabledOn\" value=\"1\" "));
                        if (debugEnabled) client.println(F("checked"));
                        client.println(F("> On</td></tr>"));
                        client.println(F("</table>"));
                        client.println(F("</div>"));
                        client.println(F("</div>"));

                        // Second row: Relay Status and LSS side by side
                        client.println(F("<div class=\"container\">"));
                        client.println(F("<div class=\"column\">"));
                        client.println(F("<h2>Relay Status</h2>"));
                        client.println(F("<table>"));
                        for (int i = 0; i < numOfRelays; i++) {
                            client.print(F("<tr><td>Relay "));
                            client.print(i + 1);
                            client.print(F("</td><td><input type=\"checkbox\" id=\"relay"));
                            client.print(i + 1);
                            client.print(F("\" "));
                            client.print(F("><input type=\"text\" id=\"alias_relay"));
                            client.print(i + 1);
                            client.print(F("\" value=\""));
                            client.print(aliasRelays[i]);
                            client.print(F("\"></td></tr>"));
                        }
                        client.println(F("</table>"));
                        client.println(F("</div>"));
                        client.println(F("<div class=\"column\">"));
                        client.println(F("<h2>Low-Side Switches Status (0-255 = 0-V+)</h2>"));
                        client.println(F("<table>"));
                        for (int i = 0; i < numOfLSSwitches; i++) {
                            client.print(F("<tr><td>LSS "));
                            client.print(i + 1);
                            client.print(F("</td><td><input type=\"number\" id=\"lss"));
                            client.print(i + 1);
                            client.print(F("\" min=\"0\" max=\"255\" value=\"0\">"));
                            client.print(F("<input type=\"text\" id=\"alias_lss"));
                            client.print(i + 1);
                            client.print(F("\" value=\""));
                            client.print(aliasLSS[i]);
                            client.print(F("\"></td></tr>"));
                        }
                        client.println(F("</table>"));
                        client.println(F("</div>"));
                        client.println(F("</div>"));

                        // Third row: Digital Inputs (2x12) side by side
                        client.println(F("<div class=\"container\">"));
                        client.println(F("<div class=\"column\">"));
                        client.println(F("<h2>Digital Inputs 1-12</h2>"));
                        client.println(F("<table>"));
                        client.println(F("<tr><th>Input</th><th>State</th><th>Alias</th></tr>"));
                        for (int i = 0; i < 12; i++) {
                            client.print(F("<tr><td>DI"));
                            client.print(i + 1);
                            client.print(F("</td><td><span id=\"di"));
                            client.print(i + 1);
                            client.print(F("\"></span></td><td><input type=\"text\" id=\"alias_di"));
                            client.print(i + 1);
                            client.print(F("\" value=\""));
                            client.print(aliasDigInputs[i]);
                            client.print(F("\"></td></tr>"));
                        }
                        client.println(F("</table>"));
                        client.println(F("</div>"));
                        client.println(F("<div class=\"column\">"));
                        client.println(F("<h2>Digital Inputs 13-24</h2>"));
                        client.println(F("<table>"));
                        client.println(F("<tr><th>Input</th><th>State</th><th>Alias</th></tr>"));
                        for (int i = 12; i < 24; i++) {
                            client.print(F("<tr><td>DI"));
                            client.print(i + 1);
                            client.print(F("</td><td><span id=\"di"));
                            client.print(i + 1);
                            client.print(F("\"></span></td><td><input type=\"text\" id=\"alias_di"));
                            client.print(i + 1);
                            client.print(F("\" value=\""));
                            client.print(aliasDigInputs[i]);
                            client.print(F("\"></td></tr>"));
                        }
                        client.println(F("</table>"));
                        client.println(F("</div>"));
                        client.println(F("</div>"));

                        // Fourth row: Analog Inputs and Pulse Counts side by side
                        client.println(F("<div class=\"container\">"));
                        client.println(F("<div class=\"column\">"));
                        client.println(F("<h2>Analog Inputs (0-1024 = 0-10V)</h2>"));
                        client.println(F("<table>"));
                        client.println(F("<tr><th>Input</th><th>Value</th><th>Voltage</th><th>Alias</th></tr>"));
                        for (int i = 0; i < numOfAnaInputs; i++) {
                            client.print(F("<tr><td>AI"));
                            client.print(i + 1);
                            client.print(F("</td><td><span id=\"ai"));
                            client.print(i + 1);
                            client.print(F("\"></span></td><td><span id=\"ai_voltage"));
                            client.print(i + 1);
                            client.print(F("\"></span></td><td><input type=\"text\" id=\"alias_ai"));
                            client.print(i + 1);
                            client.print(F("\" value=\""));
                            client.print(aliasAnaInputs[i]);
                            client.print(F("\"></td></tr>"));
                        }
                        client.println(F("</table>"));
                        client.println(F("</div>"));
                        client.println(F("<div class=\"column\">"));
                        client.println(F("<h2>Pulse Counts (DI18, DI19, DI20)</h2>"));
                        client.println(F("<table>"));
                        client.println(F("<tr><th>Input</th><th>Count</th></tr>"));
                        client.println(F("<tr><td>DI18</td><td><span id=\"pulse1\"></span></td></tr>"));
                        client.println(F("<tr><td>DI19</td><td><span id=\"pulse2\"></span></td></tr>"));
                        client.println(F("<tr><td>DI20</td><td><span id=\"pulse3\"></span></td></tr>"));
                        client.println(F("</table>"));
                        client.println(F("</div>"));
                        client.println(F("</div>"));

                        // Fifth row: 1-Wire Sensors DS2438 and DS18B20 side by side
                        client.println(F("<div class=\"container\">"));
                        client.println(F("<div class=\"column\">"));
                        client.println(F("<h2>1-Wire Sensors DS2438</h2>"));
                        client.println(F("<table>"));
                        client.println(F("<tr><th>Sensor</th><th>Serial Number</th><th>Temp (°C)</th><th>Vad (V)</th><th>Vsens (V)</th><th>Alias</th></tr>"));
                        for (int i = 0; i < DS2438count; i++) {
                            client.print(F("<tr><td>DS2438-"));
                            client.print(i + 1);
                            client.print(F("</td><td>"));
                            client.print(oneWireAddressToString(sensors2438[i]));
                            client.print(F("</td><td><span id=\"ds2438_temp"));
                            client.print(i);
                            client.print(F("\"></span></td><td><span id=\"ds2438_vad"));
                            client.print(i);
                            client.print(F("\"></span></td><td><span id=\"ds2438_vsens"));
                            client.print(i);
                            client.print(F("\"></span></td><td><input type=\"text\" id=\"alias_ds2438_"));
                            client.print(i + 1);
                            client.print(F("\" value=\""));
                            client.print(aliasDS2438[i]);
                            client.print(F("\"></td></tr>"));
                        }
                        client.println(F("</table>"));
                        client.println(F("</div>"));
                        client.println(F("<div class=\"column\">"));
                        client.println(F("<h2>1-Wire Sensors DS18B20</h2>"));
                        client.println(F("<table>"));
                        client.println(F("<tr><th>Sensor</th><th>Serial Number</th><th>Temp (°C)</th><th>Alias</th></tr>"));
                        for (int i = 0; i < DS18B20count; i++) {
                            client.print(F("<tr><td>DS18B20-"));
                            client.print(i + 1);
                            client.print(F("</td><td>"));
                            client.print(oneWireAddressToString(sensors18B20[i]));
                            client.print(F("</td><td><span id=\"ds18b20_temp"));
                            client.print(i);
                            client.print(F("\"></span></td><td><input type=\"text\" id=\"alias_ds18b20_"));
                            client.print(i + 1);
                            client.print(F("\" value=\""));
                            client.print(aliasDS18B20[i]);
                            client.print(F("\"></td></tr>"));
                        }
                        client.println(F("</table>"));
                        client.println(F("</div>"));
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
    }

    // Initialize relays
    for (int i = 0; i < numOfRelays; i++) {
        pinMode(relayPins[i], OUTPUT);
        relayOnCommands[i] = relayStr + String(i + 1, DEC) + " on"; // Command for turning on
        relayOffCommands[i] = relayStr + String(i + 1, DEC) + " off"; // Command for turning off
        setRelay(i, 0); // Turn off relay
    }

    // Initialize Low-Side switches
    for (int i = 0; i < numOfLSSwitches; i++) {
        pinMode(LSSwitchPins[i], OUTPUT);
        LSSwitchOnCommands[i] = LSSwitchStr + String(i + 1, DEC) + " on"; // Command for turning on
        LSSwitchOffCommands[i] = LSSwitchStr + String(i + 1, DEC) + " off"; // Command for turning off
        LSSwitchPWMCommands[i] = LSSwitchStr + String(i + 1, DEC) + "_pwm"; // Např. "lo1_pwm"
        setLSSwitch(i, 0); // Turn off LSS
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

    EEPROM.get(EEPROM_WEBUPDATE, webUpdateCycle);
    if (webUpdateCycle < 1000 || webUpdateCycle > 60000) {
        webUpdateCycle = 2000; // Zvýšena výchozí hodnota na 2000 ms
        EEPROM.put(EEPROM_WEBUPDATE, webUpdateCycle);
    }

    for (int i = 0; i < numOfRelays; i++) {
        EEPROM.get(EEPROM_ALIAS_RELAYS + (i * 21), aliasRelays[i]);
        aliasRelays[i][20] = '\0';
        if (!isValidAlias(aliasRelays[i])) {
            strcpy(aliasRelays[i], "");
            EEPROM.put(EEPROM_ALIAS_RELAYS + (i * 21), aliasRelays[i]);
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

    if (initFlag != 0xAA) {
        // První spuštění: Inicializuj všechny aliasy na prázdné stringy
        dbgln("Inicializace EEPROM aliasů na prázdné hodnoty...");
        char emptyAlias[21] = "";  // Prázdný string s null terminátorem
        
        EEPROM.put(EEPROM_SERIALLOCK, false);

        for (int i = 0; i < numOfRelays; i++) {
            EEPROM.put(EEPROM_ALIAS_RELAYS + (i * 21), emptyAlias);
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
        attachInterrupt(digitalPinToInterrupt(15), [](){pulse1++;}, FALLING);
        attachInterrupt(digitalPinToInterrupt(16), [](){pulse2++;}, FALLING);
        attachInterrupt(digitalPinToInterrupt(17), [](){pulse3++;}, FALLING);
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
    sendMsg("di18 " + String(pulse1));
    sendMsg("di19 " + String(pulse2));
    sendMsg("di20 " + String(pulse3));
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

void readDigInputs() {
    int timestamp = millis();
    for (int i = 0; i < numOfDigInputs; i++) {      
        int oldValue = inputStatus[i];
        int newValue = inputStatusNew[i];
        int curValue = digitalRead(inputPins[i]);
        int byteNo = i / 8;
        int bitPos = i - (byteNo * 8);
        
        if (pulseOn && (i == 17 || i == 18 || i == 19)) { // DI18, DI19, DI20 (pins 15, 16, 17)
            curValue = 1;
        }
        
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
            sendAnaInput(i + 1, Mb.MbData[anaInp1Byte + i]);
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
    digitalWrite(ledPins[0], HIGH);
    if (ethernetOK) {
        udpSend.beginPacket(sendIpAddress, remPort);
        udpSend.write(outputPacketBuffer, message.length());
        udpSend.endPacket();
    }
    
    digitalWrite(ledPins[0], LOW);
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

// Set Low-Side switch state or PWM
void setLSSwitch(int lsswitch, int value) {
    if (lsswitch >= numOfLSSwitches) {
        return;
    }
    if (value == 0 || value == 1) {
        digitalWrite(LSSwitchPins[lsswitch], value); // Binary on/off
    } else if (value >= 2 && value <= 255) {
        analogWrite(LSSwitchPins[lsswitch], value); // PWM
    }
    lssVoltage[lsswitch] = (value / 255.0) * 10.0; // Update voltage for web interface
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
    for (int i = 0; i < numOfLSSwitches; i++) {
        setLSSwitch(i, bitRead(Mb.MbData[lssByte], i) ? Mb.MbData[lssPWM1Byte + i] : 0);
    }
    if (bitRead(Mb.MbData[serviceByte], 0)) {
        resetFunc();
    }
    if (receivePacket(&cmd)) {
        dbg("Received packet: ");
        dbgln(cmd);
        if (cmd.startsWith(relayStr)) {
            for (int i = 0; i < numOfRelays; i++) {
                if (i < 8) { byteNo = relOut1Byte; bitPos = i; } else { byteNo = relOut2Byte; bitPos = i-8; }
                if (cmd == relayOnCommands[i]) {
                    bitWrite(Mb.MbData[byteNo], bitPos, 1);
                    setRelay(i, 1);
                    dbg(F("Relay "));
                    dbg(String(i + 1));
                    dbgln(F(" ON"));
                } else if (cmd == relayOffCommands[i]) {
                    bitWrite(Mb.MbData[byteNo], bitPos, 0);
                    setRelay(i, 0);
                    dbg(F("Relay "));
                    dbg(String(i + 1));
                    dbgln(F(" OFF"));
                }
            }
        } else if (cmd.startsWith(LSSwitchStr)) {
            for (int i = 0; i < numOfLSSwitches; i++) {
                if (cmd == LSSwitchOnCommands[i]) {
                    Mb.MbData[lssPWM1Byte + i] = 255;
                    bitWrite(Mb.MbData[lssByte], i, 1);
                    setLSSwitch(i, 255);
                    dbg(F("LSS "));
                    dbg(String(i + 1));
                    dbgln(F(" ON"));
                } else if (cmd == LSSwitchOffCommands[i]) {
                    Mb.MbData[lssPWM1Byte + i] = 0;
                    bitWrite(Mb.MbData[lssByte], i, 0);
                    setLSSwitch(i, 0);
                    dbg(F("LSS "));
                    dbg(String(i + 1));
                    dbgln(F(" OFF"));
                } else if (cmd.startsWith(LSSwitchPWMCommands[i])) {
                    String pwmValue = cmd.substring(LSSwitchPWMCommands[i].length() + 1);
                    int value = pwmValue.toInt();
                    if (value >= 0 && value <= 255) {
                        Mb.MbData[lssPWM1Byte + i] = value;
                        bitWrite(Mb.MbData[lssByte], i, (value > 0) ? 1 : 0);
                        setLSSwitch(i, value);
                        dbg(F("LSS PWM "));
                        dbg(String(i + 1));
                        dbg(F(" set to "));
                        dbgln(String(value));
                    }
                }
            }
        } else if (cmd.startsWith(rstStr)) {
            bitWrite(Mb.MbData[serviceByte], 0, 1);
            dbg(F("Reset command received"));
        }
    }
}