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
  delay(time_turn);

  unsigned long t0 = millis();
  while (1) {
    read_black_line();
    if (anyHigh(5, 7)) {
      if (!logged) {
        // show which sensors triggered
        int s6 = sensorDigital[6];
        int s7 = sensorDigital[7];
        LOGF("S67", "6=%ld 7=%ld", s6, s7);   // ✅ one-time log
        logged = true;
      }
      break;
    }
    motor(-L, R);
    if (millis() - t0 > time_turn_execute) break;
  }

  stopMotors();
  direction = "straight";
}

// RIGHT turn:
// 1) kick right for 200ms
// 2) keep turning right until any of sensors 0..3 sees line
void turnRight(int L, int R) {
  bool logged = false;
  motor(L, -R);
  delay(time_turn);

  unsigned long t0 = millis();
  while (1) {
    read_black_line();
    if (anyHigh(0, 3)) {
      if (!logged) {
        // show which sensors triggered
        int s0 = sensorDigital[0];
        int s1 = sensorDigital[1];
        LOGF("S01", "0=%ld 1=%ld", s0, s1);   // ✅ one-time log
        logged = true;
      }
      break;
    }
    motor(L, -R);
    if (millis() - t0 > time_turn_execute) break;
  }

  stopMotors();
  direction = "straight";
}

// --- U-turn using direction history ---
void uTurn() {
  LOGC("TURN", "U");

  bool spinRight = (lastDir == 'R');

  if (spinRight) motor(turnSpeed, -turnSpeed);
  else           motor(-turnSpeed, turnSpeed);

  delay(time_uturn);
  stopMotors();

  unsigned long t0 = millis();
  while (millis() - t0 < 700) {
    read_black_line();
    if (sensorDigital[4] == 1) break;

    if (spinRight) motor(turnSpeed, -turnSpeed);
    else           motor(-turnSpeed, turnSpeed);
  }
  stopMotors();
}

// ================= SEGMENT / ROUTE DECISIONS =================
// Routes mapping (AUTO + MANUAL same):
//   P->A : LEFT
//   P->B : RIGHT
//   P->C : STRAIGHT
//   A->P : RIGHT
//   B->P : LEFT
//   C->P : STRAIGHT
//
// AUTO loop order (one loop):
//   P->A -> A->P -> P->B -> B->P
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
    // charge location is C
    if (chargeLeg == 0) { LOGC("PATH", "P-C"); LOGC("TURN", "S"); }
    else                { LOGC("PATH", "C-P"); LOGC("TURN", "S"); }
    return;
  }
}

// ================= DESTINATION HANDLING =================
void handleDestination() {
  stopMotors();

  // ---------------- AUTO ----------------
  if (mission == MODE_AUTO) {

    bool atPick = (bitSensor == PATTERN_PICK);
    bool atDrop = (bitSensor == PATTERN_DROP);

    if (atPick) {
      LOGC("DEST", "P");
      gripEngage();
    } else if (atDrop) {
      if (routeState == 0) LOGC("DEST", "A");
      else if (routeState == 2) LOGC("DEST", "B");
      else LOGC("DEST", "D");

      gripDisengage();
    } else {
      return;
    }

    if (waitingTime > 0) {
      LOGF("WAIT", "%ld", waitingTime);
      delay((unsigned long)waitingTime * 60000UL);
    }

    // ✅ U-turn ONLY when destination detected
    uTurn();

    routeState++;

    // AUTO loop only 4 segments
    if (routeState >= 4) {
      routeState = 0;
      currentLoop++;
      LOGF("LOOP", "%ld", currentLoop);

      if (currentLoop >= loopCount) {
        agv_running = false;
        mission = MODE_NONE;
        LOGC("STATE", "DONE");
      }
    }
    return;
  }

  // ---------------- MANUAL TRIP ----------------
  if (mission == MODE_MANUAL_TRIP) {

    bool atPick = (bitSensor == PATTERN_PICK);
    bool atDrop = (bitSensor == PATTERN_DROP);

    if (tripLeg == 0) {
      if (!atDrop) return;

      if      (tripDrop == 'A') LOGC("DEST", "A");
      else if (tripDrop == 'B') LOGC("DEST", "B");
      else if (tripDrop == 'C') LOGC("DEST", "C");
      else                      LOGC("DEST", "D");

      gripDisengage();

    } else {
      if (!atPick) return;
      LOGC("DEST", "P");
      gripEngage();
    }

    if (waitingTime > 0) {
      LOGF("WAIT", "%ld", waitingTime);
      delay((unsigned long)waitingTime * 60000UL);
    }

    uTurn();

    tripLeg = 1 - tripLeg;

    if (tripLeg == 0) {
      tripLoopNow++;
      LOGF("LOOP", "%ld", tripLoopNow);

      if (tripLoopNow >= tripLoopTarget) {
        agv_running = false;
        mission = MODE_NONE;
        LOGC("STATE", "DONE");
      }
    }
    return;
  }

  // ---------------- MANUAL CHARGE ----------------
  if (mission == MODE_MANUAL_CHARGE) {

    bool atCharge = (bitSensor == PATTERN_CHARGE);
    bool atPick   = (bitSensor == PATTERN_PICK);

    if (chargeLeg == 0) {
      if (!atCharge) return;

      LOGC("DEST", "CH");

      if (chargeTimeMin > 0) {
        LOGF("WAIT", "%ld", chargeTimeMin);
        delay((unsigned long)chargeTimeMin * 60000UL);
      }

      uTurn();
      chargeLeg = 1;
      return;

    } else {
      if (!atPick) return;

      LOGC("DEST", "P");
      stopMotors();
      agv_running = false;
      mission = MODE_NONE;
      LOGC("STATE", "DONE");
      return;
    }
  }
}

void hard_stop() {
  while (1) motor(0, 0);
}

void stop() {
  motor(0, 0);
}