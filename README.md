# DMX512 — ESP32 Web Lighting Console

Controller DMX512 open-source berbasis ESP32 + MAX485/MAX3485 dengan Web UI dan aplikasi desktop.

## Fitur utama

- Web UI responsif dari browser/HP/tablet.
- Kontrol fader channel, fader grup, master, strobe, blackout, preset, scene, dan chase.
- WebSocket untuk kontrol fader realtime; HTTP menjadi fallback.
- 18 fixture patch: 10× PAR (9ch), 2× MOVING (20ch), 2× BEAM (16ch), 2× STROBE (4ch), 2× FOG (2ch).
- 30 preset dengan fade/hold per preset.
- 20 scene, masing-masing hingga 50 langkah preset.
- Import/export JSON dan Save/Load Data ke NVS.
- WiFi STA dengan SSID/password kustom dari Web UI atau aplikasi desktop.
- Fallback AP darurat bila koneksi gagal.
- W5500 Ethernet melalui SPI.
- Kontrol serial untuk integrasi eksternal.
- Desktop controller PySide6 dengan USB Serial, WiFi HTTP, MIDI controller, dan MIDI-learn.
- Dual-core: timing DMX pada Core 0, jaringan pada Core 1.

## Struktur

```text
dmx_web_rgb/        # Sketch firmware utama
 desktop/            # Aplikasi desktop PySide6 + build spec
 docs/               # Desain, riset, wiring, testing, changelog
 LICENSE             # MIT License
```

## Firmware

Buka `dmx_web_rgb/dmx_web_rgb.ino` menggunakan Arduino IDE.

Wiring default MAX485:

```text
ESP32 GPIO17 -> MAX485 DI
ESP32 GPIO16 -> MAX485 RO
ESP32 GPIO4  -> MAX485 DE + RE
```

W5500 default SPI:

```text
SCLK GPIO18 · MISO GPIO19 · MOSI GPIO23 · CS GPIO5
```

Upload firmware. Serial Monitor harus menampilkan `DMX Web Console v44` atau versi lebih baru.

> Firmware belum dapat di-compile otomatis di repository environment ini. Compile/upload harus divalidasi menggunakan Arduino IDE dan board ESP32 target.

## Desktop

```powershell
cd desktop
python -m pip install -r requirements.txt
python main.py
```

Untuk distribusi Windows, salin seluruh folder hasil build:

```text
desktop/dist/DMX512Controller/
```

Jangan hanya menyalin file `.exe`; DLL Qt dan runtime berada di folder yang sama.

## Pengujian

Lihat [`docs/TESTING.md`](./docs/TESTING.md) untuk prosedur uji firmware, Web UI, desktop, serial, WiFi, Ethernet, dan MIDI.

## Lisensi

MIT License. Lihat [`LICENSE`](./LICENSE).
