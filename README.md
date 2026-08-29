# DMX512 — ESP32 Web Lighting Console

Controller DMX512 open-source berbasis ESP32 + MAX485 dengan Web UI dan aplikasi desktop Windows. Satu universe (512 channel), preset/scene/chase, patch fixture runtime, custom fixture type, dan kontrol dari browser, desktop, maupun serial.

> **Status**: aktif dikembangkan — v48. Cocok untuk prototyping closed-network (lihat [Keamanan](#keamanan)).
> Changelog lengkap per sesi: [`docs/logs.md`](./docs/logs.md)

## Fitur utama

### Kontrol & playback
- Fader channel per fixture dengan label per fungsi (Dim/R/G/B, Pan/Tilt, dll).
- **Dual-pane mixer per tipe fixture**: kiri fader per-fixture, kanan **fader bank** (satu fader mempengaruhi semua fixture se-tipe).
- Master, strobe, blackout, chase.
- 30 preset dengan fade & hold independen per preset.
- 20 scene × 50 langkah; **copy-on-write** — scene aman dari perubahan/hapusan preset yang dirujuknya.
- Custom fixture type (11 slot): nama slider & jumlah channel bebas, mode **Fader** (0-255) atau **Switch** (binary 0/255 untuk relay/beban on-off) per channel.
- Patch runtime (maks 32 fixture) dari Web UI / desktop / serial — tersimpan NVS.

### Konektivitas
- **WiFi STA** (kredensial kustom dari UI) + **W5500 Ethernet** (prioritas 1) + AP darurat (prioritas 3).
- Web UI responsif (browser/HP/tablet); WebSocket realtime, HTTP fallback.
- Aplikasi desktop Windows (PySide6): fitur setara Web UI, USB serial atau WiFi, MIDI controller + MIDI-learn.
- Protokol serial teks (JSON) untuk integrasi eksternal.
- Lintas-client: dua operator (web + desktop) tersinkron otomatis.

### Keandalan
- NVS **transactional**: commit-marker generation + read-back verify — listrik mati saat simpan tidak menghasilkan data campuran.
- Urutan boot Ethernet → WiFi → AP dengan staged delay (ramah PSU marginal).
- Import/export seluruh data JSON, Save/Load Data ke NVS.
- Dual-core: timing DMX di Core 0, jaringan/UI di Core 1.

## Struktur

```text
dmx_web_rgb/        # Sketch firmware utama (satu file .ino)
desktop/            # Aplikasi desktop PySide6 (+ build spec PyInstaller)
  ui/               #   tab mixer/preset/scene/patch/sistem + widgets
  midi_handler.py   #   mapping MIDI controller
docs/               # Desain, riset DMX512, wiring, prosedur testing, changelog
tests/              # Unit test logika murni (python)
.kilo/rules.md      # Aturan kerja repo (commit/push per fitur, dsb.)
ses/                # Log sesi pengerjaan (arsip)
```

## Setup — Firmware (Arduino IDE)

### 1. Kebutuhan

| Komponen | Versi |
|---|---|
| Arduino IDE | 2.x |
| Board ESP32 (arduino-esp32) | **core 3.x** (diuji di 3.3.7) |
| Board target | ESP32 Dev Module (diuji di ESP32-D0WD-V3 rev 3.1) |
| Python (untuk esptool bawaan IDE) | 3.9+ |

### 2. Library

Instal via **Library Manager** Arduino IDE:

| Library | Kegunaan |
|---|---|
| `Dmx_ESP32` (Tobias Blum / arduino-nodeconfig) | Output DMX512 via UART |

Library lain (`WiFi`, `WebServer`, `ESPAsyncWebServer`, `Preferences`, `ETH`, `Arduino.h`) bawaan/bundled core ESP32.

> `ESPAsyncWebServer` di core 3.x sudah bundled. Bila versi lama menuntut instal manual, gunakan fork `ESP32Async/ESPAsyncWebServer`.

### 3. Wiring

**MAX485 (output DMX):**

```text
ESP32 GPIO17 -> MAX485 DI
ESP32 GPIO16 -> MAX485 RO   (input, opsional)
ESP32 GPIO4  -> MAX485 DE + RE
VCC 3.3V/5V sesuai modul, GND bersama
DMX keluar: A(+) / B(-) via kabel twisted-pair, ujung 120Ω terminator
```

**W5500 Ethernet (opsional, SPI):**

```text
SCLK GPIO18 · MISO GPIO19 · MOSI GPIO23 · CS GPIO5
W5500 VCC -> 3.3V (modul dgn regulator bisa 5V), GND bersama
```

Detail: [`docs/wiring_diagram.txt`](./docs/wiring_diagram.txt)

### 4. Kredensial default

Ubah di bagian atas `dmx_web_rgb.ino` **atau** set dari Web UI (tersimpan NVS):

```cpp
const char* WIFI_SSID = "SIGMA";        // ganti SSID WiFi Anda
const char* AP_SSID   = "DMX-RGB";      // AP darurat bila WiFi gagal
```

### 5. Compile & upload

1. Buka `dmx_web_rgb/dmx_web_rgb.ino`.
2. Board: **ESP32 Dev Module**; Partition Scheme: default (NVS ±20 KB).
3. Upload. Serial Monitor 115200 baud harus menampilkan:

```text
=== DMX Web Console v48 ===
Ethernet W5500: inisialisasi... (prioritas 1)
WiFi: menyambung ke <SSID> ....
WiFi tersambung. IP: http://192.168.x
```

4. Buka IP tersebut di browser.

> **Catatan daya**: boot + radio + tulis flash memunculkan lonjakan arus. Gunakan PSU/port USB ≥ 900 mA (USB 3.0) atau PSU eksternal 5V ≥ 2A. Gejala kurang daya: `E BOD: Brownout detector was triggered` berulang.

## Setup — Desktop (Windows)

Kebutuhan: **Python 3.9-3.12** (PySide6 dipin ke 6.6.3.1; lihat komentar `requirements.txt` bila pakai Python 3.10+).

```powershell
cd desktop
python -m pip install -r requirements.txt
python main.py        # atau: python run.py
```

Hubungkan via USB serial (pilih COM port) atau WiFi (masukkan IP ESP32).

### Build EXE (distribusi)

```powershell
cd desktop
build.bat            # PyInstaller via DMX512Controller.spec
```

Distribusikan **seluruh folder** `desktop/dist/DMX512Controller/` — jangan hanya `.exe` (DLL Qt runtime ada di folder yang sama).

## Penggunaan singkat

1. **Patch** (tab/panel Patch): atur nama/tipe/alamat fixture, atau buat **Tipe Custom** (slot 5-15) untuk fixture non-standar.
2. **Mixer**: geser fader fixture (kiri) atau **bank** (kanan) — realtime.
3. **Preset**: REKAM kondisi channel; atur fade/hold per preset.
4. **Scene**: susun urutan preset (50 langkah), PLAY untuk playback.
5. **Save Data** → persist ke NVS (juga auto-save 60 dtk).

## Pengujian

[`docs/TESTING.md`](./docs/TESTING.md) — prosedur uji firmware, Web UI, desktop, serial, WiFi, Ethernet, MIDI.
Unit test logika: `python tests/test_labels.py`.

## Roadmap

### Selesai
- [x] Web UI + WebSocket realtime (v33+)
- [x] Patch fixture runtime + NVS (v45)
- [x] Copy-on-write proteksi scene (v46)
- [x] NVS transactional commit-marker (v46)
- [x] Mixer section per tipe + label chart Moving/Beam (v47)
- [x] Dual-fader bank + custom fixture type + switch mode (v48)
- [x] Streaming UI anti-fragmentasi heap (v48)

### Berikutnya (prioritas)
- [ ] Refactor: streaming export/import JSON (menghapus alokasi ~64 KB terakhir)
- [ ] Pecah `.ino` besar jadi modul (`webui_html.h`, `nvs_storage.h`, dll) — pindahan verbatim
- [ ] Partisi NVS diperbesar (48 KB) + `PATCH_CH_TOTAL` 176→512
- [ ] **sACN/Art-Net input** — kendalikan controller dari QLC+/xLights (merge HTP/LTP + toggle LOCAL/NETWORK)
- [ ] Tap-sync BPM chase + XY-pad pan/tilt (WebUI)

### Visi (jangka panjang)
- [ ] Sistem **multi-universe modular**: node ESP32+W5500+MAX485 per universe, link Art-Net via switch LAN
- [ ] Output Art-Net dari controller (gateway N universe)
- [ ] Autentikasi HTTP/WS (prasyarat penggunaan di luar closed-network)
- [ ] Sound-active (modul mic + deteksi beat)

## Keamanan

Endpoint HTTP/WS **tanpa autentikasi** dan beberapa mutasi memakai GET — dirancang untuk **closed-network prototyping** (SSID/switch sendiri, tidak ter-expose internet). Sebelum dipakai di venue publik/jaringan bersama: implementasi auth + mutasi POST (ada di roadmap).

## Lisensi

MIT License — lihat [`LICENSE`](./LICENSE).
