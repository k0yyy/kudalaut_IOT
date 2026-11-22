#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHTesp.h>
#include <ESP32Servo.h>

// ===== WIFI & MQTT =====
const char* WIFI_SSID = "Wokwi-GUEST";   // di board asli: nama WiFi kamu
const char* WIFI_PASS = "";              // di board asli: password WiFi
const char* MQTT_HOST = "broker.emqx.io";
const int   MQTT_PORT = 1883;

#define T_SUHU   "smartroom/suhu"
#define T_LDR    "smartroom/cahaya"
#define T_GAS    "smartroom/gas"
#define T_KIPAS  "smartroom/kipas"

// ===== PIN =====
#define DHT_PIN    15
#define LDR_PIN    34
#define MQ2_PIN    35
#define SERVO_PIN  18
#define RELAY_PIN  26
#define LED_PIN    2

// ===== THRESHOLD =====
#define LDR_DARK_ON    2500   // di atas ini dianggap gelap (jika DARK_IS_HIGH)
#define LDR_DARK_OFF   2000   // di bawah ini dianggap terang
#define DARK_IS_HIGH   true   // ubah ke false kalau di Wokwi arahnya kebalik

#define TEMP_ON        25.0
#define TEMP_OFF       24.0

#define GAS_ON         2000
#define GAS_OFF        1800

#define VENT_OPEN      90
#define VENT_CLOSE     0

// false = Relay aktif HIGH, true = Relay aktif LOW
#define RELAY_ACTIVE_LOW false

DHTesp dht;
Servo ventServo;
WiFiClient espClient;
PubSubClient mqtt(espClient);

bool fanOn = false, dark = false, gas = false, lastGas = false;
unsigned long lastPrint = 0;

// ===== KONTROL KIPAS =====
void setFan(bool state) {
  fanOn = state;
  bool level = RELAY_ACTIVE_LOW ? !state : state;
  digitalWrite(RELAY_PIN, level ? HIGH : LOW);
}

// ===== KONEKSI =====
void connectWiFi() {
  Serial.print("[WiFi] menghubungkan");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[WiFi] connected");
}

void connectMQTT() {
  while (!mqtt.connected()) {
    String clientId = "esp32-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    if (mqtt.connect(clientId.c_str())) {
      Serial.println("[MQTT] connected");
    } else {
      Serial.printf("[MQTT] gagal, state: %d\n", mqtt.state());
      delay(2000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  setFan(false);

  dht.setup(DHT_PIN, DHTesp::DHT22);

  ventServo.setPeriodHertz(50);
  ventServo.attach(SERVO_PIN, 500, 2400);
  ventServo.write(VENT_CLOSE);

  connectWiFi();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  // ===== LDR -> LED (gelap = nyala) =====
  int lightValue = analogRead(LDR_PIN);

  if (DARK_IS_HIGH) {
    if (lightValue > LDR_DARK_ON)       dark = true;
    else if (lightValue < LDR_DARK_OFF) dark = false;
  } else {
    if (lightValue < LDR_DARK_OFF)      dark = true;
    else if (lightValue > LDR_DARK_ON)  dark = false;
  }
  digitalWrite(LED_PIN, dark ? HIGH : LOW);

  // ===== DHT22 -> RELAY -> KIPAS =====
  TempAndHumidity data = dht.getTempAndHumidity();
  float temp = data.temperature;

  if (!isnan(temp)) {
    if (temp >= TEMP_ON)      setFan(true);
    else if (temp < TEMP_OFF) setFan(false);
  }

  // ===== MQ2 -> SERVO =====
  int gasValue = analogRead(MQ2_PIN);

  if (gasValue >= GAS_ON)      gas = true;
  else if (gasValue < GAS_OFF) gas = false;

  if (gas != lastGas) {
    ventServo.write(gas ? VENT_OPEN : VENT_CLOSE);
    lastGas = gas;
  }

  // ===== PUBLISH + SERIAL (tiap 2 detik) =====
  if (millis() - lastPrint >= 2000) {
    lastPrint = millis();

    char buf[16];
    if (!isnan(temp)) {
      snprintf(buf, sizeof(buf), "%.1f", temp);
      mqtt.publish(T_SUHU, buf);
    }
    snprintf(buf, sizeof(buf), "%d", lightValue);
    mqtt.publish(T_LDR, buf);
    snprintf(buf, sizeof(buf), "%d", gasValue);
    mqtt.publish(T_GAS, buf);
    mqtt.publish(T_KIPAS, fanOn ? "ON" : "OFF");

    Serial.printf("LDR:%d (%s) | Suhu:%.1f C | Gas:%d (%s) | Kipas:%s\n",
      lightValue, dark ? "gelap" : "terang", temp,
      gasValue, gas ? "ADA" : "aman", fanOn ? "ON" : "OFF");
  }

  delay(200);
}