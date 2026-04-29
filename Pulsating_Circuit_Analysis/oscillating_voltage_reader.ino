const int analogPin = A0;
const int buttonPin = 2;
const int powerPin  = 8;
const float vRef    = 5.0;
const int SAMPLE_RATE = 10;
const int NUM_SAMPLES = 2000;

bool lastButtonState = HIGH;

void setup() {
  Serial.begin(9600);
  pinMode(buttonPin, INPUT_PULLUP);
  pinMode(powerPin, OUTPUT);
  digitalWrite(powerPin, LOW);
}

void loop() {
  bool buttonState = digitalRead(buttonPin);

  if (buttonState == LOW && lastButtonState == HIGH) {
    delay(50);

    digitalWrite(powerPin, HIGH);


    Serial.println("START");

    for (int i = 0; i < NUM_SAMPLES; i++) {
      int raw = analogRead(analogPin);
      float voltage = raw * (vRef / 1023.0);
      Serial.println(voltage);
      delay(SAMPLE_RATE);
    }

    Serial.println("END");
    digitalWrite(powerPin, LOW);

  }

  lastButtonState = buttonState;
}