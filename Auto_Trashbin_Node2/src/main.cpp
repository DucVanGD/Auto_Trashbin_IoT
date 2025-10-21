#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_SH110X.h>

// Định nghĩa các chân PIN
#define TRIG_PIN 26
#define ECHO_PIN 25
#define MQ135_PIN 34

// Định nghĩa các thông số OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define I2C_ADDRESS 0x3C

Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Định nghĩa các thông số MQTT & WIFI
#define TOKEN "LMWzjfQz2PdvfCFIpV5K"
#define THINGSBOARD_SERVER "demo.thingsboard.io"

const char *ssid = "Duc";
const char *password = "12345678";

WiFiClient espClient;
PubSubClient client(espClient);

// Cấu hình UART
#define UART_BAUD 115200 // Tốc độ baud cho UART
#define UART_RX 16
#define UART_TX 17

// Định nghĩa các ngưỡng cảnh báo
const float DISTANCE_THRESHOLD = 80.0; // %
const float GAS_THRESHOLD_HIGH = 500.0; // ppm
const float DISTANCE_MAX = 23.0; // cm

// Các hằng để tính toán nồng độ khí (ppm) từ cảm biến MQ-135 
const float VCC_SENSOR = 5.0f;
const float ADC_MAX = 4095.0f;
const float ADC_REF_VOLTAGE = 3.3f;
const float RL_VALUE = 20000.0f;
float R0 = 200000.0f;
const float GAS_A = 116.603f;
const float GAS_B = -2.769f;

// Biến trạng thái hệ thống
String currentState = "OK";
bool systemOpen = false; 

// Các hàm đọc cảm biến
float readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  float duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return 0.0;
  return duration * 0.0343 / 2.0;
}

float readGas() {
  int raw = analogRead(MQ135_PIN);
  float v_out = (raw / ADC_MAX) * ADC_REF_VOLTAGE;
  if (v_out <= 0) return 0.0;
  float rs = RL_VALUE * (VCC_SENSOR - v_out) / v_out;
  float ratio = rs / R0;
  float ppm = GAS_A * pow(ratio, GAS_B);
  return (ppm < 0) ? 0 : ppm;
}
// Kết nối WiFi
void setupWiFi() {
  Serial.print("Dang ket noi Wifi ");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("\nWifi da ket noi");
}
// Hàm gửi và kiểm tra thông tin gửi
void sendUartState(const String &state) {
  Serial2.println(state);
  Serial.print("UART gui: ");
  Serial.println(state);
}
// Hàm callback MQTT
void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();

  String topicStr(topic);
  int idx = topicStr.lastIndexOf('/');
  String requestId = "";
  if (idx >= 0 && idx < (int)topicStr.length() - 1) requestId = topicStr.substring(idx + 1);
  String responseTopic = "";
  if (requestId.length() > 0) responseTopic = String("v1/devices/me/rpc/response/") + requestId;

  String lower = msg;
  lower.toLowerCase();

  if (msg.startsWith("{")) {
    if (lower.indexOf("setstate") >= 0) {
      int pIdx = lower.indexOf("params");
      bool paramFound = false;
      bool paramTrue = false;
      if (pIdx >= 0) {
        int colon = lower.indexOf(':', pIdx);
        if (colon >= 0) {
          int start = colon + 1;
          int end = lower.indexOf(',', start);
          int end2 = lower.indexOf('}', start);
          if (end2 >= 0 && (end < 0 || end2 < end)) end = end2;
          if (end < 0) end = lower.length();
          String ps = lower.substring(start, end);
          ps.trim();
          if (ps.indexOf("true") >= 0 || ps.indexOf("1") >= 0) { paramTrue = true; paramFound = true; }
          else if (ps.indexOf("false") >= 0 || ps.indexOf("0") >= 0) { paramTrue = false; paramFound = true; }
        }
      }
      if (paramFound) {
        if (paramTrue) {
          Serial.println("Button mo, gui tin hieu OPEN");
          sendUartState("OPEN"); // Gửi lệnh OPEN qua UART ve node 1
          systemOpen = true;
        } else {
          Serial.println("Button dong, gui tin hieu CLOSE");
          sendUartState("CLOSE"); // Gửi lệnh CLOSE qua UART ve node 1
          systemOpen = false;
        }
        if (responseTopic.length() > 0) client.publish(responseTopic.c_str(), paramTrue ? "true" : "false");
      } else {
        if (responseTopic.length() > 0) client.publish(responseTopic.c_str(), "error");
      }
      return;
    }

    if (lower.indexOf("getstate") >= 0) {
      if (responseTopic.length() > 0) client.publish(responseTopic.c_str(), systemOpen ? "true" : "false");
      return;
    }
  }
}

// Kiểm tra kết nối mqtt và tái kết nối
void reconnectMQTT() {
  while (!client.connected()) {
    String cid = "ESP32-" + WiFi.macAddress();
    if (client.connect(cid.c_str(), TOKEN, NULL)) {
      client.subscribe("v1/devices/me/rpc/request/+");
      Serial.println("MQTT da ket noi");
    } else {
      Serial.println("MQTT tai ket noi...");
      delay(2000);
    }
  }
}

// Hàm setup
void setup() {
  Serial.begin(115200);
  Serial2.begin(UART_BAUD, SERIAL_8N1, UART_RX, UART_TX);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  Wire.begin();
  if (!display.begin(I2C_ADDRESS, true)) {
    Serial.println("Ko tim duoc OLED!");
    while (true) delay(100);
  }
  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Dang thuc hien ket noi Wifi...");
  display.display();

  setupWiFi();
  client.setServer(THINGSBOARD_SERVER, 1883);
  client.setCallback(mqttCallback);
}

// Hàm loop chính
void loop() {
  if (!client.connected()) reconnectMQTT();
  client.loop();

  float distance = readDistance();
  float percent = (DISTANCE_MAX - distance) * 100 / DISTANCE_MAX; // %
  float gas = readGas();
  bool isAlert = (percent >= DISTANCE_THRESHOLD || gas >= GAS_THRESHOLD_HIGH) ? true : false;
  if (isAlert) {
    currentState = "ALERT";
  } else {
    currentState = "OK";
  }
  // Nếu hệ thống đang đóng (CLOSE) thì mới gửi trạng thái cảm biến
  if (!systemOpen) {
    sendUartState(currentState);
  } else {
    Serial.println("He thong dang OPEN");
  }

  // Hiển thị OLED
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.printf("Perc: %.1f %\n", percent);
  display.printf("Gas : %.1f ppm\n", gas);
  display.setCursor(0, 40);
  display.print("State: ");
  display.println(currentState);
  display.setCursor(0, 56);
  display.print("Mode: ");
  display.println(systemOpen ? "OPEN" : "CLOSE");
  display.display();

  // Gửi Telemetry lên ThingsBoard
  String payload = "{";
  payload += "\"percent\":" + String(percent, 1) + ",";
  payload += "\"gas\":" + String(gas, 1) + ",";
  payload += "\"state\":\"" + currentState + "\",";
  payload += "\"mode\":\"" + String(systemOpen ? "OPEN" : "CLOSE") + "\"}";
  Serial.printf("Payload: %s\n", payload.c_str());
  client.publish("v1/devices/me/telemetry", payload.c_str());

  delay(1000);
}