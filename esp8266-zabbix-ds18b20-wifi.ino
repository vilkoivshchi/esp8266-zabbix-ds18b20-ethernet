#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP8266WiFi.h>
#include <rBase64.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <Adafruit_AHT10.h>


//Pin to which is attached a DS18B20 sensor
#define ONE_WIRE_BUS D4


//The maximum number of devices
const uint8_t ONE_WIRE_MAX_DEV = 16;

Adafruit_AHT10 aht;

//The resolution of the DS18B20 is configurable (9, 10, 11, or 12 bits), with 12-bit readings the factory default state. This
// equates to a temperature resolution of 0.5°C, 0.25°C, 0.125°C, or 0.0625°C.
const uint8_t defDsResolution = 9;
uint8_t dsResolution = 9;

const char apSsid[] = "ESP8266";
const char* defaultSsid = "your_ssid";
const char* defaultWifiPass = "your_pass";

char ssid[32];
char WiFiPassword[32];
uint8_t ssidLenght = sizeof(ssid);
uint8_t wifiPwdLen = sizeof(WiFiPassword);

bool WifiSta = true;

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
long lastTempAht;
//The frequency of temperature measurement in ms
const uint16_t durationTemp = 5000;
char temperatureString[6];

uint8_t currentIp[] = { 0, 0, 0, 0 };
uint8_t nameserver[] = { 8, 8, 4, 4 };
uint8_t gateway[] = { 0, 0, 0, 0 };
uint8_t mask[] = { 0, 0, 0, 0 };

//Default network settings
const uint8_t defaultIp[] = { 192, 168, 1, 200 };
const uint8_t defaultGateway[] = { 192, 168, 1, 1 };
const uint8_t defaultMask[] = { 255, 255, 255, 0 };

const char defaultLogin[17] = "admin";
const char defaultPassword[17] = "admin";


char login[17];
char password[17];

//this var need when you will change AP
uint8_t wifiConnRes = 6;

uint8_t loginLength = sizeof(login);
uint8_t passwordLength = sizeof(password);

//const int eeprom_size = 47;
/*
0...3 == IP
4...7 == subnet
8...1 == gateway
12...28 == web username
29...45 == web password
46 == DS18B20 resolution (common)
47...78 == SSID
79...110 == WiFi pass
111 == CH01 relay state
*/
//const uint8_t ch01_state_addr = 111;
const int eeprom_size = 128;

//var for relay state
//bool relay_ch01_check;

char base64Str[32];

WiFiServer webserver(80);

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

void WifiSetup(const char* staSsid, const char* staPass, uint8_t staLocalIp[], uint8_t staNameServer[], uint8_t staGw[], uint8_t staMask[], const char apSSID[], uint8_t apSSIDSize )
{

  WiFi.mode(WIFI_STA);
  WiFi.config(staLocalIp, staGw, staMask, staNameServer);
  WiFi.begin(staSsid, staPass);
  uint8_t WifiConnectAttempts = 10;
  Serial.println();
  Serial.print("Connecting to ");
  Serial.print(staSsid);
  Serial.println();
  for(uint8_t i = 0; i < WifiConnectAttempts; i++)
  {

      if(WiFi.status() != WL_CONNECTED)
      {
          delay(1000);
          Serial.print(".");
      }
      else
      {
          Serial.println("Connected to " + WiFi.SSID());
          delay(1000);
          Serial.print("Local IP: ");
          Serial.println(WiFi.localIP());
          WifiSta = true;
          break;
      }
  }
  if(WiFi.status() != WL_CONNECTED)
  {
    WiFi.mode(WIFI_AP);

    WiFi.softAPConfig(staLocalIp, staGw, staMask);
    WiFi.softAP(apSSID);
    Serial.println(WiFi.softAPIP());
    WifiSta = false;
    Serial.print("Cant'connect. ");
    for (uint8_t i = 0; i < sizeof(apSsid); i++)
    {
      Serial.print(apSsid[i]);
    }
    Serial.print(" was created. IP: ");
    for (size_t i = 0; i < sizeof(staLocalIp); i++) {
      Serial.print(staLocalIp[i]);
    }
    Serial.println();
  }

}



unsigned long previousMillis = 0;
unsigned long interval = 1000;
int tachoCounter = 0;
int currentRpm = 0;
//Loop measuring the temperature
void TempLoop(long now)
{
  if ( now - lastTemp > durationTemp )
  {
    for (int i = 0; i < numberOfDevices; i++)
    {
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

int ahtMeasurement[2] = {0,0};
bool isAhtPresent = false;

bool SetupAHT10()
{
  bool ahtFound = false;
  Serial.println("Looking for AHT10...");
  if(aht.begin())
  {
    Serial.println("AHT10 found!");
    ahtFound = true;
  }
  else
  {
    Serial.println("Could not find AHT10? Check wiring");
  }
  return ahtFound;
}

void ahtMeasurementLoop(long now)
{
  if ( now - lastTempAht > durationTemp )
  {
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);
    ahtMeasurement[0] = round(temp.temperature);
    ahtMeasurement[1] = round(humidity.relative_humidity);
    lastTempAht = millis();

  }
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


//normally not used, let to see what in EEPROM.
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
    currentIp[ipAddrIndex] = EEPROM.read(address);
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
  if (currentIp[0] >= 224) {
    Serial.println("===============================");
    Serial.println("Using default IP");
    Serial.println("===============================");
    for (ipAddrIndex = 0; ipAddrIndex < 4; ipAddrIndex++){
      currentIp[ipAddrIndex] = defaultIp[ipAddrIndex];
      mask[ipAddrIndex] = defaultMask[ipAddrIndex];
      gateway[ipAddrIndex] = defaultGateway[ipAddrIndex];
    }
    //and write default IP to EEPROM
    ipAddrIndex = 0;
    for (address = 0; address < 4; address++){
      Serial.println(defaultIp[ipAddrIndex]);
      EEPROM.write(address, defaultIp[ipAddrIndex]);
      ipAddrIndex++;
    }
    ipAddrIndex = 0;
    for (address = 4; address < 8; address++){
      Serial.println(defaultMask[ipAddrIndex]);
      EEPROM.write(address, defaultMask[ipAddrIndex]);
      ipAddrIndex++;
    }
    ipAddrIndex = 0;
    for (address = 8; address < 12; address++){
      Serial.println(defaultGateway[ipAddrIndex]);
      EEPROM.write(address, defaultGateway[ipAddrIndex]);
      ipAddrIndex++;
    }
    if (EEPROM.commit()) {
      Serial.println("Default IP stored to EEPROM");
    }
    else {
      Serial.println("Default IP NOT stored to EEPROM");
    }

  }
}

void choosePasswordConfig() {
  uint8_t address = 12;
  for (uint8_t k = 0; k < loginLength; k++) {
    login[k] = EEPROM.read(address);
    address++;
  }
  for (uint8_t k = 0; k < loginLength; k++) {
    password[k] = EEPROM.read(address);
    address++;
  }
  if(login[0] == 255 || password[0] == 255) {

    address = 12;
    for (uint8_t k = 0; k < loginLength; k++) {
      login[k] = defaultLogin[k];
      password[k] = defaultPassword[k];
    }

    for (uint8_t k = 0; k < loginLength; k++) {
      EEPROM.write(address, login[k]);
      address++;
    }

    for (uint8_t k = 0; k < loginLength; k++) {
      EEPROM.write(address, password[k]);
      address++;
    }

    if (EEPROM.commit()) {
      Serial.println("Login/Password admin:admin saved");
    }
    else {
      Serial.println("Login/Password admin:admin NOT saved");
    }
  }
}

//normally not used, let erase EEPROM.
void clearEEPROM() {
  for (uint8_t address = 0; address < eeprom_size; address++) {
    EEPROM.write(address, 255);
  }
  EEPROM.commit();
  Serial.println("=================================");
  Serial.println("EEPROM cleaed!");
  Serial.println("=================================");
}


//set default DS18B20 resolution
void chooseDsRes() {

  uint8_t address = 46;
  uint8_t userDsRes = EEPROM.read(address);
  if (userDsRes < 9 || userDsRes > 12) {
    dsResolution = defDsResolution;
    EEPROM.write(address, 9);
    if (EEPROM.commit()) {
      Serial.println("DS18B20 resolution set to 9");
    }
    else {
      Serial.println("Can't set DS18B20 resolution");
    }
  } else {
    dsResolution = userDsRes;
  }
}

void chooseWifi() {
  uint8_t eeprom_offset = 47;
  uint8_t checkSsid = EEPROM.read(eeprom_offset);

  if (checkSsid == 0 || checkSsid == 255) {
    strcpy(ssid, defaultSsid);
    strcpy(WiFiPassword, defaultWifiPass);
  }
  else {
    for (uint8_t i = 0; i < ssidLenght; i++) {
      ssid[i] = EEPROM.read(eeprom_offset);
      eeprom_offset++;
    }
    for (uint8_t i = 0; i < wifiPwdLen; i++) {
      WiFiPassword[i] = EEPROM.read(eeprom_offset);
      eeprom_offset++;
    }
  }
}

void WebPageRoot(WiFiClient client)
{
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
  client.println(F(".button { text-align: center; cursor: pointer; font-family: inherit; width: 400px; width: 400px; padding: 15px; }"));
  //client.println(F(".button1 { padding: 12px 400px; }"));
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
  client.println(F("</tr></td></tfoot></table>"));

  client.print(F("<table><tfoot><tr><td><a href=\"http://"));
  client.print(WiFi.localIP());

  client.print(F("/setup\">Setup</a></td></tr></tfoot></table></body></html>"));
}

void WebPageSetup(WiFiClient client)
{

}

void setup() {
  Serial.begin(115200);
  //Enable EEPROM
  EEPROM.begin(eeprom_size);
  //clearEEPROM();

  chooseNetConfig();
  chooseDsRes();
  choosePasswordConfig();
  chooseWifi();

  //show EEPROM data in console at startup
  eepromReadData();
  WifiSetup(ssid, WiFiPassword, currentIp, nameserver, gateway, mask, apSsid, sizeof(apSsid));

  delay(200);
  webserver.begin();

  //Setup DS18b20 temperature sensor
  SetupDS18B20();
  isAhtPresent = SetupAHT10();

  memset(base64Str, 0, sizeof(base64Str));
  encodeAuthData(login, loginLength, password, passwordLength);
  Serial.println("Boot complete");
}

void loop()
{
  unsigned long currentMillis = millis();

  long t = millis();
  //Periodic measurement of temperature
  TempLoop( t );
  if(isAhtPresent)
  {
    ahtMeasurementLoop(t);
  }
  //---------------------------------------------------------------
  // Web server section

  WiFiClient client = webserver.available();
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

    char userSsid[32];
    memset(userSsid, 0, sizeof(userSsid));

    bool isIpChanged = false;

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

            char userWifiPass[32];
            memset(userWifiPass, 0, sizeof(userWifiPass));

            uint8_t ipToSaveIndex = 0;
            uint8_t maskToSaveIndex = 0;
            uint8_t gateipToSaveIndex = 0;

            uint8_t postBodyIndex = 0;


            char* postToken;
            //Serial.println("postBody");
            //Serial.println(postBody);
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
                /*
                Serial.println("=================================");
                Serial.println("Network settings are saved");
                Serial.println("=================================");
                */
                char printToHtml[27] = "Network settings are saved";
                memcpy(actionResponse, printToHtml, sizeof(printToHtml));
                isIpChanged = true;
              } else {
                /*
                Serial.println("=================================");
                Serial.println("Network settings NOT saved");
                Serial.println("=================================");
                */
                char printToHtml[27] = "Network settings NOT saved";
                memcpy(actionResponse, printToHtml, sizeof(printToHtml));
              }
              //Authentication change block
            }
            else if (strcmp(postToken, "login") == 0) {
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
                /*
                Serial.println("=================================");
                Serial.println("Password was saved");
                Serial.println("=================================");
                */
                char printToHtml[27] = "Password are saved";
                memcpy(actionResponsePwd, printToHtml, sizeof(printToHtml));
              }
              else {
                /*
                Serial.println("=================================");
                Serial.println("Password was NOT saved");
                Serial.println("=================================");
                */
                char printToHtml[27] = "Password NOT saved";
                memcpy(actionResponsePwd, printToHtml, sizeof(printToHtml));
              }

              //Reset button block
              //redirect browser and reboot device
            }
            else if (strcmp(postToken, "reboot_sum") == 0) {
              Serial.println("=================================");
              Serial.println("Reboot by user");
              Serial.println("=================================");
              client.println(F("HTTP/1.1 200 OK"));
              client.println(F("Content-Type: text/html"));
              client.println(F("Connection: close"));
              client.println();
              client.println(F("<!DOCTYPE HTML>"));
              client.print(F("<html><head><link rel=\"shortcut icon\" href=\"#\"><meta http-equiv=\"refresh\" content=\"10;url=http://"));
              client.print(EEPROM.read(0));
              client.print(".");
              client.print(EEPROM.read(1));
              client.print(".");
              client.print(EEPROM.read(2));
              client.print(".");
              client.print(EEPROM.read(3));
              client.print(F("/setup\"></head><body><style type=\"text/css\">body { background: #FE938C; font-family: Georgia, serif; }</style>Device rebooting. You will be redirect after 10 seconds. Or click <a href=\"http://"));
              client.print(EEPROM.read(0));
              client.print(".");
              client.print(EEPROM.read(1));
              client.print(".");
              client.print(EEPROM.read(2));
              client.print(".");
              client.print(EEPROM.read(3));
              client.print(F("/setup\">here</a>. Please stand by...</body></html>"));
              client.println();
              client.stop();

              ESP.restart();
              //DS18B20 resolution change
            }
            else if (strcmp(postToken, "ds_res") == 0) {
              postToken = strtok(NULL, "&=");
              EEPROM.write(46, (uint8_t)atoi(postToken));
              if (EEPROM.commit()) {
                /*
                Serial.println("=================================");
                Serial.println("Resolution was saved");
                Serial.println("=================================");
                */
                char printToHtml[21] = "Resolution are saved";
                memcpy(actionResponseDs, printToHtml, sizeof(printToHtml));
              } else {
                /*
                Serial.println("=================================");
                Serial.println("Resolution was NOT saved");
                Serial.println("=================================");
                */
                char printToHtml[21] = "Resolution NOT saved";
                memcpy(actionResponseDs, printToHtml, sizeof(printToHtml));
              }

            }


            //Copy even result, becuse POST contain key=value
            else if (strcmp(postToken, "ssid") == 0) {
              for (postBodyIndex = 1; postBodyIndex < 4; postBodyIndex++) {
                postToken = strtok(NULL, "&=");

                if (postBodyIndex == 1) {

                  strcpy(userSsid, postToken);
                  //POST brings SPACE as "+", get back to SPACE
                  for (uint8_t l = 0; l < sizeof(userSsid); l++) {
                    if(userSsid[l] == '+') {
                      userSsid[l] = ' ';
                    }
                  }

                  char *leader = userSsid;
                  char *follower = leader;
                  //Thx for this function https://arduino.stackexchange.com/questions/18007/simple-url-decoding/
                  // While we're not at the end of the string (current character not NULL)
                  while (*leader) {
                    // Check to see if the current character is a %
                    if (*leader == '%') {

                      // Grab the next two characters and move leader forwards
                      leader++;
                      char high = *leader;
                      leader++;
                      char low = *leader;

                      // Convert ASCII 0-9A-F to a value 0-15
                      if (high > 0x39) high -= 7;
                      high &= 0x0f;

                      // Same again for the low byte:
                      if (low > 0x39) low -= 7;
                      low &= 0x0f;

                      // Combine the two into a single byte and store in follower:
                      *follower = (high << 4) | low;
                    } else {
                      // All other characters copy verbatim
                      *follower = *leader;
                    }

                    // Move both pointers to the next character:
                    leader++;
                    follower++;
                  }
                  // Terminate the new string with a NULL character to trim it off
                  *follower = 0;

                }

                for (uint8_t l = 0; l < sizeof(userSsid); l++) {
                  if(userSsid[l] == '\0') {
                    userSsid[l + 1] = '\0';
                  }
                }

                if (postBodyIndex == 3) {
                  if (!postToken) {
                    Serial.println("user password is empty");
                    userWifiPass[0]='\0';
                  }
                  else {
                    strcpy(userWifiPass, postToken);
                  }
                }
              }

              client.println(F("HTTP/1.1 200 OK"));
              client.println(F("Content-Type: text/html"));
              client.println(F("Connection: keep-alive"));
              client.println();
              client.println(F("<!DOCTYPE HTML>"));
              client.print(F("<html><head><link rel=\"shortcut icon\" href=\"#\"><meta http-equiv=\"Refresh\" content=\"10;url=http://"));
              client.print(WiFi.localIP());
              client.print(F("/wificheckresult\"></head><body><style type=\"text/css\">body { background: #FE938C; font-family: Georgia, serif; }</style>Checking connection with "));
              client.print(userSsid);
              client.print(F(". You will be redirected after 10 seconds. Please stand by...</body></html>"));
              client.println();
              client.stop();
              WiFi.disconnect(0);
              WiFi.mode(WIFI_STA);
              WiFi.begin(userSsid, userWifiPass);


              if (WiFi.waitForConnectResult() == WL_CONNECTED) {
                wifiConnRes = 0;
                //define adress of login in EEPROM;

                ipToSaveIndex = 47;
                for (uint8_t k; k < sizeof(userSsid); k++) {
                  EEPROM.write(ipToSaveIndex, userSsid[k]);
                  ipToSaveIndex++;
                }

                for (uint8_t k; k < sizeof(userWifiPass); k++) {
                  EEPROM.write(ipToSaveIndex, userWifiPass[k]);
                  ipToSaveIndex++;
                }

                if (EEPROM.commit()) {

                  Serial.println("=================================");
                  Serial.println("SSID was saved");
                  Serial.println("=================================");

                }
                else {
                  Serial.println("=================================");
                  Serial.println("SSID was NOT saved");
                  Serial.println("=================================");
                }
              }
              else if (WiFi.waitForConnectResult() == WL_NO_SSID_AVAIL) {
                wifiConnRes = 1;
              }
              else if (WiFi.waitForConnectResult() == WL_CONNECT_FAILED) {
                wifiConnRes = 2;
              }
              else if (WiFi.waitForConnectResult() == WL_IDLE_STATUS) {
                wifiConnRes = 3;
              }
              else if (WiFi.waitForConnectResult() == WL_DISCONNECTED) {
                wifiConnRes = 4;
              }
              else if (WiFi.waitForConnectResult() == -1) {
                wifiConnRes = 5;
              }
              Serial.println("=================================");
              Serial.println("Connect back.");
              Serial.println("=================================");
              WiFi.disconnect(0);
              if(WifiSta == true)
              {
                WiFi.begin(ssid, WiFiPassword);

                while (WiFi.status() != WL_CONNECTED) {
                  delay(500);
                  Serial.println(WiFi.status());
                }
                ESP.restart();
              }
              else
              {
                WiFi.softAPConfig(currentIp, gateway, mask);
                WiFi.softAP(apSsid);
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
          if (strcmp(webPageAddr, "/") == 0)
          {
            WebPageRoot(client);
          }
//#region Setup
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
              client.println(F("table { border-collapse: collapse; table-layout: fixed; width: 300px; padding: 15px;  text-align: center; }"));
              client.println(F("input { font-family: inherit }"));
              client.println(F("tr:nth-child(odd) { background: #EAD2AC; }"));
              client.println(F("tr:nth-child(even) { background: #E6B89C; }"));
              client.println(F("thead th, tfoot td { background: #9CAFB7; }"));
              client.println(F("</style>"));
              //CSS ends here
              client.println(F("<form id=\"change_ip\" method=\"post\"></form>"));
              client.println(F("<table><thead><tr><th colspan = \"100%\" >Network parameters</th></thead>"));
              client.println(F("<tbody><tr><td colspan = \"40%\">ip address</td>"));
              client.print(F("<td colspan = \"15%\" ><input name=\"ip_a\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\" pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[1-9][0-9]?)$\" placeholder=\""));
              client.print(currentIp[0]);
              client.print(F("\" required></td>"));
              client.println(F("<td colspan = \"15%\"><input name=\"ip_b\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
              client.print(currentIp[1]);
              client.print(F("\" required></td>"));
              client.println(F("<td colspan = \"15%\"><input name=\"ip_c\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
              client.print(currentIp[2]);
              client.print(F("\" required></td>"));
              client.println(F("<td colspan = \"15%\"><input name=\"ip_d\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
              client.print(currentIp[3]);
              client.print(F("\" required></td></tr>"));
              client.println(F("<tr><td colspan = \"40%\">subnet</td>"));
              client.println(F("<td colspan = \"15%\"><input name=\"mask_a\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
              client.print(mask[0]);
              client.print(F("\" required></td>"));
              client.println(F("<td colspan = \"15%\"><input name=\"mask_b\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
              client.print(mask[1]);
              client.print(F("\" required></td>"));
              client.println(F("<td colspan = \"15%\"><input name=\"mask_c\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
              client.print(mask[2]);
              client.print(F("\" required></td>"));
              client.println(F("<td colspan = \"15%\"><input name=\"mask_d\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
              client.print(mask[3]);
              client.print(F("\" required></td></tr>"));
              client.println(F("<tr><td colspan = \"40%\">gateway</td>"));
              client.println(F("<td colspan = \"15%\"><input name=\"gate_a\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[1-9][0-9]?)$\" placeholder=\""));
              client.print(gateway[0]);
              client.print(F("\" required></td>"));
              client.println(F("<td colspan = \"15%\"><input name=\"gate_b\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
              client.print(gateway[1]);
              client.print(F("\" required></td>"));
              client.println(F("<td colspan = \"15%\"><input name=\"gate_c\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
              client.print(gateway[2]);
              client.print(F("\" required></td>"));
              client.println(F("<td colspan = \"15%\"><input name=\"gate_d\" type=\"text\" form=\"change_ip\" maxlength=\"3\" size=\"3\"  pattern=\"^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$\" placeholder=\""));
              client.print(gateway[3]);
              client.print(F("\" required></td></tr>"));
              client.print(F("<tr><td colspan = \"100%\">"));
              client.print(actionResponse);
              client.print(F("</tr></td></tbody>"));
              client.println(F("<tfoot><tr><td colspan = \"100%\"><input type=\"submit\" form=\"change_ip\"  value=\"Apply\"  formmethod=\"post\"></td></tr></tfoot></table><p></p>"));

              client.println(F("<form id=\"change_pass\" method=\"post\"></form>"));
              client.println(F("<table><thead><tr><th colspan = \"100%\">Authentication parameters</th></thead>"));
              client.println(F("<tbody><tr><td colspan = \"40%\">username</td><td colspan = \"60%\">"));
              client.println(F("<input name=\"login\" type=\"text\" form=\"change_pass\" maxlength=\"16\" size=\"18\" placeholder=\"login\" pattern = \"^[a-zA-Z0-9]{1,16}$\" required></td></tr>"));
              client.println(F("<tr><td colspan = \"40%\">password</td><td colspan = \"60%\">"));
              client.println(F("<input name=\"password\" type=\"password\" form=\"change_pass\" maxlength=\"16\" size=\"18\" placeholder=\"password\" pattern = \"^[a-zA-Z0-9]{1,16}$\" required></td></tr>"));
              client.println(F("<tr><td colspan = \"100%\">"));
              client.println(actionResponsePwd);
              client.println(F("</td></tr></tbody>"));
              client.println(F("<tfoot><tr><td colspan = \"100%\"><input type=\"submit\" form=\"change_pass\"  value=\"Apply\" formmethod=\"post\"></td></tr></tfoot></table><p></p>"));

              client.println(F("<form id=\"reset_ip\" method=\"post\"></form>"));
              client.println(F("<form id=\"ds_set_res\" method=\"post\"></form>"));
              client.println(F("<table><thead><tr><th colspan = \"100%\">Miscellaneous</th></thead>"));
              client.print(F("<tbody><tr><td colspan = \"60%\">Resolution:<input type=\"text\" name=\"ds_res\" form=\"ds_set_res\" maxlength=\"2\" size=\"2\" pattern=\"^(9|1[0-2])$\" title=\"9...12\" placeholder=\""));
              client.print(dsResolution);
              client.print(F("\" required></td>"));
              client.println(F("<td colspan = \"15%\">  </td><td colspan=\"25%\"><input type=\"submit\" form=\"ds_set_res\" value=\"Set\" formmethod=\"post\"></td></tr>"));
              client.println(F("<tr><td colspan = \"60%\">"));
              client.println(ssid);
              client.println(F("</td><td colspan = \"15%\">"));
              client.println(WiFi.RSSI());
              client.println(F("</td><td colspan = \"25%\"><a href=\"http://"));
              client.print(WiFi.localIP());
              client.println(F("/wifiscan\">Change</a></td></tr>"));
              client.println(F("<tr><td colspan = \"100%\">"));
              client.println(actionResponseDs);
              client.println(F("</td></tr>"));
              client.println(F("<tfoot><tr><td colspan = \"100%\"><a href=\"http://"));
              client.print(WiFi.localIP());
              client.println(F("/\">Main page</a></td></tr></tfoot></table><p></p>"));

              client.println(F("<form id=\"reboot_device\" method=\"post\"></form>"));
              client.println(F("<table><thead><tr><th colspan = \"100%\">Reboot device</th></thead>"));
              client.println(F("<tbody><tr><td colspan=\"50%\">5+4=<input type=\"text\" name=\"reboot_sum\" form=\"reboot_device\" maxlength=\"2\" size=\"2\" pattern=\"^[9]$\" placeholder=\"9\" required></td>"));
              client.println(F("<td colspan = \"20%\">  </td><td colspan=\"15%\"><input type=\"submit\" form=\"reboot_device\" value=\"Reboot\" formmethod=\"post\"></td></td><td colspan = \"15%\">   </td></tr></tbody>"));
              client.println(F("<tfoot><tr><td colspan=\"100%\">Сhanges will take effect after reboot</td></tr></tfoot></table><p></p>"));

              client.println(F("</body></html>"));


            }
            else {
              client.println(F("HTTP/1.1 401 Unauthorized"));
              client.println(F("WWW-Authenticate: Basic realm=\"Say password\", charset=\"utf-8\""));
            }
//end of /setup scope
          }
//#endregion
//#region JSON
          else if (strcmp(webPageAddr, "/json") == 0) {
            //increase here if there are more than 16 devices
            StaticJsonDocument<1100> doc;
            //String myStr;
            char myStr[2];
            JsonArray dev = doc.createNestedArray("dev");
            for (uint8_t jsonNum = 0; jsonNum < numberOfDevices; jsonNum++)
            {
              itoa(jsonNum, myStr, 10);
              JsonObject myStr = dev.createNestedObject();
              myStr["id"] = jsonNum;
              myStr["sn"] = GetAddressToString(devAddr[jsonNum]);
              myStr["temp"] = tempDev[jsonNum];
            }
            JsonObject aht = doc.createNestedObject("aht");
            aht["temp"] = ahtMeasurement[0];
            aht["humidity"] = ahtMeasurement[1];

            //doc["ch_01"] = relay_ch01_check;
            client.println(F("HTTP/1.1 200 OK"));
            client.println(F("Content-Type: application/json"));
            client.println(F("Connection: close"));
            client.print(F("Content-Length: "));
            client.println(measureJsonPretty(doc));
            client.println();

            // Write JSON document
            serializeJsonPretty(doc, client);
            delay(50);
            client.stop();
//#endregion
          }
          else if (strcmp(webPageAddr, "/wifiscan") == 0) {
            if (strcmp(base64Str, webAuthTokenStr) == 0) {
              client.println(F("HTTP/1.1 200 OK"));
              client.println(F("Content-Type: text/html"));
              client.println(F("Cache-Control: no-Cache"));
              client.println(F("Connection: keep-alive"));
              client.println();

              client.println(F("<!DOCTYPE HTML>"));
              client.println(F("<html><head><link rel=\"shortcut icon\" href=\"#\" /><meta charset=\"utf-8\"><title>SSID list</title></head><body>"));
              //CSS begin here
              client.println(F("<style type=\"text/css\">"));
              client.println(F("body { background: #FE938C; font-family: Georgia, serif; }"));
              client.println(F("table { border-collapse: collapse; table-layout: fixed; width: 500px; padding: 5px; text-align: center; }"));
              client.println(F("table td { width: inherit; }"));
              client.println(F("input { font-family: inherit; }"));
              client.println(F("tr:nth-child(odd) { background: #EAD2AC; }"));
              client.println(F("tr:nth-child(even) { background: #E6B89C; }"));
              client.println(F("thead th, tfoot td { background: #9CAFB7; width: inherit; }"));
              client.println(F("</style>"));
              //CSS ends here
              client.println(F("<form id=\"ssid_choise\"  method=\"post\"></form>"));
              client.println(F("<table><thead><tr><th colspan = \"70%\">SSID</th><th colspan = \"15%\">RSSI</th><th colspan = \"15%\">Pwd</th></thead>"));
              client.println(F("<tbody>"));

              uint8_t n = WiFi.scanNetworks();

              if (n == 0) {
                client.println(F("<tr><td colspan = \"100%\">No networks found</td></tr></table></body></html>"));
              }
              else {

                for (int i = 0; i < n; ++i) {
                  // Print SSID and RSSI for each network found
                  client.println(F("<tr><td colspan = \"70%\" align=\"left\">"));
                  client.println(F("<input type=\"radio\" form=\"ssid_choise\" name=\"ssid\""));
                  client.print(F(" value=\""));
                  client.print(WiFi.SSID(i));
                  client.print(F("\" required><label for=\"ssid\">"));
                  client.print(WiFi.SSID(i));
                  client.println(F("</label>"));
                  client.println(F("</td><td colspan = \"15%\">"));
                  client.print(WiFi.RSSI(i));
                  client.println(F("</td><td colspan = \"15%\">"));
                  client.println((WiFi.encryptionType(i) == ENC_TYPE_NONE) ? "no" : "yes");
                  client.println(F("</td></tr>"));
                }
                client.println(F("<tr><td colspan=\"30%\" align=\"left\">&emsp; Password: </td><td colspan=\"55%\"><input type=\"password\" name=\"wifipass\" form=\"ssid_choise\" maxlength=\"32\" size=\"32\"></td>"));
                client.println(F("<td colspan = \"15%\"><input type=\"submit\" form=\"ssid_choise\" value=\"Set\" formmethod=\"post\"></td></tr>"));
                client.println(F("</tbody><tfoot><tr><td colspan = \"85%\"align=\"left\">&emsp; Total networks:</td><td colspan = \"15%\">"));
                client.print(n);
                client.println(F("</td></tr></table><p></p>"));
                client.print(F("<table><tfoot><tr><td colspan=\"50%\"><a href=\"http://"));
                client.print(WiFi.localIP());
                client.print(F("/setup\">Back to Setup</a></td><td colspan = \"50%\"><a href=\"http://"));
                client.print(WiFi.localIP());
                client.println(F("/wifiscan\">Refresh</a></td></tr></tfoot></table></body></html><p></p>"));
              }
            }
            //end of Authentication block
            else {
              client.println(F("HTTP/1.1 401 Unauthorized"));
              client.println(F("WWW-Authenticate: Basic realm=\"Say password\", charset=\"utf-8\""));
            }

          }
          else if (strcmp(webPageAddr, "/wificheckresult") == 0) {
            client.println(F("HTTP/1.1 200 OK"));
            client.println(F("Content-Type: text/html"));
            client.println(F("Cache-Control: no-Cache"));
            client.println(F("Connection: close"));
            client.println();

            client.println(F("<!DOCTYPE HTML>"));
            client.println(F("<html><head><link rel=\"shortcut icon\" href=\"#\" /><meta charset=\"utf-8\"><title>SSID list</title></head><body>"));
            //CSS begin here
            client.println(F("<style type=\"text/css\">"));
            client.println(F("body { background: #FE938C; font-family: Georgia, serif; }"));
            client.println(F("</style>"));
            //CSS ends here
            Serial.print("Wifi status: ");
            Serial.print(wifiConnRes);
            Serial.print("\n");
            if (wifiConnRes == 0) {
              client.print(F("Connect to "));
              client.print(userSsid);
              client.print(F(" successful. SSID and password stored. <a href=\"http://"));
              client.print(WiFi.localIP());
              client.print(F("/setup\">Check IP</a> and reboot device.</body></html>"));
            }
            else if (wifiConnRes == 1) {
              client.print(F("Connect to "));
              client.print(userSsid);
              client.print(F("fail. SSID cannot be reached. <a href=\"http://"));
              client.print(WiFi.localIP());
              client.print(F("/wifiscan\">Try again.</a></body></html>"));

            }
            else if (wifiConnRes == 2) {
              client.print(F("Connect to "));
              client.print(userSsid);
              client.print(F("fail. Password is incorrect. <a href=\"http://"));
              client.print(WiFi.localIP());
              client.print(F("/wifiscan\">Try again.</a></body></html>"));

            }
            else if (wifiConnRes == 3) {
              client.print(F("Connect to "));
              client.print(userSsid);
              client.print(F("fail. Wi-Fi is in process of changing between statuses. <a href=\"http://"));
              client.print(WiFi.localIP());
              client.print(F("/wifiscan\">Try again.</a></body></html>"));

            }
            else if (wifiConnRes == 4) {
              client.print(F("Connect to "));
              client.print(userSsid);
              client.print(F("fail. Module is not configured in station mode. <a href=\"http://"));
              client.print(WiFi.localIP());
              client.print(F("/wifiscan\">Try again.</a></body></html>"));

            }
            else if (wifiConnRes == 5) {
              client.print(F("Connect to "));
              client.print(userSsid);
              client.print(F("fail. Timeout. <a href=\"http://"));
              client.print(WiFi.localIP());
              client.print(F("/wifiscan\">Try again.</a></body></html>"));

            }
            else {
              client.print(F("Something went wrong. <a href=\"http://"));
              client.print(WiFi.localIP());
              client.print(F("/wifiscan\">Try again.</a></body></html>"));
            }
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

}
