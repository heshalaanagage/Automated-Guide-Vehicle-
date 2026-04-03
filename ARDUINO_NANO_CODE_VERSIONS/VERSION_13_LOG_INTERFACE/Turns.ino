// ================= TURN HELPERS + MISSION LOGIC =================

extern volatile char lastDir;

// helper: any sensor in a range sees line?
static bool anyHigh(int a, int b) {
  for (int i = a; i <= b; i++) {
    if (sensorDigital[i] == 1) return true;
  }
  return false;
}

void turnLeft(int L, int R) {
  bool logged = false;
  motor(-L, R);
  delay(2000);

  unsigned long t0 = millis();
  while (1) {
    read_black_line();
    if (anyHigh(6, 7)) {
      if (!logged) {
        int s6 = sensorDigital[6];
        int s7 = sensorDigital[7];
        LOGF("S67", "6=%ld 7=%ld", s6, s7);
        logged = true;
      }
      break;
    }
    motor(-L, R);
    if (millis() - t0 > 4000) break;
  }

  stopMotors();
  direction = "straight";
}

void turnRight(int L, int R) {
  bool logged = false;
  motor(L, -R);
  delay(2000);

  unsigned long t0 = millis();
  while (1) {
    read_black_line();
    if (anyHigh(0, 3)) {
      if (!logged) {
        int s0 = sensorDigital[0];
        int s1 = sensorDigital[1];
        LOGF("S01", "0=%ld 1=%ld", s0, s1);
        logged = true;
      }
      break;
    }
    motor(L, -R);
    if (millis() - t0 > 4000) break;
  }

  stopMotors();
  direction = "straight";
}

// --- U-turn using direction history ---
void uTurn() {
  LOGC("TURN", "U");

  bool logged = false;
  bool spinRight = (lastDir == 'R');

  // initial spin
  if (spinRight) motor(turnSpeed, -turnSpeed);
  else           motor(-turnSpeed, turnSpeed);

  delay(3000);
  stopMotors();

  unsigned long t0 = millis();
  while (millis() - t0 < 4500) {
    read_black_line();

    if (anyHigh(3, 7)) {
      if (!logged) {
        int s3 = sensorDigital[3];
        int s4 = sensorDigital[4];
        int s5 = sensorDigital[5];
        int s6 = sensorDigital[6];
        LOGF("S3456", "3=%ld 4=%ld 5=%ld 6=%ld", s3, s4, s5, s6);
        logged = true;
      }
      break; // ✅ break ONLY when line found
    }

    // keep spinning until found or timeout
    if (spinRight) motor(turnSpeed, -turnSpeed);
    else           motor(-turnSpeed, turnSpeed);
  }

  stopMotors();
}

// ================= SEGMENT / ROUTE DECISIONS =================
void handleTJunction() {

  // -------- AUTO --------
  if (mission == MODE_AUTO) {
    // routeState: 0=P->A, 1=A->P, 2=P->B, 3=B->P
    switch (routeState) {
      case 0: LOGC("PATH", "P-A"); LOGC("TURN", "L"); turnLeft(turnSpeed, turnSpeed);  break;
      case 1: LOGC("PATH", "A-P"); LOGC("TURN", "R"); turnRight(turnSpeed, turnSpeed); break;
      case 2: LOGC("PATH", "P-B"); LOGC("TURN", "R"); turnRight(turnSpeed, turnSpeed); break;
      case 3: LOGC("PATH", "B-P"); LOGC("TURN", "L"); turnLeft(turnSpeed, turnSpeed);  break;
      default:
        LOGC("PATH", "AUTO");
        LOGC("TURN", "S");
        break;
    }
    return;
  }

  // -------- MANUAL TRIP --------
  if (mission == MODE_MANUAL_TRIP) {
    // Trip legs: 0 = PICK -> DROP, 1 = DROP -> PICK
    if (tripLeg == 0) {
      if      (tripDrop == 'A') { LOGC("PATH", "P-A"); LOGC("TURN", "L"); turnLeft(turnSpeed, turnSpeed); }
      else if (tripDrop == 'B') { LOGC("PATH", "P-B"); LOGC("TURN", "R"); turnRight(turnSpeed, turnSpeed); }
      else if (tripDrop == 'C') { LOGC("PATH", "P-C"); LOGC("TURN", "S"); /* straight */ }
      else                      { LOGC("PATH", "P-?"); LOGC("TURN", "S"); }
    } else {
      if      (tripDrop == 'A') { LOGC("PATH", "A-P"); LOGC("TURN", "R"); turnRight(turnSpeed, turnSpeed); }
      else if (tripDrop == 'B') { LOGC("PATH", "B-P"); LOGC("TURN", "L"); turnLeft(turnSpeed, turnSpeed); }
      else if (tripDrop == 'C') { LOGC("PATH", "C-P"); LOGC("TURN", "S"); /* straight */ }
      else                      { LOGC("PATH", "?-P"); LOGC("TURN", "S"); }
    }
    return;
  }

  // -------- MANUAL CHARGE --------
  if (mission == MODE_MANUAL_CHARGE) {
    if (chargeLeg == 0) { LOGC("PATH", "P-C"); LOGC("TURN", "S"); }
    else                { LOGC("PATH", "C-P"); LOGC("TURN", "S"); }
    return;
  }
}

// ================= DESTINATION HANDLING =================

// routeState map (if you use 6 states): 0:P-A,1:A-P,2:P-B,3:B-P,4:P-C,5:C-P
static inline bool autoIsCState() {
  return (routeState == 4 || routeState == 5);
}

// wait helper: 1 unit = 10 seconds  -> milliseconds
static inline unsigned long WAIT_MS() {
  if (waitingTime <= 0) return 0;
  return (unsigned long)waitingTime * 10000UL;   // ✅ 1 = 10,000 ms
}

// Normal action sequence (STOP -> DISENGAGE -> UTURN -> WAIT -> ENGAGE)
static void normalSequenceWithUTurn(const char* locTag) {
  stopMotors();
  LOGC("DEST", locTag);                 // A / B / P
  gripDisengage();
  uTurn();

  unsigned long ms = WAIT_MS();
  LOGF("WAIT", "unit=%ld ms=%ld", (long)waitingTime, (long)ms);
  delay(ms);

  gripEngage();
}

void handleDestination() {

  // =========================================================
  // 1) MANUAL CHARGE MODE (P <-> C): NO GRIPPER ACTIONS
  // =========================================================
  if (mission == MODE_MANUAL_CHARGE) {

    bool atCharge = (bitSensor == PATTERN_CHARGE);
    bool atPick   = (bitSensor == PATTERN_PICK);

    // P -> C reached
    if (chargeLeg == 0) {
      if (!atCharge) return;

      stopMotors();
      LOGC("DEST", "C");

      unsigned long chargeMs = (unsigned long)chargeTimeMin * 10000UL;
      LOGF("WAIT", "chargeUnit=%ld ms=%ld", (long)chargeTimeMin, (long)chargeMs);

  if (chargeTimeMin > 0) {
    delay(chargeMs);
  }

  uTurn();
  chargeLeg = 1;
  return;
}

    // C -> P reached
    if (chargeLeg == 1) {
      if (!atPick) return;

      stopMotors();
      LOGC("DEST", "P");
      agv_running = false;
      mission = MODE_NONE;
      LOGC("STATE", "DONE");
      return;
    }

    return;
  }

  // =========================================================
  // 2) AUTO MODE: C legs (P<->C) -> NO GRIPPER ACTIONS
  // =========================================================
  if (mission == MODE_AUTO && autoIsCState()) {

    if (routeState == 4 && bitSensor == PATTERN_CHARGE) {
      stopMotors();
      LOGC("DEST", "C");
      uTurn();
      routeState = 5;
      return;
    }

    if (routeState == 5 && bitSensor == PATTERN_PICK) {
      stopMotors();
      LOGC("DEST", "P");
      routeState = 0;
      currentLoop++;
      if (currentLoop >= loopCount) {
        mission = MODE_NONE;
        agv_running = false;
        stopMotors();
        LOGC("STATE", "DONE");
      }
      return;
    }

    return;
  }

  // =========================================================
  // 3) AUTO MODE: A/B normal gripper timing
  // =========================================================
  if (mission == MODE_AUTO) {

    bool atPick = (bitSensor == PATTERN_PICK);
    bool atDrop = (bitSensor == PATTERN_DROP);

    // DROP reached on outgoing legs
    if (atDrop && (routeState == 0 || routeState == 2)) {
      if (routeState == 0) normalSequenceWithUTurn("A");
      else                 normalSequenceWithUTurn("B");

      routeState++;   // return leg
      return;
    }

    // PICK reached on return legs
    if (atPick && (routeState == 1 || routeState == 3)) {
      normalSequenceWithUTurn("P");

      routeState++;   // next leg
      if (routeState >= 4) {          // AUTO uses 0..3 in your handleTJunction()
        routeState = 0;
        currentLoop++;
        if (currentLoop >= loopCount) {
          mission = MODE_NONE;
          agv_running = false;
          stopMotors();
          LOGC("STATE", "DONE");
        }
      }
      return;
    }

    return;
  }

  // =========================================================
  // 4) MANUAL TRIP MODE: A/B normal gripper timing
  // =========================================================
  if (mission == MODE_MANUAL_TRIP) {

    bool atPick = (bitSensor == PATTERN_PICK);
    bool atDrop = (bitSensor == PATTERN_DROP);

    // reached DROP
    if (tripLeg == 0 && atDrop) {
      if (tripDrop == 'B') normalSequenceWithUTurn("B");
      else                 normalSequenceWithUTurn("A");

      tripLeg = 1;
      return;
    }

    // reached Pick
    if (tripLeg == 1 && atPick) {
      normalSequenceWithUTurn("P");

      tripLeg = 0;
      tripLoopNow++;
      if (tripLoopNow >= tripLoopTarget) {
        mission = MODE_NONE;
        agv_running = false;
        stopMotors();
        LOGC("STATE", "DONE");
      }
      return;
    }

    return;
  }
}

void hard_stop() {
  while (1) motor(0, 0);
}

void stop() {
  motor(0, 0);
}