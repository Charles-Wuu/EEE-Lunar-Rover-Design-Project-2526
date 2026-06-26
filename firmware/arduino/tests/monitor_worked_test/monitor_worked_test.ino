/**************************************************************************************************************************************
  EEERover Detection Serial Output

  RF version:
    Hardware UART on D0/RX using Serial1 @ 600 baud.
    RF packet uses '#' as separator.

    Example:
      #123#456#789#

    Output:
      [RF packet] 123
      [RF packet] 456
      [RF packet] 789

    Logic is unchanged from the tested RF-only version.
 ***************************************************************************************************************************************/

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
// Detection pins
// ======================================================================

#define PIN_MAG_SENSOR A0
#define PIN_IR_SENSOR  9
#define PIN_US_SENSOR  6

// RF UART input:
// RF output -> D0 / RX
#define RF_BAUD 600

#define AREF_VOLTAGE 3.3

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

bool wifiReady = false;


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

  server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
  server.send(200, F("application/json"), json);
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

  Serial.print("[MAG] Raw: ");
  Serial.print(magRaw);
  Serial.print("  Voltage: ");
  Serial.print(magVoltage, 2);
  Serial.print("V  Polarity: ");
  Serial.println(magPolarity);
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

  Serial.print("Pulse count in 2s: ");
  Serial.println(irCount);

  Serial.print("Lambda: ");
  Serial.print(irLambda, 1);
  Serial.println(" s^-1");

  if (irLambda >= 430.0)
  {
    Serial.println("IR class: HIGH");
  }
  else
  {
    Serial.println("IR class: LOW");
  }
}


// ======================================================================
// Detection: Ultrasound
// ======================================================================

void updateUltrasound()
{
  unsigned long now = millis();

  if (now - lastUSTime < US_INTERVAL_MS)
  {
    return;
  }

  lastUSTime = now;

  int usValue = digitalRead(PIN_US_SENSOR);

  usDetected = usValue == HIGH;

  if (usDetected)
  {
    Serial.println("[US] detected");
  }
  else
  {
    Serial.println("[US] absent");
  }
}


// ======================================================================
// Detection: RF UART
// This logic is copied from the tested RF-only version.
// Do not change packet rule.
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

  Serial.print("[RF packet] ");
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

  // Any non-digit after '#' means the packet is not #DDD format.
  // Treat a new '#' as the start of a fresh packet; otherwise return to idle.
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

    Serial.print("[RF raw] ");

    if (c == '\n')
    {
      Serial.println("\\n");
    }
    else if (c == '\r')
    {
      Serial.println("\\r");
    }
    else
    {
      Serial.println(c);
    }

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
    Serial.println("[RF] no signal");
  }
  else
  {
    Serial.print("[Latest RF packet] ");
    Serial.println(latestRadioPacket);
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
  Serial.println(F("RF input: D0 / RX, Serial1 @ 600 baud"));
  Serial.println(F("RF packet rule: # starts packet, next # ends previous packet"));
  Serial.println(F("========================================"));

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  pinMode(PIN_IR_SENSOR, INPUT);
  pinMode(PIN_US_SENSOR, INPUT);

  analogReadResolution(10);

  Serial1.begin(RF_BAUD);

  Serial.println(F("\nStarting EEERover Detection"));
  Serial.println(F("RF mode: hardware UART on D0/RX"));

  attachInterrupt(digitalPinToInterrupt(PIN_IR_SENSOR), countIRPulse, RISING);

  if (WiFi.status() == WL_NO_SHIELD)
  {
    Serial.println(F("WiFi shield not present"));
    Serial.println(F("Continuing without network so Serial Monitor can still show sensor data."));
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
    Serial.println(F("Continuing without network so Serial Monitor can still show sensor data."));
    return;
  }

  server.on(F("/"), handleRoot);
  server.on(F("/data"), handleData);

  server.onNotFound(handleNotFound);

  server.begin();
  wifiReady = true;

  Serial.print(F("HTTP server started @ "));
  Serial.println(static_cast<IPAddress>(WiFi.localIP()));

  Serial.println(F("Detection started"));
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

  updateMagnetic();
  updateIR();
  updateUltrasound();
  updateRadio();
  printRFStatus();

  unsigned long now = millis();

  if (now - lastHeartbeatTime >= HEARTBEAT_INTERVAL_MS)
  {
    lastHeartbeatTime = now;

    Serial.print(F("[HEARTBEAT] wifi="));
    Serial.print(wifiReady ? F("on") : F("off"));

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
