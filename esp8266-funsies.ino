// Experiments with esp8266 + Google Maps API
// Author: Sean Klein

// Include the ESP8266 WiFi library. (Works a lot like the
// Arduino WiFi library.)
#include <ESP8266WiFi.h>

// Include the SparkFun Phant library.
#include <Phant.h>

// I2C (for LCD)
#include <Wire.h>

// LCD
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// HTTPS
#include <GoogleMapsDirectionsApi.h>
#include <GoogleMapsApi.h>
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

////////////////
// Phant Keys //
////////////////
const char PhantHost[] = "data.sparkfun.com";
const char PhantPublicKey[] = "wpvZ9pE1qbFJAjaGd3bn";
const char PhantPrivateKey[] = PHANT_PRIVATE_KEY;

const char GoogleHost[] = "www.google.com";

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
bool firstTime = true;

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
  int tailLen = (strLen - start);
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

void gmapsQuery() {
  Serial.println("Querying Google Maps");
  // Inputs
  // TODO(smklein): I don't think the client library deals
  // spaces all too well.
  String origin = "Sunnyvale";
  String destination = "Googleplex";

  // These are all optional (although departureTime needed for traffic)
  String departureTime = "now";        // can also be a future timestamp
  String trafficModel = "best_guess";  // Defaults to this anyways
  DirectionsInputOptions options;
  options.departureTime = "now";
  options.trafficModel = "best_guess";
  DirectionsResponse response = gmaps_api.directionsApi(origin, destination, options);

  snprintf(topStatus, sizeof(topStatus), "%s%s%s%s   ",
           "Traffic from ",
           response.start_address.c_str(),
           " to ",
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

void loop() {
  if (firstTime || lastPost + postRate <= millis()) {
    memset(topStatus, 0, sizeof(topStatus));
    strcpy(topStatus, "Recalculating...");
    draw();
    gmapsQuery();
    lastPost = millis();
    firstTime = false;
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

int postToPhant() {
  Serial.print("Posting to Phant...\n");
  // LED turns on when we enter, it'll go off when we successfully post.
  digitalWrite(LED_PIN, HIGH);

  // Declare an object from the Phant library - phant
  Phant phant(PhantHost, PhantPublicKey, PhantPrivateKey);

  // Do a little work to get a unique-ish name. Append the
  // last two bytes of the MAC (HEX'd) to "Thing-":
  uint8_t mac[WL_MAC_ADDR_LENGTH];
  WiFi.macAddress(mac);
  String macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                 String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
  macID.toUpperCase();
  String postedID = "SK-Thing-" + macID;

  // Add the four field/value pairs defined by our stream:
  phant.add("id", postedID);
  phant.add("analog", analogRead(ANALOG_PIN));
  phant.add("digital", digitalRead(DIGITAL_PIN));
  phant.add("time", millis());

  // Now connect to data.sparkfun.com, and post our data:
  WiFiClient pclient;
  const int httpPort = 443;
  if (!pclient.connect(PhantHost, httpPort)) {
    return -1;
  }
  // If we successfully connected, print our Phant post:
  pclient.print(phant.post());

  // Read all the lines of the reply from server and print them to Serial
  delay(1000);
  while (pclient.available()) {
    String line = pclient.readStringUntil('\r');
    Serial.print(line);  // Trying to avoid using serial
  }
  Serial.print("Posted to phant (success)\n");
  pclient.stop();

  // Before we exit, turn the LED off.
  digitalWrite(LED_PIN, LOW);

  Serial.print("Posted to googs (success)\n");
  return NO_ERROR;
}
