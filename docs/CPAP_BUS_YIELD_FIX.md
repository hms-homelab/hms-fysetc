# SD Bus Yield Fix — Design Document

## Problem

Once the FSM enters `UPLOADING` state, GPIO26 (SD_SWITCH_PIN) is held
LOW for the entire duration of the upload task. The MUX gives the ESP32
exclusive access to the SD card bus. If the CPAP machine tries to access
the SD card during this window — periodic EDF record flushes, RTC
updates, or writes during the ~60min post-therapy consolidation period —
it asserts CS but gets no response from the SD card. The CPAP throws an
SD card error.

`TrafficMonitor` continues sampling GPIO33 via PCNT during the upload
but the result is never checked by the upload task. There is no mechanism
to interrupt or yield the upload once it starts.

## Hardware Topology

```
CPAP ──DAT3/CS──► [GPIO33 sense tap] ──► MUX ──► SD Card
                                          ▲
                                       GPIO26
                                          ▲
                              ESP32 ──────┘
```

GPIO33 (CS_SENSE) is connected to the CPAP side of the MUX, upstream of
the switching point. This means:

- When GPIO26 is HIGH (CPAP owns bus): GPIO33 reflects normal CPAP SD
  activity — this is what TrafficMonitor uses in LISTENING state.
- When GPIO26 is LOW (ESP owns bus): the MUX blocks CPAP signals from
  reaching the SD card, BUT GPIO33 still sees the CPAP asserting CS.

**A FALLING edge on GPIO33 while `sdManager.hasControl() == true` means
the CPAP is requesting bus access and being silently blocked.**

This is directly observable with a GPIO interrupt — no polling required.

## Solution

Use a `FALLING` edge interrupt on GPIO33, active only while the ESP owns
the bus, to set a yield flag. The upload task checks the flag at chunk
boundaries and returns a `YIELD_NEEDED` result. The FSM then releases the
bus, waits for the CPAP to finish, and resumes from the last checkpoint.

## Implementation

### 1. `main.cpp`

Add a global yield flag and ISR:

```cpp
// Near other upload task globals
volatile bool g_cpapYieldRequest = false;

void IRAM_ATTR cpapYieldISR() {
    // CPAP asserting CS while ESP owns bus — request yield
    g_cpapYieldRequest = true;
}
```

In `handleAcquiring()`, after `sdManager.takeControl()` succeeds:

```cpp
g_cpapYieldRequest = false;
attachInterrupt(digitalPinToInterrupt(CS_SENSE), cpapYieldISR, FALLING);
LOG("[FSM] Bus yield interrupt armed on GPIO33");
```

In `handleReleasing()`, before `sdManager.releaseControl()`:

```cpp
detachInterrupt(digitalPinToInterrupt(CS_SENSE));
g_cpapYieldRequest = false;
```

Add `YIELD_NEEDED` handling in `handleUploading()` task result switch:

```cpp
case UploadResult::YIELD_NEEDED:
    LOG("[FSM] CPAP requested bus — yielding");
    transitionTo(UploadState::RELEASING);
    // Set a flag so RELEASING re-enters LISTENING instead of COOLDOWN
    g_yieldResume = true;
    break;
```

In `handleReleasing()`, after releasing, if `g_yieldResume`:

```cpp
if (g_yieldResume) {
    g_yieldResume = false;
    transitionTo(UploadState::LISTENING);  // re-enter, wait for idle, re-acquire
}
```

### 2. `include/UploadFSM.h`

Add the new result value:

```cpp
enum class UploadResult {
    COMPLETE,
    TIMEOUT,
    ERROR,
    NOTHING_TO_DO,
    YIELD_NEEDED    // ← new: CPAP requested bus mid-upload
};
```

### 3. `src/SMBUploader.cpp`

In the chunk loop (~line 565), check the flag at the top of each
iteration before reading from SD:

```cpp
while (localFile.available()) {
    // Yield to CPAP if it requested bus access
    extern volatile bool g_cpapYieldRequest;
    if (g_cpapYieldRequest) {
        LOG_WARN("[SMB] CPAP requested bus — suspending upload after current file");
        localFile.close();
        return false;  // caller propagates as YIELD_NEEDED
    }

    size_t bytesRead = localFile.read(uploadBuffer, uploadBufferSize);
    // ... rest of loop unchanged
}
```

The upload function returns `false`, which propagates up through
`uploadSingleFileSmb()` → `uploadDatalogFolderSmb()` →
`uploadWithExclusiveAccess()` → signals `YIELD_NEEDED` to the FSM.

### 4. `src/FileUploader.cpp`

Check the flag between files in the folder iteration loop so the yield
also works for the Cloud (SleepHQ) path:

```cpp
for (const String& filePath : files) {
    extern volatile bool g_cpapYieldRequest;
    if (g_cpapYieldRequest) {
        LOG_WARN("[FileUploader] CPAP requested bus — pausing folder upload");
        return UploadResult::YIELD_NEEDED;
    }
    uploadSingleFileCloud(sdManager, filePath, false);
}
```

## Resume Behaviour

`UploadStateManager` tracks completed files by checksum. When the FSM
re-enters `LISTENING` after a yield, waits for idle, re-acquires the bus,
and restarts the upload task, already-completed files are skipped via
`hasFileChanged()`. The upload resumes from the first incomplete file.

For large files (e.g. `_BRP.edf`, several MB) interrupted mid-transfer:
the partial remote write is discarded and the file is re-uploaded in
full on resume. This is acceptable for v1 — byte-level resume within a
single file can be added later if needed.

## Compatibility Notes

- **PCNT coexistence**: The ESP32 PCNT peripheral uses an independent
  signal path from the GPIO interrupt matrix. `TrafficMonitor` continues
  operating normally on the same GPIO33 pin while the yield interrupt is
  attached.
- **Interrupt scope**: The interrupt is only attached after
  `takeControl()` and detached before `releaseControl()`. It is never
  active during LISTENING or COOLDOWN states, so it does not interfere
  with normal bus monitoring.
- **ISR safety**: `cpapYieldISR` only sets a `volatile bool`. No heap
  allocation, no logging, safe for `IRAM_ATTR`.

## Why This Works

SD host command timeouts on the AirSense 10/11 are in the 100–500ms
range. The ISR fires on the GPIO33 falling edge and GPIO26 goes HIGH
within one upload chunk cycle (8KB at ~4MB/s SD read = ~2ms). The CPAP
receives its SD response well within timeout. From the machine's
perspective it was a brief arbitration delay, not an error.

## Future Improvements

- **Byte-level resume within files**: Store `totalBytesRead` in
  UploadStateManager before yield, resume from that offset using
  `file.seek()` on re-entry. This avoids re-uploading partial progress
  on large BRP files.
- **Yield latency measurement**: Add telemetry to measure GPIO33 falling
  edge → GPIO26 release time to confirm it stays well under CPAP timeout.
- **Adaptive chunk size**: If yields are frequent, reduce chunk size to
  minimize re-upload overhead on resume.
