# DMX512 — ESP32 Web Lighting Console

Controller DMX512 berbasis ESP32 + MAX485/MAX3485 dengan Web UI lengkap.
Code utama ada di folder [`dmx_web_rgb/`](./dmx_web_rgb/).

## Fitur
- Web UI (WiFi STA/AP) kontrol DMX dari browser/HP/tablet.
- 18 fixture patch: 10× PAR (9ch), 2× MOVING (20ch), 2× BEAM (16ch), 2× STROBE (4ch), 2× FOG (2ch).
- 16 preset (fade/hold per-preset), 20 scene × 30 langkah, Fader Bank, Master dimmer, Blackout, Chase.
- Ekspor/Impor preset (file `.json`), Save/Load Data (NVS compact sejak v28), mode EDIT/SHOW.
- Dual-core: DMX timing Core 0, WebServer Core 1.

## Skema Wiring
GPIO17 → MAX485 DI · GPIO16 → MAX485 RO · GPIO4 → DE/RE · Lihat [`docs/wiring_diagram.txt`](./docs/wiring_diagram.txt).

## Dokumentasi
- [`docs/`](./docs/) — DESAIN, riset, analisis, log pengembangan.
- [`logs.md`](./docs/logs.md) — changelog per sesi.

## Struktur
```
dmx_web_rgb/        # Sketch utama (Arduino IDE: open folder ini)
docs/               # Desain, riset, wiring, changelog
```