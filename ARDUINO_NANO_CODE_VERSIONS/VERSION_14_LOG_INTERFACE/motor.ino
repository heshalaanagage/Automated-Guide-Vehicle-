void motor(int LPWM, int RPWM) {

  LPWM = constrain(LPWM, -255, 255);
  RPWM = constrain(RPWM, -255, 255);

  // ===== LEFT MOTOR =====
  if (LPWM > 0) {
    analogWrite(L_RPWM, LPWM);
    analogWrite(L_LPWM, 0);
  } 
  else if (LPWM < 0) {
    analogWrite(L_RPWM, 0);
    analogWrite(L_LPWM, -LPWM);
  } 
  else {
    analogWrite(L_RPWM, 0);
    analogWrite(L_LPWM, 0);
  }

  // ===== RIGHT MOTOR =====
  if (RPWM > 0) {
    analogWrite(R_RPWM, RPWM);
    analogWrite(R_LPWM, 0);
  } 
  else if (RPWM < 0) {
    analogWrite(R_RPWM, 0);
    analogWrite(R_LPWM, -RPWM);
  } 
  else {
    analogWrite(R_RPWM, 0);
    analogWrite(R_LPWM, 0);
  }
}

void reverseForTime(int speedVal, unsigned long ms) {
  motor(-speedVal, -speedVal);
  delay(ms);
  stopMotors();
}
