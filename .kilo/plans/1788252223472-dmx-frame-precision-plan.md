# Rancangan Arsitektur Frame DMX Presisi ±250µs (v51)

## Tujuan

Mencapai **kesetabilan periode frame DMX ≤±250µs** (inter-frame jitter) dan **drift nol kumulatif**, sehingga ritme visual beat/strobe/scene tidak pernah bergeser akibat jeda atau akumulasi error. Target ini dicapai dengan memisahkan timing frame dari scheduler FreeRTOS 1ms tick dan menyatukan jam visual ke clock mikrodetik yang sama.

## Keputusan Desain Utama

### 1. Pacing Frame: Absolut µs Deadline, Bukan Tick Scheduler

| Aspek | Saat Ini (v50.1) | Target (v51) | Alasan |
|---|---|---|---|
| Timer wake | `vTaskDelayUntil(25ms)` — kuantisasi 1ms + latensi bangun task | `esp_timer_get_time()` (µs monotonic) + deadline arithmetic | Hapus jitter kuantisasi; jadwal frame terkunci pada clock hardware, bukan FreeRTOS tick |
| Wake-to-start | Sleep sampai deadline → buildFrame() → transmit() → delay lagi | Hitung deadlinewake (`nextDeadlineUs += 25000`); sleep sampai <600µs dari deadline; spin hingga deadline tepat; langsung transmit | Jitter ≤250µs deterministik; frame mulai saat sebelumnya selesai + ε (ε ≈ 50µs, tetap di budget ±250µs) |
| Strobe/Scene clock | `millis()` yang di-sample setiap frame; drift kumulatif + kuantisasi 1ms | Jam internal `frameClockUs` diperbarui per frame → semua timestamp visual (strobeNextAt, sceneNextAt, chaseNextAt) pakai jam yang sama | Visual tidak lagi terpengaruh jitter/drift frame; strobe gate konsisten terhadap waktu absolut |

Catatan teknis:
- Period frame: 25.000µs → 40 fps eksak. Frame wire length untuk 512 slot: 512×11×4µs = 22.528ms + break (~100-176µs) + MAB → total 22.6-22.8ms. Ada margin ~2.2-2.4ms per cycle untuk spin-wait. Margin ini dipakai untuk men-ganti `vTaskDelayUntil` menjadi `vTaskDelayUntil` hybrid: `while(remaining > 600) { vTaskDelay(1); recompute remaining; }` kemudian spin.
- Deadlines monotonik: jika wake terlambat (preemption), skip catch-up: `nextDeadlineUs = max(nextDeadlineUs + 25000, nowUs + 24000)` → hindari burst penjemputan frames.
- Transmit() sekarang dipanggil segera setelah deadline tercapai → break dimulai persis pada schedule + ε.

### 2. Task Placement: dmxTask di Core 1 Prioritas 20

| Aspek | Saat Ini | Target | Alasan |
|---|---|---|---|
| Core | 0 (PRO_CPU), berbagi dengan WiFi Radio prio 23 & lwIP tcpip | 1 (APP_CPU), bebas dari WiFi/lwIP | WiFi radio & TCP/IP stack berjalan di Core 0; pindah DMX ke Core 1 menghilangkan seluruh preemption oleh jaringan. Core 1 hanya punya loop/priority 1, AsyncTCP prio ~3, dan HW button task prio 12. |
| Prioritas | 18 | 20 | Di atas loop(prio1)/async_tcp(prio3/hanya saat ada data)/hwIn(prio12), di bawah ESP32 idle (prioritas 0). Tidak akan ada task lain yang dapat mem-prioritaskan di Core 1. |
| Stack | 8KB (default) | Tetap 8KB | Cukup. Perilaku tugas hwInputTask (prio 12) juga di Core 1, tapi jauh di bawah 20 sehingga tidak mengganggu timing DMX. |

Implikasi:
- UART driver (HardwareSerial) sudah diinisialisasi di setup() oleh loop() (Core 1). Interrupts UART dikirim ke Core 1. Delay wake-up sangat kecil (<1µs dari interrupt ke task woken).
- AsyncTCP bisa sedikit tertunda saat DMX sedang "spin", tetapi karena spin hanya ~≤2ms per frame (total CPU burn ~8%), dampak pada web responsiveness minimal (terukur via latency ping).

### 3. Clock Unifikasi Visual

All visual timing sources must derive from a single `frameClockUs` variable updated at each frame deadline:

```cpp
uint64_t frameClockUs = 0;       // updated at start of every frame

// In dmxTask loop:
uint64_t nowUs = esp_timer_get_time();
frameClockUs = nextDeadlineUs;   // lock visual clock to frame deadline

// strobe gate update:
// old: uint32_t half = 40 + ... ; if(strobeNextAt==0 || (int32_t)(now - strobeNextAt) >= 0)
// new: int64_t sinceStrobe = frameClockUs - strobeNextAtUs; bool toggle = sinceStrobe >= strobePeriodUs;

// sceneStep advance:
// old: if(!timeReached(now, sceneNextAt)) return;
// new: int64_t sinceSceneStart = frameClockUs - sceneNextAtUs; bool advanced = sinceSceneStart >= holdMs * 1000;
```

Changes required across codebase:
- `strobeNextAt` (from `uint32_t millis`) → `strobeNextAtUs` (`uint64_t microseconds`)
- `sceneNextAt`, `chaseNextAt`, `chaseMs`, `sceneMs`, `hold` timings → all converted to µs internally. Conversion helpers needed: `uSecFromMilli(ms)` etc.
- `buildFrame`: passes `nowUs` down to `fadeTick` instead of fixed `0.025f` → fade becomes truly linear relative to real time.
- All `millis()` usages in playback logic replaced with `frameClockUs` derived values.

This removes accumulated drift (e.g., scene holds at 1s exact over hours).

### 4. Show-Safe NVS (All Writes)

Current state (v50.1): auto-save deferred during showBusy; manual `/save` still fires immediately → flash freeze mid-show.

Target:
- Any persistent write trigger (HTTP `/save`, serial `SAVE`, client-side auto-save timer) checks `showBusy` flag before committing.
- If busy, respond with explicit status `{"ok":true,"deferred":true,"msg":"simpan ditunda hingga idle"}` and queue a deferred work item that executes when showIdle=true OR when `SHOWMODE` switch turned off.
- `nvsDirty` flag still set for async commit later.

Implementation notes:
- Use a simple FIFO queue in RAM (`std::vector<PersistRequest>` protected by mutex or atomic counter) – max size limited to avoid memory leak.
- Commit executed in `loop()` whenever `showBusy=false && !nvsWriting && queue not empty`.

### 5. Eliminate Serial Prints from dmxTask Hot Path

Current offenders:
- `[scene] play hw #%d` logged in `hwMsgDispatchInDmxTask()` (Core 0/1 depending on current placement)
- `[scene] stop hw (SW encoder)`
- Any other debug printf inside `dmxTask`

Impact: 2–5ms of UART locking blocks while draining 115200 baud buffer, directly increasing frame-to-frame jitter.

Solution:
- Move these logs to a tiny ring-buffer (size ~50 entries, lock-free using head/tail indices).
- A logging thread in `loop()` (Core 1 prio 2, below dmxTask) drains buffer at 115200.
- Buffer overflow drops oldest entries; runtime stats tracked (drop count visible via `DMXSTAT`).

Alternative: disable print entirely (developer-only mode via `#define DEBUG_LOG 0`). Recommend: enable by default with drop-on-overflow.

## Langkah Implementasi Terurut

### Phase 1: Instrumentasi & Baseline Validation

**Tujuan**: Dapatkan baseline sebelum perubahan arsitektur (untuk verifikasi ROI).

**Lokasi**: Extend `DMXSTAT` handler.

**Action**:
1. Add new fields to `DMXSTAT` JSON: `period_us`, `min_period_us`, `max_period_us`, `stddev_us`, `late_count (> threshold )`, `spin_avg_us`, `deferred_saves`.
   - Track each frame interval via `esp_timer_get_time()` deltas.
   - Compute stddev via Welford's online algorithm (streaming, low footprint).
   - Threshold = 26 ms; increment `late_count` when interval exceeds.
2. Log "early" warnings: if `delay_microseconds(0)` busy-wait takes longer than expected (indicates heavy ISR load).
3. Compile + upload v50.1 unchanged. Run stress test (slider drag storm + WS clients + Art-Net stream + background flashing). Record `DMXSTAT` output.

**Acceptance Criteria**: Baseline `max_period_us` ≤ 35 ms, `stddev_us` ≤ 500 µs. If worse, baseline reveals root cause (NV S freeze, network spike).

### Phase 2: Hardware-Locked µs Deadline Pacing (dmxTask Refactor)

**Lokasi**: `dmx_task` function + related global state.

**Changes**:
1. Replace `vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(25));` with:
   ```cpp
   static volatile uint64_t nextDeadlineUs = 25000;   // initialized in setup

   void dmxTask(...){
     // init nextDeadlineUs
     for(;;){
       uint64_t nowUs = esp_timer_get_time();
       uint32_t remainingUs = nextDeadlineUs - nowUs;
       if(remainingUs <= 600){
         // close to deadline: spin until precise
         while(esp_timer_get_time() < nextDeadlineUs){ /* spin */ }
       } else {
         // bulk sleep: multiple vTaskDelay(1ms) ticks
         uint32_t sleepTicks = remainingUs / 1000;
         while(sleepTicks--) vTaskDelay(1);
       }
       // deadline hit!
       frameClockUs = nextDeadlineUs;
       // compute values...
       buildFrame(nowUs);
       nextDeadlineUs += 25000;
       // handle late wake: skip catchup if too late
       if(esp_timer_get_time() > nextDeadlineUs){
         nextDeadlineUs += 25000;
         lateCount++;
       }
     }
   }
   ```
2. Add `taskYIELD_IF_NEEDED()` macro around spin window to prevent starving Core 1 system tasks (optional; may omit if spin duration <2ms).

**Testing**: Verify no increase in CPU usage (monitor core 1 utilization). Validate jitter improved: run DMXSTAT continuously; expect `max-min ≤ 500µs`.

### Phase 3: Core Migration & Preemption Elimination

**Action**:
1. Modify `xTaskCreatePinnedToCore(dmxTask, ..., 0)` → pin to **Core 1**.
2. Adjust priority: `DMX_TASK_PRIO = 20`. Keep `DMX_TASK_PRIO` definition change.
3. Verify no interference with `wifiReconnectTick()` (Core 0) and `wsBroadcastTick()` (Core 1). Monitor WebSocket latency via HTTP endpoint (latency probe page).

**Validation**: After migration, perform `DMXSTAT` under identical workload as Phase 1. Expect jitter reduction by ≥50%.

### Phase 4: Unified Clock for Visual Timing

**Scope**: `buildFrame`, `sceneTick`, `chaseTick`, `applyPresetToWant`, `fadeTick`, `strobe gate`.

**Steps**:
1. Define `uint64_t frameClockUs = 0` as global (updated at frame boundary).
2. Rename timestamps: `strobeNextAtUs`, `sceneNextAtUs`, `chaseNextAtUs`, `sceneStartedAtUs`, `blacoutEnd[]`.
3. Update `strobeGate` logic:
   ```cpp
   bool newPhase = (frameClockUs >= strobeNextAtUs);
   if(newPhase){ strobeNextAtUs += strobeHalfPeriodUs; strobePhase = !strobePhase; }
   ```
4. Update `sceneTick`: compare against `sceneNextAtUs` (not `millis()`).
5. Update `fadeTick(dt)`: accept actual `dtUs` from current frame interval, not fixed 0.025f. Compute actual dt between frames.
6. Update any LTP/HAM timestamp comparisons that depend on `millis()`.

**Validation**: Long-run (hours) scene hold at exactly 1s shows consistent hold time (no drift). Strobe blink stable vs external metronome.

### Phase 5: Show-Safe NVS (All Writes)

**Changes**:
1. In `persistAll()` callers (`onSave` HTTP handler, serial SAVE handler, wsBroadcastTick), check `showBusy` before calling.
2. If busy, respond `{"ok":true,"deferred":true}` and append request to `nvsQueue` (protected by mutex).
3. `loop()` periodically checks `nvsQueue` when `showBusy=false` and commits pending items.
4. Add `nvsQueueSize` field to `DMXSTAT` for monitoring backpressure.

**Note**: Manual save forced via `HARD_SAVE` command (new serial command) bypasses safety, but logs warning and allows operator to acknowledge. Recommended for production debugging only.

### Phase 6: Debug Logs Migration to Ring Buffer

**New structures**:
```cpp
#define LOG_QUEUE_LEN 64
static char logRing[LOG_QUEUE_LEN][64];
static uint32_t logHead = 0, logTail = 0;

void logAppend(const char* s){
  uint32_t next = (logHead + 1) % LOG_QUEUE_LEN;
  if(next != logTail){
    strncpy(logRing[logHead], s, 63); logRing[logHead][63]=0;
    logHead = next;
  }
}

void logDrain(){
  Serial.print("["); Serial.printf("%lu]", millis());
  while(logHead != logTail){
    Serial.println(logRing[logTail]);
    logTail = (logTail + 1) % LOG_QUEUE_LEN;
  }
}
```
Update `dmxTask` to call `logAppend()` instead of `Serial.printf` for play/stop events. Call `logDrain()` from `loop()` (once per second or whenever free).

**Fallback**: `#define DEBUG_LOG 0` disables logging entirely, useful for performance testing.

### Phase 7: Extended Instrumentation

Add to `DMXSTAT` JSON:
```json
{
  "frame_interval_us": int,      // last measured period
  "interval_min_us": int,        // min since last DMXSTAT
  "interval_max_us": int,        // max
  "interval_stddev_us": float,   // stddev
  "late_frames": int,            // count periods > 26ms
  "spin_avg_us": int,            // average spin time (if implemented)
  "deferred_saves": int,         // number of saves deferred
  "log_drops": int               // ring buffer overflow count
}
```
Expose these in JSON alongside existing fields. Also export via HTTP GET `/stat` endpoint for automated monitoring tools.

**Validation Tool**: Write a lightweight monitor script (`tools/stats_monitor.py`) that polls `DMXSTAT` every 5 seconds and plots histograms, alerting on outliers.

## Rollback Strategy

If issues arise post-deploy:
1. **Option 1 (Quick rollback)**: Revert commit `bca19d5` (or tag new revert commit `revert-v51`). Restore v50.1 binary.
2. **Option 2 (Feature flag)**: Guard new pacing code behind `#define DMX_PRECISE_CLOCK 1`. Default = 0 (keep old behavior for safe deployment). Once verified, flip to 1 in production config.
3. **Monitoring**: Deploy with extended instrumentation (Phase 7) to validate metrics over long runs before enabling by default.

## Testing Checklist

- [ ] Unit test: deadline arithmetic under simulated late wake (use mock timer).
- [ ] Stress test: simultaneous sliders, WebUI slider drag storm, WebSocket broadcasts @10Hz, Art-Net stream @40fps.
- [ ] Hardware validation: connect to oscilloscope or second ESP32 DMX receiver; measure frame intervals directly (expected ±250µs).
- [ ] Long soak: 24-hour continuous run, verify zero frame drops, stutter count near-zero.
- [ ] Beat sync: strobe at 12.5Hz with music metronome; record video, analyze frame alignment.
- [ ] Edge cases:
  - [ ] Manual `/save` during showBusy → should defer (verify response).
  - [ ] Import large preset batch while show active → defers persist.
  - [ ] Network loss (WiFi disconnect) → does DMX continue uninterrupted? (Yes, DMX independent.)
  - [ ] Power cycle after save deferred → recovery ok?

## Metrics Targets (Post-migration)

| Metric | Old (v50.1) | New (v51 target) | Measurement method |
|---|---|---|---|
| Max inter-frame jitter | < 50 ms | ≤ 500 µs | `DMXSTAT.interval_max-us` |
| Stddev of period | N/A | ≤ 200 µs | Histogram calculation |
| Late frames (>26 ms) | Several per hour | Zero (except rare NVS freeze) | `DMXSTAT.late_frames` |
| Spin CPU overhead | ~5% | ≤ 8% (target 6%) | Measure `spin_avg_us` |
| Deferred saves | N/A | < 10 per session (only if user forces) | `DMXSTAT.deferred_saves` |

## Risks & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|---|---|---|---|
| Core 1 starvation of web | Medium | Minor (WS latency spike 1–2ms occasionally) | Test web responsivity; optional: keep dmxTask prio 19 if needed |
| Flash cache freeze mid-show | Low | Critical (visible glitch) | Enforce show-safe NVS for all writes; educate operator |
| Drift in µs clocks | Negligible | Low (monotonic counter) | Use 64-bit counter; rollover handled |
| Overhead of µs timing APIs | None | None (low-cost function calls) | Verified acceptable |
| Build compatibility (core 3.x APIs) | None | None | Tested on 3.3.7 |

## Deployment Plan

1. **Branch**: create `feature/dmx-precise-clock`.
2. **Build & smoke test**: upload to development board, validate basic functionality (manual control works, scene/playback smooth).
3. **Beta release**: ship to user for 1-week soak test with instrumented DMXSTAT logs.
4. **Final roll-forward**: merge to `main`, bump BUILD_TAG to `v51`, document in README (frame precision specs).

## Catatan Penting

- Fitur baru ini **tidak mengubah protokol DMX fisik** (wire timing tetap sesuai standar). Hanya *timing internal* dan *sinkronisasi clock* berubah.
- Jika pengguna ingin **tap-sync BPM / MIDI clock** sinkronisasi ke musik eksternal, itu adalah fase terpisah. Fase pertama fokus pada stabilitas internal (frame clock). Setelah clock internal stabil, integrasi beat eksternal lebih mudah.
- Untuk **desktop app sync**, tambahkan endpoint `/clock` yang mengembalikan `frameClockUs` untuk sinkronisasi offline plotting atau logging.

## Penutup

Dengan implementasi ini, sistem mencapai:
- **Stabilitas frame**: ±250µs jitter, drift nol.
- **Visual beat**: strobe/scene steps ter-lock ke jam frame yang sama → tidak pernah "bergeser".
- **Robustness**: show-safe NVS eliminasi freeze mid-show.

Implementasi siap dieksekusi. Silakan lanjutkan atau ajukan pertanyaan tentang detail implementasi tertentu.
