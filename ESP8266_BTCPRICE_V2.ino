#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <LedControl.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <PubSubClient.h>
#include <FS.h>

#include "webserver.h"  // webserver.h file

// Constants for configuration
const char* API_HOST = "api.coingecko.com";
const char* API_URL = "/api/v3/simple/price?ids=bitcoin&vs_currencies=usd";
const int UPDATE_INTERVAL = 60000;
const int DISPLAY_TOGGLE = 15000;
const int REFRESH_RATE = 1000;
const int TIME_OFFSET = 7200; // timezone GMT+2
const int MAX_RETRY = 3;
const int ALARM_DISPLAY_DURATION = 30000; //30s

// Pins for the LED display
const uint8_t DIN_PIN = D7;
const uint8_t CLK_PIN = D5;
const uint8_t LOAD_PIN = D6;

// NTP and display configuration
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", TIME_OFFSET);
WiFiManager wifiManager;
std::unique_ptr<BearSSL::WiFiClientSecure> wifiClient(new BearSSL::WiFiClientSecure);
LedControl lc = LedControl(DIN_PIN, CLK_PIN, LOAD_PIN, 1);
ESP8266WebServer server(80);

// MQTT Configuration
WiFiClient espClient;
PubSubClient mqttClient(espClient);
String mqttServer = "";
String mqttUser = "";
String mqttPassword = "";
const int mqttPort = 1883;
const char* mqttTopic = "sonos/play_spotify_alarm_btcclock"; //MQTT topic to play the alarm on remote speaker

// Global variables
uint32_t lastBitcoinPrice = 0;
bool showPrice = true;
bool lastUpdateSuccessful = false;
bool alarmEnabled = false;
uint8_t alarmHour = 0;
uint8_t alarmMinute = 0;
bool alarmTriggered = false;
uint8_t currentBrightness = 0;
uint32_t alarmTriggerTime = 0;
bool alarmHandledForDay = false;

// Task timing variables, avoid using delay()
unsigned long previousRefreshMillis = 0;
unsigned long previousMqttReconnectMillis = 0;
unsigned long previousWiFiReconnectMillis = 0;
unsigned long previousDisplayToggleMillis = 0;
unsigned long previousPriceUpdateMillis = 0;

// Function prototypes
void saveMqttConfig();
void loadMqttConfig();
bool updateBitcoinPrice();
void setupDisplay();
bool initializeWiFiAndServices();
void SendMQTTMessage();
void DisplayNumber(uint32_t number);
void DisplayTime(uint8_t hours, uint8_t minutes);
void DisplayManager();

// Function to save MQTT configuration to SPIFFS
void saveMqttConfig() {
    StaticJsonDocument<256> doc;
    doc["mqtt_ip"] = mqttServer;
    doc["mqtt_user"] = mqttUser;
    doc["mqtt_password"] = mqttPassword;

    File configFile = SPIFFS.open("/mqtt_config.json", "w");
    if (!configFile) {
        Serial.println("Failed to open config file for writing");
        return;
    }
    serializeJson(doc, configFile);
    configFile.close();
}

// Function to load MQTT configuration from SPIFFS
void loadMqttConfig() {
    File configFile = SPIFFS.open("/mqtt_config.json", "r");
    if (!configFile) {
        Serial.println("Failed to open config file");
        return;
    }

    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, configFile);
    if (error) {
        Serial.println("Failed to parse config file");
        return;
    }

    mqttServer = doc["mqtt_ip"].as<String>();
    mqttUser = doc["mqtt_user"].as<String>();
    mqttPassword = doc["mqtt_password"].as<String>();

    configFile.close();
}

// Function to get the Bitcoin price via API
bool updateBitcoinPrice() {
    HTTPClient http;
    String url = String(F("https://")) + API_HOST + API_URL;

    for (int retry = 0; retry < MAX_RETRY; retry++) {
        http.begin(*wifiClient, url);
        int httpCode = http.GET();

        if (httpCode == HTTP_CODE_OK) {
            StaticJsonDocument<200> doc;
            DeserializationError error = deserializeJson(doc, http.getString());

            if (!error) {
                lastBitcoinPrice = doc["bitcoin"]["usd"].as<uint32_t>();
                Serial.printf_P(PSTR("Bitcoin price: $%u\n"), lastBitcoinPrice);
                http.end();
                return true;
            }
        }
        delay(1000 + (retry * 500));  // Adding exponential backoff between retries
    }

    http.end();
    return false;
}

// Function to initialize the LED display
void setupDisplay() {
    lc.shutdown(0, false);
    lc.setIntensity(0, currentBrightness);
    lc.clearDisplay(0);
}

// Wi-Fi and associated services initialization
bool initializeWiFiAndServices() {
    if (!wifiManager.autoConnect("BTCTICKERAP")) {
        Serial.println(F("Failed to connect to WiFi"));
        return false;
    }
    Serial.println(F("WiFi connected"));
    Serial.print(F("IP address: "));
    Serial.println(WiFi.localIP());

    wifiClient->setInsecure();
    timeClient.begin();
    timeClient.setTimeOffset(TIME_OFFSET);

    if (mqttServer.isEmpty() || mqttUser.isEmpty() || mqttPassword.isEmpty()) {
        Serial.println(F("MQTT settings are incomplete. Skipping MQTT connection."));
        return true;
    }

    mqttClient.setServer(mqttServer.c_str(), mqttPort);
    if (!mqttClient.connect("ESP8266Client", mqttUser.c_str(), mqttPassword.c_str())) {
        Serial.println(F("Failed to connect to MQTT broker"));
    } else {
        Serial.println(F("Connected to MQTT broker"));
    }
    return true;
}

// Send MQTT message for the alarm
void SendMQTTMessage() {
    StaticJsonDocument<256> doc;
    doc["media_content_id"] = "spotify:track:5DzPLP7e17GxxHMt9d8bwo";
    doc["media_content_type"] = "music";
    doc["volume_level"] = 0.3;

    char jsonBuffer[256];
    serializeJson(doc, jsonBuffer);

    if (mqttClient.publish(mqttTopic, jsonBuffer)) {
        Serial.println(F("MQTT message sent successfully"));
    } else {
        Serial.println(F("Failed to send MQTT message"));
    }
}

// Display number on the LED display
void DisplayNumber(uint32_t number) {
    lc.clearDisplay(0);
    uint8_t numDigits = number == 0 ? 1 : log10(number) + 1;
    uint8_t startPos = (8 - numDigits) / 2;

    if (number == 0) {
        lc.setDigit(0, 7 - startPos, 0, false);
        return;
    }

    char buffer[9];
    sprintf(buffer, "%u", number);

    for (uint8_t i = 0; i < numDigits; i++) {
        lc.setDigit(0, 7 - (startPos + i), buffer[i] - '0', false);
    }
}

// Display the time on the LED display
void DisplayTime(uint8_t hours, uint8_t minutes) {
    lc.clearDisplay(0);
    const uint8_t startPos = 2;
    lc.setDigit(0, 7 - startPos, hours / 10, false);
    lc.setDigit(0, 7 - (startPos + 1), hours % 10, true);
    lc.setDigit(0, 7 - (startPos + 2), minutes / 10, false);
    lc.setDigit(0, 7 - (startPos + 3), minutes % 10, false);
}

// Management of the display (price or time)
void DisplayManager() {
    if (alarmTriggered && millis() - alarmTriggerTime < ALARM_DISPLAY_DURATION) {
        DisplayNumber(88888888);
        return;
    }

    if (alarmTriggered && millis() - alarmTriggerTime >= ALARM_DISPLAY_DURATION) {
        alarmTriggered = false;
    }

    if (showPrice && lastUpdateSuccessful) {
        DisplayNumber(lastBitcoinPrice);
    } else {
        timeClient.update();
        DisplayTime(timeClient.getHours(), timeClient.getMinutes());
    }
}


void setup() {
    Serial.begin(115200);

    // SPIFFS initialization
    if (!SPIFFS.begin()) {
        Serial.println("Failed to mount file system");
        return;
    }

    // Load MQTT configuration from SPIFFS
    loadMqttConfig();

    setupDisplay();

    if (!initializeWiFiAndServices()) {
        Serial.println(F("WiFi initialization failed, but proceeding to start the web server..."));
    }

    setupWebServer(server, mqttServer, mqttUser, mqttPassword, alarmHour, alarmMinute, alarmEnabled, currentBrightness);

    previousDisplayToggleMillis = millis();
    previousPriceUpdateMillis = millis();
    lastUpdateSuccessful = updateBitcoinPrice();

    if (lastUpdateSuccessful) {
        DisplayNumber(lastBitcoinPrice);
    } else {
        timeClient.update();
        DisplayTime(timeClient.getHours(), timeClient.getMinutes());
    }
}


void loop() {
    unsigned long currentMillis = millis();

    // Reconnect MQTT
    if (!mqttClient.connected()) {
        if (currentMillis - previousMqttReconnectMillis >= 5000) {  // Tous les 5 secondes
            previousMqttReconnectMillis = currentMillis;
            Serial.println(F("Attempting MQTT connection..."));
            if (mqttClient.connect("ESP8266Client", mqttUser.c_str(), mqttPassword.c_str())) {
                Serial.println(F("Reconnected to MQTT broker"));
            } else {
                Serial.println(F("Failed to reconnect to MQTT broker. Will retry in 5 seconds."));
            }
        }
    }

    // Reconnect WiFi
    if (WiFi.status() != WL_CONNECTED) {
        if (currentMillis - previousWiFiReconnectMillis >= 10000) {  // Tous les 10 secondes
            previousWiFiReconnectMillis = currentMillis;
            Serial.println(F("WiFi connection lost. Attempting to reconnect..."));
            WiFi.reconnect();
        }
    }

    // Web server
    server.handleClient();

    // MQTT
    mqttClient.loop();

    // Manage display
    if (currentMillis - previousRefreshMillis >= REFRESH_RATE) {
        previousRefreshMillis = currentMillis;
        DisplayManager();
    }

    // Alternate between time and price
    if (currentMillis - previousDisplayToggleMillis >= DISPLAY_TOGGLE) {
        previousDisplayToggleMillis = currentMillis;
        showPrice = !showPrice;
    }

    // Update Bitcoin price
    if (currentMillis - previousPriceUpdateMillis >= UPDATE_INTERVAL) {
        previousPriceUpdateMillis = currentMillis;
        lastUpdateSuccessful = updateBitcoinPrice();
    }

    // Check alarm
    timeClient.update();
    uint8_t hours = timeClient.getHours();
    uint8_t minutes = timeClient.getMinutes();

    if (alarmEnabled && hours == alarmHour && minutes == alarmMinute && !alarmTriggered && !alarmHandledForDay) {
        alarmTriggered = true;
        alarmTriggerTime = currentMillis;
        SendMQTTMessage();
        DisplayNumber(8888);
        alarmHandledForDay = true;
    }

    // Reset alarm once triggered
    if (hours != alarmHour || minutes != alarmMinute) {
        alarmHandledForDay = false;
    }
}
