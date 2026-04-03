// ================= TURN HELPERS =================

static inline const char* dirName(char d) {
  if (d == 'L') return "LEFT";
  if (d == 'R') return "RIGHT";
  return "STRAIGHT";
}

static char decideTurn() {
  // Keep your SAME logic (functionality unchanged)
  // AUTO
  if (mission == MODE_AUTO) {
    if (routeState == 2) return 'R';
    if (routeState == 3) return 'L';
    return 'S';
  }

  // MANUAL TRIP
  if (mission == MODE_MANUAL_TRIP) {
    if (tripDrop == 'B') {
      if (tripLeg == 0) return 'R';
      else              return 'L';
    }
    return 'S';
  }

  // MANUAL CHARGE
  if (mission == MODE_MANUAL_CHARGE) {
    if (chargeLeg == 0) return 'L';
    else                return 'R';
  }

  return 'S';
}

void turnRight(int L, int R) {
  unsigned long t0 = millis();
  while (sensorDigital[4] != 1) {
    checkEmergencyCommand();
    if (emergency_stop) { stopMotors(); return; }

    motor(L, -R);
    read_black_line();

    if (millis() - t0 > 2500) {
      LOG("ERR", "turnRight timeout");
      stopMotors();
      return;
    }
  }
}

void turnLeft(int L, int R) {
  unsigned long t0 = millis();
  while (sensorDigital[4] != 1) {
    checkEmergencyCommand();
    if (emergency_stop) { stopMotors(); return; }

    motor(-L, R);
    read_black_line();

    if (millis() - t0 > 2500) {
      LOG("ERR", "turnLeft timeout");
      stopMotors();
      return;
    }
  }
}

void stop() {
  motor(0, 0);
}

// ================== U-TURN ==================
void uTurn() {
  LOG("TURN", "uTurn START");

  turnRight(turnSpeed, turnSpeed);
  delay(150);
  turnRight(turnSpeed, turnSpeed);

  delay(200);
  LOG("TURN", "uTurn END");
}

// ================= ROUTE ACTIONS ==========================

void handleTJunction() {

  // Decide direction (same logic, only better logs)
  char d = decideTurn();

  // Show decision clearly in live logs
  LOGF("TURN", "JUNC mission=%ld rs=%ld leg=%ld drop=%ld -> %ld",
       (long)mission,
       (long)routeState,
       (long)tripLeg,
       (long)tripDrop,
       (long)d);

  // Human readable
  LOG("TURN", String("DO ") + dirName(d));

  stopMotors();
  delay(200);  // keep small; junction logic unchanged

  if (d == 'R') turnRight(turnSpeed, turnSpeed);
  else if (d == 'L') turnLeft(turnSpeed, turnSpeed);
  else {
    // Straight: do nothing (follow line by PID)
  }

  delay(150);
  LOG("TURN", "JUNC END");
}

void handleDestination() {

  LOGF("DEST", "START mission=%ld rs=%ld leg=%ld",
       (long)mission, (long)routeState, (long)tripLeg);

  stopMotors();
  delay(200);

  // ---------- AUTO ----------
  if (mission == MODE_AUTO) {

    if (routeState == 0 || routeState == 2) {
      gripDisengage();   // DROP
      LOG("DEST", "AUTO DROP -> release");
    } else {
      gripEngage();      // PICK
      LOG("DEST", "AUTO PICK -> grip");
    }

    delay(300);

    LOGF("WAIT", "min=%ld", (long)waitingTime);
    delay((unsigned long)waitingTime * 60000UL);

    uTurn();

    destination_detected = true;
    delay(300);

    routeState++;
    if (routeState > 3) {
      routeState = 0;
      currentLoop++;
    }

    if (currentLoop >= loopCount) {
      agv_running = false;
      stopMotors();
      mission = MODE_NONE;
      LOG("STATE", "AUTO FINISHED");
    }
    return;
  }

  // ---------- MANUAL TRIP ----------
  if (mission == MODE_MANUAL_TRIP) {

    if (tripLeg == 0) {
      gripDisengage();
      LOG("DEST", "TRIP DROP -> release");
    } else {
      gripEngage();
      LOG("DEST", "TRIP PICK -> grip");
    }

    delay(300);

    LOGF("WAIT", "min=%ld", (long)waitingTime);
    delay((unsigned long)waitingTime * 60000UL);

    uTurn();

    destination_detected = true;
    delay(300);

    tripLeg = 1 - tripLeg;

    if (tripLeg == 0) {
      tripLoopNow++;
      LOGF("TRIP", "loopNow=%ld", (long)tripLoopNow);
    }

    if (tripLoopNow >= tripLoopTarget) {
      agv_running = false;
      stopMotors();
      mission = MODE_NONE;
      LOG("STATE", "MANUAL TRIP FINISHED");
    }
    return;
  }

  // ---------- MANUAL CHARGE ----------
  if (mission == MODE_MANUAL_CHARGE) {

    if (chargeLeg == 0) {
      LOGF("CHG", "REACHED waitMin=%ld", (long)chargeTimeMin);

      delay((unsigned long)chargeTimeMin * 60000UL);

      uTurn();
      chargeLeg = 1;

      destination_detected = true;
      delay(300);

    } else {
      LOG("CHG", "BACK TO PICK - DONE");
      agv_running = false;
      stopMotors();
      mission = MODE_NONE;
    }
    return;
  }
}