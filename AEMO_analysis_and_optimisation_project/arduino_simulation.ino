/*
 * AEMO QLD Dispatch Receiver
 * ===========================
 * Receives JSON dispatch commands from Python over USB serial.
 * Maps each fuel type's target level (0.0–1.0) to PWM brightness
 * on a dedicated LED, simulating generator output levels.
 *
 * Fuel LEDs hold their brightness continuously.
 * Signal LED blinks briefly when a new dispatch command is received,
 * then goes off until the next update.
 *
 * CIRCUIT — Arduino Uno
 * ─────────────────────────────────────────────────────────────────
 * PWM FUEL LEDs  (pins marked ~ on the Uno board)
 *   Pin  3  → coal    LED   (RED)
 *   Pin  5  → gas     LED   (YELLOW)
 *   Pin  6  → hydro   LED   (BLUE)
 *   Pin  9  → solar   LED   (WHITE)
 *   Pin 10  → wind    LED   (GREEN)
 *   Pin 11  → oil     LED   (ORANGE)
 *
 * DIGITAL PRICE SIGNAL LED
 *   Pin  7  → 220Ω → price signal LED → GND
 *   Blinks briefly on each new dispatch, then turns off.
 *
 * LIBRARY REQUIRED:
 *   ArduinoJson v6 — Sketch → Include Library → Manage Libraries
 */
 
#include <ArduinoJson.h>
 
// ── Pin assignments ──────────────────────────────────────────────
const int PIN_COAL   = 3;
const int PIN_GAS    = 5;
const int PIN_HYDRO  = 6;
const int PIN_SOLAR  = 9;
const int PIN_WIND   = 10;
const int PIN_OIL    = 11;
const int PIN_SIGNAL = 7;
 
// ── Signal LED state ─────────────────────────────────────────────
// On new dispatch: blink N times based on price signal, then go dark.
String        priceSignal      = "BALANCED";
bool          signalActive     = false;   // true while blink sequence is running
int           signalBlinksLeft = 0;       // remaining blink pulses
bool          signalLedOn      = false;   // current LED state within sequence
unsigned long signalLastChange = 0;       // millis() of last state change
 
// Blink parameters per signal type
struct SignalConfig {
  int  blinks;    // number of on/off pulses to fire
  int  on_ms;     // how long the LED stays on per pulse
  int  off_ms;    // how long the LED stays off between pulses
};
 
SignalConfig getSignalConfig(const String& sig) {
  if      (sig == "SEVERE_OVERSUPPLY") return {6,  60,  60};   // rapid burst
  else if (sig == "OVERSUPPLY")        return {2, 150, 150};   // double blink
  else if (sig == "BALANCED")          return {1, 300, 0};     // single short blink
  else if (sig == "ELEVATED")          return {3, 120, 120};   // triple blink
  else if (sig == "HIGH_DEMAND")       return {4,  80,  80};   // quad fast blink
  else                                 return {6,  80,  80};   // PRICE_SPIKE: 6 rapid
}
 
// ── Serial input ─────────────────────────────────────────────────
String inputBuffer = "";
 
// ── Helpers ──────────────────────────────────────────────────────
int levelToPWM(float level) {
  return (int)constrain(level * 255.0f, 0, 255);
}
 
void applyLevel(int pin, float level) {
  analogWrite(pin, levelToPWM(level));
}
 
// ── Setup ────────────────────────────────────────────────────────
void setup() {
  Serial.begin(9600);
 
  pinMode(PIN_COAL,   OUTPUT);
  pinMode(PIN_GAS,    OUTPUT);
  pinMode(PIN_HYDRO,  OUTPUT);
  pinMode(PIN_SOLAR,  OUTPUT);
  pinMode(PIN_WIND,   OUTPUT);
  pinMode(PIN_OIL,    OUTPUT);
  pinMode(PIN_SIGNAL, OUTPUT);
 
  // Startup: brief full-brightness sweep to confirm wiring,
  // then hold each LED at ~30% until first real data arrives.
  // This makes it obvious the circuit works without misleading
  // anyone about actual generation levels before data loads.
  int fuelPins[] = {PIN_COAL, PIN_GAS, PIN_HYDRO, PIN_SOLAR, PIN_WIND, PIN_OIL};
  for (int i = 0; i < 6; i++) {
    analogWrite(fuelPins[i], 0);
    delay(120);
  }
  Serial.println("READY");
}
 
// ── Main loop ────────────────────────────────────────────────────
void loop() {
  // Read incoming serial
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\n') {
      processCommand(inputBuffer);
      inputBuffer = "";
    } else {
      inputBuffer += c;
    }
  }
 
  // Drive signal LED blink sequence (non-blocking)
  updateSignalLED();
}
 
// ── Command processor ────────────────────────────────────────────
void processCommand(String json) {
  json.trim();
  if (json.length() == 0) return;
 
  StaticJsonDocument<512> doc;
  DeserializationError err = deserializeJson(doc, json);
 
  if (err) {
    Serial.print("{\"error\":\"json_parse\",\"msg\":\"");
    Serial.print(err.c_str());
    Serial.println("\"}");
    return;
  }
 
  const char* cmd = doc["cmd"];
  if (!cmd || strcmp(cmd, "DISPATCH") != 0) {
    Serial.println("{\"error\":\"unknown_cmd\"}");
    return;
  }
 
  // Update price signal
  const char* sig = doc["signal"];
  if (sig) priceSignal = String(sig);
 
  // Apply fuel LED levels — these stay at the written brightness
  // until the next dispatch command overwrites them.
  JsonObject levels = doc["levels"];
  if (!levels.isNull()) {
    applyLevel(PIN_COAL,  levels["coal"]  | 0.0f);
    applyLevel(PIN_GAS,   levels["gas"]   | 0.0f);
    applyLevel(PIN_HYDRO, levels["hydro"] | 0.0f);
    applyLevel(PIN_SOLAR, levels["solar"] | 0.0f);
    applyLevel(PIN_WIND,  levels["wind"]  | 0.0f);
    applyLevel(PIN_OIL,   levels["oil"]   | 0.0f);
  }
 
  // Trigger a new signal LED blink sequence.
  // Any in-progress sequence is cancelled and restarted.
  SignalConfig cfg = getSignalConfig(priceSignal);
  signalBlinksLeft = cfg.blinks;
  signalActive     = true;
  signalLedOn      = true;
  signalLastChange = millis();
  digitalWrite(PIN_SIGNAL, HIGH);   // start first pulse immediately
 
  // Acknowledge
  Serial.print("{\"ack\":\"DISPATCH\",\"signal\":\"");
  Serial.print(priceSignal);
  Serial.print("\",\"price\":");
  Serial.print((float)doc["price"], 2);
  Serial.println("}");
}
 
// ── Non-blocking signal LED sequencer ────────────────────────────
// Fires N blink pulses when a dispatch arrives, then goes dark.
void updateSignalLED() {
  if (!signalActive) return;
 
  unsigned long now = millis();
  SignalConfig cfg  = getSignalConfig(priceSignal);
 
  if (signalLedOn) {
    // LED is currently on — wait for on_ms, then turn off
    if (now - signalLastChange >= (unsigned long)cfg.on_ms) {
      digitalWrite(PIN_SIGNAL, LOW);
      signalLedOn      = false;
      signalLastChange = now;
      signalBlinksLeft--;
 
      if (signalBlinksLeft <= 0) {
        // Sequence complete — go dark until next dispatch
        signalActive = false;
      }
    }
  } else {
    // LED is off between pulses — wait for off_ms, then turn on again
    if (signalBlinksLeft > 0 && now - signalLastChange >= (unsigned long)cfg.off_ms) {
      digitalWrite(PIN_SIGNAL, HIGH);
      signalLedOn      = true;
      signalLastChange = now;
    }
  }
}
 