# BtcPriceTickerTime

This program runs on an ESP8266 (Wemos D1 mini) and displays the Bitcoin price as well as the NTP time on a MAX7219 8 digits 7 segments display. 
Time and price are checked at regular interval (60s). The display alternates between time and price every 8s.

I have designed a Bitcoin character that can be 3D printed, where the Wemos and the display can fit.
You can download it from: https://www.thingiverse.com/thing:4747477/files

You need to adjust the timeOffset value according to your timezone. Default value is 3600 seconds, GMT+1.
Example of values: GMT +1 = 3600, GMT +8 = 28800, GMT -1 = -3600, GMT 0 = 0.

It uses Wifimanager so you dont need to hardcode your wifi credentials. Flash your ESP and it will make a wifi network that you can join from your phone (default password is: "pwd123456")to select your wifi and set your password.

Firmware can be updated OTA. Go to http://WEMOS_IP_ADDRESS/update
  
I have decided to use the Coingecko API because it does not require an API key and there is no real limitation regarding the number of request that can be made per 24 hours. Price is checked every minutes, so 1440 requests every 24 hours.
https://www.coingecko.com/en/api


# Libraries needed

- Wifimanager(0.16.0). Can be installed directly from Plaform.io
- Arduinojson(6.17.2). Can be installed directly from Plaform.io
- Ledcontroller(2.0.0-rc1). https://github.com/noah1510/LedController
- NTPClient(3.1.0). Can be installed directly from Plaform.io
- ElegantOTA(2.2.4). Can be installed directly from Plaform.io

# Hardware
- Wemos d1 mini
- MAX7219 8 digits segment display
- Micro USB on PCB    https://www.aliexpress.com/item/4001247388735.html
- Wire


# Future improvements

- Add simple webpage to configure basic settings (display brightness, alarm, etc...)
- Add price and time alarm functions (could add a buzzer)
- Motorize the right arm of character so it can point up when Bitcoin price is up the last 24h, and the opposite when price is down.


