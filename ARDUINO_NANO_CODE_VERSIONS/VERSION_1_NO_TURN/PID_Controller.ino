void PID_Controller(int base_speed, int p, int d) {
  read_black_line();

  if (sumOnSensor > 0) line_position = sensorWight / sumOnSensor;
  error = center_position - line_position;

  switch (bitSensor) {
    case 0b11111000:
    case 0b11110000:
    case 0b11111100:
      direction = "left";
      break;

    case 0b00011111:
    case 0b00001111:
    case 0b00111111:
      direction = "right";
      break;

    default:
      direction = "straight";
      break;
  }

  if (bitSensor == 0) {
    error = 0;
    if (direction != "straight") {
      delay(delay_before_turn);

      if (direction == "right") {
        turnRight(turnSpeed, turnSpeed);
      } else {
        turnLeft(turnSpeed, turnSpeed);
      }
    }
  } 
  derivative = error - previous_error;
  int right_motor_correction = base_speed + (error * p + derivative * d);
  int left_motor_correction  = base_speed - (error * p + derivative * d);
  previous_error = error;

  motor(left_motor_correction, right_motor_correction);
}
