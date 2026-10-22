// serial_protocol.cpp
// Multi Dryer HMI — ESP-Now communication with the Multi Dryer controller
//
// Replaces UART transport. All public functions keep the same signatures so
// callers (control_screen, diagnostics_screen, etc.) need no changes.

#include "serial_protocol.h"
#include "espnow_protocol.h"
#include "lvgl_v8_port.h"  // for lvgl_port_lock / lvgl_port_unlock
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_mac.h>

// ── Controller peer MAC — UPDATE with the MAC printed by the controller on boot ─
// How to find: boot MultiDryerController, read "[ESP-NOW] Controller MAC: XX:XX:XX:XX:XX:XX"
// Fill in the 6 bytes below, then re-flash the HMI.
static uint8_t CONTROLLER_PEER_MAC[6] = { 0x68, 0x09, 0x47, 0x9A, 0xB8, 0xD0 };  // ← paste Controller MAC here

// ── Receive buffer (set in ESP-Now callback, consumed in serialProtoUpdate) ───
static volatile bool        newStatusReceived = false;
static EspNowStatusPacket   pendingStatus;

// ── Connection tracking ───────────────────────────────────────────────────────
#define STATUS_TIMEOUT_MS  6000   // mark disconnected after 6 s with no packet

// ────────────────────────────────────────────────────────────────────
// Internal: map EspNowStatusPacket → DryerData
// (must be called while holding the LVGL mutex)
// ────────────────────────────────────────────────────────────────────
static void applyStatusPacket(const EspNowStatusPacket& pkt) {
    switch (pkt.state) {
        case DSTATE_DRYING:   dryerData.systemState = STATE_DRYING;   break;
        case DSTATE_COMPLETE: dryerData.systemState = STATE_COMPLETE; break;
        case DSTATE_PAUSED:   dryerData.systemState = STATE_PAUSED;   break;
        default:              dryerData.systemState = STATE_IDLE;     break;
    }

    dryerData.heaterOn   = (pkt.flags & FLAG_HEATER)  != 0;
    dryerData.fanOn      = (pkt.flags & FLAG_FAN)     != 0;
    dryerData.exhaustOn  = (pkt.flags & FLAG_EXHAUST) != 0;
    dryerData.pidEnabled = (pkt.flags & FLAG_PID)     != 0;
    dryerData.sht31Detected = (pkt.flags & FLAG_SHT31) != 0;

    dryerData.temperature     = pkt.temperature;
    dryerData.humidity        = pkt.humidity;
    dryerData.weight          = pkt.weight;
    dryerData.waterLoss       = pkt.waterLoss;
    dryerData.targetTemp      = pkt.setpoint;
    dryerData.targetWaterLoss = pkt.waterLossTarget;
    dryerData.pidOutput       = pkt.pidOutput;
    dryerData.dryingElapsedMs = (unsigned long)pkt.runtimeSeconds * 1000UL;
    dryerData.estimatedEDT    = pkt.estimatedEDT;

    dryerData.lastUpdateMs = millis();
    dryerData.connected    = true;
}

// ────────────────────────────────────────────────────────────────────
// Internal: send a command packet to the controller
// ────────────────────────────────────────────────────────────────────
static void sendCmd(uint8_t cmdType, float value = 0.0f) {
    EspNowCmdPacket pkt;
    pkt.packetType = ESPNOW_PKT_CMD;
    pkt.cmdType    = cmdType;
    pkt.value      = value;
    pkt.checksum   = espnowCmdChecksum(&pkt);
    esp_now_send(CONTROLLER_PEER_MAC, (const uint8_t*)&pkt, sizeof(pkt));
}

// ── ESP-Now receive callback (runs in Wi-Fi/ISR context — keep it short) ──────
// ESP32 Arduino core 3.x / IDF 5.x passes an esp_now_recv_info_t* (source MAC is
// in info->src_addr); the old const uint8_t* signature no longer matches the API.
static void onEspNowReceive(const esp_now_recv_info_t* info,
                             const uint8_t* data, int len) {
    (void)info;
    if (len < (int)sizeof(EspNowStatusPacket)) return;
    const EspNowStatusPacket* pkt = (const EspNowStatusPacket*)data;
    if (!espnowStatusValid(pkt)) {
        Serial.println("[ESP-Now] Bad status packet (type/checksum)");
        return;
    }
    if (!newStatusReceived) {
        memcpy((void*)&pendingStatus, pkt, sizeof(EspNowStatusPacket));
        newStatusReceived = true;
    }
}

// ── ESP-Now send callback ─────────────────────────────────────────────────────
static void onEspNowSent(const uint8_t* mac, esp_now_send_status_t status) {
    if (status != ESP_NOW_SEND_SUCCESS) {
        Serial.println("[ESP-Now] Send failed — check CONTROLLER_PEER_MAC");
    }
}

// ── MAC helpers ───────────────────────────────────────────────────────────────
static bool macIsZero(const uint8_t mac[6]) {
    static const uint8_t zero[6] = { 0, 0, 0, 0, 0, 0 };
    return memcmp(mac, zero, 6) == 0;
}

// Ask the Wi-Fi driver for the STA MAC, falling back to the eFuse factory MAC.
static bool readStaMac(uint8_t mac[6]) {
    memset(mac, 0, 6);
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK && !macIsZero(mac)) {
        return true;
    }
    memset(mac, 0, 6);
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK && !macIsZero(mac)) {
        return true;
    }
    return false;
}

// ────────────────────────────────────────────────────────────────────
void serialProtoInit() {
    Serial.println("[ESP-Now] Initialising Wi-Fi + ESP-NOW...");

    // Wi-Fi must be in STA mode (no connection) for ESP-Now
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    // The ESP32-S3 needs a moment after mode(STA) before the driver answers
    // esp_wifi_get_mac(); on core 3.x it can otherwise return zeros/fail.
    delay(100);

    // Read + print the MAC BEFORE esp_now_init(), so this line appears even if
    // ESP-NOW init fails. Retry a few times for slow-starting Wi-Fi drivers.
    uint8_t ownMac[6];
    bool haveMac = false;
    for (uint8_t attempt = 0; attempt < 10 && !haveMac; attempt++) {
        haveMac = readStaMac(ownMac);
        if (!haveMac) delay(50);
    }

    if (haveMac) {
        Serial.printf("[HMI] HMI MAC (paste into controller HMI_PEER_MAC): "
                      "%02X:%02X:%02X:%02X:%02X:%02X\n",
                      ownMac[0], ownMac[1], ownMac[2], ownMac[3], ownMac[4], ownMac[5]);
    } else {
        // Never hide the line: an unreadable MAC is itself a useful diagnostic.
        Serial.println("[HMI] HMI MAC: 00:00:00:00:00:00 (READ FAILED)");
        Serial.println("[HMI] Wi-Fi driver did not report a MAC — check the module/eFuse");
    }

    esp_err_t err = esp_now_init();
    if (err != ESP_OK) {
        Serial.printf("[ESP-Now] Init FAILED (0x%04X)\n", (unsigned)err);
        return;
    }
    Serial.println("[ESP-Now] esp_now_init OK");

    esp_now_register_recv_cb(onEspNowReceive);
    esp_now_register_send_cb(onEspNowSent);

    if (macIsZero(CONTROLLER_PEER_MAC)) {
        Serial.println("[ESP-Now] CONTROLLER_PEER_MAC is NOT set (00:00:00:00:00:00)");
        Serial.println("[ESP-Now] → put the controller MAC in serial_protocol.cpp and re-flash");
        return;
    }

    // Register the controller as peer
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, CONTROLLER_PEER_MAC, 6);
    peerInfo.channel = ESPNOW_WIFI_CHANNEL;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("[ESP-Now] Add peer FAILED — is CONTROLLER_PEER_MAC valid?");
    }

    Serial.printf("[HMI] ESP-Now ready. Controller peer: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  CONTROLLER_PEER_MAC[0], CONTROLLER_PEER_MAC[1], CONTROLLER_PEER_MAC[2],
                  CONTROLLER_PEER_MAC[3], CONTROLLER_PEER_MAC[4], CONTROLLER_PEER_MAC[5]);
}

void serialProtoUpdate() {
    // Apply any received status packet — acquire LVGL mutex to protect dryerData
    if (newStatusReceived) {
        newStatusReceived = false;
        EspNowStatusPacket pkt;
        memcpy(&pkt, (const void*)&pendingStatus, sizeof(pkt));
        if (lvgl_port_lock(-1)) {
            applyStatusPacket(pkt);
            lvgl_port_unlock();
        }
    }

    // Check connection timeout
    unsigned long now = millis();
    if (dryerData.connected && (now - dryerData.lastUpdateMs > STATUS_TIMEOUT_MS)) {
        if (lvgl_port_lock(-1)) {
            dryerData.connected = false;
            lvgl_port_unlock();
        }
    }
}

// ────────────────────────────────────────────────────────────────────
// Command senders
// ────────────────────────────────────────────────────────────────────
void sendSetTemperature(float temp) {
    sendCmd(CMD_SET_TEMPERATURE, temp);
}

void sendHeaterControl(bool on) {
    sendCmd(on ? CMD_HEATER_ON : CMD_HEATER_OFF);
}

void sendFanControl(bool on) {
    sendCmd(on ? CMD_FAN_ON : CMD_FAN_OFF);
}

void sendExhaustControl(bool on) {
    sendCmd(on ? CMD_EXHAUST_ON : CMD_EXHAUST_OFF);
}

void sendPIDStart() {
    sendCmd(CMD_PID_START);
}

void sendPIDStop() {
    sendCmd(CMD_PID_STOP);
}

void sendStartDrying() {
    dryerData.dryingStartMs = millis();
    dryerData.initialWeight = dryerData.weight;
    sendCmd(CMD_START_DRYING);
}

void sendStopDrying() {
    sendCmd(CMD_STOP_DRYING);
}

void sendPauseDrying() {
    sendCmd(CMD_PAUSE_DRYING);
}

void sendResumeDrying() {
    sendCmd(CMD_RESUME_DRYING);
}

void sendStatusRequest() {
    sendCmd(CMD_STATUS_REQUEST);
}

void sendSetWaterLoss(float pct) {
    sendCmd(CMD_SET_WATER_LOSS, pct);
}

void sendTareScale() {
    sendCmd(CMD_TARE_SCALE);
}

void sendCalibrateScale(float knownKg) {
    sendCmd(CMD_CALIBRATE_SCALE, knownKg);
}

void sendSensorTest() {
    sendCmd(CMD_SENSOR_TEST);
}
