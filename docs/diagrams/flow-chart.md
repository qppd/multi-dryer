# Flow Charts

Control-flow diagrams (Mermaid — renders on GitHub).

---

## 1. Controller Boot Flow

```mermaid
flowchart TD
    A["Power on / reset"] --> B["setup()<br/>1. SSR pins forced LOW (fail-safe)"]
    B --> C["2. initLoadCell() — HX711<br/>restore calibration factor from NVS"]
    C --> D["3. initSHT31() — I2C<br/>temp/humidity, presence check"]
    D --> E["4. initPID() — MANUAL (off)"]
    E --> F["5. initDrying() — restore<br/>session + config from NVS"]
    F --> G["6. initEspNow() — pair peer<br/>+ register callbacks"]
    G --> H{"Saved session?"}
    H -->|"PAUSED"| I["Stay PAUSED — all outputs off"]
    H -->|"DRYING"| J["Resume DRYING — PID back on,<br/>HX711 offset re-applied"]
    H -->|"none / COMPLETE"| K["IDLE — config restored<br/>(setpoint + water-loss target)"]
    I --> L["loop()"]
    J --> L
    K --> L
```

---

## 2. Main Loop (every iteration)

```mermaid
flowchart TD
    A["loop()"] --> B["updateSHT31()<br/>non-blocking sensor read<br/>state machine"]
    B --> C["pidCOMPUTE()<br/>heat or vent — SHT31 vent guard<br/>(PID_v1 throttles to 2 s)"]
    C --> D["updateDrying()<br/>1 Hz tick — weight cache, water loss,<br/>EDT, auto-complete, session save"]
    D --> E["espnowUpdate()<br/>dequeue 1 cmd + 1 Hz status"]
    E --> F["5 s debug status print"]
    F --> A
```

---

## 3. Drying State Machine

```mermaid
stateDiagram-v2
    [*] --> IDLE: boot — no saved session
    [*] --> DRYING: boot — saved DRYING<br/>(PID back on)
    [*] --> PAUSED: boot — saved PAUSED<br/>(all outputs off)
    IDLE --> DRYING: START_DRYING<br/>(capture initial weight, PID on)
    DRYING --> PAUSED: PAUSE_DRYING<br/>(all SSRs off, session saved)
    PAUSED --> DRYING: RESUME_DRYING<br/>(PID back on)
    DRYING --> COMPLETE: water loss ≥ target<br/>(auto — all SSRs off, session cleared)
    COMPLETE --> IDLE: STOP_DRYING
    COMPLETE --> DRYING: START_DRYING<br/>(fresh session)
    PAUSED --> IDLE: STOP_DRYING
    DRYING --> IDLE: STOP_DRYING / manual override
    IDLE --> IDLE: manual HEATER/FAN/EXHAUST
```

> **Boot-resume:** `initDrying()` restores a saved session from NVS — a DRYING
> session re-enables the PID; a PAUSED session stays off. Completed sessions
> clear the NVS marker, so a finished cycle always boots to IDLE.

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
