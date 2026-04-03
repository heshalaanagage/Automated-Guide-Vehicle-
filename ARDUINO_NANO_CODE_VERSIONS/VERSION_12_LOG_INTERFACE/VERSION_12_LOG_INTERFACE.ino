/************************************************************
 *  AGV MAIN CONTROL - VERSION 11 (LOCK + DIR HISTORY)
 *  ESP32 -> Nano : compact binary frames (NO CHANGE)
 *  Logs : JSON only (as you already use)
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
/*******************************************************/

// ================= ROUTE / MISSION SYSTEM =================
enum MissionMode { MODE_NONE, MODE_AUTO, MODE_MANUAL_TRIP, MODE_MANUAL_CHARGE };
MissionMode mission = MODE_NONE;

// ---- AUTO params ----
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

// ---- Patterns (8-bit) ----
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

// Direction hint (debug). Mission turns handled elsewhere.
String direction = "straight";

// ✅ Direction history (last non-straight)
volatile char lastDir = 'S';   // 'L' or 'R' or 'S'

// If bitSensor==0 (line lost) → move forward a little then U-turn.
int afterLostForwardMs = 400;   // tune if needed

// DEFAULT PID VALUES
int base_speed = 20;
int kp = 2;
int kd = 14;
int time_uturn = 2000;
int time_turn = 2000;
int time_strght_bfr_turn =3000;
int time_turn_execute = 2500;
int turnSpeed = 20;

// ================= EMERGENCY FLAGS =================
bool agv_running = false;
bool emergency_stop = false;
bool gripper_engaged = false;

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

  loopCount = 2;
  currentLoop = 0;
  waitingTime = 1;
  routeState = 0;

  tripDrop = 'A';
  tripLoopTarget = 0;
  tripLoopNow = 0;
  tripLeg = 0;

  chargeTimeMin = 0;
  chargeLeg = 0;

  T_detected = false;
  destination_detected = false;

  previous_error = 0;
  error = 0;

  // ✅ reset history
  lastDir = 'S';

  LOGC("STATE", "RST");
}

// ============================================================
//               COMPACT BINARY PROTOCOL (ESP32->NANO)
// ============================================================
enum {
  CMD_AUTO_START     = 1,   // loop(u16), waitMin(u16)
  CMD_MANUAL_TRIP    = 2,   // dropId(u8), waitMin(u16), loops(u16)
  CMD_MANUAL_CHARGE  = 3,   // chargeMin(u16)
  CMD_CHARGE_CANCEL  = 4,   // -
  CMD_EMG_STOP       = 10,
  CMD_EMG_START      = 11,
  CMD_GRIP_ENGAGE    = 12,
  CMD_GRIP_DISENGAGE = 13,
  CMD_PID_UPDATE     = 20,  // base(u16), kp(u16), kd(u16)
  CMD_PARAM_UPDATE   = 21,  // base,kp,kd,timeU,timeT,straightBefore,turnExecute,turnSpeed (all u16)
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


  // Full parameter update (kept compact + lightweight)
  if (cmd == CMD_PARAM_UPDATE && len == 16) {
    base_speed            = (int)rdU16LE(&pl[0]);
    kp                    = (int)rdU16LE(&pl[2]);
    kd                    = (int)rdU16LE(&pl[4]);
    time_uturn            = (int)rdU16LE(&pl[6]);
    time_turn             = (int)rdU16LE(&pl[8]);
    time_strght_bfr_turn  = (int)rdU16LE(&pl[10]);
    time_turn_execute     = (int)rdU16LE(&pl[12]);
    turnSpeed             = (int)rdU16LE(&pl[14]);
    LOGF("PAR", "B%ld K%ld D%ld U%ld T%ld",
     base_speed, kp, kd, time_uturn, time_turn);

    LOGF("PAR", "S%ld E%ld V%ld",
     time_strght_bfr_turn, time_turn_execute, turnSpeed);
    return;
  }

  if (emergency_stop) { LOGC("STATE", "IGN"); return; }

  if (cmd == CMD_AUTO_START && len == 4) {
    loopCount   = (int)rdU16LE(&pl[0]);
    waitingTime = (int)rdU16LE(&pl[2]);

    currentLoop = 0;
    routeState  = 0;

    mission = MODE_AUTO;
    agv_running = true;

    T_detected = false;
    destination_detected = false;

    LOGF("MODE", "A %ld %ld", loopCount, waitingTime);
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

    LOGF("MODE", "T %ld %ld %ld", (long)tripDrop, waitingTime, tripLoopTarget);
    return;
  }

  if (cmd == CMD_MANUAL_CHARGE && len == 2) {
    chargeTimeMin = (int)rdU16LE(&pl[0]);
    chargeLeg = 0;

    mission = MODE_MANUAL_CHARGE;
    agv_running = true;

    T_detected = false;
    destination_detected = false;

    LOGF("MODE", "C %ld", chargeTimeMin);
    return;
  }

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

// ================= UART RX STATE MACHINE =================
static inline uint8_t crcXor(const uint8_t* data, uint8_t n) {
  uint8_t c = 0;
  for (uint8_t i = 0; i < n; i++) c ^= data[i];
  return c;
}

static void rxProtocol() {
  while (Serial.available()) {
    uint8_t b = (uint8_t)Serial.read();

    switch (rxState) {
      case RX_WAIT:
        if (b == 0xA5) {
          rxCrc = b;
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
        if (rxLen == 0) rxState = RX_CRC;
        else rxState = RX_PAY;
        break;

      case RX_PAY:
        if (rxIdx < sizeof(rxPayload)) rxPayload[rxIdx] = b;
        rxCrc ^= b;
        rxIdx++;
        if (rxIdx >= rxLen) rxState = RX_CRC;
        break;

      case RX_CRC:
        if (rxCrc == b) {
          // valid frame
          applyCommand(rxCmd, rxPayload, rxLen);
        } else {
          LOGC("ERR", "CRC");
        }
        rxState = RX_WAIT;
        break;
    }
  }
}

// ================= Arduino setup/loop =================
void setup() {
  Serial.begin(115200);
  pinMode(EN_PIN, OUTPUT);
  digitalWrite(EN_PIN, HIGH);

  gripperServo.attach(SERVO_PIN);
  gripDisengage();

  // (your sensor + mux init stays in other tabs)
  LOGC("SYS", "RDY");
}

void loop() {
  rxProtocol();

  if (agv_running && !emergency_stop) {
    PID_Controller(base_speed, kp, kd);
    checkSpecialPatterns();
  } else {
    stopMotors();
  }
}