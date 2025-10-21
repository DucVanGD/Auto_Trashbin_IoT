#include <Arduino.h>
#include <ESP32Servo.h>

#define PIR_PIN 27
#define TRIG_PIN 26
#define ECHO_PIN 25
#define SERVO_PIN 14
#define RX2 16
#define TX2 17

Servo myServo;

bool motionDetected = false;
bool lidOpen = false;
bool alertActive = false;
bool openLock = false;
String uartCommand = "OK";

unsigned long lastMotionTime = 0;
unsigned long motionStartTime = 0;
bool countingMotion = false;

float readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  float distance = duration * 0.034 / 2;
  if (distance <= 0 || distance > 400) distance = -1;
  return distance;
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, RX2, TX2);
  pinMode(PIR_PIN, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  myServo.attach(SERVO_PIN);
  myServo.write(0);
  Serial.println("Khoi dong he thong...");
  delay(3000);
  Serial.println("He thong san sang!");
}

void loop() {
  if (Serial2.available()) {
    String newCmd = Serial2.readStringUntil('\n');
    newCmd.trim();
    newCmd.toUpperCase();
    if (openLock && newCmd != "CLOSE") {
      Serial.println("Bo qua lenh vi dang OPEN mode");
    } else {
      if (newCmd.length() > 0) {
        uartCommand = newCmd;
        Serial.print("Nhan lenh UART: ");
        Serial.println(uartCommand);
      }
    }
  }

  if (uartCommand == "OPEN") {
    Serial.println("UART: OPEN - Mo nap va giu nguyen cho den khi CLOSE");
    myServo.write(90);
    lidOpen = true;
    openLock = true;
    uartCommand = "OK";
  }

  if (uartCommand == "CLOSE") {
    Serial.println("UART: CLOSE - Dong nap lai, cho phep nhan lenh moi");
    myServo.write(0);
    lidOpen = false;
    openLock = false;
    uartCommand = "OK";
  }

  if (openLock) {
    delay(200);
    return;
  }

  if (uartCommand == "ALERT") {
    if (!alertActive) {
      Serial.println("ALERT: Dong nap va khoa mo");
      myServo.write(0);
      lidOpen = false;
      alertActive = true;
    }
    delay(300);
    return;
  }

  if (uartCommand == "OK" && alertActive) {
    Serial.println("ALERT ket thuc - He thong tro lai binh thuong");
    alertActive = false;
  }

  if (alertActive) {
    delay(200);
    return;
  }

  int pirState = digitalRead(PIR_PIN);
  float distance = readDistance();
  unsigned long now = millis();

  if (pirState == HIGH && distance > 0 && distance < 50) {
    if (!countingMotion) {
      motionStartTime = now;
      countingMotion = true;
    }
    if (!lidOpen && (now - motionStartTime >= 1000)) {
      Serial.println("Phat hien chuyen dong va nguoi dung gan >2s - Mo nap");
      myServo.write(90);
      lidOpen = true;
      motionDetected = true;
      lastMotionTime = now;
    }
  } else {
    countingMotion = false;
    motionStartTime = 0;
  }

  if (lidOpen && (distance > 50)) {
    if (now - lastMotionTime > 3000) {
      Serial.println("Khong co nguoi trong 3s - Dong nap");
      myServo.write(0);
      lidOpen = false;
      motionDetected = false;
    }
  } else if (lidOpen && (pirState == HIGH || (distance > 0 && distance < 50))) {
    lastMotionTime = now;
  }

  Serial.print("[CB] PIR: ");
  Serial.print(pirState == HIGH ? "PHAT HIEN" : "KHONG");
  Serial.print(" | HC-SR04: ");
  if (distance == -1) Serial.print("LOI / NGOAI TAM");
  else Serial.print(String(distance, 1) + " cm");
  Serial.print(" | Nap: ");
  Serial.println(lidOpen ? "MO" : "DONG");

  delay(200);
}
