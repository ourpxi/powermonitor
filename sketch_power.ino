#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiUdp.h>
#include <Adafruit_NeoPixel.h>
#include <WiFiClientSecure.h>

#define SENSE_PIN       GPIO_NUM_35
#define PIN_RGB         2
#define PIN_BUZZER      27
#define NUM_LEDS        1

#define SSID            "YOUR_WIFI_SSID"
#define PASS            "YOUR_WIFI_PASSWORD"
#define POST_URL        "https://example.com/api/endpoint"
#define PING_IP         "192.168.1.100"

#define WOL_BROADCAST   "192.168.1.255"
#define WOL_PORT        9
uint8_t WOL_MAC[] = {0x00,0x00,0x00,0x00,0x00,0x00};

#define STABILIZE_MS    15000
#define PING_WAIT_MS    30000
#define WOL_RETRIES     3
#define FLUCTUATION_MS  15000

RTC_DATA_ATTR bool powerWasOut = false;

Adafruit_NeoPixel rgb(NUM_LEDS, PIN_RGB, NEO_GRB + NEO_KHZ800);

#define COLOR_OFF    rgb.Color(0,0,0)
#define COLOR_RED_HI rgb.Color(80,0,0)
#define COLOR_RED_LO rgb.Color(15,0,0)
#define COLOR_BLU_HI rgb.Color(0,0,80)
#define COLOR_BLU_LO rgb.Color(0,0,15)
#define COLOR_GREEN  rgb.Color(0,80,0)

bool powerPresent() {
  return digitalRead(SENSE_PIN) == HIGH;
}

void setupHardware() {
  pinMode(SENSE_PIN, INPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  rgb.begin();
  rgb.setBrightness(100);
  rgb.setPixelColor(0, COLOR_OFF);
  rgb.show();
}

void tickAlarm() {
  static unsigned long startTime = 0;
  static unsigned long lastToggle = 0;
  static bool phase = false;
  static bool firstDone = false;

  unsigned long now = millis();

  if (!firstDone) {
    if (startTime == 0) {
      startTime = now;
      tone(PIN_BUZZER, 1000);
      rgb.setPixelColor(0, COLOR_RED_HI);
      rgb.show();
    }

    if (now - startTime >= 3000) {
      noTone(PIN_BUZZER);
      firstDone = true;
      lastToggle = now;
    }
    return;
  }

  if (now - lastToggle >= 500) {
    lastToggle = now;
    phase = !phase;

    if (phase) {
      tone(PIN_BUZZER, 1000);
      rgb.setPixelColor(0, COLOR_RED_HI);
    } else {
      noTone(PIN_BUZZER);
      rgb.setPixelColor(0, COLOR_RED_LO);
    }
    rgb.show();
  }
}

bool postAlarmWindow() {
  Serial.println("[ALARM] Post-confirmation window (60s)");

  int x = 60;

  while (!powerPresent() && x > 0) {

    Serial.printf("[ALARM] Tick %d\n", x);

    tone(PIN_BUZZER, 1000);
    rgb.setPixelColor(0, COLOR_RED_HI);
    rgb.show();

    unsigned long t = millis();
    while (millis() - t < 500) {
      if (powerPresent()) {
        noTone(PIN_BUZZER);
        Serial.println("[ALARM] Power restored (ON)");
        return true;
      }
      delay(10);
    }

    noTone(PIN_BUZZER);
    rgb.setPixelColor(0, COLOR_RED_LO);
    rgb.show();

    t = millis();
    while (millis() - t < 500) {
      if (powerPresent()) {
        Serial.println("[ALARM] Power restored (OFF)");
        return true;
      }
      delay(10);
    }

    x--;
  }

  Serial.println("[ALARM] Timeout");
  return false;
}

void tickBlue() {
  static unsigned long last = 0;
  static bool phase = false;

  if (millis() - last >= 500) {
    last = millis();
    phase = !phase;
    rgb.setPixelColor(0, phase ? COLOR_BLU_HI : COLOR_BLU_LO);
    rgb.show();
  }
}

void signalPowerRestored() {
  Serial.println("[EVENT] Power RESTORED");

  tone(PIN_BUZZER, 900);
  rgb.setPixelColor(0, COLOR_BLU_HI);
  rgb.show();

  delay(5000);

  noTone(PIN_BUZZER);
}

void signalPingAttempt() {
  Serial.println("[PING] Attempt");

  tone(PIN_BUZZER, 1000);
  delay(500);
  noTone(PIN_BUZZER);
}

void signalSuccess() {
  Serial.println("[PING] SUCCESS");

  tone(PIN_BUZZER, 900);
  rgb.setPixelColor(0, COLOR_GREEN);
  rgb.show();

  delay(3000);

  noTone(PIN_BUZZER);
  rgb.setPixelColor(0, COLOR_OFF);
  rgb.show();
}

bool checkFluctuation() {
  if (powerPresent()) return false;

  Serial.println("[WARN] Fluctuation");

  unsigned long start = millis();
  while (millis() - start < FLUCTUATION_MS) {
    tickAlarm();
    if (powerPresent()) return false;
    delay(10);
  }
  return true;
}

void connectWiFiAlarm() {
  Serial.println("[WIFI] Connecting (alarm)");
  WiFi.begin(SSID, PASS);

  while (WiFi.status() != WL_CONNECTED) {
    tickAlarm();
    delay(50);
  }

  Serial.println("[WIFI] Connected");
}

void connectWiFiBlue() {
  Serial.println("[WIFI] Connecting (recovery)");
  WiFi.begin(SSID, PASS);

  while (WiFi.status() != WL_CONNECTED) {
    tickBlue();
    delay(50);
  }

  Serial.println("[WIFI] Connected");
}

int sendPOST() {
  Serial.println("[POST] Sending");

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(3000);
  http.begin(client, POST_URL);

  int code = http.POST("");

  Serial.print("[POST] Code: ");
  Serial.println(code);

  http.end();
  return code;
}

void sendWOL() {
  Serial.println("[WOL] Sending");

  uint8_t packet[102];
  memset(packet, 0xFF, 6);
  for (int i = 1; i <= 16; i++) memcpy(packet + i * 6, WOL_MAC, 6);

  WiFiUDP udp;
  udp.begin(WOL_PORT);
  udp.beginPacket(WOL_BROADCAST, WOL_PORT);
  udp.write(packet, sizeof(packet));
  udp.endPacket();
}

bool pingHost() {
  WiFiClient client;
  client.setTimeout(3000);
  bool ok = client.connect(PING_IP, 80);
  client.stop();
  return ok;
}

void sleepUntilLow() {
  Serial.println("[SLEEP] Waiting LOW");
  noTone(PIN_BUZZER);
  rgb.setPixelColor(0, COLOR_OFF);
  rgb.show();
  delay(10);
  esp_sleep_enable_ext0_wakeup(SENSE_PIN, 0);
  esp_deep_sleep_start();
}

void sleepUntilHigh() {
  Serial.println("[SLEEP] Waiting HIGH");
  noTone(PIN_BUZZER);
  rgb.setPixelColor(0, COLOR_OFF);
  rgb.show();
  delay(10);
  esp_sleep_enable_ext0_wakeup(SENSE_PIN, 1);
  esp_deep_sleep_start();
}

void setup() {
  delay(1000);
  Serial.begin(115200);

  Serial.println("\n=== BOOT ===");
  setupHardware();

  if (powerPresent()) {

    if (!powerWasOut) {
      sleepUntilLow();
    }

    signalPowerRestored();

    unsigned long start = millis();
    while (millis() - start < STABILIZE_MS) {
      tickBlue();
      if (checkFluctuation()) sleepUntilHigh();
      delay(10);
    }

    connectWiFiBlue();

    for (int i = 0; i < WOL_RETRIES; i++) {

      sendWOL();

      unsigned long waitStart = millis();
      while (millis() - waitStart < PING_WAIT_MS) {
        tickBlue();
        if (checkFluctuation()) {
          WiFi.disconnect(true);
          sleepUntilHigh();
        }
        delay(10);
      }

      signalPingAttempt();

      if (pingHost()) {
        WiFi.disconnect(true);
        signalSuccess();
        powerWasOut = false;
        sleepUntilLow();
      }
    }

    WiFi.disconnect(true);
    powerWasOut = false;
    sleepUntilLow();

  } else {

    Serial.println("[EVENT] Power LOST");

    powerWasOut = true;

    connectWiFiAlarm();
    int code = sendPOST();
    WiFi.disconnect(true);

    if (code == 200) {

      bool restored = postAlarmWindow();

      if (!restored) {
        sleepUntilHigh();
      }

      signalPowerRestored();

      unsigned long start = millis();
      while (millis() - start < STABILIZE_MS) {
        tickBlue();
        if (checkFluctuation()) sleepUntilHigh();
        delay(10);
      }

      connectWiFiBlue();

      for (int i = 0; i < WOL_RETRIES; i++) {

        sendWOL();

        unsigned long waitStart = millis();
        while (millis() - waitStart < PING_WAIT_MS) {
          tickBlue();
          if (checkFluctuation()) {
            WiFi.disconnect(true);
            sleepUntilHigh();
          }
          delay(10);
        }

        signalPingAttempt();

        if (pingHost()) {
          WiFi.disconnect(true);
          signalSuccess();
          powerWasOut = false;
          sleepUntilLow();
        }
      }

      WiFi.disconnect(true);
      powerWasOut = false;
      sleepUntilLow();

    } else {
      sleepUntilHigh();
    }
  }
}

void loop() {
  delay(1000);
}