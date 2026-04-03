// ===== PATH LOG =====
static int lastPath = 0;
static unsigned long lastJuncMs = 0;

static int computePathCode() {
  if (!agv_running || mission == MODE_NONE) return 0;

  // AUTO: 0:P->A, 1:A->P, 2:P->B, 3:B->P
  if (mission == MODE_AUTO) {
    if (routeState == 0) return 1;
    if (routeState == 1) return 2;
    if (routeState == 2) return 3;
    if (routeState == 3) return 4;
    return 0;
  }

  // MANUAL TRIP: tripDrop A/B, tripLeg 0:P->Drop, 1:Drop->P
  if (mission == MODE_MANUAL_TRIP) {
    if (tripDrop == 'A') return (tripLeg == 0) ? 1 : 2;
    if (tripDrop == 'B') return (tripLeg == 0) ? 3 : 4;
    return 0;
  }

  // MANUAL CHARGE: 0:P->C, 1:C->P
  if (mission == MODE_MANUAL_CHARGE) {
    return (chargeLeg == 0) ? 5 : 6;
  }

  return 0;
}

static const char* pathToken(int p) {
  switch (p) {
    case 1: return "P-A";
    case 2: return "A-P";
    case 3: return "P-B";
    case 4: return "B-P";
    case 5: return "P-C";
    case 6: return "C-P";
    default: return "";
  }
}

static void updatePathLog() {
  int p = computePathCode();
  if (p != 0 && p != lastPath) {
    lastPath = p;
    LOGC("PATH", pathToken(p));
  }
}

void PID_Controller(int base_speed, int p, int d) {
  read_black_line();

  if (sumOnSensor > 0) line_position = (float)sensorWight / (float)sumOnSensor;
  error = center_position - line_position;

  derivative = error - previous_error;
  int corr = (int)(error * p + derivative * d);

  int right_motor_correction = base_speed + corr;
  int left_motor_correction  = base_speed - corr;

  previous_error = error;

  motor(left_motor_correction, right_motor_correction);

  // ✅ PATH while moving (only when changes)
  updatePathLog();
}

// ==========================================================
// ================= PATTERN ROUTING ========================
// ==========================================================
void checkSpecialPatterns() {

  // ---- Cross junction edge detect + cooldown ----
  if (bitSensor == PATTERN_T && !T_detected) {
    unsigned long now = millis();
    if (now - lastJuncMs > 1200) {     // anti-spam
      lastJuncMs = now;

      T_detected = true;
      LOGC("JUNC", "X");               // cross detected

      delay(1000);
      handleTJunction();
    }
  }
  if (bitSensor != PATTERN_T) {
    T_detected = false;
  }

  // ---- Destination edge detect ----
  bool isPick   = (bitSensor == PATTERN_PICK);
  bool isDrop   = (bitSensor == PATTERN_DROP);
  bool isCharge = (bitSensor == PATTERN_CHARGE);

  bool expected = false;

  if (mission == MODE_AUTO) {
    if ((routeState == 0 || routeState == 2) && isDrop) expected = true;
    if ((routeState == 1 || routeState == 3) && isPick) expected = true;
  }
  else if (mission == MODE_MANUAL_TRIP) {
    if (tripLeg == 0 && isDrop) expected = true;
    if (tripLeg == 1 && isPick) expected = true;
  }
  else if (mission == MODE_MANUAL_CHARGE) {
    if (chargeLeg == 0 && isCharge) expected = true;
    if (chargeLeg == 1 && isPick) expected = true;
  }

  if (expected && !destination_detected) {
    destination_detected = true;

    if (isPick)   LOGC("DEST", "P");
    if (isDrop)   LOGC("DEST", "D");
    if (isCharge) LOGC("DEST", "C");

    handleDestination();
  }

  if (!expected) {
    destination_detected = false;
  }
}