#include <WiFi.h>
#include <WebServer.h>

// ================== WIFI SETTINGS ==================
const char* ssid = "Heshala";
const char* password = "12341234";

WebServer server(80);
bool connected = true;

// ================== ESP32 <-> ARDUINO SERIAL2 ==================
#define RXD2 16  // ESP32 RX2  <- Nano TX  (use divider/level shifter)
#define TXD2 17  // ESP32 TX2  -> Nano RX

// ================== LOG BUFFER (NANO -> ESP32 -> PYTHON) ==================
const int LOG_MAX = 200;
String logBuf[LOG_MAX];
volatile int logHead = 0;
volatile int logCount = 0;

static inline void pushLog(const String &line) {
  if (line.length() == 0) return;
  logBuf[logHead] = line;
  logHead = (logHead + 1) % LOG_MAX;
  if (logCount < LOG_MAX) logCount++;
}

static inline bool isSafeChar(char c) { return (c == '\t') || (c >= 32 && c <= 126); }

static void readArduinoLogs() {
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
    if (c == '\r') continue;
    if (!isSafeChar(c)) continue;

    line += c;
    if (line.length() > 600) line = "";
  }
}

static void handleLogs() {
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

// ================== BINARY FRAME SENDER ==================
static uint8_t crcXor(const uint8_t* data, size_t n) {
  uint8_t c = 0;
  for (size_t i = 0; i < n; i++) c ^= data[i];
  return c;
}
static void writeU16LE(uint8_t* p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static void sendFrame(uint8_t cmd, const uint8_t* payload, uint8_t len) {
  uint8_t hdr[3] = {0xA5, cmd, len};
  uint8_t crc = crcXor(hdr, 3);
  for (uint8_t i = 0; i < len; i++) crc ^= payload[i];

  Serial2.write(hdr, 3);
  if (len && payload) Serial2.write(payload, len);
  Serial2.write(&crc, 1);
}

// ================== HELPERS ==================
static uint8_t dropToId(const String &d) {
  if (d.length() == 0) return 1;
  char c = toupper(d.charAt(0));
  if (c == 'A') return 1;
  if (c == 'B') return 2;
  if (c == 'C') return 3;
  return 1;
}

static uint16_t toU16(const String &s, uint16_t defV=0) {
  long v = s.toInt();
  if (v < 0) v = 0;
  if (v > 65535) v = 65535;
  if (s.length() == 0) return defV;
  return (uint16_t)v;
}

// ================== HTTP HANDLERS ==================
static void handleRoot() {
  server.send(200, "text/plain", connected ? "Connected" : "Disconnected");
}

static void handleDisconnect() {
  // manual charge cancel
  sendFrame(4, nullptr, 0);
  server.send(200, "text/plain", "Cancel Sent");
}

static void handleData() {
  if (!connected) { server.send(400, "text/plain", "ESP Disconnected"); return; }
  if (!server.hasArg("mode")) { server.send(400, "text/plain", "Mode Missing"); return; }

  String mode = server.arg("mode"); mode.trim();

  // -------- AUTO --------
  if (mode == "Auto") {
    if (!server.hasArg("loop_count") || !server.hasArg("wait_time")) {
      server.send(400, "text/plain", "Missing loop_count or wait_time");
      return;
    }
    uint16_t loopCount = toU16(server.arg("loop_count"), 1);
    uint16_t waitMin   = toU16(server.arg("wait_time"), 0);

    uint8_t p[4];
    writeU16LE(&p[0], loopCount);
    writeU16LE(&p[2], waitMin);
    sendFrame(1, p, 4);

    server.send(200, "text/plain", "OK");
    return;
  }

  // -------- MANUAL --------
  if (mode == "Manual") {
    if (!server.hasArg("type")) { server.send(400, "text/plain", "Missing type"); return; }
    String type = server.arg("type"); type.trim();

    if (type == "Charge") {
      if (!server.hasArg("charge_time")) { server.send(400, "text/plain", "Missing charge_time"); return; }
      uint16_t cmin = toU16(server.arg("charge_time"), 1);

      uint8_t p[2];
      writeU16LE(&p[0], cmin);
      sendFrame(3, p, 2);

      server.send(200, "text/plain", "OK");
      return;
    }

    if (type == "Trip") {
      if (!server.hasArg("drop_location") || !server.hasArg("wait_time") || !server.hasArg("loop_count")) {
        server.send(400, "text/plain", "Missing drop_location/wait_time/loop_count");
        return;
      }

      uint8_t dropId  = dropToId(server.arg("drop_location"));
      uint16_t waitMin = toU16(server.arg("wait_time"), 0);
      uint16_t loops   = toU16(server.arg("loop_count"), 1);

      uint8_t p[5];
      p[0] = dropId;
      writeU16LE(&p[1], waitMin);
      writeU16LE(&p[3], loops);
      sendFrame(2, p, 5);

      server.send(200, "text/plain", "OK");
      return;
    }

    server.send(400, "text/plain", "Unknown Manual type");
    return;
  }

  // -------- EMERGENCY --------
  if (mode == "Emergency") {
    if (!server.hasArg("command")) { server.send(400, "text/plain", "Missing command"); return; }
    String cmd = server.arg("command"); cmd.trim();

    if (cmd == "AGV_STOP")       { sendFrame(10, nullptr, 0); server.send(200, "text/plain", "OK"); return; }
    if (cmd == "AGV_START")      { sendFrame(11, nullptr, 0); server.send(200, "text/plain", "OK"); return; }
    if (cmd == "GRIP_ENGAGE")    { sendFrame(12, nullptr, 0); server.send(200, "text/plain", "OK"); return; }
    if (cmd == "GRIP_DISENGAGE") { sendFrame(13, nullptr, 0); server.send(200, "text/plain", "OK"); return; }

    if (cmd == "UPDATE_PID") {
      if (!server.hasArg("base") || !server.hasArg("kp") || !server.hasArg("kd")) {
        server.send(400, "text/plain", "Missing base/kp/kd");
        return;
      }
      uint16_t base = toU16(server.arg("base"), 60);
      uint16_t kp   = toU16(server.arg("kp"), 2);
      uint16_t kd   = toU16(server.arg("kd"), 12);

      uint8_t p[6];
      writeU16LE(&p[0], base);
      writeU16LE(&p[2], kp);
      writeU16LE(&p[4], kd);
      sendFrame(20, p, 6);

      server.send(200, "text/plain", "OK");
      return;
    }

    server.send(400, "text/plain", "Unknown emergency command");
    return;
  }

  server.send(400, "text/plain", "Unknown mode");
}

// ================== SETUP / LOOP ==================
void setup() {
  Serial.begin(9600);
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 25) {
    delay(500);
    attempts++;
  }

  server.on("/", handleRoot);
  server.on("/send", handleData);
  server.on("/disconnect", handleDisconnect);
  server.on("/logs", HTTP_GET, handleLogs);
  server.begin();
}

void loop() {
  server.handleClient();
  readArduinoLogs();
}