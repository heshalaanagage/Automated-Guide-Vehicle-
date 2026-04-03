/************************************************************
 *        AGV MAIN CONTROL CODE (EMERGENCY ENABLED)
 *        ESP32 → SERIAL → ARDUINO NANO
 *        BTS7960 + SERVO + GLOBAL PID UPDATE
 *
 *        FIXES:
 *        - Robust serial line parser
 *        - Ignore empty commands
 *        - CRLF safe
 *        - LOG tag safe fallback
 ************************************************************/

#include <Servo.h>

/**************  DEBUG LOG SYSTEM  **************/
#define LOG_ENABLED 1
#define LOG_DEFAULT_THROTTLE_MS 120

void LOG(const String &tagIn, const String &msgIn) {
#if LOG_ENABLED
  String tag = tagIn;
  String msg = msgIn;

  // tag.replace("\r", "");
  // tag.trim();
  // msg.replace("\r", "");
  // msg.trim();

  // if (tag.length() == 0) tag = "GEN";   // ✅ prevent {"tag":""}

  Serial.print("{\"t\":");
  Serial.print(millis());
  Serial.print(",\"tag\":\"");
  Serial.print(tag);
  Serial.print("\",\"msg\":\"");

  String safe = msg;
  safe.replace("\\", "/");
  safe.replace("\"", "'");
  Serial.print(safe);

  Serial.println("\"}");
#endif
}

void LOG_THROTTLED(const String &tag, const String &msg, unsigned long intervalMs = LOG_DEFAULT_THROTTLE_MS) {
#if LOG_ENABLED
  static unsigned long lastMs = 0;
  unsigned long now = millis();
  if (now - lastMs >= intervalMs) {
    lastMs = now;
    LOG(tag, msg);
  }
#endif
}
/*******************************************************/

// ================= ROUTE / MISSION SYSTEM =================
enum MissionMode { MODE_NONE, MODE_AUTO, MODE_MANUAL_TRIP, MODE_MANUAL_CHARGE };
MissionMode mission = MODE_NONE;

// ---- AUTO mode params ----
int loopCount = 2;
int currentLoop = 0;
int waitingTime = 1;
int routeState = 0;

// ---- MANUAL Trip params ----
char tripDrop = 'A';
int tripLoopTarget = 0;
int tripLoopNow = 0;
int tripLeg = 0;

// ---- MANUAL Charge params ----
int chargeTimeMin = 0;
int chargeLeg = 0;

// ---- Pattern flags ----
bool T_detected = false;
bool destination_detected = false;

// ---- Patterns ----
const uint8_t PATTERN_PICK   = 0b11000011;
const uint8_t PATTERN_DROP   = 0b00111100;
const uint8_t PATTERN_T      = 0b11111111;
const uint8_t PATTERN_CHARGE = 0b11100111;

// ================= BTS7960 MOTOR PINS =================
#define L_RPWM 6
#define L_LPWM 9
#define R_RPWM 3
#define R_LPWM 5
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

// DEFAULT PID VALUES
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

// ================= SERIAL PARSER BUFFER =================
static String rxLine = "";

// ================== PLACEHOLDERS (ඔයාගේ project එකේ already තියෙන functions) ==================
void PID_Controller(int base_speed, int p, int d);   // your existing
void checkSpecialPatterns();                         // your existing
void uTurn();                                        // your existing
void gripEngage();
void gripDisengage();
void stopMotors();

// ================= SETUP =================
void setup() {

  Serial.begin(9600);   // MUST match ESP32 Serial2 (9600)

  pinMode(L_RPWM, OUTPUT);
  pinMode(L_LPWM, OUTPUT);
  pinMode(R_RPWM, OUTPUT);
  pinMode(R_LPWM, OUTPUT);

  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, HIGH);

  gripperServo.attach(SERVO_PIN);
  gripperServo.write(0);

  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);

  Serial.println("🚀 AGV READY - GLOBAL PID SYSTEM");
  LOG("SYS", "AGV READY - GLOBAL PID SYSTEM");
}

// ================= MAIN LOOP =================
void loop() {

  checkEmergencyCommand();   // Always check serial

  if (emergency_stop) {
    stopMotors();
    return;
  }

  if (agv_running) {
    PID_Controller(base_speed, kp, kd);
    checkSpecialPatterns();
  } else {
    stopMotors();
  }
}

// ================= SERIAL COMMAND HANDLER (ROBUST) =================
void checkEmergencyCommand() {
  while (Serial.available()) {
    char c = (char)Serial.read();

    if (c == '\n') {
      rxLine.replace("\r", "");
      rxLine.trim();

      if (rxLine.length() == 0) {     // ✅ ignore blank line
        rxLine = "";
        continue;
      }

      // ✅ print only real commands
      Serial.print("📥 CMD: ");
      Serial.println(rxLine);
      LOG("CMD", rxLine);

      processCommand(rxLine);
      rxLine = "";
    } else {
      rxLine += c;

      // safety: too long -> reset
      if (rxLine.length() > 220) rxLine = "";
    }
  }
}

void processCommand(const String &cmd) {

  // ================= EMERGENCY =================
  if (cmd.startsWith("EMERGENCY")) {

    if (cmd.indexOf("AGV_START") > 0) {
      agv_running = true;
      emergency_stop = false;
      Serial.println("✅ AGV STARTED");
      LOG("STATE", "EMERGENCY AGV_START -> agv_running=1 emergency_stop=0");
      return;
    }

    if (cmd.indexOf("AGV_STOP") > 0) {
      agv_running = false;
      emergency_stop = true;
      stopMotors();
      Serial.println("🛑 AGV STOPPED");
      LOG("STATE", "EMERGENCY AGV_STOP -> agv_running=0 emergency_stop=1");
      return;
    }

    if (cmd.indexOf("GRIP_ENGAGE") > 0) {
      gripper_engaged = true;
      gripEngage();
      Serial.println("✋ GRIPPER ENGAGED");
      LOG("ACT", "GRIP_ENGAGE");
      return;
    }

    if (cmd.indexOf("GRIP_DISENGAGE") > 0) {
      gripper_engaged = false;
      gripDisengage();
      Serial.println("🤚 GRIPPER DISENGAGED");
      LOG("ACT", "GRIP_DISENGAGE");
      return;
    }

    // ================= PID UPDATE =================
    if (cmd.indexOf("UPDATE_PID") > 0) {

      int baseIndex = cmd.indexOf("BASE:");
      int kpIndex   = cmd.indexOf("KP:");
      int kdIndex   = cmd.indexOf("KD:");

      if (baseIndex > 0 && kpIndex > 0 && kdIndex > 0) {

        // safer parsing (handles commas/spaces)
        String baseStr = cmd.substring(baseIndex + 5, kpIndex);
        String kpStr   = cmd.substring(kpIndex + 3, kdIndex);
        String kdStr   = cmd.substring(kdIndex + 3);

        baseStr.replace(",", " "); baseStr.trim();
        kpStr.replace(",", " ");   kpStr.trim();
        kdStr.replace(",", " ");   kdStr.trim();

        base_speed = baseStr.toInt();
        kp = kpStr.toInt();
        kd = kdStr.toInt();

        Serial.println("⚙️ PID UPDATED GLOBALLY");
        Serial.print("Base Speed: "); Serial.println(base_speed);
        Serial.print("KP: "); Serial.println(kp);
        Serial.print("KD: "); Serial.println(kd);
        LOG("PID", "UPDATED base=" + String(base_speed) + " kp=" + String(kp) + " kd=" + String(kd));
      } else {
        LOG("PID", "UPDATE_PID parse fail");
      }
      return;
    }

    return;
  }

  // ================= AUTO MODE =================
  if (cmd.startsWith("AUTO")) {

    Serial.println("🤖 AUTO MODE RECEIVED");

    int loopIndex = cmd.indexOf("loop_count=");
    int waitIndex = cmd.indexOf("wait_time=");

    if (loopIndex > 0 && waitIndex > 0) {

      int comma = cmd.indexOf(",", loopIndex);
      if (comma < 0) comma = cmd.length();

      loopCount   = cmd.substring(loopIndex + 11, comma).toInt();
      waitingTime = cmd.substring(waitIndex + 10).toInt();

      LOG("MODE", "AUTO loopCount=" + String(loopCount) + " waitMin=" + String(waitingTime));

      currentLoop = 0;
      routeState  = 0;

      mission = MODE_AUTO;
      agv_running = true;
      emergency_stop = false;

      T_detected = false;
      destination_detected = false;
    } else {
      LOG("MODE", "AUTO parse fail");
    }
    return;
  }

  // ================= MANUAL MODE =================
  if (cmd.startsWith("MANUAL")) {

    Serial.println("🕹 MANUAL MODE RECEIVED");

    int typeIdx = cmd.indexOf("TYPE:");
    if (typeIdx < 0) return;

    int commaAfterType = cmd.indexOf(',', typeIdx);
    String typeToken = (commaAfterType > 0) ? cmd.substring(typeIdx + 5, commaAfterType) : cmd.substring(typeIdx + 5);
    typeToken.trim();

    // ========= CHARGE CANCEL =========
    if (typeToken == "CHARGE_CANCEL") {

      if (mission == MODE_MANUAL_CHARGE) {
        chargeLeg = 1;
        uTurn();

        destination_detected = true;
        T_detected = false;

        agv_running = true;
        emergency_stop = false;

        Serial.println("↩️ CHARGE CANCEL → RETURN PICK");
        LOG("MODE", "CHARGE_CANCEL -> chargeLeg=1 returnPick");
      }
      return;
    }

    // -------- CHARGE --------
    if (typeToken == "Charge") {

      int cIdx = cmd.indexOf("CTIME:");
      chargeTimeMin = (cIdx > 0) ? cmd.substring(cIdx + 6).toInt() : 1;

      LOG("MODE", "MANUAL_CHARGE chargeTimeMin=" + String(chargeTimeMin));

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

      LOG("MODE", "MANUAL_TRIP drop=" + String(tripDrop) + " waitMin=" + String(waitingTime) + " loops=" + String(tripLoopTarget));

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
void gripEngage() { gripperServo.write(180); }
void gripDisengage() { gripperServo.write(0); }