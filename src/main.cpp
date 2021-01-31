#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <WiFiClientSecure.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <LedController.hpp>
#include <ElegantOTA.h>

#define DIN 15
#define CS 13
#define CLK 14

LedController<1, 1> lc;

ESP8266WebServer server(80);

const String COIN = "bitcoin";
const String CURRENCY = "usd";

const char *host = "api.coingecko.com";
const int httpsPort = 443;

// Use web browser to view and copy SHA1 fingerprint of the certificate
const char fingerprint[] PROGMEM = "8925605D5044FCC0852B98D7D3665228684DE6E2";

// Use WiFiClientSecure class to create TLS connection
WiFiClientSecure client;

WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org");

unsigned long previousMillis = 0;
unsigned long previousMillisPrice = 0;

const long interval = 60000;      //update time and Bitcoin price every minute
const long intervalDislay = 8000; //alternate display between price and time every 8 seconds

int hh, mm, digiTime;

bool bitTurn = false;

int bitcoinPrice;

// alternate display between price and time, every 8s (intervalDislay)

void alternate()
{

  unsigned long currentMillis = millis();

  if ((currentMillis - previousMillis >= intervalDislay) && bitTurn == true)
  {
    // save the last time you swap the display
    previousMillis = currentMillis;

    lc.clearMatrix();

    int h1 = hh / 10;
    int h2 = hh % 10;
    int m1 = mm / 10;
    int m2 = mm % 10;

    //display hours and minutes
    lc.setDigit(0, 5, h1, false);
    lc.setDigit(0, 4, h2, true);
    lc.setDigit(0, 3, m1, false);
    lc.setDigit(0, 2, m2, false);

    Serial.print("Time on display: ");
    Serial.println(digiTime);

    bitTurn = false;
  }

  else if ((currentMillis - previousMillis >= intervalDislay) && bitTurn == false)
  {
    // save the last time you swap the display
    previousMillis = currentMillis;

    lc.clearMatrix();

    int bitcoinToDisplay = bitcoinPrice;

    // keep Bitcoin price centered on display
    int index;
    if (bitcoinToDisplay < 100)
    {
      index = floor((8 - sizeof(bitcoinToDisplay)) / 2) + 1;
    }
    else if (bitcoinToDisplay < 10000)
    {
      index = floor((8 - sizeof(bitcoinToDisplay)) / 2);
    }
    else if (bitcoinToDisplay >= 1000000)
    {
      index = floor((8 - sizeof(bitcoinToDisplay)) / 2) - 2;
    }
    else
    {
      index = floor((8 - sizeof(bitcoinToDisplay)) / 2) - 1;
    }

    while (bitcoinToDisplay > 0)
    {
      lc.setDigit(0, index, bitcoinToDisplay % 10, false);
      bitcoinToDisplay = bitcoinToDisplay / 10;
      Serial.print("btc calc: ");
      Serial.println(bitcoinToDisplay);
      index++;
    }

    Serial.print("Price on display: ");
    Serial.println(bitcoinPrice);

    bitTurn = true;
  }
}

//update Bitcoin price and time function

void updatePriceTime()
{

  Serial.print("connecting to ");
  Serial.println(host);
  Serial.printf("Using fingerprint '%s'\n", fingerprint);

  client.setFingerprint(fingerprint);

  if (!client.connect(host, httpsPort))
  {
    Serial.println("connection failed");
    return;
  }

  String url = "/api/v3/simple/price?ids=" + COIN + "&vs_currencies=" + CURRENCY;

  Serial.print("requesting URL: ");
  Serial.println(url);

  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "User-Agent: BuildFailureDetectorESP8266\r\n" +
               "Connection: close\r\n\r\n");

  Serial.println("request sent");

  // check HTTP response
  char httpStatus[32] = {0};
  client.readBytesUntil('\r', httpStatus, sizeof(httpStatus));
  if (strcmp(httpStatus, "HTTP/1.1 200 OK") != 0)
  {
    Serial.print("Unexpected response: ");
    Serial.println(httpStatus);
    return;
  }

  // skip HTTP headers
  while (client.connected())
  {
    String line = client.readStringUntil('\n');
    Serial.println(line);
    if (line == "\r")
    {
      break;
    }
  }

  // skip content length
  if (client.connected())
  {
    String line = client.readStringUntil('\n');
    Serial.println(line);
  }

  // get response
  String response = "";
  while (client.connected())
  {
    String line = client.readStringUntil('\n');
    Serial.println(line);
    line.trim();
    if (line != "\r")
    {
      response += line;
    }
  }

  client.stop();

  // parse response
  DynamicJsonDocument jsonDocument(2024);
  DeserializationError error = deserializeJson(jsonDocument, response);
  if (error)
  {
    Serial.println("Deserialization failed");

    switch (error.code())
    {
    case DeserializationError::Ok:
      Serial.print(F("Deserialization succeeded"));
      break;
    case DeserializationError::InvalidInput:
      Serial.print(F("Invalid input!"));
      break;
    case DeserializationError::NoMemory:
      Serial.print(F("Not enough memory"));
      Serial.println("doc capacity: ");
      Serial.print(jsonDocument.capacity());
      break;
    default:
      Serial.print(F("Deserialization failed"));
      break;
    }

    return;
  }

  JsonObject root = jsonDocument.as<JsonObject>();
  JsonObject coin = root["bitcoin"];
  bitcoinPrice = coin["usd"];

  timeClient.update();
  hh = timeClient.getHours();
  mm = timeClient.getMinutes();
  digiTime = (hh * 100) + mm;

  Serial.print("Refreshed time: ");
  Serial.println(digiTime);
  //Serial.print(timeClient.getFormattedTime());
}

void setup()
{

  Serial.begin(115200);

  WiFiManager wifiManager;
  wifiManager.autoConnect("AutoConnectAP","pwd123456");

  lc = LedController<1, 1>(CS, CLK, DIN);

  lc.activateAllSegments();
  /* Set the brightness to a medium values */
  lc.setIntensity(1);
  /* and clear the display */
  lc.clearMatrix();

  timeClient.begin();
  timeClient.setTimeOffset(3600); //Time offset, adjust depends your timezone

  updatePriceTime();

  // starts OTA manager
  ElegantOTA.begin(&server);
  server.begin();
  Serial.println("HTTP server started");
}

void loop()
{

  unsigned long currentMillis = millis();

  //Update time and Bitcoin price every minute (interval)

  if (currentMillis - previousMillisPrice >= interval)
  {
    // save the last time price have been checked
    previousMillisPrice = currentMillis;

    updatePriceTime();
  }

  alternate();

  server.handleClient(); // needed for OTA
}