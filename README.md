# BtcPriceTickerTime

This program runs on an ESP8266 (Wemos D1 mini) and displays the Bitcoin price as well as the NTP time on a MAX7219 8 digits 7 segments display. 
Time and price are checked at regular interval (60s). The display alternates between time and price every 8s.

I have designed a Bitcoin character that can be 3D printed, where the Wemos and the display can fit.
You can download it from: https://www.thingiverse.com/thing:4747477/files

You need to adjust the timeOffset value according to your timezone. Default value is 3600 seconds, GMT+1.
Example of values: GMT +1 = 3600, GMT +8 = 28800, GMT -1 = -3600, GMT 0 = 0.

It uses Wifimanager so you dont need to hardcode your wifi credentials. Flash your ESP and it will make a wifi network that you can join from your phone (default password is: "pwd123456")to select your wifi and set your password.

Firmware can be updated OTA.
  
It to use the Coingecko API because (no API key needed). Price is checked every minutes, so 1440 requests every 24 hours.
https://www.coingecko.com/en/api

I got inspired by the following code, so thanks to Nimrod-Galor. https://github.com/Nimrod-Galor/Simple-Bitcoin-Ticker



**UPDATE 2024:**


- Added a web server
- Host a web page that shows the time and BTC price. Values are automatically refreshed.
- Now possible to configure your MQTT broker details directly from the webpage
- Added Alarm feature. You can configure the alarm from the webpage. When the alarm is triggered, the display will show '88888888'.
  If you use Home Assistant, you can modify the MQTT topic in the code:
  see: const char* mqttTopic = "sonos/play_spotify_alarm_btcclock";
  Music source and volume can be adjusted in the 'SendMQTTMessage' function
- Added Brightness feature: can be adjusted from the webpage
- Added OTA tab
- Added restart button
- Added light and dark theme
  

# Libraries needed

- WiFiManager.h (2.0.17) /  to managed wifi networks
- ArduinoJson.h (7.2.0) /  to manipulate json
- LedControl.h (1.0.6) /  to control the leg segments
- NTPClient.h (3.2.1) /  to check time online
- ESP8266WebServer.h /  to host a webpage
- PubSubClient.h (2.8) / MQTT client
- ESP8266WiFi.h / wifi network
- ESP8266HTTPClient.h / HTTP client
- WiFiClientSecureBearSSL.h / use to support SSL
- WiFiUdp.h / for UDP
- FS.h / use to interact with SPIFSS flash memory
- ESP8266HTTPUpdateServer.h / OTA update

# Hardware
- Wemos d1 mini
- MAX7219 8 digits segment display
- Micro USB on PCB    https://www.aliexpress.com/item/4001247388735.html
- Wire




