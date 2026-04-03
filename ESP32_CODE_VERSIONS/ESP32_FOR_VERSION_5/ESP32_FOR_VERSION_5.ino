#include <WiFi.h>
#include <WebServer.h>

// ================== WIFI SETTINGS ==================
const char* ssid = "Heshala";
const char* password = "12341234";

WebServer server(80);
bool connected = true;

// ================== ESP32 <-> ARDUINO SERIAL2 ==================
#define RXD2 16  // ESP32 RX2  -> Arduino TX
#define TXD2 17  // ESP32 TX2  -> Arduino RX

// ================== LOG BUFFER (ARDUINO -> ESP32 -> PYTHON) ==================
const int LOG_MAX = 200;          // keep last 200 log lines
String logBuf[LOG_MAX];
volatile int logHead = 0;
volatile int logCount = 0;

void pushLog(const String &line) {
  logBuf[logHead] = line;
  logHead = (logHead + 1) % LOG_MAX;
  if (logCount < LOG_MAX) logCount++;
}

void readArduinoLogs() {
  static String line = "";
  while (Serial2.available()) {
    char c = (char)Serial2.read();
    if (c == '\n') {
      line.trim();
      if (line.length() > 0) {
        pushLog(line);
        // Serial.println("🧾 LOG ← " + line); // optional
      }
      line = "";
    } else {
      line += c;
      if (line.length() > 400) line = ""; // safety
    }
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

  // ************* AUTO MODE *************
  if (mode == "Auto") {

    String loopCount = server.arg("loop_count");
    String waitTime = server.arg("wait_time");

    Serial.println("\n🤖 AUTO MODE");

    String sendMsg =
      "AUTO,loop_count=" + loopCount + ",wait_time=" + waitTime;

    sendToArduino(sendMsg);
  }

  // ************* MANUAL MODE *************
  else if (mode == "Manual") {

    String id = server.arg("id");
    String type = server.arg("type");

    Serial.println("\n🕹 MANUAL MODE");

    if (type == "Charge") {
      String chargeTime = server.arg("charge_time");

      String sendMsg =
        "MANUAL,ID:" + id + ",TYPE:Charge,CTIME:" + chargeTime;

      sendToArduino(sendMsg);
    }

    else if (type == "Trip") {
      String drop = server.arg("drop_location");
      String waitTime = server.arg("wait_time");
      String loopCount = server.arg("loop_count");

      String sendMsg =
        "MANUAL,ID:" + id + ",TYPE:Trip,DROP:" + drop + ",WAIT:" + waitTime + ",LOOP:" + loopCount;

      sendToArduino(sendMsg);
    }
  }

  // ************* EMERGENCY MODE *************
  else if (mode == "Emergency") {

    String cmd = server.arg("command");

    Serial.println("\n🛑 EMERGENCY MODE");
    Serial.println("Command: " + cmd);

    // 🔥 PID UPDATE SUPPORT
    if (cmd == "UPDATE_PID") {

      String base = server.arg("base");
      String kp = server.arg("kp");
      String kd = server.arg("kd");

      Serial.println("⚙️ PID UPDATE RECEIVED");
      Serial.println("Base: " + base);
      Serial.println("KP  : " + kp);
      Serial.println("KD  : " + kd);

      String sendMsg =
        "EMERGENCY,UPDATE_PID,BASE:" + base + ",KP:" + kp + ",KD:" + kd;

      sendToArduino(sendMsg);
    } else {
      // Normal emergency commands
      String sendMsg = "EMERGENCY," + cmd;
      sendToArduino(sendMsg);
    }
  }

  else {
    Serial.println("❌ UNKNOWN MODE");
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
  Serial.println("Connected!");
  Serial.print("ESP32 IP Address: ");
  Serial.println(WiFi.localIP());   // 👈 meka thamai IP eka

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WIFI CONNECTED");
    Serial.print("📍 IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n❌ WIFI FAILED");
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
