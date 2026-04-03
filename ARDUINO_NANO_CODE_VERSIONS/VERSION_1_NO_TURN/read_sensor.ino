void read_black_line() {
  sumOnSensor = 0;
  sensorWight = 0;
  bitSensor = 0;

  for (int i = 0; i < 8; i++) {   // 🔄 changed from 8-15 to 0-7

    selectChannel(i);
    delayMicroseconds(20);
    sensorADC[i] = digitalRead(SIG_PIN);

    // analog to digital
    if (sensorADC[i] > theshold) {
      sensorDigital[i] = 1;
    } else {
      sensorDigital[i] = 0;
    }

    sumOnSensor += sensorDigital[i];
    sensorWight += sensorDigital[i] * WeightValue[i];
    bitSensor += sensorDigital[i] * bitWeight[7 - i];  // 🔄 corrected indexing

    // PRINT EACH SENSOR VALUE
    Serial.print(sensorDigital[i]);
    Serial.print("\t");
  }
  Serial.println();
}
void selectChannel(int channel) {
  digitalWrite(S0, bitRead(channel, 0));
  digitalWrite(S1, bitRead(channel, 1));
  digitalWrite(S2, bitRead(channel, 2));
  digitalWrite(S3, bitRead(channel, 3));
}