/*
    Copyright (C) 2024  Ing. Pavel Sedlacek
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
     analog input state:             rail1 ai1 1020
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

   MODBUS TCP commands: FC1 - FC16
   MODBUS RTU commands: FC3, FC6, FC16
   
   MODBUS register map (1 register = 2 bytes = 16 bits)
          register number            description
          0                          relay outputs 1-8
          1                          relay outputs 9-12
          2                          digital outputs HSS 1-4, LSS 1-4
          3                          analog output 1
          4                          analog output 2
          5                          digital inputs 1-8
          6                          digital inputs 9-16
          7                          digital inputs 17-24
          8                          analog input 1           0-256
          9                          analog input 2           0-256
          10                         service - reset (bit 0)
          11                         1st DS2438 Temp (value multiplied by 100)
          12                         1st DS2438 Vad (value multiplied by 100)
          13                         1st DS2438 Vsens (value multiplied by 100)
          -
          39                         DS2438 values (up to 10 sensors) 
          40-50                      DS18B20 Temperature (up to 10 sensors) (value multiplied by 100)
   
   Combination of 1wire sensors must be up to 10pcs maximum (DS18B20 or DS2438)
   
   using RS485 the UDP syntax must have \n symbol at the end of the command line
     
	 
	 
   Obsah:

Debug makra pro ladění
Knihovny
Nastavení Ethernet komunikace
Nastavení RS485 komunikace
Nastavení UDP protokolu
Modbus mapování registrů
Časování (časové cykly)
GPIO mapování a konfigurace
Další konfigurace
Řetězce pro příkazy
Definice časovače
Instance časovačů
1-Wire senzory
Funkce pro resetování Ethernet modulu
Funkce pro kontrolu připojení LAN
Inicializace digitálních vstupů
Inicializace relé
Inicializace High-Side switche
Inicializace Low-Side switche
Inicializace analogových výstupů
Inicializace analogových vstupů
Inicializace LED diod
Inicializace časovačů
Čtení DIP switchů pro adresu desky
Nastavení Ethernet a RTU režimů podle DIP switchů
Výpis základních informací pro RS485
Inicializace Ethernetu a UDP
Inicializace Modbus a sériové komunikace
Inicializace 1-Wire senzorů
Aktivace watchdog
Reset funkce
Obnova IP adresy při DHCP
Výpis IP adresy do debug logu
Heartbeat pro RS485
Správa stavu status LED
Převede adresu 1-Wire senzoru na řetězec
Vyhledání všech připojených 1-Wire senzorů
Nastavení rozlišení 1-Wire senzoru
Spuštění měření teploty na 1-Wire senzoru
Čtení teploty z 1-Wire senzoru
Správa procesu měření 1-Wire senzorů
Čtení stavu digitálních vstupů
Čtení stavu analogových vstupů
Odeslání zprávy o změně stavu digitálního vstupu (zapnuto)
Odeslání zprávy o změně stavu digitálního vstupu (vypnuto)
Odeslání zprávy o změně stavu analogového vstupu
Odeslání zprávy UDP přes LAN nebo RS485
Nastavení stavu relé
Nastavení stavu high-side spínače (HSS)
Nastavení stavu low-side spínače (LSS)
Nastavení analogového výstupu (PWM)
Přijetí příchozího příkazu přes RS485 nebo UDP
Zpracování příchozích příkazů
   	 
	 
*/


// -----------------------------------------------------------------------------
// Debug makra pro ladění
// -----------------------------------------------------------------------------
#define dbg(x) ;                  // Debug výstup - neaktivní
//#define dbg(x) Serial.print(x);
#define dbgln(x) ;                // Debug výstup s novým řádkem - neaktivní
//#define dbgln(x) Serial.println(x);

#define ver 2.2	                  // Verze firmwaru

// -----------------------------------------------------------------------------
// Knihovny
// -----------------------------------------------------------------------------
#include <ArduinoRS485.h>         // Závislost pro ModbusRTU
#include <ArduinoModbus.h>        // ArduinoModbus knihovna
#include <OneWire.h>              // Podpora 1-Wire senzorů
#include <DS2438.h>               // DS2438 senzor podpora
#include <Ethernet.h>             // Ethernet komunikace
#include <EthernetUdp.h>          // UDP komunikace
#include <avr/wdt.h>              // Watchdog podpora

// -----------------------------------------------------------------------------
// Nastavení Ethernet komunikace
// -----------------------------------------------------------------------------
byte mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0};  // MAC adresa zařízení
unsigned long lastCheckTime = 0;   			     // Čas poslední kontroly připojení LAN
unsigned long checkInterval = 5000; 			  // Interval kontrol připojení LAN(ms)
IPAddress listenIpAddress;                       // Lokální IP adresa
IPAddress sendIpAddress(255, 255, 255, 255);     // Vzdálená IP adresa (broadcast)
IPAddress dhcpServer;              				 // DHCP brána
bool isCheckingEthernet = false;  // Stavová proměnná pro kontrolu Ethernetu
EthernetClient testClient;

// -----------------------------------------------------------------------------
// Nastavení Modbus TCP a RTU komunikace
// -----------------------------------------------------------------------------
EthernetServer modbusServer(502);
EthernetClient modbusClient;
#define NUM_REGISTERS 50           // Počet registrů Modbus
ModbusTCPServer modbusTCPServer;
bool isModbusActive = false;                     // Flag pro aktivní Modbus komunikaci
long baudRate = 115200;            // Baudrate pro sériovou komunikaci
#define serial3TxControl 16        // Pin pro řízení RS485 komunikace
#define RS485_SERIAL_PORT Serial3
#define RS485_DEFAULT_TX_PIN 14
#define RS485_DEFAULT_DE_PIN serial3TxControl
#define RS485_DEFAULT_RE_PIN 15
int serial3TxDelay = 10;           // Zpoždění sériového vysílání (ms)
int serial3TimeOut = 20;           // Timeout sériové komunikace (ms)
bool ticTac = 0;                   // Stav pro heartbeat

// -----------------------------------------------------------------------------
// Nastavení UDP protokolu
// -----------------------------------------------------------------------------
EthernetUDP udpRecv;               						// UDP příjem
EthernetUDP udpSend;               						// UDP odesílání
unsigned int listenPort = 44444;                 // UDP příchozí port
unsigned int sendPort = 55554;                   // UDP odchozí port
unsigned int remPort = 55555;                    // UDP vzdálený port
#define inputPacketBufferSize UDP_TX_PACKET_MAX_SIZE 	// Příchozí UDP buffer
char inputPacketBuffer[UDP_TX_PACKET_MAX_SIZE];
#define outputPacketBufferSize 100                  	// Odchozí UDP buffer
char outputPacketBuffer[outputPacketBufferSize];

// -----------------------------------------------------------------------------
// Modbus mapování registrů
// -----------------------------------------------------------------------------
#define relOut1Byte 0              // Relé výstupy 1-8
#define relOut2Byte 1              // Relé výstupy 9-12
#define hssLssByte 2               // High-side a Low-side switche
#define anaOut1Byte 3              // Analogové výstupy
#define anaOut2Byte 4              // Analogové výstupy
#define digInp1Byte 5              // Digitální vstupy 1-8
#define digInp2Byte 6              // Digitální vstupy 9-16
#define digInp3Byte 7              // Digitální vstupy 17-24
#define anaInp1Byte 8              // Analogový vstup 1
#define anaInp2Byte 9              // Analogový vstup 2
#define serviceByte 10             // Servisní bajt
#define oneWireTempByte 11         // Teplota z DS2438
#define oneWireVadByte 12          // Napětí (Vad) z DS2438
#define oneWireVsensByte 13        // Napětí (Vsens) z DS2438
#define oneWireDS18B20Byte 40      // Teploty z DS18B20

// -----------------------------------------------------------------------------
// Časování (časové cykly)
// -----------------------------------------------------------------------------
#define oneWireCycle 30000         // Cyklus pro 1-Wire senzory (30 s)
#define oneWireSubCycle 5000       // Sub-cyklus pro 1-Wire senzory (5 s)
#define anaInputCycle 10000        // Cyklus pro čtení analogových vstupů (10 s)
#define heartBeatCycle 60000       // Cyklus heartbeat (60 s)
#define statusLedTimeOn 50         // Stavová LED zapnutá (50 ms)
#define statusLedTimeOff 990       // Stavová LED vypnutá (990 ms)
#define commLedTimeOn 50           // Komunikační LED zapnutá (50 ms)
#define commLedTimeOff 50          // Komunikační LED vypnutá (50 ms)
#define debouncingTime 5           // Debouncing pro digitální vstupy (5 ms)

// -----------------------------------------------------------------------------
// GPIO mapování a konfigurace
// -----------------------------------------------------------------------------
#define numOfRelays 12             // Počet relé výstupů
int relayPins[numOfRelays] = {37, 35, 33, 31, 29, 27, 39, 41, 43, 45, 47, 49};

#define numOfHSSwitches 4          // High-side switche
int HSSwitchPins[numOfHSSwitches] = {5, 6, 7, 8};

#define numOfLSSwitches 4          // Low-side switche
int LSSwitchPins[numOfLSSwitches] = {9, 11, 12, 18};

#define numOfAnaOuts 2             // Analogové výstupy
int anaOutPins[numOfAnaOuts] = {3, 2};

#define numOfAnaInputs 2           // Analogové vstupy
int analogPins[numOfAnaInputs] = {64, 63};
float analogStatus[numOfAnaInputs];

#define numOfDigInputs 24          // Digitální vstupy
int inputPins[numOfDigInputs] = {34, 32, 30, 28, 26, 24, 22, 25, 23, 21, 20, 19, 36, 38, 40, 42, 44, 46, 48, 69, 68, 67, 66, 65};
int inputStatus[numOfDigInputs];     // Aktuální stav vstupů
int inputStatusNew[numOfDigInputs];  // Nový stav vstupů
int inputChangeTimestamp[numOfDigInputs]; // Čas změny vstupů

#define numOfDipSwitchPins 6       // DIP spínače
int dipSwitchPins[numOfDipSwitchPins] = {57, 56, 55, 54, 58, 59};

#define numOfLedPins 2             // LED diody
int ledPins[numOfLedPins] = {13, 17};

// -----------------------------------------------------------------------------
// Další konfigurace
// -----------------------------------------------------------------------------
int boardAddress = 0;              // Fyzická adresa desky
int ethOn = 0;                     // Ethernet vypnuto
int rtuOn = 0;                     // RTU vypnuto

// -----------------------------------------------------------------------------
// Řetězce pro příkazy
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// Definice časovače
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// Instance časovačů
// -----------------------------------------------------------------------------
Timer statusLedTimerOn;
Timer statusLedTimerOff;
Timer oneWireTimer;
Timer oneWireSubTimer;
Timer analogTimer;
Timer heartBeatTimer;

// -----------------------------------------------------------------------------
// 1-Wire senzory
// -----------------------------------------------------------------------------
OneWire ds(62);
byte oneWireData[12];
byte oneWireAddr[8];

#define maxSensors 10
byte readstage = 0, resolution = 11;
byte sensors[maxSensors][8], DS2438count, DS18B20count;
byte sensors2438[maxSensors][8], sensors18B20[maxSensors][8];
DS2438 ds2438(&ds); // DS2438 senzor


// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// Funkce pro resetování Ethernet modulu
// -----------------------------------------------------------------------------
void resetEthernetModule() {
    const int resetPin = 4; // Pin připojený k resetu modulu W5100
    pinMode(resetPin, OUTPUT);
    digitalWrite(resetPin, LOW);
    delay(100);             // Zpoždění pro reset
    digitalWrite(resetPin, HIGH);
    delay(1000);            // Čekání na obnovení modulu
    Ethernet.begin(mac);    // Znovu inicializace Ethernet knihovny
    dbgln("Ethernet modul byl restartován.");
}

// -----------------------------------------------------------------------------
// Funkce pro kontrolu připojení LAN
// -----------------------------------------------------------------------------
void checkEthernet() {
    dbgln("Kontroluji připojení...");
    // Ověření pomocí výchozí brány
    if (testClient.connect(dhcpServer, 80)) {
        dbgln("Připojení je funkční.");
        testClient.stop();
    } else {
        dbgln("Připojení selhalo. Restartuji Ethernet modul...");
        resetEthernetModule();
    }
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);  
  dbg("Railduino firmware version: ");
  dbgln(ver);

  // -------------------------------------------------------------------------
  // Inicializace digitálních vstupů
  // -------------------------------------------------------------------------
  for (int i = 0; i < numOfDigInputs; i++) {
      pinMode(inputPins[i], INPUT);
      inputStatus[i] = 1;                     // Výchozí stav vstupu
      inputStatusNew[i] = 0;                  // Inicializace nového stavu
      digStatCommand[i] = digStatStr + String(i + 1, DEC); // Příkaz pro stav vstupu
  }

  // -------------------------------------------------------------------------
  // Inicializace relé
  // -------------------------------------------------------------------------
  for (int i = 0; i < numOfRelays; i++) {
      pinMode(relayPins[i], OUTPUT);
      relayOnCommands[i] = relayStr + String(i + 1, DEC) + " on";  // Příkaz pro zapnutí
      relayOffCommands[i] = relayStr + String(i + 1, DEC) + " off"; // Příkaz pro vypnutí
      setRelay(i, 0);                        // Vypnutí všech relé
  }

  // -------------------------------------------------------------------------
  // Inicializace High-Side switche
  // -------------------------------------------------------------------------
  for (int i = 0; i < numOfHSSwitches; i++) {
      pinMode(HSSwitchPins[i], OUTPUT);
      HSSwitchOnCommands[i] = HSSwitchStr + String(i + 1, DEC) + " on";   // Příkaz pro zapnutí
      HSSwitchOffCommands[i] = HSSwitchStr + String(i + 1, DEC) + " off"; // Příkaz pro vypnutí
      setHSSwitch(i, 0);                  // Vypnutí všech High-Side switchů
  }

  // -------------------------------------------------------------------------
  // Inicializace Low-Side switche
  // -------------------------------------------------------------------------
  for (int i = 0; i < numOfLSSwitches; i++) {
      pinMode(LSSwitchPins[i], OUTPUT);
      LSSwitchOnCommands[i] = LSSwitchStr + String(i + 1, DEC) + " on";   // Příkaz pro zapnutí
      LSSwitchOffCommands[i] = LSSwitchStr + String(i + 1, DEC) + " off"; // Příkaz pro vypnutí
      setLSSwitch(i, 0);                  // Vypnutí všech Low-Side switchů
  }

  // -------------------------------------------------------------------------
  // Inicializace analogových výstupů
  // -------------------------------------------------------------------------
  for (int i = 0; i < numOfAnaOuts; i++) {
      pinMode(anaOutPins[i], OUTPUT);
      anaOutCommand[i] = anaOutStr + String(i + 1, DEC); // Příkaz pro analogový výstup
      setAnaOut(i, 0);                // Nastavení všech analogových výstupů na 0
  }

  // -------------------------------------------------------------------------
  // Inicializace analogových vstupů
  // -------------------------------------------------------------------------
  for (int i = 0; i < numOfAnaInputs; i++) {
      pinMode(analogPins[i], INPUT);
  }

  // -------------------------------------------------------------------------
  // Inicializace LED diod
  // -------------------------------------------------------------------------
  for (int i = 0; i < numOfLedPins; i++) {
      pinMode(ledPins[i], OUTPUT);
  }

  // -------------------------------------------------------------------------
  // Inicializace časovačů
  // -------------------------------------------------------------------------
  analogTimer.sleep(anaInputCycle);
  statusLedTimerOn.sleep(statusLedTimeOn);
  statusLedTimerOff.sleep(statusLedTimeOff);
  heartBeatTimer.sleep(heartBeatCycle);

  // -------------------------------------------------------------------------
  // Čtení DIP switchů pro adresu desky
  // -------------------------------------------------------------------------
  for (int i = 0; i < 4; i++) {
      pinMode(dipSwitchPins[i], INPUT);
      if (!digitalRead(dipSwitchPins[i])) { 
          boardAddress |= (1 << i); // Určení adresy desky
      }
  }
  boardAddressStr = String(boardAddress);
  boardAddressRailStr = railStr + String(boardAddress);
  dbg("Fyzická adresa modulu: ");
  dbgln(boardAddressStr);

  // -------------------------------------------------------------------------
  // Nastavení Ethernet a RTU režimů podle DIP switchů
  // -------------------------------------------------------------------------
  pinMode(dipSwitchPins[4], INPUT);
  if (!digitalRead(dipSwitchPins[4])) {
      ethOn = 1;
      dbgln("Ethernet ON");
  } else {
      ethOn = 0;
      dbgln("Ethernet OFF");
  }

  pinMode(dipSwitchPins[5], INPUT);
  if (!digitalRead(dipSwitchPins[5])) {
      rtuOn = 1;
      dbgln("485 RTU ON");
  } else {
      rtuOn = 0;
      dbgln("485 RTU OFF");
  }

  // -------------------------------------------------------------------------
  // Výpis základních informací pro RS485
  // -------------------------------------------------------------------------
  dbg(baudRate);
  dbg(" Bd, Tx Delay: ");
  dbg(serial3TxDelay);
  dbg(" ms, Timeout: ");
  dbg(serial3TimeOut);
  dbgln(" ms");

  // -------------------------------------------------------------------------
  // Inicializace Ethernetu
  // -------------------------------------------------------------------------
  if (ethOn) {
      mac[5] = (0xED + boardAddress);
      listenIpAddress = IPAddress(192, 168, 150, 150 + boardAddress);

      if (Ethernet.begin(mac) == 0) {
          dbgln("Failed to configure Ethernet using DHCP, using Static Mode");
          Ethernet.begin(mac, listenIpAddress);
      }
    
      dbg("IP modulu: ");
      printIPAddress();
      dbgln();

      dhcpServer = Ethernet.gatewayIP();
      dbg("Výchozí brána (DHCP server): ");
      dbgln(dhcpServer);     
    }

  // -------------------------------------------------------------------------
  // Inicializace Modbus TCP a RTU
  // -------------------------------------------------------------------------
  
  if (ethOn) {
    if (!modbusTCPServer.begin()) {
      dbgln("Chyba při inicializaci Modbus TCP serveru!");
      while (true) delay(1000);
    }
    modbusTCPServer.configureHoldingRegisters(0x00, NUM_REGISTERS);
  }
  
  if (rtuOn) {
    Serial3.begin(baudRate);
    Serial3.setTimeout(serial3TimeOut);
    pinMode(serial3TxControl, OUTPUT);
    digitalWrite(serial3TxControl, 0);
    if (!ModbusRTUServer.begin(1,9600,SERIAL_8N1)) { // Slave ID = 1
      Serial.println("Chyba při inicializaci Modbus RTU serveru!");
      while (true) delay(1000);
    }
    ModbusRTUServer.configureHoldingRegisters(0x00, NUM_REGISTERS);
  }

  dbgln("Modbus inicializován.");

  // -------------------------------------------------------------------------
  // Inicializace UDP
  // -------------------------------------------------------------------------
  udpRecv.begin(listenPort);
  udpSend.begin(sendPort);     

  // -------------------------------------------------------------------------
  // Inicializace 1-Wire senzorů
  // -------------------------------------------------------------------------
  lookUpSensors();

  // -------------------------------------------------------------------------
  // Aktivace watchdog
  // -------------------------------------------------------------------------
  wdt_enable(WDTO_8S);
}

// -------------------------------------------------------------------------
// -------------------------------------------------------------------------

void loop() {
    
    wdt_reset();

    readDigInputs();

    readAnaInputs();

    processCommands();
    
    processOnewire();

    statusLed();
    
    if (ethOn) {   
        unsigned long currentTime = millis();
        if (currentTime - lastCheckTime >= checkInterval)  {
          isCheckingEthernet = true;  // Nastavit stav na "probíhá kontrola Ethernetu"
          checkEthernet();
          lastCheckTime = currentTime;
          isCheckingEthernet = false;  // Ukončit kontrolu Ethernetu
        }
        
        if (!isCheckingEthernet ) {handleModbus();}
        
      }
      IPrenew();
   // } 
    
    if (rtuOn) {
      ModbusRTUServer.poll();
      heartBeat();
    }      
 
}

// -----------------------------------------------------------------------------
// Funkce pro obsluhu Modbus TCP klienta a kontrolu LAN připojení
// -----------------------------------------------------------------------------
void handleModbus() {
    modbusClient = modbusServer.available();  // Přijmout nového klienta
    if (modbusClient) {
        dbgln("New Modbus client connected");
    }

      // Pokud je klient připojen, zpracovat Modbus požadavky
    if (modbusClient && modbusClient.connected()) {
      modbusTCPServer.accept(modbusClient);
      modbusTCPServer.poll();
    }
  
    if (modbusClient && (!modbusClient.connected() || !modbusClient.available())) {
      modbusClient.stop();  // Uvolnění socketu
      dbgln("Modbus client disconnected, socket freed.");
    }
/*
    if (!modbusClient || !modbusClient.connected()) {
      isModbusActive = false;
    } else {
      isModbusActive = true;
    }
*/
}


// -----------------------------------------------------------------------------
// Reset funkce
// -----------------------------------------------------------------------------
void (*resetFunc)(void) = 0; // Ukazatel na reset funkci (resetování mikrokontroléru)

// -----------------------------------------------------------------------------
// Obnova IP adresy při DHCP
// -----------------------------------------------------------------------------
void IPrenew() {
    byte rtnVal = Ethernet.maintain();
    switch (rtnVal) {
        case 1: dbgln(F("DHCP renew fail")); break;
        case 2: dbgln(F("DHCP renew ok")); break;
        case 3: dbgln(F("DHCP rebind fail")); break;
        case 4: dbgln(F("DHCP rebind ok")); break;
    }
}

// -----------------------------------------------------------------------------
// Výpis IP adresy do debug logu
// -----------------------------------------------------------------------------
void printIPAddress() {
    for (byte thisByte = 0; thisByte < 4; thisByte++) {
        dbg(Ethernet.localIP()[thisByte]);
        dbg(".");
    }
}

// -----------------------------------------------------------------------------
// Heartbeat pro RS485
// -----------------------------------------------------------------------------
void heartBeat() {
    if (!heartBeatTimer.isOver()) return;

    heartBeatTimer.sleep(heartBeatCycle);
    if (ticTac) {
        sendMsg(heartBeatStr + " 1");
        ticTac = 0;
    } else {
        sendMsg(heartBeatStr + " 0");
        ticTac = 1;
    }
}

// -----------------------------------------------------------------------------
// Správa stavu status LED
// -----------------------------------------------------------------------------
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

// -----------------------------------------------------------------------------
// Převede adresu 1-Wire senzoru na řetězec
// -----------------------------------------------------------------------------
String oneWireAddressToString(byte addr[]) {
    String s = "";
    for (int i = 0; i < 8; i++) {
        s += String(addr[i], HEX);
    }
    return s;
}

// -----------------------------------------------------------------------------
// Vyhledání všech připojených 1-Wire senzorů
// -----------------------------------------------------------------------------
void lookUpSensors() {
    byte j = 0, k = 0, l = 0, m = 0;

    while ((j <= maxSensors) && (ds.search(sensors[j]))) {
        if (!OneWire::crc8(sensors[j], 7) != sensors[j][7]) {
            if (sensors[j][0] == 38) {
                for (l = 0; l < 8; l++) {
                    sensors2438[k][l] = sensors[j][l];
                }
                k++;
            } else {
                for (l = 0; l < 8; l++) {
                    sensors18B20[m][l] = sensors[j][l];
                }
                m++;
                dssetresolution(ds, sensors[j], resolution);
            }
        }
        j++;
    }

    DS2438count = k;
    DS18B20count = m;
    dbg("1-wire sensors found: ");
    dbgln(k + m);
}

// -----------------------------------------------------------------------------
// Nastavení rozlišení 1-Wire senzoru
// -----------------------------------------------------------------------------
void dssetresolution(OneWire ow, byte addr[8], byte resolution) {
    byte resbyte = 0x1F;
    if (resolution == 12) resbyte = 0x7F;
    else if (resolution == 11) resbyte = 0x5F;
    else if (resolution == 10) resbyte = 0x3F;

    ow.reset();
    ow.select(addr);
    ow.write(0x4E);
    ow.write(0);
    ow.write(0);
    ow.write(resbyte);
    ow.write(0x48);
}

// -----------------------------------------------------------------------------
// Spuštění měření teploty na 1-Wire senzoru
// -----------------------------------------------------------------------------
void dsconvertcommand(OneWire ow, byte addr[8]) {
    ow.reset();
    ow.select(addr);
    ow.write(0x44, 1);
}

// -----------------------------------------------------------------------------
// Čtení teploty z 1-Wire senzoru
// -----------------------------------------------------------------------------
float dsreadtemp(OneWire ow, byte addr[8]) {
    int i;
    byte data[12];
    float celsius;

    ow.reset();
    ow.select(addr);
    ow.write(0xBE);

    for (i = 0; i < 9; i++) {
        data[i] = ow.read();
    }

    int16_t TReading = (data[1] << 8) | data[0];
    celsius = 0.0625 * TReading;
    return celsius;
}

// -----------------------------------------------------------------------------
// Správa procesu měření 1-Wire senzorů
// -----------------------------------------------------------------------------
void processOnewire() {
    static byte oneWireState = 0;
    static byte oneWireCnt = 0;

    switch (oneWireState) {
        case 0:
            if (!oneWireTimer.isOver()) return;
            oneWireTimer.sleep(oneWireCycle);
            oneWireSubTimer.sleep(oneWireSubCycle);
            oneWireCnt = 0;
            oneWireState++;
            break;

        case 1:
            if (!oneWireSubTimer.isOver()) return;
            if ((oneWireCnt < DS2438count)) {
                ds2438.begin();
                ds2438.update(sensors2438[oneWireCnt]);
                if (!ds2438.isError()) {
                    modbusTCPServer.holdingRegisterWrite(oneWireTempByte + (oneWireCnt * 3), ds2438.getTemperature() * 100);
                    modbusTCPServer.holdingRegisterWrite(oneWireVadByte + (oneWireCnt * 3), ds2438.getVoltage(DS2438_CHA) * 100);
                    modbusTCPServer.holdingRegisterWrite(oneWireVsensByte + (oneWireCnt * 3), ds2438.getVoltage(DS2438_CHB) * 100);
                    sendMsg("1w " + oneWireAddressToString(sensors2438[oneWireCnt]) + " " + String(ds2438.getTemperature(), 2) + " " + String(ds2438.getVoltage(DS2438_CHA), 2) + " " + String(ds2438.getVoltage(DS2438_CHB), 2));
                }
                oneWireCnt++;
            } else {
                oneWireCnt = 0;
                oneWireState++;
            }
            break;

        case 2:
            if (!oneWireSubTimer.isOver()) return;
            if ((oneWireCnt < DS18B20count)) {
                dsconvertcommand(ds, sensors18B20[oneWireCnt]);
                oneWireCnt++;
            } else {
                oneWireCnt = 0;
                oneWireState++;
            }
            break;

        case 3:
            if (!oneWireSubTimer.isOver()) return;
            if ((oneWireCnt < DS18B20count)) {
                modbusTCPServer.holdingRegisterWrite(oneWireDS18B20Byte + oneWireCnt, dsreadtemp(ds, sensors18B20[oneWireCnt]) * 100);
                sendMsg("1w " + oneWireAddressToString(sensors18B20[oneWireCnt]) + " " + String(dsreadtemp(ds, sensors18B20[oneWireCnt]), 2));
                oneWireCnt++;
            } else {
                oneWireState = 0;
            }
            break;
    }
}

// -----------------------------------------------------------------------------
// Čtení stavu digitálních vstupů
// -----------------------------------------------------------------------------
void readDigInputs() {
    int timestamp = millis();

    for (int i = 0; i < numOfDigInputs; i++) {
        int oldValue = inputStatus[i];
        int newValue = inputStatusNew[i];
        int curValue = digitalRead(inputPins[i]);
        int byteNo = i / 8; // Určení byte čísla
        int bitPos = i - (byteNo * 8); // Pozice bitu v bytu

        // Kontrola změny stavu vstupu
        if (oldValue != newValue) {
            if (newValue != curValue) {
                inputStatusNew[i] = curValue;
            } else if (timestamp - inputChangeTimestamp[i] > debouncingTime) {
                inputStatus[i] = newValue;

                if (!newValue) {
                    int dig = modbusTCPServer.holdingRegisterRead(byteNo + 5);
                    bitWrite(dig, bitPos, 1); 
                    modbusTCPServer.holdingRegisterWrite(byteNo + 5, dig);
                    sendInputOn(i + 1);
                } else {
                    int dig = modbusTCPServer.holdingRegisterRead(byteNo + 5);
                    bitWrite(dig, bitPos, 0); 
                    modbusTCPServer.holdingRegisterWrite(byteNo + 5, dig);
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

// -----------------------------------------------------------------------------
// Čtení stavu analogových vstupů
// -----------------------------------------------------------------------------
void readAnaInputs() {
    if (!analogTimer.isOver()) return; // Kontrola časovače

    analogTimer.sleep(anaInputCycle);

    for (int i = 0; i < numOfAnaInputs; i++) {
        int pin = analogPins[i];
        float value = analogRead(pin); // Čtení hodnoty
        float oldValue = analogStatus[i];
        analogStatus[i] = value;

        // Kontrola změny hodnoty
        if (value != oldValue) {
            modbusTCPServer.holdingRegisterWrite(i + 8,(int)value); // Aktualizace Modbus dat
            sendAnaInput(i + 1, modbusTCPServer.holdingRegisterRead(i + 8));
        }
    }
}

// -----------------------------------------------------------------------------
// Odeslání zprávy o změně stavu digitálního vstupu (zapnuto)
// -----------------------------------------------------------------------------
void sendInputOn(int input) {
    sendMsg(digInputStr + String(input, DEC) + " 1");
}

// -----------------------------------------------------------------------------
// Odeslání zprávy o změně stavu digitálního vstupu (vypnuto)
// -----------------------------------------------------------------------------
void sendInputOff(int input) {
    sendMsg(digInputStr + String(input, DEC) + " 0");
}

// -----------------------------------------------------------------------------
// Odeslání zprávy o změně stavu analogového vstupu
// -----------------------------------------------------------------------------
void sendAnaInput(int input, float value) {
    sendMsg(anaInputStr + String(input, DEC) + " " + String(value, 2));
}

// -----------------------------------------------------------------------------
// Odeslání zprávy UDP přes LAN nebo RS485
// -----------------------------------------------------------------------------
void sendMsg(String message) {
    message = railStr + boardAddressStr + " " + message;
    message.toCharArray(outputPacketBuffer, outputPacketBufferSize);

    digitalWrite(ledPins[1], HIGH); // Indikace přenosu

    // Odeslání přes Ethernet UDP
    if (ethOn) {
        udpSend.beginPacket(sendIpAddress, remPort);
        udpSend.write(outputPacketBuffer, message.length());
        udpSend.endPacket();
    }

    // Odeslání přes RS485
    if (!rtuOn) {
        digitalWrite(serial3TxControl, HIGH);
        Serial3.print(message + "\n");
        delay(serial3TxDelay);
        digitalWrite(serial3TxControl, LOW);
    }

    digitalWrite(ledPins[1], LOW); // Konec přenosu

    dbg("Sending packet: ");
    dbgln(message);
}

// -----------------------------------------------------------------------------
// Nastavení stavu relé
// -----------------------------------------------------------------------------
void setRelay(int relay, int value) {
    if (relay > numOfRelays) return;
    digitalWrite(relayPins[relay], value);
}

// -----------------------------------------------------------------------------
// Nastavení stavu high-side spínače (HSS)
// -----------------------------------------------------------------------------
void setHSSwitch(int hsswitch, int value) {
    if (hsswitch > numOfHSSwitches) return;
    digitalWrite(HSSwitchPins[hsswitch], value);
}

// -----------------------------------------------------------------------------
// Nastavení stavu low-side spínače (LSS)
// -----------------------------------------------------------------------------
void setLSSwitch(int lsswitch, int value) {
    if (lsswitch > numOfLSSwitches) return;
    digitalWrite(LSSwitchPins[lsswitch], value);
}

// -----------------------------------------------------------------------------
// Nastavení analogového výstupu (PWM)
// -----------------------------------------------------------------------------
void setAnaOut(int pwm, int value) {
    if (pwm > numOfAnaOuts) return;
    analogWrite(anaOutPins[pwm], value);
}

// -----------------------------------------------------------------------------
// Přijetí příchozího příkazu přes RS485 nebo UDP
// -----------------------------------------------------------------------------
boolean receivePacket(String *cmd) {
    // Čtení příkazu přes RS485
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

    // Čtení příkazu přes UDP
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

// -----------------------------------------------------------------------------
// Zpracování příchozích příkazů
// -----------------------------------------------------------------------------
void processCommands() {
    String cmd;
    byte byteNo, curBitValue, bitPos;
    int curAnaVal;

    // Nastavení výstupů podle Modbus dat
    for (int i = 0; i < numOfRelays; i++) {
        if (i < 8) {
            setRelay(i, bitRead(modbusTCPServer.holdingRegisterRead(relOut1Byte), i));
        } else {
            setRelay(i, bitRead(modbusTCPServer.holdingRegisterRead(relOut2Byte), i - 8));
        }
    }

    for (int i = 0; i < numOfHSSwitches; i++) {
        setHSSwitch(i, bitRead(modbusTCPServer.holdingRegisterRead(hssLssByte), i));
    }

    for (int i = 0; i < numOfLSSwitches; i++) {
        setLSSwitch(i, bitRead(modbusTCPServer.holdingRegisterRead(hssLssByte), i + numOfHSSwitches));
    }

    for (int i = 0; i < numOfAnaOuts; i++) {
        setAnaOut(i, modbusTCPServer.holdingRegisterRead(anaOut1Byte + i));
    }

    // Reset zařízení, pokud je aktivován příslušný bit
    if (bitRead(modbusTCPServer.holdingRegisterRead(serviceByte), 0)) {
        resetFunc();
    }

    // Zpracování příchozích příkazů
    if (receivePacket(&cmd)) {
        dbg("Received packet: ");
        dbgln(cmd);
        digitalWrite(ledPins[1], HIGH);

        // Zpracování příkazů na relé
        if (cmd.startsWith(relayStr)) {
            for (int i = 0; i < numOfRelays; i++) {
                if (i < 8) {
                    byteNo = relOut1Byte;
                    bitPos = i;
                } else {
                    byteNo = relOut2Byte;
                    bitPos = i - 8;
                }
                if (cmd == relayOnCommands[i]) {
                    int rel = modbusTCPServer.holdingRegisterRead(byteNo);
                    bitWrite(rel, bitPos, 1); 
                    modbusTCPServer.holdingRegisterWrite(byteNo, rel);
                } else if (cmd == relayOffCommands[i]) {
                    int rel = modbusTCPServer.holdingRegisterRead(byteNo);
                    bitWrite(rel, bitPos, 0); 
                    modbusTCPServer.holdingRegisterWrite(byteNo, rel);
                }
            }
        }
        // Zpracování dalších příkazů (HSS, LSS, analogové výstupy, reset)
        else if (cmd.startsWith(HSSwitchStr)) {
            for (int i = 0; i < numOfHSSwitches; i++) {
                if (cmd == HSSwitchOnCommands[i]) {
                    int hsslss = modbusTCPServer.holdingRegisterRead(hssLssByte);
                    bitWrite(hsslss, i, 1); 
                    modbusTCPServer.holdingRegisterWrite(hssLssByte, hsslss);
                } else if (cmd == HSSwitchOffCommands[i]) {
                    int hsslss = modbusTCPServer.holdingRegisterRead(hssLssByte);
                    bitWrite(hsslss, i, 0); 
                    modbusTCPServer.holdingRegisterWrite(hssLssByte, hsslss);
                }
            }
        } else if (cmd.startsWith(LSSwitchStr)) {
            for (int i = 0; i < numOfLSSwitches; i++) {
                if (cmd == LSSwitchOnCommands[i]) {
                    int hsslss = modbusTCPServer.holdingRegisterRead(hssLssByte);
                    bitWrite(hsslss, i + numOfHSSwitches, 1); 
                    modbusTCPServer.holdingRegisterWrite(hssLssByte, hsslss);
                } else if (cmd == LSSwitchOffCommands[i]) {
                    int hsslss = modbusTCPServer.holdingRegisterRead(hssLssByte);
                    bitWrite(hsslss, i + numOfHSSwitches, 0); 
                    modbusTCPServer.holdingRegisterWrite(hssLssByte, hsslss);
                }
            }
        } else if (cmd.startsWith(anaOutStr)) {
            String anaOutValue = cmd.substring(anaOutStr.length() + 2);
            for (int i = 0; i < numOfAnaOuts; i++) {
                if (cmd.substring(0, anaOutStr.length() + 1) == anaOutCommand[i]) {
                    modbusTCPServer.holdingRegisterWrite(anaOut1Byte + i, anaOutValue.toInt());
                }
            }
        } else if (cmd.startsWith(rstStr)) {
            int serbyte = modbusTCPServer.holdingRegisterRead(serviceByte);
            bitWrite(serbyte, 0, 1); 
            modbusTCPServer.holdingRegisterWrite(serviceByte, serbyte);
        }
        digitalWrite(ledPins[1], LOW);
    }
}

