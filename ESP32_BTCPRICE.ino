/* ============================================================================
 * ESP32_BTCPRICE  -  Bitcoin price + NTP time ticker on a MAX7219 display
 * Firmware version 2.2.0
 * ----------------------------------------------------------------------------
 * ESP32 (ESP-WROOM-32, D1 mini form factor).
 *
 * v2.2.0 adds:
 *   - Display mode setting: alternate (default) / price only / time only
 *
 * v2.1.0 adds:
 *   - Dark web theme by default (a saved "light" choice is still respected)
 *   - On-device price history (sampled every 15 min) -> persistent chart
 *   - WiFi reset button in the Settings tab (with confirmation)
 *   - Firmware version shown in the UI
 *
 * WIRING (MAX7219):  DIN->GPIO23   CLK->GPIO18   CS/LOAD->GPIO19
 *                    VCC->5V (VCC pin)   GND->GND
 *
 * LIBRARIES: WiFiManager (tzapu), ArduinoJson 7.x, LedControl, PubSubClient, ElegantOTA 3.x
 * BOARD    : "ESP32 Dev Module", 4MB flash, default partition scheme
 * ========================================================================== */

#include <WiFi.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <LedControl.h>
#include <Preferences.h>
#include <WebServer.h>
#include <PubSubClient.h>
#include <ElegantOTA.h>
#include <time.h>

#include "webserver.h"

// ---------- Firmware version (bump on each release) ----------
const char* FIRMWARE_VERSION = "2.2.0";

// ---------- Fixed configuration ----------
const char*   API_HOST            = "api.coinbase.com";
const unsigned long UPDATE_INTERVAL = 60000;    // normal price refresh (ms)
const unsigned long RETRY_INTERVAL  = 10000;    // retry delay after a failed fetch (ms)
const unsigned long REFRESH_RATE    = 1000;     // display refresh (ms)
const int          MAX_RETRY        = 3;
const unsigned long ALARM_DISPLAY_DURATION = 30000;
const unsigned long HISTORY_INTERVAL = 900000;  // record a chart point every 15 min (ms)

const char* DEFAULT_TZ = "CET-1CEST,M3.5.0,M10.5.0/3";
const char* DEFAULT_TOPIC = "sonos/play_alarm_btcclock";

// ---------- MAX7219 pins ----------
const uint8_t DIN_PIN  = 23;
const uint8_t CLK_PIN  = 18;
const uint8_t LOAD_PIN = 19;

// ---------- Objects ----------
WiFiManager  wifiManager;
LedControl   lc = LedControl(DIN_PIN, CLK_PIN, LOAD_PIN, 1);
WebServer    server(80);
Preferences  preferences;

WiFiClient   espClient;
PubSubClient mqttClient(espClient);
String       mqttServer   = "";
String       mqttUser     = "";
String       mqttPassword = "";
const int    mqttPort     = 1883;

// ---------- User-configurable settings (persisted) ----------
String   currencyCode     = "USD";
String   tzString         = DEFAULT_TZ;
uint16_t displayToggleSec = 15;
String   mqttTopicStr     = DEFAULT_TOPIC;

// Display modes
const uint8_t MODE_ALTERNATE  = 0;
const uint8_t MODE_PRICE_ONLY = 1;
const uint8_t MODE_TIME_ONLY  = 2;
uint8_t  displayMode = MODE_ALTERNATE;

// ---------- State ----------
uint32_t lastBitcoinPrice    = 0;
bool     showPrice           = true;
bool     lastUpdateSuccessful = false;
bool     alarmEnabled        = false;
uint8_t  alarmHour           = 0;
uint8_t  alarmMinute         = 0;
bool     alarmTriggered      = false;
uint8_t  currentBrightness   = 8;
uint32_t alarmTriggerTime    = 0;
bool     alarmHandledForDay  = false;

// ---------- Price fetch scheduling (non-blocking) ----------
unsigned long nextFetchAt    = 0;
int           fetchRetryCount = 0;

// ---------- Price history (RAM ring buffer, 15-min samples) ----------
const uint16_t HIST_SIZE = 96;           // 96 * 15 min = 24 h
uint32_t  histEpoch[HIST_SIZE];
uint32_t  histPrice[HIST_SIZE];
uint16_t  histHead  = 0;
uint16_t  histCount = 0;
bool      firstHistoryRecorded = false;
unsigned long lastHistoryMillis = 0;

// ---------- Loop timers ----------
unsigned long previousRefreshMillis       = 0;
unsigned long previousMqttReconnectMillis = 0;
unsigned long previousWiFiReconnectMillis = 0;
unsigned long previousDisplayToggleMillis = 0;

// ---------- Prototypes ----------
void loadConfig();
void saveMqttConfig();
void saveBrightnessConfig();
void saveAlarmConfig();
void saveSettingsConfig();
bool attemptPriceFetch();
void requestPriceRefresh();
void applyTimezone();
void applyMqttConfig();
void resetWifiSettings();
void recordHistoryPoint();
String getHistoryJson();
void getClockTime(uint8_t& hours, uint8_t& minutes);
void setupDisplay();
bool initializeWiFiAndServices();
void SendMQTTMessage();
void DisplayNumber(uint32_t number);
void DisplayTime(uint8_t hours, uint8_t minutes);
void DisplayManager();

// ============================================================================
//  Persistent configuration (Preferences / NVS)
// ============================================================================
void loadConfig() {
    preferences.begin("btcclock", true);
    mqttServer        = preferences.getString("mqtt_ip", "");
    mqttUser          = preferences.getString("mqtt_user", "");
    mqttPassword      = preferences.getString("mqtt_pass", "");
    currentBrightness = preferences.getUChar("brightness", 8);
    alarmHour         = preferences.getUChar("alarm_h", 0);
    alarmMinute       = preferences.getUChar("alarm_m", 0);
    alarmEnabled      = preferences.getBool("alarm_en", false);
    currencyCode      = preferences.getString("currency", "USD");
    tzString          = preferences.getString("tz", DEFAULT_TZ);
    displayToggleSec  = preferences.getUShort("toggle_s", 15);
    mqttTopicStr      = preferences.getString("mqtt_topic", DEFAULT_TOPIC);
    displayMode       = preferences.getUChar("disp_mode", MODE_ALTERNATE);
    preferences.end();
}

void saveMqttConfig() {
    preferences.begin("btcclock", false);
    preferences.putString("mqtt_ip", mqttServer);
    preferences.putString("mqtt_user", mqttUser);
    preferences.putString("mqtt_pass", mqttPassword);
    preferences.end();
}

void saveBrightnessConfig() {
    preferences.begin("btcclock", false);
    preferences.putUChar("brightness", currentBrightness);
    preferences.end();
}

void saveAlarmConfig() {
    preferences.begin("btcclock", false);
    preferences.putUChar("alarm_h", alarmHour);
    preferences.putUChar("alarm_m", alarmMinute);
    preferences.putBool("alarm_en", alarmEnabled);
    preferences.end();
}

void saveSettingsConfig() {
    preferences.begin("btcclock", false);
    preferences.putString("currency", currencyCode);
    preferences.putString("tz", tzString);
    preferences.putUShort("toggle_s", displayToggleSec);
    preferences.putString("mqtt_topic", mqttTopicStr);
    preferences.putUChar("disp_mode", displayMode);
    preferences.end();
}

// ============================================================================
//  Bitcoin price - single non-blocking attempt
// ============================================================================
bool attemptPriceFetch() {
    if (WiFi.status() != WL_CONNECTED) return false;

    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setConnectTimeout(4000);
    http.setTimeout(4000);

    String url = "https://" + String(API_HOST) + "/v2/prices/BTC-" + currencyCode + "/spot";

    bool ok = false;
    if (http.begin(client, url)) {
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, http.getString());
            if (!err) {
                const char* amount = doc["data"]["amount"];
                if (amount != nullptr) {
                    lastBitcoinPrice     = (uint32_t) strtoul(amount, nullptr, 10);
                    lastUpdateSuccessful = true;
                    Serial.printf("Bitcoin price: %u %s\n", lastBitcoinPrice, currencyCode.c_str());
                    ok = true;
                }
            }
        } else {
            Serial.printf("HTTP GET failed, code: %d\n", httpCode);
        }
        http.end();
    } else {
        Serial.println("Unable to start HTTPS connection");
    }
    return ok;
}

void requestPriceRefresh() {
    nextFetchAt     = millis();
    fetchRetryCount = 0;
}

// ============================================================================
//  Price history
// ============================================================================
void recordHistoryPoint() {
    histEpoch[histHead] = (uint32_t) time(nullptr);
    histPrice[histHead] = lastBitcoinPrice;
    histHead = (histHead + 1) % HIST_SIZE;
    if (histCount < HIST_SIZE) histCount++;
}

String getHistoryJson() {
    JsonDocument doc;
    JsonArray labels = doc["labels"].to<JsonArray>();
    JsonArray prices = doc["prices"].to<JsonArray>();

    uint16_t idx = (histHead + HIST_SIZE - histCount) % HIST_SIZE;  // oldest first
    for (uint16_t i = 0; i < histCount; i++) {
        time_t t = (time_t) histEpoch[idx];
        struct tm tmv;
        localtime_r(&t, &tmv);
        char buf[6];
        snprintf(buf, sizeof(buf), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
        labels.add(String(buf));
        prices.add(histPrice[idx]);
        idx = (idx + 1) % HIST_SIZE;
    }

    String out;
    serializeJson(doc, out);
    return out;
}

// ============================================================================
//  Time
// ============================================================================
void applyTimezone() {
    setenv("TZ", tzString.c_str(), 1);
    tzset();
}

void getClockTime(uint8_t& hours, uint8_t& minutes) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 50)) {
        hours   = (uint8_t) timeinfo.tm_hour;
        minutes = (uint8_t) timeinfo.tm_min;
    } else {
        hours = 0;
        minutes = 0;
    }
}

// ============================================================================
//  Display
// ============================================================================
void setupDisplay() {
    lc.shutdown(0, false);
    lc.setIntensity(0, currentBrightness);
    lc.clearDisplay(0);
}

void DisplayNumber(uint32_t number) {
    lc.clearDisplay(0);
    uint8_t numDigits = number == 0 ? 1 : log10(number) + 1;
    uint8_t startPos  = (8 - numDigits) / 2;

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

void DisplayTime(uint8_t hours, uint8_t minutes) {
    lc.clearDisplay(0);
    const uint8_t startPos = 2;
    lc.setDigit(0, 7 - startPos,       hours / 10,   false);
    lc.setDigit(0, 7 - (startPos + 1), hours % 10,   true);
    lc.setDigit(0, 7 - (startPos + 2), minutes / 10, false);
    lc.setDigit(0, 7 - (startPos + 3), minutes % 10, false);
}

void DisplayManager() {
    if (alarmTriggered && millis() - alarmTriggerTime < ALARM_DISPLAY_DURATION) {
        DisplayNumber(88888888);
        return;
    }
    if (alarmTriggered && millis() - alarmTriggerTime >= ALARM_DISPLAY_DURATION) {
        alarmTriggered = false;
    }

    struct tm timeinfo;
    bool timeOk = getLocalTime(&timeinfo, 50);

    // Price only / Time only: show just that, blank until the data is available
    if (displayMode == MODE_PRICE_ONLY) {
        if (lastUpdateSuccessful) DisplayNumber(lastBitcoinPrice);
        else lc.clearDisplay(0);
        return;
    }
    if (displayMode == MODE_TIME_ONLY) {
        if (timeOk) DisplayTime(timeinfo.tm_hour, timeinfo.tm_min);
        else lc.clearDisplay(0);
        return;
    }

    // Alternate (default)
    if (showPrice && lastUpdateSuccessful) {
        DisplayNumber(lastBitcoinPrice);
    } else if (timeOk) {
        DisplayTime(timeinfo.tm_hour, timeinfo.tm_min);
    } else if (lastUpdateSuccessful) {
        DisplayNumber(lastBitcoinPrice);
    } else {
        lc.clearDisplay(0);
    }
}

// ============================================================================
//  Wi-Fi / services
// ============================================================================
bool initializeWiFiAndServices() {
    if (!wifiManager.autoConnect("BTCTICKERAP")) {
        Serial.println("Failed to connect to WiFi");
        return false;
    }
    Serial.println("WiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    configTzTime(tzString.c_str(), "pool.ntp.org", "time.google.com", "time.cloudflare.com");

    if (mqttServer.isEmpty() || mqttUser.isEmpty() || mqttPassword.isEmpty()) {
        Serial.println("MQTT settings are incomplete. Skipping MQTT connection.");
        return true;
    }

    mqttClient.setServer(mqttServer.c_str(), mqttPort);
    if (!mqttClient.connect("ESP32Client", mqttUser.c_str(), mqttPassword.c_str())) {
        Serial.println("Failed to connect to MQTT broker");
    } else {
        Serial.println("Connected to MQTT broker");
    }
    return true;
}

void applyMqttConfig() {
    if (mqttClient.connected()) {
        mqttClient.disconnect();
    }
    if (!mqttServer.isEmpty()) {
        mqttClient.setServer(mqttServer.c_str(), mqttPort);
    }
    previousMqttReconnectMillis = 0;
}

// Clear stored WiFi credentials and restart -> reopens the BTCTICKERAP portal
void resetWifiSettings() {
    Serial.println("Resetting WiFi settings...");
    wifiManager.resetSettings();
    delay(500);
    ESP.restart();
}

void SendMQTTMessage() {
    JsonDocument doc;
    doc["media_content_id"]   = "spotify:track:5DzPLP7e17GxxHMt9d8bwo"; // ignored by HA favorites flow
    doc["media_content_type"] = "music";
    doc["volume_level"]       = 0.3;

    char jsonBuffer[256];
    serializeJson(doc, jsonBuffer);

    if (mqttClient.publish(mqttTopicStr.c_str(), jsonBuffer)) {
        Serial.println("MQTT message sent successfully");
    } else {
        Serial.println("Failed to send MQTT message");
    }
}

// ============================================================================
//  Setup
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(100);

    loadConfig();
    applyTimezone();
    setupDisplay();

    if (!initializeWiFiAndServices()) {
        Serial.println("WiFi init failed, starting the web server anyway...");
    }

    setupWebServer(server, mqttServer, mqttUser, mqttPassword,
                   alarmHour, alarmMinute, alarmEnabled, currentBrightness);

    ElegantOTA.begin(&server);
    server.begin();
    Serial.println("HTTP server started");

    previousDisplayToggleMillis = millis();

    attemptPriceFetch();
    nextFetchAt = millis() + UPDATE_INTERVAL;
}

// ============================================================================
//  Loop
// ============================================================================
void loop() {
    unsigned long currentMillis = millis();
    bool mqttConfigured = !mqttServer.isEmpty() && !mqttUser.isEmpty() && !mqttPassword.isEmpty();

    // --- MQTT reconnect (server re-applied each attempt -> live config works without reboot) ---
    if (mqttConfigured && !mqttClient.connected()) {
        if (currentMillis - previousMqttReconnectMillis >= 5000) {
            previousMqttReconnectMillis = currentMillis;
            mqttClient.setServer(mqttServer.c_str(), mqttPort);
            Serial.println("Attempting MQTT connection...");
            if (mqttClient.connect("ESP32Client", mqttUser.c_str(), mqttPassword.c_str())) {
                Serial.println("Connected to MQTT broker");
            } else {
                Serial.println("Failed to connect to MQTT broker. Will retry in 5 s.");
            }
        }
    }

    // --- WiFi reconnect ---
    if (WiFi.status() != WL_CONNECTED) {
        if (currentMillis - previousWiFiReconnectMillis >= 10000) {
            previousWiFiReconnectMillis = currentMillis;
            Serial.println("WiFi connection lost. Attempting to reconnect...");
            WiFi.reconnect();
        }
    }

    server.handleClient();
    ElegantOTA.loop();
    if (mqttConfigured) mqttClient.loop();

    // --- Display refresh ---
    if (currentMillis - previousRefreshMillis >= REFRESH_RATE) {
        previousRefreshMillis = currentMillis;
        DisplayManager();
    }

    // --- Alternate price / time ---
    if (currentMillis - previousDisplayToggleMillis >= (unsigned long)displayToggleSec * 1000UL) {
        previousDisplayToggleMillis = currentMillis;
        showPrice = !showPrice;
    }

    // --- Price fetch scheduler (non-blocking, rollover-safe) ---
    if ((long)(currentMillis - nextFetchAt) >= 0) {
        bool ok = attemptPriceFetch();
        if (ok) {
            fetchRetryCount = 0;
            nextFetchAt = currentMillis + UPDATE_INTERVAL;
        } else if (fetchRetryCount < MAX_RETRY) {
            fetchRetryCount++;
            nextFetchAt = currentMillis + RETRY_INTERVAL;
        } else {
            fetchRetryCount = 0;
            nextFetchAt = currentMillis + UPDATE_INTERVAL;
        }
    }

    // --- Price history sampling: first point at the first reading, then every 15 min ---
    if (!firstHistoryRecorded || (currentMillis - lastHistoryMillis >= HISTORY_INTERVAL)) {
        struct tm tmv;
        if (lastUpdateSuccessful && getLocalTime(&tmv, 0)) {
            recordHistoryPoint();
            lastHistoryMillis    = currentMillis;
            firstHistoryRecorded = true;
        }
    }

    // --- Alarm (only when the clock is synced) ---
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 0)) {
        uint8_t hours   = timeinfo.tm_hour;
        uint8_t minutes = timeinfo.tm_min;

        if (alarmEnabled && hours == alarmHour && minutes == alarmMinute &&
            !alarmTriggered && !alarmHandledForDay) {
            alarmTriggered     = true;
            alarmTriggerTime   = currentMillis;
            SendMQTTMessage();
            DisplayNumber(88888888);
            alarmHandledForDay = true;
        }

        if (hours != alarmHour || minutes != alarmMinute) {
            alarmHandledForDay = false;
        }
    }
}
