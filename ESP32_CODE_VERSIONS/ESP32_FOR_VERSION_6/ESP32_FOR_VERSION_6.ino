#include <WiFi.h>
#include <WebServer.h>

// ================== WIFI SETTINGS ==================
const char* ssid = "Heshala";
const char* password = "12341234";

WebServer server(80);
bool connected = true;

// ================== ESP32 <-> ARDUINO SERIAL2 ==================
#define RXD2 16  // ESP32 RX2  <- Nano TX  (⚠️ use divider/level shifter)
#define TXD2 17  // ESP32 TX2  -> Nano RX

// ================== LOG BUFFER (ARDUINO -> ESP32 -> PYTHON) ==================
const int LOG_MAX = 200;
String logBuf[LOG_MAX];
volatile int logHead = 0;
volatile int logCount = 0;

void pushLog(const String &line) {
  if (line.length() == 0) return;
  logBuf[logHead] = line;
  logHead = (logHead + 1) % LOG_MAX;
  if (logCount < LOG_MAX) logCount++;
}

bool isSafeChar(char c) {
  // allow normal printable ASCII + tab
  return (c == '\t') || (c >= 32 && c <= 126);
}

void readArduinoLogs() {
  static String line = "";
  while (Serial2.available()) {
    char c = (char)Serial2.read();

    if (c == '\n') {
      line.replace("\r", "");
      line.trim();
      if (line.length() > 0) pushLog(line);
      line = "";
      continue;
    }

    // drop CR and any non-printable/control bytes
    if (c == '\r') continue;
    if (!isSafeChar(c)) continue;

    line += c;

    // safety cap
    if (line.length() > 600) line = "";
  }
}

void handleLogs() {
  // returns logs as JSON array of strings (safe for any line)
  String out = "[";
  int start = (logHead - logCount + LOG_MAX) % LOG_MAX;

  for (int i = 0; i < logCount; i++) {
    int idx = (start + i) % LOG_MAX;
    String s = logBuf[idx];
    s.replace("\\", "/");
    s.replace("\"", "\\\"");
    out += "\"" + s + "\"";
    if (i < logCount - 1) out += ",";
  }
  out += "]";
  server.send(200, "application/json", out);
}

// ================== SEND DATA TO ARDUINO ==================
void sendToArduino(String msg) {
  msg.replace("\r", "");
  msg.trim();

  if (msg.length() == 0) {              // ✅ STOP sending blank lines
    Serial.println("⚠️ IGNORE: empty cmd");
    return;
  }

  Serial2.println(msg);
  Serial.print("📤 SENT → ");
  Serial.println(msg);
}

// ================== HANDLE DATA FROM PYTHON ==================
void handleData() {

  if (!connected) {
    server.send(400, "text/plain", "ESP Disconnected");
    return;
  }

  if (!server.hasArg("mode")) {
    server.send(400, "text/plain", "Mode Missing");
    return;
  }

  String mode = server.arg("mode");
  mode.trim();

  // ************* AUTO MODE *************
  if (mode == "Auto") {

    if (!server.hasArg("loop_count") || !server.hasArg("wait_time")) {
      server.send(400, "text/plain", "Missing loop_count or wait_time");
      return;
    }

    String loopCount = server.arg("loop_count");
    String waitTime = server.arg("wait_time");
    loopCount.trim();
    waitTime.trim();

    String sendMsg = "AUTO,loop_count=" + loopCount + ",wait_time=" + waitTime;
    Serial.println("\n🤖 AUTO MODE");
    sendToArduino(sendMsg);
  }

  // ************* MANUAL MODE *************
  else if (mode == "Manual") {

    if (!server.hasArg("type")) {
      server.send(400, "text/plain", "Missing type");
      return;
    }

    String id = server.hasArg("id") ? server.arg("id") : "";
    String type = server.arg("type");
    id.trim();
    type.trim();

    Serial.println("\n🕹 MANUAL MODE");

    if (type == "Charge") {
      if (!server.hasArg("charge_time")) {
        server.send(400, "text/plain", "Missing charge_time");
        return;
      }

      String chargeTime = server.arg("charge_time");
      chargeTime.trim();

      String sendMsg = "MANUAL,ID:" + id + ",TYPE:Charge,CTIME:" + chargeTime;
      sendToArduino(sendMsg);
    }

    else if (type == "Trip") {

      if (!server.hasArg("drop_location") || !server.hasArg("wait_time") || !server.hasArg("loop_count")) {
        server.send(400, "text/plain", "Missing drop_location/wait_time/loop_count");
        return;
      }

      String drop = server.arg("drop_location");
      String waitTime = server.arg("wait_time");
      String loopCount = server.arg("loop_count");
      drop.trim(); waitTime.trim(); loopCount.trim();

      String sendMsg = "MANUAL,ID:" + id + ",TYPE:Trip,DROP:" + drop + ",WAIT:" + waitTime + ",LOOP:" + loopCount;
      sendToArduino(sendMsg);
    }
    else {
      server.send(400, "text/plain", "Unknown Manual type");
      return;
    }
  }

  // ************* EMERGENCY MODE *************
  else if (mode == "Emergency") {

    if (!server.hasArg("command")) {
      server.send(400, "text/plain", "Missing command");
      return;
    }

    String cmd = server.arg("command");
    cmd.trim();

    Serial.println("\n🛑 EMERGENCY MODE");
    Serial.println("Command: " + cmd);

    if (cmd.length() == 0) {
      server.send(400, "text/plain", "Empty command");
      return;
    }

    // 🔥 PID UPDATE SUPPORT
    if (cmd == "UPDATE_PID") {

      if (!server.hasArg("base") || !server.hasArg("kp") || !server.hasArg("kd")) {
        server.send(400, "text/plain", "Missing base/kp/kd");
        return;
      }

      String base = server.arg("base");
      String kp = server.arg("kp");
      String kd = server.arg("kd");
      base.trim(); kp.trim(); kd.trim();

      String sendMsg = "EMERGENCY,UPDATE_PID,BASE:" + base + ",KP:" + kp + ",KD:" + kd;
      sendToArduino(sendMsg);

    } else {
      String sendMsg = "EMERGENCY," + cmd;
      sendToArduino(sendMsg);
    }
  }

  else {
    server.send(400, "text/plain", "Unknown mode");
    return;
  }

  server.send(200, "text/plain", "✅ Data Received");
}

void handleDisconnect() {
  Serial.println("↩️ CHARGE CANCEL REQUEST");
  sendToArduino("MANUAL,TYPE:CHARGE_CANCEL");
  server.send(200, "text/plain", "Cancel Sent");
}

void handleRoot() {
  server.send(200, "text/plain", connected ? "Connected" : "Disconnected");
}

// ================== SETUP ==================
void setup() {

  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);

  delay(1500);
  Serial.println("\n🚀 ESP32 STARTING");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("📡 Connecting");
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 25) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ WIFI CONNECTED");
    Serial.print("📍 IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("❌ WIFI FAILED");
  }

  // ROUTES
  server.on("/", handleRoot);
  server.on("/send", handleData);
  server.on("/disconnect", handleDisconnect);
  server.on("/logs", HTTP_GET, handleLogs);

  server.begin();
  Serial.println("🌐 WEB SERVER STARTED");
}

// ================== LOOP ==================
void loop() {
  server.handleClient();
  readArduinoLogs();
}