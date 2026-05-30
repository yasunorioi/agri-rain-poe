// mqtt_pub.h — PubSubClient publisher for rainfall.
//
// Topics
//   <prefix>          ← single JSON message per cadence (compat with the
//                       pre-refactor sketch which used the bare prefix).
//   <prefix>/status   ← LWT-managed "online"/"offline" retained.

#pragma once

#include <Arduino.h>
#include <Ethernet.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "sensors.h"

extern EthernetClient g_ethClient;
extern PubSubClient   g_mqtt;

inline bool mqttHasHost() { return g_cfg.mqtt_host[0] != '\0'; }

inline void statusTopic(char *out, size_t n) {
  snprintf(out, n, "%s/status", g_cfg.mqtt_topic_prefix);
}

inline bool mqttReconnect() {
  if (!mqttHasHost()) return false;
  if (g_mqtt.connected()) return true;
  g_mqtt.setServer(g_cfg.mqtt_host, g_cfg.mqtt_port);
  char will[96];
  statusTopic(will, sizeof(will));
  bool ok;
  if (g_cfg.mqtt_user[0]) {
    ok = g_mqtt.connect(g_cfg.node_id, g_cfg.mqtt_user, g_cfg.mqtt_pass,
                        will, 0, true, "offline");
  } else {
    ok = g_mqtt.connect(g_cfg.node_id, nullptr, nullptr,
                        will, 0, true, "offline");
  }
  Serial.printf("[MQTT] connect(%s:%u) = %s\n",
                g_cfg.mqtt_host, g_cfg.mqtt_port, ok ? "OK" : "FAIL");
  if (ok) g_mqtt.publish(will, "online", true);
  return ok;
}

inline bool mqttPublishRain() {
  if (!mqttHasHost() || !g_mqtt.connected()) return false;

  JsonDocument doc;
  doc["rainfall_mm"] = rainfallMm();
  doc["raw_tips"]    = g_raw_tips;
  doc["work_min"]    = g_work_minutes;
  doc["node_id"]     = g_cfg.node_id;
  doc["uptime_s"]    = millis() / 1000;

  char payload[256];
  size_t n = serializeJson(doc, payload, sizeof(payload));

  bool ok = g_mqtt.publish(g_cfg.mqtt_topic_prefix,
                           (const uint8_t*)payload, n, true);
  Serial.printf("[MQTT] %s %s (%u bytes)\n",
                g_cfg.mqtt_topic_prefix, ok ? "OK" : "FAIL", (unsigned)n);
  return ok;
}
