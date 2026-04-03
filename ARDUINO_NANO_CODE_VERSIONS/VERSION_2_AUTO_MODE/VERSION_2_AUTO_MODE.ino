/************************************************************
 *        AGV MAIN CONTROL CODE (EMERGENCY ENABLED)
 *        ESP32 → SERIAL → ARDUINO
 *        BTS7960 + SERVO + GLOBAL PID UPDATE
 ************************************************************/

#include <Servo.h>

// ================= AUTO MODE SYSTEM =================
int loopCount = 2;      // ESP32 eken passe set karanna puluwan passe
int currentLoop = 0;
int waitingTime = 1;    // minutes

int routeState = 0;
/*
0 = PICK → A
1 = A → PICK
2 = PICK → B
3 = B → PICK
*/

bool T_detected = false;
bool destination_detected = false;


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
    PID_Controller(base_speed, kp, kd);
    checkSpecialPatterns();   // 🔥 ADD THIS LINE ONLY
  }else {
    stopMotors();
  }
}

// ================= SERIAL COMMAND HANDLER =================
void checkEmergencyCommand() {

  if (Serial.available()) {

    String cmd = Serial.readStringUntil('\n');
    cmd.trim();

    Serial.print("📥 CMD: ");
    Serial.println(cmd);

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
    }
    // ================= AUTO MODE RECEIVE =================
else if (cmd.startsWith("AUTO")) {

  Serial.println("🤖 AUTO MODE RECEIVED");

  int loopIndex = cmd.indexOf("loop_count=");
  int waitIndex = cmd.indexOf("wait_time=");

  if (loopIndex > 0 && waitIndex > 0) {

    loopCount = cmd.substring(loopIndex + 11, cmd.indexOf(",", loopIndex)).toInt();
    waitingTime = cmd.substring(waitIndex + 10).toInt();

    Serial.print("Loop Count: ");
    Serial.println(loopCount);

    Serial.print("Waiting Time (min): ");
    Serial.println(waitingTime);

    currentLoop = 0;
    routeState = 0;

    agv_running = true;
    emergency_stop = false;
  }
}

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
