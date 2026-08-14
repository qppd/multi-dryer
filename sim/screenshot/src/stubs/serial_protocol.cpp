// serial_protocol.cpp — host stand-in for the real ESP-NOW link.
// In the simulator there is no controller, so all senders are no-ops;
// the renderer feeds dryerData directly to drive the screens.

#include "serial_protocol.h"
#include "dryer_data.h"

void serialProtoInit() {}
void serialProtoUpdate() {}

void sendSetTemperature(float temp)     { (void)temp; }
void sendHeaterControl(bool on)         { (void)on; }
void sendFanControl(bool on)            { (void)on; }
void sendExhaustControl(bool on)        { (void)on; }
void sendPIDStart() {}
void sendPIDStop() {}
void sendStartDrying() {}
void sendStopDrying() {}
void sendPauseDrying() {}
void sendResumeDrying() {}
void sendStatusRequest() {}
void sendSetWaterLoss(float pct)        { (void)pct; }
void sendTareScale() {}
void sendCalibrateScale(float knownKg)  { (void)knownKg; }
void sendSensorTest() {}
