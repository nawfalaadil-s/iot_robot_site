/*
 * =========================================================================
 * IoT-Enabled AI Autonomous Industrial Monitoring & Inspection Robot
 * FINAL v5.0 - LIVE BACKEND + FULL-SPEED DIGITAL DRIVE
 * =========================================================================
 *
 * Backend:   Netlify Functions + Netlify Blobs (real-time data storage)
 * Dashboard: https://YOUR-SITE.netlify.app   <-- set DASHBOARD_URL below!
 *
 * ALL FEATURES INCLUDED:
 * ✅ Full-Speed Digital Drive (ENA/ENB jumpers shorted on L298N)
 * ✅ Software-PWM Speed Control (Slow/Medium/Fast via IN pins)
 * ✅ PID Line Following Algorithm
 * ✅ Intelligent Obstacle Avoidance
 * ✅ RFID Machine Identification (3 machines)
 * ✅ Multi-Sensor Monitoring (DHT11, MQ135, Ultrasonic, IR)
 * ✅ WiFi + Bluetooth Control
 * ✅ Real-time IoT to Netlify Dashboard
 * ✅ AI/ML Analytics Integration
 * ✅ Anomaly Detection
 * ✅ OTA Updates
 * ✅ Watchdog Timer
 *
 * Version: 5.2 FINAL (SD, ESP32-CAM & battery monitoring removed - not used)
 * Date: September 9, 2026
 * Tested: ESP32 Arduino Core 3.x
 * =========================================================================
 */
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <BluetoothSerial.h>
#include <DHT.h>
#include <SPI.h>
#include <MFRC522.h>
#include <EEPROM.h>
#include <time.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>
#include "robot_types.h"   // enums & structs — MUST be the LAST include (Arduino prototype fix)

// =========================================================================
// 📝 CONFIGURATION - CHANGE THESE FOR YOUR SETUP
// =========================================================================

// WiFi Settings
const char* ssid = "robo";                    // Your WiFi name
const char* password = "robo@123";            // Your WiFi password

// Dashboard URL  ⚠️ CHANGE THIS to your own Netlify site URL after deploying!
const char* DASHBOARD_URL = "https://iot-car.netlify.app/";

// Backend endpoints (Netlify Functions - same origin as the dashboard)
String inspectionEndpoint() { return String(DASHBOARD_URL) + "/api/inspection"; }
String liveEndpoint()       { return String(DASHBOARD_URL) + "/api/live"; }

// Bluetooth Name
const char* BT_NAME = "ESP32_INDUSTRIAL_ROBOT";

// Time & Location
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 0;

// System Settings
const char* hostname = "industrial-robot";
#define WDT_TIMEOUT 30

// =========================================================================
// 🔌 HARDWARE PIN CONFIGURATION
// =========================================================================

// Sensors
#define DHTPIN 4
#define DHTTYPE DHT11
#define LEFT_IR 32
#define RIGHT_IR 33
#define TRIG_PIN 17
#define ECHO_PIN 35
#define MQ135_PIN 34

// RFID Reader
#define RST_PIN 22
#define SS_PIN 21

// Motors (L298N) — ENA/ENB jumpers are SHORTED on the driver (full-speed mode)
// GPIO 12 / GPIO 25 are therefore NOT used. Speed control is done in software
// by duty-cycling the IN1..IN4 pins (100 Hz software PWM).
#define IN1 13
#define IN2 14
#define IN3 27
#define IN4 26

// Other
#define BUZZER_PIN 5
#define STATUS_LED 2

// Software PWM Configuration (replaces hardware LEDC PWM - ENA/ENB are shorted)
#define PWM_STEPS 10        // 10-step duty resolution (10% granularity)
#define PWM_STEP_US 1000    // 1 ms per step → 10 ms PWM period (100 Hz)

// =========================================================================
// 🎯 GLOBAL OBJECTS & VARIABLES
// =========================================================================

DHT dht(DHTPIN, DHTTYPE);
MFRC522 rfid(SS_PIN, RST_PIN);
BluetoothSerial SerialBT;

// (Enums & structs moved to the robot_types.h tab — do not redefine here)

// State Variables
RobotMode currentMode = MODE_IDLE;
SpeedProfile currentSpeed = SPEED_FAST;

Machine machines[3] = {
  {"", "Machine A - Hydraulic Press", "Zone 1", 0, 0, 0, 0, false, 50.0, 15.0, 450},
  {"", "Machine B - CNC Lathe",       "Zone 2", 0, 0, 0, 0, false, 45.0, 15.0, 400},
  {"", "Machine C - Conveyor Motor",  "Zone 3", 0, 0, 0, 0, false, 48.0, 15.0, 420}
};

Machine* currentMachine = nullptr;
int lastDetectedMachineIndex = -1;

// Motor Control
int currentSpeedLeft = 0;
int currentSpeedRight = 0;
int targetSpeedLeft = 0;
int targetSpeedRight = 0;

// Timing
unsigned long lastIoTUpdate = 0;
unsigned long lastSensorRead = 0;
unsigned long lastObstacleCheck = 0;
unsigned long inspectionStartTime = 0;

const unsigned long IOT_UPDATE_INTERVAL = 3000;
const unsigned long SENSOR_READ_INTERVAL = 500;
const unsigned long OBSTACLE_CHECK_INTERVAL = 200;
const unsigned long INSPECTION_DURATION = 15000;

// Flags
String currentAlert = "";
bool obstacleDetected = false;
unsigned long totalInspections = 0;
unsigned long alertCount = 0;

// =========================================================================
// 🚗 MOTOR CONTROL — L298N with ENA/ENB SHORTED (software PWM on IN pins)
// =========================================================================

uint8_t pwmStep = 0;
unsigned long lastPwmTickUs = 0;

// Drive one wheel: duty 0-100%, direction by sign of currentSpeed value
void applyWheel(uint8_t pinA, uint8_t pinB, int signedDuty) {
  int mag = abs(signedDuty);
  if (mag == 0) {
    digitalWrite(pinA, LOW);
    digitalWrite(pinB, LOW);
    return;
  }
  bool drive = pwmStep < ((mag * PWM_STEPS) / 100);   // duty% → active steps
  if (drive) {
    if (signedDuty > 0) { digitalWrite(pinA, HIGH); digitalWrite(pinB, LOW); }
    else                { digitalWrite(pinA, LOW);  digitalWrite(pinB, HIGH); }
  } else {
    digitalWrite(pinA, LOW);
    digitalWrite(pinB, LOW);
  }
}

// One PWM step (~1 ms). Called continuously from loop()/delayPWM().
void pwmTick() {
  applyWheel(IN1, IN2, currentSpeedLeft);
  applyWheel(IN3, IN4, currentSpeedRight);
  pwmStep = (pwmStep + 1) % PWM_STEPS;
  delayMicroseconds(PWM_STEP_US);
}

// Non-blocking delay that keeps the motors PWM-ed (used instead of delay())
void delayPWM(unsigned long ms) {
  unsigned long endTime = millis() + ms;
  while ((long)(millis() - endTime) < 0) {
    pwmTick();
    esp_task_wdt_reset();
  }
}

void setupMotorPWM() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  stopMotors();
  Serial.println("[MOTOR] Digital full-speed drive ready (soft-PWM on IN pins)");
}

void setMotorSpeed(int speedLeft, int speedRight) {
  targetSpeedLeft = constrain(speedLeft, -100, 100);
  targetSpeedRight = constrain(speedRight, -100, 100);
  currentSpeedLeft = targetSpeedLeft;    // instant apply; PWM smooths the torque
  currentSpeedRight = targetSpeedRight;
}

void smoothAccelerate() {
  // Ramp current duty toward target (rate-limited for smooth acceleration)
  static unsigned long lastRamp = 0;
  unsigned long now = millis();
  if (now - lastRamp < 50) return;
  lastRamp = now;
  
  if (currentSpeedLeft < targetSpeedLeft) currentSpeedLeft = min(currentSpeedLeft + 5, targetSpeedLeft);
  else if (currentSpeedLeft > targetSpeedLeft) currentSpeedLeft = max(currentSpeedLeft - 5, targetSpeedLeft);
  
  if (currentSpeedRight < targetSpeedRight) currentSpeedRight = min(currentSpeedRight + 5, targetSpeedRight);
  else if (currentSpeedRight > targetSpeedRight) currentSpeedRight = max(currentSpeedRight - 5, targetSpeedRight);
}

void stopMotors() {
  targetSpeedLeft = 0;
  targetSpeedRight = 0;
  currentSpeedLeft = 0;
  currentSpeedRight = 0;
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
}

void moveForward(int speed = SPEED_FAST) {
  targetSpeedLeft = speed;
  targetSpeedRight = speed;
}

void moveBackward(int speed = SPEED_FAST) {
  targetSpeedLeft = -speed;
  targetSpeedRight = -speed;
}

void turnLeft(int speed = SPEED_FAST) {
  targetSpeedLeft = -speed / 2;
  targetSpeedRight = speed;
}

void turnRight(int speed = SPEED_FAST) {
  targetSpeedLeft = speed;
  targetSpeedRight = -speed / 2;
}

// Timed movement that keeps software PWM alive during the maneuver
void driveFor(int left, int right, unsigned long ms) {
  setMotorSpeed(left, right);
  unsigned long endTime = millis() + ms;
  while ((long)(millis() - endTime) < 0) {
    pwmTick();
    esp_task_wdt_reset();
  }
  stopMotors();
}

// =========================================================================
// 🎯 PID LINE FOLLOWING
// =========================================================================

void performPIDLineFollowing() {
  int leftIR = digitalRead(LEFT_IR);
  int rightIR = digitalRead(RIGHT_IR);
  
  float error = 0;
  
  if (leftIR == 0 && rightIR == 0) {
    error = 0;
  } else if (leftIR == 0 && rightIR == 1) {
    error = -1;
    stats.lineDeviations++;
  } else if (leftIR == 1 && rightIR == 0) {
    error = 1;
    stats.lineDeviations++;
  } else {
    error = pidController.lastError * 2;
    stats.lineDeviations++;
  }
  
  pidController.integral += error;
  pidController.integral = constrain(pidController.integral, -100, 100);
  
  float derivative = error - pidController.lastError;
  pidController.output = (pidController.kp * error) + 
                         (pidController.ki * pidController.integral) + 
                         (pidController.kd * derivative);
  pidController.lastError = error;
  
  int baseSpeed = currentSpeed;   // duty % (speed profile)
  int leftSpeed = baseSpeed - pidController.output;
  int rightSpeed = baseSpeed + pidController.output;
  
  targetSpeedLeft = constrain(leftSpeed, 0, 100);
  targetSpeedRight = constrain(rightSpeed, 0, 100);
}

// =========================================================================
// 🚧 OBSTACLE AVOIDANCE
// =========================================================================

float readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  
  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return -1;
  return duration * 0.0343 / 2.0;
}

bool checkObstacle() {
  if (millis() - lastObstacleCheck < OBSTACLE_CHECK_INTERVAL) return obstacleDetected;
  
  lastObstacleCheck = millis();
  float distance = readDistance();
  
  if (distance > 0 && distance < 15.0) {
    obstacleDetected = true;
    stats.obstaclesDetected++;
    return true;
  }
  
  obstacleDetected = false;
  return false;
}

void performObstacleAvoidance() {
  Serial.println("[AVOID] Obstacle detected!");
  
  stopMotors();
  delayPWM(300);
  tone(BUZZER_PIN, 2000, 200);
  delayPWM(300);
  
  // Back up (PWM stays alive during the maneuver)
  driveFor(-SPEED_SLOW, -SPEED_SLOW, 800);
  delayPWM(200);
  
  // Try right
  driveFor(SPEED_SLOW, -SPEED_SLOW, 500);
  
  float newDistance = readDistance();
  if (newDistance < 15.0 || newDistance < 0) {
    // Try left
    driveFor(-SPEED_SLOW, SPEED_SLOW, 1000);
  }
  
  currentMode = MODE_AUTO_LINE_FOLLOW;
}

// =========================================================================
// 📊 SENSOR READING
// =========================================================================

void readAllSensors() {
  if (millis() - lastSensorRead < SENSOR_READ_INTERVAL) return;
  
  lastSensorRead = millis();
  
  currentReading.temperature = dht.readTemperature();
  currentReading.humidity = dht.readHumidity();
  currentReading.distance = readDistance();
  currentReading.leftIR = digitalRead(LEFT_IR);
  currentReading.rightIR = digitalRead(RIGHT_IR);
  currentReading.gasLevel = analogRead(MQ135_PIN);
  currentReading.timestamp = millis();
  
  if (isnan(currentReading.temperature)) currentReading.temperature = 0;
  if (isnan(currentReading.humidity)) currentReading.humidity = 0;
  if (currentReading.distance < 0) currentReading.distance = 0;
}

// =========================================================================
// 🏷️ RFID FUNCTIONS
// =========================================================================

String readRFID() {
  if (!rfid.PICC_IsNewCardPresent()) return "";
  if (!rfid.PICC_ReadCardSerial()) return "";
  
  String uid = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    uid += String(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
    uid += String(rfid.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();
  
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
  return uid;
}

int identifyMachine(String uid) {
  for (int i = 0; i < 3; i++) {
    if (machines[i].uid == uid) return i;
  }
  return -1;
}

void registerMachine(String uid) {
  for (int i = 0; i < 3; i++) {
    if (machines[i].uid == "") {
      machines[i].uid = uid;
      Serial.println("[RFID] Machine " + String(i) + " registered: " + uid);
      EEPROM.writeString(i * 32, uid);
      EEPROM.commit();
      
      for (int j = 0; j < 3; j++) {
        tone(BUZZER_PIN, 1500, 100);
        delay(150);
      }
      return;
    }
  }
}

// =========================================================================
// 🚨 ANOMALY DETECTION
// =========================================================================

bool detectAnomalies(SensorData data, Machine* machine) {
  bool anomalyDetected = false;
  currentAlert = "";
  
  if (data.temperature > machine->maxTemp) {
    currentAlert += "HIGH_TEMP,";
    anomalyDetected = true;
  }
  if (data.gasLevel > machine->maxGas) {
    currentAlert += "HIGH_GAS,";
    anomalyDetected = true;
  }
  if (data.humidity > 85.0) {
    currentAlert += "HIGH_HUMIDITY,";
    anomalyDetected = true;
  }
  
  if (anomalyDetected) {
    alertCount++;
    stats.alertsTriggered++;
    tone(BUZZER_PIN, 2000, 500);
  }
  
  return anomalyDetected;
}

// =========================================================================
// 🔍 INSPECTION ROUTINE
// =========================================================================

void performInspection(Machine* machine) {
  Serial.println("\n========================================");
  Serial.println("[INSPECTION] " + machine->name);
  Serial.println("========================================");
  
  stopMotors();
  
  for (int i = 0; i < 5; i++) {
    digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
    delay(100);
  }
  
  tone(BUZZER_PIN, 1800, 200);
  delay(300);
  tone(BUZZER_PIN, 2000, 200);
  
  inspectionStartTime = millis();
  int sampleCount = 0;
  float tempSum = 0, gasSum = 0, humiditySum = 0, distanceSum = 0;
  
  while (millis() - inspectionStartTime < INSPECTION_DURATION) {
    readAllSensors();
    
    tempSum += currentReading.temperature;
    gasSum += currentReading.gasLevel;
    humiditySum += currentReading.humidity;
    distanceSum += currentReading.distance;
    sampleCount++;
    
    if (detectAnomalies(currentReading, machine)) {
      machine->hasAlert = true;
    }
    
    esp_task_wdt_reset();
    delay(1000);
  }
  
  float avgTemp = tempSum / sampleCount;
  float avgGas = gasSum / sampleCount;
  float avgHumidity = humiditySum / sampleCount;
  float avgDistance = distanceSum / sampleCount;
  
  machine->inspectionCount++;
  machine->lastInspection = millis();
  machine->avgTemperature = (machine->avgTemperature + avgTemp) / 2.0;
  machine->avgGasLevel = (machine->avgGasLevel + avgGas) / 2.0;
  
  sendInspectionData(machine, avgTemp, avgHumidity, avgGas, avgDistance);
  
  Serial.println("[INSPECTION] Complete");
  Serial.println("  Temp: " + String(avgTemp) + "C");
  Serial.println("  Gas: " + String(avgGas) + " PPM");
  Serial.print("  Alert: ");
  Serial.println(machine->hasAlert ? "YES" : "NO");
  Serial.println("========================================\n");
  
  totalInspections++;
  stats.inspectionsCompleted++;
  
  tone(BUZZER_PIN, 2200, 150);
  delay(200);
  tone(BUZZER_PIN, 2500, 150);
  
  currentMode = MODE_AUTO_LINE_FOLLOW;
}

// =========================================================================
// 🌐 IOT COMMUNICATION
// =========================================================================

void sendInspectionData(Machine* machine, float temp, float humidity, float gas, float distance) {
  if (WiFi.status() != WL_CONNECTED) return;
  
  HTTPClient http;
  StaticJsonDocument<2048> doc;
  
  doc["machineId"] = machine->uid;
  doc["machineName"] = machine->name;
  doc["location"] = machine->location;
  doc["timestamp"] = millis();
  doc["inspectionCount"] = machine->inspectionCount;
  
  JsonObject sensors = doc.createNestedObject("sensors");
  sensors["temperature"] = temp;
  sensors["humidity"] = humidity;
  sensors["gasLevel"] = gas;
  sensors["distance"] = distance;
  
  JsonObject status = doc.createNestedObject("status");
  status["hasAlert"] = machine->hasAlert;
  status["alertType"] = currentAlert;
  status["totalInspections"] = totalInspections;
  
  JsonObject statistics = doc.createNestedObject("statistics");
  statistics["obstaclesDetected"] = stats.obstaclesDetected;
  statistics["lineDeviations"] = stats.lineDeviations;
  statistics["alertsTriggered"] = stats.alertsTriggered;
  statistics["uptime"] = millis() - stats.startTime;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  Serial.println("[IoT] Sending to dashboard...");
  
  http.begin(inspectionEndpoint());
  http.addHeader("Content-Type", "application/json");
  int httpCode = http.POST(jsonString);
  
  if (httpCode > 0) {
    Serial.println("[IoT] Response: " + String(httpCode));
  } else {
    Serial.println("[IoT] Error: " + String(httpCode));
  }
  
  http.end();
  
  machine->hasAlert = false;
  currentAlert = "";
}

void sendLiveData() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (millis() - lastIoTUpdate < IOT_UPDATE_INTERVAL) return;
  
  lastIoTUpdate = millis();
  
  HTTPClient http;
  StaticJsonDocument<1024> doc;
  
  doc["robotId"] = "ROBOT_01";
  doc["mode"] = getModeString();
  doc["speed"] = currentSpeed;
  doc["timestamp"] = millis();
  doc["wifiRssi"] = WiFi.RSSI();
  doc["freeHeap"] = ESP.getFreeHeap();
  
  JsonObject sensors = doc.createNestedObject("sensors");
  sensors["temperature"] = currentReading.temperature;
  sensors["humidity"] = currentReading.humidity;
  sensors["gasLevel"] = currentReading.gasLevel;
  sensors["distance"] = currentReading.distance;
  sensors["leftIR"] = currentReading.leftIR;
  sensors["rightIR"] = currentReading.rightIR;
  
  JsonObject stats_obj = doc.createNestedObject("stats");
  stats_obj["obstaclesDetected"] = stats.obstaclesDetected;
  stats_obj["inspectionsCompleted"] = stats.inspectionsCompleted;
  stats_obj["uptime"] = millis() - stats.startTime;
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  http.begin(liveEndpoint());
  http.addHeader("Content-Type", "application/json");
  http.POST(jsonString);
  http.end();
}

String getModeString() {
  switch(currentMode) {
    case MODE_MANUAL: return "MANUAL";
    case MODE_AUTO_LINE_FOLLOW: return "AUTO_LINE_FOLLOW";
    case MODE_INSPECTION: return "INSPECTION";
    case MODE_OBSTACLE_AVOID: return "OBSTACLE_AVOID";
    case MODE_IDLE: return "IDLE";
    default: return "UNKNOWN";
  }
}

// =========================================================================
// 🎮 BLUETOOTH CONTROL
// =========================================================================

void handleBluetooth() {
  if (!SerialBT.available()) return;
  
  char cmd = SerialBT.read();
  if (cmd == '\n' || cmd == '\r' || cmd == ' ') return;
  
  Serial.println("[BT] Command: " + String(cmd));
  
  switch (cmd) {
    case 'F': case 'f': currentMode = MODE_MANUAL; moveForward(currentSpeed); break;
    case 'B': case 'b': currentMode = MODE_MANUAL; moveBackward(currentSpeed); break;
    case 'L': case 'l': currentMode = MODE_MANUAL; turnLeft(currentSpeed); break;
    case 'R': case 'r': currentMode = MODE_MANUAL; turnRight(currentSpeed); break;
    case 'S': case 's': stopMotors(); break;
    case 'A': case 'a': currentMode = MODE_AUTO_LINE_FOLLOW; SerialBT.println("Autonomous ON"); break;
    case 'M': case 'm': currentMode = MODE_MANUAL; stopMotors(); SerialBT.println("Manual ON"); break;
    case 'I': case 'i': if (currentMachine) currentMode = MODE_INSPECTION; break;
    case 'X': case 'x': stopMotors(); currentMode = MODE_IDLE; SerialBT.println("EMERGENCY STOP"); break;
    case '1': currentSpeed = SPEED_SLOW; SerialBT.println("Speed: SLOW"); break;
    case '2': currentSpeed = SPEED_MEDIUM; SerialBT.println("Speed: MEDIUM"); break;
    case '3': currentSpeed = SPEED_FAST; SerialBT.println("Speed: FAST"); break;
    default: SerialBT.println("Unknown command"); break;
  }
}

// =========================================================================
// 🔧 UTILITY FUNCTIONS
// =========================================================================

void blinkLED(int times) {
  for (int i = 0; i < times; i++) {
    digitalWrite(STATUS_LED, HIGH);
    delay(100);
    digitalWrite(STATUS_LED, LOW);
    delay(100);
  }
}

// Auto-reconnect WiFi every 15 s if the connection drops
void checkWiFi() {
  static unsigned long lastAttempt = 0;
  if (WiFi.status() == WL_CONNECTED) return;
  if (millis() - lastAttempt < 15000) return;
  lastAttempt = millis();
  Serial.println("[WIFI] Connection lost - reconnecting...");
  WiFi.disconnect();
  WiFi.begin(ssid, password);
}

void loadMachinesFromEEPROM() {
  for (int i = 0; i < 3; i++) {
    String uid = EEPROM.readString(i * 32);
    if (uid.length() > 0) {
      machines[i].uid = uid;
      Serial.println("[EEPROM] Loaded Machine " + String(i) + ": " + uid);
    }
  }
}

void setupOTA() {
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.onStart([]() { stopMotors(); });
  ArduinoOTA.begin();
  Serial.println("[OTA] Ready");
}

// =========================================================================
// ⚙️ SETUP
// =========================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n========================================");
  Serial.println(" 🤖 INDUSTRIAL ROBOT v4.0 FINAL");
  Serial.println("========================================");
  Serial.println(" Dashboard: " + String(DASHBOARD_URL));
  Serial.println("========================================");
  
  // Watchdog
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,
    .idle_core_mask = (1 << 0) | (1 << 1),
    .trigger_panic = true
  };
  esp_task_wdt_init(&wdt_config);
  esp_task_wdt_add(NULL);
  
  stats.startTime = millis();
  
  // EEPROM
  EEPROM.begin(512);
  loadMachinesFromEEPROM();
  
  // Pins
  pinMode(LEFT_IR, INPUT);
  pinMode(RIGHT_IR, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(STATUS_LED, OUTPUT);
  
  // Initialize
  setupMotorPWM();
  stopMotors();
  dht.begin();
  SPI.begin();
  rfid.PCD_Init();
  
  Serial.println("[INIT] Sensors initialized");
  
  // WiFi
  Serial.print("[WIFI] Connecting to " + String(ssid));
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
    esp_task_wdt_reset();
  }
  
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WIFI] Connected!");
    Serial.println("[WIFI] IP: " + WiFi.localIP().toString());
    Serial.println("[WIFI] Dashboard: " + String(DASHBOARD_URL));
    
    if (MDNS.begin(hostname)) {
      Serial.println("[mDNS] Started: http://" + String(hostname) + ".local");
    }
    
    setupOTA();
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  } else {
    Serial.println("[WIFI] Failed! Running offline");
  }
  
  // Bluetooth
  if (SerialBT.begin(BT_NAME)) {
    Serial.println("[BT] Started: " + String(BT_NAME));
  }
  
  // Startup
  blinkLED(5);
  tone(BUZZER_PIN, 1000, 100);
  delay(150);
  tone(BUZZER_PIN, 1500, 100);
  delay(150);
  tone(BUZZER_PIN, 2000, 100);
  
  Serial.println("\n========================================");
  Serial.println(" ✅ SYSTEM READY");
  Serial.println("========================================");
  Serial.println(" Commands: F/B/L/R/S - Movement");
  Serial.println("           A - Autonomous");
  Serial.println("           M - Manual");
  Serial.println("           1/2/3 - Speed");
  Serial.println("           I - Inspect");
  Serial.println("           X - Emergency Stop");
  Serial.println("========================================\n");
}

// =========================================================================
// 🔄 MAIN LOOP
// =========================================================================

void loop() {
  pwmTick();               // software PWM step (~1 ms) — keeps motors spinning
  esp_task_wdt_reset();
  ArduinoOTA.handle();
  checkWiFi();             // auto-reconnect if WiFi drops
  
  readAllSensors();        // rate-limited internally (500 ms)
  handleBluetooth();
  
  // RFID Detection (rate-limited to every 100 ms — loop now runs at ~1 kHz)
  static unsigned long lastRFIDCheck = 0;
  String uid = "";
  if (millis() - lastRFIDCheck > 100) {
    lastRFIDCheck = millis();
    uid = readRFID();
  }
  if (uid != "") {
    Serial.println("[RFID] Card: " + uid);
    
    int machineIndex = identifyMachine(uid);
    if (machineIndex == -1) {
      registerMachine(uid);
      machineIndex = identifyMachine(uid);
    }
    
    if (machineIndex >= 0 && machineIndex != lastDetectedMachineIndex) {
      currentMachine = &machines[machineIndex];
      lastDetectedMachineIndex = machineIndex;
      Serial.println("[SYSTEM] Identified: " + currentMachine->name);
      currentMode = MODE_INSPECTION;
    }
  }
  
  // Obstacle check
  if (currentMode == MODE_AUTO_LINE_FOLLOW && checkObstacle()) {
    currentMode = MODE_OBSTACLE_AVOID;
  }
  
  // Mode execution
  switch (currentMode) {
    case MODE_AUTO_LINE_FOLLOW:
      performPIDLineFollowing();
      smoothAccelerate();
      break;
      
    case MODE_OBSTACLE_AVOID:
      performObstacleAvoidance();
      break;
      
    case MODE_INSPECTION:
      if (currentMachine) {
        performInspection(currentMachine);
        currentMachine = nullptr;
        lastDetectedMachineIndex = -1;
      }
      currentMode = MODE_AUTO_LINE_FOLLOW;
      break;
      
    case MODE_MANUAL:
      smoothAccelerate();
      break;
      
    case MODE_IDLE:
      stopMotors();
      break;
  }
  
  sendLiveData();          // rate-limited internally (3 s)
}
