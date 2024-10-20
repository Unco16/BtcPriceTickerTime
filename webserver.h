/* webserver.h */
#include <ESP8266WebServer.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <NTPClient.h>
#include <LedControl.h>
#include <ESP8266HTTPUpdateServer.h>

extern NTPClient timeClient;
extern bool lastUpdateSuccessful;
extern uint32_t lastBitcoinPrice;
extern LedControl lc;

void loadMqttConfig();
void saveMqttConfig();
bool updateBitcoinPrice();

void setupWebServer(ESP8266WebServer& server, String& mqttServer, String& mqttUser, String& mqttPassword, uint8_t& alarmHour, uint8_t& alarmMinute, bool& alarmEnabled, uint8_t& currentBrightness) {
    server.on("/", HTTP_GET, [&]() {
        timeClient.update();
        uint8_t hours = timeClient.getHours();
        uint8_t minutes = timeClient.getMinutes();

        String html = "<h1 style='color: orange; font-size: 2em;'>" + String(hours) + ":" + (minutes < 10 ? "0" : "") + String(minutes) + "</h1>";
        html += "<div class='tabs'>";
        html += "  <button class='tablinks' onclick=\"openTab(event, 'bitcoin')\">Bitcoin Price</button>";
        html += "  <button class='tablinks' onclick=\"openTab(event, 'mqtt')\">MQTT Configuration</button>";
        html += "  <button class='tablinks' onclick=\"openTab(event, 'alarm')\">Alarm Configuration</button>";
        html += "  <button class='tablinks' onclick=\"openTab(event, 'brightness')\">Brightness</button>";
        html += "  <button class='tablinks' onclick=\"openTab(event, 'update')\">Firmware Update</button>";
        html += "  <button class='tablinks toggle-theme' style='float: right;' onclick=\"toggleTheme()\">Toggle Light/Dark</button>";
        html += "  <button class='tablinks restart-button' style='float: right;' onclick=\"restartDevice()\">Restart</button>";
        html += "</div>";

        // Bitcoin Price Tab
        html += "<div id='bitcoin' class='tabcontent'>";
        html += "<p style='color: orange; font-size: 2em;'>Bitcoin price : <span id='bitcoinPrice'>";
        if (lastUpdateSuccessful) {
            html += String(lastBitcoinPrice) + " USD";
        } else {
            html += "Not available (waiting for update)";
        }
        html += "</span></p>";
        html += "<canvas id='bitcoinChart' width='400' height='200'></canvas>";
        html += "</div>";

        // MQTT Configuration Tab
        html += "<div id='mqtt' class='tabcontent'>";
        html += "<h2>MQTT Configuration</h2>";
        html += "<form method='POST' action='/set_mqtt'>";
        html += "MQTT Server IP: <input type='text' name='mqtt_ip' value='" + mqttServer + "'><br>";
        html += "MQTT User: <input type='text' name='mqtt_user' value='" + mqttUser + "'><br>";
        html += "MQTT Password: <input type='password' name='mqtt_password' value='" + mqttPassword + "'><br>";
        html += "<input type='submit' value='Set MQTT'>";
        html += "</form>";
        html += "</div>";

        // Alarm Configuration Tab
        html += "<div id='alarm' class='tabcontent'>";
        html += "<h2>Alarm Configuration</h2>";
        html += "<form method='POST' action='/set_alarm'>";
        html += "Alarm Time: <input type='number' name='hour' min='0' max='23' value='" + String(alarmHour) + "'>:";
        html += "<input type='number' name='minute' min='0' max='59' value='" + String(alarmMinute) + "'><br>";
        html += "<input type='checkbox' name='enabled' " + String(alarmEnabled ? "checked" : "") + "> Enable Alarm<br>";
        html += "<input type='submit' value='Set Alarm'>";
        html += "</form>";
        html += "</div>";

        // Brightness Configuration Tab
        html += "<div id='brightness' class='tabcontent'>";
        html += "<h2>Brightness Configuration</h2>";
        html += "<form method='POST' action='/set_brightness'>";
        html += "Brightness (0-15): <input type='number' name='brightness' min='0' max='15' value='" + String(currentBrightness) + "'><br>";
        html += "<input type='submit' value='Set Brightness'>";
        html += "</form>";
        html += "</div>";

        // Firmware Update Tab
        html += "<div id='update' class='tabcontent'>";
        html += "<h2>Firmware Update</h2>";
        html += "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>";
        html += "</div>";

        // JavaScript for Tabs
        html += "<script>";
        html += "function openTab(evt, tabName) {";
        html += "  var i, tabcontent, tablinks;";
        html += "  tabcontent = document.getElementsByClassName('tabcontent');";
        html += "  for (i = 0; i < tabcontent.length; i++) {";
        html += "    tabcontent[i].style.display = 'none';";
        html += "  }";
        html += "  tablinks = document.getElementsByClassName('tablinks');";
        html += "  for (i = 0; i < tablinks.length; i++) {";
        html += "    tablinks[i].className = tablinks[i].className.replace(' active', '');";
        html += "  }";
        html += "  document.getElementById(tabName).style.display = 'block';";
        html += "  evt.currentTarget.className += ' active';";
        html += "}";
        html += "document.getElementsByClassName('tablinks')[0].click();";
        html += "</script>";

        // JavaScript for theme toggling
        html += "<script>";
        html += "let isDarkTheme = localStorage.getItem('theme') === 'dark';";
        html += "applyTheme();";
        html += "function toggleTheme() {";
        html += "  isDarkTheme = !isDarkTheme;";
        html += "  localStorage.setItem('theme', isDarkTheme ? 'dark' : 'light');";
        html += "  applyTheme();";
        html += "}";
        html += "function applyTheme() {";
        html += "  document.body.style.backgroundColor = isDarkTheme ? '#121212' : '#d3d3d3';";
        html += "  document.body.style.color = isDarkTheme ? '#ffffff' : '#000000';";
        html += "  document.querySelector('.tabs').style.backgroundColor = isDarkTheme ? '#121212' : '#f0f0f0';";
        html += "}";
        html += "</script>";

        // JavaScript for restarting the device
        html += "<script>";
        html += "function restartDevice() {";
        html += "  if (confirm('Are you sure you want to restart the device?')) {";
        html += "    fetch('/restart', { method: 'POST' })";
        html += "      .then(response => response.text())";
        html += "      .then(data => alert(data));";
        html += "  }";
        html += "}";
        html += "</script>";

        // JavaScript for real-time update and Chart.js
        html += "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>";
        html += "<script>";
        html += "var priceData = [];";
        html += "var labels = [];";
        html += "var ctx = null;";
        html += "var bitcoinChart = null;";
        html += "window.onload = function() {";
        html += "  ctx = document.getElementById('bitcoinChart').getContext('2d');";
        html += "  bitcoinChart = new Chart(ctx, {";
        html += "    type: 'line',";
        html += "    data: {";
        html += "      labels: labels,";
        html += "      datasets: [{";
        html += "        label: 'Bitcoin Price (USD)',";
        html += "        data: priceData,";
        html += "        borderColor: 'rgba(75, 192, 192, 1)',";
        html += "        borderWidth: 2,";
        html += "        fill: false";
        html += "      }]";
        html += "    },";
        html += "    options: {";
        html += "      scales: {";
        html += "        x: {";
        html += "          type: 'time',";
        html += "          time: {";
        html += "            unit: 'minute'";
        html += "          }";
        html += "        },";
        html += "        y: {";
        html += "          beginAtZero: false";
        html += "        }";
        html += "      }";
        html += "    }";
        html += "  });";
        html += "};";

        html += "setInterval(function() {";
        html += "  fetch('/api/data').then(response => response.json()).then(data => {";
        html += "    document.getElementById('bitcoinPrice').innerText = data.price + ' USD';";
        html += "    document.querySelector('h1').innerText = data.time;";
        html += "    var currentTime = new Date().toLocaleTimeString();";
        html += "    labels.push(currentTime);";
        html += "    priceData.push(data.price);";
        html += "    if (labels.length > 20) {";
        html += "      labels.shift();";
        html += "      priceData.shift();";
        html += "    }";
        html += "    bitcoinChart.update();";
        html += "  });";
        html += "}, 10000);";
        html += "</script>";

        // CSS Styling
        html += "<style>";
        html += "button, input[type='submit'] {";
        html += "  background-color: #4CAF50;";
        html += "  color: white;";
        html += "  padding: 10px 20px;";
        html += "  margin: 5px 0;";
        html += "  border: none;";
        html += "  border-radius: 5px;";
        html += "  cursor: pointer;";
        html += "  transition: background-color 0.3s;";
        html += "}";
        html += "button:hover, input[type='submit']:hover {";
        html += "  background-color: #45a049;";
        html += "}";
        html += "input[type='text'], input[type='number'], input[type='password'] {";
        html += "  padding: 8px;";
        html += "  margin: 5px 0;";
        html += "  border: 1px solid #ccc;";
        html += "  border-radius: 4px;";
        html += "}";
        html += ".tabs { overflow: hidden; background-color: #f0f0f0; padding: 10px; position: relative; }";
        html += ".toggle-theme {";
        html += "  float: right;";
        html += "  background-color: #4CAF50;";
        html += "  margin-left: auto;";
        html += "}";
        html += ".restart-button {";
        html += "  background-color: #f44336;";
        html += "  color: white;";
        html += "  margin-left: 10px;";
        html += "}";
        html += ".restart-button:hover {";
        html += "  background-color: #d32f2f;";
        html += "}";
        html += ".tablinks {";
        html += "  float: left;";
        html += "  border: none;";
        html += "  outline: none;";
        html += "  cursor: pointer;";
        html += "  padding: 14px 16px;";
        html += "  transition: 0.3s;";
        html += "}";
        html += ".tablinks.active { background-color: #FFA500; }";
        html += ".tabcontent {";
        html += "  display: none;";
        html += "  padding: 6px 12px;";
        html += "  border-top: none;";
        html += "}";
        html += "</style>";

        server.send(200, "text/html", html);
    });

    server.on("/api/data", HTTP_GET, [&]() {
        timeClient.update();
        uint8_t hours = timeClient.getHours();
        uint8_t minutes = timeClient.getMinutes();
        StaticJsonDocument<256> jsonResponse;
        jsonResponse["time"] = String(hours) + ":" + (minutes < 10 ? "0" : "") + String(minutes);
        jsonResponse["price"] = lastBitcoinPrice;
        String response;
        serializeJson(jsonResponse, response);
        server.send(200, "application/json", response);
    });

    server.on("/set_mqtt", HTTP_POST, [&]() {
        if (server.hasArg("mqtt_ip") && server.hasArg("mqtt_user") && server.hasArg("mqtt_password")) {
            mqttServer = server.arg("mqtt_ip");
            mqttUser = server.arg("mqtt_user");
            mqttPassword = server.arg("mqtt_password");
            saveMqttConfig();
            server.send(200, "text/html", "MQTT settings saved. <a href='/'>Go back</a>");
        } else {
            server.send(400, "text/html", "Invalid input. <a href='/'>Go back</a>");
        }
    });

    server.on("/set_alarm", HTTP_POST, [&]() {
        if (server.hasArg("hour") && server.hasArg("minute")) {
            alarmHour = server.arg("hour").toInt();
            alarmMinute = server.arg("minute").toInt();
            alarmEnabled = server.hasArg("enabled");
            server.send(200, "text/html", "Alarm set successfully. <a href='/'>Go back</a>");
        } else {
            server.send(400, "text/html", "Invalid input. <a href='/'>Go back</a>");
        }
    });

    server.on("/set_brightness", HTTP_POST, [&]() {
        if (server.hasArg("brightness")) {
            int brightness = server.arg("brightness").toInt();
            if (brightness >= 0 && brightness <= 15) {
                currentBrightness = brightness;
                lc.setIntensity(0, currentBrightness);
                server.send(200, "text/html", "Brightness set successfully. <a href='/'>Go back</a>");
            } else {
                server.send(400, "text/html", "Invalid brightness value. <a href='/'>Go back</a>");
            }
        } else {
            server.send(400, "text/html", "Brightness value missing. <a href='/'>Go back</a>");
        }
    });

    server.on("/update", HTTP_POST, [&]() {
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", (Update.hasError()) ? "Update Failed" : "Update Successful. Rebooting...");
        delay(1000);
        ESP.restart();
    }, [&]() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            Serial.printf("Update: %s\n", upload.filename.c_str());
            if (!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Update.printError(Serial);
            }
        } else if (upload.status == UPLOAD_FILE_END) {
            if (Update.end(true)) {
                Serial.printf("Update Success: %u bytes\n", upload.totalSize);
            } else {
                Update.printError(Serial);
            }
        }
    });

    server.on("/restart", HTTP_POST, [&]() {
        server.send(200, "text/plain", "Device is restarting...");
        delay(1000);
        ESP.restart();
    });

    server.begin();
    Serial.println("HTTP server started");
}
