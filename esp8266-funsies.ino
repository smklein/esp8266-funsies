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
GoogleMapsApi gmaps_api(GMAPS_API_KEY, client);
unsigned long api_mtbs = 60000;  // mean time between api requests
unsigned long api_due_time = 0;
bool firstTime = true;

/////////////////
// Post Timing //
/////////////////
const unsigned long serialRate = 2000;
const unsigned long postRate = 30000;
unsigned long lastPost = 0;
unsigned long lastSerial = 0;

void displayTest() {
  // Draw a single pixel
  display.clearDisplay();
  display.drawPixel(10, 10, WHITE);
  display.display();
  delay(2000);

  // Draw some text
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  for (uint8_t i = 0; i < 168; i++) {
    if (i == '\n') continue;
    display.write(i);
    if ((i > 0) && (i % 21 == 0)) {
      display.println();
    }
  }
  display.display();
  delay(2000);
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
  displayTest();
}

void gmapsQuery() {
  Serial.println("Querying Google Maps");
  // Inputs
  String origin = "Galway";
  String destination = "Dublin,Ireland";
  String waypoints =
      "via:Cork,Ireland";  // You need to include the via: before your waypoint

  // These are all optional (although departureTime needed for traffic)
  String departureTime = "now";        // can also be a future timestamp
  String trafficModel = "best_guess";  // Defaults to this anyways
  String responseString = gmaps_api.distanceMatrix(origin, destination,
                                                   departureTime, trafficModel);
  DynamicJsonBuffer jsonBuffer;
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
}

void loop() {
  if (lastPost + postRate <= millis()) {
    gmapsQuery();
    /*
    if (postToPhant() == NO_ERROR) {
      lastPost = millis();
    } else {
      delay(100);
    }
    */
  }

  if (lastSerial + serialRate <= millis()) {
    Serial.print("...");
    lastSerial = millis();
  }
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