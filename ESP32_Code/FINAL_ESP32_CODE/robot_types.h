#ifndef ROBOT_TYPES_H
#define ROBOT_TYPES_H

// Operating Modes
enum RobotMode {
  MODE_IDLE,
  MODE_MANUAL,
  MODE_AUTO_LINE_FOLLOW,
  MODE_INSPECTION,
  MODE_OBSTACLE_AVOID,
  MODE_LOW_BATTERY
};

// Speed Profiles (software-PWM duty %, 100 = FULL SPEED)
enum SpeedProfile {
  SPEED_SLOW = 50,
  SPEED_MEDIUM = 75,
  SPEED_FAST = 100
};

// Sensor Data Structure
struct SensorData {
  float temperature;
  float humidity;
  int gasLevel;
  float distance;
  int leftIR;
  int rightIR;
  float batteryVoltage;
  float batteryPercent;
  unsigned long timestamp;
} currentReading;

// Machine Data Structure
struct Machine {
  String uid;
  String name;
  String location;
  int inspectionCount;
  unsigned long lastInspection;
  float avgTemperature;
  float avgGasLevel;
  bool hasAlert;
  String lastImagePath;
  float maxTemp;
  float minTemp;
  int maxGas;
};

// PID Controller
struct PIDController {
  float kp = 25.0;
  float ki = 0.0;
  float kd = 15.0;
  float lastError = 0;
  float integral = 0;
  float output = 0;
} pidController;

// Battery Configuration
struct BatteryConfig {
  float maxVoltage = 8.4;
  float minVoltage = 6.4;
  float lowBatteryThreshold = 6.8;
  float criticalBatteryThreshold = 6.5;
  float voltageDividerRatio = 2.0;
} batteryConfig;

// System Statistics
struct SystemStats {
  unsigned long totalDistance = 0;
  unsigned long startTime = 0;
  int obstaclesDetected = 0;
  int lineDeviations = 0;
  int alertsTriggered = 0;
  int inspectionsCompleted = 0;
} stats;

#endif