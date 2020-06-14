// Big Matrix clock for ESP32 based on md_parola library
// See below
// David Bottrill June 2020

// - MD_MAX72XX library can be found at https://github.com/MajicDesigns/MD_MAX72XX
//
// Each font file has the lower part of a character as ASCII codes 0-127 and the
// upper part of the character in ASCII code 128-255. Adding 128 to each lower
// character creates the correct index for the upper character.
// The upper and lower portions are managed as 2 zones 'stacked' on top of each other
// so that the module numbers are in the sequence shown below:
//
// * Modules (like FC-16) that can fit over each other with no gap
//  n n-1 n-2 ... n/2+1   <- this direction top row
//  n/2 ... 3  2  1  0    <- this direction bottom rowm
//
// * Modules (like Generic and Parola) that cannot fit over each other with no gap
//  n/2+1 ... n-2 n-1 n   -> this direction top row
//  n/2 ... 3  2  1  0    <- this direction bottom row
//
// Sending the original string to the lower zone and the modified (+128) string to the
// upper zone creates the complete message on the display.
//

#include <WiFi.h>
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager
#include <ESPmDNS.h>
#include "time.h"
#include <MD_Parola.h>
#include <MD_MAX72xx.h>
#include <SPI.h>
#include "Font_Data.h"

// Define the number of devices we have in the chain and the hardware interface
// NOTE: These pin numbers will probably not work with your hardware and may
// need to be adapted
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 32                //8 x 4 modules giving a display of 64 x 32 pixels

#define CLK_PIN   18
#define DATA_PIN  23
#define CS_PIN    19

#define ldr       36                  //LDR Light sensor +3V3 to LDR and a 1K resistor to ground

// Hardware SPI connection
MD_Parola P = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);


#define SPEED_TIME  50   //Was 75 the lower the number the faster the dispaly scrolls
#define SPEED_TIME2 40   //Was 75 the lower the number the faster the dispaly scrolls
#define PAUSE_TIME  0

WiFiServer server(80);

// Dayligh savings / NTP settings
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;
const char* Timezone = "GMT0BST,M3.5.0/01,M10.5.0/02";       // UK
//Example time zones
//const char* Timezone = "GMT0BST,M3.5.0/01,M10.5.0/02";     // UK
//const char* Timezone = "MET-2METDST,M3.5.0/01,M10.5.0/02"; // Most of Europe
//const char* Timezone = "CET-1CEST,M3.5.0,M10.5.0/3";       // Central Europe
//const char* Timezone = "EST-2METDST,M3.5.0/01,M10.5.0/02"; // Most of Europe
//const char* Timezone = "EST5EDT,M3.2.0,M11.1.0";           // EST USA
//const char* Timezone = "CST6CDT,M3.2.0,M11.1.0";           // CST USA
//const char* Timezone = "MST7MDT,M4.1.0,M10.5.0";           // MST USA
//const char* Timezone = "NZST-12NZDT,M9.5.0,M4.1.0/3";      // Auckland
//const char* Timezone = "EET-2EEST,M3.5.5/0,M10.5.5/0";     // Asia
//const char* Timezone = "ACST-9:30ACDT,M10.1.0,M4.1.0/3":   // Australia


// Global variables
char  szTimeL[6];    // mm:ss\0
char  szTimeH[6];
char  szSecs[3];
char  szAmPm[3];
char  szMesg[60];
char  szMesg2[60];

int h, m, s, dd, dw, mm, yy, ds;

#define ldr 36                // Analogue pin for Light sensor LDR
int intensity = 0;          // Display brightness
int intensity_old = 0;
int  intenAcc = 0;            // Intensity accumulator
byte intenCtr = 0;            // Intensity counter
const int intenMax = 96;      // Max number for Intensity counter
bool Shutdown = false;
float Etemperature;           //Received External Temperature
float Epressure;              //Received External Pressure
float Ehumidity;              //Received External Humidity
float Ebattery;
float Erssi;
float temperature;
float humidity;
float pressure;
float battery;
float rssi;

const long timeoutSensor = 6 * 60 * 1000;
long long s1Timer = -(timeoutSensor + 30000);
long long s2Timer = -(timeoutSensor + 30000) ;

// Current time
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0;
// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 2000;


TaskHandle_t Task1, Task2, Task3;
SemaphoreHandle_t baton;

void getTime(char *, bool);
void sendPage(void);
void sendCSS(void);

void setup(void)
{
  pinMode(LED_BUILTIN, OUTPUT);
  Serial.begin(115200);

  // initialise the LED display
  P.begin(6);

  P.setZone(0, 2, 7);     //Lower time Zone
  P.setZone(1, 10, 15);   //Upper time Zone
  P.setZone(2, 0, 1);     //Seconds Zone
  P.setZone(3, 8, 9);     //AM / PM Zone
  P.setZone(4, 16, 23);   //General Zone 1
  P.setZone(5, 24, 31);   //General Zone 2

  P.setFont(0, BigFont);
  P.setFont(1, BigFont);
  P.setFont(2, Font6x8);
  P.setFont(3, SmallFont);
  P.setFont(4, SmallFont);
  P.setFont(5, SmallFont);

  P.displayZoneText(0, szTimeL, PA_RIGHT,  SPEED_TIME, PAUSE_TIME, PA_PRINT, PA_NO_EFFECT);
  P.displayZoneText(1, szTimeH, PA_RIGHT,  SPEED_TIME, PAUSE_TIME, PA_PRINT, PA_NO_EFFECT);
  P.displayZoneText(2, szSecs,  PA_CENTER, SPEED_TIME, PAUSE_TIME, PA_PRINT, PA_NO_EFFECT);
  P.displayZoneText(3, szAmPm,  PA_CENTER, SPEED_TIME, PAUSE_TIME, PA_PRINT, PA_NO_EFFECT);
  P.displayZoneText(4, szMesg, PA_CENTER, SPEED_TIME2, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);
  P.displayZoneText(5, szMesg2, PA_CENTER, SPEED_TIME, 0, PA_SCROLL_LEFT, PA_SCROLL_LEFT);

  P.setIntensity(intensity);                    // Brightness 0 - 15

//Setup WiFi usinf WiFiManager
    WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
    WiFiManager wm;
    bool res;
     res = wm.autoConnect("clock"); // auto generated AP name from chipid
    if(!res) {
        Serial.println("Failed to connect");
         ESP.restart();
    } 
    else {
        //if you get here you have connected to the WiFi    
        Serial.println("WiFi connected)");
    }  
  
  WiFi.setHostname("clock");
  Serial.println();
  Serial.println(WiFi.localIP());
  Serial.println(WiFi.getHostname());

  configTime(0, 0, "192.168.1.1", "pool.ntp.org", "time.nist.gov");
  setenv("TZ", Timezone, 1);
  Serial.println("NTP Client started");
  
  if (!MDNS.begin(WiFi.getHostname())) {
    Serial.println("Error setting up MDNS responder!");
    delay(1000);
  }
  Serial.println("mDNS responder started");
  // Add service to MDNS-SD
  MDNS.addService("http", "tcp", 80);


  // Start TCP (HTTP) server
  server.begin();
  Serial.println("TCP server started");

  //Start the tasks
  baton = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(
    clockTask,
    "ClockTask",
    3000,
    NULL,
    1,
    &Task1,
    1);

  delay(500);  // needed to start-up task1

  xTaskCreatePinnedToCore(
    serverTask,
    "serverTask",
    3000,
    NULL,
    1,
    &Task2,
    1) ;

  delay(500);

  xTaskCreatePinnedToCore(
    displayTask,
    "DisplayTask",
    3000,
    NULL,
    1,
    &Task3,
    1);

  delay(500);


}

void loop(void)
{

  // Average LDR ambient light level reading over a number of samples
  if (intenCtr == 0 ) {
    intenCtr = intenMax;
    intensity = round(((intenAcc / intenMax) - 1024) / 204);
    if (intensity < 0 ) intensity = 0;
    if (intensity > 15 ) intensity = 15;
    if (intensity != intensity_old) {
      P.setIntensity(intensity);                    // Brightness 0 - 15
      Serial.printf("Intensity: %d\r\n", intensity);
      intensity_old = intensity;

      if(intensity == 0 && Shutdown == false){
        Shutdown = true;
        Serial.println("Display Shutdown");
        P.displayClear();
        P.displayShutdown(1);
        P.displayShutdown(1);
        delay(100);
      }
      if(intensity > 0 && Shutdown == true){
        Shutdown = false;
        Serial.println("Display On");
        P.displayShutdown(0);
        P.displayShutdown(0);
        delay(100);
        P.displayClear();
        delay(100);
        P.displayReset();
        delay(100);
      }
      
    }
    intenAcc = 0;
  } else {
    intenAcc = intenAcc + analogRead(ldr);           // Add current light intensity to accumulator
    intenCtr --;
  }

  delay(10);
  
}

//Clock Task
void clockTask( void * parameter )
{
  for (;;) {
    static uint32_t  lastTime; // millis() memory
    static bool  flasher;  // seconds passing flasher
    if (P.getZoneStatus(0) && P.getZoneStatus(1) && P.getZoneStatus(2) && P.getZoneStatus(3))
    {
      // Adjust the time string if we have to. It will be adjusted
      // every second at least for the flashing colon separator.
      if (millis() - lastTime >= 1000)
      {
        lastTime = millis();
        getTime(szTimeL, flasher);
        createHString(szTimeH, szTimeL);
        flasher = !flasher;
        digitalWrite(LED_BUILTIN, flasher);

        P.displayReset(0);
        P.displayReset(1);
        P.displayReset(2);
        P.displayReset(3);
        // synchronise the start
        //P.synchZoneStart();
      }
    }
    delay(100);
  }
}


//Server Task
void serverTask( void * parameter )
{
  String request;
  for (;;) {
    delay(10);
    WiFiClient client = server.available();   // Listen for incoming clients
    //if (client) {     // If a new client connects,
    if (client.available() > 0) {
      currentTime = millis();
      previousTime = currentTime;
      Serial.println("New Client.");          // print a message out in the serial port
      String currentLine = "";                // make a String to hold incoming data from the client
      while (client.connected() && currentTime - previousTime <= timeoutTime) {
        currentTime = millis();
        // loop while the client's connected
        if (client.available()) {             // if there's bytes to read from the client,
          char c = client.read();             // read a byte, then
          Serial.write(c);                    // print it out the serial monitor
          request += c;
          if (c == '\n') {                    // if the byte is a newline character
            // if the current line is blank, you got two newline characters in a row.
            // that's the end of the client HTTP request, so send a response:
            if (currentLine.length() == 0) {
              // HTTP requests always start with a response code (e.g. HTTP/1.1 200 OK)
              // and a content-type so the client knows what's coming, then a blank line:
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/html");
              client.println("Connection: close");
              client.println();

              if (request.indexOf("/ethernetcss.css") != -1) {  // Send Stylesheet
                sendCSS(client);
                break;
              }
              if (request.indexOf("/favicon.ico") != -1) {  // ignore
                break;
              }
              if (request.indexOf("?temp1=") != -1) {
                String etempStr = request.substring(12, 16);
                Serial.print("Internal Temperature received: ");
                Serial.println(etempStr);
                temperature = etempStr.toFloat();
                break;
              }
              if (request.indexOf("?press1=") != -1) {
                Serial.print("Internal Pressure received: ");
                String etempStr = request.substring(13, 20);
                Serial.println(etempStr);
                pressure = etempStr.toFloat();
                break;
              }
              if (request.indexOf("?humid1=") != -1) {
                Serial.print("Internal Humidity received: ");
                String etempStr = request.substring(13, 17);
                Serial.println(etempStr);
                humidity = etempStr.toFloat();
                break;
              }
              if (request.indexOf("?batt1=") != -1) {
                Serial.print("Internal Battery voltage received: ");
                String etempStr = request.substring(12, 16);
                Serial.println(etempStr);
                battery = etempStr.toFloat();
                break;
              }
              if (request.indexOf("?rssi1=") != -1) {
                Serial.print("Internal RSSI received: ");
                String etempStr = request.substring(12, 16);
                Serial.println(etempStr);
                rssi = etempStr.toFloat();
                s1Timer = millis();   //store time received
                break;
              }
              if (request.indexOf("?temp2=") != -1) {
                Serial.print("External Temperature received: ");
                String etempStr = request.substring(12, 16);
                Serial.println(etempStr);
                Etemperature = etempStr.toFloat();
                break;
              }
              if (request.indexOf("?press2=") != -1) {
                Serial.print("External Pressure received: ");
                String etempStr = request.substring(13, 20);
                Serial.println(etempStr);
                Epressure = etempStr.toFloat();
                break;
              }
              if (request.indexOf("?humid2=") != -1) {
                Serial.print("External Humidity received: ");
                String etempStr = request.substring(13, 17);
                Serial.println(etempStr);
                Ehumidity = etempStr.toFloat();
                break;
              }
              if (request.indexOf("?batt2=") != -1) {
                Serial.print("External Battery voltage received: ");
                String etempStr = request.substring(12, 16);
                Serial.println(etempStr);
                Ebattery = etempStr.toFloat();
                break;
              }
              if (request.indexOf("?rssi2=") != -1) {
                Serial.print("External RSSI received: ");
                String etempStr = request.substring(12, 16);
                Serial.println(etempStr);
                Erssi = etempStr.toFloat();
                s2Timer = millis();   //store time received
                break;
              }

              sendPage(client);
              // Break out of the while loop
              break;
            } else { // if you got a newline, then clear currentLine
              currentLine = "";
            }
          } else if (c != '\r') {  // if you got anything else but a carriage return character,
            currentLine += c;      // add it to the end of the currentLine
          }
        }
      }
      // Clear the request variable
      request = "";
      // Close the connection
      client.stop();
      Serial.println("Client disconnected.");
      Serial.println("");
    }

  }
}


//Display Task
void displayTask( void * parameter )
{
  uint8_t  display = 0;  // current display mode
  uint8_t  display2 = 0;  // current display mode
  for (;;) {
    if (P.getZoneStatus(4))
    {
      // Calendar
      display = 0;
      getDate(szMesg);

      P.displayReset(4);
    }

    if (P.getZoneStatus(5)) {
      switch (display2) {

        case 0: // Outside Temperature deg C
          display2++;
          if (millis() > s2Timer + timeoutSensor) {
            sprintf(szMesg2, "Outside Sensor Timeout");
          } else {
            dtostrf(Etemperature, 3, 1, szMesg2);
            strcat(szMesg2, " \x90\C Outside");
          }
          break;

        case 1: // `Inside Temperature deg C

          if (millis() > s1Timer + timeoutSensor) {
            sprintf(szMesg2, "Inside Sensor Timeout");
            display2 = 0;
          } else {
            dtostrf(temperature, 3, 1, szMesg2);
            strcat(szMesg2, " \x90\C Inside");
            display2++;
          }
          break;

        case 2: // Air Pressure
          display2++;
          dtostrf(pressure, 4, 1, szMesg2);
          strcat(szMesg2, " hPa");
          break;

        case 3: // Relative Humidity
          display2 = 0;
          dtostrf(humidity, 3, 1, szMesg2);
          strcat(szMesg2, "% RH");
          break;
      }
      P.displayReset(5);


    }
    P.displayAnimate();

    delay(10);

  }
}


void sendPage(WiFiClient wc) { // Function to send Web Page
  wc.println("<HTML>");
  wc.println("<HEAD>");
  wc.println("<link rel='stylesheet' type='text/css' href='/ethernetcss.css' />");
  wc.println("<TITLE>ESP32 Big Matrix Clock</TITLE>");
  wc.println("</HEAD>");
  wc.println("<BODY>");
  wc.println("<H1>ESP32 Big Matrix Clock</H1>");
  wc.println("<hr />");
  wc.println("<H2>");

  wc.print("Connected to:<br />SSID: ");
  wc.print(WiFi.SSID());
  wc.print("&nbsp&nbsp&nbspRSSI: ");
  wc.print(WiFi.RSSI());
  wc.println(" dB");
  wc.println("<br />");

  wc.print("Use this URL : ");
  wc.print("http://");
  wc.print(WiFi.getHostname());
  wc.print(".local");
  wc.println("<br />");
  wc.print("or use this URL : ");
  wc.print("http://");
  wc.print(WiFi.localIP());
  wc.println("<br />");

  wc.printf("Time now: %02d:%02d\n", h, m);
  wc.println("<br />");
  wc.printf("Display Intensity: %2d\n", intensity);
  wc.println("<br />");
  wc.printf("Internal Temperature: %2.1f &#186;C\n", temperature);
  wc.println("<br />");
  wc.printf("External Temperature: %2.1f &#186;C\n", Etemperature);
  wc.println("<br />");
  wc.printf("Internal Air Pressure: %4.1f hPa\n", pressure);
  wc.println("<br />");
  wc.printf("External Air Pressure: %4.1f hPa\n", Epressure);
  wc.println("<br />");
  wc.printf("Internal Humidity: %3.1f%% RH\n", humidity);
  wc.println("<br />");
  wc.printf("External Humidity: %3.1f%% RH\n", Ehumidity);
  wc.println("<br />");
  wc.printf("Internal Battery: %1.2f V\n", battery);
  wc.println("<br />");
  wc.printf("External Battery: %1.2f V\n", Ebattery);
  wc.println("<br />");
  wc.printf("Internal RSSI: %3.0f dB\n", rssi);
  wc.println("<br />");
  wc.printf("External RSSI: %3.0f dB\n", Erssi);
  wc.println("<br />");
  wc.println("<br />");
  wc.println("<a href=\"/\"\">Refresh</a><br />");
  wc.println("<br />");

  wc.println("</H2>");
  wc.println("</BODY>");
  wc.println("</HTML>");
}


void sendCSS(WiFiClient wc) { // function to send Stylesheet
  wc.println("body{font-size:200%; margin:60px 0px; padding:0px;text-align:center;}");
  wc.println("h1{text-align: center;  font-family:Arial, \"Trebuchet MS\", Helvetica, sans-serif;}");
  wc.println("h2{text-align: center;  font-family:Arial, \"Trebuchet MS\", Helvetica, sans-serif;}");
  wc.println("a{text-decoration:none;width:75px;height:50px;");
  wc.println("border-color:black;");
  wc.println("border-top:2px solid;");
  wc.println("border-bottom:2px solid;");
  wc.println("border-right:2px solid;");
  wc.println("border-left:2px solid;");
  wc.println("border-radius:10px 10px 10px;");
  wc.println("-o-border-radius:10px 10px 10px;");
  wc.println("-webkit-border-radius:10px 10px 10px;");
  wc.println("font-size:100%; font-family:\"Trebuchet MS\",Arial, Helvetica, sans-serif;");
  wc.println("-moz-border-radius:10px 10px 10px;");
  wc.println("background-color:#293F5E;");
  wc.println("padding:8px;");
  wc.println("text-align: center;}");
  wc.println("a:link {color:white;}");      // unvisited link
  wc.println("a:visited {color:white;}");  // visited link
  wc.println("a:hover {color:white;}");  // mouse over link
  wc.println("a:active {color:white;}");  // selected link
}




void createHString(char *pH, char *pL)
{
  for (; *pL != '\0'; pL++)
    *pH++ = *pL | 0x80;   // offset character

  *pH = '\0'; // terminate the string
}



// Code for reading clock date
void getDate(char *psz)
{
  char  szBuf[10];
  char dd1[] = "th";

  if (dd == 1 || dd == 21 || dd == 31) {
    dd1[0] = 's';
    dd1[1] = 't';
  }
  if (dd == 2 || dd == 22) {
    dd1[0] = 'n';
    dd1[1] = 'd';
  }
  if (dd == 3 || dd == 23) {
    dd1[0] = 'r';
    dd1[1] = 'd';
  }
  //sprintf(psz, "%d %s %04d", dd, mon2str(mm, szBuf, sizeof(szBuf) - 1), yy);
  //sprintf(psz, "%d%s %s %04d", dd, dd1, mon2str(mm, szBuf, sizeof(szBuf) - 1), yy);
  sprintf(psz, "%s %d%s %s %04d", dow2str(dw, szMesg, sizeof(szMesg) - 1), dd, dd1, mon2str(mm, szBuf, sizeof(szBuf) - 1), yy);

}



void getTime(char *psz, bool f = true)
// Code for reading clock time
{
  time_t now;
  time(&now);
  struct tm * timeinfo;
  timeinfo = localtime(&now);
  h  = timeinfo->tm_hour;
  m  = timeinfo->tm_min;
  s  = timeinfo->tm_sec;
  dd = timeinfo->tm_mday;
  dw = timeinfo->tm_wday;
  mm = timeinfo->tm_mon;
  yy = 1900 + timeinfo->tm_year;

  //sprintf(psz, "%02d%c%02d", h, (f ? ':' : ' '), m);
  sprintf(psz, "%02d:%02d", h, m);;
  //sprintf(szMesg, "%02d:%02d", m, s);
  sprintf(szSecs, "%02d", s);
  if (h > 11) {
    sprintf(szAmPm, "PM");
  } else {
    sprintf(szAmPm, "AM");
  }
}


char *dow2str(uint8_t code, char *psz, uint8_t len)
{
  static const __FlashStringHelper*  str[] =
  {
    F("Sunday"), F("Monday"), F("Tuesday"),
    F("Wednesday"), F("Thursday"), F("Friday"),
    F("Saturday")
  };

  //strncpy_P(psz, (const char PROGMEM *)str[code - 1], len);
  strncpy_P(psz, (const char PROGMEM *)str[code], len);
  psz[len] = '\0';

  return (psz);
}

// Get a label from PROGMEM into a char array
char *mon2str(uint8_t mon, char *psz, uint8_t len)
{
  static const __FlashStringHelper* str[] =
  {
    F("January"), F("February"), F("March"), F("April"),
    F("May"), F("June"), F("July"), F("August"),
    F("September"), F("October"), F("November"), F("December")
  };

  //strncpy_P(psz, (const char PROGMEM *)str[mon - 1], len);
  strncpy_P(psz, (const char PROGMEM *)str[mon], len);
  psz[len] = '\0';

  return (psz);
}
