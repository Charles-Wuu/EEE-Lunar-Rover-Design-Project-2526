/**************************************************************************************************************************************
  EEE Rover drive + steering + probe servo control

  Web control follows the WiFiWebServer pattern used by monitor_worked_test.
  Serial control is kept for quick testing:
    W/S = motor forward/backward
    A/D = steering left/right
    X   = stop motor + steering center
    Q/E = probe servo +10/-10 degrees per command
    R   = probe servo reset to 90 degrees
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
// ======================================================================

const int MOTOR_EN  = 1;   // D1 -> EN
const int MOTOR_DIR = 2;   // D2 -> DIR
const int SERVO_PIN = 3;   // D3 -> steering SG90 signal wire
const int PROBE_SERVO_PIN = 4;   // D4 -> probe servo signal wire

Servo steeringServo;
Servo probeServo;


// ======================================================================
// Angles and state
// ======================================================================

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

// Swap HIGH & LOW if actual movement is reverse to expected
const int FORWARD_DIR = LOW;
const int BACKWARD_DIR = HIGH;


// ======================================================================
// Response helpers
// ======================================================================

void sendText(const __FlashStringHelper* text)
{
  server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
  server.sendHeader(F("Access-Control-Allow-Methods"), F("GET, OPTIONS"));
  server.send(200, F("text/plain"), text);
}


void sendText(const String& text)
{
  server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
  server.sendHeader(F("Access-Control-Allow-Methods"), F("GET, OPTIONS"));
  server.send(200, F("text/plain"), text);
}


void handleRoot()
{
  sendText(F("EEE Rover control server running"));
}


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
  json += "}";

  server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
  server.send(200, F("application/json"), json);
}


void handleNotFound()
{
  server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
  server.send(404, F("text/plain"), F("Not Found"));
}


// ======================================================================
// Motor and steering
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
// HTTP endpoints
// ======================================================================

void handleForward()
{
  motorForward();
  sendText(F("FORWARD"));
}


void handleBackward()
{
  motorBackward();
  sendText(F("BACKWARD"));
}


void handleStopMotor()
{
  stopMotor();
  sendText(F("MOTOR STOP"));
}


void handleLeft()
{
  steeringLeft();
  sendText(F("LEFT"));
}


void handleRight()
{
  steeringRight();
  sendText(F("RIGHT"));
}


void handleCenter()
{
  steeringCenter();
  sendText(F("CENTER"));
}


void handleStop()
{
  stopRover();
  sendText(F("STOP"));
}


void handleProbeDown()
{
  probeDown();
  sendText(String("PROBE ") + String(probeAngle));
}


void handleProbeUp()
{
  probeUp();
  sendText(String("PROBE ") + String(probeAngle));
}


void handleProbeReset()
{
  probeReset();
  sendText(F("PROBE 90"));
}


void ledON()
{
  digitalWrite(LED_BUILTIN, HIGH);
  sendText(F("ON"));
}


void ledOFF()
{
  digitalWrite(LED_BUILTIN, LOW);
  sendText(F("OFF"));
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
    Serial.println(F("Servo right 130"));
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
// Setup
// ======================================================================

void setup()
{
  Serial.begin(9600);
  delay(1500);

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  pinMode(MOTOR_EN, OUTPUT);
  pinMode(MOTOR_DIR, OUTPUT);

  steeringServo.attach(SERVO_PIN);
  steeringCenter();

  probeServo.attach(PROBE_SERVO_PIN);
  probeReset();

  stopMotor();

  Serial.println();
  Serial.println(F("EEE Rover control booting"));
  Serial.println(F("W/S = motor forward/backward"));
  Serial.println(F("A/D = steering left/right"));
  Serial.println(F("Q/E = probe -5/+5 degrees"));
  Serial.println(F("R = probe reset 90"));
  Serial.println(F("X = stop motor + steering center"));

  if (WiFi.status() == WL_NO_SHIELD)
  {
    Serial.println(F("WiFi shield not present"));
    Serial.println(F("Serial control still works."));
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
    Serial.println(F("Serial control still works."));
    return;
  }

  server.on(F("/"), handleRoot);
  server.on(F("/state"), handleState);

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
  if (wifiReady)
  {
    server.handleClient();
  }

  while (Serial.available() > 0)
  {
    handleSerialCommand((char)Serial.read());
  }
}
