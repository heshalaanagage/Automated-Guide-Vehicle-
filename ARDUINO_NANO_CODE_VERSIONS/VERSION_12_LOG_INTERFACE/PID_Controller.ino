// ================= PID CONTROLLER =================

extern volatile char lastDir;  // from main file

void PID_Controller(int base_speed_in, int p, int d) {
  read_black_line();

  base_speed = base_speed_in;

  if (sumOnSensor == 0) {
    error = 0;
    derivative = 0;
    previous_error = 0;
    motor(base_speed_in, base_speed_in);
    return;
  }

  line_position = (float)sensorWight / (float)sumOnSensor;
  error = center_position - line_position;

  derivative = error - previous_error;

  int right_motor_correction = base_speed_in + (int)(error * p + derivative * d);
  int left_motor_correction  = base_speed_in - (int)(error * p + derivative * d);

  previous_error = error;

  motor(left_motor_correction, right_motor_correction);
}

// ============================================================
//  SPECIAL PATTERN HANDLER
//   - bitSensor == 0  => recover line (NO U-turn)
//   - T junction       => execute ONCE per segment (ignore until destination)
//   - destination      => handle, then unlock T for next segment
// ============================================================

static const unsigned long TURN_COOLDOWN_MS = 550;
static const unsigned long LINE_LOST_HOLD_MS = 1800;

static unsigned long lastTurnMs = 0;
static unsigned long lineLostSinceMs = 0;
static bool lineLostArmed = true;

// --- LINE RECOVERY TUNING ---
static const unsigned long LOST_STRAIGHT_MS = 2000;
static const int LOST_SPEED = 10;

static bool lineSeenNow() {
  return (sumOnSensor > 0);
}

static bool recoverStraight(unsigned long ms) {
  LOGC("REC", "S");  // one meaningful log (no spam)
  unsigned long t0 = millis();
  while (millis() - t0 < ms) {
    read_black_line();
    if (lineSeenNow()) return true;
    motor(LOST_SPEED, LOST_SPEED);
  }
  return false;
}

static void stopAndFail() {
  stopMotors();
  agv_running = false;
  mission = MODE_NONE;
  LOGC("STATE", "STOP");
}

void checkSpecialPatterns() {
  if (!agv_running || mission == MODE_NONE) return;

  static bool lostLogged = false;

  // ===================== 1) LINE LOST =====================
  if (bitSensor == 0) {

    if (!lostLogged) {
      LOGC("LOST", "1");  // one-shot
      lostLogged = true;
    }

    if (lineLostSinceMs == 0) lineLostSinceMs = millis();

    // hold a bit to avoid false triggers
    if (lineLostArmed && (millis() - lineLostSinceMs) >= LINE_LOST_HOLD_MS) {
      lineLostArmed = false;

      stopMotors();

      // A) Try straight first
      if (recoverStraight(LOST_STRAIGHT_MS)) {
        lineLostSinceMs = 0;
        lineLostArmed = true;
        lostLogged = false;
        return;
      }
      stopAndFail();
      return;
    }

    return;  // still within hold time
  }

  // ===================== regained line =====================
  lostLogged = false;
  lineLostSinceMs = 0;
  lineLostArmed = true;

  // cooldown after any turn to avoid double detection
  if ((millis() - lastTurnMs) < TURN_COOLDOWN_MS) return;

  // ===================== 2) DESTINATION =====================
  if (bitSensor == PATTERN_PICK || bitSensor == PATTERN_DROP || bitSensor == PATTERN_CHARGE) {
    destination_detected = true;
    motor(turnSpeed,turnSpeed);
    handleDestination();

    // allow ONE T-junction again for next segment
    T_detected = false;
    destination_detected = false;

    lastTurnMs = millis();
    return;
  }

  // ===================== 3) T JUNCTION / CROSS =====================
  if (bitSensor == PATTERN_T) {
    if (!T_detected) {
      LOGC("JUNC", "X");
      motor(turnSpeed,turnSpeed);
      delay(time_strght_bfr_turn);
      handleTJunction();
      T_detected = true;
      lastTurnMs = millis();
    }
    return;
  }
}