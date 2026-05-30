// sensors.h — DFRobot SEN0575 rainfall sensor over Modbus RTU.
// Same wire protocol and register layout as the agri-relay OGMS reference.

#pragma once

#include <Arduino.h>

// Grove UART on M5 ATOM Lite
static const int PIN_SEN0575_TX = 26;   // M5 -> sensor RX
static const int PIN_SEN0575_RX = 32;   // M5 <- sensor TX

// Slave addr + register map cross-checked against ~/agri-relay/ogms.ino.
static const uint8_t  SEN0575_ADDR          = 0xC0;
static const uint16_t SEN0575_REG_PID       = 0x0000;
static const uint16_t SEN0575_REG_VID       = 0x0001;
static const uint16_t SEN0575_REG_CUMRAIN_L = 0x0008;
static const uint16_t SEN0575_REG_RAWDATA_L = 0x000A;
static const uint16_t SEN0575_REG_SYSTIME   = 0x000C;
static const uint32_t SEN0575_PID_EXPECTED  = 0x000100C0;

extern bool     g_sensor_detected;
extern uint32_t g_cum_rain_raw;    // 0.0001 mm units
extern uint32_t g_raw_tips;
extern uint16_t g_work_minutes;

extern HardwareSerial SensorSerial;

inline uint16_t modbusCRC(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) {
      if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
      else              crc >>= 1;
    }
  }
  return crc;
}

inline bool modbusReadInput(uint8_t addr, uint16_t reg, uint16_t count,
                            uint16_t *out1, uint16_t *out2) {
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

inline void sensorsBegin() {
  SensorSerial.begin(9600, SERIAL_8N1, PIN_SEN0575_RX, PIN_SEN0575_TX);
  Serial.printf("[SENS] SEN0575 UART TX=GPIO%d RX=GPIO%d 9600 8N1\n",
                PIN_SEN0575_TX, PIN_SEN0575_RX);
  delay(100);
}

inline void detectSensor() {
  uint16_t pidReg = 0, vidReg = 0;
  bool pidOk = modbusReadInput(SEN0575_ADDR, SEN0575_REG_PID, 1, &pidReg, nullptr);
  delay(50);
  bool vidOk = modbusReadInput(SEN0575_ADDR, SEN0575_REG_VID, 1, &vidReg, nullptr);
  if (pidOk && vidOk) {
    uint32_t pid = ((uint32_t)(vidReg & 0xC000) << 2) | pidReg;
    g_sensor_detected = (pid == SEN0575_PID_EXPECTED);
    Serial.printf("[SENS] SEN0575 PID=0x%05lX VID=0x%04X %s\n",
                  (unsigned long)pid, vidReg,
                  g_sensor_detected ? "DETECTED" : "PID mismatch");
  } else {
    g_sensor_detected = false;
    Serial.println("[SENS] SEN0575 not responding");
  }
}

inline void sensorsPoll() {
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

inline float rainfallMm() {
  return g_cum_rain_raw / 10000.0f;
}
