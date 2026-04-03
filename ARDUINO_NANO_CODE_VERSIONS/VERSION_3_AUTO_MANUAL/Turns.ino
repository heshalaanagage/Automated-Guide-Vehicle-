void turnRight(int L, int R) {
  while (sensorDigital[4] != 1) {
    motor(L, -R);
    read_black_line();
    direction = "straight";
  }
}

void turnLeft(int L, int R) {
  while (sensorDigital[4] != 1) {
    motor(-L, R);
    read_black_line();
    direction = "straight";
  }
}

void hard_stop() {
  while (1) motor(0, 0);
}

void stop() {
  motor(0, 0);
}

// ==========================================================
// ================== NEW: U-TURN FUNCTION ==================
// ==========================================================
void uTurn() {
  // 180° = right + right (or left + left)
  turnRight(turnSpeed, turnSpeed);
  delay(150);
  turnRight(turnSpeed, turnSpeed);

  // little settle time to avoid immediate re-detect
  delay(200);
}

// ==========================================================
// ================= ROUTE ACTIONS ==========================
// ==========================================================

void handleTJunction() {

  stopMotors();
  delay(150);

  // ---------- AUTO ----------
  if (mission == MODE_AUTO) {
    if (routeState == 2) {
      turnRight(turnSpeed, turnSpeed);
    } else if (routeState == 3) {
      turnLeft(turnSpeed, turnSpeed);
    }
  }

  // ---------- MANUAL TRIP ----------
  else if (mission == MODE_MANUAL_TRIP) {
    if (tripDrop == 'B') {
      if (tripLeg == 0) turnRight(turnSpeed, turnSpeed);
      else              turnLeft(turnSpeed, turnSpeed);
    }
  }

  // ---------- MANUAL CHARGE ----------
  else if (mission == MODE_MANUAL_CHARGE) {
    if (chargeLeg == 0) turnLeft(turnSpeed, turnSpeed);
    else                turnRight(turnSpeed, turnSpeed);
  }

  delay(200);
}

void handleDestination() {

  stopMotors();
  delay(300);

  // ---------- AUTO ----------
  if (mission == MODE_AUTO) {

    // routeState 0/2 ends at DROP, 1/3 ends at PICK
    if (routeState == 0 || routeState == 2) {
      gripDisengage();   // DROP
    } else {
      gripEngage();      // PICK
    }

    delay(500);

    // ✅ wait at destination
    delay(waitingTime * 60000);

    // ✅ U-turn to come back to track
    uTurn();

    // ✅ prevent instant re-trigger on same pattern
    destination_detected = true;
    delay(400);

    // next state
    routeState++;
    if (routeState > 3) {
      routeState = 0;
      currentLoop++;
    }

    if (currentLoop >= loopCount) {
      agv_running = false;
      stopMotors();
      mission = MODE_NONE;
      Serial.println("✅ AUTO FINISHED");
    }
    return;
  }

  // ---------- MANUAL TRIP ----------
  if (mission == MODE_MANUAL_TRIP) {

    if (tripLeg == 0) {
      gripDisengage();   // reached DROP
    } else {
      gripEngage();      // reached PICK
    }

    delay(500);

    // ✅ wait at destination
    delay(waitingTime * 60000);

    // ✅ U-turn to go opposite direction
    uTurn();

    // ✅ prevent instant re-trigger on same pattern
    destination_detected = true;
    delay(400);

    // flip leg
    tripLeg = 1 - tripLeg;

    if (tripLeg == 0) {
      tripLoopNow++;
      Serial.print("🔁 Trip Loop Done: ");
      Serial.println(tripLoopNow);
    }

    if (tripLoopNow >= tripLoopTarget) {
      agv_running = false;
      stopMotors();
      mission = MODE_NONE;
      Serial.println("✅ MANUAL TRIP FINISHED");
    }
    return;
  }

  // ---------- MANUAL CHARGE ----------
  if (mission == MODE_MANUAL_CHARGE) {

    if (chargeLeg == 0) {
      Serial.println("🔋 CHARGE POSITION REACHED");

      // ✅ charge wait time (CTIME)
      delay(chargeTimeMin * 60000);

      // ✅ U-turn after charging to return
      uTurn();

      destination_detected = true;
      delay(400);

      chargeLeg = 1; // now going back to PICK
    } else {
      Serial.println("🏁 BACK TO PICK - CHARGE DONE");
      agv_running = false;
      stopMotors();
      mission = MODE_NONE;
    }
    return;
  }
}
