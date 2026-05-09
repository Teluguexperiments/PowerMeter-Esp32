#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <PZEM004Tv30.h>

// WiFi mode:
// Leave WIFI_SSID empty to create a phone-connectable access point.
// Or set your router WiFi details to join your home network.
const char *WIFI_SSID = "";
const char *WIFI_PASS = "";
const char *AP_SSID = "ESP32-Power-Meter";
const char *AP_PASS = "12345678";

// 1.8 inch TFT SPI display pins.
// Display labels: LED, SCK, SDA, A0, RESET, CS, GND, VCC
constexpr uint8_t TFT_LED = 32;
constexpr uint8_t TFT_CS = 5;
constexpr uint8_t TFT_DC = 21;     // A0 / DC
constexpr uint8_t TFT_RST = 22;    // RESET
constexpr uint8_t TFT_SCK = 18;
constexpr uint8_t TFT_MOSI = 23;   // SDA

// PZEM-004T UART pins. ESP32 RX connects to PZEM TX, ESP32 TX connects to PZEM RX.
constexpr uint8_t PZEM_RX = 16;
constexpr uint8_t PZEM_TX = 17;

// Button: one side to GPIO27, other side to GND.
constexpr uint8_t RESET_BUTTON = 27;

Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_RST);
PZEM004Tv30 pzem(Serial2, PZEM_RX, PZEM_TX);
WebServer server(80);

constexpr uint16_t COL_BG = 0x0841;
constexpr uint16_t COL_PANEL = 0x10A4;
constexpr uint16_t COL_MUTED = 0x8410;
constexpr uint16_t COL_CYAN = 0x07FF;
constexpr uint16_t COL_YELLOW = 0xFFE0;
constexpr uint16_t COL_ORANGE = 0xFD20;
constexpr uint16_t COL_GREEN = 0x07E0;
constexpr uint16_t COL_RED = 0xF800;

struct MeterData {
  float voltage = NAN;
  float current = NAN;
  float power = NAN;
  float energy = NAN;
  float frequency = NAN;
  float powerFactor = NAN;
  bool online = false;
};

MeterData meter;
uint32_t lastReadMs = 0;
uint32_t lastScreenMs = 0;
uint32_t lastButtonChangeMs = 0;
bool lastButtonState = HIGH;
bool stableButtonState = HIGH;
bool resetDoneForPress = false;
bool screenInitialized = false;

String fmt(float value, unsigned int decimals) {
  if (isnan(value)) return "--";
  return String(value, decimals);
}

void printRight(const String &text, int16_t rightX, int16_t y, uint8_t size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  tft.setCursor(rightX - w, y);
  tft.print(text);
}

void readMeter() {
  meter.voltage = pzem.voltage();
  meter.current = pzem.current();
  meter.power = pzem.power();
  meter.energy = pzem.energy();
  meter.frequency = pzem.frequency();
  meter.powerFactor = pzem.pf();
  meter.online = !(isnan(meter.voltage) && isnan(meter.current) && isnan(meter.power));
}

void drawCard(int16_t y, const char *label, const String &value, const char *unit, uint16_t accent, uint8_t valueSize) {
  tft.fillRoundRect(7, y, 114, 27, 5, COL_PANEL);
  tft.drawRoundRect(7, y, 114, 27, 5, accent);

  tft.setTextSize(1);
  tft.setTextColor(accent);
  tft.setCursor(14, y + 4);
  tft.print(label);

  int16_t unitRight = 114;
  tft.setTextSize(1);
  tft.setTextColor(COL_MUTED);
  int16_t x1, y1;
  uint16_t unitW, unitH;
  tft.getTextBounds(unit, 0, 0, &x1, &y1, &unitW, &unitH);
  tft.setCursor(unitRight - unitW, y + 17);
  tft.print(unit);

  printRight(value, unitRight - unitW - 4, y + 11, valueSize, accent);
}

void drawScreen() {
  if (!screenInitialized) {
    tft.fillScreen(COL_BG);
    screenInitialized = true;
  }

  tft.setTextWrap(false);
  tft.fillRect(6, 42, 116, 6, COL_BG);

  tft.fillRoundRect(5, 5, 118, 35, 6, COL_PANEL);
  tft.setTextSize(1);
  tft.setTextColor(COL_CYAN);
  tft.setCursor(12, 12);
  tft.print("POWER METER");
  tft.setTextColor(COL_MUTED);
  tft.setCursor(12, 25);
  tft.print("Telugu experiments");
  tft.fillCircle(110, 16, 7, meter.online ? COL_GREEN : COL_RED);
  tft.drawFastHLine(5, 43, 118, COL_CYAN);

  drawCard(48, "VOLTAGE", fmt(meter.voltage, 1), "V", COL_YELLOW, 2);
  drawCard(79, "CURRENT", fmt(meter.current, 2), "A", COL_ORANGE, 2);
  drawCard(110, "POWER", fmt(meter.power, 1), "W", COL_CYAN, 2);

  tft.fillRoundRect(7, 141, 114, 17, 5, COL_PANEL);
  tft.drawRoundRect(7, 141, 114, 17, 5, COL_GREEN);
  tft.setTextSize(1);
  tft.setTextColor(COL_GREEN);
  tft.setCursor(14, 147);
  tft.print("UNITS");
  printRight(fmt(meter.energy, 3), 93, 144, 1, COL_GREEN);
  tft.setTextSize(1);
  tft.setTextColor(COL_MUTED);
  tft.setCursor(97, 147);
  tft.print("kWh");
}

void resetEnergyCounter() {
  bool ok = pzem.resetEnergy();
  readMeter();
  drawScreen();
  tft.fillRect(6, 42, 116, 6, COL_BG);
  tft.setTextSize(1);
  tft.setTextColor(ok ? COL_GREEN : COL_RED);
  tft.setCursor(32, 42);
  tft.print(ok ? "ENERGY RESET" : "RESET FAILED");
}

String htmlPage() {
  return R"rawliteral(
<!doctype html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 Power Meter</title>
  <style>
    :root { color-scheme: dark; font-family: Arial, sans-serif; background:#101418; color:#f6f7f8; }
    * { box-sizing:border-box; }
    body { margin:0; padding:18px; }
    header { display:flex; justify-content:space-between; align-items:center; margin-bottom:18px; gap:12px; }
    h1 { font-size:22px; margin:0; font-weight:800; }
    .sub { color:#aab4bd; font-size:13px; margin-top:4px; }
    .status { font-size:12px; padding:6px 9px; border-radius:999px; background:#1f2a33; color:#8ee6a2; font-weight:700; }
    .grid { display:grid; grid-template-columns:1fr 1fr; gap:12px; }
    .card { background:#171e25; border:1px solid #2b3640; border-radius:8px; padding:14px; }
    .label { color:#aab4bd; font-size:12px; }
    .value { font-size:28px; margin-top:8px; font-weight:700; }
    .unit { color:#aab4bd; font-size:14px; margin-left:4px; }
    .wide { grid-column:1 / -1; }
    button { width:100%; margin-top:16px; border:0; border-radius:8px; padding:14px; background:#e25544; color:white; font-weight:700; font-size:15px; }
    .accent-v { border-color:#ffe100; }
    .accent-c { border-color:#ff9800; }
    .accent-p { border-color:#00e5ff; }
    .accent-e { border-color:#00e51b; }
    footer { color:#76838f; font-size:12px; margin-top:16px; line-height:1.6; }
  </style>
</head>
<body>
  <header><div><h1>Telugu experiments</h1><div class="sub">PZEM-004T Power Meter</div></div><div id="online" class="status">LIVE</div></header>
  <main class="grid">
    <section class="card accent-v"><div class="label">Voltage</div><div class="value"><span id="v">--</span><span class="unit">V</span></div></section>
    <section class="card accent-c"><div class="label">Current</div><div class="value"><span id="c">--</span><span class="unit">A</span></div></section>
    <section class="card accent-p"><div class="label">Power</div><div class="value"><span id="p">--</span><span class="unit">W</span></div></section>
    <section class="card accent-e"><div class="label">Units</div><div class="value"><span id="e">--</span><span class="unit">kWh</span></div></section>
    <section class="card wide"><div class="label">Frequency / Power factor</div><div class="value"><span id="f">--</span><span class="unit">Hz</span> <span id="pf">--</span></div></section>
  </main>
  <button onclick="resetEnergy()">Reset kWh Energy</button>
  <footer><div id="updated">Waiting for meter...</div><div>Connect phone to ESP32-Power-Meter WiFi and open http://192.168.4.1</div></footer>
  <script>
    const setText = (id, value) => document.getElementById(id).textContent = value;
    async function refresh() {
      try {
        const r = await fetch('/api');
        const d = await r.json();
        setText('v', d.voltage);
        setText('c', d.current);
        setText('p', d.power);
        setText('e', d.energy);
        setText('f', d.frequency);
        setText('pf', 'PF ' + d.powerFactor);
        document.getElementById('online').textContent = d.online ? 'LIVE' : 'NO SIGNAL';
        document.getElementById('online').style.color = d.online ? '#8ee6a2' : '#ff8b7f';
        setText('updated', 'Updated ' + new Date().toLocaleTimeString());
      } catch (e) {
        document.getElementById('online').textContent = 'OFFLINE';
      }
    }
    async function resetEnergy() {
      if (confirm('Reset kWh units to zero?')) {
        await fetch('/reset', { method: 'POST' });
        refresh();
      }
    }
    refresh();
    setInterval(refresh, 2000);
  </script>
</body>
</html>
)rawliteral";
}

void handleApi() {
  String json = "{";
  json += "\"voltage\":\"";
  json += fmt(meter.voltage, 1);
  json += "\",\"current\":\"";
  json += fmt(meter.current, 3);
  json += "\",\"power\":\"";
  json += fmt(meter.power, 1);
  json += "\",\"energy\":\"";
  json += fmt(meter.energy, 3);
  json += "\",\"frequency\":\"";
  json += fmt(meter.frequency, 1);
  json += "\",\"powerFactor\":\"";
  json += fmt(meter.powerFactor, 2);
  json += "\",";
  json += "\"online\":";
  json += (meter.online ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

void setupWiFi() {
  if (strlen(WIFI_SSID) > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    uint32_t startMs = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startMs < 10000) {
      delay(250);
    }
    if (WiFi.status() == WL_CONNECTED) return;
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
}

void setupServer() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", htmlPage());
  });

  server.on("/api", HTTP_GET, handleApi);

  server.on("/reset", HTTP_POST, []() {
    resetEnergyCounter();
    server.send(200, "application/json", "{\"ok\":true}");
  });

  server.begin();
}

void handleButton() {
  bool currentState = digitalRead(RESET_BUTTON);

  if (currentState != lastButtonState) {
    lastButtonChangeMs = millis();
    lastButtonState = currentState;
  }

  if (millis() - lastButtonChangeMs < 40) return;

  if (currentState != stableButtonState) {
    stableButtonState = currentState;

    if (stableButtonState == LOW && !resetDoneForPress) {
      resetEnergyCounter();
      resetDoneForPress = true;
    }

    if (stableButtonState == HIGH) {
      resetDoneForPress = false;
    }
  }
}

void setup() {
  pinMode(TFT_LED, OUTPUT);
  digitalWrite(TFT_LED, HIGH);
  pinMode(RESET_BUTTON, INPUT_PULLUP);

  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, PZEM_RX, PZEM_TX);

  SPI.begin(TFT_SCK, -1, TFT_MOSI, TFT_CS);
  tft.initR(INITR_BLACKTAB);
  tft.setSPISpeed(27000000);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);

  setupWiFi();
  setupServer();
  readMeter();
  drawScreen();
}

void loop() {
  server.handleClient();
  handleButton();

  if (millis() - lastReadMs >= 1000) {
    lastReadMs = millis();
    readMeter();
  }

  if (millis() - lastScreenMs >= 2000) {
    lastScreenMs = millis();
    drawScreen();
  }
}
