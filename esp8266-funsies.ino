// Experiments with esp8266 + Google Maps API
// Author: Sean Klein

// Include the ESP8266 WiFi library. (Works a lot like the
// Arduino WiFi library.)
#include <ESP8266WiFi.h>

// I2C (for LCD)
#include <Wire.h>

// LCD
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Maps
#include <GoogleMapsApi.h>
#include <GoogleMapsDirectionsApi.h>
#include <TinyGPS++.h>
#include <SoftwareSerial.h>

// Https
#include <WiFiClientSecure.h>

// Calendar
#include <GoogleCalendarArduino.h>

// Authentication
#include <GoogleOauthArduino.h>

// Getting time
#include <time.h>
// Setting time
#include <Time.h>

#include "keys.h"

//////////////////////
// WiFi Definitions //
//////////////////////
const char WiFiSSID[] = WIFI_SSID;
const char WiFiPSK[] = WIFI_PASS;

/////////////////////
// Pin Definitions //
/////////////////////
const int LED_PIN = 5;       // Thing's onboard, green LED
const int ANALOG_PIN = A0;   // The only analog pin on the Thing

const int GPS_PIN_RX = 12;
const int GPS_PIN_TX = 13;
const uint32_t GPSBaud = 9600;

///////////////
// GPS Setup //
///////////////

TinyGPSPlus gps;
SoftwareSerial ss(GPS_PIN_RX, GPS_PIN_TX);

///////////////
// LCD Setup //
///////////////
#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);

/////////////////
// Google Maps //
/////////////////
WiFiClientSecure client;
GoogleAuthenticator auth(GCAL_CLIENT_ID, GCAL_CLIENT_SECRET);
GoogleMapsDirectionsApi gmaps_api(GMAPS_API_KEY, client);

/////////////////
// Post Timing //
/////////////////
const unsigned long serialRate = 2000;
const unsigned long postRate = 20000;
unsigned long lastPost = 0;
unsigned long lastSerial = 0;

size_t topStatusIndex = 0;
char topStatus[512] = "Welcome!";
char medStatus[11] = "...";
char lowStatus[22] = "Wifi: Off";

void printCapped(char* str, size_t cap) {
  char buf[30];
  strncpy(buf, str, cap - 1);
  buf[cap] = '\0';
  display.print(buf);
}

void printCappedRing(char* str, size_t start, size_t cap) {
  char buf[30];
  size_t strLen = strlen(str);
  // Copy the tail end of the string to the buffer
  size_t tailLen = (strLen - start);
  tailLen = (tailLen > cap ? cap : tailLen);
  strncpy(buf, str + start, tailLen);
  if (tailLen < cap) {
    int headLen = cap - tailLen;
    if (headLen > 0) {
      strncpy(buf + tailLen, str, headLen);
    }
  }
  buf[cap] = '\0';
  display.print(buf);
}

void draw() {
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.setTextSize(1);

  if (strlen(topStatus) < 21) {
    printCapped(topStatus, 21);
  } else {
    printCappedRing(topStatus, topStatusIndex++, 21);
    if (topStatusIndex == strlen(topStatus)) {
      topStatusIndex = 0;
    }
  }

  display.setTextSize(2);
  display.setCursor(0, 8);
  printCapped(medStatus, 11);
  display.setTextSize(1);
  display.setCursor(0, 24);
  printCapped(lowStatus, 21);
  display.display();
}

void initHardware() {
  Serial.begin(9600);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  // Don't need to set ANALOG_PIN as input, that's all it can be.

  // By default, we'll generate the high voltage from the 3.3v line internally!
  // (neat!)
  display.begin(SSD1306_SWITCHCAPVCC,
                0x3C);  // initialize with the I2C addr 0x3C (for the 128x32)
  display.display();
}

void setup() {
  initHardware();
  ss.begin(GPSBaud);

  snprintf(topStatus, sizeof(topStatus), "Initializing...");
  snprintf(medStatus, sizeof(medStatus), "Wifi");
  draw();
  connectWiFi();
  snprintf(medStatus, sizeof(medStatus), "Time");
  draw();
  // Without a way to acquire location, we're hardcoding the
  // timezone as Pacific
  int pdt_offset_seconds = -7 * 3600;
  configTime(pdt_offset_seconds, 0, "pool.ntp.org", "time.nist.gov");
  while (!time(nullptr)) {
    Serial.println("Setting time...");
    delay(1000);
  }
  setTime(time(nullptr));
  draw();
  digitalWrite(LED_PIN, HIGH);
}

enum {
  STATE_INIT,
  STATE_ACCESS_TOKEN_QUERY,
  STATE_AUTHENTICATED,
} state = STATE_INIT;

void gcalQuery() {
  Serial.println("Querying Google Calendar");
  GoogleCalendar cal;
  GoogleCalendarEvent events[5]{};
  int eventCount = sizeof(events) / sizeof(events[0]);
  eventCount = cal.ListEvents(client, String(auth.AccessToken()), events, eventCount);
  if (eventCount < 0) {
    return;
  }

  for (size_t i = 0; i < eventCount; i++) {
    Serial.println("Event:");
    Serial.println(events[i].summary);
    Serial.println(events[i].location);
  }
}

void gmapsQuery() {
  Serial.println("Querying Google Maps");
  // Inputs
  String origin = "AMD Sunnyvale";
  String destination = "Google Building 1842";

  // These are all optional (although departureTime needed for traffic)
  String departureTime = "now";        // can also be a future timestamp
  String trafficModel = "best_guess";  // Defaults to this anyways
  DirectionsInputOptions options;
  options.departureTime = "now";
  options.trafficModel = "best_guess";
  DirectionsResponse response =
      gmaps_api.directionsApi(origin, destination, options);

  snprintf(topStatus, sizeof(topStatus), "%s%s%s%s   ", "Traffic from ",
           response.start_address.c_str(), " to ",
           response.end_address.c_str());

  snprintf(medStatus, sizeof(medStatus), "%s",
           response.durationTraffic_text.c_str());

  Serial.println("Response:");
  Serial.print("Trafic from ");
  Serial.print(response.start_address);
  Serial.print(" to ");
  Serial.println(response.end_address);

  Serial.print("Duration in Traffic text: ");
  Serial.println(response.durationTraffic_text);
  Serial.print("Duration in Traffic in minutes: ");
  Serial.println(response.durationTraffic_value / 60);

  Serial.print("Normal duration text: ");
  Serial.println(response.duration_text);
  Serial.print("Normal duration in minutes: ");
  Serial.println(response.duration_value / 60);

  Serial.print("Distance text: ");
  Serial.println(response.distance_text);
  Serial.print("Distance in meters: ");
  Serial.println(response.distance_value);
  /*
  Serial.println("Response String Length: ");
  Serial.println(responseString.length());

  // XXX with your.. modifications to the gmaps lib, I think
  // things might be failing due to low memory issues... hrm..
  DynamicJsonBuffer jsonBuffer(responseString.length());
  JsonObject& response = jsonBuffer.parseObject(responseString);
  if (!response.success()) {
    Serial.println("Failed to parse Json");
    return;
  }

  if (!response.containsKey("rows")) {
    Serial.println("Reponse did not contain rows");
    return;
  }
  JsonObject& element = response["rows"][0]["elements"][0];
  String status = element["status"];
  if (status != "OK") {
    Serial.println("Unexpected status: " + status);
  }
  String distance = element["distance"]["text"];
  String duration = element["duration"]["text"];
  String durationInTraffic = element["duration_in_traffic"]["text"];
  Serial.println("Distance: " + distance);
  Serial.println("Duration: " + duration);
  Serial.println("Duration In Traffic: " + durationInTraffic);
  */
}

#define GCAL_SCOPE "https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fcalendar"
#define EEPROM_REFRESH_TOKEN_ADDR 0

GoogleAuthRequest authRequest;

static void gpsDelay(unsigned long ms) {
  unsigned long start = millis();
  do {
    while (ss.available())
      gps.encode(ss.read());
  } while(millis() - start < ms);
}

void loop() {
  if ((state == STATE_INIT) || lastPost + postRate <= millis()) {
    if (state == STATE_INIT) {
      strcpy(topStatus, "Authenticating...");
      draw();
      Serial.println("Authenticating...");
      auth.EEPROMAcquire(EEPROM_REFRESH_TOKEN_ADDR);
      if (auth.QueryRefresh(client) == 0) {
        state = STATE_AUTHENTICATED;
      } else if (auth.QueryUserCode(client, GCAL_SCOPE, &authRequest)) {
        Serial.println("Error sending user auth query");
        delay(3000);
      } else {
        Serial.println("Access Token READY for user approval\n");
        state = STATE_ACCESS_TOKEN_QUERY;
        snprintf(topStatus, sizeof(topStatus), "Enter token below at: %s  ",
            authRequest.VerifyURLCStr());
        strcpy(medStatus, authRequest.UserCodeCStr());
      }
    } else if (state == STATE_ACCESS_TOKEN_QUERY) {
      if (auth.QueryAccessToken(client, &authRequest) == 0) {
        Serial.println("AUTHENTICATED\n");
        auth.EEPROMStore(EEPROM_REFRESH_TOKEN_ADDR);
        state = STATE_AUTHENTICATED;
      }
    }

    if (state == STATE_AUTHENTICATED) {
      gcalQuery();
      gmapsQuery();
      lastPost = millis();
    }
  }

  if (lastSerial + serialRate <= millis()) {
    Serial.print("...");
    lastSerial = millis();

    Serial.println(F("Sats HDOP Latitude   Longitude   Fix  Date       Time     Date Alt    Course Speed Card  Chars Sentences Checksum"));
    Serial.println(F("          (deg)      (deg)       Age                      Age  (m)    --- from GPS ----  RX    RX        Fail"));
    Serial.println(F("-----------------------------------------------------------------------------------------------------------------"));
    printInt(gps.satellites.value(), gps.satellites.isValid(), 5);
    printInt(gps.hdop.value(), gps.hdop.isValid(), 5);
    printFloat(gps.location.lat(), gps.location.isValid(), 11, 6);
    printFloat(gps.location.lng(), gps.location.isValid(), 12, 6);
    printInt(gps.location.age(), gps.location.isValid(), 5);
    printDateTime(gps.date, gps.time);
    printFloat(gps.altitude.meters(), gps.altitude.isValid(), 7, 2);
    printFloat(gps.course.deg(), gps.course.isValid(), 7, 2);
    printFloat(gps.speed.kmph(), gps.speed.isValid(), 6, 2);
    printStr(gps.course.isValid() ? TinyGPSPlus::cardinal(gps.course.value()) : "*** ", 6);
    printInt(gps.charsProcessed(), true, 6);
    printInt(gps.sentencesWithFix(), true, 10);
    printInt(gps.failedChecksum(), true, 9);
    Serial.println();
    Serial.println();
  }

  gpsDelay(30);
  draw();
}

// TODO(smklein): Remove the following helper functions

static void printFloat(float val, bool valid, int len, int prec) {
  if (!valid) {
    while (len-- > 1)
      Serial.print('*');
    Serial.print(' ');
  } else {
    Serial.print(val, prec);
    int vi = abs((int)val);
    int flen = prec + (val < 0.0 ? 2 : 1); // . and -
    flen += vi >= 1000 ? 4 : vi >= 100 ? 3 : vi >= 10 ? 2 : 1;
    for (int i=flen; i<len; ++i)
      Serial.print(' ');
  }
}

static void printInt(unsigned long val, bool valid, int len) {
  char sz[32] = "*****************";
  if (valid)
    sprintf(sz, "%ld", val);
  sz[len] = 0;
  for (int i=strlen(sz); i<len; ++i)
    sz[i] = ' ';
  if (len > 0)
    sz[len-1] = ' ';
  Serial.print(sz);
}

static void printDateTime(TinyGPSDate &d, TinyGPSTime &t) {
  if (!d.isValid()) {
    Serial.print(F("********** "));
  } else {
    char sz[32];
    sprintf(sz, "%02d/%02d/%02d ", d.month(), d.day(), d.year());
    Serial.print(sz);
  }

  if (!t.isValid()) {
    Serial.print(F("******** "));
  } else {
    char sz[32];
    sprintf(sz, "%02d:%02d:%02d ", t.hour(), t.minute(), t.second());
    Serial.print(sz);
  }

  printInt(d.age(), d.isValid(), 5);
}

static void printStr(const char *str, int len) {
  int slen = strlen(str);
  for (int i=0; i<len; ++i)
    Serial.print(i<slen ? str[i] : ' ');
}

void connectWiFi() {
  Serial.print("Connecting to WiFi\n");
  byte ledStatus = LOW;

  // Set WiFi mode to station (as opposed to AP or AP_STA)
  WiFi.mode(WIFI_STA);

  // WiFI.begin([ssid], [passkey]) initiates a WiFI connection
  // to the stated [ssid], using the [passkey] as a WPA, WPA2,
  // or WEP passphrase.
  WiFi.begin(WiFiSSID, WiFiPSK);

  // Use the WiFi.status() function to check if the ESP8266
  // is connected to a WiFi network.
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print("(v6) Not connected to WiFi\n");
    // Blink the LED
    digitalWrite(LED_PIN, ledStatus);  // Write LED high/low
    ledStatus = (ledStatus == HIGH) ? LOW : HIGH;

    // Delays allow the ESP8266 to perform critical tasks
    // defined outside of the sketch. These tasks include
    // setting up, and maintaining, a WiFi connection.
    delay(100);
    // Potentially infinite loops are generally dangerous.
    // Add delays -- allowing the processor to perform other
    // tasks -- wherever possible.
  }
  memset(lowStatus, 0, sizeof(lowStatus));
  strcpy(lowStatus, "Wifi: On");
  Serial.print("Connected to WiFi!\n");
}
