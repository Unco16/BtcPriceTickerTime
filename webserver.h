/* ============================================================================
 * webserver.h  -  ESP32 web UI for the Bitcoin ticker (firmware 2.2.0)
 * ----------------------------------------------------------------------------
 * Timezone is now a friendly dropdown, the chart is responsive (fits without
 * scrolling), and brightness is a horizontal slider applied on release.
 * ========================================================================== */
#pragma once

#include <WebServer.h>
#include <ArduinoJson.h>
#include <LedControl.h>

// Globals defined in the main sketch
extern bool        lastUpdateSuccessful;
extern uint32_t    lastBitcoinPrice;
extern LedControl  lc;
extern String      currencyCode;
extern String      tzString;
extern uint16_t    displayToggleSec;
extern uint8_t     displayMode;
extern String      mqttTopicStr;
extern const char* FIRMWARE_VERSION;

// Functions defined in the main sketch
void saveMqttConfig();
void saveBrightnessConfig();
void saveAlarmConfig();
void saveSettingsConfig();
void getClockTime(uint8_t& hours, uint8_t& minutes);
void applyTimezone();
void applyMqttConfig();
void requestPriceRefresh();
void resetWifiSettings();
String getHistoryJson();

// ---------------------------------------------------------------------------
//  Timezone options: friendly label -> POSIX TZ string (DST handled by the OS)
// ---------------------------------------------------------------------------
struct TzOption { const char* label; const char* posix; };
static const TzOption TZ_OPTIONS[] = {
    {"Europe/Amsterdam (CET/CEST)",   "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Europe/London (GMT/BST)",       "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Lisbon (WET/WEST)",      "WET0WEST,M3.5.0/1,M10.5.0"},
    {"Europe/Athens (EET/EEST)",      "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Europe/Moscow (MSK)",           "MSK-3"},
    {"UTC",                           "UTC0"},
    {"America/New_York (ET)",         "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago (CT)",          "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver (MT)",           "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Los_Angeles (PT)",      "PST8PDT,M3.2.0,M11.1.0"},
    {"America/Sao_Paulo (BRT)",       "<-03>3"},
    {"Asia/Dubai (GST)",              "<+04>-4"},
    {"Asia/Kolkata (IST)",            "IST-5:30"},
    {"Asia/Shanghai (CST)",           "CST-8"},
    {"Asia/Tokyo (JST)",              "JST-9"},
    {"Australia/Sydney (AEST/AEDT)",  "AEST-10AEDT,M10.1.0,M4.1.0/3"},
};

// ---------------------------------------------------------------------------
//  Static page assets (kept in flash to avoid building the whole page in RAM)
// ---------------------------------------------------------------------------
static const char PAGE_SCRIPTS[] PROGMEM = R"rawliteral(
<script>
function openTab(evt, tabName) {
  var i, tabcontent, tablinks;
  tabcontent = document.getElementsByClassName('tabcontent');
  for (i = 0; i < tabcontent.length; i++) { tabcontent[i].style.display = 'none'; }
  tablinks = document.getElementsByClassName('tablinks');
  for (i = 0; i < tablinks.length; i++) { tablinks[i].className = tablinks[i].className.replace(' active', ''); }
  document.getElementById(tabName).style.display = 'block';
  evt.currentTarget.className += ' active';
}
document.getElementsByClassName('tablinks')[0].click();
</script>
<script>
// Dark theme is the default; only an explicit 'light' choice opts out.
let isDarkTheme = localStorage.getItem('theme') !== 'light';
applyTheme();
function toggleTheme() {
  isDarkTheme = !isDarkTheme;
  localStorage.setItem('theme', isDarkTheme ? 'dark' : 'light');
  applyTheme();
}
function applyTheme() {
  document.body.style.backgroundColor = isDarkTheme ? '#121212' : '#d3d3d3';
  document.body.style.color = isDarkTheme ? '#ffffff' : '#000000';
  document.querySelector('.tabs').style.backgroundColor = isDarkTheme ? '#121212' : '#f0f0f0';
}
</script>
<script>
function restartDevice() {
  if (confirm('Are you sure you want to restart the device?')) {
    fetch('/restart', { method: 'POST' }).then(r => r.text()).then(d => alert(d));
  }
}
function resetWifi() {
  if (confirm('Reset WiFi settings? The device will restart and open the BTCTICKERAP setup network so you can reconnect it to a WiFi network.')) {
    fetch('/reset_wifi', { method: 'POST' }).then(r => r.text()).then(d => alert(d));
  }
}
function setBrightness(v) {
  document.getElementById('brightnessValue').innerText = v;
  fetch('/set_brightness', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: 'brightness=' + encodeURIComponent(v) });
}
</script>
<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>
<script>
var bitcoinChart = null;
function loadHistory() {
  fetch('/api/history').then(r => r.json()).then(data => {
    bitcoinChart.data.labels = data.labels;
    bitcoinChart.data.datasets[0].data = data.prices;
    bitcoinChart.update();
  });
}
window.onload = function() {
  var ctx = document.getElementById('bitcoinChart').getContext('2d');
  bitcoinChart = new Chart(ctx, {
    type: 'line',
    data: { labels: [], datasets: [{ label: 'Bitcoin Price', data: [],
            borderColor: 'rgba(75, 192, 192, 1)', borderWidth: 2, fill: false }] },
    options: { responsive: true, maintainAspectRatio: false,
               scales: { x: { type: 'category' }, y: { beginAtZero: false } } }
  });
  loadHistory();
};
// Live header (price text / time / alarm) every 10 s
setInterval(function() {
  fetch('/api/data').then(response => response.json()).then(data => {
    document.getElementById('bitcoinPrice').innerText = data.price + ' ' + data.currency;
    document.querySelector('h1').innerText = data.time;
    var as = document.getElementById('alarmStatus');
    if (as) { as.innerText = data.alarm_enabled ? ('Alarm: ON (' + data.alarm_time + ')') : 'Alarm: OFF'; }
  });
}, 10000);
// Refresh the chart from on-device history every 60 s
setInterval(loadHistory, 60000);
</script>
)rawliteral";

static const char PAGE_STYLES[] PROGMEM = R"rawliteral(
<style>
button, input[type='submit'] { background-color: #4CAF50; color: white; padding: 10px 20px; margin: 5px 0; border: none; border-radius: 5px; cursor: pointer; transition: background-color 0.3s; }
button:hover, input[type='submit']:hover { background-color: #45a049; }
input[type='text'], input[type='number'], input[type='password'], select { padding: 8px; margin: 5px 0; border: 1px solid #ccc; border-radius: 4px; }
input[type='range'] { width: 100%; max-width: 400px; height: 28px; cursor: pointer; }
.tabs { overflow: hidden; background-color: #f0f0f0; padding: 10px; position: relative; }
.toggle-theme { float: right; background-color: #4CAF50; margin-left: auto; }
.restart-button { background-color: #f44336; color: white; margin-left: 10px; }
.restart-button:hover { background-color: #d32f2f; }
.tablinks { float: left; border: none; outline: none; cursor: pointer; padding: 14px 16px; transition: 0.3s; }
.tablinks.active { background-color: #FFA500; }
.tabcontent { display: none; padding: 6px 12px; border-top: none; }
.alarm-status { font-size: 1.2em; font-weight: bold; }
.fw-version { font-size: 0.85em; opacity: 0.8; }
.chart-wrap { position: relative; width: 100%; height: 40vh; }
</style>
)rawliteral";

// ---------------------------------------------------------------------------
//  Route registration
// ---------------------------------------------------------------------------
void setupWebServer(WebServer& server, String& mqttServer, String& mqttUser, String& mqttPassword,
                    uint8_t& alarmHour, uint8_t& alarmMinute, bool& alarmEnabled, uint8_t& currentBrightness) {

    // ---- Main page ----
    server.on("/", HTTP_GET, [&]() {
        uint8_t hours, minutes;
        getClockTime(hours, minutes);

        String alarmStatusText = alarmEnabled
            ? ("Alarm: ON (" + String(alarmHour) + ":" + (alarmMinute < 10 ? "0" : "") + String(alarmMinute) + ")")
            : String("Alarm: OFF");

        String html;
        html.reserve(3500);
        html  = "<h1 style='color: orange; font-size: 2em;'>" + String(hours) + ":" +
                (minutes < 10 ? "0" : "") + String(minutes) + "</h1>";
        html += "<p id='alarmStatus' class='alarm-status' style='color: orange;'>" + alarmStatusText + "</p>";

        html += "<div class='tabs'>";
        html += "<button class='tablinks' onclick=\"openTab(event, 'bitcoin')\">Bitcoin Price</button>";
        html += "<button class='tablinks' onclick=\"openTab(event, 'mqtt')\">MQTT Configuration</button>";
        html += "<button class='tablinks' onclick=\"openTab(event, 'alarm')\">Alarm Configuration</button>";
        html += "<button class='tablinks' onclick=\"openTab(event, 'brightness')\">Brightness</button>";
        html += "<button class='tablinks' onclick=\"openTab(event, 'settings')\">Settings</button>";
        html += "<button class='tablinks' onclick=\"openTab(event, 'update')\">Firmware Update</button>";
        html += "<button class='tablinks toggle-theme' style='float: right;' onclick=\"toggleTheme()\">Toggle Light/Dark</button>";
        html += "<button class='tablinks restart-button' style='float: right;' onclick=\"restartDevice()\">Restart</button>";
        html += "</div>";

        // Bitcoin tab
        html += "<div id='bitcoin' class='tabcontent'>";
        html += "<p style='color: orange; font-size: 2em;'>Bitcoin price : <span id='bitcoinPrice'>";
        html += lastUpdateSuccessful ? (String(lastBitcoinPrice) + " " + currencyCode)
                                     : String("Not available (waiting for update)");
        html += "</span></p>";
        html += "<div class='chart-wrap'><canvas id='bitcoinChart'></canvas></div>";
        html += "</div>";

        // MQTT tab
        html += "<div id='mqtt' class='tabcontent'>";
        html += "<h2>MQTT Configuration</h2>";
        html += "<form method='POST' action='/set_mqtt'>";
        html += "MQTT Server IP: <input type='text' name='mqtt_ip' value='" + mqttServer + "'><br>";
        html += "MQTT User: <input type='text' name='mqtt_user' value='" + mqttUser + "'><br>";
        html += "MQTT Password: <input type='password' name='mqtt_password' value='" + mqttPassword + "'><br>";
        html += "<input type='submit' value='Set MQTT'>";
        html += "</form></div>";

        // Alarm tab
        html += "<div id='alarm' class='tabcontent'>";
        html += "<h2>Alarm Configuration</h2>";
        html += "<form method='POST' action='/set_alarm'>";
        html += "Alarm Time: <input type='number' name='hour' min='0' max='23' value='" + String(alarmHour) + "'>:";
        html += "<input type='number' name='minute' min='0' max='59' value='" + String(alarmMinute) + "'><br>";
        html += "<input type='checkbox' name='enabled' " + String(alarmEnabled ? "checked" : "") + "> Enable Alarm<br>";
        html += "<input type='submit' value='Set Alarm'>";
        html += "</form></div>";

        // Brightness tab (horizontal slider, applied on release)
        html += "<div id='brightness' class='tabcontent'>";
        html += "<h2>Brightness Configuration</h2>";
        html += "<label>Brightness: <span id='brightnessValue'>" + String(currentBrightness) + "</span> / 15</label><br>";
        html += "<input type='range' id='brightnessSlider' min='0' max='15' value='" + String(currentBrightness) + "' ";
        html += "oninput=\"document.getElementById('brightnessValue').innerText=this.value\" ";
        html += "onchange=\"setBrightness(this.value)\">";
        html += "</div>";

        // Settings tab
        html += "<div id='settings' class='tabcontent'>";
        html += "<h2>General Settings</h2>";
        html += "<form method='POST' action='/set_settings'>";
        html += "Display mode: <select name='dispmode'>";
        html += "<option value='0'" + String(displayMode == 0 ? " selected" : "") + ">Alternate price &amp; time</option>";
        html += "<option value='1'" + String(displayMode == 1 ? " selected" : "") + ">Price only</option>";
        html += "<option value='2'" + String(displayMode == 2 ? " selected" : "") + ">Time only</option>";
        html += "</select><br>";
        html += "Currency: <select name='currency'>";
        html += "<option value='USD'" + String(currencyCode == "USD" ? " selected" : "") + ">USD</option>";
        html += "<option value='EUR'" + String(currencyCode == "EUR" ? " selected" : "") + ">EUR</option>";
        html += "</select><br>";
        html += "Price/Time toggle interval (s): <input type='number' name='toggle' min='2' max='600' value='" + String(displayToggleSec) + "'><br>";
        html += "Timezone: <select name='tz'>";
        for (uint8_t i = 0; i < (sizeof(TZ_OPTIONS) / sizeof(TZ_OPTIONS[0])); i++) {
            String rawVal = String(TZ_OPTIONS[i].posix);
            String escVal = rawVal;
            escVal.replace("&", "&amp;");
            escVal.replace("<", "&lt;");
            escVal.replace(">", "&gt;");
            html += "<option value='" + escVal + "'" + (tzString == rawVal ? " selected" : "") + ">" + TZ_OPTIONS[i].label + "</option>";
        }
        html += "</select><br>";
        html += "MQTT alarm topic: <input type='text' name='topic' value='" + mqttTopicStr + "'><br>";
        html += "<input type='submit' value='Set Settings'>";
        html += "</form>";
        html += "<p style='font-size:0.85em;'>Note: if you change the MQTT topic, update the matching trigger topic in your Home Assistant automation.</p>";
        html += "<h2>WiFi</h2>";
        html += "<p><button type='button' class='restart-button' onclick='resetWifi()'>Reset WiFi</button></p>";
        html += "<p class='fw-version'>Firmware version: " + String(FIRMWARE_VERSION) + "</p>";
        html += "</div>";

        // Firmware tab -> ElegantOTA page
        html += "<div id='update' class='tabcontent'>";
        html += "<h2>Firmware Update</h2>";
        html += "<p>Current firmware version: " + String(FIRMWARE_VERSION) + "</p>";
        html += "<p>Open the OTA updater to upload a new firmware (.bin):</p>";
        html += "<p><a href='/update'><button type='button'>Open firmware updater</button></a></p>";
        html += "</div>";

        // Stream: dynamic head first, then static scripts + styles from flash
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.send(200, "text/html", "");
        server.sendContent(html);
        server.sendContent_P(PAGE_SCRIPTS);
        server.sendContent_P(PAGE_STYLES);
        server.sendContent("");
    });

    // ---- Live data (polled by the page) ----
    server.on("/api/data", HTTP_GET, [&]() {
        uint8_t hours, minutes;
        getClockTime(hours, minutes);

        JsonDocument jsonResponse;
        jsonResponse["time"]          = String(hours) + ":" + (minutes < 10 ? "0" : "") + String(minutes);
        jsonResponse["price"]         = lastBitcoinPrice;
        jsonResponse["currency"]      = currencyCode;
        jsonResponse["alarm_enabled"] = alarmEnabled;
        jsonResponse["alarm_time"]    = String(alarmHour) + ":" + (alarmMinute < 10 ? "0" : "") + String(alarmMinute);

        String response;
        serializeJson(jsonResponse, response);
        server.send(200, "application/json", response);
    });

    // ---- Price history (for the persistent chart) ----
    server.on("/api/history", HTTP_GET, [&]() {
        server.send(200, "application/json", getHistoryJson());
    });

    // ---- MQTT config (applied live, no reboot) ----
    server.on("/set_mqtt", HTTP_POST, [&]() {
        if (server.hasArg("mqtt_ip") && server.hasArg("mqtt_user") && server.hasArg("mqtt_password")) {
            mqttServer   = server.arg("mqtt_ip");
            mqttUser     = server.arg("mqtt_user");
            mqttPassword = server.arg("mqtt_password");
            saveMqttConfig();
            applyMqttConfig();
            server.send(200, "text/html", "MQTT settings saved. <a href='/'>Go back</a>");
        } else {
            server.send(400, "text/html", "Invalid input. <a href='/'>Go back</a>");
        }
    });

    // ---- Alarm config ----
    server.on("/set_alarm", HTTP_POST, [&]() {
        if (server.hasArg("hour") && server.hasArg("minute")) {
            alarmHour    = server.arg("hour").toInt();
            alarmMinute  = server.arg("minute").toInt();
            alarmEnabled = server.hasArg("enabled");
            saveAlarmConfig();
            server.send(200, "text/html", "Alarm set successfully. <a href='/'>Go back</a>");
        } else {
            server.send(400, "text/html", "Invalid input. <a href='/'>Go back</a>");
        }
    });

    // ---- Brightness (slider posts here on release) ----
    server.on("/set_brightness", HTTP_POST, [&]() {
        if (server.hasArg("brightness")) {
            int brightness = server.arg("brightness").toInt();
            if (brightness >= 0 && brightness <= 15) {
                currentBrightness = brightness;
                lc.setIntensity(0, currentBrightness);
                saveBrightnessConfig();
                server.send(200, "text/html", "Brightness set successfully.");
            } else {
                server.send(400, "text/html", "Invalid brightness value.");
            }
        } else {
            server.send(400, "text/html", "Brightness value missing.");
        }
    });

    // ---- General settings ----
    server.on("/set_settings", HTTP_POST, [&]() {
        bool currencyChanged = false;
        if (server.hasArg("currency")) {
            String c = server.arg("currency");
            if (c == "USD" || c == "EUR") {
                if (c != currencyCode) currencyChanged = true;
                currencyCode = c;
            }
        }
        if (server.hasArg("toggle")) {
            int t = server.arg("toggle").toInt();
            if (t >= 2 && t <= 600) displayToggleSec = (uint16_t) t;
        }
        if (server.hasArg("dispmode")) {
            int m = server.arg("dispmode").toInt();
            if (m >= 0 && m <= 2) displayMode = (uint8_t) m;
        }
        if (server.hasArg("tz")) {
            tzString = server.arg("tz");
        }
        if (server.hasArg("topic")) {
            String tp = server.arg("topic");
            if (tp.length() > 0) mqttTopicStr = tp;
        }
        saveSettingsConfig();
        applyTimezone();
        if (currencyChanged) requestPriceRefresh();
        server.send(200, "text/html", "Settings saved. <a href='/'>Go back</a>");
    });

    // ---- WiFi reset ----
    server.on("/reset_wifi", HTTP_POST, [&]() {
        server.send(200, "text/plain", "WiFi credentials cleared. The device is restarting and will open the 'BTCTICKERAP' setup network.");
        delay(500);
        resetWifiSettings();
    });

    // ---- Restart ----
    server.on("/restart", HTTP_POST, [&]() {
        server.send(200, "text/plain", "Device is restarting...");
        delay(1000);
        ESP.restart();
    });

    // Note: /update is handled by ElegantOTA (registered in the main sketch).
}
