# Factory AT V2 Design (xiao-nrf54lm20a)

**Date:** 2026-04-17
**Scope:** `/factory` app AT firmware for phase/state factory flow

## 1. Goal
Implement an AT-slave factory firmware on `UART21` with dual-layer commands (item AT + state AT), secure persistent flag/data in `storage` partition, and split startup/integration from AT command handling.

## 2. Constraints and Baseline
- Keep `UART21` as the only command/data channel.
- Firmware reports structured test data only; no PASS/FAIL decision.
- Startup must read factory boot flag before entering main AT loop.
- Boot flag `2` keeps existing factory loop behavior.
- `AT+FLASH` remains the command to set boot flag semantics.
- Existing `main.c` is oversized and mixes startup + parsing + handlers.

## 3. Architecture
### 3.1 Files and Responsibility
- `factory/src/main.c`
  - Startup/integration only: device readiness, flash load, boot-flag dispatch, AT loop.
- `factory/src/at_handler.c`
  - AT parse/dispatch; item handlers; state aggregators; structured UART outputs.
- `factory/src/at_handler.h`
  - Cross-file API, shared data models, error enums.
- `factory/src/factory_storage.c`
  - Safe flash-area access using `FLASH_AREA_ID(storage)` for read/write/erase.
- `factory/src/factory_storage.h`
  - Persistent schema and storage API.

### 3.2 Persistent Schema
Use `storage` partition with magic/versioned structure:
- `magic`
- `version`
- `boot_flag`
- `state_bitmap`
- `item_bitmap`
- `reserved[]`

Compatibility target:
- Keep boot flag meaning unchanged (`0/1/2`, where `2` enters factory loop).
- `AT+FLASH=<v>` updates persisted flag atomically through erase+write in partition.

### 3.3 AT Command Model
- Item commands: one handler per measurement item, one AT command per item.
- State commands: one handler per state, sequentially call each item handler and report each item.
- Unified output:
  - `+TESTDATA:<STATE>,ITEM=<name>,VALUE=<v>,UNIT=<u>,RAW=<raw>,META=<k:v>`
  - tail line `OK` or `ERROR:<reason>`

### 3.4 Error Model
Use explicit reasons:
- `NOT_IMPLEMENTED`
- `PRECONDITION_NOT_MET`
- `HW_NOT_READY`
- `INVALID_PARAM`
- `STORAGE_IO_FAIL`
- `UNKNOWN_COMMAND`

## 4. Command Mapping (V2 Baseline)
- `AT+VBUS`, `AT+V3P3`, `AT+UARTLOOP` -> `AT+STATE1`
- `AT+CHGCUR`, `AT+BATV` -> `AT+STATE2`
- `AT+BLESCAN`, `AT+GPIOLOOP`, `AT+NFCLOOP` -> `AT+STATE3`
- `AT+IMU6D`, `AT+MICAMP` -> `AT+STATE4`
- `AT+SLEEPI` -> `AT+STATE5`
- `AT+KEYWAKE` -> `AT+STATE6`
- `AT+FLASHWRITE` -> `AT+STATE7`
- `AT+SHIPMODEA` -> `AT+STATE8A`
- `AT+VBUS_B`, `AT+V3P3_B` -> `AT+STATE8B`
- `AT+SHIPMODEB` -> `AT+STATE9B`
- Control commands:
  - `AT`
  - `AT+HELP`
  - `AT+FLASH=<0|1|2>`
  - `AT+FLASH?`

Unimplemented measurements keep interface and return `ERROR:NOT_IMPLEMENTED` with structured `+TESTDATA` context.

## 5. Data Flow
1. Boot: init UART/GPIO/storage.
2. Load persistent data from `storage` partition.
3. If boot flag is `2`, enter preserved factory loop.
4. Else, enter AT receive loop and dispatch commands.
5. Item command reports one item; State command reports all items in that state.

## 6. Verification Strategy
- Static/TDD guard script validates architecture and command coverage.
- Build verification through PlatformIO (or report environment blocker explicitly).
- Manual UART sequence for smoke test:
  - `AT`
  - `AT+HELP`
  - `AT+FLASH?`
  - `AT+FLASH=2`
  - `AT+STATE1`
  - `AT+STATE8A`

## 7. Risks
- ADC/GPIO/BLE/NFC/IMU dependencies are board-specific; V2 first step keeps stable interfaces and clear `NOT_IMPLEMENTED` return for blocked items.
- Build environment may need writable PlatformIO cache path.
