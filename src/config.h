// config.h — agri-rain-poe NVS-backed config. Wraps the library's
// CommonConfig with a single WRainfallAmt CCM channel.

#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <AgriCommonConfig.h>

struct AppConfig {
  agri::CommonConfig common;
  int16_t            ccm_order;  // single rainfall channel
};

extern AppConfig g_cfg;

inline void setDefaults() {
  // ccm_rp2350_relay convention: weather sensors sit at sensor_region (11) +
  // weather offset (30) = 41.
  agri::commonDefaults(g_cfg.common,
                       "rain_node_01", "agri-rain-01",
                       "agriha/h01/sensor/SEN0575",
                       /*default_ccm_region=*/41);
  g_cfg.common.mqtt_interval_s = 10;
  g_cfg.ccm_order              = 1;
}

inline void loadConfig() {
  setDefaults();
  Preferences p;
  if (!p.begin("rain-cfg", true)) return;
  agri::commonLoad(g_cfg.common, p);
  g_cfg.ccm_order = p.getShort("ccm_ord", g_cfg.ccm_order);
  p.end();
}

inline bool saveConfig() {
  Preferences p;
  if (!p.begin("rain-cfg", false)) return false;
  agri::commonSave(g_cfg.common, p);
  p.putShort("ccm_ord", g_cfg.ccm_order);
  p.end();
  return true;
}
