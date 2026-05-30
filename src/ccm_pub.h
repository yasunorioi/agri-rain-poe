// ccm_pub.h — UECS-CCM WRainfallAmt publisher.
//
// Packet shape and region convention follow ccm_rp2350_relay (see
// ccmSendStates() in ccm_rp2350_relay.ino). Region default 41 = sensor
// region 11 + weather offset 30.

#pragma once

#include <Arduino.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include "config.h"
#include "sensors.h"

static const IPAddress CCM_MULTICAST(224, 0, 0, 1);
static const uint16_t  CCM_PORT      = 16520;
static const char* const UECS_VERSION = "1.00-E10";

extern EthernetUDP g_ccmUDP;

inline void ccmBegin() {
  g_ccmUDP.begin(CCM_PORT);
}

inline bool ccmPublish() {
  if (!g_cfg.ccm_enabled || !g_sensor_detected) return false;

  char buf[16];
  dtostrf(rainfallMm(), 1, 4, buf);

  String xml;
  xml.reserve(256);
  xml  = "<UECS ver=\"";
  xml += UECS_VERSION;
  xml += "\"><DATA type=\"WRainfallAmt.cMC\" room=\"";
  xml += g_cfg.ccm_room;
  xml += "\" region=\"";
  xml += g_cfg.ccm_region;
  xml += "\" order=\"";
  xml += g_cfg.ccm_order;
  xml += "\" priority=\"";
  xml += g_cfg.ccm_priority;
  xml += "\" lv=\"S\" cast=\"uni\">";
  xml += buf;
  xml += "</DATA></UECS>";

  if (!g_ccmUDP.beginPacket(CCM_MULTICAST, CCM_PORT)) return false;
  g_ccmUDP.write((const uint8_t*)xml.c_str(), xml.length());
  bool ok = g_ccmUDP.endPacket();
  if (ok) Serial.printf("[CCM] TX %u bytes\n", (unsigned)xml.length());
  return ok;
}
