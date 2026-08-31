# DMX512 — ESP32 Web Lighting Console

Controller DMX512 open-source berbasis ESP32 + MAX485 dengan Web UI dan aplikasi desktop Windows. Satu universe (512 channel), preset/scene/chase, patch fixture runtime, custom fixture type, input **Art-Net**, **deck tombol fisik**, dan kontrol dari browser, desktop, maupun serial.

> **Status**: aktif dikembangkan — v49.4. Cocok untuk prototyping closed-network (lihat [Keamanan](#keamanan)).
> Changelog lengkap per sesi: [`docs/logs.md`](./docs/logs.md)

## Daftar fitur lengkap

### Mixer & output DMX
- 512 channel DMX512-A (RS-485, 250 kbps, 8N2), frame 40 fps presisi FreeRTOS.
- Fader per-channel dengan label per fungsi (PAR chart, Moving Head 20ch, Beam 16ch, Strobe, Fog).
- **Dual-pane mixer per tipe**: kiri fader per-fixture, kanan **fader bank** (satu fader menulis channel sama ke semua fixture se-tipe, 1 pesan agregat per frame DMX).
- **Mobile-first**: fader bank tampil di atas fader individual di layar sempit; tabel patch berubah jadi kartu (tanpa scroll horizontal).
- Master dimmer, strobe master (gate 40 ms–2 s), blackout.
- **Fade akurat**: interpolasi linear dengan akumulator fraksi per channel — lintasan selalu tuntas ke nilai target (pernah bug "mentok 230-an", fixed v49.4).
- HTP/LTP per-timestamp: fader manual, playback, dan jaringan saling mengambil alih sesuai urutan sentuhan.

### Preset, scene, chase
- 30 preset, fade (0–2,55 s) & hold (100 ms–5 s) per preset.
- 20 scene × 50 langkah (playback loop otomatis, validasi playable).
- **Copy-on-write preset**: merekam ulang/mengubah/menghapus preset tidak merusak scene yang merujuknya (slot bayangan otomatis; `shadow_full` ditolak dengan pesan jelas bila slot habis).
- Chase preset-round-robin.
- Preset detail edit tanpa rekam ulang: ubah fade/hold via `/psetfade` tanpa menyentuh data channel.
- Import/export seluruh data JSON (portabel antar device).

### Patch & custom fixture
- Patch runtime maks 32 fixture (nama, tipe, alamat awal, jumlah channel, flag pan/tilt), validasi tumpang tindih & batas 512.
- **Tipe fixture custom (11 slot, slot 5-15)**: nama tipe, jumlah channel (1-32), nama label tiap channel, dan **mode per channel**: Fader (0-255 kontinu) atau Switch (binary 0/255 — relay/beban on-off; di-snap dua lapis client + firmware).
- Tersimpan NVS (`ctcfg`), commit by-slot, read-back verify.

### Input fisik (hardware button deck)
- **4 tombol scene** + rotary encoder (opsional) di GPIO:
  - B1=GPIO32, B2=GPIO33, B3=GPIO27, B4=GPIO14; encoder CLK=25, DT=26, SW=13.
  - Semua `INPUT_PULLUP` internal (aktif LOW ke GND, nol resistor eksternal).
- **Tap B1-B4 = play scene** grup aktif (1-4, 5-8, ..., 17-20); **HOLD B4/B1 ≥600 ms = pindah grup** (wrap); encoder juga bisa pindah grup; SW = stop playback.
- **Task dedikasi prio 12 Core 1** — deteksi tombol ≤5 ms, bebas kelaparan oleh traffic web.
- **5 lapis anti-noise EMI** (dikembangkan dari kasus nyata): common-mode detector (≥2 pin serempak = noise), debounce dua-arah 60 ms, rate-limit 300 ms, pin-lockout 30 s dengan log pin bermasalah, kill switch `HWOFF/HWON`.
- Status deck terekspos realtime di state JSON (`hwBank`, `hwEnc`, `hwB[4]`) dan ditampilkan di Web UI.
- B2/B3 = go-button (play saat ditekan), B1/B4 = dual-fungsi (play saat dilepas, hold = pindah grup).

### Art-Net (input)
- **Art-Net 4 node input**: ArtDmx UDP 6454 — parser header/opcode/ProtVer/sequence-window sesuai spec (drop duplikat & out-of-order), universe 0:0, dlen 2-512, buffer stack (nol alokasi heap).
- **ArtPoll → ArtPollReply**: node terdeteksi otomatis di QLC+/xLights (nama + versi build + status port).
- Layer ketiga mixer (netWant) LTP-timestamp — QLC+ dan fader lokal saling mengambil alih mulus.
- Mode LOCAL/NETWORK via tombol Web UI, HTTP `/artnet`, atau serial `ARTNET`/`ARTSTAT` (ephemeral, tidak menyentuh flash).
- Status paket diterima terpantau (`ARTSTAT`).

### Konektivitas & jaringan
- **WiFi STA** (kredensial kustom dari UI/desktop, tersimpan NVS) + **W5500 Ethernet** (prioritas 1, auto DHCP) + AP darurat (prioritas 3, hanya bila keduanya gagal).
- Boot bertahap Ethernet → WiFi → AP dengan staged delay anti-spike arus (ramah PSU marginal).
- Web UI responsif (PC/HP/tablet): WebSocket realtime port 81 + HTTP fallback.
- Aplikasi desktop Windows (PySide6): fitur setara Web UI via USB serial atau WiFi; MIDI controller + MIDI-learn.
- **Sinkron lintas-client**: WebUI & desktop tersinkron otomatis (state revision + sceneRev).
- Protokol serial teks/JSON lengkap (SET, GRP, MAST, STRB, PS*, SP*, CHASE, FIXSET, CTSET, ARTNET, DMXSTAT, EXPORT/IMPORT, dsb).

### Web UI
- Single-page dari firmware (streaming PROGMEM chunked — nol alokasi heap besar; anti `__SCNDATA__` class bug).
- Panel: Master (fader/strobe/blackout/chase/tombol Art-Net), Scene, Preset, Mixer, Patch, Sistem.
- Editor Tipe Custom di panel Patch (slot/nama/channel/label/mode per channel, radio Fader/Switch).
- Indikator status: koneksi, mode Art-Net, deck fisik, save status, scene playing, error banner.
- State JSON lengkap tiap broadcast (`/cur` + WS): nilai channel, master, mode, deck fisik, revision, dsb.

### Keandalan & arsitektur
- NVS **transactional**: commit-marker generation + read-back verify semua blob — listrik mati saat simpan tak menghasilkan data campuran; migrasi otomatis data lama (v45 → commit-marker, deferred 10 dtk anti brownout).
- Dual-core dipisah tugas: **DMX engine Core 0** (prio 18 — bebas kelaparan oleh traffic jaringan), Web/serial/deck Core 1 (task dedikasi prio 12 untuk input fisik).
- WS broadcast di-throttle 10 Hz + heartbeat 1 s (hemat heap & bandwidth).
- JSON builder dengan `reserve()` tepat (anti fragmentasi heap; pelajaran bug `__SCNDATA__`).
- Serial `DMXSTAT`: interval frame min/avg/max + snapshot runtime — alat kesehatan DMX built-in.
- auto-save NVS 60 dtk + tombol Save/Load manual.

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

## API & protokol (ringkas)

| Jalur | Endpoint / perintah |
|---|---|
| Kontrol realtime | WS `{"t":"s"\|"mast"\|"strb"\|"all"\|"b",...}` ; HTTP `/set /grp /ctrl /chase` |
| Preset | `/psave /pload /psetfade /pclear /presets` ; serial `PSL PSV PREC PDEL PSF` |
| Scene | `/spush /spop /sclear /splay` ; serial `SPUSH SPOP SCLR SPLAY SSTOP` |
| Patch | GET/POST `/fixes` ; serial `LISTF FIXSET` |
| Grup | `/groups` ; serial `LISTG GRP` |
| Custom type | GET/POST `/ctypes` ; serial `LISTCT CTSET` |
| Art-Net | `/artnet` ; serial `ARTNET ARTSTAT` |
| Kesehatan | `/health /cur` ; serial `DMXSTAT` |
| WiFi | `/wifistat /wifiset` ; serial `WIFISTAT WIFISSET` |
| Data | `/save /loaddata /export /import` ; serial `SAVE LOAD EXPORT IMPORT` |
| Deck fisik | serial `HWOFF HWON` |

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

**Deck tombol fisik (opsional, v49+):**

```text
Tombol 1-4: GPIO 32 / 33 / 27 / 14  -> kaki lain ke GND (INPUT_PULLUP internal)
Encoder  : CLK GPIO25, DT GPIO26, SW GPIO13 (opsional)
Rekomendasi anti-EMI: pull-up eksternal 4,7k per pin + kapasitor 100nF ke GND
(dari kabel daya/dimmer, dan jangan sejalur kabel tombol dgn kabel DMX/daya)
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
=== DMX Web Console v49 ===
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

1. **Patch** (panel Patch): atur nama/tipe/alamat fixture, atau buat **Tipe Custom** (slot 5-15) untuk fixture non-standar.
2. **Mixer**: geser fader fixture atau **fader bank** — realtime. Di HP, bank tampil paling atas.
3. **Preset**: REKAM kondisi channel; atur fade/hold per preset.
4. **Scene**: susun urutan preset (maks 50 langkah), PLAY untuk playback loop.
5. **Art-Net**: klik `Art-Net: NETWORK`, lalu kendalikan dari QLC+/xLights (node terdeteksi otomatis).
6. **Deck fisik**: pasang 4 tombol + encoder, tap untuk play scene, hold B4/B1 pindah grup.
7. **Save Data** → persist ke NVS (juga auto-save 60 dtk).

## Pengujian

[`docs/TESTING.md`](./docs/TESTING.md) — prosedur uji firmware, Web UI, desktop, serial, WiFi, Ethernet, MIDI.
Unit test logika: `python tests/test_labels.py`.
Kesehatan DMX runtime: serial `DMXSTAT` (normal: `avg` ~24-25 ms, `max` < 50 ms).

## Roadmap

### Selesai
- [x] Web UI + WebSocket realtime (v33+)
- [x] Patch fixture runtime + NVS (v45)
- [x] Copy-on-write proteksi scene (v46)
- [x] NVS transactional commit-marker (v46)
- [x] Mixer section per tipe + label chart Moving/Beam (v47)
- [x] Dual-fader bank + custom fixture type + switch mode (v48)
- [x] Streaming UI anti-fragmentasi heap (v48)
- [x] Art-Net input node + ArtPoll discovery (v49)
- [x] Deck tombol fisik + encoder + 5 lapis anti-noise EMI (v49)
- [x] Prioritas task & task dedikasi: anti kelaparan frame & tombol (v49.2/49.4)
- [x] Fade akurat (akumulator fraksi, v49.4)
- [x] Mobile: bank-first + patch card-mode (v49.4)

### Berikutnya (prioritas)
- [ ] Refactor: streaming export/import JSON (menghapus alokasi ~64 KB terakhir)
- [ ] Pecah `.ino` besar jadi modul (`webui_html.h`, `nvs_storage.h`, dll) — pindahan verbatim
- [ ] Partisi NVS diperbesar (48 KB) + `PATCH_CH_TOTAL` 176→512
- [ ] sACN (E1.31) input sebagai pelengkap Art-Net
- [ ] Output Art-Net (gateway universe)
- [ ] Tap-sync BPM chase + XY-pad pan/tilt (WebUI)

### Visi (jangka panjang)
- [ ] Sistem **multi-universe modular**: node ESP32+W5500+MAX485 per universe, link Art-Net via switch LAN
- [ ] Autentikasi HTTP/WS (prasyarat penggunaan di luar closed-network)
- [ ] Sound-active (modul mic + deteksi beat)
- [ ] Port ke **ESP32-S3**: touch button (deck tanpa wiring EMI), USB MIDI native, PSRAM utk multi-universe buffer

## Keamanan

Endpoint HTTP/WS **tanpa autentikasi** dan beberapa mutasi memakai GET — dirancang untuk **closed-network prototyping** (SSID/switch sendiri, tidak ter-expose internet). Art-Net/sACN juga tanpa autentikasi by design (protokol industri). Sebelum dipakai di venue publik/jaringan bersama: implementasi auth + mutasi POST (ada di roadmap).

## Lisensi

MIT License — lihat [`LICENSE`](./LICENSE).
