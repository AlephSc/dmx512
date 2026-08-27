# Session Logs - DMX512 Controller ESP32 Project

## Session 50 - 2026-08-27 - Bug fixes + Patch panel (fixture config)

### Scope
User reported 3 bugs and 2 feature requests. All addressed in this session.

### Bugs fixed

**Bug 1: Preset color not syncing between Desktop and WebUI**
- Root cause: desktop polled LISTP every 3.0 s (DATA_INTERVAL); also `presetsJson()`
  hardcoded r/g/b from `row[2..4]` (PAR1 fixed position).
- Fix: reduced `DATA_INTERVAL` from 3.0 to 1.5 s in `desktop/worker.py`.
- Fix: `presetsJson()` now computes r/g/b from the first PAR fixture's actual
  `start` address (dynamic), so preview stays correct after patch changes.

**Bug 2: Preset hold/data not visibly preserved when preset deleted (scene risk)**
- Firmware already preserves channel/fade/hold data when `used=0` (PDEL only
  clears the flag). The gap was UI: hidden presets still referenced by scenes
  looked identical to truly empty slots.
- Fix: added `PadButton.set_hidden_in_scene()` in `desktop/ui/widgets.py`;
  `presets_tab.py` now computes scene-referenced slots and shows them with a
  dashed orange border.
- Fix: WebUI `renderBank()` adds `.hidden-scene` class to hidden presets that
  are still referenced by any scene step; new CSS `.pad.hidden-scene` added.

**Bug 3: Scene play button does not change to Stop in Desktop**
- Root cause: `ScenesTab.b_cek` text was static ("▶ Cek").
- Fix: `apply_state()` now sets `b_cek` text to "■ Stop" when the selected
  scene is playing (`_scene_on and _playing_scene == local_sel`), matching
  the WebUI `btnSPlay` behaviour.

### Features added

**Feature 1: Desktop fixtures displayed in rows by type**
- `desktop/ui/mixer_tab.py`: `build_fixtures()` now groups fixtures by type
  (PAR / Moving Head / Beam / Strobe / Fog) and renders each type as a
  labelled horizontal row inside a vertical scroll area. Fixture name label
  now also shows the DMX address range (e.g. "PAR 1\n1-9").

**Feature 2: Runtime fixture count + DMX address configuration (WebUI + Desktop)**
- Firmware (`dmx_web_rgb/dmx_web_rgb.ino`):
  - `Fixture.name` changed from `const char*` to `char[25]` (mutable).
  - `N_FIX` changed from compile-time `#define` to runtime `uint8_t`;
    `MAX_FIX=32` used for static array sizes (`fix[]`, `blackoutEnd[]`).
  - `loadDefaultFixtures()` restores the 18-factory default patch.
  - New NVS key `fixcfg` stores fixture config (binary, 2 + N*31 bytes,
    version byte `FIXCFG_VER=1`). Loaded at boot; falls back to default.
  - `validateFixtures()`: rejects count 0 or >32, address <1, foot <1,
    `start+foot-1 > 512`, and overlapping ranges.
  - `applyFixtures()`: validates, applies under DMX mutex, persists to NVS.
  - New HTTP endpoint `POST /fixes` (`onFixesPost`): parses JSON body,
    validates, applies. Returns 409 with error detail on validation failure.
  - New serial command `FIXSET <json>`: same semantics as POST /fixes.
  - Serial line buffer raised from 384 to 2048 chars to fit fixture JSON.
  - `fixJson()` now includes `hasMove` field.
  - `BUILD_TAG` raised to `v45`.
- WebUI:
  - New `#patchPanel` section with editable table (name, type, start address,
    channel count, end address, pan/tilt flag, delete button).
  - Client-side validation mirrors server rules; errors shown inline.
  - Buttons: Simpan Patch (POST /fixes), + Tambah Fixture, Reset Default.
  - On successful save, page reloads to pick up new FIX data from server.
- Desktop:
  - New `desktop/ui/patch_tab.py` (`PatchTab`): editable QTableWidget with
    same fields and validation as WebUI.
  - Serial transport: sends `FIXSET <json>`; HTTP transport: emits
    `http_fixtures` signal handled by `MainWindow._patch_apply_http()`
    (urllib POST /fixes).
  - `main.py`: Patch tab added to tab bar; LISTF response now also populates
    `PatchTab.build_from_listf()`; FIXSET triggers LISTF refresh after ACK.

### Files changed
- `dmx_web_rgb/dmx_web_rgb.ino` (firmware v45)
- `desktop/worker.py`
- `desktop/main.py`
- `desktop/ui/mixer_tab.py`
- `desktop/ui/presets_tab.py`
- `desktop/ui/scenes_tab.py`
- `desktop/ui/widgets.py`
- `desktop/ui/patch_tab.py` (new)

### Intentionally not changed
- Authentication/CSRF still deferred per user request.
- Existing GET mutation endpoints retained for compatibility.
- No C++ build performed, per project rule (user compiles on target).

### Validation
- All edited Python files pass `python -m py_compile`.
- `git diff --check` passed.
- Static source review completed.
- Runtime and board-specific compile test remain required by user on target
  ESP32/core/library versions.

### Remaining risks
- NVS writes are still multi-key and not journaled/transactional.
- Fixture config NVS write is a single key (lower risk than preset keys).
- Large `String` JSON allocations remain in some handlers.
- State revision is not fully atomic across cores.

### Review follow-up
- Added fixture `type`, `hasMove`, and non-empty-name validation.
- `loadFixtures()` now commits under `dmxMutex`.
- `applyFixtures()` snapshots the old patch, rolls back RAM on NVS failure,
  and marks state changed only after persistence succeeds.
- Moved `recomputeWant()` outside the fixture loop in `applyPresetToWant()`.
- Serial `FIXSET` now preserves case-sensitive JSON field names.
- Desktop Patch table selects whole rows and validates type/hasMove changes immediately.
- Python compile and `git diff --check` pass. Firmware C++ build intentionally not run per project rule.

## Session 49 - 2026-08-27 - Firmware review fixes

### File
- `dmx_web_rgb/dmx_web_rgb.ino`

### Changes
- Fixed LTP timestamp comparison with wrap-safe `timestampNewer()`.
- Optimized bulk `ALL` and group operations: update layers first, call `recomputeWant()` once.
- Fixed JSON import commit to preserve original preset indexes using `parsed[]`.
- Reduced HTTP import cap from 64 KB to 48 KB to reduce heap pressure.
- Added mutex allocation failure guard.
- Added `Preferences.begin()` checks for main NVS and WiFi NVS paths.
- Added HTTP POST registration for `/wifiset`, while retaining GET compatibility for current desktop `HttpTransport`.
- Rejected serial `IMPORT_C` chunks over 64 values.
- Kept desktop protocol unchanged: `GET`, `LISTF`, `LISTG`, `LISTP`, `LISTS`, `EXPORT`, `SAVE`, `LOAD`, control commands, WiFi commands, and serial import remain available.

### Intentionally not changed
- Authentication deferred per user request.
- Existing GET mutation endpoints retained for desktop compatibility.
- No C++ build performed, per project rule.

### Validation
- `git diff --check` passed.
- Static source review completed.
- Runtime and board-specific compile test remain required by user on target ESP32/core/library versions.

### Remaining risks
- NVS writes are still multi-key and not journaled/transactional.
- Large `String` JSON allocations remain.
- State revision is not fully atomic across cores.
- Network startup remains blocking.

## Prior history

Previous detailed session history was condensed during this review. The current source of truth is the firmware, desktop protocol implementation, and this changelog.
