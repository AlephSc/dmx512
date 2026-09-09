# Plan v48: Dual-Fader Layout + Custom Fixture + Switch Mode + Bugfix Patch

## Fase 0 — Bugfix Patch Panel (Riset Selesai, Akar Ditemukan)

### Diagnosa bug auto-scroll "New Fixture"
- Lokasi: `bindPatchEvents()` — handler `input` memanggil `renderPatchTable()`
  pada setiap ketikan (dmx_web_rgb.ino ~line 2094).
- `renderPatchTable()` rebuild seluruh tabel via `box.innerHTML=h`.
- Akibatnya: elemen `<input>` yang sedang diketik DIHANCURKAN dan dibuat ulang
  → fokus hilang → browser scroll ke atas (cursor "menghilang").

### Fix (surgical update, tanpa rebuild)
1. `renderPatchTable()`: sel turunan diberi id: `pstart{i}`, `pfoot{i}`,
   `pend{i}` (kolom Akhir).
2. Fungsi baru `patchUpdateDerived()`: update hanya
   - teks + warna `pend{i}` (alamat akhir),
   - `classList.toggle('err')` pada input start/foot,
   - teks status via `patchUpdateStatus(errs)` (diekstrak dari render).
3. Handler `input` → panggil `patchUpdateDerived()` BUKAN `renderPatchTable()`.
4. Full render tetap dipakai untuk: add row, delete row, initial load
   (operasi struktural yang wajar kehilangan fokus).

### Paritas desktop
- Desktop patch tab tidak punya bug ini (Qt widget tidak rebuild saat typing).
- Custom fixture (Fase 2) harus ditambahkan ke desktop juga (lihat bawah).

## Fase 1 — Dual-Fader Layout (Individu kiri, Bank kanan)

### Konsep
- WebUI & Desktop, semua tipe fixture:
  - KIRI: fader per-channel fixture TERPILIH (individu).
  - KANAN: "Fader Bank" = fader untuk channel yang sama, tapi menulis ke
    SEMUA fixture dalam grup/tipe (mirror 1:1 jumlah channel fixture itu).
- Parled 9ch → bank 9 fader; Moving 20ch → bank 20 fader. Jumlah fader bank
  selalu = foot fixture terpilih.

### Implementasi WebUI
1. Pemilihan fixture: klik nama fixture di section tipe → `selFix` state JS.
2. Layout: CSS grid 2 kolom dalam tiap `.type-sec`:
   `.dualfx{display:grid;grid-template-columns:1fr 1fr;gap:12px}`
   (mobile: 1 kolom, bank di bawah).
3. Panel kanan `bankPane`: dirender saat `selFix` berubah:
   - Judul: "BANK {nama tipe} ×{jumlah fixture tipe itu}".
   - Fader bank ditulis via endpoint grup-yang-ada ATAU loop `SET` ke semua
     fixture tipe itu (pilih: loop SET per fixture — tanpa endpoint baru;
     N ≤ 32, payload kecil, WS/HTTP sama).
4. Highlight fixture terpilih (border). Sinkron nilai bank dari `j.cur`
   rata-rata ATAU konsisten-ketika-seragam (aturan syncGroups yang sudah ada).

### Implementasi Desktop
1. `mixer_tab.py`: panel kanan `QSplitter`: kiri daftar fixture section
   (sudah ada), kanan `BankPane(QWidget)` baru.
2. `BankPane.set_fixture(ftype, foot)`: render fader sesuai `channel_labels`.
3. Drag bank fader → emit `GRP`-style loop: kirim `SET fi_c=v` untuk tiap
   fixture tipe itu (sama seperti WebUI).
4. Klik fixture di kiri → update BankPane.

## Fase 2 — Custom Fixture Type + Mode Toggle per Channel

### Model data (firmware)
```cpp
// Custom type: slot tipe 5..15 (t>=5 = custom, didefinisikan user)
struct CustomType {
  uint8_t  used;              // slot terpakai?
  char     name[17];          // nama tipe, mis "RELAY 8CH"
  uint8_t  channels;          // 1..32
  uint8_t  mode[32];          // 0=fader, 1=switch (binary 0/255) per channel
  char     labels[32][9];     // nama slider per channel (maks 8 char + NUL)
};
#define N_CUSTOM_TYPES 6
```
- Ukuran/slot: ±(1+17+1+32+288) ≈ 340 B × 6 = 2,0 KB RAM + NVS.
- Label default channel custom: "CH1..CHn".

### Patch integration
- `FIX_TYPES` + slot custom (dari `/ctypes` JSON).
- Fixture memakai `type=5..15` → `labelOf()` membaca tabel custom, bukan
  chart bawaan.
- `applyFixtures()` validasi: type custom harus `used=1`; foot ≤
  channels? TIDAK — foot bebas 1..512 seperti sekarang; channels custom hanya
  menentukan label & mode default. (Simpel & aman.)

### Mode Switch (binary fader)
- Fader tetap terlihat sebagai slider; nilai dibatasi 0/255:
  - WebUI: `input[type=range]` + logic di `onInput`: `v = v<128?0:255`
    (snap, bukan hysteresis), style thumb beda warna + tick di tengah.
  - Desktop: `VFader` tambah properti `switch_mode`; di `_on_change` snap.
- FIRMWARE WAJIB sinkron: mode tersimpan di CustomType; saat `SET`/`GRP`
  masuk untuk channel mode-switch, firmware snap `v<128?0:255` (sumber
  kebenaran ganda). Mencegah desktop lama/API lama mengirim nilai tengah
  ke relay.
- Mode di WebUI toggle per-channel di PATCH (radio kecil per kolom channel
  di dialog custom type, bukan di mixer).

### Persistence (NVS)
- Namespace yang sama, key baru `ct` (blob kompaksi 6×340 = 2.040 B, satu
  key).
- `STORAGE_VER` tetap 8 + key `ctver` (versi layout custom sendiri) —
  menghindari reset data preset/scene saat layout custom berubah.
- Masuk `persistAll()` snapshot+verify+gen (skema v46 otomatis melindungi).
- CRC tidak perlu: read-back verify sudah ada.
- API: `GET /ctypes` (JSON), `POST /ctypes` (commit penuh, validasi lalu
  persist). Serial: `CTSET <json>` paritas (dipakai desktop).

### Latency & stability (Fase 4 — arsitektur)
1. **Latency path** (sudah baik, dipertahankan): WS fire-and-forget →
   `manualWant` → frame 25 ms. Bank-fader loop `SET` ke N fixture: kirim
   sekali per frame (aggregate) — jangan N request HTTP; via WS satu pesan
   `{"t":"b","ty":<type>,"c":<ch>,"v":<val>}` baru → firmware tulis semua
   member tipe itu sekaligus (1 pesan, nol polling, paling murah).
2. **Stability**:
   - Bank drag: throttle kirim 25-40 ms (paritas frame DMX) di client.
   - `buildSections()` re-render penuh HANYA saat patch berubah (sudah);
     bank fader & pilihan fixture = update DOM surgical (pola Fase 0).
   - Desktop: rebuild section saat LISTF/LISTG saja (sudah); BankPane
     update nilai via `set_value` blockSignals (sudah ada polanya).
3. **NVS**: custom type ditulis HANYA saat commit `/ctypes` (bukan tiap
   toggle) — flash awet, konsisten dengan aturan proyek.

## Urutan implementasi
1. Fase 0 bugfix patch (surgical update) — kecil, langsung terasa.
2. Fase 2 model data custom type + NVS + API (`/ctypes`, `CTSET`).
3. Fase 2 UI patch: dialog create/edit custom type (nama, channels, label
   per channel, radio fader/switch per channel).
4. Fase 1 dual-fader layout WebUI (butuh custom type siap agar bank ikut
   label & mode).
5. Fase 1 desktop BankPane + mode switch di VFader.
6. Fase 4: pesan agregat bank `{"t":"b"}` + throttle client.
7. `docs/logs.md` Session 60; BUILD_TAG v48.

## Validasi
- Bug: ketik nama fixture → fokus & posisi scroll TIDAK berpindah; kolom
  Akhir & status tetap live-update.
- Custom: buat tipe "RELAY 8CH" 8 ch, 3 channel mode switch → patch fixture
  ke alamat bebas → mixer tampil label custom, slider switch snap 0/255,
  firmware menolak nilai tengah (uji via API langsung).
- Bank: pilih Parled → bank 9 fader → geser bank ch2 (G) → SEMUA Parled
  berubah G serentak satu frame.
- Desktop paritas: bank & switch identik dengan WebUI.
- py_compile desktop; node --check WebUI; test_labels.py diperluas utk
  custom type.
