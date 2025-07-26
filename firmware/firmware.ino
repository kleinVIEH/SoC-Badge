#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <EEPROM.h>
#include <Adafruit_NeoPixel.h>

// ======== Konfiguration ========
#define RESET_COUNTER_ADDR 300
#define AUTO_RESET_FLAG_ADDR 301
#define RESET_THRESHOLD 5
#define RESET_CLEAR_DELAY 10000

#define LED_PIN D2
#define LED_COUNT 10
#define BRIGHTNESS_PERCENT 5

WiFiClient espClient;
PubSubClient client(espClient);
Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

// ======== Globale Variablen ========
unsigned long resetStartTime;

char mqtt_server[40] = "192.168.1.10";
char mqtt_user[20] = "user";
char mqtt_pass[20] = "";
char mqtt_topic[50] = "akku/soc";
char mqtt_topic_color[50] = "akku/farbe";
char mqtt_topic_charging[50] = "akku/charging";
int mqtt_port = 1883;

bool shouldSaveConfig = false;
int soc = 0;
bool socReceived = false;
bool mqttConnected = false;
unsigned long lastSocUpdate = 0;
static unsigned long wifiDisconnectTime = 0;
const unsigned long SOC_TIMEOUT = 15000; // 15 Sekunden
bool blinkState = false;
int colorOverride = 0;
bool isCharging = false;
unsigned long knightRiderStart = 0;
unsigned long wifiDisconnectStart = 0;
bool knightRiderActive = false;
unsigned long knightRiderStartTime = 0;

// Farben
uint32_t COLOR_RED, COLOR_YELLOW, COLOR_GREEN, COLOR_OFF;

// ======== Hilfsfunktion für automatischen Reset ========
void doAutoRestart() {
  Serial.println("Automatischer Neustart...");
  EEPROM.write(AUTO_RESET_FLAG_ADDR, 1); // Flag setzen
  EEPROM.commit();
  delay(100);
  ESP.restart();
}

// ======== Angepasste Reset-Counter-Prüfung ========
void checkMultipleReset() {
  byte autoResetFlag = EEPROM.read(AUTO_RESET_FLAG_ADDR);

  if (autoResetFlag == 1) {
    Serial.println("Automatischer Reset erkannt, Zähler wird nicht erhöht.");
    EEPROM.write(AUTO_RESET_FLAG_ADDR, 0); // Flag zurücksetzen
    EEPROM.commit();
    resetStartTime = millis();
    return; // Kein Hochzählen
  }

  // Normaler Boot - Zähler erhöhen
  byte counter = EEPROM.read(RESET_COUNTER_ADDR);
  counter++;
  EEPROM.write(RESET_COUNTER_ADDR, counter);
  EEPROM.commit();

  resetStartTime = millis();

  Serial.print("Reset-Counter erhöht auf: ");
  Serial.println(counter);

  if (counter >= RESET_THRESHOLD) {
    Serial.println("Reset-Schwelle erreicht, WiFi-Einstellungen werden zurückgesetzt.");
    WiFiManager wm;
    wm.resetSettings();
    EEPROM.write(RESET_COUNTER_ADDR, 0);
    EEPROM.commit();
    delay(500);
    ESP.restart();
  }
}

void resetCounterResetTask() {
  if (millis() - resetStartTime > RESET_CLEAR_DELAY) {
    EEPROM.write(RESET_COUNTER_ADDR, 0);
    EEPROM.commit();
    resetStartTime = millis() + 99999999;
  }
}

// ======== Restlicher Code ========

void callback(char* topic, byte* payload, unsigned int length) {
  String t = String(topic);
  String value;
  for (unsigned int i = 0; i < length; i++) value += (char)payload[i];

  if (t == mqtt_topic) {
    soc = value.toInt();
    socReceived = true;
    lastSocUpdate = millis();
    updateLEDs();
  } else if (t == mqtt_topic_color) {
    colorOverride = value.toInt();
  } else if (t == mqtt_topic_charging) {
    isCharging = (value.toInt() == 1);
  }

  Serial.print("MQTT empfangen auf Topic: ");
  Serial.println(topic);
  Serial.print("Payload als String: ");
  Serial.println(value);
  Serial.print("SOC als Integer: ");
  Serial.println(soc);
}

void updateLEDs() {
  strip.clear();
  int ledsOn = map(soc, 0, 100, 0, LED_COUNT);
  uint32_t color = COLOR_GREEN;

  if (colorOverride == 1) color = COLOR_RED;
  else if (colorOverride == 2) color = COLOR_YELLOW;
  else if (colorOverride == 3) color = COLOR_GREEN;
  else {
    if (soc < 20) color = COLOR_RED;
    else if (soc < 40) color = COLOR_YELLOW;
    else color = COLOR_GREEN;
  }

  for (int i = 0; i < ledsOn; i++) {
    strip.setPixelColor(i, color);
  }

  if (isCharging) {
    bool phase = (millis() / 500) % 2;
    int blinkLed = (ledsOn < LED_COUNT) ? ledsOn : (LED_COUNT - 1);

    if (!phase) {
      strip.setPixelColor(blinkLed, COLOR_OFF);
    } else {
      strip.setPixelColor(blinkLed, color);
    }
  }

  strip.show();
}

void blinkErrorPattern() {
  bool phase = (millis() / 500) % 2;
  uint32_t c1 = phase ? COLOR_RED : COLOR_OFF;
  uint32_t c2 = phase ? COLOR_YELLOW : COLOR_OFF;

  for (int i = 0; i < LED_COUNT; i++) {
    strip.setPixelColor(i, i % 2 == 0 ? c1 : c2);
  }
  strip.show();
}

void knightRiderEffect() {
  static int pos = 0;
  static int dir = 1;
  static unsigned long lastUpdate = 0;
  const int delayMs = 50;

  if (millis() - lastUpdate < delayMs) return;
  lastUpdate = millis();

  strip.clear();

  float percent = (float)pos / (float)(LED_COUNT - 1);
  uint32_t color;

  if (percent < 0.2) {
    color = strip.Color(255, 0, 0);
  } else if (percent < 0.4) {
    color = strip.Color(255, 255, 0);
  } else {
    color = strip.Color(0, 255, 0);
  }

  strip.setPixelColor(pos, color);
  strip.show();

  pos += dir;
  if (pos <= 0) dir = 1;
  if (pos >= LED_COUNT - 1) dir = -1;

  client.loop();
}

void reconnect() {
  Serial.println("MQTT: Verbindung prüfen...");

  WiFiClient testClient;
  if (!testClient.connect(mqtt_server, mqtt_port)) {
    Serial.println("MQTT-Server nicht erreichbar → Automatischer Neustart");
    doAutoRestart();
  }
  testClient.stop();

  Serial.print("MQTT verbinden... ");
  String clientId = "ESP-" + String(ESP.getChipId());

  if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
    Serial.println("erfolgreich");
    client.subscribe(mqtt_topic);
    client.subscribe(mqtt_topic_color);
    client.subscribe(mqtt_topic_charging);
    mqttConnected = true;
  } else {
    Serial.print("fehlgeschlagen, rc=");
    Serial.print(client.state());
    Serial.println(" → Automatischer Neustart");
    doAutoRestart();
  }
}

void saveConfigCallback() {
  shouldSaveConfig = true;
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  checkMultipleReset();

  strip.begin();
  strip.setBrightness((255 * BRIGHTNESS_PERCENT) / 100);
  strip.show();
  COLOR_RED = strip.Color(255, 0, 0);
  COLOR_YELLOW = strip.Color(255, 100, 0);
  COLOR_GREEN = strip.Color(0, 255, 0);
  COLOR_OFF = strip.Color(0, 0, 0);

  EEPROM.get(0, mqtt_server);
  EEPROM.get(40, mqtt_user);
  EEPROM.get(60, mqtt_pass);
  EEPROM.get(80, mqtt_topic);
  EEPROM.get(130, mqtt_port);
  EEPROM.get(140, mqtt_topic_color);
  EEPROM.get(190, mqtt_topic_charging);

  WiFiManager wm;
  wm.setSaveConfigCallback(saveConfigCallback);

  char mqtt_port_str[6];
  snprintf(mqtt_port_str, sizeof(mqtt_port_str), "%d", mqtt_port);

  WiFiManagerParameter p_server("server", "MQTT Server", mqtt_server, 40);
  WiFiManagerParameter p_user("user", "MQTT User", mqtt_user, 20);
  WiFiManagerParameter p_pass("pass", "MQTT Passwort", mqtt_pass, 20);
  WiFiManagerParameter p_topic("topic", "MQTT Topic SoC", mqtt_topic, 50);
  WiFiManagerParameter p_port("port", "MQTT Port", mqtt_port_str, 6);
  WiFiManagerParameter p_topic_color("topiccolor", "MQTT Topic Farbe", mqtt_topic_color, 50);
  WiFiManagerParameter p_topic_charge("topiccharge", "MQTT Topic Laden", mqtt_topic_charging, 50);

  wm.addParameter(&p_server);
  wm.addParameter(&p_user);
  wm.addParameter(&p_pass);
  wm.addParameter(&p_topic);
  wm.addParameter(&p_port);
  wm.addParameter(&p_topic_color);
  wm.addParameter(&p_topic_charge);
  wm.setConfigPortalTimeout(600);

  if (!wm.autoConnect("SoC-Light")) {
    Serial.println("Verbindung fehlgeschlagen. Neustart...");
    doAutoRestart();
  }

  strncpy(mqtt_server, p_server.getValue(), 40);
  strncpy(mqtt_user, p_user.getValue(), 20);
  strncpy(mqtt_pass, p_pass.getValue(), 20);
  strncpy(mqtt_topic, p_topic.getValue(), 50);
  strncpy(mqtt_topic_color, p_topic_color.getValue(), 50);
  strncpy(mqtt_topic_charging, p_topic_charge.getValue(), 50);
  mqtt_port = atoi(p_port.getValue());

  if (shouldSaveConfig) {
    EEPROM.put(0, mqtt_server);
    EEPROM.put(40, mqtt_user);
    EEPROM.put(60, mqtt_pass);
    EEPROM.put(80, mqtt_topic);
    EEPROM.put(130, mqtt_port);
    EEPROM.put(140, mqtt_topic_color);
    EEPROM.put(190, mqtt_topic_charging);
    EEPROM.commit();
  }

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  reconnect();
}

void loop() {
  // MQTT-Verbindung prüfen
  if (!client.connected()) {
    reconnect();
  } else {
    client.loop();
  }

  // WLAN-Verbindung prüfen
  if (WiFi.status() != WL_CONNECTED || WiFi.localIP().toString() == "0.0.0.0") {
    Serial.println("WLAN-Verbindung verloren oder keine gültige IP → Automatischer Neustart");
    doAutoRestart();
  }

  // Prüfen, ob SoC veraltet ist (älter als 15 Sekunden)
  bool socTimedOut = (millis() - lastSocUpdate > SOC_TIMEOUT);

  if (!socReceived || socTimedOut) {
    knightRiderEffect();

    if (knightRiderStartTime == 0) {
      knightRiderStartTime = millis();
    }

    if (millis() - knightRiderStartTime > 60000) {
      Serial.println("Timeout im KnightRider-Modus → Automatischer Neustart");
      doAutoRestart();
    }

    return;
  }

  // Wenn gültiger SoC → KnightRider-Zeit zurücksetzen und normale Anzeige zeigen
  knightRiderStartTime = 0;
  updateLEDs();
  resetCounterResetTask();
}