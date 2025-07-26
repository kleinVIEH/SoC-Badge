# SoC-Badge
The SoC-Badge is a small display based on an affordable D1 mini with an ESP8266 chip. It connects to an MQTT broker via Wi-Fi and receives the state of charge (SoC) of the home battery as a percentage.

The type of smart home central unit doesn’t really matter, as long as MQTT is available. I use it with ioBroker, but Node-RED or Home Assistant work just as well.

For the display, an ARGB LED strip with a density of 144 LEDs/m is used. A segment with a length of 10 LEDs is required, but the firmware supports flexible scaling, allowing for different display formats.

Optionally, the color of the display can be controlled by the smart home system to signal various status messages. It’s also possible to indicate whether the battery is currently charging. In that case, the next higher LED starts blinking – making it easy to see in which direction the charge level is moving.

The case is 3D-printed. It looks professional and modern. Inspired by the design of a light switch, it can be discreetly wall-mounted. Optionally, a matching table stand is available for freestanding placement in the room.

Complete dokumentation on: 

https://www.mykaefer.net/extended-soc-badge-hausakku-fuellstand-auf-einen-blick-mit-erweiterter-funktionalitaet/ (German language)
