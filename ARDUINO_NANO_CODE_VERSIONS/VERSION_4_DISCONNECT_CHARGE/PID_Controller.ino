void PID_Controller(int base_speed, int p, int d) {
  read_black_line();

  if (sumOnSensor > 0) line_position = sensorWight / sumOnSensor;
  error = center_position - line_position;

  derivative = error - previous_error;
  int right_motor_correction = base_speed + (error * p + derivative * d);
  int left_motor_correction  = base_speed - (error * p + derivative * d);
  previous_error = error;

  motor(left_motor_correction, right_motor_correction);
}

// ==========================================================
// ================= PATTERN ROUTING ========================
// ==========================================================
void checkSpecialPatterns() {
  // ---- T Junction edge detect ----
  if (bitSensor == PATTERN_T && !T_detected) {
    T_detected = true;
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
    handleDestination();
  }

  if (!expected) {
    destination_detected = false;
  }
}
