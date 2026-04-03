void read_black_line() {
  sumOnSensor = 0;
  sensorWight = 0;
  bitSensor = 0;

  for (int i = 0; i < 8; i++) {
    selectChannel(i);
    delayMicroseconds(20);
    sensorADC[i] = digitalRead(SIG_PIN);

    sensorDigital[i] = (sensorADC[i] > theshold) ? 1 : 0;

    sumOnSensor += sensorDigital[i];
    sensorWight += sensorDigital[i] * WeightValue[i];
    bitSensor   += sensorDigital[i] * bitWeight[7 - i];
  }
}

void selectChannel(int channel) {
  digitalWrite(S0, bitRead(channel, 0));
  digitalWrite(S1, bitRead(channel, 1));
  digitalWrite(S2, bitRead(channel, 2));
  digitalWrite(S3, bitRead(channel, 3));
}