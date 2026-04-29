#include <Servo.h>

// ---------- pin definitions ----------
const int TRIG_PIN  = 4;
const int ECHO_PIN  = 10;
const int SERVO_PIN = 6;

// ---------- radar parameters ----------
const int MIN_ANGLE     = 0;
const int MAX_ANGLE     = 180;
const int STEP_DEG      = 2;
const int STEP_DELAY    = 80;
const int NUM_SAMPLES   = 3;
const int DETECT_RANGE  = 6;   // max detection distance (cm)

Servo radarServo;

// flag: has an object been detected in this sweep?
bool detectedThisSweep = false;
int lastDetectedAngle = -1;  // -1 means no previous detection
// ---------- setup ----------
void setup() {
  Serial.begin(9600);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  radarServo.attach(SERVO_PIN);
  radarServo.write(90);
  delay(500);
}

// ---------- single ping ----------
long singlePing() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  delay(10);

  return duration;
}

// ---------- averaged measurement ----------
int measureCM() {
  long total = 0;

  for (int i = 0; i < NUM_SAMPLES; i++) {
    long duration = singlePing();

    if (duration == 0) {
      total += DETECT_RANGE + 1;
    } else {
      total += duration / 58;
    }
  }

  int dist = total / NUM_SAMPLES;

  // ignore anything beyond 6 cm
  if (dist > DETECT_RANGE) return 0;

  return dist;
}

// ---------- main loop ----------
void loop() {
  lastDetectedAngle = -1;
  // forward sweep: 0 → 180
  for (int angle = MIN_ANGLE; angle <= MAX_ANGLE; angle += STEP_DEG) {
    radarServo.write(angle);
    delay(STEP_DELAY);

    int dist = measureCM();

    // detect ONLY ONCE per sweep and only if at same angle as last detected
    if (dist > 0 && angle != lastDetectedAngle) {
      Serial.print(angle);
      Serial.print(',');
      Serial.println(dist);
      lastDetectedAngle = angle;   // store this angle as last detected
    }
  }

  // reverse sweep: 180 → 0
  lastDetectedAngle = -1;
  for (int angle = MAX_ANGLE; angle >= MIN_ANGLE; angle -= STEP_DEG) {
    radarServo.write(angle);
    delay(STEP_DELAY);

    int dist = measureCM();

    if (dist > 0 && angle != lastDetectedAngle) {
      Serial.print(angle);
      Serial.print(',');
      Serial.println(dist);
      lastDetectedAngle = angle;
    }
  }
}