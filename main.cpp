// Tempo - main.cpp (for PlatformIO)

/***********************************************************************************
  
  Objective
    Obtain Tempo color for a day, using the RTE API Tempo Like Supply Contract.

  Steps
  1. Connect to a local network using a Wifi Access Point.
  2. Send an HTTP GET Request to the RTE API, to obtain an access Token.
  3. Init the time system with a time zone string, to handle local times.
  4. If the current local time is required, call an NTP server to init the RTC clock.
  5. Send HTTP¨GET requests to the RTE API, to obtain the Tempo colors.

  NB
  - In steps 2 and 5, decode the JSON data sent by the RTE API.
  - The results are displayed on the serial monitor.
  - Use robust Wifi connection :
    . restart the ESP32 when the connections don't succeed ;
    . automatic Wifi reconnection when accidentally lost.
  - If required, use a robust connection to the NTP server :
    . restart the ESP32 when the connections don't succeed.

  References
  - ESP32-DevKitC V4 and ESP32-WROOM-32UE :
    . https://docs.espressif.com/projects/esp-idf/en/latest/esp32/hw-reference/esp32/get-started-devkitc.html
    . https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32e_esp32-wroom-32ue_datasheet_en.pdf
    . https://randomnerdtutorials.com/solved-failed-to-connect-to-esp32-timed-out-waiting-for-packet-header/
  - Wifi :
    . https://randomnerdtutorials.com/esp32-useful-wi-fi-functions-arduino/
    . https://randomnerdtutorials.com/solved-reconnect-esp32-to-wifi/
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
    . https://arduinojson.org/v7/doc/
    . https://arduinojson.org/v7/assistant/
    . https://arduinojson.org/v7/how-to/use-arduinojson-with-httpclient/
  - Watch doc timer (WDT)
    . https://iotassistant.io/esp32/enable-hardware-watchdog-timer-esp32-arduino-ide/

/***********************************************************************************
  Libraries and types
***********************************************************************************/

#include <WiFi.h>
#include <time.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

/***********************************************************************************
  Constants
***********************************************************************************/

// Local network access point
const char *WIFI_SSID = "......";
const char *WIFI_PASSWORD = "......";
const uint32_t WIFI_TIMEOUT = 20; // s

// NTP server (=>UTC time) and Time zone
const char* NTP_SERVER = "pool.ntp.org";  // Server address (or "ntp.obspm.fr", "ntp.unice.fr", ...) 
const char* TIME_ZONE = "CET-1CEST,M3.5.0,M10.5.0/3"; // Europe/Paris time zone 
const uint32_t NTP_TIMEOUT = 20; // s

// RTE API Tempo Like Supply Contract
const char *TOKEN_URL = "https://digital.iservices.rte-france.com/token/oauth/";
const char *BASIC_AUTH = "Basic YjY5N2VmMzktNDczYS00NTY5LTk2OGMtNjRmNTU0ZGZlMDgzOjU2MDA0NjQ5LWU4MTEtNDZiZS05NGMyLTVmMGQ5YjhlYjM2Nw==";
const char *TEMPO_URL = "https://digital.iservices.rte-france.com/open_api/tempo_like_supply_contract/v1/tempo_like_calendars?start_date=YYYY-MM-DDThh:mm:sszzzzzz&end_date=YYYY-MM-DDThh:mm:sszzzzzz";
const char *UNDEFINED = "UNDEFINED"; // Undefined color

// Print flag
// #define PRINT_FLAG

/***********************************************************************************
  Global variables
***********************************************************************************/

JsonDocument doc;

/***********************************************************************************
  Tool functions
***********************************************************************************/

void initWiFi(const char *ssid, const char *password, const uint32_t timeOut) 
{
  esp_task_wdt_init(timeOut, true);  // Enable panic so ESP32 restarts
  esp_task_wdt_add(NULL);            // Add current thread to WDT watch
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) delay(1000);
  esp_task_wdt_delete(NULL);         // Delete the WDT for the current thread
#ifdef PRINT_FLAG
  Serial.printf("Wifi connected : IP=%s RSSI=%d\n", WiFi.localIP().toString(), WiFi.RSSI());
#endif
}

void WiFiDisconnected(WiFiEvent_t event, WiFiEventInfo_t info)
{
#ifdef PRINT_FLAG
  Serial.printf("Wifi disconnected : Reason=%d\n", info.wifi_sta_disconnected.reason);
#endif
  if (info.wifi_sta_disconnected.reason != 8) // Not a call to Wifi.disconnect()
  {
    initWiFi(WIFI_SSID, WIFI_PASSWORD, WIFI_TIMEOUT);
  }
}

void setTimeZone(const char *timeZone)
{
  // To work with Local time (custom and RTC)
  setenv("TZ", timeZone, 1); 
  tzset();
}

void initRTC(const char *NTPServer, const char *timeZone, const uint32_t timeOut)
{
  // Set RTC with Local time, using an NTP server
  esp_task_wdt_init(timeOut, true);  // Enable panic so ESP32 restarts
  esp_task_wdt_add(NULL);            // Add current thread to WDT watch
  configTime(0, 0, NTPServer);       // To get UTC time
  tm time;
  while (!getLocalTime(&time)) delay(1000); // UTC time
  setTimeZone(timeZone);             // Transform to Local time
  esp_task_wdt_delete(NULL);         // Delete the WDT for the current thread
#ifdef PRINT_FLAG
  Serial.println("RTC clock initialized with Local time, using an NTP server");
#endif
}

bool getCustomTime(int year, int month, int day, int hour, int minute, int second, tm *timePtr)
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

bool getJsonDocumentFromHTTPRequest(const char *url, const char *auth)
{
  bool ok = false;
  HTTPClient http;
  http.useHTTP10();  // to prevent chunked transfer encoding
  http.setTimeout(1000);
  http.begin(url);
  http.addHeader("Accept", "application/json");
  if (auth != NULL) http.addHeader("Authorization", auth);
  if ((http.GET() == 200) && !deserializeJson(doc, http.getStream())) ok = true;
  http.end();
  return ok;
}

const char* getTempoDayColor(const int year, const int month, const int day, const char *auth)
{
   // Copy dates in URL with ISO 8601 format
  tm timeStart; getCustomTime(year, month, day, 0, 0, 0, &timeStart);
  tm timeEnd; getCustomTime(year, month, day+1, 0, 0, 0, &timeEnd);
  char url[200];
  strcpy(url, TEMPO_URL);
  strftime(url+112, 23, "%FT%T%z", &timeStart);
  strftime(url+147, 23, "%FT%T%z", &timeEnd);
  strcpy(url+134, ":00");
  strcpy(url+169, ":00");
  url[137] = '&'; 
#ifdef PRINT_FLAG
  Serial.printf("URL : %s\nAuthorization : %s\n", url, auth);
#endif
  if (getJsonDocumentFromHTTPRequest(url, auth))
  {
    return (doc["tempo_like_calendars"]["values"][0]["value"] | UNDEFINED);
  }
  return UNDEFINED;
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
  WiFi.onEvent(WiFiDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  initWiFi(WIFI_SSID, WIFI_PASSWORD, WIFI_TIMEOUT);
  Serial.println("\nACCESS TO THE RTE API \"TEMPO LIKE SUPPLY CONTRACT\"");

  Serial.println("\nGET ACCESS TOKEN");
  if (getJsonDocumentFromHTTPRequest(TOKEN_URL, BASIC_AUTH))
  {
    Serial.printf("Token : %s\n", (const char *)doc["access_token"]);

    // Bearer authorization using the Access Token
    char auth[100];
    strcpy(auth, "Bearer ");
    strcpy(auth+7, doc["access_token"]);

    // Set time zone (not necessary after InitRTC)
    setTimeZone(TIME_ZONE);

    // Tempo days color
    Serial.println("\nCUSTOM DAY TEMPO COLOR");
    Serial.println("12/2/2024 :");
    Serial.printf("Day J-1 Tempo color : %s\n", getTempoDayColor(2024, 2, 11, auth));
    Serial.printf("Day J Tempo color : %s\n", getTempoDayColor(2024, 2, 12, auth));
    Serial.printf("Day J+1 Tempo color : %s\n", getTempoDayColor(2024, 2, 13, auth));
    
    Serial.println("\nCURRENT DAY TEMPO COLOR");
    initRTC(NTP_SERVER, TIME_ZONE, NTP_TIMEOUT); // Init the RTC with Local time, using an NTP server
    tm t;
    getLocalTime(&t);
    Serial.printf("%d/%d/%d :\n", t.tm_mday, t.tm_mon+1, t.tm_year+1900);
    Serial.printf("Day J Tempo color : %s\n", getTempoDayColor(t.tm_year+1900, t.tm_mon+1, t.tm_mday , auth));
    Serial.printf("Day J Tempo color : %s\n", getTempoDayColor(t.tm_year+1900, t.tm_mon+1, t.tm_mday+2 , auth));
  }
  else 
  {
    Serial.println("Error : cannot obtain access token");
    esp_deep_sleep_start();
  }
}

void loop()
{
}