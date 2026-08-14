// serial_protocol.h
// Multi Dryer HMI — ESP-Now communication with the Multi Dryer controller
//
// Public API is unchanged — all callers (control_screen, diagnostics_screen, etc.)
// continue to call the same functions; only the transport has changed from UART to
// ESP-Now.
//
// ── MAC Setup ──────────────────────────────────────────────────────────────────
// 1. Boot the controller, read "[ESP-NOW] Controller MAC: XX:XX:XX:XX:XX:XX" on its Serial.
// 2. Paste that MAC into CONTROLLER_PEER_MAC in serial_protocol.cpp, re-flash the HMI.
// 3. Boot the HMI, read "[HMI] Controller peer MAC: XX:XX:XX:XX:XX:XX" on its Serial.
// 4. Paste that MAC into HMI_PEER_MAC in espnow_link.h (controller), re-flash the controller.

#ifndef SERIAL_PROTOCOL_H
#define SERIAL_PROTOCOL_H

#include <Arduino.h>
#include "dryer_data.h"

// Initialize ESP-Now communication
void serialProtoInit();

// Call from loop() — receives status packets, checks connection timeout
void serialProtoUpdate();

// Commands to send to the controller over ESP-Now
void sendSetTemperature(float temp);
void sendHeaterControl(bool on);
void sendFanControl(bool on);
void sendExhaustControl(bool on);
void sendPIDStart();
void sendPIDStop();
void sendStartDrying();
void sendStopDrying();
void sendPauseDrying();
void sendResumeDrying();
void sendStatusRequest();
void sendSetWaterLoss(float pct);
void sendTareScale();
void sendCalibrateScale(float knownKg);
void sendSensorTest();   // triggers SHT31 + load-cell read on the controller

#endif // SERIAL_PROTOCOL_H
