// agri-rain-poe — DFRobot SEN0575 rain gauge node on M5Stack ATOM PoE.
// MQTT + UECS-CCM publishers, web UI, mDNS + OTA. Most of the network /
// UI / publisher plumbing lives in agri-node-poe-core; this sketch only
// wires the rainfall sensor in.

#include <Arduino.h>
#include <AgriNode.h>

#include "config.h"
#include "sensors.h"
#include "mqtt_pub.h"
#include "ccm_pub.h"

const char *FW_NAME     = "agri-rain-poe";
const char *FW_VERSION  = "0.6.0";
const char *FW_REPO     = "yasunorioi/agri-rain-poe";
const char *FW_BIN_NAME = "agri-rain-poe.bin";

// globals declared extern in headers
AppConfig g_cfg;
HardwareSerial SensorSerial(1);
bool     g_sensor_detected = false;
uint32_t g_cum_rain_raw    = 0;
uint32_t g_raw_tips        = 0;
uint16_t g_work_minutes    = 0;

// ---- Dashboard / Config hooks --------------------------------------------
static String renderDashboardSensors() {
  String s;
  s.reserve(300);
  char rmm[12]; dtostrf(rainfallMm(), 1, 4, rmm);
  s  = F("<h3>Rainfall</h3><table>");
  s += "<tr><th>Cumulative</th><td>"; s += rmm;            s += " mm</td></tr>";
  s += "<tr><th>Raw tips</th><td>";   s += g_raw_tips;     s += "</td></tr>";
  s += "<tr><th>Work time</th><td>";  s += g_work_minutes; s += " min</td></tr>";
  s += "<tr><th>Sensor</th><td>";     s += g_sensor_detected ? "detected" : "NOT detected"; s += "</td></tr>";
  s += F("</table>");
  return s;
}

static String renderConfigSensorRows() {
  String s;
  s += "<tr><th>Order</th><td><input type=number name=ccm_ord value='";
  s += g_cfg.ccm_order;
  s += "'></td></tr>";
  return s;
}

static void applyConfigSensorForm(const String &body) {
  g_cfg.ccm_order = (int16_t)agri::parseFormInt(body, "ccm_ord", g_cfg.ccm_order);
}

static void addStatusFields(JsonObject doc) {
  doc["sensor_ok"]   = g_sensor_detected;
  doc["rainfall_mm"] = rainfallMm();
  doc["raw_tips"]    = g_raw_tips;
  doc["work_min"]    = g_work_minutes;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n=== %s v%s ===\n", FW_NAME, FW_VERSION);

  agri::Led::begin();
  loadConfig();
  Serial.printf("[CFG] node=%s mqtt_host=%s ccm=%s\n",
                g_cfg.common.node_id,
                g_cfg.common.mqtt_host[0] ? g_cfg.common.mqtt_host : "(unset)",
                g_cfg.common.ccm_enabled ? "on" : "off");

  sensorsBegin();
  detectSensor();

  agri::Network::begin(g_cfg.common.hostname);
  agri::Network::waitForLease();

  agri::ccmBegin();
  agri::MQTT::begin();

  agri::WebHooks hooks;
  hooks.nodeTitle             = [](){ return FW_NAME; };
  hooks.renderDashboardSensors= renderDashboardSensors;
  hooks.renderConfigSensorRows= renderConfigSensorRows;
  hooks.applyConfigSensorForm = applyConfigSensorForm;
  hooks.addStatusFields       = addStatusFields;
  hooks.saveConfig            = [](){ saveConfig(); };
  agri::WebUI::begin(g_cfg.common, hooks, FW_NAME, FW_VERSION);

  agri::mdnsBegin(g_cfg.common.hostname);
  agri::otaBegin(g_cfg.common.hostname);

  agri::OTA::begin(FW_REPO, FW_BIN_NAME, FW_VERSION);
  agri::OTA::checkLatest();

  Serial.println("[BOOT] ready");
}

void loop() {
  agri::otaHandle();
  agri::OTA::poll();
  agri::WebUI::handle(agri::Network::link_up, agri::Network::have_lease);

  uint32_t now = millis();

  static uint32_t lastSensorPoll = 0;
  if (now - lastSensorPoll >= 5000) {
    lastSensorPoll = now;
    sensorsPoll();
  }

  if (agri::networkUp() && agri::MQTT::hasHost(g_cfg.common)) {
    if (!agri::MQTT::connected()) {
      static uint32_t lastTry = 0;
      if (now - lastTry > 5000) { lastTry = now; agri::MQTT::reconnect(g_cfg.common); }
    } else {
      agri::MQTT::loop();
      static uint32_t lastPub = 0;
      uint32_t interval = (uint32_t)g_cfg.common.mqtt_interval_s * 1000UL;
      if (now - lastPub >= interval) {
        lastPub = now;
        if (mqttPublishRain()) agri::Led::flashPublish();
      }
    }
  }

  if (agri::networkUp() && g_cfg.common.ccm_enabled) {
    static uint32_t lastCcm = 0;
    uint32_t interval = (uint32_t)g_cfg.common.ccm_interval_s * 1000UL;
    if (now - lastCcm >= interval) {
      lastCcm = now;
      if (ccmPublish()) agri::Led::flashPublish();
    }
  }

  agri::LedState desired;
  if (!agri::networkUp())                                          desired = agri::LED_NO_LINK;
  else if (!g_sensor_detected)                                     desired = agri::LED_NO_SENSOR;
  else if (agri::MQTT::hasHost(g_cfg.common) && !agri::MQTT::connected()) desired = agri::LED_NO_MQTT;
  else                                                             desired = agri::LED_OK;
  agri::Led::set(desired);

  static uint32_t lastStatus = 0;
  if (now - lastStatus >= 30000) {
    lastStatus = now;
    Serial.printf("[STATUS] link=%d lease=%d mqtt=%d sensor=%d rain=%.4f mm up=%lus\n",
                  agri::Network::link_up, agri::Network::have_lease,
                  agri::MQTT::connected(), g_sensor_detected, rainfallMm(),
                  (unsigned long)(now / 1000));
  }

  delay(20);
}
