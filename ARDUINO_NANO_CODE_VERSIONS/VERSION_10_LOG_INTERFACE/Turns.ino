static char decideTurn() {
  // SAME logic as before (no change)
  if (mission == MODE_AUTO) {
    if (routeState == 2) return 'R';
    if (routeState == 3) return 'L';
    return 'S';
  }
  if (mission == MODE_MANUAL_TRIP) {
    if (tripDrop == 'B') {
      if (tripLeg == 0) return 'R';
      else              return 'L';
    }
    return 'S';
  }
  if (mission == MODE_MANUAL_CHARGE) {
    if (chargeLeg == 0) return 'L';
    else                return 'R';
  }
  return 'S';
}

void turnRight(int L, int R) {
  LOGC("TURN", "R+");
  unsigned long t0 = millis();

  while (sensorDigital[4] != 1) {
    checkEmergencyCommand();
    if (emergency_stop) { stopMotors(); return; }

    motor(-L, R);
    delay(2000);
    read_black_line();

    if (millis() - t0 > 2500) {
      LOGC("ERR", "TR");
      stopMotors();
      return;
    }
  }

  LOGC("TURN", "R-");
}

void turnLeft(int L, int R) {
  LOGC("TURN", "L+");
  unsigned long t0 = millis();

  while (sensorDigital[4] != 1) {
    checkEmergencyCommand();
    if (emergency_stop) { stopMotors(); return; }

    motor(L,-R);
    delay(2000);
    read_black_line();

    if (millis() - t0 > 2500) {
      LOGC("ERR", "TL");
      stopMotors();
      return;
    }
  }

  LOGC("TURN", "L-");
}

void stop() { motor(0, 0); }

void uTurn() {
  LOGC("TURN", "U+");
  turnRight(turnSpeed, turnSpeed);
  delay(150);
  turnRight(turnSpeed, turnSpeed);
  delay(200);
  LOGC("TURN", "U-");
}

void handleTJunction() {
  char d = decideTurn();

  // show ONLY L/R/S in live log
  if (d == 'R') LOGC("TURN", "R");
  else if (d == 'L') LOGC("TURN", "L");
  else LOGC("TURN", "S");

  stopMotors();
  delay(1000);

  if (d == 'R') turnRight(turnSpeed, turnSpeed);
  else if (d == 'L') turnLeft(turnSpeed, turnSpeed);
  // S -> follow PID straight

  delay(120);
}

void handleDestination() {
  stopMotors();
  delay(800);

  // Arrival sequence requested:
  // DISENGAGE -> UTURN -> WAIT -> ENGAGE -> continue
  LOGC("ACT", "A");      // arrive
  gripDisengage();
  delay(250);

  uTurn();

  // wait (minutes)
  delay((unsigned long)waitingTime * 60000UL);

  gripEngage();
  delay(200);

  destination_detected = true;
  delay(250);

  // ---- AUTO ----
  if (mission == MODE_AUTO) {
    routeState++;
    if (routeState > 3) {
      routeState = 0;
      currentLoop++;
    }

    if (currentLoop >= loopCount) {
      // finish request was handled in your previous logic; keeping minimal here
      agv_running = false;
      mission = MODE_NONE;
      stopMotors();
      LOGC("STATE", "DONE");
    }
    return;
  }

  // ---- MANUAL TRIP ----
  if (mission == MODE_MANUAL_TRIP) {
    tripLeg = 1 - tripLeg;

    if (tripLeg == 0) tripLoopNow++;

    if (tripLoopNow >= tripLoopTarget) {
      agv_running = false;
      mission = MODE_NONE;
      stopMotors();
      LOGC("STATE", "DONE");
    }
    return;
  }

  // ---- MANUAL CHARGE ----
  if (mission == MODE_MANUAL_CHARGE) {
    if (chargeLeg == 0) {
      delay((unsigned long)chargeTimeMin * 60000UL);
      uTurn();
      chargeLeg = 1;
      destination_detected = true;
      delay(250);
    } else {
      agv_running = false;
      mission = MODE_NONE;
      stopMotors();
      gripDisengage();
      LOGC("STATE", "DONE");
    }
    return;
  }
}