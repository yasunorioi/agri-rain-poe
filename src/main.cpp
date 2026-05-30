// agri-rain-poe — M5 ATOM PoE rainfall sensor node
//   SEN0575 (DFRobot Gravity Rainfall, Modbus RTU) → MQTT + UECS-CCM
//
// Hardware
//   M5Stack ATOM Lite + ATOM PoE base (W5500 on SPI)
//   SEN0575 on Grove: G26 (TX) -> sensor RX, G32 (RX) <- sensor TX, UART
//
// First flash via USB-C; subsequent flashes over Ethernet via ArduinoOTA.

#include <Arduino.h>
#include <SPI.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <PubSubClient.h>
// ArduinoOTA pulls ESPmDNS which crashes ("Invalid mbox" in tcpip.c) when
// the ESP-IDF lwIP tcpip thread isn't running — i.e. whenever we're on
// Arduino-Ethernet via W5500 SPI instead of ESP32's native ETH driver. We
// won't use OTA until we either migrate to the native driver or write a
// W5500-side mDNS responder. Reflash via USB for now.
// #include <ArduinoOTA.h>
#include <FastLED.h>

#include "config.h"
#include "sensors.h"
#include "ccm_pub.h"
#include "mqtt_pub.h"
#include "web_ui.h"
#include "led_state.h"

const char *FW_NAME    = "agri-rain-poe";
const char *FW_VERSION = "0.2.0";

// W5500 SPI pinout (M5 ATOM PoE base)
static const int PIN_W5500_SCK  = 22;
static const int PIN_W5500_MISO = 23;
static const int PIN_W5500_MOSI = 33;
static const int PIN_W5500_CS   = 19;

// globals declared extern in headers
Config g_cfg;

bool     g_sensor_detected = false;
uint32_t g_cum_rain_raw    = 0;
uint32_t g_raw_tips        = 0;
uint16_t g_work_minutes    = 0;
HardwareSerial SensorSerial(1);  // UART1

EthernetUDP    g_ccmUDP;
EthernetClient g_ethClient;
PubSubClient   g_mqtt(g_ethClient);
EthernetServerWrap g_webServer(80);

CRGB g_led[NEOPIXEL_NUM];
LedState g_led_state = LED_BOOT;

static bool g_link_up    = false;
static bool g_have_lease = false;

static void macFromEfuse(uint8_t mac[6]) {
  uint64_t chip = ESP.getEfuseMac();
  mac[0] = 0x02;  // locally administered, unicast
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
  Serial.printf("[NET] MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.print("[NET] DHCP…");
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
  if (g_link_up && !g_have_lease) ethernetBegin();
  if (g_link_up && g_have_lease)  Ethernet.maintain();
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n=== %s v%s ===\n", FW_NAME, FW_VERSION);

  ledBegin();
  ledApply();

  loadConfig();
  Serial.printf("[CFG] node=%s mqtt_host=%s ccm=%s\n",
                g_cfg.node_id,
                g_cfg.mqtt_host[0] ? g_cfg.mqtt_host : "(unset)",
                g_cfg.ccm_enabled ? "on" : "off");

  sensorsBegin();
  detectSensor();
  ethernetBegin();
  ccmBegin();
  g_webServer.begin();
  g_mqtt.setBufferSize(512);
  g_mqtt.setKeepAlive(30);

  Serial.println("[BOOT] ready");
}

void loop() {
  ethernetMaintain();
  handleWebClient(g_link_up, g_have_lease);

  uint32_t now = millis();

  static uint32_t lastSensorPoll = 0;
  if (now - lastSensorPoll >= 5000) {
    lastSensorPoll = now;
    sensorsPoll();
  }

  if (g_link_up && g_have_lease && mqttHasHost()) {
    if (!g_mqtt.connected()) {
      static uint32_t lastTry = 0;
      if (now - lastTry > 5000) { lastTry = now; mqttReconnect(); }
    } else {
      g_mqtt.loop();
      static uint32_t lastPub = 0;
      uint32_t interval = (uint32_t)g_cfg.mqtt_interval_s * 1000UL;
      if (now - lastPub >= interval) {
        lastPub = now;
        if (mqttPublishRain()) ledFlashPublish();
      }
    }
  }

  if (g_link_up && g_have_lease && g_cfg.ccm_enabled) {
    static uint32_t lastCcm = 0;
    uint32_t interval = (uint32_t)g_cfg.ccm_interval_s * 1000UL;
    if (now - lastCcm >= interval) {
      lastCcm = now;
      if (ccmPublish()) ledFlashPublish();
    }
  }

  // LED state machine
  LedState desired;
  if (!g_link_up || !g_have_lease)              desired = LED_NO_LINK;
  else if (!g_sensor_detected)                  desired = LED_NO_SENSOR;
  else if (mqttHasHost() && !g_mqtt.connected()) desired = LED_NO_MQTT;
  else                                          desired = LED_OK;
  if (desired != g_led_state) { g_led_state = desired; ledApply(); }

  static uint32_t lastStatus = 0;
  if (now - lastStatus >= 30000) {
    lastStatus = now;
    Serial.printf("[STATUS] link=%d lease=%d mqtt=%d sensor=%d rain=%.4f mm up=%lus\n",
                  g_link_up, g_have_lease, g_mqtt.connected(),
                  g_sensor_detected, rainfallMm(),
                  (unsigned long)(now / 1000));
  }

  delay(20);
}
