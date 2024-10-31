// Tempo - main.cpp (for PlatformIO)

/***********************************************************************************
  
  Objective
    Obtain Tempo color for a day, using the RTE API Tempo Like Supply Contract.

  Steps
  1. Connect to a local network using Wifi.
  2. Send an HTTP GET Request to the RTE API, to obtain an access Token.
  3. Init the time system with a time zone string, to handle local times.
  4. If current local time is required, call an NTP server to init the RTC clock.
  5. Send HTTP¨GET requests to the RTE API, to obtain different Tempo colors.

  NB
  - In steps 2 and 5, decode the JSON data sent by the RTE API.
  - The results are displayed on the serial monitor.

  References
  - ESP32-DevKitC V4 and ESP32-WROOM-32UE :
    . https://docs.espressif.com/projects/esp-idf/en/latest/esp32/hw-reference/esp32/get-started-devkitc.html
    . https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32e_esp32-wroom-32ue_datasheet_en.pdf
    . https://randomnerdtutorials.com/solved-failed-to-connect-to-esp32-timed-out-waiting-for-packet-header/
  - Wifi :
    . https://randomnerdtutorials.com/esp32-useful-wi-fi-functions-arduino/
  - Current time :
    . https://randomnerdtutorials.com/esp32-date-time-ntp-client-server-arduino/
    . https://randomnerdtutorials.com/esp32-ntp-timezones-daylight-saving/
    . https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv
    . https://sourceware.org/newlib/libc.html#Timefns
    . https://cplusplus.com/reference/ctime/ 
    . https://github.com/espressif/arduino-esp32/blob/master/cores/esp32/esp32-hal-time.c#L47
  - HTTPClient :
    . https://github.com/espressif/arduino-esp32/tree/master/libraries/HTTPClient/src
    . https://randomnerdtutorials.com/esp32-http-get-post-arduino/
  - ArduinoJSON :
    . https://github.com/bblanchon/ArduinoJson
    . https://arduinojson.org/v6/doc/
    . https://arduinojson.org/v6/assistant/
    . https://arduinojson.org/v6/how-to/use-arduinojson-with-httpclient/

/***********************************************************************************
  Libraries and types
***********************************************************************************/

#include <WiFi.h>
#include <time.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

/***********************************************************************************
  Constants
***********************************************************************************/

// Local network access point
const char *SSID = "--------";
const char *PWD = "--------"; 

// NTP server (=>UTC time) and Time zone
const char* NTP_SERVER = "pool.ntp.org";  // Server address (or "ntp.obspm.fr", "ntp.unice.fr", ...) 
const char* TIME_ZONE = "CET-1CEST,M3.5.0,M10.5.0/3"; // Europe/Paris time zone 

// RTE basic authorization
const char *AUTH = "--------";

// Debug print
// #define DEBUG_PRINT

/***********************************************************************************
  Global variables
***********************************************************************************/

HTTPClient http;
static char payload[1000];
JsonDocument doc; 

/***********************************************************************************
  Tool functions
***********************************************************************************/

void setTimeZone(const char *timeZone)
{
  // To work with Local time (custom and RTC)
  setenv("TZ", timeZone, 1); 
  tzset();
}

void initRTC(const char *timeZone)
{
  // Set RTC with Local time, using an NTP server
  configTime(0, 0, "pool.ntp.org"); // To get UTC time
  tm time;
  getLocalTime(&time);    
  setTimeZone(timeZone);  // Transform to Local time
}

bool getCustomTime(const int year, const int month, const int day, const int hour, const int minute, const int second, tm *timePtr)
{
  // Set a time (date) without DST indication
  *timePtr = {0};
  timePtr->tm_year = year - 1900; 
  timePtr->tm_mon = month-1;
  timePtr->tm_mday = day;
  timePtr->tm_hour = hour;
  timePtr->tm_min = minute;
  timePtr->tm_sec = second;
  time_t t = mktime(timePtr);
  timePtr->tm_hour--;
  time_t t1 = mktime(timePtr);
  memcpy(timePtr, localtime(&t), sizeof(tm)); 
  if (localtime(&t1)->tm_isdst==1)
  {
    if (timePtr->tm_isdst==0) return false;  // Ambigous
    else timePtr->tm_hour--;
  }
  return true;
}

bool getAccessToken(String *tokenPtr)
{
  // Local variables
  bool okToken = false;
  String url = "https://digital.iservices.rte-france.com/token/oauth/";
  String auth = "Basic " + String(AUTH);
#ifdef DEBUG_PRINT
  Serial.printf("URL : %s\nAuthorization : %s\n", url.c_str(), auth.c_str());
#endif

  // HTTP Get request
  http.begin(url);
	http.setTimeout(1000);
  http.addHeader("Authorization", auth);
  http.addHeader("Accept", "application/json");

  // Send and decode
  if (http.GET() == 200) 
  {
    strcpy(payload, http.getString().c_str());
    if (!deserializeJson(doc, payload))
    {
      serializeJsonPretty(doc, payload);
      const char* token = doc["access_token"];
#ifdef DEBUG_PRINT
      Serial.printf("Payload : \n%s\nToken : %s\n", payload, token);
#endif
      *tokenPtr = String(token);
      okToken = true;
    }
  }
  http.end();
  return okToken;
}

bool getTempoDayColor(const int year, const int month, const int day, const String *tokenPtr, String *colorPtr)
{
  // Local variables
  bool okColor = false;
  String url = "https://digital.iservices.rte-france.com/open_api/tempo_like_supply_contract/v1/tempo_like_calendars?start_date=YYYY-MM-DDThh:mm:sszzzzzz&end_date=YYYY-MM-DDThh:mm:sszzzzzz";

  // Copy dates in URL with ISO 8601 format
  tm timeStart; getCustomTime(year, month, day, 0, 0, 0, &timeStart);
  tm timeEnd; getCustomTime(year, month, day+1, 0, 0, 0, &timeEnd);
  strftime((char*)(url.c_str())+112, 23, "%FT%T%z", &timeStart);
  strftime((char*)(url.c_str())+147, 23, "%FT%T%z", &timeEnd);
  strcpy((char*)(url.c_str())+134, ":00");
  strcpy((char*)(url.c_str())+169, ":00"); 
  url[137]='&';
  String auth = "Bearer " + *tokenPtr;
#ifdef DEBUG_PRINT
  Serial.printf("URL : %s\nAuthorization : %s\n", url.c_str(), auth.c_str());
#endif

  // HTTP Get request
  http.begin(url);
	http.setTimeout(1000);
  http.addHeader("Authorization", auth);
  http.addHeader("Accept", "application/json");

  // Send and decode response
  int code = http.GET();
  if (code == 200) 
  {
    strcpy(payload, http.getString().c_str());
    if (!deserializeJson(doc, payload))
    {
      serializeJsonPretty(doc, payload);
      const char* color = doc["tempo_like_calendars"]["values"][0]["value"];
#ifdef DEBUG_PRINT
      Serial.printf("Payload : \n%s\nColor : %s\n", payload, color);
#endif
      *colorPtr = String(color);
      okColor = true;
    }
  }
  else if (code == 400 )
  {
    *colorPtr = "UNDEFINED";
    okColor = true;
  }
  http.end();
  return okColor;
}

/***********************************************************************************
  setup and loop functions
***********************************************************************************/

void setup()
{
  // Open serial port
  Serial.begin(115200);
  while (!Serial);

  // Connect to the Wifi access point 
  WiFi.begin(SSID, PWD);
  while (WiFi.status() != WL_CONNECTED); 
#ifdef DEBUG_PRINT
  Serial.printf("IP=%s RSSI=%d\n", WiFi.localIP().toString(), WiFi.RSSI());
#endif
  Serial.println("\nACCESS TO THE RTE API \"TEMPO LIKE SUPPLY CONTRACT\"");

  // Get access token
  Serial.println("\nGET ACCESS TOKEN");
  String token;
  if (getAccessToken(&token)) Serial.printf("Token : %s\n", token.c_str());
  else 
  {
    Serial.println("Error : cannot obtain access token");
    exit(0);
  }

  // Set time zone (not necessary after InitRTC)
  setTimeZone(TIME_ZONE);

  // Custom day Tempo color
  Serial.println("\nGET CUSTOM DAY TEMPO COLOR");
  String color;
  Serial.println("12/2/2024 :");
  if (getTempoDayColor(2024, 2, 11, &token, &color)) Serial.printf("Day J-1 Tempo color : %s\n", color.c_str());
  if (getTempoDayColor(2024, 2, 12, &token, &color)) Serial.printf("Day J Tempo color : %s\n", color.c_str());
  if (getTempoDayColor(2024, 2, 13, &token, &color)) Serial.printf("Day J+1 Tempo color : %s\n", color.c_str());

  // Init RTC with Local time using an NTP server
  initRTC(TIME_ZONE);
  char buf[30];

  // Current day Tempo color
  Serial.println("\nGET CURRENT DAY TEMPO COLOR");
  tm time;
  getLocalTime(&time);
  int year = time.tm_year+1900;
  int month = time.tm_mon+1;
  int day = time.tm_mday;
  Serial.printf("%d/%d/%d :\n", day, month, year);
  if (getTempoDayColor(year, month, day , &token, &color)) Serial.printf("Day J Tempo color : %s\n", color.c_str());
  if (getTempoDayColor(year, month, day+2 , &token, &color)) Serial.printf("Day J+2 Tempo color : %s\n", color.c_str());
}

void loop()
{
}