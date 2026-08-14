# Block Diagram

System block diagram (Mermaid — renders on GitHub).

```mermaid
flowchart TB
    subgraph AC["220 VAC Mains"]
        M[AC-L / AC-N]
    end

    subgraph PWR["Power"]
        PSU["5 V isolated PSU<br/>(HLK-PM03 ≥3 A)"]
        F1["Fuse 10A"] & F2["Fuse 5A"] & F3["Fuse 2A"] & F4["Fuse 2A"]
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
        CELL["Load cell 1–50 kg"]
    end

    subgraph SSR["SSR Bank (3–32 VDC in)"]
        R1["SSR1 — PTC heater<br/>(GPIO 26)"]
        R2["SSR2 — exhaust outlet<br/>(GPIO 25)"]
        R3["SSR3 — inlet fan<br/>(GPIO 27)"]
        R4["SSR4 — exhaust fan<br/>(GPIO 13)"]
    end

    subgraph LOADS["AC Loads"]
        HTR["PTC heater 500–2000 W"]
        OUT["Exhaust outlet"]
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

    M --> PSU --> ESP
    ESP --> S31 & HX
    CELL --> HX
    S31 -.->|I2C| SHT
    HX -.->|kg| LC
    M --> F1 --> TCO --> R1
    M --> F2 --> R2
    M --> F3 --> R3
    M --> F4 --> R4
    R1 --> HTR
    R2 --> OUT
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
| Mains | 220 VAC, fused per branch (10/5/2/2 A) |
| PSU | Isolated 5 V; ESP32's onboard regulator makes 3.3 V |
| Thermal cutoff | Mandatory, in series with the PTC heater branch |
| SSR bank | Opto-isolated 3–32 VDC input; firmware forces inputs LOW at boot |
| ESP-NOW | Wireless, channel 1, packed binary packets (see `api/espnow-protocol.md`) |
| Sensors | SHT31 on I2C; HX711 fully digital (no ADC2 dependence) |
