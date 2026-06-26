/**************************************************************************************************************************************
  EEE Rover combined control + detection server

  Combined from:
    - control_final_worked.ino
    - monitor_worked_test.ino

  Web endpoints:
    Control:
      /state
      /forward /backward /stopMotor
      /left /right /center /stop
      /probeDown /probeUp /probeReset
      /on /off

    Detection:
      /data

  Pin plan, avoiding D5, D7 and D10:
    RF input:        D0 / RX, Serial1 @ 600 baud
    Motor EN:        D8
    Motor DIR:       D2
    Steering servo:  D3
    Probe servo:     D4
    Magnetic sensor: A0
    IR sensor:       D9
    Ultrasound:      D6
 ***************************************************************************************************************************************/

#include <Servo.h>

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
bool wifiReady = false;


// ======================================================================
// Pins
// D5, D7 and D10 are intentionally unused.
// ======================================================================

const int MOTOR_EN  = 8;   // moved off D1 to avoid Serial1/TX interaction
const int MOTOR_DIR = 2;
const int SERVO_PIN = 3;
const int PROBE_SERVO_PIN = 4;

#define PIN_MAG_SENSOR A0
#define PIN_IR_SENSOR  9
#define PIN_US_SENSOR  6

// RF output -> D0 / RX
#define RF_BAUD 600


// ======================================================================
// Rover control state
// ======================================================================

Servo steeringServo;
Servo probeServo;

const int LEFT_ANGLE = 45;
const int CENTER_ANGLE = 90;
const int RIGHT_ANGLE = 135;

const int PROBE_MIN_ANGLE = 0;
const int PROBE_CENTER_ANGLE = 90;
const int PROBE_MAX_ANGLE = 180;
const int PROBE_STEP = 10;

int probeAngle = PROBE_CENTER_ANGLE;
String currentMotor = "STOP";
String currentSteering = "CENTER";

// Swap HIGH & LOW if actual movement is reverse to expected.
const int FORWARD_DIR = LOW;
const int BACKWARD_DIR = HIGH;


// ======================================================================
// Detection values and timing
// ======================================================================

#define AREF_VOLTAGE 3.3

#define MAG_UNKNOWN_CENTER_V 2.34
#define MAG_UNKNOWN_HALF_WIDTH_V 0.1
#define V_THRESHOLD_HIGH (MAG_UNKNOWN_CENTER_V + MAG_UNKNOWN_HALF_WIDTH_V)
#define V_THRESHOLD_LOW  (MAG_UNKNOWN_CENTER_V - MAG_UNKNOWN_HALF_WIDTH_V)

int magRaw = 0;
float magVoltage = 0.0;
String magPolarity = "unknown";

volatile unsigned long irPulseCount = 0;
unsigned long irCount = 0;
float irLambda = 0.0;
String irClass = "low";
bool usDetected = false;

unsigned long lastIRTime = 0;
unsigned long lastMagTime = 0;
unsigned long lastUSTime = 0;
unsigned long lastRadioPrintTime = 0;
unsigned long lastHeartbeatTime = 0;
unsigned long lastRFCharTime = 0;

const unsigned long IR_WINDOW_MS = 2000;
const unsigned long MAG_INTERVAL_MS = 500;
const unsigned long US_INTERVAL_MS = 500;
const unsigned long RADIO_PRINT_INTERVAL_MS = 1000;
const unsigned long HEARTBEAT_INTERVAL_MS = 2000;
const unsigned long NO_SIGNAL_TIMEOUT_MS = 2000;

String radioBuffer = "";
String latestRadioPacket = "NO SIGNAL";
String latestRadioAge = "NO SIGNAL";
bool radioReceiving = false;

enum RoverMode
{
  MODE_IDLE,
  MODE_CONTROL,
  MODE_DETECT
};

RoverMode activeMode = MODE_IDLE;
bool detectionHardwareActive = false;


const char* modeNameText()
{
  if (activeMode == MODE_DETECT)
  {
    return "detect";
  }

  if (activeMode == MODE_CONTROL)
  {
    return "control";
  }

  return "idle";
}


void resetRadioParser();
void countIRPulse();


// ======================================================================
// Response helpers
// ======================================================================

void addCorsHeaders()
{
  server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
  server.sendHeader(F("Access-Control-Allow-Methods"), F("GET, OPTIONS"));
}


void sendText(const __FlashStringHelper* text)
{
  addCorsHeaders();
  server.send(200, F("text/plain"), text);
}


void sendText(const String& text)
{
  addCorsHeaders();
  server.send(200, F("text/plain"), text);
}


// ======================================================================
// Shared endpoints
// ======================================================================

void handleRoot()
{
  sendText(F("EEE Rover combined control + detection server running"));
}


void handleNotFound()
{
  addCorsHeaders();
  server.send(404, F("text/plain"), F("Not Found"));
}


// ======================================================================
// Control state endpoint
// ======================================================================

void handleState()
{
  String json = "{";
  json += "\"motor\":\"";
  json += currentMotor;
  json += "\",";
  json += "\"steering\":\"";
  json += currentSteering;
  json += "\",";
  json += "\"probe_angle\":";
  json += String(probeAngle);
  json += ",";
  json += "\"mode\":\"";
  json += modeNameText();
  json += "\"";
  json += "}";

  addCorsHeaders();
  server.send(200, F("application/json"), json);
}


// ======================================================================
// Motor, steering and probe
// ======================================================================

void motorForward()
{
  digitalWrite(MOTOR_DIR, FORWARD_DIR);
  digitalWrite(MOTOR_EN, HIGH);
  currentMotor = "FORWARD";
}


void motorBackward()
{
  digitalWrite(MOTOR_DIR, BACKWARD_DIR);
  digitalWrite(MOTOR_EN, HIGH);
  currentMotor = "BACKWARD";
}


void stopMotor()
{
  digitalWrite(MOTOR_EN, LOW);
  currentMotor = "STOP";
}


void steeringLeft()
{
  steeringServo.write(LEFT_ANGLE);
  currentSteering = "LEFT";
}


void steeringRight()
{
  steeringServo.write(RIGHT_ANGLE);
  currentSteering = "RIGHT";
}


void steeringCenter()
{
  steeringServo.write(CENTER_ANGLE);
  currentSteering = "CENTER";
}


void stopRover()
{
  stopMotor();
  steeringCenter();
}


void probeDown()
{
  probeAngle = max(PROBE_MIN_ANGLE, probeAngle + PROBE_STEP);
  probeServo.write(probeAngle);
}


void probeUp()
{
  probeAngle = min(PROBE_MAX_ANGLE, probeAngle - PROBE_STEP);
  probeServo.write(probeAngle);
}


void probeReset()
{
  probeAngle = PROBE_CENTER_ANGLE;
  probeServo.write(probeAngle);
}


// ======================================================================
// Mode switching
// ======================================================================

void stopDetectionHardware()
{
  if (!detectionHardwareActive)
  {
    return;
  }

  detachInterrupt(digitalPinToInterrupt(PIN_IR_SENSOR));
  Serial1.end();
  resetRadioParser();
  detectionHardwareActive = false;
}


void startDetectionHardware()
{
  if (detectionHardwareActive)
  {
    return;
  }

  irPulseCount = 0;
  lastIRTime = millis();
  lastRFCharTime = 0;
  latestRadioPacket = "NO SIGNAL";
  latestRadioAge = "NO SIGNAL";
  resetRadioParser();

  Serial1.begin(RF_BAUD);
  attachInterrupt(digitalPinToInterrupt(PIN_IR_SENSOR), countIRPulse, RISING);
  detectionHardwareActive = true;
}


void activateControlMode()
{
  if (activeMode == MODE_CONTROL && !detectionHardwareActive)
  {
    return;
  }

  stopDetectionHardware();

  if (!steeringServo.attached())
  {
    steeringServo.attach(SERVO_PIN);
  }

  if (!probeServo.attached())
  {
    probeServo.attach(PROBE_SERVO_PIN);
  }

  steeringServo.write(CENTER_ANGLE);
  probeServo.write(probeAngle);
  currentSteering = "CENTER";
  activeMode = MODE_CONTROL;

  Serial.println(F("[MODE] control"));
}


void activateDetectMode()
{
  if (activeMode == MODE_DETECT && detectionHardwareActive)
  {
    return;
  }

  stopRover();

  if (steeringServo.attached())
  {
    steeringServo.detach();
  }

  if (probeServo.attached())
  {
    probeServo.detach();
  }

  startDetectionHardware();
  activeMode = MODE_DETECT;

  Serial.println(F("[MODE] detect"));
}


void handleModeControl()
{
  activateControlMode();
  sendText(F("MODE control"));
}


void handleModeDetect()
{
  activateDetectMode();
  sendText(F("MODE detect"));
}


void handleModeStatus()
{
  String json = "{";
  json += "\"mode\":\"";
  json += modeNameText();
  json += "\",";
  json += "\"detection_active\":";
  json += detectionHardwareActive ? "true" : "false";
  json += "}";

  addCorsHeaders();
  server.send(200, F("application/json"), json);
}


bool requireControlMode()
{
  if (activeMode == MODE_CONTROL)
  {
    return true;
  }

  sendText(F("CONTROL MODE OFF"));
  return false;
}


void handleForward()
{
  if (!requireControlMode()) return;
  motorForward();
  sendText(F("FORWARD"));
}


void handleBackward()
{
  if (!requireControlMode()) return;
  motorBackward();
  sendText(F("BACKWARD"));
}


void handleStopMotor()
{
  if (!requireControlMode()) return;
  stopMotor();
  sendText(F("MOTOR STOP"));
}


void handleLeft()
{
  if (!requireControlMode()) return;
  steeringLeft();
  sendText(F("LEFT"));
}


void handleRight()
{
  if (!requireControlMode()) return;
  steeringRight();
  sendText(F("RIGHT"));
}


void handleCenter()
{
  if (!requireControlMode()) return;
  steeringCenter();
  sendText(F("CENTER"));
}


void handleStop()
{
  if (!requireControlMode()) return;
  stopRover();
  sendText(F("STOP"));
}


void handleProbeDown()
{
  if (!requireControlMode()) return;
  probeDown();
  sendText(String("PROBE ") + String(probeAngle));
}


void handleProbeUp()
{
  if (!requireControlMode()) return;
  probeUp();
  sendText(String("PROBE ") + String(probeAngle));
}


void handleProbeReset()
{
  if (!requireControlMode()) return;
  probeReset();
  sendText(F("PROBE 90"));
}


void ledON()
{
  if (!requireControlMode()) return;
  digitalWrite(LED_BUILTIN, HIGH);
  sendText(F("ON"));
}


void ledOFF()
{
  if (!requireControlMode()) return;
  digitalWrite(LED_BUILTIN, LOW);
  sendText(F("OFF"));
}


// ======================================================================
// Detection classification and data endpoint
// ======================================================================

String classifyRock()
{
  bool irHigh = irLambda >= 430.0;

  if (irHigh && usDetected && magPolarity == "down")
  {
    return "Basaltoid";
  }

  if (!irHigh && !usDetected && magPolarity == "down")
  {
    return "Gravion";
  }

  if (!irHigh && usDetected && magPolarity == "up")
  {
    return "Regolix";
  }

  if (irHigh && !usDetected && magPolarity == "up")
  {
    return "Lunarite";
  }

  return "unknown";
}


void handleData()
{
  String json = "{";

  json += "\"mag\":{";
  json += "\"raw\":" + String(magRaw) + ",";
  json += "\"voltage\":" + String(magVoltage, 2) + ",";
  json += "\"polarity\":\"" + magPolarity + "\"";
  json += "},";

  json += "\"ir\":{";
  json += "\"count\":" + String(irCount) + ",";
  json += "\"lambda_val\":" + String(irLambda, 1) + ",";
  json += "\"ir_class\":\"" + irClass + "\"";
  json += "},";

  json += "\"us\":{";
  json += "\"detected\":";
  json += usDetected ? "true" : "false";
  json += "},";

  bool rfNoSignal = lastRFCharTime == 0 || millis() - lastRFCharTime > NO_SIGNAL_TIMEOUT_MS;

  json += "\"rf\":{";
  json += "\"packet\":\"";
  json += rfNoSignal ? "NO SIGNAL" : latestRadioPacket;
  json += "\",";
  json += "\"age\":\"";
  json += rfNoSignal ? "NO SIGNAL" : latestRadioAge;
  json += "\",";
  json += "\"status\":\"";
  json += rfNoSignal ? "no_signal" : "live";
  json += "\"";
  json += "},";

  json += "\"rock_type\":\"" + classifyRock() + "\"";
  json += "}";

  addCorsHeaders();
  server.send(200, F("application/json"), json);
}


// ======================================================================
// Detection: magnetic
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

  if (now - lastMagTime < MAG_INTERVAL_MS)
  {
    return;
  }

  lastMagTime = now;

  magRaw = readMagSmoothed();
  magVoltage = (magRaw / 1023.0) * AREF_VOLTAGE;

  if (magVoltage > V_THRESHOLD_HIGH)
  {
    magPolarity = "up";
  }
  else if (magVoltage < V_THRESHOLD_LOW)
  {
    magPolarity = "down";
  }
  else
  {
    magPolarity = "unknown";
  }
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

  if (now - lastIRTime < IR_WINDOW_MS)
  {
    return;
  }

  lastIRTime = now;

  noInterrupts();
  unsigned long count = irPulseCount;
  irPulseCount = 0;
  interrupts();

  irCount = count;
  irLambda = count / 2.0;

  if (irLambda >= 430.0)
  {
    irClass = "high";
  }
  else
  {
    irClass = "low";
  }

  Serial.print(F("[IR] count="));
  Serial.print(irCount);
  Serial.print(F(" lambda="));
  Serial.print(irLambda, 1);
  Serial.print(F(" s^-1 class="));
  Serial.println(irClass);
}


// ======================================================================
// Detection: ultrasound
// ======================================================================

void updateUltrasound()
{
  unsigned long now = millis();

  if (now - lastUSTime < US_INTERVAL_MS)
  {
    return;
  }

  lastUSTime = now;
  usDetected = digitalRead(PIN_US_SENSOR) == HIGH;
}


// ======================================================================
// Detection: RF UART
// This keeps the tested #DDD packet rule.
// ======================================================================

void resetRadioParser()
{
  radioBuffer = "";
  radioReceiving = false;
}


void publishRadioPacket()
{
  String digits = radioBuffer.substring(1);

  latestRadioPacket = digits;
  latestRadioAge = digits;

  Serial.print(F("[RF packet] "));
  Serial.println(latestRadioPacket);
}


void processRadioChar(char c)
{
  lastRFCharTime = millis();

  if (!radioReceiving)
  {
    if (c == '#')
    {
      radioBuffer = "#";
      radioReceiving = true;
    }

    return;
  }

  if (c >= '0' && c <= '9')
  {
    radioBuffer += c;

    if (radioBuffer.length() == 4)
    {
      publishRadioPacket();
      resetRadioParser();
    }

    return;
  }

  if (c == '#')
  {
    radioBuffer = "#";
    radioReceiving = true;
  }
  else
  {
    resetRadioParser();
  }
}


void updateRadio()
{
  while (Serial1.available() > 0)
  {
    char c = (char)Serial1.read();
    processRadioChar(c);
  }
}


void printRFStatus()
{
  unsigned long now = millis();

  if (now - lastRadioPrintTime < RADIO_PRINT_INTERVAL_MS)
  {
    return;
  }

  lastRadioPrintTime = now;

  if (lastRFCharTime == 0 || now - lastRFCharTime > NO_SIGNAL_TIMEOUT_MS)
  {
    Serial.println(F("[RF] no signal"));
  }
  else
  {
    Serial.print(F("[Latest RF packet] "));
    Serial.println(latestRadioPacket);
  }
}


// ======================================================================
// Serial commands
// ======================================================================

void handleSerialCommand(char cmd)
{
  if (cmd == 'w' || cmd == 'W')
  {
    motorForward();
    Serial.println(F("Motor forward"));
  }
  else if (cmd == 's' || cmd == 'S')
  {
    motorBackward();
    Serial.println(F("Motor backward"));
  }
  else if (cmd == 'a' || cmd == 'A')
  {
    steeringLeft();
    Serial.println(F("Servo left 45"));
  }
  else if (cmd == 'd' || cmd == 'D')
  {
    steeringRight();
    Serial.println(F("Servo right 135"));
  }
  else if (cmd == 'q' || cmd == 'Q')
  {
    probeDown();
    Serial.print(F("Probe servo "));
    Serial.println(probeAngle);
  }
  else if (cmd == 'e' || cmd == 'E')
  {
    probeUp();
    Serial.print(F("Probe servo "));
    Serial.println(probeAngle);
  }
  else if (cmd == 'r' || cmd == 'R')
  {
    probeReset();
    Serial.println(F("Probe servo reset 90"));
  }
  else if (cmd == 'x' || cmd == 'X')
  {
    stopRover();
    Serial.println(F("Stop motor, servo center"));
  }
}


// ======================================================================
// Setup helpers
// ======================================================================

void registerRoutes()
{
  server.on(F("/"), handleRoot);

  server.on(F("/state"), handleState);
  server.on(F("/data"), handleData);

  server.on(F("/mode/control"), handleModeControl);
  server.on(F("/mode/detect"), handleModeDetect);
  server.on(F("/mode/status"), handleModeStatus);

  server.on(F("/on"), ledON);
  server.on(F("/off"), ledOFF);

  server.on(F("/forward"), handleForward);
  server.on(F("/backward"), handleBackward);
  server.on(F("/stopMotor"), handleStopMotor);
  server.on(F("/left"), handleLeft);
  server.on(F("/right"), handleRight);
  server.on(F("/center"), handleCenter);
  server.on(F("/stop"), handleStop);

  server.on(F("/probeDown"), handleProbeDown);
  server.on(F("/probeUp"), handleProbeUp);
  server.on(F("/probeReset"), handleProbeReset);

  server.onNotFound(handleNotFound);
}


void connectWiFi()
{
  if (WiFi.status() == WL_NO_SHIELD)
  {
    Serial.println(F("WiFi shield not present"));
    Serial.println(F("Serial control and sensor logging still work."));
    return;
  }

  if (groupNumber)
  {
    WiFi.config(IPAddress(192, 168, 0, groupNumber + 1));
  }

  Serial.print(F("Connecting to WPA SSID: "));
  Serial.println(ssid);

  unsigned long wifiStartTime = millis();

  while (WiFi.begin(ssid, pass) != WL_CONNECTED && millis() - wifiStartTime < 30000)
  {
    delay(500);
    Serial.print('.');
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println();
    Serial.println(F("WiFi connection timed out."));
    Serial.println(F("Serial control and sensor logging still work."));
    return;
  }

  registerRoutes();
  server.begin();
  wifiReady = true;

  Serial.print(F("HTTP server started @ "));
  Serial.println(static_cast<IPAddress>(WiFi.localIP()));
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
  Serial.println(F("EEE Rover combined control + detection booting"));
  Serial.println(F("Pins D5, D7 and D10 are unused."));
  Serial.println(F("RF input: D0 / RX, Serial1 @ 600 baud"));
  Serial.println(F("========================================"));

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  pinMode(MOTOR_EN, OUTPUT);
  pinMode(MOTOR_DIR, OUTPUT);
  stopMotor();

  steeringServo.attach(SERVO_PIN);
  steeringCenter();

  probeServo.attach(PROBE_SERVO_PIN);
  probeReset();
  steeringServo.detach();
  probeServo.detach();

  pinMode(PIN_IR_SENSOR, INPUT);
  pinMode(PIN_US_SENSOR, INPUT);

  analogReadResolution(10);

  connectWiFi();
}


// ======================================================================
// Loop
// ======================================================================

void loop()
{
  if (wifiReady)
  {
    server.handleClient();
  }

  while (activeMode == MODE_CONTROL && Serial.available() > 0)
  {
    handleSerialCommand((char)Serial.read());
  }

  if (activeMode == MODE_DETECT)
  {
    updateMagnetic();
    updateIR();
    updateUltrasound();
    updateRadio();
    printRFStatus();
  }

  unsigned long now = millis();

  if (now - lastHeartbeatTime >= HEARTBEAT_INTERVAL_MS)
  {
    lastHeartbeatTime = now;

    Serial.print(F("[HEARTBEAT] wifi="));
    Serial.print(wifiReady ? F("on") : F("off"));

    Serial.print(F(" motor="));
    Serial.print(currentMotor);

    Serial.print(F(" steering="));
    Serial.print(currentSteering);

    Serial.print(F(" probe="));
    Serial.print(probeAngle);

    Serial.print(F(" mag="));
    Serial.print(magPolarity);

    Serial.print(F(" ir="));
    Serial.print(irClass);

    Serial.print(F(" us="));
    Serial.print(usDetected ? F("detected") : F("absent"));

    Serial.print(F(" rf="));
    if (lastRFCharTime == 0 || now - lastRFCharTime > NO_SIGNAL_TIMEOUT_MS)
    {
      Serial.print(F("no signal"));
    }
    else
    {
      Serial.print(latestRadioPacket);
    }

    Serial.print(F(" rock="));
    Serial.println(classifyRock());
  }
}
