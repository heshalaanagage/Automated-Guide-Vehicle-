/************************************************************
 *        AGV MAIN CONTROL CODE (EMERGENCY ENABLED)
 *        ESP32 → SERIAL → ARDUINO
 *        BTS7960 + SERVO + GLOBAL PID UPDATE
 ************************************************************/

#include <Servo.h>

// ================= ROUTE / MISSION SYSTEM =================
// Mission Modes
enum MissionMode { MODE_NONE, MODE_AUTO, MODE_MANUAL_TRIP, MODE_MANUAL_CHARGE };
MissionMode mission = MODE_NONE;

// ---- AUTO mode params ----
int loopCount = 2;      // received from ESP32
int currentLoop = 0;
int waitingTime = 1;    // minutes
int routeState = 0;     // 0:PICK→A, 1:A→PICK, 2:PICK→B, 3:B→PICK

// ---- MANUAL Trip params ----
char tripDrop = 'A';    // 'A' or 'B'
int tripLoopTarget = 0;
int tripLoopNow = 0;
int tripLeg = 0;        // 0:PICK→DROP, 1:DROP→PICK

// ---- MANUAL Charge params ----
int chargeTimeMin = 0;
int chargeLeg = 0;      // 0:PICK→CHARGE, 1:CHARGE→PICK

// ---- Pattern flags ----
bool T_detected = false;
bool destination_detected = false;

// ---- Patterns ----
const uint8_t PATTERN_PICK   = 0b11000011;
const uint8_t PATTERN_DROP   = 0b00111100; // used for Drop A and Drop B
const uint8_t PATTERN_T      = 0b11111111;
const uint8_t PATTERN_CHARGE = 0b11100111;


// ================= BTS7960 MOTOR PINS =================

// LEFT MOTOR
#define L_RPWM 6//6
#define L_LPWM 9//9

// RIGHT MOTOR
#define R_RPWM 3//3
#define R_LPWM 5//5

// Shared ENABLE pin
#define EN_PIN 2

// ================= SERVO =================
#define SERVO_PIN 12
Servo gripperServo;

// ================= MUX PINS =================
#define S0 11
#define S1 10
#define S2 8
#define S3 7
#define SIG_PIN 4

// ================= SENSOR VARIABLES =================
#define sensorNumber 16
int sensorADC[sensorNumber];
int sensorDigital[sensorNumber];
int bitWeight[sensorNumber]   = {1,2,4,8,16,32,64,128};
int WeightValue[sensorNumber] = {10,20,30,40,50,60,70,80};

int theshold = 0;
int sumOnSensor;
int sensorWight;
int bitSensor;

// ================= PID VARIABLES =================
float line_position;
float error;
float center_position = 45;
float derivative, previous_error;

// 🔥 DEFAULT PID VALUES
int base_speed = 60;
int kp = 2;
int kd = 12;

String direction = "straight";
#define delay_before_turn 100 
#define turnSpeed 80

// ================= EMERGENCY FLAGS =================
bool agv_running = false;
bool emergency_stop = false;
bool gripper_engaged = false;

// ================= SETUP =================
void setup() {

  Serial.begin(9600);

  // Motor pins
  pinMode(L_RPWM, OUTPUT);
  pinMode(L_LPWM, OUTPUT);
  pinMode(R_RPWM, OUTPUT);
  pinMode(R_LPWM, OUTPUT);

  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, HIGH);   // Enable BTS7960

  // Servo setup
  gripperServo.attach(SERVO_PIN);
  gripperServo.write(0);   // Default open

  // MUX
  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);

  Serial.println("🚀 AGV READY - GLOBAL PID SYSTEM");
}

// ================= MAIN LOOP =================
void loop() {

  checkEmergencyCommand();   // Always check serial

  if (emergency_stop) {
    stopMotors();
    return;
  }

  if (agv_running) {
    PID_Controller(base_speed, kp, kd);   // 🔥 GLOBAL PID USED
    checkSpecialPatterns();               // pattern routing
  } else {
    stopMotors();
  }
}

// ================= SERIAL COMMAND HANDLER =================
void checkEmergencyCommand() {

  if (!Serial.available()) return;

  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  Serial.print("📥 CMD: ");
  Serial.println(cmd);

  // ================= EMERGENCY MODE (UNCHANGED LOGIC) =================
  if (cmd.startsWith("EMERGENCY")) {

    if (cmd.indexOf("AGV_START") > 0) {
      agv_running = true;
      emergency_stop = false;
      Serial.println("✅ AGV STARTED");
    }

    else if (cmd.indexOf("AGV_STOP") > 0) {
      agv_running = false;
      emergency_stop = true;
      stopMotors();
      Serial.println("🛑 AGV STOPPED");
    }

    else if (cmd.indexOf("GRIP_ENGAGE") > 0) {
      gripper_engaged = true;
      gripEngage();
      Serial.println("✋ GRIPPER ENGAGED");
    }

    else if (cmd.indexOf("GRIP_DISENGAGE") > 0) {
      gripper_engaged = false;
      gripDisengage();
      Serial.println("🤚 GRIPPER DISENGAGED");
    }

    // ================= PID UPDATE =================
    else if (cmd.indexOf("UPDATE_PID") > 0) {

      int baseIndex = cmd.indexOf("BASE:");
      int kpIndex   = cmd.indexOf("KP:");
      int kdIndex   = cmd.indexOf("KD:");

      if (baseIndex > 0 && kpIndex > 0 && kdIndex > 0) {

        base_speed = cmd.substring(baseIndex + 5, kpIndex - 1).toInt();
        kp         = cmd.substring(kpIndex + 3, kdIndex - 1).toInt();
        kd         = cmd.substring(kdIndex + 3).toInt();

        Serial.println("⚙️ PID UPDATED GLOBALLY");
        Serial.print("Base Speed: "); Serial.println(base_speed);
        Serial.print("KP: "); Serial.println(kp);
        Serial.print("KD: "); Serial.println(kd);
      }
    }

    return; // emergency handled
  }

  // ================= AUTO MODE RECEIVE =================
  if (cmd.startsWith("AUTO")) {

    Serial.println("🤖 AUTO MODE RECEIVED");

    int loopIndex = cmd.indexOf("loop_count=");
    int waitIndex = cmd.indexOf("wait_time=");

    if (loopIndex > 0 && waitIndex > 0) {

      loopCount   = cmd.substring(loopIndex + 11, cmd.indexOf(",", loopIndex)).toInt();
      waitingTime = cmd.substring(waitIndex + 10).toInt();

      Serial.print("Loop Count: ");
      Serial.println(loopCount);

      Serial.print("Waiting Time (min): ");
      Serial.println(waitingTime);

      currentLoop = 0;
      routeState  = 0;

      mission = MODE_AUTO;
      agv_running = true;
      emergency_stop = false;

      T_detected = false;
      destination_detected = false;
    }
    return;
  }

  // ================= MANUAL MODE RECEIVE =================
  if (cmd.startsWith("MANUAL")) {

    Serial.println("🕹 MANUAL MODE RECEIVED");

    int typeIdx = cmd.indexOf("TYPE:");
    if (typeIdx < 0) return;

    int commaAfterType = cmd.indexOf(',', typeIdx);
    String typeToken = (commaAfterType > 0) ? cmd.substring(typeIdx + 5, commaAfterType) : cmd.substring(typeIdx + 5);
    typeToken.trim();

    // -------- CHARGE --------
    if (typeToken == "Charge") {

      int cIdx = cmd.indexOf("CTIME:");
      chargeTimeMin = (cIdx > 0) ? cmd.substring(cIdx + 6).toInt() : 1;

      Serial.print("Charge Time (min): ");
      Serial.println(chargeTimeMin);

      mission = MODE_MANUAL_CHARGE;
      chargeLeg = 0;

      T_detected = false;
      destination_detected = false;

      agv_running = true;
      emergency_stop = false;
      return;
    }

    // -------- TRIP --------
    if (typeToken == "Trip") {

      int dIdx = cmd.indexOf("DROP:");
      int wIdx = cmd.indexOf("WAIT:");
      int lIdx = cmd.indexOf("LOOP:");

      if (dIdx > 0) {
        int dEnd = cmd.indexOf(',', dIdx);
        if (dEnd < 0) dEnd = cmd.length();
        tripDrop = cmd.substring(dIdx + 5, dEnd).charAt(0);
      }

      if (wIdx > 0) {
        int wEnd = cmd.indexOf(',', wIdx);
        if (wEnd < 0) wEnd = cmd.length();
        waitingTime = cmd.substring(wIdx + 5, wEnd).toInt();
      }

      if (lIdx > 0) {
        tripLoopTarget = cmd.substring(lIdx + 5).toInt();
      }

      Serial.print("Trip DROP: "); Serial.println(tripDrop);
      Serial.print("Trip WAIT (min): "); Serial.println(waitingTime);
      Serial.print("Trip LOOP: "); Serial.println(tripLoopTarget);

      mission = MODE_MANUAL_TRIP;
      tripLoopNow = 0;
      tripLeg = 0;

      T_detected = false;
      destination_detected = false;

      agv_running = true;
      emergency_stop = false;
      return;
    }

    return;
  }
}

// ================= MOTOR STOP =================
void stopMotors() {
  analogWrite(L_RPWM, 0);
  analogWrite(L_LPWM, 0);
  analogWrite(R_RPWM, 0);
  analogWrite(R_LPWM, 0);
}

// ================= GRIPPER SERVO =================
void gripEngage() {
  gripperServo.write(180);
}

void gripDisengage() {
  gripperServo.write(0);
}

/************************************************************
 * NOTE:
 * - PID_Controller()
 * - Sensor reading functions
 * MUST already exist in your project
 ************************************************************/
