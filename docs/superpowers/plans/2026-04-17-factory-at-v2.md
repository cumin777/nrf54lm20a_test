# Factory AT V2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rework `factory` into a dual-layer AT architecture with secure storage-partition persistence and startup/AT separation.

**Architecture:** Split boot/integration and AT handling into separate files, introduce dedicated storage module bound to `storage` partition, and expose per-item + per-state AT handlers with structured output and explicit errors.

**Tech Stack:** C (Zephyr), PlatformIO, UART/GPIO/flash-map/regulator APIs.

### Task 1: Add TDD guard test and observe RED

**Files:**
- Create: `factory/tests/verify_factory_v2_layout.sh`

- [ ] **Step 1: Write the failing test**

```bash
#!/usr/bin/env bash
set -euo pipefail
test -f factory/src/at_handler.c
test -f factory/src/factory_storage.c
! rg -n "FACTORY_FLASH_OFFSET" factory/src >/dev/null
rg -n "AT\+STATE1|AT\+STATE8A|AT\+STATE8B|AT\+STATE9B" factory/src/at_handler.c >/dev/null
```

- [ ] **Step 2: Run test to verify it fails**

Run: `bash factory/tests/verify_factory_v2_layout.sh`
Expected: FAIL because new files/commands do not exist yet.

- [ ] **Step 3: Commit test scaffold**

```bash
git add factory/tests/verify_factory_v2_layout.sh
git commit -m "test(factory): add failing V2 architecture guard"
```

### Task 2: Implement storage partition module

**Files:**
- Create: `factory/src/factory_storage.h`
- Create: `factory/src/factory_storage.c`

- [ ] **Step 1: Keep guard test in RED**

Run: `bash factory/tests/verify_factory_v2_layout.sh`
Expected: still FAIL before module creation.

- [ ] **Step 2: Implement storage API bound to `FLASH_AREA_ID(storage)`**

```c
int factory_storage_load(struct factory_persist *out);
int factory_storage_save(const struct factory_persist *in);
void factory_storage_defaults(struct factory_persist *data);
```

- [ ] **Step 3: Run guard test**

Run: `bash factory/tests/verify_factory_v2_layout.sh`
Expected: may still FAIL until AT module is added.

### Task 3: Implement AT handler module (item + state)

**Files:**
- Create: `factory/src/at_handler.h`
- Create: `factory/src/at_handler.c`

- [ ] **Step 1: Add dispatcher API and structured output helpers**

```c
void at_handler_init(const struct device *uart_dev, const struct device *regulator_parent, struct factory_persist *persist);
void at_handler_process_line(const char *line, size_t len);
```

- [ ] **Step 2: Add single-item handlers (`at_handle_<item>`) and state handlers (`at_handle_state_<id>`)**

Implement command mapping from V2 baseline; unimplemented items return `ERROR:NOT_IMPLEMENTED` with `+TESTDATA` context.

- [ ] **Step 3: Implement `AT+FLASH` and `AT+FLASH?` through storage module**

Ensure boot flag semantics remain `0/1/2`.

- [ ] **Step 4: Run guard test to verify GREEN**

Run: `bash factory/tests/verify_factory_v2_layout.sh`
Expected: PASS.

### Task 4: Split main integration logic

**Files:**
- Modify: `factory/src/main.c`
- Modify: `factory/zephyr/CMakeLists.txt`

- [ ] **Step 1: Keep startup/main-loop only in `main.c`**

Main should initialize devices/storage, read boot flag, dispatch to factory loop if flag==2, otherwise run UART receive loop and feed AT handler.

- [ ] **Step 2: Update `CMakeLists.txt` with new sources**

Add `../src/at_handler.c` and `../src/factory_storage.c`.

- [ ] **Step 3: Run guard test again**

Run: `bash factory/tests/verify_factory_v2_layout.sh`
Expected: PASS.

### Task 5: Verification and records

**Files:**
- Modify: `../AGENTS.md`

- [ ] **Step 1: Build verification**

Run: `PLATFORMIO_CORE_DIR=/tmp/pio-core pio run -d factory`
Expected: build success, or explicit blocker with error details.

- [ ] **Step 2: Update AGENTS completion record**

Add latest record at top with files, verification summary, git result, and next step.

- [ ] **Step 3: Commit and push**

```bash
git add AGENTS.md factory docs/superpowers
git commit -m "feat(factory): implement V2 AT skeleton with storage partition and module split"
git push -u origin feature/factory-at-v2
```
