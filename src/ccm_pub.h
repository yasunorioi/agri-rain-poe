// ccm_pub.h — rainfall (WRainfallAmt) UECS-CCM publisher. Envelope and
// per-DATA builders live in the library; we just supply the channel data.

#pragma once

#include <Arduino.h>
#include <AgriNode.h>
#include "config.h"
#include "sensors.h"

inline bool ccmPublish() {
  if (!g_cfg.common.ccm_enabled || !g_sensor_detected) return false;

  char buf[16];
  dtostrf(rainfallMm(), 1, 4, buf);

  String xml = agri::ccmEnvelopeOpen();
  xml += agri::ccmDatum("WRainfallAmt",
                        g_cfg.common.ccm_room,
                        g_cfg.common.ccm_region,
                        g_cfg.ccm_order,
                        g_cfg.common.ccm_priority,
                        buf);
  xml += agri::ccmEnvelopeClose();
  return agri::ccmSend(xml);
}
