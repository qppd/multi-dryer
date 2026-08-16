# Block Diagram

System block diagram (Mermaid — renders on GitHub).

```mermaid
flowchart TB
    subgraph AC["220 VAC Mains"]
        M[AC-L / AC-N]
    end

    subgraph PWR["Power"]
        PSU["220 V → 12 V 5 A PSU"]
        BUCK["12 V → 5 V 3 A buck"]
        F1["Fuse 10A"] & F2["Fuse 2A"] & F3["Fuse 2A"]
        TCO["Thermal cutoff<br/>(heater branch)"]
    end

    subgraph CTRL["CONTROLLER — ESP32 38-pin (WROOM-32)"]
        direction TB
        ESP["ESP32 SoC"]
        subgraph MOD["Firmware modules"]
            SHT["SHT31_CONFIG.h"]
            LC["LOADCELL_CONFIG.h"]
            PID["PID_CONFIG.h<br/>+ vent guard"]
            ST["DRYER_STATE.h<br/>state machine / NVS"]
            LNK["espnow_link.h"]
        end
    end

    subgraph SENS["Sensors"]
        S31["SHT31<br/>temp/humidity<br/>(I2C 21/22)"]
        HX["HX711<br/>(DOUT 35 / SCK 32)"]
        CELL["Load cells 4× 50 kg<br/>(summed bridge)"]
    end

    subgraph SSR["SSR Bank — 3× SSR-40DA 40 A (3–32 VDC in)"]
        R1["SSR1 — PTC heater<br/>(GPIO 26)"]
        R3["SSR3 — inlet fan<br/>(GPIO 27)"]
        R4["SSR4 — exhaust fan<br/>(GPIO 13)"]
    end

    subgraph LOADS["AC Loads"]
        HTR["PTC heater 1500 W"]
        IF["Inlet fan"]
        EF["Exhaust fan"]
    end

    subgraph HMI["HMI — ESP32-S3 + 7″ touch LCD"]
        HMI_ESP["ESP32-S3"]
        DSP["RGB LCD 800×480<br/>(ST7262)"]
        TCH["GT911 touch"]
        LVGL["LVGL 8.x UI<br/>6 screens + alerts"]
        PROTO["serial_protocol.*<br/>(ESP-NOW)"]
    end

    M --> PSU --> BUCK --> ESP
    ESP --> S31 & HX
    CELL --> HX
    S31 -.->|I2C| SHT
    HX -.->|kg| LC
    M --> F1 --> TCO --> R1
    M --> F2 --> R3
    M --> F3 --> R4
    R1 --> HTR
    R3 --> IF
    R4 --> EF

    SHT --> PID
    LC --> ST
    PID --> ST
    ST --> LNK
    LNK -. "ESP-NOW 1 Hz status" .-> PROTO
    PROTO -. "commands" .-> LNK
    PROTO --> LVGL
    LVGL --> DSP
    TCH --> LVGL
    HMI_ESP --> DSP & TCH
```

## Legend

| Block | Notes |
|---|---|
| Mains | 220 VAC, fused per branch (10/2/2 A) |
| PSU | 220 V → 12 V 5 A PSU + 12 V → 5 V 3 A buck (5 V rail → ESP32; onboard regulator makes 3.3 V) |
| Thermal cutoff | Mandatory, in series with the PTC heater branch |
| SSR bank | 3× SSR-40DA 40 A (SSR1 heater, SSR3 inlet, SSR4 exhaust); opto-isolated 3–32 VDC input; firmware forces inputs LOW at boot |
| ESP-NOW | Wireless, channel 1, packed binary packets (see `api/espnow-protocol.md`) |
| Sensors | SHT31 on I2C; HX711 fully digital (no ADC2 dependence) |
