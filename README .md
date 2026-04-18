# Smart Environmental Monitoring System

**STM32F401RE + FreeRTOS multi-sensor data acquisition system with Producer–Consumer architecture.**

A bare-metal embedded project built from scratch using STM32 HAL, FreeRTOS (CMSIS-RTOS v1), and a message queue–based task architecture. Designed as a foundation for future IoT gateway nodes.

---

## Overview

This project integrates three environmental sensors on an STM32F401RE (Cortex-M4, 84 MHz) and uses FreeRTOS to decouple sensor acquisition from data output. Sensor data is packed into a struct, passed through a FreeRTOS message queue, and consumed by an output task that streams the data over UART for verification via PuTTY (COM3, 115200 baud).

The Consumer task is intentionally structured as a standalone module so that the UART output can later be swapped for an ESP8266 / MQTT uplink without touching the sensor-side code.

---

## System Architecture

```
 ┌──────────┐   ┌──────┐   ┌────────┐
 │  AHT20   │   │ MQ-2 │   │  LDR   │
 │ (I2C)    │   │(ADC) │   │ (ADC)  │
 └────┬─────┘   └──┬───┘   └───┬────┘
      │            │           │
      └────────────┴───────────┘
                   │
           ┌───────▼────────┐
           │   SensorTask   │   Producer
           │  (reads all 3) │
           └───────┬────────┘
                   │
           ┌───────▼────────┐
           │  osMessageQ    │   FreeRTOS Queue
           └───────┬────────┘
                   │
           ┌───────▼────────┐
           │   OutputTask   │   Consumer
           │  (UART / TODO: │
           │   WiFi uplink) │
           └───────┬────────┘
                   │
              UART2 → PuTTY
              (COM3 @ 115200)
```

---

## Hardware

| Component | Interface | MCU Pin |
|-----------|-----------|---------|
| STM32F401RE (Nucleo-64) | — | — |
| AHT20 Temperature & Humidity | I2C1 | PB6 (SCL) / PB7 (SDA) |
| MQ-2 Gas Sensor | ADC1_CH0 | PA0 |
| LDR (Light) | ADC1_CH1 | PA1 |
| UART Debug | USART2 | PA2 (TX) / PA3 (RX) |

---

## Software Stack

- **Toolchain**: STM32CubeIDE
- **HAL**: STM32Cube HAL (F4)
- **RTOS**: FreeRTOS via CMSIS-RTOS v1
- **Verification**: PuTTY serial monitor on COM3 @ 115200 baud

### Tasks

| Task | Role | Priority | Stack |
|------|------|----------|-------|
| `SensorTask` | Producer — reads AHT20 (I2C), MQ-2 (ADC), LDR (ADC), packs into struct, enqueues | Normal | 512 words |
| `OutputTask` | Consumer — dequeues sensor struct, formats, prints via UART | Normal | 512 words |

### Shared Data Structure

```c
typedef struct {
    float    temp;   // °C
    float    humi;   // %RH
    uint32_t mq2;    // raw ADC
    uint32_t light;  // raw ADC
} SensorData_t;
```

---

## Engineering Challenges & Solutions

This section documents the non-trivial bugs encountered during development. These were the parts that actually taught me something about RTOS internals.

### 1. Queue passing dangling pointer → corrupted / garbage data

**Symptom:** Consumer task printed values only once, then received garbage (temperature showing `-50 °C`, humidity `0%`, values jumping randomly).

**Root cause:** CMSIS-RTOS v1 `osMessagePut` / `osMessageQueue` passes a **pointer**, not a copy of the struct. My original code did:

```c
void SensorTask(void *argument) {
    SensorData_t data;           // ❌ stack-allocated, local to this iteration
    // ... fill data ...
    osMessagePut(queue, (uint32_t)&data, 0);
}
```

The pointer was valid when enqueued, but by the time the consumer dequeued it, `SensorTask`'s stack frame had been reused — classic **dangling pointer**.

**Fix:** Promote the buffer to `static` so it persists for the program's lifetime:

```c
static SensorData_t data;        // ✅ lifetime = entire program
```

For a higher-throughput or multi-producer design, the correct long-term fix would be a queue that copies by value (CMSIS-RTOS v2's `osMessageQueueNew` with `msg_size = sizeof(SensorData_t)`), or a memory pool. Noted as future work.

---

### 2. `printf` hang → task stack overflow

**Symptom:** Output froze mid-line (often after printing `RAW`), entire system unresponsive.

**Root cause:** `printf` with `%f` format specifiers pulls in floating-point formatting code, which eats significantly more stack than integer `printf`. The default task stack (128 words) was not enough.

**Fix:** Raised `OutputTask` stack to **512 words**. Verified by checking `uxTaskGetStackHighWaterMark` during development.

---

### 3. AHT20 returning −50 °C

**Symptom:** Temperature reading pinned at `-50.0`.

**Root cause:** AHT20's conversion formula is `temp = (raw / 2^20) * 200 - 50`. When the I2C read silently failed, `raw = 0`, which produces exactly `-50 °C`. The sensor wasn't broken — the I2C transaction was failing and I was using the zeroed buffer anyway.

**Fix:** Check `HAL_I2C_Master_Receive` return status, only update the shared struct when the read succeeds, and keep the last-known-good value otherwise. Also added the required ~80 ms wait after issuing the measurement trigger command, which the datasheet specifies but is easy to miss.

---

## Verification

Since this prototype uses UART instead of WiFi, all output was verified in real time using **PuTTY on COM3 at 115200 baud, 8-N-1**. Sample output:

```
T=27.13C H=58.51% MQ2=24 Light=281
T=27.18C H=58.58% MQ2=24 Light=274
```

*(Live capture from PuTTY, COM3 @ 115200 baud)*

The `OutputTask` is written so that replacing the `HAL_UART_Transmit` call with an ESP8266 AT-command or MQTT publish is a drop-in change — the task contract (dequeue `SensorData_t`, ship it somewhere) stays the same.

---

## Build & Flash

1. Open the project in **STM32CubeIDE** (File → Open Projects from File System).
2. Build (Project → Build Project, or `Ctrl+B`).
3. Connect the Nucleo-F401RE via USB.
4. Flash (Run → Debug, or the green bug icon).
5. Open PuTTY → Serial → `COM3` @ `115200` baud to see live sensor output.

---

## Project Structure

```
.
├── Core/
│   ├── Inc/          # Headers (adc.h, i2c.h, usart.h, main.h, FreeRTOSConfig.h, ...)
│   └── Src/          # Sources (adc.c, i2c.c, usart.c, freertos.c, main.c, ...)
├── Drivers/          # STM32 HAL + CMSIS
├── Middlewares/
│   └── Third_Party/
│       └── FreeRTOS/ # FreeRTOS kernel
├── Smart_Environmental_Monitoring_System.ioc   # CubeMX config
└── README.md
```

---

## Future Work

- [ ] ESP8266 / ESP-01 integration via UART AT commands
- [ ] MQTT uplink to a broker (Mosquitto / HiveMQ)
- [ ] Migrate CMSIS-RTOS v1 → v2 for pass-by-value queue semantics
- [ ] Convert ADC polling to DMA + double-buffer
- [ ] Add UART mutex once a second producer writes to the same UART
- [ ] Simple web dashboard (Grafana + InfluxDB) for historical data

---

## Author

**ASH** — M.S. Computer Science, Jinan University
Self-taught firmware (STM32, RTOS, UEFI/EDK2) and embedded AI (Jetson + OpenPose).
