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

// Https
#include <WiFiClientSecure.h>

#define NO_ERROR 0

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
const int DIGITAL_PIN = 12;  // Digital pin to be read

///////////////
// LCD Setup //
///////////////
#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);

/////////////////
// Google Maps //
/////////////////
WiFiClientSecure client;
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
  printCapped(medStatus, 10);
  display.setTextSize(1);
  display.setCursor(0, 24);
  printCapped(lowStatus, 21);
  display.display();
}

void initHardware() {
  Serial.begin(9600);
  pinMode(DIGITAL_PIN, INPUT_PULLUP);
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
  connectWiFi();
  digitalWrite(LED_PIN, HIGH);
  draw();
}

// TODO(smklein): Refactor this into a headless auth library

#define GACCOUNT_HOST "accounts.google.com"
#define GACCOUNT_SSL_PORT 443

#define GCAL_SCOPE "https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fcalendar"

char oathDeviceCode[1024];
char oathUserCode[128];
char accessToken[1024];

String sendPostCommand(const String& host, int port,
                       const String& endpoint, const String& command) {
  if (!client.connect(host.c_str(), port)) {
    return "";
  }
  client.println("POST " + endpoint + " HTTP/1.1");
  client.println("Host: " + host);
  client.println("User-Agent: Arduino/1.0");
  client.print("Content-Length: ");
  client.println(command.length());
  client.println("Content-Type: application/x-www-form-urlencoded");
  client.println();
  client.println(command);


  // TODO(smklein): Technically, we shouldn't be reading chunks unless
  // we see "Transfer-Encoding: chunked" in the header...
  bool reading_header = true;
  bool reading_chunk = false;
  String header = "";
  String chunkLen = "";
  String body = "";

  long start = millis();
  while (millis() - start < 1500) {
    while (client.available()) {
      char c = client.read();
      if (reading_header) {
        header += c;
      } else if (reading_chunk) {
        chunkLen += c;
      } else {
        body += c;
      }

      if (reading_header && header.endsWith("\r\n\r\n")) {
        reading_header = false;
        reading_chunk = true;
      } else if (reading_chunk && chunkLen.endsWith("\r\n")) {
        reading_chunk = false;
        chunkLen = "";
      } else if (!reading_header && !reading_chunk && body.endsWith("\r\n")) {
        reading_chunk = true;
        body.trim();
      }
    }
    if (header != "" || body != "") {
      break;
    }
  }
  Serial.println("Message received from query: ");
  Serial.println(header);
  Serial.println("---");
  Serial.println(body);
  return body;
}

int deviceAndUserCodeQuery() {
  Serial.println("Querying for device and user codes");

  String command =
    "client_id=" GCAL_CLIENT_ID \
    "&scope=" GCAL_SCOPE;

  String responseString = sendPostCommand(GACCOUNT_HOST, GACCOUNT_SSL_PORT,
                                          "/o/oauth2/device/code", command);
  if (responseString == "") {
    Serial.println("Failed to send request to server");
    return -1;
  }

  Serial.println("Response from POST: ");
  Serial.println(responseString);

  DynamicJsonBuffer jsonBuffer;
  JsonObject& response = jsonBuffer.parseObject(responseString);
  if (!response.success()) {
    Serial.println("Failed to parse response");
    return -1;
  }
  Serial.println("Parsed JSON successfully");
  if (!response.containsKey("device_code") || !response.containsKey("user_code")) {
    Serial.println("JSON does not contain desired codes");
    return -1;
  }
  strncpy(oathDeviceCode, response["device_code"], sizeof(oathDeviceCode));
  strncpy(oathUserCode, response["user_code"], sizeof(oathUserCode));

  Serial.print("Device Code: ");
  Serial.println(String(oathDeviceCode));

  Serial.print("User Code: ");
  Serial.println(String(oathUserCode));

  /*
     https://developers.google.com/identity/protocols/OAuth2ForDevices
     TODO(smklein): Parse the following
     - "device_code": Will be used to refer to device asking for access
     - "user_code": Must be displayed to user, presented at verification url
     - "verification_url": Must be accessed by user
     - "expires_in": Restart after this amount of time
     - "interval": Interval this device should (minimally) wait between polling
       for authenticated access
   */

  return 0;
}

#define GCAL_HOST "www.googleapis.com"
#define GCAL_SSL_PORT 443

int accessTokenQuery() {
  Serial.println("Polling for authentication confirmation, access token");
  String command =
    "client_id=" GCAL_CLIENT_ID \
    "&client_secret=" GCAL_CLIENT_SECRET \
    "&grant_type=http://oauth.net/grant_type/device/1.0";

  command += "&code=" + String(oathDeviceCode);

  String responseString = sendPostCommand(GCAL_HOST, GCAL_SSL_PORT,
                                          "/oauth2/v4/token", command);

  if (responseString == "") {
    return -1;
  }

  DynamicJsonBuffer jsonBuffer;
  JsonObject& response = jsonBuffer.parseObject(responseString);
  if (!response.success()) {
    Serial.println("Failed to parse response");
    return -1;
  }
  Serial.println("Parsed JSON successfully");

  if (response.containsKey("error")) {
    String responseError = response["error"];
    Serial.print("Cannot acquire access token due to error: ");
    Serial.println(responseError);
    return -1;
  }

  if (!response.containsKey("access_token")) {
    Serial.println("Response does not contain access token\n");
    return -1;
  }
  strncpy(accessToken, response["access_token"], sizeof(accessToken));

  Serial.print("Access Token: ");
  Serial.println(String(accessToken));
  /*
     https://developers.google.com/identity/protocols/OAuth2ForDevices
     TODO(smklein): Parse the following
     - "access_token": Used for future gcal requests
     - "refresh_token": Mechanism to refresh access token
        TODO: do this too; store in EEPROM?
        https://github.com/esp8266/Arduino/blob/master/libraries/EEPROM
     - "expires_in": Lifetime in seconds
   */
  return 0;
}

void gcalQuery() {
  Serial.println("Querying Google Calendar");
  String command =
    "GET https://www.googleapis.com/calendar/v3/calendars/primary/events?" \
    "orderBy=startTime" \
    "&singleEvents=true" \
    "&timeMax=2017-07-05T00%3A00%3A00-07%3A00" \
    "&timeMin=2017-07-04T00%3A00%3A00-07%3A00";

  command += "&access_token=" + String(accessToken);

  String body = "";

  if (client.connect(GCAL_HOST, GCAL_SSL_PORT)){
    Serial.println("... Connected to server");
    client.println(command);

    long start = millis();
    while (millis() - start < 1500) {
      while (client.available()) {
        char c = client.read();
        body += c;
      }
      if (body != "") {
        break;
      }
    }
    Serial.println("Message received from query: ");
    Serial.println(body);
  }

  // TODO not reading the body correctly here...
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

enum {
  STATE_INIT,
  STATE_ACCESS_TOKEN_QUERY,
  STATE_AUTHENTICATED,
} state = STATE_INIT;

void loop() {
  if ((state == STATE_INIT) || lastPost + postRate <= millis()) {
    strcpy(topStatus, "Recalculating...");
    draw();

    if (state == STATE_INIT) {
      Serial.println("Authenticating...");
      if (deviceAndUserCodeQuery()) {
        Serial.println("Error sending user auth query");
        delay(3000);
      } else {
        Serial.println("Access Token READY for user approval\n");
        state = STATE_ACCESS_TOKEN_QUERY;
      }
    }
    if (state == STATE_ACCESS_TOKEN_QUERY) {
      Serial.println("Trying to acquire access token...");
      if (accessTokenQuery()) {
        Serial.println("Error acquiring acess token");
        delay(5000);
      } else {
        Serial.println("AUTHENTICATED\n");
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
  }

  delay(30);
  draw();
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
