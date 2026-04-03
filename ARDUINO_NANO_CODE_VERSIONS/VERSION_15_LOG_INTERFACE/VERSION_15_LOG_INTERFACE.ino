/************************************************************
 *  VERSION_13_LOG_INTERFACE.ino  (UPDATED: firstStartPending)
 *  + NEW: Obstacle pause via 1-byte command from ESP32
 *         0xF1 = STOP (obstacle)
 *         0xF0 = RUN  (clear)
 *  - No other logic changed
 ************************************************************/

#include <Servo.h>

/**************  LOG SYSTEM (JSON)  **************/
#define LOG_ENABLED 1

void LOG(const String &tagIn, const String &msgIn) {
#if LOG_ENABLED
  String tag = tagIn.length() ? tagIn : "GEN";
  Serial.print("{\"t\":"); Serial.print(millis());
  Serial.print(",\"tag\":\""); Serial.print(tag);
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
void LOGC(const char* tag, const char* msg) {
#if LOG_ENABLED
  Serial.print("{\"t\":"); Serial.print(millis());
  Serial.print(",\"tag\":\""); Serial.print(tag);
  Serial.print("\",\"msg\":\""); Serial.print(msg);
  Serial.println("\"}");
#endif
}

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
#define L_LPWM 11
#define R_RPWM 3
#define R_LPWM 5
#define EN_PIN 2

// ================= SERVO =================
#define SERVO_PIN 12
Servo gripperServo;

// ================= MUX PINS =================
#define S0 13
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
String direction = "straight";

// ✅ last non-straight direction
volatile char lastDir = 'S';

// PID params
int delay_before_turn = 100;
int base_speed = 25;
int kp = 2;
int kd = 14;
#define turnSpeed 25

// ================= EMERGENCY FLAGS =================
bool agv_running = false;
bool emergency_stop = false;
bool gripper_engaged = false;

// ✅ FIRST START flag (loopCount/tripLoopNow = 0 case)
bool firstStartPending = false;

// ✅ NEW: obstacle pause flag (from ESP32 1-byte command)
volatile bool obstacleStop = false;

// ================== FUNCTIONS FROM OTHER TABS ==================
void PID_Controller(int base_speed, int p, int d);
void checkSpecialPatterns();
void uTurn();
void handleTJunction();
void handleDestination();
void read_black_line();
void motor(int LPWM, int RPWM);

// ================= MOTOR STOP / GRIPPER =================
void stopMotors() { motor(0, 0); }

void gripEngage() {
  gripper_engaged = true;
  gripperServo.write(150);
  LOGC("GRIP", "1");
}
void gripDisengage() {
  gripper_engaged = false;
  gripperServo.write(0);
  LOGC("GRIP", "0");
}

// ================= RESET ALL =================
void resetAll() {
  stopMotors();
  gripDisengage();

  mission = MODE_NONE;
  agv_running = false;
  emergency_stop = false;

  loopCount = 2; currentLoop = 0; waitingTime = 1; routeState = 0;
  tripDrop = 'A'; tripLoopTarget = 0; tripLoopNow = 0; tripLeg = 0;
  chargeTimeMin = 0; chargeLeg = 0;

  T_detected = false;
  destination_detected = false;

  previous_error = 0;
  error = 0;
  lastDir = 'S';

  firstStartPending = false;
  obstacleStop = false;

  LOGC("STATE", "RST");
}

// ============================================================
//               COMPACT BINARY PROTOCOL (ESP32->NANO)
// ============================================================
enum {
  CMD_AUTO_START     = 1,
  CMD_MANUAL_TRIP    = 2,
  CMD_MANUAL_CHARGE  = 3,
  CMD_CHARGE_CANCEL  = 4,
  CMD_EMG_STOP       = 10,
  CMD_EMG_START      = 11,
  CMD_GRIP_ENGAGE    = 12,
  CMD_GRIP_DISENGAGE = 13,
  CMD_PID_UPDATE     = 20,
  CMD_RESET_ALL      = 30
};

static inline uint16_t rdU16LE(const uint8_t* p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static inline char dropIdToChar(uint8_t id) {
  if (id == 2) return 'B';
  if (id == 3) return 'C';
  return 'A';
}

enum RxState : uint8_t { RX_WAIT, RX_CMD, RX_LEN, RX_PAY, RX_CRC };
RxState rxState = RX_WAIT;
uint8_t rxCmd = 0, rxLen = 0, rxIdx = 0;
uint8_t rxPayload[16];
uint8_t rxCrc = 0;

static void applyCommand(uint8_t cmd, const uint8_t* pl, uint8_t len) {

  if (cmd == CMD_RESET_ALL) { resetAll(); return; }

  if (cmd == CMD_EMG_STOP) {
    agv_running = false;
    emergency_stop = true;
    stopMotors();
    LOGC("STATE", "E1");
    return;
  }
  if (cmd == CMD_EMG_START) {
    emergency_stop = false;
    agv_running = (mission != MODE_NONE);
    LOGC("STATE", "E0");
    return;
  }

  if (cmd == CMD_GRIP_ENGAGE)    { gripEngage(); return; }
  if (cmd == CMD_GRIP_DISENGAGE) { gripDisengage(); return; }

  if (cmd == CMD_PID_UPDATE && len == 6) {
    base_speed = (int)rdU16LE(&pl[0]);
    kp         = (int)rdU16LE(&pl[2]);
    kd         = (int)rdU16LE(&pl[4]);
    LOGF("PID", "UPD %ld %ld %ld", base_speed, kp, kd);
    return;
  }

  if (emergency_stop) { LOGC("STATE", "IGN"); return; }

  // ---------- AUTO START ----------
  if (cmd == CMD_AUTO_START && len == 4) {
    loopCount   = (int)rdU16LE(&pl[0]);
    waitingTime = (int)rdU16LE(&pl[2]);

    currentLoop = 0;
    routeState  = 0;

    mission = MODE_AUTO;
    agv_running = true;

    firstStartPending = true;

    T_detected = false;
    destination_detected = false;

    LOGF("MODE", "A %ld %ld", loopCount, waitingTime);
    return;
  }

  // ---------- MANUAL TRIP ----------
  if (cmd == CMD_MANUAL_TRIP && len == 5) {
    tripDrop       = dropIdToChar(pl[0]);
    waitingTime    = (int)rdU16LE(&pl[1]);
    tripLoopTarget = (int)rdU16LE(&pl[3]);

    tripLoopNow = 0;
    tripLeg = 0;

    mission = MODE_MANUAL_TRIP;
    agv_running = true;

    firstStartPending = true;

    T_detected = false;
    destination_detected = false;

    LOGF("MODE", "T %ld %ld %ld", (long)tripDrop, waitingTime, tripLoopTarget);
    return;
  }

  // ---------- MANUAL CHARGE ----------
  if (cmd == CMD_MANUAL_CHARGE && len == 2) {
    chargeTimeMin = (int)rdU16LE(&pl[0]);
    chargeLeg = 0;

    mission = MODE_MANUAL_CHARGE;
    agv_running = true;

    firstStartPending = false;

    T_detected = false;
    destination_detected = false;

    LOGF("MODE", "C %ld", chargeTimeMin);
    return;
  }

  // ---------- CHARGE CANCEL ----------
  if (cmd == CMD_CHARGE_CANCEL && len == 0) {
    if (mission == MODE_MANUAL_CHARGE) {
      chargeLeg = 1;
      uTurn();
      destination_detected = true;
      T_detected = false;
      agv_running = true;
      LOGC("MODE", "CC");
    }
    return;
  }

  LOGC("ERR", "CMD?");
}

void checkEmergencyCommand() {
  while (Serial.available()) {
    uint8_t b = (uint8_t)Serial.read();

    // ✅ NEW: ultra-short 1-byte obstacle commands (no frame)
    // 0xF1 = obstacle stop, 0xF0 = clear
    if (rxState == RX_WAIT) {
      if (b == 0xF1) { obstacleStop = true;  stopMotors(); continue; }
      if (b == 0xF0) { obstacleStop = false; continue; }
    }

    switch (rxState) {
      case RX_WAIT:
        if (b == 0xA5) { rxCrc = 0xA5; rxState = RX_CMD; }
        break;
      case RX_CMD:
        rxCmd = b; rxCrc ^= b; rxState = RX_LEN;
        break;
      case RX_LEN:
        rxLen = b; rxCrc ^= b; rxIdx = 0;
        if (rxLen > sizeof(rxPayload)) rxState = RX_WAIT;
        else if (rxLen == 0) rxState = RX_CRC;
        else rxState = RX_PAY;
        break;
      case RX_PAY:
        rxPayload[rxIdx++] = b; rxCrc ^= b;
        if (rxIdx >= rxLen) rxState = RX_CRC;
        break;
      case RX_CRC:
        if (b == rxCrc) applyCommand(rxCmd, rxPayload, rxLen);
        else LOGC("ERR", "CRC");
        rxState = RX_WAIT;
        break;
    }
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

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

  LOGC("SYS", "V13");
}

// ================= MAIN LOOP =================
void loop() {
  checkEmergencyCommand();

  if (emergency_stop) {
    stopMotors();
    return;
  }

  // ✅ NEW: if obstacleStop is active, pause motion (mission state kept)
  if (obstacleStop) {
    stopMotors();
    return;
  }

  if (agv_running && mission != MODE_NONE) {

    if (firstStartPending && (mission == MODE_AUTO || mission == MODE_MANUAL_TRIP)) {
      firstStartPending = false;

      gripDisengage();
      delay((unsigned long)waitingTime * 10000UL);
      gripEngage();

      LOGC("ACT", "FS");
    }

    PID_Controller(base_speed, kp, kd);
    checkSpecialPatterns();

  } else {
    stopMotors();
  }
}