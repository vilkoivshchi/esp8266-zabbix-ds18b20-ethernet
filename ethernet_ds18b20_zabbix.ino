#include <SPI.h>
#include <Ethernet2.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP8266WiFi.h>
#include <rBase64.h>
#include <EEPROM.h>
#include <ArduinoJson.h>

//DS18B20
//Pin to which is attached a temperature sensor
#define ONE_WIRE_BUS D4
//The maximum number of devices
//#define ONE_WIRE_MAX_DEV 16
const uint8_t ONE_WIRE_MAX_DEV = 16;
//The resolution of the DS18B20 is configurable (9, 10, 11, or 12 bits), with 12-bit readings the factory default state. This
// equates to a temperature resolution of 0.5°C, 0.25°C, 0.125°C, or 0.0625°C.
const uint8_t defDsResolution = 9;
uint8_t dsResolution = 9;
//Define Reset pin
#define RESET_P D1
//define W5500 SCS pin. Most likely, you may need connect SCS on W5500 to Dx pin on the NodeMCU
//trough PNP transistor (like BC550), else cheap clone unlikely to work
#define SCS_P D8

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);

//Number of temperature devices found
uint8_t numberOfDevices;
//An array device temperature sensors
DeviceAddress devAddr[ONE_WIRE_MAX_DEV];

//Saving the last measurement of temperature
float tempDev[ONE_WIRE_MAX_DEV];
//Previous temperature measurement
float tempDevLast[ONE_WIRE_MAX_DEV];

//The time of last measurement
long lastTemp;
//The frequency of temperature measurement in ms
const uint16_t durationTemp = 5000;
char temperatureString[6];

//Wiznet have MAC 00:08:DC:xx:xx:xx https://code.wireshark.org/review/gitweb?p=wireshark.git;a=blob_plain;f=manuf
uint8_t mac[] = { 0x00, 0x08, 0xDC, 0x00, 0xAA, 0x02 };

uint8_t ip[] = { 0, 0, 0, 0 };
uint8_t nameserver[] = { 8, 8, 4, 4 };
uint8_t gateway[] = { 0, 0, 0, 0 };
uint8_t mask[] = { 0, 0, 0, 0 };

//Default network settings
const uint8_t defaultIp[] = { 192, 168, 1, 200 };
const uint8_t defaultGateway[] = { 192, 168, 1, 1 };
const uint8_t defaultMask[] = { 255, 255, 255, 0 };

const char defautLogin[17] = "admin";
const char defaultPassword[17] = "admin";

char login[17];
char password[17];

uint8_t loginLength = sizeof(login);
uint8_t passwordLength = sizeof(password);

const int eeprom_size = 47;

char base64Str[32];

// start Zabbix agent and web server

EthernetServer zabbixagent(10050);
EthernetServer webserver(80);

//Convert device id to String
String GetAddressToString(DeviceAddress deviceAddress) {
  String str = "";
  for (uint8_t i = 0; i < 8; i++) {
    if ( deviceAddress[i] < 16 ) str += String(0, HEX);
    str += String(deviceAddress[i], HEX);
  }
  return str;
}

//Setting the temperature sensor
void SetupDS18B20() {
  DS18B20.begin();

  Serial.print("Parasite power is: ");
  if ( DS18B20.isParasitePowerMode() ) {
    Serial.println("ON");
  } else {
    Serial.println("OFF");
  }

  numberOfDevices = DS18B20.getDeviceCount();
  Serial.print( "Device count: " );
  Serial.println( numberOfDevices );

  lastTemp = millis();
  DS18B20.requestTemperatures();

  // Loop through each device, print out address
  for (int i = 0; i < numberOfDevices; i++) {
    // Search the 1-wire for address
    // and set resolution
    if ( DS18B20.getAddress(devAddr[i], i) ) {
      DS18B20.setResolution(devAddr[i], dsResolution);
      Serial.print("Found device ");
      Serial.print(i, DEC);
      Serial.print(" with address: " + GetAddressToString(devAddr[i]));
      Serial.println();
      wdt_reset();
    } else {
      Serial.print("Found ghost device at ");
      Serial.print(i, DEC);
      Serial.print(" but could not detect address. Check power and cabling");
    }

    //Get resolution of DS18b20
    Serial.print("Resolution: ");
    Serial.print(DS18B20.getResolution( devAddr[i] ));
    Serial.println();

    //Read temperature from DS18b20
    float tempC = DS18B20.getTempC( devAddr[i] );
    Serial.print("Temp C: ");
    Serial.println(tempC);
  }
}

//Loop measuring the temperature
void TempLoop(long now) {
  if ( now - lastTemp > durationTemp ) { //Take a measurement at a fixed time (durationTemp = 5000ms, 5s)
    for (int i = 0; i < numberOfDevices; i++) {
      float tempC = DS18B20.getTempC( devAddr[i] ); //Measuring temperature in Celsius
      tempDev[i] = tempC; //Save the measured value to the array
    }
    //No waiting for measurement
    DS18B20.setWaitForConversion(false);
    //No waiting for measurement
    DS18B20.requestTemperatures();
    //Remember the last time measurement
    lastTemp = millis();

  }
}

void WizReset() {
  Serial.println("Starting Wiz W5500 Ethernet...");
  pinMode(RESET_P, OUTPUT);
  digitalWrite(RESET_P, HIGH);
  delay(250);
  digitalWrite(RESET_P, LOW);
  delay(50);
  digitalWrite(RESET_P, HIGH);
  delay(350);
  Serial.println("Done");
}

void encodeAuthData(const char *base64login, uint8_t base64loginLenght, const char *base64password, uint8_t base64passwordLenght) {

  char resStrForBase[32];
  memset(resStrForBase, 0, sizeof(resStrForBase));
  uint8_t i;
  uint8_t j;
  uint8_t k;

  //add login into prehash string
  for (i = 0; i < base64loginLenght; i++){
    resStrForBase[i] = base64login[i];
    if (base64login[i] == '\0') {
      break;
    }
  }
  resStrForBase[i] = ':';
  i++;
  //add password into prehash string
  for (j = 0; j < (base64passwordLenght); j++){
    resStrForBase[i] = base64password[j];
    i++;
  }
  //calculate payload of the prehash string
  for (k = 0; k < sizeof(resStrForBase); k++){
    //Serial.print(resStrForBase[k]);
    if (resStrForBase[k] == '\0') {
      break;
    }
  }

  //encoding our login + password to base64 for future compare with login + password from client
  rbase64_encode(base64Str, resStrForBase, k);

}

void eepromReadData() {
  uint8_t address = 0;
  uint8_t value;

  for (address = 0; address < eeprom_size; address++){
    value = EEPROM.read(address);

    Serial.print(address);
    Serial.print("\t");
    Serial.print(value, HEX);
    Serial.println();
  }

}

void chooseNetConfig() {
  uint8_t address = 0;
  uint8_t ipAddrIndex = 0;
  memset(login, 0, loginLength);
  memset(password, 0, passwordLength);

  for (address = 0; address < 4; address++){
    ip[ipAddrIndex] = EEPROM.read(address);
    ipAddrIndex++;
  }
  ipAddrIndex = 0;
  for (address = 4; address < 8; address++){
    mask[ipAddrIndex] = EEPROM.read(address);
    ipAddrIndex++;
  }
  ipAddrIndex = 0;
  for (address = 8; address < 12; address++){
    gateway[ipAddrIndex] = EEPROM.read(address);
    ipAddrIndex++;
  }
  //checks if a ip are set, use default if not
  if (ip[0] >= 224) {
    Serial.println("===============================");
    Serial.println("Using default IP");
    Serial.println("===============================");
    for (ipAddrIndex = 0; ipAddrIndex < 4; ipAddrIndex++){
      ip[ipAddrIndex] = defaultIp[ipAddrIndex];
      mask[ipAddrIndex] = defaultMask[ipAddrIndex];
      gateway[ipAddrIndex] = defaultGateway[ipAddrIndex];
    }
  }
}

void choosePasswordConfig() {
  uint8_t address = 0;
  uint8_t loginIndex = 0;

  for(address = 12; address < loginLength + 12; address++) {
    login[loginIndex] = EEPROM.read(address);
    loginIndex++;
  }

  loginIndex = 0;
  for(address = 29; address < loginLength + 29; address++) {
    password[loginIndex] = EEPROM.read(address);
    loginIndex++;
  }

  //checks if a login and password are set, use default if not

  if(login[0] == 255 || password[0] == 255) {
    Serial.println("===============================");
    Serial.println("No user password set, using admin:admin");
    Serial.println("===============================");
    for (loginIndex = 0; loginIndex < loginLength; loginIndex++) {
      login[loginIndex] = defautLogin[loginIndex];
      password[loginIndex] = defaultPassword[loginIndex];
    }
  }
}

void clearEEPROM() {
  for (uint8_t address = 0; address < eeprom_size; address++) {
    EEPROM.write(address, 255);
  }
  EEPROM.commit();
  Serial.println("=================================");
  Serial.println("EEPROM cleaed!");
  Serial.println("=================================");
}

void chooseDsRes() {
  uint8_t userDsRes = EEPROM.read(46);
  if (userDsRes < 9 || userDsRes > 12) {
    Serial.println("=================================");
    Serial.println("Using default DS18B20 resolution: 9");
    Serial.println("=================================");
    dsResolution = defDsResolution;
  } else {
    dsResolution = userDsRes;
  }
}

void setup() {
  Serial.begin(115200);
  //Enable EEPROM
  EEPROM.begin(eeprom_size);
  //clearEEPROM();
  //eepromReadData();
  chooseNetConfig();
  chooseDsRes();
  choosePasswordConfig();

  //Force disable Wifi. It may still work if you use Erase Flash: "Only sketch"
  WiFi.mode( WIFI_OFF );
  WiFi.forceSleepBegin();

  //Start Ethernet
  Ethernet.init(SCS_P);
  delay(1000);
  WizReset();
  Ethernet.begin(mac, ip, nameserver, gateway, mask);

  // Print to serial network parameters (just for check)
  delay(1000);

  Serial.print("IP: ");
  Serial.println(Ethernet.localIP());
  Serial.print("mask: ");
  Serial.println(Ethernet.subnetMask());
  Serial.print("gw: ");
  Serial.println(Ethernet.gatewayIP());
  Serial.print("dns: ");
  Serial.println(Ethernet.dnsServerIP());

  zabbixagent.begin();
  delay(200);
  webserver.begin();

  //Setup DS18b20 temperature sensor
  SetupDS18B20();

  memset(base64Str, 0, sizeof(base64Str));
  encodeAuthData(login, loginLength, password, passwordLength);
}

void loop() {
  long t = millis();
  //Periodic measurement of temperature
  TempLoop( t );
  //---------------------------------------------------------------
  // Web server section

  EthernetClient client = webserver.available();
  if (client) {
    Serial.println("new Web client");
    char webBuff[255];
    memset(webBuff, 0, sizeof(webBuff));
    uint8_t webBuffIndex;

    //array must initilize for properly handle of first request after boot
    boolean lineIsBlank = true;
    webBuffIndex = 0;
    uint8_t webReqStrIndex = 0;
    uint8_t webStrLen;
    uint8_t reqStrNum = 0;
    char getHeaderStr[32];
    memset(getHeaderStr, 0, sizeof(getHeaderStr));
    char webAuthTokenPreStr[64];
    memset(webAuthTokenPreStr, 0, sizeof(webAuthTokenPreStr));
    char webAuthTokenStr[32];
    memset(webAuthTokenStr, 0, sizeof(webAuthTokenStr));
    bool isPostRequest = false;
    bool webTokenSet = false;
    char webPageAddr[8];
    memset(webPageAddr, 0, sizeof(webPageAddr));
    bool timeToReadPost = false;
    char postBody[128];
    memset(postBody, 0, sizeof(postBody));
    char actionResponse[32];
    memset(actionResponse, 0, sizeof(actionResponse));
    char actionResponsePwd[32];
    memset(actionResponsePwd, 0, sizeof(actionResponsePwd));
    char actionResponseDs[32];
    memset(actionResponseDs, 0, sizeof(actionResponseDs));

    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        webBuff[webBuffIndex] = c;

        //client request comlete
        if (c == '\n' && lineIsBlank) {
          uint8_t postBodyIndex = 0;
          //get POST body, if available
          while(client.available()) {
            c = client.read();
            postBody[postBodyIndex] = c;
            postBodyIndex++;
            isPostRequest = true;
          }
          //parse POST body
          if (isPostRequest) {

            uint8_t ipToSave[4];
            uint8_t maskToSave[4];
            uint8_t gateToSave[4];

            char loginToSave[17];
            memset(loginToSave, 0, sizeof(loginToSave));
            char passToSave[17];
            memset(passToSave, 0, sizeof(passToSave));

            uint8_t ipToSaveIndex = 0;
            uint8_t maskToSaveIndex = 0;
            uint8_t gateipToSaveIndex = 0;

            uint8_t postBodyIndex = 0;

            char* postToken;
            postToken = strtok(postBody, "&=");
            //IP change block
            if (strcmp(postToken, "ip_a") == 0) {
              for (postBodyIndex = 1; postBodyIndex < 24; postBodyIndex++) {
                postToken = strtok(NULL, "&=");
                if (postBodyIndex % 2 != 0) {
                  EEPROM.write(ipToSaveIndex, (uint8_t)atoi(postToken));
                  ipToSaveIndex++;
                }

              }
              if (EEPROM.commit()) {
                Serial.println("=================================");
                Serial.println("Network settings are saved");
                Serial.println("=================================");
                char printToHtml[27] = "Network settings are saved";
                memcpy(actionResponse, printToHtml, sizeof(printToHtml));
              } else {
                Serial.println("=================================");
                Serial.println("Network settings NOT saved");
                Serial.println("=================================");
                char printToHtml[27] = "Network settings NOT saved";
                memcpy(actionResponse, printToHtml, sizeof(printToHtml));
              }
              //Authentication change block
            } else if (strcmp(postToken, "login") == 0) {
              for (postBodyIndex = 1; postBodyIndex < 4; postBodyIndex++) {
                postToken = strtok(NULL, "&=");

                if (postBodyIndex == 1) {
                  strcpy(loginToSave, postToken);
                }
                if (postBodyIndex == 3) {
                  strcpy(passToSave, postToken);
                }

              }
              //define adress of login in EEPROM;
              ipToSaveIndex = 12;
              for (uint8_t k; k < sizeof(loginToSave); k++) {
                EEPROM.write(ipToSaveIndex, loginToSave[k]);
                ipToSaveIndex++;
              }

              for (uint8_t k; k < sizeof(passToSave); k++) {
                EEPROM.write(ipToSaveIndex, passToSave[k]);
                ipToSaveIndex++;
              }

              if (EEPROM.commit()) {
                Serial.println("=================================");
                Serial.println("Password was saved");
                Serial.println("=================================");
                char printToHtml[27] = "Password are saved";
                memcpy(actionResponsePwd, printToHtml, sizeof(printToHtml));
              } else {
                Serial.println("=================================");
                Serial.println("Password was NOT saved");
                Serial.println("=================================");
                char printToHtml[27] = "Password NOT saved";
                memcpy(actionResponsePwd, printToHtml, sizeof(printToHtml));
              }

              //Reset button block
              //redirect browser and reboot device
            } else if (strcmp(postToken, "reboot_sum") == 0) {
              Serial.println("=================================");
              Serial.println("Reboot by user");
              Serial.println("=================================");
              client.println(F("HTTP/1.1 200 OK"));
              client.println(F("Content-Type: text/html"));
              client.println(F("Connection: close"));
              client.println();
              client.println(F("<!DOCTYPE HTML>"));
              client.print(F("<html><head><link rel=\"shortcut icon\" href=\"#\"><meta http-equiv=\"Refresh\" content=\"10;url=http://"));
              client.print(EEPROM.read(0));
              client.print(".");
              client.print(EEPROM.read(1));
              client.print(".");
              client.print(EEPROM.read(2));
              client.print(".");
              client.print(EEPROM.read(3));
              client.print("/setup\"></head><body><style type=\"text/css\">body { background: #FE938C; font-family: Georgia, serif; }</style>Please stand by...</body></html>");
              client.println();
              client.stop();

              ESP.restart();
              //DS18B20 resolution change
            } else if (strcmp(postToken, "ds_res") == 0) {
              postToken = strtok(NULL, "&=");
              EEPROM.write(46, (uint8_t)atoi(postToken));
              if (EEPROM.commit()) {
                Serial.println("=================================");
                Serial.println("Resolution was saved");
                Serial.println("=================================");
                char printToHtml[21] = "Resolution are saved";
                memcpy(actionResponseDs, printToHtml, sizeof(printToHtml));
              } else {
                Serial.println("=================================");
                Serial.println("Resolution was NOT saved");
                Serial.println("=================================");
                char printToHtml[21] = "Resolution NOT saved";
                memcpy(actionResponseDs, printToHtml, sizeof(printToHtml));
              }
            }

          }
          //parsing auth hash from client
          if (webTokenSet == true){
            char *webTokenPtr;
            uint8_t webTokenPtrCnt = 0;
            webTokenPtr = strtok(webAuthTokenPreStr, " ");
            webTokenPtrCnt++;
            while (webTokenPtr != NULL) {
              webTokenPtr = strtok(NULL, " ");
              if (webTokenPtrCnt == 2) {
                strcpy(webAuthTokenStr, webTokenPtr);
                break;
              }
              webTokenPtrCnt++;
            }

          }
          //cut CR and LF
          if (webTokenSet == true){

            for(uint8_t z = 0; z < sizeof(webAuthTokenStr); z++) {
              if (webAuthTokenStr[z] == '\n') {
                webAuthTokenStr[z] = '\0';
              }
              if (webAuthTokenStr[z] == '\r') {
                webAuthTokenStr[z] = '\0';
              }
            }
          }

          //parse page address from first string of header
          uint8_t webReqTokenCnt = 0;
          char *webReqToken;
          webReqToken = strtok(getHeaderStr, " ");

          webReqTokenCnt++;
          while(webReqToken != NULL) {
            webReqToken = strtok(NULL, " ");
            if (webReqTokenCnt == 1) {
              strcpy(webPageAddr, webReqToken);
              break;
            }
            webReqTokenCnt++;
          }
          Serial.println("Requested:");
          Serial.println(webPageAddr);
          Serial.print("Free memory: ");
          Serial.print(ESP.getFreeHeap());
          Serial.print(" bytes\n");
          if (strcmp(webPageAddr, "/") == 0) {
            client.println(F("HTTP/1.1 200 OK"));
            client.println(F("Content-Type: text/html"));
            client.println(F("Connection: close"));  // the connection will be closed after completion of the response
            client.println();
            client.println(F("<!DOCTYPE HTML>"));
            client.println(F("<html><head><link rel=\"shortcut icon\" href=\"#\" ><meta http-equiv=\"Refresh\" content=\"30\" ><meta charset=\"utf-8\"><title>Temperature monitoring</title></head><body>"));
            //if you want turn off CSS, start comment here...
            client.println(F("<style type=\"text/css\">"));
            client.println(F("body { background: #FE938C; font-family: Georgia, serif; }"));
            client.println(F("table { border-collapse: collapse; width: 400px; padding: 15px; text-align: center; }"));
            //client.println(F("a { font-family: inherit; }"));
            client.println(F("tr:nth-child(odd) { background: #EAD2AC; }"));
            client.println(F("tr:nth-child(even) { background: #E6B89C; }"));
            client.println(F("thead th, tfoot td { background: #9CAFB7; }"));
            client.println(F("</style>"));
            //...and finish here
            client.println(F("<table><thead><tr><th>Device id</th><th>Serial #</th><th>Temperature</th></th></thead>"));
            client.println(F("<tbody>"));
            for (uint8_t i = 0; i < numberOfDevices; i++) {

              dtostrf(tempDev[i], 2, 1, temperatureString);
              Serial.print("Sending temperature: ");
              Serial.print(temperatureString);
              Serial.print(" sensor ");
              Serial.print(i);
              Serial.print("\n");

              client.println(F("<tr><td>"));
              client.println(i);
              client.println(F("</td><td>"));
              client.println(GetAddressToString(devAddr[i]));
              client.println(F("</td><td>"));
              client.println(temperatureString);
              client.println(F("</td></tr>"));
            }
            client.println(F("</tbody>"));
            client.println(F("<tfoot><tr><td colspan = \"2\" align = \"left\">"));
            client.println(F("Total devices: "));
            client.println(F("</td><td>"));
            client.println(numberOfDevices);
            client.println(F("</tr></td></tfoot></table><p></p>"));

            client.print(F("<table><tfoot><tr><td><a href=\"http://"));
            client.print(Ethernet.localIP());
            client.print(F("/setup\">Setup</a></td></tr></tfoot></table></body></html>"));
          }
          else if (strcmp(webPageAddr, "/setup") == 0) {

            //compare client auth hash with our auth hash
            if (strcmp(base64Str, webAuthTokenStr) == 0) {
              client.println(F("HTTP/1.1 200 OK"));
              client.println(F("Content-Type: text/html"));
              client.println(F("Cache-Control: no-Cache"));
              client.println(F("Connection: keep-alive"));
              client.println();

              client.println(F("<!DOCTYPE HTML>"));
              client.println(F("<html><head><link rel=\"shortcut icon\" href=\"#\" /><meta charset=\"utf-8\"><title>Setup</title></head><body>"));
              //CSS begin here
              client.println(F("<style type=\"text/css\">"));
              client.println(F("body { background: #FE938C; font-family: Georgia, serif; }"));
              client.println(F("table { border-collapse: collapse; width: 300px; padding: 15px;  text-align: center; }"));
              client.println(F("input { font-family: inherit }"));
              client.println(F("tr:nth-child(odd) { background: #EAD2AC; }"));
              client.println(F("tr:nth-child(even) { background: #E6B89C; }"));
              client.println(F("thead th, tfoot td { background: #9CAFB7; }"));
              client.println(F("</style>"));
              //CSS ends here
              client.println(F("<form id=\"change_ip\" method=\"post\"></form>"));
              client.println(F("<table><thead><tr><th colspan = \"100%\" >Network parameters</th></thead>"));
              client.println(F("<tbody><tr><td colspan = \"1\">ip address</td>"));
              client.print(F("<td colspan = \"2\" ><input name=\"ip_a\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\" pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[1-9][0-9]?)$\" placeholder=\""));
              client.print(ip[0]);
              client.print(F("\" required></td>"));
              client.println(F("<td colspan = \"2\"><input name=\"ip_b\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
              client.print(ip[1]);
              client.print(F("\" required></td>"));
              client.println(F("<td colspan = \"2\"><input name=\"ip_c\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
              client.print(ip[2]);
              client.print(F("\" required></td>"));
              client.println(F("<td colspan = \"2\"><input name=\"ip_d\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
              client.print(ip[3]);
              client.print(F("\" required></td></tr>"));
              client.println(F("<tr><td colspan = \"1\">subnet</td>"));
              client.println(F("<td colspan = \"2\"><input name=\"mask_a\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
              client.print(mask[0]);
              client.print(F("\" required></td>"));
              client.println(F("<td colspan = \"2\"><input name=\"mask_b\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
              client.print(mask[1]);
              client.print(F("\" required></td>"));
              client.println(F("<td colspan = \"2\"><input name=\"mask_c\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
              client.print(mask[2]);
              client.print(F("\" required></td>"));
              client.println(F("<td colspan = \"2\"><input name=\"mask_d\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
              client.print(mask[3]);
              client.print(F("\" required></td></tr>"));
              client.println(F("<tr><td colspan = \"1\">gateway</td>"));
              client.println(F("<td colspan = \"2\"><input name=\"gate_a\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[1-9][0-9]?)$\" placeholder=\""));
              client.print(gateway[0]);
              client.print(F("\" required></td>"));
              client.println(F("<td colspan = \"2\"><input name=\"gate_b\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
              client.print(gateway[1]);
              client.print(F("\" required></td>"));
              client.println(F("<td colspan = \"2\"><input name=\"gate_c\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
              client.print(gateway[2]);
              client.print(F("\" required></td>"));
              client.println(F("<td colspan = \"2\"><input name=\"gate_d\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
              client.print(gateway[3]);
              client.print(F("\" required></td></tr>"));
              client.print(F("<tr><td colspan = \"100%\">"));
              client.print(actionResponse);
              client.print(F("</tr></td></tbody>"));
              client.println(F("<tfoot><tr><td colspan = \"100%\"><input type=\"submit\" form=\"change_ip\"  value=\"Apply\"  formmethod=\"post\"></td></tr></tfoot></table><p></p>"));

              client.println(F("<form id=\"change_pass\" method=\"post\"></form>"));
              client.println(F("<table><thead><tr><th colspan = \"100%\">Authentication parameters</th></thead>"));
              client.println(F("<tbody><tr><td colspan = \"1\">username</td><td colspan = \"2\">"));
              client.println(F("<input name=\"login\" type=\"text\" form=\"change_pass\" maxlength=\"16\" size=\"18\" placeholder=\"login\" pattern = \"^[a-zA-Z0-9]{1,16}$\" required></td></tr>"));
              client.println(F("<tr><td colspan = \"1\">password</td><td colspan = \"2\">"));
              client.println(F("<input name=\"password\" type=\"password\" form=\"change_pass\" maxlength=\"16\" size=\"18\" placeholder=\"password\" pattern = \"^[a-zA-Z0-9]{1,16}$\" required></td></tr>"));
              client.println(F("<tr><td colspan = \"100%\">"));
              client.println(actionResponsePwd);
              client.println(F("</td></tr></tbody>"));
              client.println(F("<tfoot><tr><td colspan = \"100%\"><input type=\"submit\" form=\"change_pass\"  value=\"Apply\" formmethod=\"post\"></td></tr></tfoot></table><p></p>"));

              client.println(F("<form id=\"reset_ip\" method=\"post\"></form>"));
              client.println(F("<form id=\"ds_set_res\" method=\"post\"></form>"));
              client.println(F("<table><thead><tr><th colspan = \"100%\">Miscellaneous</th></thead>"));
              client.print(F("<tbody><tr><td colspan = \"50%\">Resolution:<input type=\"text\" name=\"ds_res\" form=\"ds_set_res\" maxlength=\"2\" size=\"2\" pattern=\"^(9|1[0-2])$\" title=\"9...12\" placeholder=\""));
              client.print(dsResolution);
              client.print(F("\" required></td>"));
              client.println(F("<td colspan=\"50%\" align=\"left\" ><input type=\"submit\" form=\"ds_set_res\" value=\"Set\" formmethod=\"post\"></td></tr>"));
              client.println(F("<tr><td colspan = \"100%\">"));
              client.println(actionResponseDs);
              client.println(F("</td></tr>"));
              client.println(F("<tfoot><tr><td colspan = \"100%\"><a href=\"http://"));
              client.print(WiFi.localIP());
              client.println(F("/\">Main page</a></td></tr></tfoot></table><p></p>"));

              client.println(F("<form id=\"reboot_device\" method=\"post\"></form>"));
              client.println(F("<table><thead><tr><th colspan = \"100%\">Reboot device</th></thead>"));
              client.println(F("<tbody><tr><td colspan=\"1\">5+4=<input type=\"text\" name=\"reboot_sum\" form=\"reboot_device\" maxlength=\"2\" size=\"2\" pattern=\"^[9]$\" placeholder=\"9\" required></td>"));
              client.println(F("<td colspan=\"2\"><input type=\"submit\" form=\"reboot_device\" value=\"Reboot\" formmethod=\"post\"></td></tr></tbody>"));
              client.println(F("<tfoot><tr><td colspan=\"100%\">Сhanges will take effect after reboot</td></tr></tfoot></table><p></p>"));

              client.println(F("</body></html>"));


            }
            else {
              client.println(F("HTTP/1.1 401 Unauthorized"));
              client.println(F("WWW-Authenticate: Basic realm=\"Say password\", charset=\"utf-8\""));
            }
            //end of /setup scope
          }
          else if (strcmp(webPageAddr, "/json") == 0) {
            //increase here if there are more than 16 devices
            StaticJsonDocument<1100> doc;
            //String myStr;
            char myStr[2];
            JsonArray dev = doc.createNestedArray("dev");
            for (uint8_t jsonNum = 0; jsonNum < numberOfDevices; jsonNum++) {
              //myStr = String(jsonNum);
              itoa(jsonNum, myStr, 10);

              JsonObject myStr = dev.createNestedObject();
              myStr["id"] = jsonNum;
              myStr["sn"] = GetAddressToString(devAddr[jsonNum]);
              myStr["temp"] = tempDev[jsonNum];

            }

            doc["total"] = numberOfDevices;
            client.println(F("HTTP/1.1 200 OK"));
            client.println(F("Content-Type: application/json"));
            client.println(F("Connection: close"));
            client.print(F("Content-Length: "));
            client.println(measureJsonPretty(doc));
            client.println();

            // Write JSON document
            serializeJsonPretty(doc, client);

          }
          else {
            client.println(F("HTTP/1.1 404 Not Found"));
            client.println(F("Content-Type: text/html"));
            client.println(F("Connection: close"));
            client.println();
          }
          break;
        }

        webBuffIndex++;
        if (c == '\n') {
          lineIsBlank = true;

          //Copying first string of request. Later we'll get the page address we'll get from it
          if (reqStrNum == 0) {
            strcpy(getHeaderStr, webBuff);
            webTokenSet = true;
          }

          //copy auth token from client for compare with our token
          char *webToken;
          webToken = strstr(webBuff, "Authorization: Basic");
          if (webToken != NULL) {
            strcpy(webAuthTokenPreStr, webBuff);
          }

          webBuffIndex = 0;
          memset(webBuff, 0, sizeof(webBuff));
          reqStrNum++;
        }
        else if (c != '\r') {
          lineIsBlank = false;
        }

      }
    }
    delay(30);
    //  memset(reqStr, 0, sizeof(reqStr));
    Serial.println("Web client disconnected");
    client.stop();
  }

  //---------------------------------------------------------------
  // Zabbix agent section
  //If no temperature data to send, report it via telnet
  if(!tempDev[0]) {
    EthernetClient client = zabbixagent.available();
    if (client) {
      Serial.println("Send DS18B20 unavailable!");
      client.println("DS18B20 unavailable!");
      client.stop();
    }
  } else {

    EthernetClient client = zabbixagent.available();
    if (client) {
      Serial.println("new Zabbix client");
      char buffZabbix[64];
      uint8_t buffIndexZabbix;

      //array must initilize for properly handle of first request after boot
      for (buffIndexZabbix = 0; buffIndexZabbix < sizeof(buffZabbix); buffIndexZabbix++) {
        buffZabbix[buffIndexZabbix] = 0;
      }
      char reqStr[16];
      buffIndexZabbix = 0;
      uint8_t reqStrIndex = 0;
      uint8_t strLen;
      bool flag = false;
      while (client.connected()) {

        if (client.available()) {
          char c = client.read();
          buffZabbix[buffIndexZabbix] = c;

          //payload start here
          if (buffZabbix[13] != 0) {
            reqStr[reqStrIndex] = buffZabbix[buffIndexZabbix];
            reqStrIndex++;
            //define data length
            if (flag == false){
              strLen = buffZabbix[5];
              strLen += 12;
              flag = true;
            }
          }
          //client send some payload or garbage
          if (buffIndexZabbix > 0 && strLen > 0 && buffIndexZabbix == strLen || buffZabbix[0] != 'Z') {
            reqStr[reqStrIndex] = '\0';
            Serial.println(reqStr);
            //now copy reqStr and serach for dev #
            char *token;
            char partOfReq[3];
            uint8_t partOfReqInt;
            char dynReq[16] = "env.temp.";
            char reqStrCopy[sizeof(reqStr)];
            strcpy(reqStrCopy, reqStr);
            token = strtok(reqStrCopy, ".");
            for (uint8_t j = 1; j < 3; j++)
            {
              token = strtok(NULL, ".");
              if (j == 2 && token != NULL) {
                partOfReqInt = atoi(token);
                strcpy(partOfReq, token);
              }
            }
            //add dynamic request to the end of template
            for (uint8_t k = 0; k < sizeof(partOfReq); k++) {
              dynReq[9 + k] = partOfReq[k];
            }

            if (strcmp(reqStr, "agent.ping") == 0) {
              Serial.println("Request:");
              Serial.println(reqStr);
              Serial.println("send agent.ping = 1");
              client.print("ZBXD\x01");
              uint8_t responseuint8_ts [] = {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, '1'};
              client.write(responseuint8_ts, 9);
              client.stop();
              //check env.temp.n
            } else if (strcmp(reqStr, dynReq) == 0) {
              Serial.println("Request:");
              Serial.println(reqStr);
              if (partOfReqInt <= (numberOfDevices - 1)) {
                Serial.println("sending temp");
                client.print("ZBXD\x01");
                int tempStr = round(tempDev[partOfReqInt]);
                //counting the number of characters in number
                uint8_t j = 1;
                //Handle if t < 0
                uint8_t minusTempStr = false;
                if (tempStr < 0) {
                  tempStr = -tempStr;
                  j++;
                  minusTempStr = true;
                }
                //count int lenght
                uint8_t devide = tempStr / 10;
                while (devide > 0) {
                  j++;
                  devide /= 10;
                }
                if (minusTempStr == true){
                  tempStr = -tempStr;
                }
                uint8_t responseuint8_ts [] = {j, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
                client.write(responseuint8_ts, 8);
                client.print(tempStr);
                Serial.println(tempStr);
                client.stop();
              } else {
                Serial.println(reqStr);
                Serial.println("Sending ZBX_NOTSUPPORTED, No such sensor");
                client.print("ZBXD\x01");
                uint8_t responseuint8_ts [] = {0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
                client.write(responseuint8_ts, 8);
                client.print("ZBX_NOTSUPPORTED\0No such sensor");
                client.stop();
              }

            } else {
              Serial.println(reqStr);
              Serial.println("Sending ZBX_NOTSUPPORTED");
              client.print("ZBXD\x01");
              uint8_t responseuint8_ts [] = {0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
              client.write(responseuint8_ts, 8);
              client.print("ZBX_NOTSUPPORTED");
              client.stop();

            }
            break;
          }
          buffIndexZabbix++;

        }

      }
      delay(10);
      // close the connection:
      client.stop();
      //clear data arrays
      memset(buffZabbix, 0, sizeof(buffZabbix));
      memset(reqStr, 0, sizeof(reqStr));
      Serial.println("Zabbix client disconnected");
    }
  }
  //bellow code make some pseudo random values for session id, will use with cookie
  //if (millis() - now > 5000) {
  //  long now = millis();
  //  for (uint8_t k = 0; k < 16; k++) {
  //randNumber = random(33, 126);
  //char randChar = randNumber;
  //Serial.println(randNumber);
  //Serial.print(randChar);

  //}
  //Serial.print("\n");
  //  delay(100);
  //  }

}
