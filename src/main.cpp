// agri-rain-poe — M5Stack ATOM PoE rain gauge node
//
// Reads DFRobot SEN0575 (Gravity Rainfall Sensor) over UART/Modbus RTU and
// publishes to MQTT topic  agriha/{house}/sensor/SEN0575  per the OGMS spec in
// ~/agri-relay/docs/operation-manual.md.
//
// Hardware:
//   M5 ATOM Lite (ESP32-PICO-D4) on ATOM PoE Base (W5500 Ethernet)
//   SEN0575 DIP set to UART mode, wired to the Grove port:
//     M5 G26 (TX) -> SEN0575 SDA/RX
//     M5 G32 (RX) <- SEN0575 SCL/TX
//
// NeoPixel status (GPIO27):
//   red    — Ethernet link down or no DHCP lease
//   yellow — Ethernet up, MQTT disconnected
//   green  — MQTT connected, sensor OK
//   blue blink — publishing
//   magenta — sensor not detected (PID mismatch)

#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>

// ===================== Compile-time configuration ============================
// Edit these constants and reflash. The OGMS project is intentionally hobby-
// scope (no admin UI), so configuration lives here rather than in NVS.

static const char*    HOUSE_ID         = "h01";
static const char*    NODE_ID          = "agri-rain-01";
static const char*    MQTT_HOST        = "192.168.1.71";
static const uint16_t MQTT_PORT        = 1883;
static const uint32_t PUBLISH_INTERVAL_MS = 10000;   // 10 s
static const uint32_t SENSOR_POLL_MS      = 5000;    // 5 s

// SEN0575 wiring on M5 Atom Grove port
static const int PIN_SEN0575_TX = 26;   // M5 -> sensor RX
static const int PIN_SEN0575_RX = 32;   // M5 <- sensor TX

// W5500 SPI pins on M5 Atom PoE Kit
static const int PIN_W5500_SCK  = 22;
static const int PIN_W5500_MISO = 23;
static const int PIN_W5500_MOSI = 33;
static const int PIN_W5500_CS   = 19;

// M5 Atom Lite onboard NeoPixel
static const int PIN_NEOPIXEL = 27;

// ===================== SEN0575 Modbus RTU =================================
// Protocol cross-checked against the OGMS reference implementation
// (~/agri-relay/ogms.ino). Slave address 0xC0, FC=0x04 (read input regs).
static const uint8_t  SEN0575_ADDR          = 0xC0;
static const uint16_t SEN0575_REG_PID       = 0x0000;
static const uint16_t SEN0575_REG_VID       = 0x0001;
static const uint16_t SEN0575_REG_CUMRAIN_L = 0x0008;
static const uint16_t SEN0575_REG_RAWDATA_L = 0x000A;
static const uint16_t SEN0575_REG_SYSTIME   = 0x000C;
static const uint32_t SEN0575_PID_EXPECTED  = 0x000100C0;

HardwareSerial SensorSerial(1);  // UART1

static bool     g_sensor_detected = false;
static uint32_t g_cum_rain_raw    = 0;   // 0.0001 mm units
static uint32_t g_raw_tips        = 0;
static uint16_t g_work_minutes    = 0;

// ===================== Network / MQTT ====================================
EthernetClient ethClient;
PubSubClient   mqtt(ethClient);
static bool    g_link_up      = false;
static bool    g_have_lease   = false;
static uint32_t g_last_publish = 0;
static uint32_t g_last_poll    = 0;

// ===================== Status LED ========================================
CRGB g_led[1];

enum LedState { LED_BOOT, LED_NO_LINK, LED_NO_MQTT, LED_NO_SENSOR, LED_OK, LED_PUB };
static LedState g_led_state = LED_BOOT;

static void ledApply() {
  switch (g_led_state) {
    case LED_BOOT:      g_led[0] = CRGB(8, 8, 8);   break;
    case LED_NO_LINK:   g_led[0] = CRGB(40, 0, 0);  break;
    case LED_NO_MQTT:   g_led[0] = CRGB(40, 30, 0); break;
    case LED_NO_SENSOR: g_led[0] = CRGB(40, 0, 40); break;
    case LED_OK:        g_led[0] = CRGB(0, 30, 0);  break;
    case LED_PUB:       g_led[0] = CRGB(0, 0, 60);  break;
  }
  FastLED.show();
}

static void ledBlinkPublish() {
  LedState save = g_led_state;
  g_led_state = LED_PUB;
  ledApply();
  delay(60);
  g_led_state = save;
  ledApply();
}

// ===================== Modbus RTU helpers ================================
static uint16_t modbusCRC(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (int b = 0; b < 8; b++) {
      if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
      else              crc >>= 1;
    }
  }
  return crc;
}

static bool modbusReadInput(uint8_t addr, uint16_t reg, uint16_t count,
                            uint16_t* out1, uint16_t* out2) {
  uint8_t frame[8];
  frame[0] = addr;
  frame[1] = 0x04;
  frame[2] = (reg >> 8) & 0xFF;
  frame[3] = reg & 0xFF;
  frame[4] = (count >> 8) & 0xFF;
  frame[5] = count & 0xFF;
  uint16_t crc = modbusCRC(frame, 6);
  frame[6] = crc & 0xFF;
  frame[7] = (crc >> 8) & 0xFF;

  while (SensorSerial.available()) SensorSerial.read();
  SensorSerial.write(frame, 8);
  SensorSerial.flush();

  const int respLen = 3 + count * 2 + 2;
  uint8_t resp[11];
  if (respLen > (int)sizeof(resp)) return false;

  int received = 0;
  uint32_t deadline = millis() + 1000UL;
  while (received < respLen && (int32_t)(millis() - deadline) < 0) {
    if (SensorSerial.available()) resp[received++] = (uint8_t)SensorSerial.read();
  }
  if (received < respLen) return false;

  uint16_t rxCRC = (uint16_t)resp[respLen - 2] | ((uint16_t)resp[respLen - 1] << 8);
  if (modbusCRC(resp, respLen - 2) != rxCRC) return false;
  if (resp[0] != addr || resp[1] != 0x04 || resp[2] != count * 2) return false;

  *out1 = ((uint16_t)resp[3] << 8) | resp[4];
  if (count >= 2 && out2) *out2 = ((uint16_t)resp[5] << 8) | resp[6];
  return true;
}

// ===================== Sensor ============================================
static void detectSensor() {
  uint16_t pidReg = 0, vidReg = 0;
  bool pidOk = modbusReadInput(SEN0575_ADDR, SEN0575_REG_PID, 1, &pidReg, nullptr);
  delay(50);
  bool vidOk = modbusReadInput(SEN0575_ADDR, SEN0575_REG_VID, 1, &vidReg, nullptr);
  if (pidOk && vidOk) {
    uint32_t pid = ((uint32_t)(vidReg & 0xC000) << 2) | pidReg;
    g_sensor_detected = (pid == SEN0575_PID_EXPECTED);
    Serial.printf("SEN0575: PID=0x%05lX VID=0x%04X %s\n",
                  (unsigned long)pid, vidReg,
                  g_sensor_detected ? "DETECTED" : "PID mismatch");
  } else {
    g_sensor_detected = false;
    Serial.println("SEN0575: not responding");
  }
}

static void pollSensor() {
  if (!g_sensor_detected) {
    detectSensor();
    return;
  }
  uint16_t cumL = 0, cumH = 0;
  if (modbusReadInput(SEN0575_ADDR, SEN0575_REG_CUMRAIN_L, 2, &cumL, &cumH)) {
    g_cum_rain_raw = ((uint32_t)cumH << 16) | cumL;
  }
  delay(50);
  uint16_t rawL = 0, rawH = 0;
  if (modbusReadInput(SEN0575_ADDR, SEN0575_REG_RAWDATA_L, 2, &rawL, &rawH)) {
    g_raw_tips = ((uint32_t)rawH << 16) | rawL;
  }
  delay(50);
  uint16_t wt = 0;
  if (modbusReadInput(SEN0575_ADDR, SEN0575_REG_SYSTIME, 1, &wt, nullptr)) {
    g_work_minutes = wt;
  }
}

// ===================== MQTT ==============================================
static void mqttPublishRain() {
  if (!mqtt.connected()) return;

  char topic[96];
  snprintf(topic, sizeof(topic), "agriha/%s/sensor/SEN0575", HOUSE_ID);

  JsonDocument doc;
  doc["rainfall_mm"] = g_cum_rain_raw / 10000.0;
  doc["raw_tips"]    = g_raw_tips;
  doc["work_min"]    = g_work_minutes;
  doc["node_id"]     = NODE_ID;
  doc["timestamp"]   = 0;  // no NTP on this node; broker can stamp

  char payload[192];
  size_t n = serializeJson(doc, payload, sizeof(payload));
  bool ok = mqtt.publish(topic, (const uint8_t*)payload, n, true);
  Serial.printf("MQTT pub %s : %s (%s)\n", topic, payload, ok ? "ok" : "fail");
  if (ok) ledBlinkPublish();
}

static void mqttPublishStatus(const char* state) {
  if (!mqtt.connected()) return;
  char topic[96];
  snprintf(topic, sizeof(topic), "agriha/%s/sensor/SEN0575/status", HOUSE_ID);
  mqtt.publish(topic, state, true);
}

static bool mqttReconnect() {
  if (mqtt.connected()) return true;
  char willTopic[96];
  snprintf(willTopic, sizeof(willTopic), "agriha/%s/sensor/SEN0575/status", HOUSE_ID);
  Serial.printf("MQTT connect to %s:%u as %s ...\n", MQTT_HOST, MQTT_PORT, NODE_ID);
  bool ok = mqtt.connect(NODE_ID,
                         /*user*/ nullptr, /*pass*/ nullptr,
                         willTopic, 0, true, "offline");
  if (ok) {
    Serial.println("MQTT connected");
    mqtt.publish(willTopic, "online", true);
  } else {
    Serial.printf("MQTT connect failed rc=%d\n", mqtt.state());
  }
  return ok;
}

// ===================== Ethernet ==========================================
static void macFromEfuse(uint8_t mac[6]) {
  uint64_t chip = ESP.getEfuseMac();
  // Locally administered, unicast
  mac[0] = 0x02;
  mac[1] = (chip >> 8)  & 0xFF;
  mac[2] = (chip >> 16) & 0xFF;
  mac[3] = (chip >> 24) & 0xFF;
  mac[4] = (chip >> 32) & 0xFF;
  mac[5] = (chip >> 40) & 0xFF;
}

static void ethernetBegin() {
  SPI.begin(PIN_W5500_SCK, PIN_W5500_MISO, PIN_W5500_MOSI, PIN_W5500_CS);
  Ethernet.init(PIN_W5500_CS);

  uint8_t mac[6];
  macFromEfuse(mac);
  Serial.printf("MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  Serial.print("DHCP ...");
  if (Ethernet.begin(mac, 8000, 4000) == 1) {
    g_have_lease = true;
    IPAddress ip = Ethernet.localIP();
    Serial.printf(" %u.%u.%u.%u\n", ip[0], ip[1], ip[2], ip[3]);
  } else {
    g_have_lease = false;
    Serial.println(" failed");
  }
}

static void ethernetMaintain() {
  EthernetLinkStatus link = Ethernet.linkStatus();
  g_link_up = (link == LinkON);
  if (g_link_up && !g_have_lease) {
    ethernetBegin();
  }
  if (g_link_up && g_have_lease) {
    Ethernet.maintain();
  }
}

// ===================== Lifecycle =========================================
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== agri-rain-poe boot ===");

  FastLED.addLeds<WS2812, PIN_NEOPIXEL, GRB>(g_led, 1);
  FastLED.setBrightness(40);
  g_led_state = LED_BOOT;
  ledApply();

  SensorSerial.begin(9600, SERIAL_8N1, PIN_SEN0575_RX, PIN_SEN0575_TX);
  Serial.printf("SEN0575 UART: TX=GPIO%d RX=GPIO%d 9600 8N1\n",
                PIN_SEN0575_TX, PIN_SEN0575_RX);
  delay(100);
  detectSensor();

  ethernetBegin();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(30);
}

void loop() {
  ethernetMaintain();

  uint32_t now = millis();

  if (now - g_last_poll >= SENSOR_POLL_MS) {
    g_last_poll = now;
    pollSensor();
  }

  if (g_link_up && g_have_lease) {
    if (!mqtt.connected()) {
      static uint32_t lastTry = 0;
      if (now - lastTry > 5000) {
        lastTry = now;
        mqttReconnect();
      }
    } else {
      mqtt.loop();
      if (now - g_last_publish >= PUBLISH_INTERVAL_MS) {
        g_last_publish = now;
        mqttPublishRain();
      }
    }
  }

  // LED state machine
  LedState desired;
  if (!g_link_up || !g_have_lease)       desired = LED_NO_LINK;
  else if (!mqtt.connected())            desired = LED_NO_MQTT;
  else if (!g_sensor_detected)           desired = LED_NO_SENSOR;
  else                                   desired = LED_OK;
  if (desired != g_led_state) {
    g_led_state = desired;
    ledApply();
  }

  delay(10);
}
