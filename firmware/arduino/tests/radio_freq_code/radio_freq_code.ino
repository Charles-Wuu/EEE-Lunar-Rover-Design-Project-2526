// ============================================================
// EEERover Control Endpoints + Detection Serial Output
// RF version: standard UART, where HIGH = 1 and LOW = 0.
// ============================================================

#define USE_WIFI_NINA false
#define USE_WIFI101   true
#include <WiFiWebServer.h>


// ======================================================================
// WiFi
// ======================================================================

const char ssid[] = "YOUR_WIFI_SSID";
const char pass[] = "YOUR_WIFI_PASSWORD";
const int groupNumber = 0;

WiFiWebServer server(80);


// ======================================================================
// Motor pins
// ======================================================================

const int leften   = 0;
const int leftdir  = 1;
const int righten  = 2;
const int rightdir = 3;

const int FORWARD  = HIGH;
const int BACKWARD = LOW;

String currentMotor = "STOP";


// ======================================================================
// Detection pins
// ======================================================================

#define PIN_MAG_SENSOR A0
#define PIN_IR_SENSOR  4
#define PIN_US_SENSOR  8

// RF UART input on D6 — standard UART, 600 baud.
// Idle = HIGH, start bit = LOW, data: HIGH=1 LOW=0, LSB first.
#define PIN_RADIO_RX   6
#define RADIO_BAUD     600
#define RADIO_BIT_US   (1000000UL / RADIO_BAUD)   // 1667 µs per bit

#define AREF_VOLTAGE     3.3
#define V_THRESHOLD_HIGH 2.5
#define V_THRESHOLD_LOW  0.9


// ======================================================================
// Detection values and timing
// ======================================================================

int magRaw = 0;
float magVoltage = 0.0;
String magPolarity = "unknown";

volatile unsigned long irPulseCount = 0;
unsigned long irCount = 0;
float irLambda = 0.0;
String irClass = "low";
bool usDetected = false;

unsigned long lastIRTime        = 0;
unsigned long lastMagTime       = 0;
unsigned long lastUSTime        = 0;
unsigned long lastRadioPrintTime = 0;

const unsigned long IR_WINDOW_MS          = 2000;
const unsigned long MAG_INTERVAL_MS       = 500;
const unsigned long US_INTERVAL_MS        = 500;
const unsigned long RADIO_PRINT_INTERVAL_MS = 500;

String latestRadioPacket = "--";
String latestRadioAge    = "--";
String radioBuffer       = "";

// Edge-detection state for the RF pin (must outlive updateRadio calls)
bool rfPrevHigh = true;

bool wifiReady = false;
unsigned long lastHeartbeatTime = 0;
const unsigned long HEARTBEAT_INTERVAL_MS = 2000;


// ======================================================================
// Helper: send response with CORS
// ======================================================================

void sendText(const String& text)
{
  server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
  server.send(200, F("text/plain"), text);
}


// ======================================================================
// Root
// ======================================================================

void handleRoot()
{
  server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
  server.send(200, F("text/plain"), F("EEE Rover Arduino is running"));
}


// ======================================================================
// Rock classification
// ======================================================================

String classifyRock()
{
  bool irHigh = irLambda >= 430.0;

  if ( irHigh &&  usDetected && magPolarity == "down") return "Basaltoid";
  if (!irHigh && !usDetected && magPolarity == "down") return "Gravion";
  if (!irHigh &&  usDetected && magPolarity == "up")   return "Regolix";
  if ( irHigh && !usDetected && magPolarity == "up")   return "Lunarite";
  return "unknown";
}


// ======================================================================
// /data endpoint
// ======================================================================

void handleData()
{
  String json = "{";

  json += "\"mag\":{";
  json += "\"raw\":"      + String(magRaw)         + ",";
  json += "\"voltage\":"  + String(magVoltage, 2)  + ",";
  json += "\"polarity\":\"" + magPolarity           + "\"";
  json += "},";

  json += "\"ir\":{";
  json += "\"count\":"      + String(irCount)      + ",";
  json += "\"lambda_val\":" + String(irLambda, 1)  + ",";
  json += "\"ir_class\":\"" + irClass               + "\"";
  json += "},";

  json += "\"us\":{";
  json += "\"detected\":";
  json += usDetected ? "true" : "false";
  json += "},";

  json += "\"rf\":{";
  json += "\"packet\":\"" + latestRadioPacket + "\",";
  json += "\"age\":\""    + latestRadioAge    + "\"";
  json += "},";

  json += "\"rock_type\":\"" + classifyRock() + "\"";
  json += "}";

  server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
  server.send(200, F("application/json"), json);
}


// ======================================================================
// Motor functions
// ======================================================================

void stopMotors()
{
  digitalWrite(leften,  LOW);
  digitalWrite(righten, LOW);
}

void forward()
{
  digitalWrite(leftdir,  FORWARD);
  digitalWrite(rightdir, FORWARD);
  digitalWrite(leften,   HIGH);
  digitalWrite(righten,  HIGH);
  currentMotor = "FORWARD";
  sendText("FORWARD");
}

void backward()
{
  digitalWrite(leftdir,  BACKWARD);
  digitalWrite(rightdir, BACKWARD);
  digitalWrite(leften,   HIGH);
  digitalWrite(righten,  HIGH);
  currentMotor = "BACKWARD";
  sendText("BACKWARD");
}

void left()
{
  digitalWrite(rightdir, FORWARD);
  digitalWrite(leften,   LOW);
  digitalWrite(righten,  HIGH);
  currentMotor = "LEFT";
  sendText("LEFT");
}

void right()
{
  digitalWrite(leftdir, FORWARD);
  digitalWrite(leften,  HIGH);
  digitalWrite(righten, LOW);
  currentMotor = "RIGHT";
  sendText("RIGHT");
}

void spinLeft()
{
  digitalWrite(leftdir,  BACKWARD);
  digitalWrite(rightdir, FORWARD);
  digitalWrite(leften,   HIGH);
  digitalWrite(righten,  HIGH);
  currentMotor = "SPIN LEFT";
  sendText("SPIN LEFT");
}

void spinRight()
{
  digitalWrite(leftdir,  FORWARD);
  digitalWrite(rightdir, BACKWARD);
  digitalWrite(leften,   HIGH);
  digitalWrite(righten,  HIGH);
  currentMotor = "SPIN RIGHT";
  sendText("SPIN RIGHT");
}

void stopRover()
{
  stopMotors();
  currentMotor = "STOP";
  sendText("STOP");
}

void ledON()
{
  digitalWrite(LED_BUILTIN, HIGH);
  sendText("ON");
}

void ledOFF()
{
  digitalWrite(LED_BUILTIN, LOW);
  sendText("OFF");
}


// ======================================================================
// Detection: Magnetic
// ======================================================================

int readMagSmoothed()
{
  long sum = 0;
  for (int i = 0; i < 16; i++)
  {
    sum += analogRead(PIN_MAG_SENSOR);
    delay(2);
  }
  return sum / 16;
}

void updateMagnetic()
{
  unsigned long now = millis();
  if (now - lastMagTime < MAG_INTERVAL_MS) return;
  lastMagTime = now;

  magRaw     = readMagSmoothed();
  magVoltage = (magRaw / 1023.0) * AREF_VOLTAGE;

  if      (magVoltage > V_THRESHOLD_HIGH) magPolarity = "up";
  else if (magVoltage < V_THRESHOLD_LOW)  magPolarity = "down";
  else                                    magPolarity = "unknown";

  Serial.print("[MAG] Raw: ");    Serial.print(magRaw);
  Serial.print("  Voltage: ");    Serial.print(magVoltage, 2);
  Serial.print("V  Polarity: ");  Serial.println(magPolarity);
}


// ======================================================================
// Detection: IR pulse count
// ======================================================================

void countIRPulse()
{
  irPulseCount++;
}

void updateIR()
{
  unsigned long now = millis();
  if (now - lastIRTime < IR_WINDOW_MS) return;
  lastIRTime = now;

  noInterrupts();
  unsigned long count = irPulseCount;
  irPulseCount = 0;
  interrupts();

  float lambda = count / 2.0;
  irCount  = count;
  irLambda = lambda;
  irClass  = (lambda >= 430) ? "high" : "low";

  Serial.print("Pulse count in 2s: "); Serial.println(count);
  Serial.print("Lambda: ");             Serial.print(lambda, 1); Serial.println(" s^-1");
  Serial.println(lambda >= 430 ? "IR class: HIGH" : "IR class: LOW");
}


// ======================================================================
// Detection: Ultrasound
// ======================================================================

void updateUltrasound()
{
  unsigned long now = millis();
  if (now - lastUSTime < US_INTERVAL_MS) return;
  lastUSTime = now;

  usDetected = (digitalRead(PIN_US_SENSOR) == HIGH);
  Serial.println(usDetected ? "[US] detected" : "[US] absent");
}


// ======================================================================
// Detection: Radio UART ASCII  (600 baud, standard logic, 8N1, LSB first)
//
// FIX: Use falling-edge detection so readRadioByte() is only entered at
//      t=0 of the start bit, giving precise sample timing.
//
// Timing from falling edge:
//   +  BIT_US/2  → middle of start bit  → verify still LOW
//   +  BIT_US    → middle of bit 0      → sample
//   +  BIT_US    → middle of bit 1      → sample  … ×8
// ======================================================================

// Called immediately after a HIGH→LOW transition is detected.
// Returns the decoded byte, or -1 if the start bit was a glitch.
int readRadioByte()
{
  // Advance to the middle of the start bit and verify it is still LOW.
  delayMicroseconds(RADIO_BIT_US / 2);
  if (digitalRead(PIN_RADIO_RX) != LOW) return -1;   // glitch — ignore

  // Advance from the start-bit midpoint to the centre of data bit 0.
  delayMicroseconds(RADIO_BIT_US);

  byte value = 0;
  for (int bit = 0; bit < 8; bit++)
  {
    if (digitalRead(PIN_RADIO_RX) == HIGH)
    {
      value |= (1 << bit);          // standard UART: HIGH = logical 1
    }
    delayMicroseconds(RADIO_BIT_US);   // advance to centre of next bit
  }
  // We are now sitting inside the stop bit — no explicit wait needed.
  return (int)(unsigned char)value;
}

bool isValidRadioAgeChar(char c)
{
  return (c >= '0' && c <= '9') || c == '.';
}

bool isValidRadioAgePacket(const String& packet)
{
  if (packet.length() == 0 || packet.length() > 8) return false;

  bool hasDigit = false;
  bool hasDot   = false;

  for (unsigned int i = 0; i < packet.length(); i++)
  {
    char c = packet.charAt(i);
    if (c >= '0' && c <= '9')      { hasDigit = true; }
    else if (c == '.' && !hasDot)  { hasDot   = true; }
    else                           { return false; }
  }
  return hasDigit;
}

void updateRadio()
{
  // ── Falling-edge detection ───────────────────────────────────────────
  // Sample the pin once per loop iteration.  When we see the transition
  // HIGH→LOW we are at (or within one loop iteration of) t=0 of the
  // start bit, so readRadioByte() timing is correct.
  bool rfNowHigh  = (digitalRead(PIN_RADIO_RX) == HIGH);
  bool fallingEdge = rfPrevHigh && !rfNowHigh;
  rfPrevHigh = rfNowHigh;

  if (fallingEdge)
  {
    int incoming = readRadioByte();

    if (incoming >= 0)
    {
      char c = (char)incoming;

      if (c == '\n' || c == '\r')
      {
        radioBuffer.trim();
        if (isValidRadioAgePacket(radioBuffer))
        {
          latestRadioPacket = radioBuffer;
          latestRadioAge    = radioBuffer;
        }
        radioBuffer = "";
      }
      else if (isValidRadioAgeChar(c))
      {
        radioBuffer += c;
        if (radioBuffer.length() > 8) radioBuffer = "";
      }
      else
      {
        radioBuffer = "";   // unexpected char — discard and resync
      }
    }
  }

  // ── Periodic Serial print ───────────────────────────────────────────
  unsigned long now = millis();
  if (now - lastRadioPrintTime >= RADIO_PRINT_INTERVAL_MS)
  {
    lastRadioPrintTime = now;
    Serial.print("Latest packet: "); Serial.print(latestRadioPacket);
    Serial.print("    Age: ");        Serial.println(latestRadioAge);
  }
}


// ======================================================================
// 404
// ======================================================================

void handleNotFound()
{
  server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
  server.send(404, F("text/plain"), F("Not Found"));
}


// ======================================================================
// Setup
// ======================================================================

void setup()
{
  Serial.begin(9600);
  delay(1500);

  Serial.println();
  Serial.println(F("========================================"));
  Serial.println(F("EEE Rover booting"));
  Serial.println(F("Serial baud: 9600"));
  Serial.println(F("RF input: D6, standard UART, 600 baud"));
  Serial.println(F("========================================"));

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  pinMode(leften,  OUTPUT);
  pinMode(leftdir, OUTPUT);
  pinMode(righten, OUTPUT);
  pinMode(rightdir, OUTPUT);

  pinMode(PIN_IR_SENSOR,  INPUT);
  pinMode(PIN_US_SENSOR,  INPUT);
  pinMode(PIN_RADIO_RX,   INPUT);

  analogReadResolution(10);
  stopMotors();

  attachInterrupt(digitalPinToInterrupt(PIN_IR_SENSOR), countIRPulse, RISING);

  if (WiFi.status() == WL_NO_SHIELD)
  {
    Serial.println(F("WiFi shield not present — running sensors only."));
    return;
  }

  if (groupNumber)
  {
    WiFi.config(IPAddress(192, 168, 0, groupNumber + 1));
  }

  Serial.print(F("Connecting to SSID: "));
  Serial.println(ssid);

  unsigned long wifiStart = millis();
  while (WiFi.begin(ssid, pass) != WL_CONNECTED && millis() - wifiStart < 30000)
  {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println(F("WiFi timed out — running sensors only."));
    return;
  }

  server.on(F("/"),         handleRoot);
  server.on(F("/data"),     handleData);
  server.on(F("/on"),       ledON);
  server.on(F("/off"),      ledOFF);
  server.on(F("/forward"),  forward);
  server.on(F("/backward"), backward);
  server.on(F("/left"),     left);
  server.on(F("/right"),    right);
  server.on(F("/spinleft"), spinLeft);
  server.on(F("/spinright"),spinRight);
  server.on(F("/stop"),     stopRover);
  server.onNotFound(handleNotFound);

  server.begin();
  wifiReady = true;

  Serial.print(F("HTTP server started @ "));
  Serial.println(static_cast<IPAddress>(WiFi.localIP()));
}


// ======================================================================
// Loop
// ======================================================================

void loop()
{
  if (wifiReady) server.handleClient();

  updateMagnetic();
  updateIR();
  updateUltrasound();
  updateRadio();          // ← polls pin every iteration for edge detection

  unsigned long now = millis();
  if (now - lastHeartbeatTime >= HEARTBEAT_INTERVAL_MS)
  {
    lastHeartbeatTime = now;
    Serial.print(F("[HB] wifi="));   Serial.print(wifiReady ? F("on") : F("off"));
    Serial.print(F(" mag="));        Serial.print(magPolarity);
    Serial.print(F(" ir="));         Serial.print(irClass);
    Serial.print(F(" us="));         Serial.print(usDetected ? F("det") : F("abs"));
    Serial.print(F(" rf="));         Serial.println(latestRadioAge);
  }
}
