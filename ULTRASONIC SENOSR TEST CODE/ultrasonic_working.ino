// Define pins
const int trigPin = 25;
const int echoPin = 26;

// Define sound velocity in cm/uS
#define SOUND_VELOCITY 0.034

void setup() {
  Serial.begin(115200); // Start serial communication
  pinMode(trigPin, OUTPUT); // Sets the trigPin as an Output
  pinMode(echoPin, INPUT);  // Sets the echoPin as an Input
}

void loop() {
  // Clear the trigPin
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  
  // Set the trigPin on HIGH state for 10 micro seconds
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  
  // Reads the echoPin, returns the sound wave travel time in microseconds
  long duration = pulseIn(echoPin, HIGH);
  
  // Calculate the distance
  // Formula: Distance = (Time * Speed of Sound) / 2
  float distanceCm = duration * SOUND_VELOCITY / 2;
  
  // Prints the distance on the Serial Monitor
  Serial.print("Distance (cm): ");
  Serial.println(distanceCm);
  
  delay(500); // Wait half a second before next reading
}