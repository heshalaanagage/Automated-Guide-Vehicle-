void turnRight(int L, int R) {
  while (sensorDigital[4] != 1) {  //loop will break when sensor number 4 detects line.
    motor(L, -R);                  //right motor backward and left motor forward.
    read_black_line();             //observing continous change of sensor value.
    direction = "straight";        //set the direction value to default
  }
}
void turnLeft(int L, int R) {
  while (sensorDigital[4] != 1) {  //loop will break when sensor number 4 detects line.
    motor(-L, R);                  //right motor forward and left motor backward.
    read_black_line();             //observing continous change of sensor value.
    direction = "straight";        //set the direction value to default
  }
}
void hard_stop() {
  while (1) motor(0, 0);
}
void stop() {
  motor(0, 0);
}

void handleTJunction() {

  stopMotors();
  delay(150);

  if (routeState == 2) {
    turnRight(turnSpeed, turnSpeed);
  }

  else if (routeState == 3) {
    turnLeft(turnSpeed, turnSpeed);
  }

  // routeState 0 & 1 = straight

  delay(200);
}
void handleDestination() {

  stopMotors();
  delay(300);

  // ===== GRIPPER =====
  if (routeState == 0 || routeState == 2) {
    gripEngage();      // PICK
  }
  else {
    gripDisengage();   // DROP
  }

  delay(500);

  // ===== WAIT =====
  delay(waitingTime * 60000);

  // ===== NEXT STATE =====
  routeState++;

  if (routeState > 3) {
    routeState = 0;
    currentLoop++;
  }

  // ===== LOOP FINISH =====
  if (currentLoop >= loopCount) {
    agv_running = false;
    stopMotors();
  }
}
