Smart Environmental Monitoring System

STM32F401RE + FreeRTOS multi-sensor acquisition with a producer–consumer task architecture.

Three environmental sensors on a Cortex-M4, sampled by one task, published through a FreeRTOS message queue, and consumed by an output task that streams over UART. Built on STM32 HAL and CMSIS-RTOS v1, intended as the base for an IoT gateway node.

Overview

The point of the architecture is that the consumer takes a SensorData_t off a queue and ships it, without knowing whether "ship it" means a UART line, an ESP8266 AT sequence, or an MQTT publish. Swapping the transport should be a change in one task.

Most of what follows is about the gap between that being true in the diagram and being true in the code.

 ┌──────────┐   ┌──────┐   ┌────────┐
 │  AHT20   │   │ MQ-2 │   │  LDR   │
 │  (I2C1)  │   │(ADC) │   │ (ADC)  │
 └────┬─────┘   └──┬───┘   └───┬────┘
      │            │           │
      └────────────┴───────────┘
                   │
           ┌───────▼────────┐
           │   SensorTask   │  Producer, ~1 Hz
           │  fills one     │  enqueues &data
           │  static buffer │
           └───────┬────────┘
                   │  osMessageQ, depth 4 — carries a POINTER
           ┌───────▼────────┐
           │   WifiTask     │  Consumer
           │  (UART today)  │  blocks on osMessageGet
           └───────┬────────┘
                   │
              USART2 → serial terminal
              (115200 8-N-1)
Hardware
Component	Interface	MCU Pin
STM32F401RE (Nucleo-64)	—	—
AHT20 temperature & humidity	I2C1	PB6 (SCL) / PB7 (SDA)
MQ-2 gas sensor	ADC1_CH0	PA0
LDR (light)	ADC1_CH1	PA1
Debug UART	USART2	PA2 (TX) / PA3 (RX)

Clock: HSI 16 MHz → PLL (M=16, N=336, P=4) → SYSCLK 84 MHz. ADC clock: PCLK2 / 4 = 21 MHz. At 144-cycle sampling plus 12 cycles for 12-bit resolution, one conversion takes (144 + 12) / 21 MHz ≈ 7.4 µs.

Software
Toolchain: STM32CubeIDE, arm-none-eabi-gcc, newlib-nano
HAL: STM32Cube HAL (F4)
RTOS: FreeRTOS via CMSIS-RTOS v1
Verification: serial terminal, 115200 8-N-1
Tasks
Task	Role	Priority	Stack
SensorTask	Producer — reads AHT20, MQ-2, LDR into a struct, enqueues a pointer to it	Normal	128 words
WifiTask	Consumer — blocks on the queue, formats, transmits over UART	Normal	256 words
defaultTask	CubeMX-generated placeholder, for(;;) osDelay(1);	Normal	128 words

Both working tasks run at the same priority deliberately. Ordering comes from the queue: the consumer blocks in osMessageGet until the producer publishes. A priority split would add a scheduling assumption without buying anything.

defaultTask is generated scaffolding that was never removed. It holds a TCB and 512 bytes of heap for nothing — see Known Limitations.

Timing and sizing
Parameter	Value
Nominal sample period	osDelay(1000)
Actual period	≈ 1.08 s — osDelay is relative, so the 80 ms AHT20 conversion wait and the UART transmit add on top
Queue depth	4
configTOTAL_HEAP_SIZE	15360 bytes
AHT20 conversion wait	80 ms (datasheet minimum)
ADC poll timeout	20 ms
ADC sampling time	144 cycles
Shared data structure

Currently declared inside Core/Src/freertos.c:

c
typedef struct {
    float    temp;   /* °C  */
    float    humi;   /* %RH */
    uint32_t mq2;    /* raw ADC, 12-bit */
    uint32_t light;  /* raw ADC, 12-bit */
} SensorData_t;
Engineering Challenges

These are the non-trivial failures hit during development, with the fix that shipped and — where the fix is only conditionally correct — the condition it depends on.

1. The queue carries a pointer, not a copy

Symptom: the consumer printed one good sample, then garbage — temperature at -50 °C, humidity 0%, values jumping.

Root cause: CMSIS-RTOS v1's osMessagePut takes a uint32_t, so what travels through the queue is a pointer. The original producer enqueued the address of a stack-local struct:

c
void StartSensorTask(void *argument) {
    SensorData_t data;           /* stack-allocated, local to this iteration */
    /* ... fill data ... */
    osMessagePut(queue, (uint32_t)&data, 0);
}

The pointer was valid at enqueue time, but by the time the consumer dequeued it, SensorTask's stack frame had been reused. Classic dangling pointer.

Fix that shipped:

c
static SensorData_t data;        /* lifetime = whole program */

What this fix actually guarantees — and what it does not. static solves the lifetime problem, and the garbage stops. But it trades a lifetime bug for an aliasing one: with a queue of depth 4 and a single shared buffer, four enqueued pointers would all refer to the same bytes. The producer overwrites that buffer on its next iteration whether or not the consumer has finished reading it.

This version is therefore correct only under an unstated assumption: the consumer drains the queue within one sample period. It holds here because the consumer blocks on osWaitForever and a 1 Hz sample takes it milliseconds to process, so the queue's occupancy never exceeds 1. Raise the sample rate, add a second consumer, or slow the output path, and the same code races.

osMessagePut's return value is also discarded, so a full queue would drop samples with no indication.

The version-independent fixes, for reference: CMSIS-RTOS v2's osMessageQueueNew(4, sizeof(SensorData_t), NULL) copies by value and removes the class of bug entirely; within v1, a ring of buffers one deeper than the queue, with the write index advanced only on a successful put, gives the same guarantee. Both are on the roadmap.

2. printf hang — task stack overflow

Symptom: output froze mid-line and the system stopped responding.

Root cause: printf with %f pulls in newlib's floating-point formatting, which needs considerably more stack than the integer path. The CubeMX default of 128 words was not enough for the consumer.

Fix: the consumer's stack was raised from 128 to 256 words.

Also required, and easy to miss: newlib-nano omits float support from printf by default, so %f silently produces nothing. The build sets -u _printf_float in the linker flags. Without it the symptom is different but equally confusing — the format specifier just disappears from the output.

Still open. The consumer is not the only task that logs: SensorTask also calls printf on an AHT20 read failure. newlib's printf family is not reentrant unless FreeRTOS is built with configUSE_NEWLIB_REENTRANT, which this project does not set — so both tasks share one _impure_ptr and one internal buffer. That is a live race, not a hypothetical one; it is rare only because AHT20 failures are rare. See Known Limitations.

3. AHT20 returning −50 °C

Symptom: temperature pinned at exactly -50.0.

Root cause: the conversion is temp = (raw / 2²⁰) × 200 − 50. When the I2C read failed silently, the receive buffer stayed zeroed, and raw = 0 produces exactly −50. The sensor was fine; the transaction was not, and the zeroed buffer was used anyway.

Fix: check HAL_I2C_Master_Receive's return status and only update the shared struct on success, so the previous good reading is carried forward instead of being replaced by a zero. The datasheet's ~80 ms wait after the 0xAC 0x33 0x00 measurement trigger was also added — it is specified but easy to skip, and skipping it produces the same symptom intermittently.

What this covers, and what it leaves open. Checking the HAL return code closes one of three ways this part hands back a plausible-looking wrong number:

#	Condition	Result	Covered?
1	I2C transfer fails	raw = 0 → −50 °C, obviously wrong	Yes
2	Read lands inside the conversion window	status bit 7 set; payload is the previous sample	No
3	0xBE calibration never took effect	status bit 3 clear; readings uncalibrated	No

Case 1 gets noticed because −50 is absurd. Cases 2 and 3 return values in a believable range, which makes them the expensive ones. Closing them needs the status byte checked and the 7th byte (CRC-8, polynomial 0x31) read and verified — the current read requests only 6 bytes, so the CRC is not even fetched. On the roadmap.

ADC: one peripheral, two channels

ADC1 runs in single-conversion mode with NbrOfConversion = 1, so the channel must be reselected before every read. Issuing back-to-back HAL_ADC_Start / PollForConversion pairs without reconfiguring returns the same pin twice — a failure that produces two plausible numbers and no error. HAL_ADC_ConfigChannel is therefore called before each conversion, and HAL_ADC_Stop after each one.

Sampling time is 144 cycles, not the CubeMX default of 3. The MQ-2 output and the LDR divider are high-impedance sources; the sample-and-hold capacitor needs the longer aperture to charge to the true source voltage. Too short an aperture reads low and lets the previous channel's residue bleed into the next reading.

Verification

Serial output at 115200 8-N-1:

T=27.13C H=58.51% MQ2=24 Light=281
T=27.18C H=58.58% MQ2=24 Light=274

The WifiTask output path is written so that replacing the printf with an ESP8266 AT sequence or an MQTT publish leaves the task contract unchanged: block on the queue, take a SensorData_t, send it somewhere.

Known Limitations

Listed because they are known, not because they are acceptable. Each is addressed on the roadmap below.

Queue holds pointers into a single shared buffer. Correct only while the consumer keeps up within one sample period — see Challenge 1.
AHT20 status byte and CRC are not checked. Two silent wrong-data paths remain open — see Challenge 3. AHT20_Init's return value is also discarded, so an uncalibrated part looks identical to a healthy one.
HAL return values are discarded on the ADC path. On a PollForConversion timeout, HAL_ADC_GetValue returns the previous conversion and stale data flows onward as if fresh — structurally the same failure as Challenge 3, in a peripheral where it was not looked for.
printf is called from two tasks without configUSE_NEWLIB_REENTRANT. See Challenge 2. The _write override in usart.c also uses HAL_MAX_DELAY, so a stalled UART blocks the calling task indefinitely.
Stack headroom has not been measured. INCLUDE_uxTaskGetStackHighWaterMark is not enabled in FreeRTOSConfig.h, and neither is configCHECK_FOR_STACK_OVERFLOW. The 256-word figure in Challenge 2 has margin but no data behind it, and an overflow would present as an unexplained hang rather than a hook with the task name in hand.
SensorData_t is declared inside freertos.c, not a header. No separate uplink module can include it, which makes the decoupling claim above aspirational rather than structural.
defaultTask is still created, and MX_I2C1_Init() is called twice in main.c. Generated scaffolding that was never cleaned up.
Flash and RAM footprint not recorded.
Build & Flash
Open in STM32CubeIDE (File → Open Projects from File System).
Build (Ctrl+B).
Connect the Nucleo-F401RE over USB.
Flash (Run → Debug).
Open a serial terminal on the ST-Link VCP at 115200 8-N-1.
Project Structure
.
├── Core/
│   ├── Inc/          # aht20.h, adc.h, i2c.h, usart.h, main.h, FreeRTOSConfig.h
│   └── Src/          # freertos.c, aht20.c, adc.c, i2c.c, usart.c, main.c
├── Drivers/          # STM32 HAL + CMSIS
├── Middlewares/
│   └── Third_Party/
│       └── FreeRTOS/
├── STM32F401RETX_FLASH.ld
├── STM32F401RETX_RAM.ld
├── multisensor-rtos.ioc
└── README.md
Roadmap

Ordered by what removes the most risk first, not by what is most visible.

 Replace the shared static buffer with a ring one deeper than the queue, and check osMessagePut's return value
 Validate the AHT20 status byte (busy / calibrated) and the CRC byte; check AHT20_Init's return value
 Check the ADC HAL return values; mark stale readings so the consumer can distinguish them from fresh ones
 Serialise all logging behind one lock, and replace %f with fixed-point integer formatting so the float path leaves the hot loop entirely
 Enable configCHECK_FOR_STACK_OVERFLOW, INCLUDE_uxTaskGetStackHighWaterMark and configUSE_MALLOC_FAILED_HOOK; record measured stack headroom and footprint here
 Remove defaultTask; move SensorData_t into its own header
 Switch osDelay to osDelayUntil for a fixed cadence
 Migrate CMSIS-RTOS v1 → v2 for pass-by-value queue semantics
 ADC via DMA with a double buffer; UART TX via DMA with a completion semaphore
 ESP8266 / ESP-01 uplink via UART AT commands, then MQTT to Mosquitto or HiveMQ
 Grafana + InfluxDB dashboard for historical data
Author

Ash (莊為竹) — M.S. Computer Science, National Chi Nan University, Taiwan. Self-taught firmware: STM32, FreeRTOS, UEFI/EDK II, OpenBMC.
## Author

**ASH** — M.S. Computer Science, Jinan University
Self-taught firmware (STM32, RTOS, UEFI/EDK2) and embedded AI (Jetson + OpenPose).
