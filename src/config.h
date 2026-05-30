// config.h — NVS-backed runtime configuration for agri-rain-poe.
// Mirrors agri-env-poe's layout; one CCM channel only (WRainfallAmt).

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>

struct Config {
  // identity
  char     node_id[16];           // "rain_node_01"
  char     hostname[32];          // "agri-rain-01"

  // MQTT
  char     mqtt_host[64];         // empty = MQTT disabled
  uint16_t mqtt_port;
  char     mqtt_user[32];
  char     mqtt_pass[32];
  char     mqtt_topic_prefix[64]; // "agriha/h01/sensor/SEN0575" default
  uint16_t mqtt_interval_s;

  // UECS-CCM (single rainfall channel)
  bool     ccm_enabled;
  uint16_t ccm_interval_s;
  int16_t  ccm_room;              // 1
  int16_t  ccm_region;            // 41 — ccm_rp2350_relay convention is
                                  //   sensor_region (11) + weather offset (30).
  int16_t  ccm_order;             // 1
  int16_t  ccm_priority;          // 29
};

extern Config g_cfg;

inline void setConfigDefaults() {
  strlcpy(g_cfg.node_id,           "rain_node_01",                       sizeof(g_cfg.node_id));
  strlcpy(g_cfg.hostname,          "agri-rain-01",                       sizeof(g_cfg.hostname));
  strlcpy(g_cfg.mqtt_host,         "",                                   sizeof(g_cfg.mqtt_host));
  g_cfg.mqtt_port           = 1883;
  strlcpy(g_cfg.mqtt_user,         "",                                   sizeof(g_cfg.mqtt_user));
  strlcpy(g_cfg.mqtt_pass,         "",                                   sizeof(g_cfg.mqtt_pass));
  strlcpy(g_cfg.mqtt_topic_prefix, "agriha/h01/sensor/SEN0575",          sizeof(g_cfg.mqtt_topic_prefix));
  g_cfg.mqtt_interval_s     = 10;

  g_cfg.ccm_enabled         = false;
  g_cfg.ccm_interval_s      = 10;
  g_cfg.ccm_room            = 1;
  g_cfg.ccm_region          = 41;
  g_cfg.ccm_order           = 1;
  g_cfg.ccm_priority        = 29;
}

inline void loadConfig() {
  setConfigDefaults();
  Preferences p;
  if (!p.begin("rain-cfg", true)) return;

  p.getString("node_id",   g_cfg.node_id,           sizeof(g_cfg.node_id));
  p.getString("hostname",  g_cfg.hostname,          sizeof(g_cfg.hostname));
  p.getString("mq_host",   g_cfg.mqtt_host,         sizeof(g_cfg.mqtt_host));
  g_cfg.mqtt_port = p.getUShort("mq_port", g_cfg.mqtt_port);
  p.getString("mq_user",   g_cfg.mqtt_user,         sizeof(g_cfg.mqtt_user));
  p.getString("mq_pass",   g_cfg.mqtt_pass,         sizeof(g_cfg.mqtt_pass));
  p.getString("mq_pfx",    g_cfg.mqtt_topic_prefix, sizeof(g_cfg.mqtt_topic_prefix));
  g_cfg.mqtt_interval_s = p.getUShort("mq_int", g_cfg.mqtt_interval_s);

  g_cfg.ccm_enabled    = p.getBool("ccm_en",  g_cfg.ccm_enabled);
  g_cfg.ccm_interval_s = p.getUShort("ccm_int", g_cfg.ccm_interval_s);
  g_cfg.ccm_room       = p.getShort("ccm_room", g_cfg.ccm_room);
  g_cfg.ccm_region     = p.getShort("ccm_reg",  g_cfg.ccm_region);
  g_cfg.ccm_order      = p.getShort("ccm_ord",  g_cfg.ccm_order);
  g_cfg.ccm_priority   = p.getShort("ccm_pri",  g_cfg.ccm_priority);

  p.end();
}

inline bool saveConfig() {
  Preferences p;
  if (!p.begin("rain-cfg", false)) return false;
  p.putString("node_id",   g_cfg.node_id);
  p.putString("hostname",  g_cfg.hostname);
  p.putString("mq_host",   g_cfg.mqtt_host);
  p.putUShort("mq_port",   g_cfg.mqtt_port);
  p.putString("mq_user",   g_cfg.mqtt_user);
  p.putString("mq_pass",   g_cfg.mqtt_pass);
  p.putString("mq_pfx",    g_cfg.mqtt_topic_prefix);
  p.putUShort("mq_int",    g_cfg.mqtt_interval_s);
  p.putBool  ("ccm_en",    g_cfg.ccm_enabled);
  p.putUShort("ccm_int",   g_cfg.ccm_interval_s);
  p.putShort ("ccm_room",  g_cfg.ccm_room);
  p.putShort ("ccm_reg",   g_cfg.ccm_region);
  p.putShort ("ccm_ord",   g_cfg.ccm_order);
  p.putShort ("ccm_pri",   g_cfg.ccm_priority);
  p.end();
  return true;
}

inline String configToJson() {
  JsonDocument doc;
  doc["node_id"]   = g_cfg.node_id;
  doc["hostname"]  = g_cfg.hostname;
  doc["mqtt"]["host"]       = g_cfg.mqtt_host;
  doc["mqtt"]["port"]       = g_cfg.mqtt_port;
  doc["mqtt"]["user"]       = g_cfg.mqtt_user;
  doc["mqtt"]["prefix"]     = g_cfg.mqtt_topic_prefix;
  doc["mqtt"]["interval_s"] = g_cfg.mqtt_interval_s;
  doc["ccm"]["enabled"]    = g_cfg.ccm_enabled;
  doc["ccm"]["interval_s"] = g_cfg.ccm_interval_s;
  doc["ccm"]["room"]       = g_cfg.ccm_room;
  doc["ccm"]["region"]     = g_cfg.ccm_region;
  doc["ccm"]["order"]      = g_cfg.ccm_order;
  doc["ccm"]["priority"]   = g_cfg.ccm_priority;
  String out;
  serializeJson(doc, out);
  return out;
}
