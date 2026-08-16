# Flow Charts

Control-flow diagrams (Mermaid — renders on GitHub).

---

## 1. Controller Boot Flow

```mermaid
flowchart TD
    A["Power on / reset"] --> B["setup()<br/>1. SSR pins forced LOW"]
    B --> C["2. Init SHT31 (I2C)"]
    C --> D["3. Init HX711 + load cell<br/>restore factor from NVS"]
    D --> E["4. Init PID (MANUAL)"]
    E --> F["5. initDrying()<br/>restore session from NVS"]
    F --> G{"Session saved?"}
    G -->|"PAUSED"| H["Stay PAUSED — all outputs off"]
    G -->|"DRYING"| I["Resume: PID back on,<br/>offset re-applied"]
    G -->|"none"| J["IDLE"]
    H --> K["loop()"]
    I --> K
    J --> K
```

---

## 2. Main Loop (every iteration)

```mermaid
flowchart TD
    A["loop()"] --> B["updateDrying()<br/>1 Hz tick — refresh weight cache"]
    B --> C["pidCOMPUTE()<br/>heat or vent (SHT31 vent guard)"]
    C --> D["espnowUpdate()<br/>1 cmd/iteration + 1 Hz status"]
    D --> E["Debounce / housekeeping"]
    E --> A
```

---

## 3. Drying State Machine

```mermaid
stateDiagram-v2
    [*] --> IDLE
    IDLE --> DRYING: START_DRYING<br/>(capture initial weight, PID on)
    DRYING --> PAUSED: PAUSE_DRYING<br/>(all SSRs off, session saved)
    PAUSED --> DRYING: RESUME_DRYING<br/>(PID back on)
    DRYING --> COMPLETE: water loss ≥ target<br/>(auto — all SSRs off)
    COMPLETE --> IDLE: STOP_DRYING
    PAUSED --> IDLE: STOP_DRYING
    DRYING --> IDLE: STOP_DRYING / manual override
    IDLE --> IDLE: manual HEATER/FAN/EXHAUST
```

---

## 4. PID + Sensor-Fail Decision

```mermaid
flowchart TD
    A["pidCOMPUTE()"] --> B{"PID mode == AUTOMATIC?"}
    B -->|"NO (MANUAL)"| Z["Return — outputs untouched"]
    B -->|"YES"| C{"SHT31 valid?"}
    C -->|"NO"| V["VENT SAFETY:<br/>heater OFF, inlet OFF<br/>exhaust fan ON"]
    C -->|"YES"| D["pid.Compute()"]
    D --> E{"PID_OUTPUT > 0?"}
    E -->|"YES"| H["HEAT:<br/>SSR1 PTC + SSR3 inlet ON<br/>SSR4 OFF"]
    E -->|"NO"| V2["VENT:<br/>SSR1 + SSR3 OFF<br/>SSR4 exhaust fan ON"]
    V --> Z
    H --> Z
    V2 --> Z
```

---

## 5. ESP-NOW Command Path (HMI → Controller)

```mermaid
sequenceDiagram
    participant UI as LVGL screen (HMI)
    participant H as serial_protocol (HMI)
    participant C as espnow_link (Controller)
    participant S as State machine / PID

    UI->>H: button callback
    H->>H: optimistic UI state (instant)
    H->>C: EspNowCmdPacket (7 B)
    C->>C: validate + enqueue (8-slot ring)
    C->>C: dequeue 1 per loop
    C->>S: handleCmd()
    S-->>C: effect (start/pause/…)
    C-->>H: status packet (1 Hz, corrects UI)
```

---

> Related: [`../system-architecture.md`](../system-architecture.md) ·
> [`api/espnow-protocol.md`](../api/espnow-protocol.md) ·
> [`guides/bring-up-checklist.md`](../guides/bring-up-checklist.md)
