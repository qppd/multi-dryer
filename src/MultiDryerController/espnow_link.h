// espnow_link.h
// Multi Dryer Controller — ESP-Now link to the HMI board (ESP32-S3 + touch).
//
// Ported from the HMIDisplay reference (references/HMIDisplay/serial_protocol.cpp),
// but from the controller side: we SEND EspNowStatusPacket (SHT31 + load cell
// data) and RECEIVE EspNowCmdPacket (control commands from the HMI).
//
// Wiring: none between boards — wireless, same channel as the HMI (channel 1).
//
// MAC setup:
//   1. Boot the controller, read "[ESP-NOW] Controller MAC: XX:XX:XX:XX:XX:XX"
//   2. Paste that MAC into NODEMCU_PEER_MAC in the HMI's serial_protocol.cpp
//   3. Boot the HMI, read its "NodeMCU peer MAC" print
//   4. Paste that MAC into HMI_PEER_MAC below, re-flash the controller

#ifndef ESPNOW_LINK_H
#define ESPNOW_LINK_H

#include <WiFi.h>
#include <esp_now.h>
#include "espnow_protocol.h"
#include "PINS_CONFIG.h"
#include "SHT31_CONFIG.h"
#include "LOADCELL_CONFIG.h"
#include "PID_CONFIG.h"

// ── HMI peer MAC — UPDATE with the MAC printed by the HMI on boot ─────────────
static uint8_t HMI_PEER_MAC[6] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

// ── Link state ────────────────────────────────────────────────────────────────
// hmiReachable is written from the ESP-NOW send callback (Wi-Fi task context)
// and read from loop() — hence volatile.
static volatile bool hmiReachable   = false;   // last ESP-NOW send was ACKed by the HMI
static unsigned long lastStatusSent = 0;

#define STATUS_SEND_INTERVAL_MS  1000   // status packet cadence (1 Hz)

// ── Command queue (ESP-Now callback → loop context) ───────────────────────────
// The ESP-Now receive callback runs in the Wi-Fi task context — never call
// blocking code (HX711 reads, I2C, prints) from there. We just store the
// latest valid command and process it in espnowUpdate() from loop().
static volatile uint8_t pendingCmdType  = 0;
static volatile float   pendingCmdValue = 0.0f;
static volatile bool    pendingCmdFlag  = false;

static void handleCmd(uint8_t cmdType, float value);

// ── Receive callback (Wi-Fi task context — keep it short) ─────────────────────
static void onEspNowReceive(const uint8_t* mac, const uint8_t* data, int len) {
    if (len < (int)sizeof(EspNowCmdPacket)) return;
    const EspNowCmdPacket* pkt = (const EspNowCmdPacket*)data;
    if (!espnowCmdValid(pkt)) {
        Serial.println("[ESP-NOW] Bad command packet (type/checksum)");
        return;
    }
    pendingCmdType  = pkt->cmdType;
    pendingCmdValue = pkt->value;
    pendingCmdFlag  = true;
}

// ── Send callback (Wi-Fi task context — keep it short) ────────────────────────
static void onEspNowSent(const uint8_t* mac, esp_now_send_status_t status) {
    hmiReachable = (status == ESP_NOW_SEND_SUCCESS);
}

// ── Build + send one status packet with current sensor data ───────────────────
static void sendStatusNow() {
    EspNowStatusPacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.packetType  = ESPNOW_PKT_STATUS;
    pkt.state       = DSTATE_IDLE;                       // state machine: not implemented yet

    // Flags reflect live outputs so the HMI dashboard shows real state
    uint8_t flags = sht31OK ? FLAG_SHT31 : 0;
    if (pid.GetMode() == AUTOMATIC) {
        flags |= FLAG_PID;
        if (PID_OUTPUT > 0) flags |= FLAG_HEATER | FLAG_FAN;   // heating: PTC + inlet fan
        else                flags |= FLAG_EXHAUST;              // venting
    }
    pkt.flags       = flags;
    pkt.temperature = getTemperature();                  // °C
    pkt.humidity    = getHumidity();                     // %
    pkt.weight      = readLoadCell();                    // kg
    pkt.setpoint    = (float)TEMPERATURE_SETPOINT;       // target °C
    pkt.pidOutput   = (float)PID_OUTPUT;
    // waterLoss / waterLossTarget / runtimeSeconds / estimatedEDT: 0 until
    // the drying state machine is implemented
    pkt.checksum    = espnowStatusChecksum(&pkt);
    esp_now_send(HMI_PEER_MAC, (const uint8_t*)&pkt, sizeof(pkt));
}

// ── Init (call once in setup()) ───────────────────────────────────────────────
void initEspNow() {
    // Wi-Fi must be in STA mode (no connection) for ESP-Now
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // Print own MAC so user can copy it into the HMI's NODEMCU_PEER_MAC
    Serial.printf("[ESP-NOW] Controller MAC (copy to HMI NODEMCU_PEER_MAC): %s\n",
                  WiFi.macAddress().c_str());

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] Init FAILED");
        return;
    }

    esp_now_register_recv_cb(onEspNowReceive);
    esp_now_register_send_cb(onEspNowSent);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, HMI_PEER_MAC, 6);
    peerInfo.channel = ESPNOW_WIFI_CHANNEL;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ESP-NOW] Add peer FAILED — is HMI_PEER_MAC set?");
    }

    Serial.printf("[ESP-NOW] Link ready. HMI peer: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  HMI_PEER_MAC[0], HMI_PEER_MAC[1], HMI_PEER_MAC[2],
                  HMI_PEER_MAC[3], HMI_PEER_MAC[4], HMI_PEER_MAC[5]);
}

// ── Process queued commands + send periodic status (call every loop()) ────────
void espnowUpdate() {
    // 1) Handle any command the HMI sent (processed from loop context)
    if (pendingCmdFlag) {
        pendingCmdFlag = false;
        handleCmd(pendingCmdType, pendingCmdValue);
    }

    // 2) Periodic status broadcast (1 Hz)
    unsigned long now = millis();
    if (now - lastStatusSent >= STATUS_SEND_INTERVAL_MS) {
        lastStatusSent = now;
        sendStatusNow();
    }
}

// ── Command dispatch ──────────────────────────────────────────────────────────
static void handleCmd(uint8_t cmdType, float value) {
    switch (cmdType) {
        case CMD_STATUS_REQUEST:
            sendStatusNow();              // HMI wants an immediate reading
            break;
        case CMD_SENSOR_TEST:
            sendStatusNow();
            break;

        // Load cell calibration
        case CMD_TARE_SCALE:
            tareLoadCell();
            break;
        case CMD_CALIBRATE_SCALE:
            calibrateLoadCell(value);
            break;

        // PID temperature control
        case CMD_SET_TEMPERATURE:
            setPIDSetpoint(value);
            break;
        case CMD_PID_START:
        case CMD_START_DRYING:            // temporary alias until the state machine
            startPID();
            break;
        case CMD_PID_STOP:
        case CMD_STOP_DRYING:
            stopPID();
            break;

        // Manual heater override (HMI manual-operation screen)
        case CMD_HEATER_ON:
            stopPID();
            operateSSR(1, true);          // PTC heater ON
            operateSSR(3, true);          // inlet fan ON (airflow with heat!)
            break;
        case CMD_HEATER_OFF:
            stopPID();
            break;

        // PAUSE / RESUME / FAN / EXHAUST / WATER_LOSS: pending the drying
        // state machine
        default:
            Serial.printf("[ESP-NOW] CMD 0x%02X received (not implemented yet)\n", cmdType);
            break;
    }
}

#endif // ESPNOW_LINK_H
