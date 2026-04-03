/************************************************************
 *  AGV MAIN CONTROL - COMPACT PROTOCOL (NANO)
 *  ESP32 -> Nano : binary frames (no long strings)
 *  Keeps your existing PID_Controller.ino + Turns.ino logic
 *
 *  IMPORTANT:
 *  - This file MUST NOT define uTurn() or checkSpecialPatterns()
 *    because they are already in Turns.ino and PID_Controller.ino
 ************************************************************/

#include <Servo.h>

/**************  DEBUG LOG SYSTEM  **************/
#define LOG_ENABLED 1
#define LOG_DEFAULT_THROTTLE_MS 120

void LOG(const String &tagIn, const String &msgIn) {
#if LOG_ENABLED
  String tag = tagIn;
  if (tag.length() == 0) tag = "GEN";

  Serial.print("{\"t\":");
  Serial.print(millis());
  Serial.print(",\"tag\":\"");
  Serial.print(tag);
  Serial.print("\",\"msg\":\"");

  String safe = msgIn;
  safe.replace("\\", "/");
  safe.replace("\"", "'");
  Serial.print(safe);

  Serial.println("\"}");
#endif
}

void LOGF(const char* tag, const char* fmt, long a=0, long b=0, long c=0, long d=0, long e=0) {
#if LOG_ENABLED
  char buf[120];
  snprintf(buf, sizeof(buf), fmt, a, b, c, d, e);

  Serial.print("{\"t\":"); Serial.print(millis());
  Serial.print(",\"tag\":\""); Serial.print(tag);
  Serial.print("\",\"msg\":\""); Serial.print(buf);
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
int waitingTime = 1;      // minutes
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

// ================== FUNCTIONS FROM OTHER TABS ==================
void PID_Controller(int base_speed, int p, int d);  // PID_Controller.ino
void checkSpecialPatterns();                        // PID_Controller.ino
void uTurn();                                       // Turns.ino
void handleTJunction();                             // Turns.ino
void handleDestination();                           // Turns.ino
void read_black_line();                             // read_sensor.ino
void motor(int LPWM, int RPWM);                     // motor.ino

// ================= MOTOR STOP / GRIPPER =================
void stopMotors() { motor(0, 0); }

void gripEngage() { gripperServo.write(180); }
void gripDisengage() { gripperServo.write(0); }

// ============================================================
//               COMPACT BINARY PROTOCOL (ESP32->NANO)
// ============================================================
// Frame: [0xA5][CMD][LEN][PAYLOAD...][CRC_XOR]
enum {
  CMD_AUTO_START     = 1,   // payload: loop(u16), waitMin(u16)
  CMD_MANUAL_TRIP    = 2,   // payload: dropId(u8), waitMin(u16), loops(u16)
  CMD_MANUAL_CHARGE  = 3,   // payload: chargeMin(u16)
  CMD_CHARGE_CANCEL  = 4,   // no payload
  CMD_EMG_STOP       = 10,  // no payload
  CMD_EMG_START      = 11,  // no payload
  CMD_GRIP_ENGAGE    = 12,  // no payload
  CMD_GRIP_DISENGAGE = 13,  // no payload
  CMD_PID_UPDATE     = 20   // payload: base(u16), kp(u16), kd(u16)
};

static inline uint16_t rdU16LE(const uint8_t* p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static inline char dropIdToChar(uint8_t id) {
  if (id == 2) return 'B';
  if (id == 3) return 'C';
  return 'A';
}

// Receiver state machine
enum RxState : uint8_t { RX_WAIT, RX_CMD, RX_LEN, RX_PAY, RX_CRC };
RxState rxState = RX_WAIT;
uint8_t rxCmd = 0, rxLen = 0, rxIdx = 0;
uint8_t rxPayload[16];
uint8_t rxCrc = 0;

static void applyCommand(uint8_t cmd, const uint8_t* pl, uint8_t len) {

  // ---- emergency always works ----
  if (cmd == CMD_EMG_STOP) {
    agv_running = false;
    emergency_stop = true;
    stopMotors();
    LOG("STATE", "EMG_STOP");
    return;
  }

  if (cmd == CMD_EMG_START) {
    emergency_stop = false;
    agv_running = true;
    LOG("STATE", "EMG_START");
    return;
  }

  if (cmd == CMD_GRIP_ENGAGE) {
    gripper_engaged = true;
    gripEngage();
    LOG("ACT", "GRIP_ENGAGE");
    return;
  }

  if (cmd == CMD_GRIP_DISENGAGE) {
    gripper_engaged = false;
    gripDisengage();
    LOG("ACT", "GRIP_DISENGAGE");
    return;
  }

  if (cmd == CMD_PID_UPDATE && len == 6) {
    base_speed = (int)rdU16LE(&pl[0]);
    kp         = (int)rdU16LE(&pl[2]);
    kd         = (int)rdU16LE(&pl[4]);
    LOGF("PID", "UPDATED base=%ld kp=%ld kd=%ld", base_speed, kp, kd);
    return;
  }

  // if stopped by emergency, ignore mission starts
  if (emergency_stop) {
    LOG("STATE", "IGNORED: emergency_stop=1");
    return;
  }

  // ---- missions ----
  if (cmd == CMD_AUTO_START && len == 4) {
    loopCount   = (int)rdU16LE(&pl[0]);
    waitingTime = (int)rdU16LE(&pl[2]);

    currentLoop = 0;
    routeState  = 0;

    mission = MODE_AUTO;
    agv_running = true;

    T_detected = false;
    destination_detected = false;

    LOGF("MODE", "AUTO loop=%ld wait=%ld", loopCount, waitingTime);
    return;
  }

  if (cmd == CMD_MANUAL_TRIP && len == 5) {
    tripDrop       = dropIdToChar(pl[0]);
    waitingTime    = (int)rdU16LE(&pl[1]);
    tripLoopTarget = (int)rdU16LE(&pl[3]);

    tripLoopNow = 0;
    tripLeg = 0;

    mission = MODE_MANUAL_TRIP;
    agv_running = true;

    T_detected = false;
    destination_detected = false;

    LOGF("MODE", "TRIP drop=%ld wait=%ld loops=%ld", (long)tripDrop, waitingTime, tripLoopTarget);
    return;
  }

  if (cmd == CMD_MANUAL_CHARGE && len == 2) {
    chargeTimeMin = (int)rdU16LE(&pl[0]);
    chargeLeg = 0;

    mission = MODE_MANUAL_CHARGE;
    agv_running = true;

    T_detected = false;
    destination_detected = false;

    LOGF("MODE", "CHARGE time=%ld", chargeTimeMin);
    return;
  }

  if (cmd == CMD_CHARGE_CANCEL && len == 0) {
    if (mission == MODE_MANUAL_CHARGE) {
      chargeLeg = 1;
      // keep your existing behavior
      uTurn();
      destination_detected = true;
      T_detected = false;
      agv_running = true;
      LOG("MODE", "CHARGE_CANCEL -> return pick");
    }
    return;
  }

  LOGF("ERR", "Unknown cmd=%ld len=%ld", cmd, len);
}

// This function name is kept because your Turns.ino calls it
void checkEmergencyCommand() {
  while (Serial.available()) {
    uint8_t b = (uint8_t)Serial.read();

    switch (rxState) {
      case RX_WAIT:
        if (b == 0xA5) {
          rxCrc = 0xA5;
          rxState = RX_CMD;
        }
        break;

      case RX_CMD:
        rxCmd = b;
        rxCrc ^= b;
        rxState = RX_LEN;
        break;

      case RX_LEN:
        rxLen = b;
        rxCrc ^= b;
        rxIdx = 0;
        if (rxLen > sizeof(rxPayload)) {
          rxState = RX_WAIT;
        } else if (rxLen == 0) {
          rxState = RX_CRC;
        } else {
          rxState = RX_PAY;
        }
        break;

      case RX_PAY:
        rxPayload[rxIdx++] = b;
        rxCrc ^= b;
        if (rxIdx >= rxLen) rxState = RX_CRC;
        break;

      case RX_CRC:
        if (b == rxCrc) {
          applyCommand(rxCmd, rxPayload, rxLen);
        } else {
          LOG("ERR", "CRC_FAIL");
        }
        rxState = RX_WAIT;
        break;
    }
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);  // MUST match ESP32 Serial2 baud

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
  pinMode(SIG_PIN, INPUT);

  LOG("SYS", "AGV READY - COMPACT PROTOCOL");
}

// ================= MAIN LOOP =================
void loop() {

  checkEmergencyCommand();   // keep receiving always

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