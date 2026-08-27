# Session Logs - DMX512 Controller ESP32 Project

## Session 1 - 2026-08-18 08:00

### Konteks
User ingin membuat controller DMX512 menggunakan ESP32 + MAX485

### Tasks Completed

#### 1. Riset DMX512 Protocol
- ✅ Delegasi research ke sub-agent (task_id: ses_feda5131dffemWzHoNtsU8KiOn)
- ✅ Generated comprehensive documentation
- ✅ Created directory structure: `E:\code\Arduino\DMX512`

**Files Created:**
- `DMX512_Research.md` - Full protocol specification (368 lines)
  - Protocol basics (RS-485, 250kbaud, 8N2)
  - Timing specs (break 88μs, MAB 8μs)
  - Hardware implementation ESP32+MAX485
  - Library comparison (esp_dmx, ESP-Dmx)
  - Best practices & common pitfalls
  
- `wiring_diagram.txt` - Pinout ESP32→MAX485→XLR-5
  - GPIO17 (TX2) → DI
  - GPIO16 (RX2) → RO
  - GPIO21 (RTS) → DE/RE
  - Termination notes
  
- `examples/dmx_transmitter_basic.ino` - Basic TX example (35 lines)
  - esp_dmx library usage
  - 3-channel RGB demo
  - Brightness fade effect
  
- `examples/dmx_receiver_basic.ino` - Basic RX example (35 lines)
  - Packet reception
  - Error handling
  - Serial output

#### 2. Analisis Pro/Kontra ESP32 untuk DMX
- ✅ Created `ESP32_DMX_Analysis.md` - Comprehensive analysis

**Pro (8 points):**
- Hardware UART powerful (3 UART, DMA, 250kbaud easy)
- Dual core CPU (dedicated DMX task)
- WiFi/BLE built-in (Art-Net, sACN bridge)
- Large memory (520KB RAM, 4MB+ Flash)
- Cheap ($2-5)
- 30+ GPIO (encoders, LCD, buttons)
- Development friendly (Arduino, PlatformIO, OTA)
- Low power modes (battery feasible)

**Kontra (8 points) + Solutions:**
1. 3.3V logic → MAX485 needs 5V
   - Solution: Level shifter TXS0108E or SN65HVD75 (3.3V native)
   
2. WiFi timing jitter
   - Solution: Pin DMX task to Core 0, max priority
   
3. Break generation not native
   - Solution: esp_dmx library handles it
   
4. No optical isolation
   - Solution: 6N137 optocoupler + TVS diode
   
5. Half-duplex only
   - Solution: Fast DE/RE switching or dual MAX485
   
6. Flash wear
   - Solution: Wear leveling, RAM cache
   
7. Heat dissipation
   - Solution: Heatsink 20×20mm
   
8. Limited RDM support
   - Solution: esp_dmx library or skip RDM

**Production-Ready Design:**
- ESP32-S3 recommended (better WiFi, more RAM)
- SN65HVD75 transceiver (3.3V native, ESD 15kV)
- TXS0108E level shifter (if using MAX485)
- 6N137 optocoupler (galvanic isolation 2500V)
- SMAJ5.0CA TVS diode (bus protection)
- Neutrik NC5FD-LX XLR-5 connector
- 120Ω terminator (jumper selectable)

**Software Architecture:**
- Core 0: DMX task (highest priority, no WiFi)
- Core 1: Art-Net, UI, application logic
- Library: esp_dmx v4.0+ (RDM support)
- FreeRTOS task pinning

**BOM Cost: ~$15**
**Development Timeline: 4 weeks**

### Recommendations
- ESP32 **sangat cocok** untuk DMX controller dengan proper design
- Use cases: WiFi bridge, portable controller, multi-universe, DIY projects
- NOT suitable: Professional fixtures (use Teensy), safety-critical, 24/7 industrial

### Files Structure
```
E:\code\Arduino\DMX512\
├── DMX512_Research.md          (368 lines - protocol specs)
├── ESP32_DMX_Analysis.md       (NEW - pro/kontra analysis)
├── wiring_diagram.txt          (39 lines - pinout)
├── logs.md                     (THIS FILE)
└── examples/
    ├── dmx_transmitter_basic.ino
    └── dmx_receiver_basic.ino
```

### Next Steps (User Decision)
- Hardware procurement (ESP32-S3, MAX485/SN65HVD75, connectors)
- Library installation (esp_dmx via Arduino Library Manager)
- Breadboard prototype
- Code implementation (basic TX first, then WiFi integration)

---

## Changelogs

### 2026-08-18 08:00 - Initial Research
- Created project directory structure
- Completed DMX512 protocol research (10 topics)
- Generated wiring diagrams
- Created transmitter/receiver code examples
- Analyzed ESP32 suitability for DMX512
- Documented 8 pros, 8 contras with solutions
- Provided production-ready design recommendations

---

## Technical References

**Key Specs:**
- Protocol: RS-485, 250kbaud, 8N2, unidirectional
- Frame: BREAK(88μs) + MAB(8μs) + StartCode(0x00) + 512ch(0-255)
- Timing: 44μs per byte, 25-44fps typical
- Cable: 120Ω twisted pair, XLR-5 connector
- Termination: 120Ω resistor at end of chain

**Recommended Components:**
- MCU: ESP32-S3 ($5)
- Transceiver: SN65HVD75 or MAX485+level shifter
- Isolation: 6N137 optocoupler
- Protection: SMAJ5.0CA TVS diode
- Connector: Neutrik NC5FD-LX

**Library:**
```cpp
#include <esp_dmx.h>
// GitHub: https://github.com/someweisguy/esp_dmx
```

---

## Session 2 - 2026-08-21 16:05

### Main sketch verification
- Dibaca `main.ino`, sketch berhasil menurut user.
- Library aktual: `Dmx_ESP32.h`, bukan `esp_dmx.h` dari contoh riset sebelumnya.
- UART: `HardwareSerial DMXSerial(2)` pada 250000 baud, `SERIAL_8N2`.
- Pin aktual: GPIO17 TX, GPIO16 RX, GPIO4 DE+RE.
- Fixture teruji: PAR LED, start address 1, footprint 4 channel.
- Mapping teruji: CH1 master dimmer, CH2 red, CH3 green, CH4 blue.
- API aktual: `DMX.write(value, channel)` lalu `DMX.transmit()`.
- Fungsi saat ini: RGB manual, dimmer, blackout, fade warna, color test.

### Catatan integrasi
- Contoh Web UI sebelumnya memakai API `esp_dmx`; tidak bisa langsung digabung dengan `main.ino`.
- Pengembangan berikutnya harus mempertahankan `Dmx_ESP32.h`, `DMXSerial`, pin GPIO4, dan urutan argumen `DMX.write(value, channel)`.
- TX-only: GPIO16/RO tidak diperlukan oleh logika, tetapi jika MAX485 diberi 5V jangan sambungkan RO langsung ke GPIO16. Gunakan divider atau lepaskan RO.
- Multi-lampu: tulis seluruh channel semua fixture terlebih dahulu, panggil `DMX.transmit()` satu kali per frame.

---

## Session 3 - 2026-08-21 16:25

### Web RGB Controller (`examples/dmx_web_rgb.ino`)
- Dibuat kontrol warna RGB + dimming melalui website.
- ESP32 terhubung ke WiFi SSID "Selin" / pass "lalisandi" (mode STA).
- Server: WebServer bawaan ESP32 port 80, endpoint `/`, `/set`, `/cur`, `/off`, `/white`.
- IP dicetak ke Serial (115200), dan disuntik ke elemen meta UI lewat token `__IP__`.
- Dikonfirmasi dari source library `Dmx_ESP32`:
  - `dmxTx` konstruktor `(port, pinTx, pinEnable)`; `configure()` set pin enable HIGH utk TX.
  - `write(data, channel)` channel 1..512, tulis ke `_dmxBuf[channel]`.
  - `writeBytes(data, numBytes, startChannel)` isi semua channel sekaligus.
  - `transmit()` kirim break + MAB + 513 byte; `dmxBuffer()` akses buffer langsung.
  - Warning library: jangan punya 2 transceiver DE mode bersamaan.
- UI: dark theme (alasan fungsi konsol pencahayaan), slider besar target sentuh,
  swatch live preview, dua tombol (Mati / Putih), status koneksi. Responsif PC + HP.
- Arah desain ditulis di `DESAIN.md` (ENERGY 1 / RHYTHM 2 / MOTION 1).
- Perbaikan minor: hapus variabel JS mati, token IP dimasukkan ke HTML.

### Struktur file
```
examples/dmx_web_rgb.ino   <- controller web (file aktif)
DESAIN.md                  <- arah visual UI
main.ino                   <- baseline yang teruji (tidak diubah)
```

---

## Session 4 - 2026-08-21 18:05

### Fitur: bank preset gaya konsol (`examples/dmx_web_rgb.ino`)
- Ditambahkan 8 bank preset (gaya konsol lighting).
- Model operasi: tombol REKAM ON/OFF + pad preset.
  - REKAM ON -> tekan pad = simpan kondisi slider (dimmer+R+G+B) saat ini ke pad itu.
  - REKAM OFF -> tekan pad = muat preset ke slider + langsung kirim ke DMX.
- Penyimpanan via NVS (`Preferences`, namespace "dmxrgb", key "presets"),
  8 x `struct{uint8 d,r,g,b,used}` = chunk 5 byte, tanpa padding.
- Endpoint baru: `/presets` (JSON), `/pload?n=`, `/psave?n=`, `/pclear?n=`.
- UI: grid 4x2 pad, swatch warna per pad, pad kosong diarsir (hatch),
  highlight kuning pada preset yang sedang aktif, lepas highlight saat nilai diubah.
- Realtime fader dipertahankan (throttle in-flight) dari session sebelumnya.
- Catatan: struct memakai `uint8_t used` (bukan bool) agar ukuran biner deterministik.

---

## Session 5 - 2026-08-21 18:40

### Upgrade besar: `examples/dmx_web_rgb.ino` (4 PAR + 5 fitur)
Refactor penuh menuju model multi-PAR + console-style.

#### Model data
- `N_PAR=4` PAR LED: channel per PAR = `p*4+1` (Dim), +2,+3,+4 (R,G,B).
- `ParState{d,r,g,b}`, `Preset{ ParState par[4]; used; ignoreDimmer; }`.
- Dua layer output: `want[]` (target) vs `out[]` (tampilan); fade interpolasi want->out.
- Preset NVS dengan `PRESET_VER=2` (guard format bila skema berubah).

#### Fitur yang ditambahkan
1. **Master dimmer global + Blackout** - master slider mengalikan dimmer tiap PAR.
2. **Chase / auto-run** - memutar antar preset used pada interval `/chase?on=1`, henti saat slider digerakkan.
3. **Fade-time / crossfade** - slider Fade (0-2000ms), pemuatan preset & chase lewat `want` lalu fade.
4. **Kontrol 4 PAR di UI** - grid 2 kolom (desktop), tiap PAR punya 4 slider; snapshot manual = snap realtime.
5. **Rekam preset opsi "tanpa dimmer"** - checkbox idim; saat apply preset dengan idim, dimmer diset ke 255 (warna penuh).

#### Endpoint baru
- `/set?master=&{p}{c}=` parsa `master` + per-channel (key 2 char).
- `/ctrl?fade=` set fade ms.
- `/chase?on=1|off=1[&sp=ms]` start/stop chase.
- `/cur` JSON master+fade+par (inisialisasi UI).
- `/pload|/psave?n=&idim=|/pclear` preset (diperbarui ke 4 PAR).

#### Catatan implementasi
- `mulScale` mengalikan dimmer dengan master tanpa overflow (16-bit trap).
- Loop: chaseTick -> fadeTick(0.025f) -> buildFrame tiap 25ms (frame kontinu).
- Manual slider menulis want DAN out (snap); preset load & chase menulis want saja (fade).
- JS: objek `sliders` dikunci `"{p}_{c}"`, `data-col` untuk warna fill slider.

---

## Session 6 - 2026-08-21 21:33

### Refactor: model PATCH TABLE (`examples/dmx_web_rgb.ino`)
Mengganti model offset `p*4+1` menjadi **patch table general** sesuai rekomendasi paling efisien.

#### Patch table
```c
struct Fixture { const char* name; uint8_t type; uint16_t start; uint16_t foot; };
Fixture fix[N_FIX] = {
  { "PAR 1",    FX_PAR,    1,  4 },
  { "PAR 2",    FX_PAR,    5,  4 },
  ...
  { "BEAM 1",   FX_BEAM,  21, 16 },
  { "MOVING 1", FX_MOVING,37, 20 },
  { "STROBE 1", FX_STROBE,57,  4 },
  { "FOG 1",    FX_FOG,   61,  2 },
};
```
- Jenis fixture: PAR / MOVING / BEAM / STROBE / FOG.
- Layout: blok per tipe, sisa >63 = spare ekspansi (efisien, tanpa gap antar unit).
- PAR footprint 4 (bukan 9) -> hindari boros 50 slot; blok spare terpisah untuk ekspansi.

#### Perubahan inti
- Patch table = satu sumber kebenaran.
- Bug timing: fadeTick dipindahkan ke blok 25ms (sebelumnya dipanggil tiap loop -> fade 40x lebih cepat).
- Bug fade stuck: tambah snap threshold `<2` supaya output tepat mencapai target preset/master.
- Header dokumen ditandai versi FINAL.

---

## Session 7 - 2026-08-21 22:53 (FINAL INDUSTRIAL)

### `examples/dmx_web_rgb.ino` - versi final industrial

#### Arsitektur core (anti hang/freeze)
- DMX timing di **Core 0** via `xTaskCreatePinnedToCore(dmxTask, ..., 5, ..., 0)`.
- WebServer di **Core 1** (`loop()` Arduino).
- `vTaskDelayUntil` untuk frame realtime ~40fps presisi.
- FreeRTOS **mutex (`dmxMutex`)** melindungi `want[]`/`out[]`/`master*` antar core.
- State lintas-core ditandai `volatile` (masterOut/masterWant/fadeMs/chaseOn/chaseMs/chaseIdx).

#### Persistensi NVS (storage ibarat HDD/SSD)
- Preset disimpan di NVS flash -> **tahan listrik mati / reboot** (jawaban: ya).
- Simpan NVS hanya saat user merekam atau import (bukan tiap frame) -> flash awet.
- Format NVS: `ver` tag (PRESET_VER=4) + 8 x chunk 513 byte.

#### Import / Export preset
- `GET /export` : unduh `dmx-presets.json`
- `POST /import` (multipart) : upload file JSON -> parse manual -> simpan NVS
- Format : `{"app":"DMX-RGB","ver":4,"presets":[{"u":0/1,"c":[512 nilai]},...]}`
- Parser manual tanpa ArduinoJson (hemat flash).

#### Patch table (user request: 10 PAR @9ch + 8 fixture lain)
- 10 PAR foot 9 (1-90), 2 moving (20c), 2 beam (16c), 2 strobe (4c), 2 fog (2c) -> 174 ch, sisa >174 spare.

#### Optimasi & standar industri
- `DMX.setBreakLength(100)` -> break dalam spek DMX512-A (88-176us).
- Frame 40fps; terminator 120 Ohm di-catat; kabel DMX dedicated.
- Kirim channel: slider -> `pushOne` (1 param); master/all/fade/chase -> server-side `/ctrl` (hindari request raksasa utk 174 ch).
- Catatan hardware: MAX485 5V; bila RX dipakai pasang divider 1k+2k ke GPIO16.

#### Total baris kode final: ~610 baris (fitur lengkap; ringkas krn pola terpusat patch table + 1 handler set).

---

## Session 8 - 2026-08-21 23:04

### Tambahan: slider Chase Speed di UI
- Slider baru di panel Master: `id=chase`, rentang 200-5000ms, step 100, default 1500ms.
- JS: `setChaseLabel`, listener `input` -> `pushCtrl('chase=...')`.
- Server `/ctrl`: tambah `if(server.hasArg("chase")) chaseMs=...` (bisa berubah live saat chase berjalan).
- `/cur`: tambah field `chase` agar UI tersinkron saat halaman dibuka.
- Tidak ada konflik nama: fungsi `setChase(on/off)` terpisah dari id slider `#chase`.

---

## Session 9 - 2026-08-22 01:10

### Tiga fitur Grade A (dmx_web_rgb.ino)

#### 1. Tracking UI (polling /cur)
- `syncFromServer(j, skipActive)` dipakai init & polling.
- Polling `/cur` tiap 700ms (`document.hidden` -> skip).
- Slider yang sedang digeser user dilewati via `activeKey` (di-set saat `input`, dilepas saat `change`).
- Efek: saat chase/preset fade berjalan, slider di UI ikut bergerak menunjukkan nilai `out[]` aktual (seperti konsol nyata).

#### 2. LTP + Blackout-on-move (moving head/beam)
- `Fixture` tambah field `hasMove` (1 utk MOVING/BEAM; pan=ch0, tilt=ch2).
- `applyPresetToWant`: deteksi lompatan pan/tilt >30 -> `blackoutEnd[f]=now+350`.
- `buildFrame`: saat `now < blackoutEnd[f]`, dimmer fixture dipaksa 0 (frame[base]=0) -> lampu tidak "menggambar" lintasan kotor; setelah 350ms nyala lagi di posisi baru.
- Berlaku juga untuk perpindahan chase (chaseTick memanggil applyPresetToWant).
- Slider manual tetap snap (LTP by nature).

#### 3. Preset 8 -> 16
- `N_PRESETS=16` (NVS 16x513=8208 byte, masih muat partisi default).
- `PRESET_VER=5` (format sama, versi naik agar NVS lama direset bersih).
- Bank UI, export/import, chase otomatis mengikuti karena loop `i<N_PRESETS`.

---

## Session 10 - 2026-08-22 01:35

### Code review menyeluruh + perbaikan semua temuan (dmx_web_rgb.ino, ~741 baris)

#### HIGH (semua diperbaiki)
- **H1 Race transmit saat boot**: `buildFrame()` dipindah SEBELUM `xTaskCreatePinnedToCore`
  (dulu setelahnya -> dua transmit paralel, frame boot bisa rusak).
- **H2 Import destruktif**: `importJson` kini parse ke `tmp[16][513]` (static BSS);
  commit `memcpy` per baris HANYA bila parse sukses (`found>0`). File rusak ->
  return false, preset lama utuh. Semantik merge (baris tak ditemukan tidak disentuh).
- **H3 OOM upload**: `IMPORT_MAX 65536`; chunk melebihi -> `importTooBig` -> HTTP 413;
  `UPLOAD_FILE_ABORTED` ditangani.

#### MEDIUM (semua diperbaiki)
- **M1 Mutex penulis presets**: `importJson` (commit), `onPresetClear`,
  `capturePreset` (flag used pindah ke dalam lock), `nextUsedPreset` (pembaca).
  Pembaca JSON (`presetsJson`/`exportJson`) memakai snapshot baris 513 byte per preset.
- **M2 Throttle pushOne**: `setInFlight`/`setPending` -> maks 1 request /set beredar.
- **M3 renderBank per tick**: diganti `updateBankSel()` (toggle class `sel` saja).
- **M4 Hazard "Semua Penuh"**: server `all=on` hanya PAR (dimmer+RGB)=255;
  moving/beam/strobe/fog di-nol-kan. JS `btnWhite` ikut set slider PAR saja.
- **M5 chaseOn di `/cur`**: field `chaseOn` + JS `applyChaseBtn()` sinkron saat
  server menghentikan chase sendiri.

#### LOW (diperbaiki)
- L1: clamp `fade` 0-5000ms, `chase`/`sp` 100-10000ms.
- L2: parser import dibatasi ke objek preset (`objEnd`), tidak salah tangkap `"c":[` milik
  preset lain; `pos` selalu maju (tidak ada re-parse).
- L3: `exportJson` pakai `reserve(36000)`; parser angka manual tanpa alloc `substring`.
- L4: `onCur` membaca `out[]` di dalam mutex.
- L5: dokumen header disinkronkan (16 preset, ver 5, hapus typo "pmem world").
- L6: `activeKey` dilepas juga via `blur` (master/fade/chase + slider channel).
- L7: catatan keamanan (tanpa auth) ditambahkan di header.

#### Dead code
- `inFlight`/`queued` (sisa throttle lama) dihapus.

#### Belum diubah (disengaja)
- Tanpa autentikasi endpoint (hobby; terdokumentasi di header).
- `onSet` dengan args==0 merender UI (quirk tak berbahaya).

---

## Session 11 - 2026-08-22 01:40

### Perubahan WiFi: AP -> Station ke "SIGMA"
- `WIFI_SSID="SIGMA"`, `WIFI_PASS="1ngantos12"` (mode STA, `WiFi.begin`).
- Tunggu koneksi maks 15 detik; IP DHCP dicetak ke Serial.
- **Fallback AP darurat**: bila gagal tersambung -> otomatis buka AP
  "DMX-RGB"/12345678 (http://192.168.4.1) supaya tidak terkunci dari device.
- Helper `activeIP()`: pilih `localIP` (STA) atau `softAPIP` (fallback) ->
  dipakai Serial print dan token `__IP__` di UI.
- `WiFi.setSleep(false)` agar respons web lebih responsif di mode STA.
- Header dokumen bagian KONEKSI diperbarui.

---

## Session 12 - 2026-08-22 01:48

### Fix compile error: 'DMX' was not declared in this scope
- Penyebab: deklarasi `HardwareSerial DMXSerial(2);` dan `dmxTx DMX(...);`
  hilang saat refactor besar (blok "DMX" tertinggal dari file final).
- Perbaikan: deklarasi dikembalikan setelah blok PIN:
  ```cpp
  HardwareSerial DMXSerial(2);
  dmxTx DMX(&DMXSerial, DMX_TX_PIN, DMX_ENABLE_PIN);
  ```
- Audit simbol global lain (fungsi + variabel): semua lengkap, tidak ada korban kedua.

---

## Session 13 - 2026-08-22 02:50

### Fitur: FADER BANK (1 fader -> banyak lampu, ala konsol)

#### Metode: soft-patch (grup fixture x offset channel)
- Fader terikat ke (tipe fixture + satu offset channel lokal).
- Geser fader = tulis channel offset itu di SEMUA fixture anggota grup (snap realtime).
- Kenapa: (1) persis model konsol (fader=channel, patch=fixture), (2) tanpa state baru -
  menulis ke want/out yang sama sehingga master/fade/preset/chase/blackout tetap bekerja,
  (3) tracking /cur otomatis menampilkan efeknya ke slider per-channel,
  (4) LTP: slider per-channel tetap bisa override manual.
- Alternatif yang ditolak: fader page A/B (174 ch butuh banyak halaman),
  fader intensitas + warna global terpisah (dua sistem, kompleks).

#### Implementasi
- C++: `struct FaderGroup {name, typeFilter, offset}` + tabel `grp[8]`:
  PAR Dim/R/G/B (offset 0-3), MH Dim (offset 5), Beam Dim (offset 6),
  Strobe (offset 1), Fog (offset 0). Offset = chart DMX, bisa diedit.
- Endpoint `GET /grp?i=X&v=N`: loop fixture dgn type cocok, tulis want+out (snap, mutex).
- `grpJson()` + token `__GRPDATA__` di sendUi.
- UI: panel "Fader Bank" (antara Preset dan Channel); label fader menampilkan
  jumlah anggota (mis. "PAR Red x10"); slider grup tak disentuh polling
  (posisi = input gesture, disinkronkan hanya saat halaman dibuka via `syncGroups`).
- Geser fader grup menghentikan chase (konsisten dgn slider per-channel).

---

## Session 14 - 2026-08-22 03:05

### Fitur: tombol HAPUS preset
- Tombol `HAPUS OFF/ON` di panel Preset (sejajar REKAM), pola interaksi sama:
  aktifkan HAPUS -> tekan pad yang mau dihapus -> mode keluar otomatis.
- Konfirmasi `confirm()` sebelum hapus; pad kosong diabaikan.
- Saat mode HAPUS aktif, pad berisi diberi border merah (`.bank.deleting .pad.used`)
  supaya jelas target mana yang akan terhapus.
- Mutual exclusion: mengaktifkan HAPUS mematikan REKAM, dan sebaliknya.
- Server: endpoint `/pclear?n=X` yang sudah ada (mutex + savePresets) dipakai;
  UI refresh bank setelah hapus via `refreshPresets()`.
- Helper JS `exitDelMode()` utk keluar mode secara konsisten (termasuk bila user
  membatalkan lewat dialog konfirmasi).

---

## Session 15 - 2026-08-22 03:40

### Fitur: SCENE (20 scene x 30 langkah preset, auto-play)

#### Konsep
- Scene = **rantai referensi preset** (bukan salinan channel): tiap langkah menyimpan
  nomor preset (1..16), 0=kosong. Revisi preset otomatis ikut saat scene diputar.
- Tujuan operator: sekali merangkai scene, show jalan sendiri tanpa menyentuh fader.
- Chase TETAP global di preset (tidak berubah); scene punya pemutar sendiri.

#### C++
- `scenes[20][30]` (600 byte) di NVS (`sver` tag SCENE_VER=1) -> tahan listrik mati.
- `sceneTick()` di dmxTask Core0: maju ke langkah non-kosong berikutnya (wrap),
  skip preset yang sudah dihapus, apply via applyPresetToWant (fade +
  blackout-on-move otomatis). Interval `sceneMs` clamp 100-10000ms.
- Endpoint: `/scenes` (JSON), `/spush?s&p`, `/spop?s`, `/sclear?s`, `/splay?s|off`.
- Cross-stop server-side: PLAY scene mematikan chase; nyalakan chase mematikan scene.
- `/cur` tambah `sceneOn` + `scenesp`.

#### UI (panel Scene, antara Preset dan Fader Bank)
- Bank 20 pad S1-S20 (pad kosong arsir); klik = pilih scene aktif.
- EDIT ON/OFF: saat ON, ketuk pad PRESET = append langkah berikutnya (max 30,
  alert 'penuh'). Mode saling eksklusif dgn REKAM/HAPUS.
- "Akhir" (pop langkah terakhir), "KOSONGKAN" (confirm), PLAY/STOP,
  slider Speed 100-5000ms, readout 30 sel langkah (angka preset / titik).
- `stopAuto()`: geser slider channel/fader-grup/muat preset menghentikan chase+scene.
- Posisi speed & status PLAY tersinkron via polling /cur antar perangkat.

#### Catatan desain
- Preset tetap 16 (NVS ~8KB); scene 30 langkah boleh mereferensikan preset yang sama
  berulang (pola alternasi). Ekspansi preset ke 30 butuh NVS ~15KB -> ditunda.

---

## Session 18 - 2026-08-22 14:05

### Bugfix Web UI (laporan user: scene tak terlihat pilihannya, edit preset tak tersimpan visual, kadang tak bisa play)

#### Akar masalah yang ditemukan & diperbaiki
1. **Cache browser** - tidak ada header cache; setelah firmware di-update browser
   bisa menjalankan UI lama vs endpoint baru -> gejala "bug aneh" acak.
   Fix: `Cache-Control: no-store, must-revalidate` di sendUi().
2. **Scene tak bisa play secara diam-diam** - /splay menerima scene kosong /
   langkah menunjuk preset yg sudah dihapus, lalu sceneTick berhenti sendiri
   tanpa pesan. Fix: /splay memvalidasi >=1 langkah playable; bila tidak ->
   HTTP 409 "kosong" -> JS menampilkan alert penjelasan.
3. **Preset terpilih tak terlihat** - selPreset hilang saat reload; highlight
   `.pad.sel` terlalu samar. Fix: CSS .pad.sel diperkuat (border ganda kuning +
   bg lebih terang), dan baris status `#pinfo` baru di panel Preset:
   "Preset terpilih: #N · fade X ms · hold Y ms" (update saat muat/rekam/edit).
4. **Edit fade/hold "tidak tersimpan"** - /psetfade diam-diam gagal 404 bila
   preset belum ada; tidak ada konfirmasi sukses. Fix:
   - toast hijau "disimpan ke preset #N" saat berhasil;
   - toast peringatan "preset belum ada — rekam dulu" saat 404;
   - REKAM kini otomatis memilih pad hasil rekam (selPreset=i) supaya
     edit lanjutan fade/hold tepat sasaran.

#### Tambahan kecil
- Helper JS `toast(msg)` non-blocking (pengganti alert utk sukses).
- `updatePinfo()` dipanggil di: init, muat preset, rekam, ubah fade/hold,
  dan saat user menggeser slider channel (melepas seleksi).

---

## Session 20 - 2026-08-22 15:10

### Live HTTP debugging terhadap ESP32 `192.168.0.2`

#### Hasil endpoint
- `GET /` -> halaman HTML berhasil, tetapi device yang sedang berjalan masih
  menampilkan header lama `10 PAR + 8 fx`, bukan tag build terbaru.
- `GET /cur` -> JSON valid saat dipanggil satu per satu.
- `GET /presets` -> JSON valid; seluruh preset sempat `used:false` setelah
  migrasi `PRESET_VER`, sehingga scene lama yang menunjuk preset 1-6 memang
  tidak dapat dimainkan sampai preset direkam ulang.
- `GET /scenes` -> JSON valid; Scene 1 berisi referensi `[1,2,3,4,5,6]`.
- `GET /splay?s=1` -> `409` saat semua preset kosong (perilaku benar), lalu
  `on` setelah preset 1 direkam (validasi scene bekerja).
- `GET /pload?n=1` -> `404` saat preset 1 kosong (perilaku benar).
- `GET /psave?n=1&f=100&h=500` -> `ok` (penyimpanan preset berjalan).

#### Temuan jaringan
- Saat `/cur`, `/presets`, `/scenes` ditembak paralel, sebagian request gagal.
- Saat ditembak satu per satu, semuanya berhasil. Penyebab: `WebServer`
  Arduino melayani koneksi secara serial, sedangkan UI lama membuka beberapa
  `fetch` bersamaan.

#### Perbaikan Session 20
- BUILD_TAG dinaikkan `v19 -> v20`.
- Ditambahkan wrapper `window.fetch` dengan antrean satu request aktif:
  polling, slider, preset, dan scene tidak lagi berebut koneksi ESP32.
- Poll `/cur` dilewati jika antrean sedang sibuk dan interval tetap 1000ms.
- Semua request melalui wrapper memakai `cache: no-store`.

#### Verifikasi wajib setelah upload
1. Serial harus mencetak `=== DMX Web Console v20 ===`.
2. Header web harus menampilkan `v20`, bukan `10 PAR + 8 fx`.
3. Rekam ulang preset yang kosong sebelum menjalankan scene lama.

---

## Session 19 - 2026-08-22 14:45

### Audit penuh (1239 baris dibaca utuh) + self-diagnostik

#### Ditemukan & diperbaiki
1. **`__BUILD__` token belum direplace di sendUi** (sisa edit sebelumnya)
   -> ditambah `page.replace("__BUILD__", BUILD_TAG)`.
2. **`onCur` memegang mutex selama membangun String ~3KB** (174 concat)
   -> snapshot `memcpy(snapOut)` di bawah mutex, JSON dibangun di luar;
   kontensi dgn task DMX Core0 turun drastis.
3. **Tidak ada pelapor error JS** -> `window.onerror` menulis
   "JS ERROR: <pesan>" ke baris status (merah) - bug JS tak lagi diam-diam.
4. **Tidak ada penanda versi firmware** -> BUILD_TAG "v19": tampil di header UI
   (span #buildtag) + Serial saat boot. Bila tag lama = cache/upload bermasalah.
5. **Playback indicator** - saat scene main, #sinfo menampilkan
   "▶ MEMUTAR S{n} · langkah k/30" via polling (/cur kini membawa scn/stp);
   saat STOP, renderSteps() memulihkan teks seleksi.
6. Polling /cur 700 -> 1000ms (beban lebih ringan utk multi-klien).

#### Verifikasi struktur (grep)
- BUILD_TAG: def(53) + Serial(1202) + sendUi replace(1037) + HTML(640) ✓
- Semua handler scene/preset terdaftar sekali ✓
- Tidak ada referensi tersisa ke slider global terhapus ✓
- Mutex take/give seimbang di semua jalur ✓

#### Protokol uji utk user
1. Upload -> buka Serial: harus tercetak "=== DMX Web Console v19 ===".
2. Hard-refresh browser sekali (Ctrl+Shift+R).
3. Header UI harus menampilkan "v19". Bila tidak -> cache/upload, bukan kode.
4. Bila ada interaksi mati -> lihat baris status: "JS ERROR: ..." = laporkan pesannya.

---

## Session 21 - 2026-08-22 15:20

### Live HTTP debugging terhadap ESP32 `192.168.0.2`
- `GET /`, `/cur`, `/presets`, dan `/scenes` berhasil saat dipanggil satu per satu.
- Request paralel sebagian gagal, mengonfirmasi `WebServer` single-client sebagai
  sumber bug intermiten ketika UI membuka beberapa `fetch` bersamaan.
- Firmware live saat debugging belum memuat build tag terbaru dan `/cur` belum
  memiliki `scn/stp`, sehingga belum menjalankan source terbaru.
- `/presets` live menunjukkan seluruh slot kosong setelah migrasi versi; Scene 1
  masih merujuk preset 1-6. Ini menjelaskan PLAY ditolak sebelum preset direkam.
- `/splay?s=1` memberi HTTP 409 ketika preset rujukan kosong dan `on` setelah
  preset 1 direkam, sesuai validasi.
- Source aktif dinaikkan ke `BUILD_TAG v20`.
- Ditambahkan wrapper `window.fetch` dengan antrean satu request aktif, drop polling
  saat antrean sibuk, dan `cache: no-store` untuk request API.
- Uji live membuat preset 1 sementara lalu menghapusnya kembali; Scene 1 dikembalikan
  ke referensi `[1,2,3,4,5,6]`. Preset harus direkam ulang oleh user.

### Verifikasi setelah upload
1. Serial: `=== DMX Web Console v20 ===`.
2. Header web: `v20`, bukan `10 PAR + 8 fx`.
3. Rekam ulang preset 1-6 sebelum PLAY Scene 1.

---

## Session 16 - 2026-08-22 04:10

### Perubahan arsitektur timing: fade & chase menjadi PER-PRESET

Sebelumnya: fade & chase global (satu slider untuk semua). User ingin tiap preset
punya konfigurasi fade/hold sendiri sehingga scene playback punya timing bervariasi.

#### Model baru
- Tiap preset menyimpan: **fade** (crossfade-in saat dimuat) + **hold**
  (durasi tayang sebelum chase/scene melangkah ke preset berikutnya).
- Chunk preset: [0]=used, [1..512]=channel, [513]=fade/10ms, [514]=hold/20ms.
  `PRESET_CHUNK 513 -> 515`, `PRESET_VER 5 -> 6` (NVS lama direset).
- `applyPresetToWant(idx)` kini mengaktifkan fade/hold milik preset:
  `fadeMs=presetFadeMs(idx); chaseMs=hold; sceneMs=hold;`
  -> dipakai fadeTick (crossfade) dan chaseTick/sceneTick (interval langkah).
- Slider global Fade/Chase/Scene-Speed DIHAPUS dari panel Master & Scene
  (beserta listener, label fn, dan parsing `/ctrl?fade|chase|scenesp` serta `/chase?sp`).

#### UI & endpoint
- Panel Preset: slider **Fade** (0-2s) & **Hold** (0.1-5s).
  - input = update label; persist `/psetfade?n&f&h` hanya saat `change` (hemat flash).
  - Memuat preset (pad) = slider ikut nilai f/h preset tsb.
  - REKAM menyimpan nilai f/h slider ke preset (`/psave?...&f=&h=`).
- `/psetfade` baru: ubah fade/hold tanpa rekam ulang (mutex + savePresets).
- `presetsJson`/`exportJson` sertakan `"f"`/`"h"` (ms); `importJson` parse keduanya
  (default 600/1500 bila absen), tetap bounded per objek.

---

## Session 17 - 2026-08-22 13:35

### Perbaikan UX scene EDIT: langkah duplikat berurutan
- Laporan user: Scene 1 terisi [1,2,2,2,2,2,2,2] — ketukan ganda membuat
  langkah redundan (feedback append datang belakangan).
- Server `/spush`: tolak push bila preset sama dgn langkah terakhir terisi
  (HTTP 409 "dup") — langkah identik berurutan redundan secara fungsi
  (hold per-preset; nilai sama = tanpa transisi).
- JS onPad (sceneEdit): optimistic UI (langkah langsung tampil di readout),
  cek duplikat di klien lebih dulu dgn pesan di `#sinfo`, reload dari server
  sebagai koreksi.

---
## Session 22 - 2026-08-22 17:50

### Fail-proof hardening (STATE, SELECT, SAVE, HEALTH) - BUILD_TAG v22
- Server seleksi authoritative: selectedPreset/selectedScene/stateRevision/nvsDirty.
- Endpoint baru: /select, /save (POST), /health.
- Import: markStateChanged supaya rekap UI sinkron setelah import.
- UI: api() wrapper (antrean HTTP + JSON error), state.id active Authoritative,
  Save Data button + status, .pad.playing (selain .pad.sel),
  panel grid desktop (max 1440px, scene & preset panel terpisah, mobile order).
- Protokol uji: upload v22 -> Serial "=== DMX Web Console v22 ===" -> buka web,
  verifikasi selected state + Save Data + Health (jika ada UI diag).

## Session 23 - 2026-08-22 20:10

### Bugfix laporan user (3 bug) - BUILD_TAG v22 -> v23

1. renderSceneBank is not defined (popup saat KOSONGKAN Scene 1)
   - Akar: fungsi dipanggil 5x (klik pad, syncFromServer, reloadScenes, onSPlay)
     tetapi DEFINISINYA HILANG sejak refactor buildSceneBank.
   - Fix: tambah renderSceneBank() berbasis class-toggle tanpa rebuild DOM
     (empty/sel/playing mengikuti SCN, selScene, sceneOn, serverScene).

2. Scene/Preset terpilih tidak berubah warna
   - Akar scene: exception renderSceneBank memutus handler klik SEBELUM class
     sel dipasang dan sebelum sinfo diperbarui.
   - Akar preset: style .pad.sel hampir tak terlihat (bg #3a3321 vs dasar #262b33).
   - Fix CSS: .pad.sel = fill kuning accent penuh + teks gelap;
     .pad.playing = fill hijau penuh. Terlihat jelas di PC maupun HP.

3. Fade reset ke 600ms setelah pindah preset
   - Akar: /psetfade sukses di server (toast tampil), tetapi array lokal
     presetsData[] tidak diperbarui -> onPad membaca nilai basi saat kembali.
   - Fix: setelah /psetfade sukses, perbarui presetsData[selPreset].f/.h lokal.
   - Extra: fallback onPad kini typeof p.f==='number'&&p.f>0 agar nilai 0
     dari data rusak tidak diam-diam menjadi 600.

### Verifikasi
- grep: renderSceneBank def(1068) + 4 caller OK; BUILD_TAG v23(53).
- Audit daftar definisi fungsi vs pemanggil: tidak ada yang hilang lagi.

## Session 24 - 2026-08-22 21:00 - BUILD_TAG v23 -> v24

### Laporan: lampu tidak menyala + desinkron web
- Ternyata penyebab utama = salah setting DMX address di fixture (bukan kode).
- Permintaan tetap yang dieksekusi: saat halaman dibuka/refresh, UI otomatis
  memulihkan SELURUH state dari ESP32.
- Yang sudah ada sebelumnya: /cur (channel+master+playback+selection),
  /presets (metadata), scenes via token injeksi.
- Yang ditambahkan v24: setelah refreshPresets masuk, slider Fade/Hold
  dipulihkan dari preset terpilih (applyFadeOfSelected), dan dipanggil juga
  ketika seleksi preset berubah via polling. Sebelumnya dua slider ini
  tetap di default 600/1500 sampai user mengetuk pad manual.

### Verifikasi
- BUILD_TAG v24 (53); applyFadeOfSelected def(1155) + caller(979,1151).

## Session 25 - 2026-08-22 21:15 - BUILD_TAG v24 -> v25

### Fitur: EDIT MODE / SHOW MODE (pengganti tombol PLAY)
- Tombol PLAY dihapus dari panel Scene.
- Dua mode eksklusif: EDIT MODE (merah saat aktif) dan SHOW MODE (hijau saat aktif).
- EDIT MODE: klik pad scene = pilih utk diedit; TIDAK menghasilkan output apa pun
  (masuk mode otomatis stopAuto() -> chase & scene berhenti). Pop/Akhir dan
  KOSONGKAN hanya aktif di mode ini (disabled di luar nya).
- SHOW MODE (default): klik pad scene = LANGSUNG memutar rantai presetnya
  (pengganti PLAY, penting utk ketepatan ketukan terhadap nada lagu).
  Klik scene yang sedang diputar = STOP.
- Preset pad tetap live di SHOW MODE (muat preset instan), REKAM/HAPUS tetap
  berlaku untuk bank preset.
- applySceneBtn() aman tanpa elemen tombol PLAY; status playback kini tampil
  via #sinfo (polling).

### Fix tuntas bug Fade reset ke 600ms
- Setelah /psetfade sukses, UI kini melakukan reconciliasi otoritatif:
  refreshPresets() menarik ulang data dari server. Sebelumnya hanya patch lokal
  yang bisa basi bila ada race/perangkat lain.
- onPad fallback nilai f/h diperkuat (tolak 0/tipe salah).

---

## Session 26 - 2026-08-22 21:30 - BUILD_TAG v25 -> v26

### Tombol Cek/Play kembali di panel Scene
- Tombol "? Cek" (id btnSPlay) ditambahkan kembali di baris tombol mode,
  TANPA menghapus EDIT MODE / SHOW MODE.
- Fungsi: memutar scene TERPILIH (highlight kuning) utk dicek, berlaku di
  mode EDIT maupun SHOW. Saat sedang main, tombol berubah jadi STOP.
- Stop branch memakai stopAuto() agar request /splay?off=1 benar terkirim
  (bug urutan: sceneOn dinolkan sebelum stopAuto -> off tak terkirim; sudah
  diperbaiki dgn memanggil stopAuto() langsung).
- applySceneBtn() tetap aman (guard null) dan kembali memperbarui label tombol.

## Session 27 - 2026-08-22 23:40 - BUILD_TAG v26 -> v27

### Fix 1: Fade tidak tersimpan (asimetri dgn Hold)
- Jalur simpan Fade & Hold disatukan: satu fungsi persistTiming() dipakai
  kedua slider (persis pola Hold yang terbukti).
- Akar persepsi "tidak tersimpan": fallback tampilan p.f||600 MENUTUPI nilai
  fade 0 / kecil. Sekarang p.f>=0 ditampilkan apa adanya (0 = snap, sah).
- Reconcile refreshPresets() setelah simpan tetap ada.

### Fix 2: Scene tahan hapus preset (snapshot-semantics ringan)
- Masalah user: hapus preset 2 -> scene 1-2-3 tinggal main 1 & 3.
- Solusi hemat memori (tanpa salinan 300KB): HAPUS PRESET = SEMBUNYIKAN.
  /pclear kini hanya men-set used=0; data channel [1..512] + f/h TETAP UTUH.
- sceneTick & /splay: langkah playable selama nomor preset valid (1..16),
  flag used tidak dipersyaratkan. Jadi preset tersembunyi tetap dimainkan
  scene selamanya, sampai slot itu di-REKAM ulang (konten baru sengaja).
- Bank UI tetap menampilkan hatch kosong utk preset tersembunyi (sinyal visual).

### Fix 3: Export selalu sertakan channel
- exportJson: array c diekspor utk SEMUA preset (termasuk used=0) supaya
  import antar device tidak merusak scene; reserve 36000 -> 42000.

---

## Session 28 - 2026-08-23 14:40 - BUILD_TAG v27 -> v28 (NVS compact + Load Data)

### Akar masalah "Save Data lalu reboot balik default" (TERKONFIRMASI)
- Serial user: `E (1470) phy_init: store_cal_data_to_nvs_handle: store
  calibration data failed(0x1105)` = ESP_ERR_NVS_NOT_ENOUGH_SPACE.
- Partisi NVS (~20KB) PENUH/terfragmentasi: format lama menulis blob utuh
  presets 16x515 = 8.240 byte tiap simpan + WiFi cal ikut gagal ditulis.
- Jadi Save Data tidak pernah benar-benar persisten di v27.

### Fix: Format penyimpanan kompak (storage rewrite)
- Hapus loadPresets/savePresets/loadScenes/saveScenes/saveData + kedua
  snapshot besar (savePresetsSnapshot/saveScenesSnapshot).
- Baru: PATCH_CH_TOTAL=176 (channel tertinggi patch FOG2=174+margin),
  COMPACT_CHUNK=179 byte/preset ([0]=used [1]=fade/10ms [2]=hold/20ms
  [3..178]=CH1..176). Total pc=2.864 B, sc=600 B (~3,5KB vs ~8,8KB lama).
- persistAll(): snapshot di bawah dmxMutex, tulis NVS di LUAR mutex.
  Kunci baru: sver2=STORAGE_VER(7), pc, sc, selP, selS.
- loadAll(): bila sver2 != 7 -> nvs.clear() sekali untuk RECLAIM ruang NVS
  yang penuh/terfragmentasi (menghapus key lama ver/presets/sver/scenes),
  mulai bersih dari default. Serial: "NVS: format lama terdeteksi".
- loadData(): baca-balikkan NVS -> RAM tanpa clear (untuk tombol Load Data).

### Fitur baru: tombol Load Data
- Endpoint GET /loaddata -> onLoadData() -> loadData(); 404 no_saved_data
  bila belum ada data valid. Rute didaftarkan setelah /save.
- UI: tombol #btnLoadData "Load Data" di actions row sebelah Save Data;
  confirm() dulu; sukses -> refreshPresets()+reloadScenes().
- Semua penulis state kini lewat persistAll() (satu sumber kebenaran):
  capturePreset, importJson, onSPush/Pop/Clear, onPresetFade, onPresetClear.

### Bersih-bersih
- Konstanta tak terpakai dihapus: UI_STATE_VER, SCENE_VER.
- PRESET_VER tetap ada (versi format file export JSON saja).

### Verifikasi statis
- grep: 0 sisa savePresets/saveScenes/loadPresets()/loadScenes()/saveData();
  persistAll x8 call site; loadAll di setup; /loaddata + btnLoadData OK;
  refreshPresets/reloadScenes terdefinisi; BUILD_TAG v28 konsisten
  (boot banner, header web __BUILD__, /health).

### Protokol uji (user)
1. Salin examples/dmx_web_rgb.ino -> Documents/Arduino/DMX512/DMX512.ino,
   upload. Serial HARUS tampil `=== DMX Web Console v28 ===`.
2. Boot pertama v28: harap baris "NVS: format lama terdeteksi -> clear()"
   dan error phy_init 0x1105 HARUS hilang pada boot-boot berikutnya.
3. Rekam ulang preset (format berubah, data lama dibuang) -> Save Data ->
   cabut daya -> nyalakan -> preset harus tetap ada.
4. Uji tombol Load Data: ubah sesuatu tanpa Save -> klik Load Data ->
   kembali ke kondisi tersimpan terakhir.
5. Opsional (sekali saja, jika phy_init masih muncul): Tools > Erase Flash,
   lalu upload ulang & konfigurasi awal.

## Session 29 - 2026-08-23 16:25 - Verifikasi Import/Export pasca-v28 (tanpa perubahan kode)

- Permintaan lama user (import/export preset) SUDAH terimplementasi dan
  kompatibel dgn format storage kompak v28:
  * Tombol "Ekspor"/"Impor" di baris bank preset (#btnExport/#btnImport,
    input file tersembunyi #fileIn, accept .json).
  * GET /export -> onExport() -> exportJson(): 16 slot lengkap
    {u=used,f=fade,h=hold,c[512]}, channel diekspor utk SEMUA preset
    (termasuk used=0) agar scene antar-device tetap utuh; reserve 42000.
  * POST /import (multipart, maks 64KB) -> importJson(): parse manual
    tanpa alloc String per token; commit hanya slot yg ada di file;
    persisten otomatis lewat persistAll() (sudah diganti dr savePresets
    saat refactor v28) -> tidak perlu tekan Save Data setelah Impor.
- Catatan: ini alur file via browser (komputer/HP). Simpan show langsung
  di flash ESP32 (slot show LittleFS/SD) BELUM ada - opsional bila diminta.

## Session 30 - 2026-08-24 15:35 - BUILD_TAG v28 -> v29 (WebSocket realtime push)

### Fitur: WebSocket push menggantikan polling /cur
- Library: ESPAsyncWebServer + AsyncTCP (SUDAH terpasang di
  Documents\Arduino\libraries - tanpa dependensi baru).
- Arsitektur minimalis & aman:
  * WebServer blocking port 80 + SEMUA handler REST TIDAK DIUBAH.
  * Server Async kedua khusus WS: wsSrv(81), path /ws, event handler
    onWsEvent (log connect/disconnect client).
- onCur() direfaktor -> buildStateJson(); /cur tetap ada sebagai fallback.
- wsBroadcastTick() di loop(): broadcast segera saat stateRevision berubah,
  heartbeat 1000ms saat diam; skip bila tidak ada client (hemat CPU);
  ws.cleanupClients() tiap iterasi.
- JS: startWs() ke ws://host:81/ws, onmessage -> syncFromServer(j,true);
  retry backoff maks 5x; polling lama dipertahankan sebagai fallback dan
  hanya jalan saat !wsOk.

### Manfaat
- Latensi update UI ~1s -> <10ms saat state berubah; beban HTTP Core 1
  turun drastis; alokasi String per-detik hilang (kurang fragmentasi heap).

### Verifikasi statis
- grep: include v29, wsSrv addHandler+begin, onEvent(onWsEvent),
  buildStateJson x2 pemakai, textAll, cleanupClients, startWs/fallback OK;
  BUILD_TAG v29 konsisten di banner/header/health.

### Protokol uji (user)
1. Upload sketch v29. Serial harus "=== DMX Web Console v29 ===" +
   "WebSocket push -> port 81 (/ws)".
2. Buka UI, hard-refresh (Ctrl+F5). Serial muncul "WS: client N tersambung".
3. Geser fader/preset di satu device -> perubahan tampil instan di device
   kedua tanpa jeda 1 detik (bukti push aktif).
4. Blokir port 81 (atau matikan WS) -> UI otomatis fallback polling,
   indikator status tetap hidup.

## Session 31 - 2026-08-25 01:20 - BUILD_TAG v29 -> v30 (Strobe Master)

### Fitur: Strobe Master global (permintaan user)
- Slider "Strobe" di bawah Master (id mstrb, 0-255).
- Semantik: 0 = nonaktif; >0 = SELURUH output di-gate kotak on/off,
  half-period dipetakan v=1 -> 2000ms s/d v=255 -> 40ms (besar=cepat).
- Implementasi di buildFrame() (Core0, 40fps): fase kotak strobePhase +
  strobeNextAt; saat fase OFF seluruh frame di-nol-kan (semua fixture ikut,
  termasuk tipe tanpa dimmer). Scene/chase/fader TIDAK berubah datanya -
  efek hanya sesaat pada tampilan. Re-aktif selalu mulai dari fase ON.
- API: /ctrl?strb=N (ephemeral, tidak menandai nvsDirty/tidak persisten).
- State JSON (+WS push): field "strb" -> posisi slider tersinkron antar
  device secara realtime; skipActive menghormati slider yang digeser.

### Verifikasi statis
- grep: globals 133-134; gate buildFrame 349-358; onCtrl strb 1402;
  JSON strb 1440; UI label 844; handler 1101-1103; sync 1304;
  BUILD_TAG v30 konsisten (54/629/1367/1547).

### Uji (user)
1. Upload v30, hard refresh UI. Slider Strobe di bawah Master.
2. Mainkan scene/chase lalu naikkan Strobe -> semua lampu kedip;
   turunkan ke 0 -> kembali normal persis seperti sebelum strobe.
3. Nilai kecil = lambat, besar = cepat. Buka 2 device -> posisi strobe sinkron.

## Session 34 - 2026-08-25 21:35 - Bugfix v33 -> v34 (mixer LTP-only)

### Bug yang diperbaiki: Preset menolak fader ke 0
Setelah load preset (misal PAR1 = merah, channel 1 = 255), geser fader manual ke 0 tidak mengubah output � lampu tetap 255. UI ikut memantul kembali ke 255 saat slider dilepas karena server mengembalikan nilai tersebut.

### Akar penyebab
`recomputeWant()` memakai HTP (`max`) untuk semua channel level/intensitas:
```cpp
want[ch] = max(manualWant[ch], pbWant[ch]);
// max(0, 255) = 255   ? MANUAL KALAH PADANYA PRESET
```
Karena `chIsHTP` mengklasifikasi PAR (dimmer+RGB) sebagai **level**, maka `max()` diterapkan. Hasilnya manual hanya bisa menambah terang, tidak bisa meredupkan/mematikan apa pun yang sedang dimainkan playback.

### Perbaikan
Ubah cabang HTP jadi **LTP berdasarkan timestamp** per-channel. Semua saluran sekarang menggunakan aturan tunggal: siapa yang disentuh terakhir menang. Dengan ini:
- Fader manual = 0 ? lamp padam walau preset masih 255 (manual lebih baru). ?
- Load preset/scene baru setelah pegang fader ? preset yang menang (playback lebih baru). ?
- Tidak ada lagi perilaku "menolak" atau memantul balik.

### Perubahan kode
- `recomputeWant()`: hapus klasifikasi `chIsHTP`, langsung gunakan `if (manualTouched > pbTouched)` untuk semua channel.
- Hapus fungsi `chIsHTP()` yang kini mati (dead code, memicu warning compiler).
- BUILD_TAG naik ke **v34**.

### Protokol uji
Upload v34, buka web:
1. Play preset merah (PAR1 R=255).
2. Geser slider PAR Red ke 0 � harus mati.
3. Mainkan scene/preset lain � posisi fader kembali mengikuti kondisi playback.
4. Geser lagi, lalu refresh browser � status sinkron tanpa pemantulan.

## Session 35 - 2026-08-25 23:45 - Firmware v34 -> v35 (paritas serial)

### Tujuan
Perluas protokol serial supaya USB kendali penuh � **paritas 1:1 dengan Web UI**, tidak ada fitur yang hilang saat operator pakai laptop lewat kabel. Desktop app nanti bisa jalankan semua fungsi dari Windows tanpa bergantung pada web browser/WiFi venue.

### Perubahan protokol (semua satu-baris teks di Serial 115200)
| Perintah | Deskripsi | Paritis HTTP |
|---|---|---|
| `LISTP` | Balas JSON daftar preset (`presetsJson()`) | `/presets` |
| `LISTS` | Balas JSON daftar scene (`scnJson()`) | `/scenes` |
| `GRP i v` | Fader grup ? manualWant + snap out[] | onGroup() |
| `REC n idim f h` | Rekam preset n (fade f/hold h dalam ms) | onPresetSave() |
| `PFH n f h` | Ubah fade/hold per-preset saja | onPresetFade() |
| `PDEL n` | Sembunyikan preset (used=0) | onPresetClear() |
| `SPUSH s p` | Tambah step ke scene s dengan preset p | onSPush() |
| `SPOP s` | Hapus langkah terakhir scene s | onSPop() |
| `SCLR s` | Kosongkan seluruh scene s | onSClear() |
| `SELP n` / `SELS s` | Select preset/scene (tanpa apply) | onSelect() |
| `CHASE on/off` | Aktifkan/hentikan chase | onChase() |
| `LOAD` | Muat ulang snapshot NVS ke RAM | loadData() |

Catatan kunci:
- Semua perintah menulis layer `manualWant[]` atau `pbWant[]`, lalu `recomputeWant()` ? **mixer HTP/LTP** menentukan siapa menang.
- Snap `out[ch]=want[ch]` ditambahkan untuk SET & GRP agar fader terasa langsung (tanpa fade).
- Tidak ubah mesin DMX/mixer/format preset/NVS. Hanya adapter teks ? handler yang sudah ada.

### Protokol uji (Serial Monitor 115200)
```
GET                    # balas state JSON lengkap
LISTP                  # list 16 preset (JSON array)
LISTS                  # list 20 scene (JSON array)
SET 0_1=255            # PAR1 Red penuh
MAST 200               # master dimmer 200
STRB 128               # strobe sedang
GRF 2 255              # grup fader 2 = 255 (terapkan ke semua fixture tipe filter)
PSL 3                  # mainkan preset 3
PSEL 5                 # pilih preset 5 (tanpa apply)
REC 5 0 500 1500       # rekam preset 5 (dimmer ignored, fade 0.5s, hold 1.5s)
PFH 5 500 1500         # update fade/hold preset 5 saja
PDEL 5                 # sembunyikan preset 5
SPUSH 1 3              # tambah langkah 3 ke scene 1
SPOP 1                 # hapus langkah terakhir scene 1
SCLR 1                 # kosongkan scene 1
SPLAY 2                # mainkan scene 2
SSTOP                  # stop scene
CHASE on               # mulai chase
CHASE off              # hentikan chase
SAVE                   # paksa simpan ke NVS
LOAD                   # muat ulang dari NVS (restore)
ALLOFF                 # hitam semua (master 0 + all off)
```

### Verifikasi statis
- All ops detected: LISTS, LISTP, GRP, REC, PFH, PDEL, SPUSH, SPOP, SCLR, SELP, SELS, CHASE, LOAD.
- `serArgInt()` helper added; BUILD_TAG v35.
- No structural change to mixer/DMX/NVS.

Upload v35 dan uji command-by-command via Serial Monitor sebelum lanjut ke desktop .exe.

## Session 36 - 2026-08-25 23:55 - Firmware v35 -> v36 (LISTF/LISTG metadata)

### Tujuan
Tambahkan perintah serial untuk memuat daftar fixture dan grup fader secara dinamis dari ESP32 � tidak ada lagi hardcoding fixture/grup di aplikasi desktop. Ini memastikan sinkronisasi penuh dengan patch yang aktif.

### Perintah baru
| Perintah | Deskripsi | JSON format |
|---|---|---|
| `LISTF` | Balas semua fixture yang terpatch (N_FIX=18) | `[{"name":"PAR 1","type":0,"start":1,"foot":9},...]` |
| `LISTG` | Balas semua grup fader (N_GROUPS=8) | `[{"name":"PAR Dim","type":0,"offset":0},...]` |

Data ini dipanggil sekali saat koneksi dibuka ? disimpan lokal di desktop ? digunakan untuk render mixer tab + group faders. Tidak ubah mekanisme DMX/mixer/layer apa pun.

### Verifikasi
- LISTF returns fixJson(): N_FIX items per fixture index.
- LISTG returns grpJson(): N_GROUPS items per group definition.
- Desktop akan memanggil GET, LISTF, LISTG setelah connect, lalu meng-cache seluruh state.

Upload v36 dan uji `LISTF` + `LISTG` via Serial Monitor sebelum build desktop .exe.

## Session 37 - 2026-08-26 00:05 - Firmware v36 -> v37 (EXPORT parity)

### Tujuan
Paritas dengan Web UI `/export` � export data preset lengkap (semua 512 channel per preset) ke serial untuk backup/transfer antar device via desktop .exe. Karena format sama dengan JSON web, desktop bisa parse dan save ke disk (`presets.json`). Note: response ~42KB ? sekitar 3-4 detik di 115200 baud. OK untuk MVP v1.

### Perintah baru
| Perintah | Deskripsi | Response |
|---|---|---|
| `EXPORT` | Balas full export JSON (`exportJson()`) | `{"app":"DMX-RGB","ver":PRESET_VER,"presets":[...]}` |

Desktop will call this once on demand (Export button) and save to a `.json` file. Import over serial is deferred to phase 4 (HTTP) due to upload complexity.

Upload v37 dan uji: ketik `EXPORT`, lalu copy output besar ke Notepad dan simpan sebagai `dmx-export.json`.

## Session 38 - 2026-08-26 01:05 - Firmware v38 + Desktop Controller v1 (PySide6)

### Firmware v38
- FIX unit REC/PFH: kini menerima milidetik persis seperti web
  (sebelumnya argumen dikali 10/20 -> hasil salah/clamped).
- Perintah baru `ALL on/off` (paritas tombol Blackout & PAR Full web):
  off = nol-kan semua manualWant; on = 255 hanya utk PAR (aman perangkat).
- BUILD_TAG v38.

### Desktop Controller v1 (folder desktop/)
Aplikasi Windows (PySide6) kontroler penuh via USB serial, sinkron dua arah
dengan Web UI lewat protokol paritas v35-v38:
- transport.py  : SerialTransport (reader thread + request/response ber-lock),
                  auto-detect COM via VID (CH340/CP210x/FTDI/Espressif).
- worker.py     : SerialWorker di QThread; proses antrean perintah lalu
                  polling GET 250ms -> sinkron realtime dua arah.
- state.py      : DeviceState (fixtures/groups/presets/scenes/live) +
                  channel_labels() mirror dari labelOf() web.
- ui/widgets.py : VFader (slider vertikal console-style) + PadButton.
- ui/mixer_tab  : Master, Strobe, Blackout, PAR Full, Chase, 8 fader grup,
                  fader per-channel semua fixture (dibangun dari LISTF/LISTG).
- ui/presets_tab: 16 pad (warna preview dari LISTP), klik=SELP+PSL,
                  REC (idim+fade+hold), PDEL, PFH update timing.
- ui/scenes_tab : 20 pad, PLAY/STOP, editor langkah (SPUSH/SPOP/SCLR),
                  indikator langkah yang sedang main.
- ui/system_tab : SAVE/LOAD NVS, EXPORT ke file .json, status + log.
- main.py       : MainWindow, wiring sinyal, shortcut (Space=ALL off,
                  Esc=SSTOP), refresh LISTP/LISTS otomatis setelah perintah
                  yang mengubah data.
- requirements.txt (PySide6, pyserial), build.bat (PyInstaller onefile),
  README.md (cara pakai, build, tabel protokol lengkap).
- Proteksi "active keys": slider yang sedang digeser tidak ditimpa sync.
- Verifikasi: python -m py_compile semua file -> SYNTAX OK.

### Uji (user)
1. Upload firmware v38 ke ESP32.
2. PC: `pip install -r desktop/requirements.txt` lalu
   `python desktop/main.py` -> pilih COM -> SAMBUNG.
3. Geser fader di .exe dan di web bergantian -> keduanya sinkron.
4. REKAM preset dari .exe, mainkan scene, SAVE DATA, cabut-colok USB ->
   sambung lagi, data tetap ada.
5. Build exe: jalankan desktop/build.bat -> dist/DMX512Controller.exe.

## Session 39 - 2026-08-26 00:45 - Firmware v39 + Desktop: Import serial, EDIT/SHOW mode, ? Cek

### Latar belakang
User menemukan 3 gap paritas vs Web UI: (1) import JSON via serial belum ada,
(2) scene tidak punya EDIT/SHOW mode (paling penting utk keselamatan panggung),
(3) tidak ada tombol ? Cek utk tes scene.

### Firmware v39
- IMPORT batch via serial (paritas /import web, semantik sama persis):
  * IMPORT_BEGIN  -> nol-kan staging
  * IMPORT_P n u f_ms h_ms -> metadata preset n
  * IMPORT_C n off v1,v2,... -> chunk maks 64 channel per baris
  * IMPORT_END    -> commit HANYA baris yg dikirim (sisanya tidak disentuh),
                     persistAll, markStateChanged
- Staging: serImport[16][515] + serImportProvided[16] di RAM (~8.3KB, aman).
- Buffer baris serial dinaikkan 256 -> 384 char (baris IMPORT_C ~280 char).
- BUILD_TAG v39.

### Desktop
- scenes_tab: toggle EDIT MODE / SHOW MODE (default SHOW, paritas web):
  * EDIT: klik scene = SELS saja tanpa output; masuk EDIT otomatis
    SSTOP + CHASE off (safety, paritas stopAuto); editor langkah enabled.
  * SHOW: klik scene = SPLAY; klik scene yg sedang main = SSTOP.
  * ? Cek: play/stop scene terpilih utk tes, berlaku di KEDUA mode.
- presets_tab: set_scene_edit(on) � saat EDIT MODE aktif, klik pad preset
  mengirim SPUSH <scene> <preset> (tambah langkah), bukan SELP+PSL.
- system_tab: tombol IMPORT preset dari file -> main._do_import():
  parse JSON lokal -> antre IMPORT_BEGIN + IMPORT_P + 8x IMPORT_C per preset
  + IMPORT_END + refresh LISTP (~146 perintah, �5 detik).
- main.py: wiring edit_mode_changed -> set_scene_edit; import_requested ->
  _do_import; QSS editBtn/showBtn.
- Verifikasi: py_compile semua file desktop OK; grep firmware OK.

### Uji (user)
1. Upload firmware v39.
2. Serial Monitor: IMPORT_BEGIN -> IMPORT_P 1 1 600 1500 ->
   IMPORT_C 1 0 255,0,0,0,... -> IMPORT_END -> cek preset 1 terisi.
3. Desktop: buka tab Scene -> EDIT MODE -> klik scene (tidak ada output!) ->
   klik pad preset utk menambah langkah -> ? Cek utk tes -> SHOW MODE utk live.
4. Round-trip: EXPORT dari device A -> simpan file -> IMPORT ke device B.

## Session 40 - 2026-08-26 02:30 - Packaging DMX512Controller.exe (PySide6 v1)

### Lingkungan Python
- Python 3.9.0 + PySide6 6.10.3 + pyserial 3.5 + PyInstaller 6.22.2

### Hasil packaging
- **dist/DMX512Controller.exe** (22.2 MB, single-file Windows executable)
- Build menggunakan `--onefile --windowed --name DMX512Controller main.py`
- Bundle mencakup Qt 6.x + Shiboken runtime (Qt framework embedding via PySide6)

### Cara pakai (.exe jadi)
1. Colok ESP32 ke USB ? buka `DMX512Controller.exe`
2. Pilih COM port (terdeteksi otomatis dengan VID CH340/CP210x/FTDI/ESP32) ? **SAMBUNG**
3. Gunakan tab Mixer/Preset/Scene/Sistem seperti deskripsi di README.md desktop
4. Shortcut: Space = BLACKOUT, Esc = Stop Scene

### Catatan teknis
- DLL Shiboken/Qt ter-embed dalam EXE ? tidak perlu install Python terpisah
- Ukuran 22 MB karena Qt framework full bundle; dapat dioptimalkan lebih kecil dengan opsi strip/hide-but bukan prioritas MVP
- .gitignore sudah exclude dist/, build/, __pycache__
- Reprodusibilitas build tersedia via `desktop/DMX512Controller.spec` (commit terakhir)

### Status akhir
Firmware v39 + Desktop App + EXE sudah complete ? repo GitHub https://github.com/AlephSc/dmx512 siap dipakai.

## Session 41 - 2026-08-26 12:00 - Firmware v40+v41 & Desktop transport WiFi/Ethernet

### Permintaan user
Kontrol jarak jauh (2-3m+) lewat router tanpa bergantung WiFi: modul RJ45 W5500
di ESP32, laptop tetap bisa kontrol semua perangkat. Jarak dekat tetap USB+desktop.

### Firmware v40 (endpoint metadata HTTP)
- GET /fixes  -> fixJson()  (paritas LISTF serial)
- GET /groups -> grpJson()  (paritas LISTG serial)
- Dibutuhkan desktop transport HTTP agar bisa render mixer tanpa hardcode.

### Firmware v41 (Ethernet W5500)
- #include <ETH.h> (dukungan bawaan core ESP32 3.3.7, tanpa library tambahan).
- Pin W5500: CS=GPIO5, RST/IRQ=-1; SPI VSPI: SCLK=18, MISO=19, MOSI=23.
  (Tidak bentrok dgn DMX: GPIO17/16/4.)
- setup(): SPI.begin + ETH.begin(ETH_PHY_W5500,...) setelah WiFi; tunggu link
  5 detik + DHCP 3 detik; laporkan IP Ethernet di Serial.
- activeIP() kini prioritas: Ethernet > WiFi STA > AP.
- Fallback AP darurat HANYA bila WiFi gagal DAN Ethernet tidak link-up.
- WebServer(port 80) & AsyncWebSocket(port 81) otomatis melayani semua
  interface (bind 0.0.0.0) -> tidak ada perubahan handler.

### Desktop
- transport.py: HttpTransport � interface identik dgn SerialTransport;
  menerjemahkan perintah serial-style ke endpoint REST; IMPORT via multipart
  POST /import (lebih cepat dari batch serial); urllib stdlib (tanpa deps baru).
- main.py: selector "USB Serial / WiFi (HTTP)" di toolbar + input IP;
  _toggle_conn cabang per transport; _do_import via HTTP saat WiFi aktif.
- Verifikasi: py_compile semua file desktop OK.

### Arsitektur final (sesuai permintaan user)
- Jarak dekat : USB -> desktop app (SerialTransport) � latensi terendah.
- Jarak jauh  : W5500 -> router -> laptop via WiFi/HTTP+WS (HttpTransport).
- Desktop TIDAK perlu tahu transport mana: cukup masukkan IP ESP32 (Ethernet
  dari DHCP router). Web UI browser juga tetap bisa dipakai bersamaan.

### Uji (user)
1. Pasang W5500: VCC->3.3V, GND->GND, SCLK->18, MISO->19, MOSI->23, CS->5.
2. Colok kabel LAN W5500 -> router; upload firmware v41.
3. Serial Monitor harus menampilkan "Ethernet tersambung. IP: http://x.x.x.x".
4. Buka IP tsb di browser (Web UI) ATAU desktop app mode WiFi (HTTP).
5. Cabut kabel LAN -> device kembali via WiFi SIGMA; tanpa keduanya -> AP darurat.

## Session 42 - 2026-08-26 14:30 - Desktop: MIDI controller support

### Tujuan
Fitur pembeda untuk produk jual: fader/knob/pad fisik dari controller MIDI
(nanoKONTROL2, APC Mini, Launchpad, dll.) langsung mengendalikan rig DMX.

### Komponen baru
- midi_handler.py:
  * MidiMapper � mapping JSON (midi_map.json): CC & Note -> aksi
    (master/strb/group/chan/preset/scene_play/scene_stop/blackout/
    all_full/chase_on/chase_off). Default ramah controller umum:
    CC 0-7 = grup fader, CC 16/17 = master/strobe, Note 36-51 = preset 1-16,
    Note 64-67 = blackout/full/chase, Note 70-89 = scene 1-20, Note 90 = stop.
  * MidiInputWorker � QObject di QThread; baca port via mido, translate,
    throttle CC 30ms (hindari banjir), mode MIDI-learn, deteksi device dicabut.
  * Skala MIDI 0-127 -> DMX 0-255.
- ui/midi_tab.py: tab MIDI � pilih device, status & aktivitas realtime,
  tabel mapping (tambah/hapus/simpan/default), MIDI-learn (pilih aksi ->
  klik LEARN -> gerakkan kontrol -> otomatis terpetakan & tersimpan).
- main.py: tab MIDI + handler lengkap; perintah MIDI mengalir ke transport
  AKTIF (USB Serial maupun WiFi/HTTP) -> MIDI bekerja di kedua jalur.
- requirements.txt: + mido>=1.3.0, python-rtmidi>=1.5.0.
- Verifikasi: py_compile semua file OK; mido 1.3.3 + python-rtmidi 1.5.8
  terpasang di mesin dev.

### Catatan build
- PyInstaller perlu --hidden-import=mido.backends.rtmidi (backend dimuat dinamis).

### Uji (user)
1. Colok controller MIDI USB -> buka DMX512Controller.exe -> tab MIDI ->
   Cari Device -> SAMBUNG MIDI.
2. Geser fader controller (default CC 0-7) -> grup fader rig ikut bergerak.
3. Tekan pad (Note 36-51) -> preset 1-16 dimainkan.
4. MIDI-learn: pilih aksi "Master dimmer", klik LEARN, gerakkan knob ->
   mapping tersimpan otomatis ke midi_map.json.
5. MIDI berfungsi sama baik saat transport aktif USB Serial maupun WiFi.

---

## Session 43 — Fix fatal: exe gagal start "DLL load failed while importing Shiboken"

### Akar masalah (terbukti, bukan tebakan)
- PySide6 6.10.3 dipasang di mesin build; shiboken6-nya mengimpor simbol
  stable-ABI `PyCMethod_New` dari `python3.dll`.
- Python 3.9.0 yang terpasang TIDAK mengekspor simbol itu dari python3.dll
  (bug CPython: rilis awal 3.9 tidak mem-forward semua fungsi stable ABI).
- Verifikasi: parse tabel import PE shiboken6.abi3.dll -> 185 simbol dari
  python3.dll -> 1 hilang: `PyCMethod_New` -> WinError 127.
- Bukan antivirus, bukan mode onefile, bukan versi Windows.
- Uji "berhasil" sebelumnya false positive: cek PID saja tidak membedakan
  GUI vs dialog error (proses tetap hidup).

### Perbaikan
- Pin `PySide6==6.6.3.1` di requirements.txt (versi terakhir resmi dukung
  py3.9; shiboken-nya tidak mereferensi PyCMethod_New).
- Entry point baru `run.py`: crash logger ke
  `%LOCALAPPDATA%\DMX512Controller\crash.log` supaya kegagalan startup
  berikutnya punya jejak tertulis.
- Build diganti ONEDIR (`DMX512Controller.spec` + build.bat):
  - tanpa ekstraksi DLL ke %TEMP% saat start -> tidak rawan blokir AV;
  - upx=False (UPX pada DLL Qt pemicu error serupa);
  - vcruntime140/140_1/msvcp140 modern dibundel di folder exe.
- Uji tervalidasi benar: jendela `DMX512 Controller — ESP32` muncul,
  diverifikasi via EnumWindows Win32 (bukan PID), crash.log nihil.

### Distribusi
- Hasil build kini FOLDER: `desktop/dist/DMX512Controller/`
  (salin seluruh folder ke PC target, jalankan DMX512Controller.exe).

### Catatan ke depan
- Kalau build machine naik ke Python >= 3.10, pin PySide6 boleh dilepas
  (hapus pin di requirements.txt) untuk dapat Qt terbaru.

---

## Session 44 — Fix 3 kelompok error kompilasi firmware (uji compile pertama user)

### Error dan perbaikan
1. **Orphan block baris 800-809**: duplikat isi `grpJson()` tanpa kepala fungsi
   (sisa edit v41 yang tidak bersih) -> dihapus. `fixJson()` dipakai di baris 799
   tapi didefinisikan di baris 1413 -> ditambahkan forward declaration eksplisit
   (tidak boleh bergantung pada auto-prototype .ino).
2. **serArgInt()**: `String s=args.trim()` pada `const String&` (trim() void &
   non-const) -> salinan dulu lalu trim; rantai `t.trim().length()` -> pisahkan.
3. **ETH.begin() salah tanda tangan**: API core esp32 3.3.7 untuk SPI PHY =
   `begin(tipe, phy_addr, cs, irq, rst, SPI)` -> diubah ke
   `ETH.begin(ETH_PHY_W5500, ETH_PHY_ADDR_AUTO, ETH_CS, ETH_IRQ, ETH_RST, SPI)`.
   Verifikasi dari source core: `ETH_SPI_SUPPORTS_NO_IRQ=1` sehingga IRQ=-1 sah
   (tanpa kabel INT), dan `ETH_PHY_ADDR_AUTO` didukung (deteksi PHY otomatis).

### Audit tambahan
- Scan seluruh file: tidak ada method-void-in-chain lain; `importJson(const
  String&)` hanya membaca; `scnJson()` terdefinisi sebelum pemakainya.
- BUILD_TAG v41 -> **v42**.

### Open source
- Ditambahkan `LICENSE` (MIT, copyright AlephSc) sesuai permintaan user.

---

## Session 45 — Audit keandalan besar: hapus delay, fix tombol macet, WiFi kustom, kapasitas 30/50

### Akar masalah yang ditemukan (audit kode aktual, bukan tebakan)
1. **Delay fader desktop**: worker MENUNGU balasan (timeout 2.5 dtk) untuk
   SETIAP perintah termasuk SET/MAST/STRB; serial GET 250ms payload ~2 KB
   (~170 ms @115200) memenuhi link; HTTP tanpa keep-alive (handshake baru
   tiap request, +30-80 ms di WiFi).
2. **Tombol preset/scene "tidak bisa diklik"**: `presets_tab._update_info_label`
   memanggil `self.info_label` padahal atributnya `info_lbl` -> AttributeError
   tiap perubahan seleksi. Karena `_on_state` memanggil tab berurutan tanpa
   proteksi, crash ini MEMBUNUH apply_state Scene & System di bawahnya.
3. **Bug JS tersembunyi**: `N_PRESETS` dipakai di `syncSelectedState` tapi
   tidak pernah didefinisikan -> ReferenceError membunuh sinkronisasi begitu
   ada preset terpilih.
4. Kapasitas NVS: satu value NVS maks ~4000 byte; 30 preset kompak = 5370 B
   -> key harus dipecah (pc0/pc1).

### Perbaikan firmware (BUILD_TAG v43)
- **Kontrol via WebSocket** (`wsHandleCtl`): pesan `{"t":"s|mast|strb|all"}`
  diaplikasikan langsung (mutex+snap); HTTP /set jadi fallback.
- **WiFi kustom**: kredensial di NVS namespace terpisah `dmxwifi`; endpoint
  `/wifistat` & `/wifiset`; reconnect non-blocking 6 percobaan di
  `wifiReconnectTick()`, gagal -> kembali ke bawaan + AP darurat; paritas
  serial `WIFIS`/`WIFIST`.
- **Kapasitas**: N_PRESETS 16->30, SCENE_STEPS 30->50; STORAGE_VER 7->8
  (preset lama direset sekali saat boot pertama v43 - rekam ulang).
- Storage NVS dipecah: `pc0`/`pc1` @15 preset (2685 B) + `sc` (1000 B).
- Injeksi `__NP__` ke JS (fix bug N_PRESETS).

### Perbaikan Web UI
- Slider channel/master/strobe + Blackout/Penuh/Off dikirim via WS (instan,
  tanpa antrean HTTP); fallback otomatis bila WS putus.
- Panel WiFi baru (status live + form SSID/sandi + panduan IP berubah).

### Perbaikan desktop
- Fix `info_lbl`; `_on_state` kini defensif per-tab (satu tab error tidak
  membunuh sinkronisasi global; error tercatat ke log Sistem).
- **Fire-and-forget** `FIRE_OPS` (SET/MAST/STRB/GRP/ALL/CHASE/SELP/SELS/SSTOP):
  `transport.send()` tanpa tunggu balasan; polling serial 0.25->0.4 dtk.
- HttpTransport keep-alive persisten (http.client) + `send()` thread latar.
- Validasi bentuk payload di `_on_data` (anti respons basi pasca fire-and-forget).
- Bank preset 30 pad (6 kolom), import cap 30, MIDI default Note 36-63 -> preset 1-28.
- Tab Sistem: panel WiFi kustom (WIFIS/WIFIST + polling status 15x).
- Tab MIDI dipoles: 3 section bernomor, riwayat aktivitas 8 event, tabel
  selang-seling, LEARN nonaktif sampai tersambung.
- Catatan: build .exe BELUM dijalankan ulang - jalankan `desktop\build.bat`
  setelah semua teruji bila ingin exe terbaru.

### Protokol uji
1. Upload firmware v43 -> Serial: `=== DMX Web Console v43 ===`; rekam ulang
   preset (migrasi storage) lalu Save Data.
2. Web: geser fader = realtime via WS (cek Serial tidak banjir request HTTP);
   panel WiFi: isi SSID/sandi -> status berubah hijau bila sukses.
3. Desktop (`python desktop\main.py`): klik pad preset/scene berfungsi,
   fader tanpa delay, tab Sistem bisa ubah WiFi ESP32.

---

## Session 46 — Fix 3 bug laporan user (ScenesTab str/int, WiFi tanpa status, __SCNDATA__)

### Bug 1: apply_state ScenesTab "'>' not supported between str and int"
- Data LISTS bisa tiba sebagai string (variasi versi firmware); `v > 0`
  membandingkan str dgn int -> crash berulang di log Sistem.
- Fix: `DeviceState.set_scenes()` menormalisasi scene jadi list-of-int
  (parse string JSON, paksa int, nilai tak dikenal = 0). `_on_data` LISTS
  kini memanggilnya. Uji 4 kasus (normal/string/str-dalam-list/sampah) lolos.

### Bug 2: WiFi desktop hanya tampil "menerapkan kredensial..." tanpa status/IP
- Respons `WIFIST` dirutekan worker ke `command_done`, bukan `data_received`,
  sehingga `set_wifi()` tak pernah terpanggil -> label status diam.
- Fix: tambahkan "WIFIST" ke tuple emit `data_received` di worker.run().
  Setelah fix, polling pasca-apply menampilkan "Terhubung ke X · IP ... dBm".

### Bug 3: Web "Uncaught ReferenceError: __SCNDATA__ is not defined"
- replace(__SCNDATA__) = substitusi terbesar (+~4 KB). Tanpa reserve, String
  direalokasi berulang saat membesar; realokasi terbesar rawan gagal saat heap
  terfragmentasi -> token tersisa di HTML.
- Fix: `page.reserve(48000)` di sendUi() -> satu alokasi besar di awal.
- Catatan: bila masih muncul, pastikan flash firmware dari salinan .ino
  TERBARU lalu hard-refresh browser (Ctrl+F5) utk singkirkan cache lama.

### Build
- desktop: rebuild onedir (dist/DMX512Controller/).

---

## Session 47 — Penguatan keandalan: banner error JS + indikator versi firmware

### Perubahan
- **Banner error JS (v44)**: handler `window.error` & `unhandledrejection`
  ditingkatkan dari teks status kecil ke banner merah fixed di atas layar.
  Dipasang SEBELUM const data -> tetap aktif walau script mati di tengah
  (pelajaran dari kasus __SCNDATA__: error hanya terlihat di console).
- **Indikator versi firmware**: `buildStateJson` kini menyertakan
  `"build":"vNN"`; toolbar desktop menampilkan `fw vNN · rev N`. Deteksi dini
  flash basi (UI baru di firmware lama / sebaliknya) tanpa tebak-tebakan.
- BUILD_TAG v43 -> v44.
