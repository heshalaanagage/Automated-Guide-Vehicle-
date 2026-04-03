void PID_Controller(int base_speed, int p, int d) {
  read_black_line();

  if (sumOnSensor > 0) line_position = (float)sensorWight / (float)sumOnSensor;
  error = center_position - line_position;

  derivative = error - previous_error;

  int corr = (int)(error * p + derivative * d);
  int right_motor_correction = base_speed + corr;
  int left_motor_correction  = base_speed - corr;

  previous_error = error;

  // short, meaningful log (throttled)
  LOGF("PID", "pos=%ld err=%ld der=%ld L=%ld R=%ld",
       (long)line_position, (long)error, (long)derivative,
       left_motor_correction, right_motor_correction);

  motor(left_motor_correction, right_motor_correction);
}

// ==========================================================
// ================= PATTERN ROUTING ========================
// ==========================================================
void checkSpecialPatterns() {

  // ---- T Junction edge detect ----
  if (bitSensor == PATTERN_T && !T_detected) {
    T_detected = true;
    LOG("DECIDE", "T_JUNCTION");
    delay(300);
    handleTJunction();
  }
  if (bitSensor != PATTERN_T) {
    T_detected = false;
  }

  // ---- Destination edge detect (ONLY when expected destination) ----
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
    if (isPick)   LOG("DECIDE", "DEST->PICK");
    if (isDrop)   LOG("DECIDE", "DEST->DROP");
    if (isCharge) LOG("DECIDE", "DEST->CHARGE");
    handleDestination();
  }

  if (!expected) {
    destination_detected = false;
  }
}