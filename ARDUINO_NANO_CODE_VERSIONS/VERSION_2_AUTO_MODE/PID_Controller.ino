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

void checkSpecialPatterns() {

  // ========= T JUNCTION =========
  if (bitSensor == 0b11111111 && !T_detected) {
    T_detected = true;
    handleTJunction();
  }

  if (bitSensor != 0b11111111) {
    T_detected = false;
  }

  // ========= PICK =========
  if (bitSensor == 0b11000011 && !destination_detected) {
    destination_detected = true;
    handleDestination();
  }

  // ========= DROP =========
  if (bitSensor == 0b00111100 && !destination_detected) {
    destination_detected = true;
    handleDestination();
  }

  if (bitSensor != 0b11000011 && bitSensor != 0b00111100) {
    destination_detected = false;
  }
}
