#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "Heshala";
const char* password = "12341234";

WebServer server(80);

#define RXD2 16
#define TXD2 17

// ================= ULTRASONIC (ESP32 PINS) =================
#define US_TRIG 25
#define US_ECHO 26

// Tune these
static const int  OBS_STOP_CM  = 20;   // stop if <= 20cm
static const int  OBS_CLEAR_CM = 25;   // resume if >= 25cm (hysteresis)
static const uint32_t US_POLL_MS = 80; // read rate
static const uint32_t PULSE_TIMEOUT_US = 25000; // 25ms timeout (~4m)

static bool obstacleActive = false;
static uint32_t lastUsMs = 0;

// ---------------- Mission lock ----------------
enum BusyMode { BUSY_NONE, BUSY_AUTO, BUSY_MANUAL_TRIP, BUSY_MANUAL_CHARGE };
volatile BusyMode busy = BUSY_NONE;

// ---------------- Log buffer ----------------
const int LOG_MAX = 200;
String logBuf[LOG_MAX];
volatile int logHead = 0;
volatile int logCount = 0;

static inline void pushLog(const String &line) {
  if (!line.length()) return;
  logBuf[logHead] = line;
  logHead = (logHead + 1) % LOG_MAX;
  if (logCount < LOG_MAX) logCount++;
}

// Small helper to add JSON log lines (so Python live log window shows it)
static void pushJsonLog(const char* tag, const String &msg) {
  String safe = msg;
  safe.replace("\\", "/");
  safe.replace("\"", "'");
  String line = "{\"t\":" + String(millis()) + ",\"tag\":\"" + String(tag) + "\",\"msg\":\"" + safe + "\"}";
  pushLog(line);
}

static void setBusyFromLog(const String &line) {
  if (line.indexOf("\"tag\":\"MODE\"") >= 0) {
    if (line.indexOf("AUTO") >= 0) busy = BUSY_AUTO;
    else if (line.indexOf("TRIP") >= 0) busy = BUSY_MANUAL_TRIP;
    else if (line.indexOf("CHG") >= 0) busy = BUSY_MANUAL_CHARGE;
  }
  if (line.indexOf("\"tag\":\"STATE\"") >= 0) {
    if (line.indexOf("RESET_DONE") >= 0) busy = BUSY_NONE;
    if (line.indexOf("CHG DONE") >= 0) busy = BUSY_NONE;
    if (line.indexOf("MANUAL TRIP FINISHED") >= 0) busy = BUSY_NONE;
    if (line.indexOf("AUTO FINISHED") >= 0) busy = BUSY_NONE;
  }
}

static void readArduinoLogs() {
  static String line = "";
  while (Serial2.available()) {
    char c = (char)Serial2.read();
    if (c == '\n') {
      line.replace("\r", "");
      line.trim();
      if (line.length()) {
        pushLog(line);
        setBusyFromLog(line);
      }
      line = "";
      continue;
    }
    if (c == '\r') continue;
    if (c < 32 || c > 126) continue;
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

// ---------------- Frame sender (NO CHANGE) ----------------
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

static uint16_t toU16(const String &s, uint16_t defV=0) {
  if (!s.length()) return defV;
  long v = s.toInt();
  if (v < 0) v = 0;
  if (v > 65535) v = 65535;
  return (uint16_t)v;
}
static uint8_t dropToId(const String &d) {
  if (!d.length()) return 1;
  char c = toupper(d.charAt(0));
  if (c == 'A') return 1;
  if (c == 'B') return 2;
  if (c == 'C') return 3;
  return 1;
}

// ================= ULTRASONIC FUNCTIONS =================
static int readDistanceCm() {
  digitalWrite(US_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(US_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(US_TRIG, LOW);

  unsigned long dur = pulseIn(US_ECHO, HIGH, PULSE_TIMEOUT_US);
  if (dur == 0) return -1; // timeout / no echo
  // cm ≈ us / 58
  int cm = (int)(dur / 58UL);
  return cm;
}

// ✅ 1-byte int commands to Nano (ultra short)
// 0xF1 = obstacle stop
// 0xF0 = obstacle clear
static void sendObstacleByte(bool stopNow) {
  uint8_t b = stopNow ? 0xF1 : 0xF0;
  Serial2.write(&b, 1);
}

// ---------------- HTTP endpoints ----------------
static void handleRoot() {
  server.send(200, "text/plain", "Connected");
}

static void handleState() {
  String mode = "NONE";
  if (busy == BUSY_AUTO) mode = "AUTO";
  else if (busy == BUSY_MANUAL_TRIP) mode = "MANUAL_TRIP";
  else if (busy == BUSY_MANUAL_CHARGE) mode = "MANUAL_CHARGE";

  String out = "{\"busy\":";
  out += (busy == BUSY_NONE ? "false" : "true");
  out += ",\"mode\":\"" + mode + "\"}";
  server.send(200, "application/json", out);
}

static void handleReset() {
  sendFrame(30, nullptr, 0);
  busy = BUSY_NONE;
  server.send(200, "text/plain", "RESET_SENT");
}

static void handleDisconnect() {
  sendFrame(4, nullptr, 0);
  server.send(200, "text/plain", "Cancel Sent");
}

static void handleSend() {
  if (!server.hasArg("mode")) { server.send(400, "text/plain", "Mode Missing"); return; }
  String mode = server.arg("mode"); mode.trim();

  if (mode == "Emergency") {
    if (!server.hasArg("command")) { server.send(400, "text/plain", "Missing command"); return; }
    String cmd = server.arg("command"); cmd.trim();

    if (cmd == "AGV_STOP")  { sendFrame(10, nullptr, 0); server.send(200,"text/plain","OK"); return; }
    if (cmd == "AGV_START") { sendFrame(11, nullptr, 0); server.send(200,"text/plain","OK"); return; }
    if (cmd == "GRIP_ENGAGE")    { sendFrame(12, nullptr, 0); server.send(200,"text/plain","OK"); return; }
    if (cmd == "GRIP_DISENGAGE") { sendFrame(13, nullptr, 0); server.send(200,"text/plain","OK"); return; }

    if (cmd == "UPDATE_PID") {
      uint16_t base = toU16(server.arg("base"), 60);
      uint16_t kp   = toU16(server.arg("kp"), 2);
      uint16_t kd   = toU16(server.arg("kd"), 12);
      uint8_t p[6];
      writeU16LE(&p[0], base);
      writeU16LE(&p[2], kp);
      writeU16LE(&p[4], kd);
      sendFrame(20, p, 6);
      server.send(200,"text/plain","OK");
      return;
    }
    server.send(400, "text/plain", "Unknown emergency command");
    return;
  }

  if (busy != BUSY_NONE) {
    server.send(409, "text/plain", "BUSY");
    return;
  }

  if (mode == "Auto") {
    uint16_t loopCount = toU16(server.arg("loop_count"), 1);
    uint16_t waitMin   = toU16(server.arg("wait_time"), 0);
    uint8_t p[4];
    writeU16LE(&p[0], loopCount);
    writeU16LE(&p[2], waitMin);
    sendFrame(1, p, 4);
    busy = BUSY_AUTO;
    server.send(200, "text/plain", "OK");
    return;
  }

  if (mode == "Manual") {
    if (!server.hasArg("type")) { server.send(400, "text/plain", "Missing type"); return; }
    String type = server.arg("type"); type.trim();

    if (type == "Charge") {
      uint16_t cmin = toU16(server.arg("charge_time"), 1);
      uint8_t p[2];
      writeU16LE(&p[0], cmin);
      sendFrame(3, p, 2);
      busy = BUSY_MANUAL_CHARGE;
      server.send(200, "text/plain", "OK");
      return;
    }

    if (type == "Trip") {
      uint8_t dropId = dropToId(server.arg("drop_location"));
      uint16_t waitMin = toU16(server.arg("wait_time"), 0);
      uint16_t loops   = toU16(server.arg("loop_count"), 1);
      uint8_t p[5];
      p[0] = dropId;
      writeU16LE(&p[1], waitMin);
      writeU16LE(&p[3], loops);
      sendFrame(2, p, 5);
      busy = BUSY_MANUAL_TRIP;
      server.send(200, "text/plain", "OK");
      return;
    }

    server.send(400, "text/plain", "Unknown Manual type");
    return;
  }

  server.send(400, "text/plain", "Unknown mode");
}

void setup() {
  Serial.begin(9600);
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);

  // Ultrasonic pins
  pinMode(US_TRIG, OUTPUT);
  pinMode(US_ECHO, INPUT);
  digitalWrite(US_TRIG, LOW);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 25) {
    delay(500);
    attempts++;
  }

  server.on("/", handleRoot);
  server.on("/send", handleSend);
  server.on("/disconnect", handleDisconnect);
  server.on("/reset", handleReset);
  server.on("/state", handleState);
  server.on("/logs", HTTP_GET, handleLogs);
  server.begin();

  pushJsonLog("SYS", "ESP32 US ON");
}

static void ultrasonicTask() {
  uint32_t now = millis();
  if (now - lastUsMs < US_POLL_MS) return;
  lastUsMs = now;

  int cm = readDistanceCm();
  if (cm < 0) return; // ignore timeouts

  // Stop condition
  if (!obstacleActive && cm <= OBS_STOP_CM) {
    obstacleActive = true;
    sendObstacleByte(true); // 0xF1
    pushJsonLog("OBS", "STOP " + String(cm) + "cm");
    return;
  }

  // Clear condition (hysteresis)
  if (obstacleActive && cm >= OBS_CLEAR_CM) {
    obstacleActive = false;
    sendObstacleByte(false); // 0xF0
    pushJsonLog("OBS", "RUN " + String(cm) + "cm");
    return;
  }
}

void loop() {
  server.handleClient();
  readArduinoLogs();
  ultrasonicTask();   // ✅ added (does not change other logic)
}