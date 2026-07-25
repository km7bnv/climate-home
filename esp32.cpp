#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "DHT.h"

/* ===== OLED ===== */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

#define I2C_SDA 21
#define I2C_SCL 22

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

/* ===== DHT ===== */
#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

/* ===== BUTTONS ===== */
#define BTN_NEXT 18
#define BTN_PREV 19

/* ===== BUZZER ===== */
#define BUZZER_PIN 23
#define BUZZER_CHANNEL 0
#define BUZZER_FREQ 2000
#define BUZZER_RESOLUTION 8

int volumeLevel = 5;

/* ===== WIFI / MQTT ===== */
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* mqtt_server = "YOUR_MQTT_SERVER_IP";

WiFiClient espClient;
PubSubClient client(espClient);

/* ===== UI ===== */
int screenIndex = 0;
const int MAX_SCREENS = 4;
bool screenOn = true;
unsigned long lastActivity = 0;
const unsigned long SCREEN_TIMEOUT = 5000;

/* ===== TIMERS ===== */
unsigned long lastPublish = 0;
const unsigned long PUBLISH_INTERVAL = 20000;

/* ===== TIME ===== */
String timeString = "--:--";
String ampm = "";
int currentHour24 = -1;

/* ===== PM ===== */
int pmValue = -1;

/* ===== WARNING ===== */
bool pmWarningActive = false;
unsigned long warningStart = 0;
unsigned long buzzerEnd = 0;

/* ===== HELPERS ===== */

void drawHeader(const char* title) {
  display.fillRect(0, 0, 128, 16, SSD1306_BLACK);
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(2, 4);
  display.print(title);
}

void wakeScreen() {
  if (!screenOn) {
    display.ssd1306_command(SSD1306_DISPLAYON);
    screenOn = true;
  }
  lastActivity = millis();
}

bool quietHours() {
  if (currentHour24 < 0) return false;
  return (currentHour24 >= 20 || currentHour24 < 9);
}

void beep(bool loud) {
  if (volumeLevel == 0 || quietHours()) return;

  ledcWrite(BUZZER_CHANNEL, loud ? 255 : 80);
  delay(60);
  ledcWrite(BUZZER_CHANNEL, 0);
}

void warn_beep(bool loud) {
  if (volumeLevel == 0 || quietHours()) return;

  ledcWrite(BUZZER_CHANNEL, loud ? 255 : 80);
  delay(500);
  ledcWrite(BUZZER_CHANNEL, 0);
  delay(500);
}

void clearWarning() {
  pmWarningActive = false;
  buzzerEnd = 0;
  screenIndex = 0;
  wakeScreen();
}

/* ===== MQTT CALLBACK ===== */

void mqttCallback(char* topic, byte* payload, unsigned int length) {

  String msg = "";

  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }

  if (strcmp(topic, "time") == 0) {

    int colon = msg.indexOf(':');

    if (colon == 2) {

      int h24 = msg.substring(0, 2).toInt();
      int m = msg.substring(3, 5).toInt();

      if (h24 >= 0 && h24 < 24 && m >= 0 && m < 60) {

        currentHour24 = h24;

        bool isPM = h24 >= 12;
        int h12 = h24 % 12;
        if (h12 == 0) h12 = 12;

        ampm = isPM ? "PM" : "AM";

        char buf[6];
        snprintf(buf, sizeof(buf), "%d:%02d", h12, m);

        timeString = String(buf);
      }
    }
  }

  if (strcmp(topic, "pm") == 0) {

    pmValue = msg.toInt();

    if (pmValue >= 50 && !pmWarningActive) {

      pmWarningActive = true;
      warningStart = millis();
      buzzerEnd = millis() + 10000;

      wakeScreen();
    }
  }

  if (strcmp(topic, "volume") == 0) {
    volumeLevel = constrain(msg.toInt(), 0, 10);
  }
}

/* ===== WIFI / MQTT ===== */

void setup_wifi() {

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Connected! IP: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {

  while (!client.connected()) {

    Serial.print("Connecting to MQTT... ");

    if (client.connect("ESP32_Climate")) {

      Serial.println("connected");

      client.subscribe("time");
      client.subscribe("pm");
      client.subscribe("volume");

    } else {

      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 5 seconds");

      delay(5000);
    }
  }
}

/* ===== SETUP ===== */

void setup() {

  Serial.begin(115200);

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {

    Serial.println("OLED allocation failed");

    while (true);
  }

  display.clearDisplay();
  display.display();

  dht.begin();

  pinMode(BTN_NEXT, INPUT_PULLUP);
  pinMode(BTN_PREV, INPUT_PULLUP);

  ledcSetup(BUZZER_CHANNEL, BUZZER_FREQ, BUZZER_RESOLUTION);
  ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);

  setup_wifi();

  client.setServer(mqtt_server, 1883);
  client.setCallback(mqttCallback);

  wakeScreen();
}

/* ===== LOOP ===== */

void loop() {

  if (WiFi.status() != WL_CONNECTED) {
    setup_wifi();
  }

  if (!client.connected()) {
    reconnect();
  }

  client.loop();

  /* ===== WARNING MODE ===== */

  if (pmWarningActive) {

    wakeScreen();

    display.clearDisplay();

    drawHeader("WARNING");

    display.setTextSize(1);

    display.setCursor(10, 26);
    display.print("High PM 2.5 Level!");

    display.setCursor(30, 42);
    display.print("AQI: ");
    display.print(pmValue);

    if (millis() < buzzerEnd && !quietHours()) {
      warn_beep(true);
    }

    if (millis() - warningStart > 7000) {
      clearWarning();
    }

    display.display();

    return;
  }

  /* ===== BUTTONS ===== */

  if (digitalRead(BTN_NEXT) == LOW) {

    screenIndex = (screenIndex + 1) % (MAX_SCREENS + 1);

    wakeScreen();

    beep(false);

    delay(200);
  }

  if (digitalRead(BTN_PREV) == LOW) {

    screenIndex = (screenIndex - 1 + MAX_SCREENS + 1) % (MAX_SCREENS + 1);

    wakeScreen();

    beep(false);

    delay(200);
  }

  display.clearDisplay();

  switch (screenIndex) {

    case 0:

      drawHeader("Climate-Home Sensor");

      display.setTextSize(3);

      display.setCursor(0, 22);
      display.print(timeString);

      display.setTextSize(1);

      display.setCursor(100, 30);
      display.print(ampm);

      break;

    case 1:

      drawHeader("Temperature");

      display.setTextSize(3);

      display.setCursor(0, 24);

      display.print((int)dht.readTemperature());

      display.print(" C");

      break;
      case 2:

      drawHeader("Humidity");

      display.setTextSize(3);

      display.setCursor(0, 24);

      display.print((int)dht.readHumidity());

      display.print(" %");

      break;

    case 3:

      drawHeader("Connectivity");

      display.setTextSize(1);

      display.setCursor(0, 20);

      display.print("WiFi: ");
      display.println(WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "OFF");

      display.print("MQTT: ");
      display.println(client.connected() ? mqtt_server : "OFF");

      break;

    case 4:

      drawHeader("AQI");

      display.setTextSize(2);

      display.setCursor(0, 26);

      if (pmValue < 0)
        display.print("N/A");
      else
        display.print(pmValue);

      break;
  }

  display.display();

  if (!pmWarningActive &&
      millis() - lastActivity > SCREEN_TIMEOUT &&
      screenOn) {

    display.ssd1306_command(SSD1306_DISPLAYOFF);
    screenOn = false;
  }

  if (millis() - lastPublish > PUBLISH_INTERVAL &&
      client.connected()) {

    lastPublish = millis();

    char buf[8];

    itoa((int)dht.readTemperature(), buf, 10);
    client.publish("home/sensor/temperature", buf);

    itoa((int)dht.readHumidity(), buf, 10);
    client.publish("home/sensor/humidity", buf);
  }
}
