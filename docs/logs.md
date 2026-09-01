# Session Logs - DMX512 Controller ESP32 Project

## Session 65 - 2026-09-01 - v50: deck fisik dimatikan (HW_DECK_ENABLE 0)

### Konteks (lanjutan dari sesi terputus)
Bisect user (2 build, satu variabel): **noHW = NORMAL, noArtNet = LAGGING**
→ pelaku eksklusi: `hwInputTask`/task `hwIn`, BUKAN Art-Net, BUKAN strobe
gate, BUKAN wiring/EMI (pin tetap terpasang saat uji noHW dan tetap normal).
v49.3-v49.5 (noise guard, restart guard, lockout global) TIDAK menuntaskan.
User meminta revert: "kembalikan dulu tanpa fitur button fisik".

### Keputusan desain: switch compile-time, bukan penghapusan
`HW_DECK_ENABLE 0` di `dmx_web_rgb.ino` — seluruh blok deck (deklarasi pin,
hwBankAdjust, hwPlayScene, hwNoiseGate, hwInputBegin, hwInputTask, hwLoop)
dibungkus `#if HW_DECK_ENABLE`. Kondisi eksekusi identik BISECT-A_noHW yang
terbukti normal: pin tak dikonfigurasi, task `hwIn` tak dibuat, polling tak
jalan. Set 1 untuk mengaktifkan lagi (kode v49.x utuh, tak perlu diketik ulang).

### Titik yang dibungkus switch
- Blok kode deck (± line 449-714) + `hwInputBegin()` di setup().
- `xTaskCreatePinnedToCore(hwLoop...)` di setup() + log boot alternatif.
- Serial `HWOFF/HWON`: cabang `#else` menjawab `{"ok":false,"err":"hw_disabled"}`.
- State JSON: `#else` mengirim `"hw":false` saja (tanpa hwBank/hwEnc/hwB).
- Web UI: banner `hwDeck` jadi "DECK FISIK: NONAKTIF di build ini".
- `sceneStartedAt` (v49.5) TETAP di luar switch — dipakai jalur play
  HTTP/serial (restart guard); `j.hwBank!==undefined` guard JS WebUI aman.

### Dokumentasi
README: status v50, section deck jadi "NONAKTIF default" + spesifikasi
bersyarat, wiring + catatan aktivasi, tabel API, roadmap Selesai.

### Validasi
Grep pasangan `#if/#endif` = 6 blok berpasangan; semua simbol hw hanya di
dalam blok atau komentar/guard JS. Compile Arduino IDE: user (aturan proyek).

### Harapan & uji
Upload v50 → strobe master 255 harus cepat seperti v48/noHW (commit 14c64f3
v49.5 ikut ter-push bersama commit ini — push lama gagal DNS).
Jika masih lambat: bukan deck, kembali ke dmxTask/frame path.

## Session 64 - 2026-08-28 - v48 fix: fader bank "bouncing" (echo server)

### Gejala (user report)
Drag fader bank pelan: nilai fader fixture "memantul" — bank 255, fixture
malah 226, dst.

### Akar masalah — ECHO LOOP
1. Drag bank → slider member di-set lokal + `bankSend` WS (throttle 30 ms).
2. Server broadcast state 10 Hz (throttle v47) — nilai yang dikirim bisa
   LEBIH TUA dari posisi drag terbaru (race antrean WS + interval broadcast).
3. `syncFromServer` menimpa SEMUA slider `fi_c` dengan nilai basi itu —
   termasuk yang baru saja di-drag → nilai "mundur" → tampak memantul.

Slider bank tidak terdaftar di `activeKey` (mekanisme anti-timpa hanya
mengenal slider individu) → tidak ada proteksi.

### Fix
- WebUI: flag `bankDragUntil` (drag aktif + 600 ms) → `syncFromServer`
  melewatkan penulisan slider selama itu. Sederhana: bank = satu channel
  pada satu waktu; 600 ms cukup menutup jendela echo (broadcast 1 Hz idle).
- Desktop: `_bank_drag_keys` (set "fi_c" member channel yang di-drag) —
  diisi saat `pressed`, dikosongkan saat `released`; `apply_state` skip
  key di set itu.
- Paritas perilaku dengan mekanisme `active_keys` lama.

### Validasi
py_compile PASS, node --check PASS. Uji runtime: drag bank pelan 0→255
→ fader member harus monoton naik tanpa turun sendiri.

## Session 63 - 2026-08-28 - v48: hapus duplikasi fader GRUP (WebUI+desktop)

- Laporan user: fader bank dan fader group duplikat — benar; GRUP (v44,
  8 channel tetap per tipe) adalah subset fungsi BANK (v48, semua channel).
- GRUP dihapus dari section WebUI + desktop; orphan group section ikut
  dihapus; `syncGroups` diberi guard null (elemen `g<i>` tak ada lagi).
- Endpoint `/grp` + perintah GRP serial tetap didukung firmware
  (kompatibilitas API lama); `group_faders` desktop kosong → apply_state
  no-op otomatis.

## Session 62 - 2026-08-28 - v48 fix: UI gagal alokasi heap → streaming PROGMEM

### Gejala (user report)
Web: "UI: gagal alokasi heap saat substitusi. Muat ulang halaman." — guard
v46 menampilkan error 500 sendUi.

### Akar masalah
- sendUi() v46 masih menyusun String halaman UTUH: base HTML v48 (~49 KB
  setelah editor ctype + dual-fader) + injeksi ≈ 54 KB alokasi KONTIGU.
- Heap ESP32 terfragmentasi saat WS aktif → alokasi besar gagal → token
  tak terganti → guard menolak. Guard bekerja sesuai desain; masalahnya
  strategi alokasi, bukan deteksi.

### Fix: streaming chunked dari PROGMEM
- sendUi() ditulis ulang: kirim potongan langsung dari flash
  (`sendContent_P`) — token diganti on-the-fly via strstr per posisi,
  SEMUA kemunculan (paritas String::replace lama).
- Puncak heap: dari ~54 KB → ≤2,3 KB (hanya JSON builder kecil; itu pun
  di-reserve sejak v47).
- `setContentLength(CONTENT_LENGTH_UNKNOWN)` + chunked terminator
  `sendContent("")` — kontrak WebServer ESP32.
- Nol alokasi halaman → error ini TIDAK MUNGKIN terulang, apapun kondisi
  heap. (ponytail: kalau suatu saat HTML > 100 KB dan streaming lambat di
  jaringan lambat, pindah ke gzip static + data via fetch — belum perlu.)

### Verifikasi
- Overload `sendContent_P(PGM_P)` dan `(PGM_P,size_t)` dikonfirmasi di
  core ESP32 3.3.7 lokal (WebServer.h).
- INDEX_HTML = `const char[] PROGMEM` → pointer flash valid di ESP32.
- JS node --check PASS, BOM bersih. Compile via Arduino IDE (user).

## Session 61 - 2026-08-28 - v48 review: 4 bug ditemukan & diperbaiki

Review menyeluruh kode v48 (self-review setelah implementasi):

1. **[BUG-FATAL] `customTypesJson()` typo** — `String j="[{";` (harusnya `"["`)
   → JSON rusak → `JSON.parse` gagal di WebUI/desktop → custom type tak
   pernah termuat. FIXED.
2. **[BUG] Desktop bank_title/bank_col sebagai `self.*`** — multi-section
   (PAR + MOVING + ...) saling menimpa; `_build_bank` section pertama
   menulis widget section terakhir. FIXED: lokal per-section.
3. **[BUG-RISK] Desktop `mousePressEvent` monkey-patch** — tidak dijamin
   semua binding PySide. FIXED: subclass `_ClickLabel`.
4. **[PERF] Desktop bank tanpa throttle** — drag bank 32 fixture via serial
   = 32×SET ≈ 640 B per tick membanjiri antrean (115200 ≈ 55 ms/tick).
   FIXED: throttle 30 ms + flush pending saat release (paritas WebUI).

Audit lolos (tanpa perubahan):
- 6 jalur `snapSwitchMode` (WS set/bank, HTTP set/grp, serial SET/GRP).
- `channelLabelsFor` closure — menangkap objek `c` benar.
- Fader bank tak masuk `chan_faders`/`sliders` → tak ditimpa sync polling.
- `commitCustomTypes` bounds: slot invalid → skip; name kosong → "CUSTOM";
  label kosong → CHn; NVS blob 3.719 B ≤ 4.000 (static_assert).
- FIXSET/POST /fixes menolak tipe custom belum-didefinisikan dengan error
  `custom_type_not_defined` (UX: pesan di patchStatus).

Validasi: py_compile PASS, node --check PASS, test_labels 9/9 PASS,
git diff --check bersih, BOM bersih.

## Session 60 - 2026-08-28 - v48: Dual-fader + custom fixture type + switch mode + patch bugfix

### Permintaan user (4 poin)
1. Dual-fader layout (kiri individu, kanan bank per tipe).
2. Custom fixture type + mode toggle Fader/Switch per channel.
3. Bug: "New Fixture" auto-scroll saat mengetik nama.
4. Rekomendasi arsitektur (persistence, latency, stability).

### Fase 0 — Bugfix auto-scroll (AKAR DITEMUKAN)
- `bindPatchEvents()` memanggil `renderPatchTable()` tiap ketikan →
  `innerHTML` rebuild → input yang sedang diketik dihancurkan → fokus hilang
  + browser scroll ke atas.
- FIX: `patchUpdateDerived()` — update surgical sel turunan
  (`pend{i}` teks+warna, kelas err pada start/foot, status bar) TANPA
  menyentuh DOM input. Rebuild penuh hanya utk add/delete row (struktural).

### Fase 2 — Custom Fixture Type (firmware)
- `CustomType` struct: used, name[17], channels(1-32), mode[32] (0=fader,
  1=switch), labels[32][9]. Slot tipe 5..15 (11 slot), `CUSTOM_IDX(t)=t-5`.
- NVS key `ctcfg`: 1 + 11×338 = 3.719 byte (static_assert ≤4000). Versi
  format sendiri (CTCFG_VER) — STORAGE_VER preset/scene tidak disentuh.
- `persistCustomTypes()`/`loadCustomTypes()`; dipanggil di setup().
- `customTypeMode(type,ch)` + `snapSwitchMode(type,ch,v)`: firmware =
  sumber kebenaran — SET/GRP/WS semua di-snap `v<128?0:255` untuk channel
  mode-switch. Dua lapis: client juga snap sebelum kirim.
- `validateFixtures()`: tidak lagi menolak type>FX_FOG; validasi
  `custom_type_not_defined` pindah ke `applyFixtures()` (pesan lebih jelas).
- API: `GET /ctypes` (hanya slot used), `POST /ctypes` (commit by-slot,
  slot tak disebut tetap utuh) + serial `LISTCT`/`CTSET <json>`.

### Fase 2b — WebUI editor custom type
- Panel Patch → tombol "Tipe Custom" → editor inline: pilih slot 5-15,
  nama, jumlah channel, per-channel: nama label (maks 8 char) + radio
  Fader/Switch. Simpan via POST /ctypes; cache CT lokal diperbarui langsung
  (tanpa reload).
- Dropdown Tipe di tabel patch menambah opsi `* NAMACUSTOM` (slot used).
- `channelLabelsFor(i)`: label dari custom type + flag mode; label switch
  ditandai ⚡; slider kelas `switchmode` (thumb persegi kuning).

### Fase 1 — Dual-fader layout
- WebUI: `.dualfx` grid — kiri fixture individu (klik nama = pilih,
  highlight biru), kanan BANK. Bank render sesuai channel fixture terpilih:
  drag bank = update slider individu (visual instan) + satu pesan agregat.
- Desktop: `BankPane` di kanan tiap section (QFrame) — klik nama fixture
  (cursor hand) membangun ulang bank; drag bank = emit `SET fi_c=v` untuk
  semua member tipe itu (N≤32, fire-and-forget).
- Custom type name tampil di header section & bank (`_type_name()`).

### Fase 4 — Latency & stability
- Pesan WS agregat bank `{"t":"b","ty","c","v"}` — SATU pesan menggantikan
  N request /set; handler firmware menulis semua member dalam satu operasi
  mutex (satu frame, snap out).
- Throttle client bank: maks 1 pesan / 30 ms (paritas frame DMX 25 ms);
  fallback HTTP loop hanya bila WS down.
- Custom type persist HANYA saat commit (bukan tiap toggle) — flash awet.
- Desktop `set_custom_types()` data-only + re-render idempotent (pola
  LISTF/LISTG).

### Validasi
- py_compile (mixer_tab/state/worker/main/widgets): PASS.
- node --check WebUI script: PASS. BOM: bersih.
- Firmware compile via Arduino IDE (user).
- Uji runtime yang harus dilakukan user:
  1. Ketik nama fixture di patch → fokus & scroll TIDAK lompat.
  2. Buat tipe "RELAY" 8ch, 3 channel switch → patch fixture → label ⚡
     tampil, slider snap 0/255, DMX output hanya 0/255 (uji relay nyata).
  3. Bank: pilih Parled → geser bank G → semua Parled G serentak.
  4. Desktop: klik nama fixture → bank kanan berubah; drag bank → semua
     member berubah.

## Session 59 - 2026-08-28 - v47: Urutan koneksi ETH>WiFi>AP + staged boot delay

### Permintaan user
1. Prioritaskan W5500 (1) → WiFi (2) → AP (3).
2. Delay sebentar setelah ESP32 menyala agar tidak spike (brownout kemarin).

### Jawaban atas "apa ini sudah?"
- Urutan SEBELUMNYA: WiFi dulu (15 dtk) baru Ethernet — kebalikan
  permintaan. `activeIP()` memang sudah memilih Ethernet dulu utk IP aktif,
  tapi proses INIT-nya terbalik.
- Delay boot: BELUM ada. Yang sudah ada hanya `pendingGenMigration`
  (tunda tulis NVS 10 dtk) — itu melindungi dari spike tulis flash, bukan
  spike radio/CPU saat boot.

### Perubahan (setup())
- TAHAP 1: Ethernet W5500 — `SPI.begin()` + `ETH.begin()` duluan.
  `ethHasIP` flag baru.
- delay(1000) antar tahap: rail 3V3 stabil setelah NVS load, sebelum radio.
- TAHAP 2: WiFi STA (kredensial kustom/bawaan, 15 dtk).
- TAHAP 3: AP darurat HANYA bila `!ethHasIP && WiFi gagal`. Perbaikan
  sekalian: cabang lama menilai `ETH.linkUp()` (link ada tapi tanpa IP tetap
  dianggap "Ethernet ada" padahal tak bisa dipakai) — kini pakai `ethHasIP`.
- Total boot bertambah ±2 dtk untuk stabilitas daya.

### Catatan
- Em-dash korup (mojibake UTF-8) pada komentar lama ikut terbersihkan.
- BOM check: clean. Brace balance: identik pola baseline HEAD.

### Validasi
- Firmware compile via Arduino IDE (user).

## Session 58 - 2026-08-28 - v47: Perbaikan hasil comprehensive review

Prioritas 1-3 + 5 ringan + 6-7 dari review Session 57 diimplementasikan.

### Prioritas 1 — Bug class attribute (desktop)
- `MixerTab._fixtures_data` pindah dari class attribute ke `__init__`
  (mutable class attr terbagi antar instance — pola bug klasik).

### Prioritas 2 — reserve() JSON builder (firmware)
- `buildStateJson()`: reserve(2048) — dipanggil wsBroadcast tiap detik;
  ±150 konkatenasi tanpa reserve = ratusan realloc/dtk.
- `fixJson()`: reserve(2300). `grpJson()`: reserve(512).
- `scnJson()`: reserve(1400).

### Prioritas 3 — Throttle WS broadcast (firmware)
- `wsBroadcastTick()`: revision change dibatasi max 10 Hz (min 100 ms antar
  broadcast). Heartbeat 1 dtk saat diam tetap. Sebelumnya slider drag =
  puluhan broadcast ~2 KB/dtk ke semua client.

### Prioritas 5 — std::atomic (firmware)
- `stateRevision` & `sceneRev` → `std::atomic<uint32_t>`: hanya dua counter
  yang ditulis Core 0 (applyPresetToWant) & dibaca Core 1. `volatile` tidak
  menjamin atomicity ++ di Xtensa.
- Semua konversi `String(atomic)` → `String(atomic.load())`; snapshot
  `wsBroadcastTick` baca `.load()` sekali.

### Prioritas 6 — LISTP/LISTS conditional fetch (desktop worker)
- Worker menyimpan `_last_rev`/`_last_scene_rev` (dari GET) dan
  `_fetched_rev`/`_fetched_scene_rev` (nilai saat fetch terakhir dikirim).
- LISTP/LISTS hanya dikirim bila berbeda. Konservatif by design (revision
  naik juga karena slider) — yang dihindari: fetch abadi tiap 1,5 dtk
  walau idle. Desain awal API `note_structural_refresh()` dibuang
  (over-engineered, menambah coupling).

### Prioritas 7 — Group yatim (WebUI)
- `buildSections()`: group yang typeFilter-nya tak cocok fixture mana pun
  kini dirender di section "GRUP LAIN" (label "×0") — dulu hilang diam-diam
  saat panel Fader Bank global dihapus.

### Unit test
- `tests/test_labels.py` (9 kasus: PAR/MOVING/BEAM potong/pas/sisa/unknown/
  STROBE/FOG): ALL PASSED.

### Validasi
- py_compile worker/mixer_tab/state: PASS.
- WebUI JS `node --check`: PASS.
- Firmware compile via Arduino IDE (user).

### Tidak dikerjakan (sesuai rekomendasi menunggu v47 tervalidasi)
- Prioritas 4: pemisahan file .h/.ino (HTML, NVS) — ditunda.

## Session 57 - 2026-08-28 - v47: Mixer section per tipe + label channel lengkap

### Permintaan user
1. Per fixture punya semua fader group-nya sendiri (PAR: RGB/Dim/Strobe/Mode;
   Moving: Dim/Pan/Tilt/dll).
2. Fixture di-sekat per grup tipe (fader bank + fader per fixture dalam satu
   section tipenya).
3. Desktop lebih clean: fixture overflow turun ke bawah (wrap), tanpa scroll
   horizontal.

### Temuan audit (mengubah scope)
- Kontrol per-channel per fixture SUDAH ada di kedua UI (WebUI buildFixes,
   desktop build_fixtures v45). Yang bikin terasa hilang: label channel
   generik `CH1..CHn` untuk Moving/Beam.
- Scope final: chart label + restrukturisasi section. NOL perubahan
  protokol/endpoint firmware.

### Perubahan
**Chart label channel (Moving 20ch / Beam 16ch, standar):**
- Moving: Pan, PanF, Tilt, TiltF, P/T Spd, Dim, Strobe, ColorSpd, Gobo,
  GoboRot, PrismRot, Focus, Zoom, Shutter, Func, Reset, CH19, CH20.
- Beam: Pan, PanF, Tilt, TiltF, P/T Spd, Dim, Strobe, Color, Gobo, GoboRot,
  Prism, Focus, Zoom, Shutter, Func, Reset.
- Rule fit: foot < chart = dipotong; foot > chart = sisa `CHn`; tipe tak
  dikenal (t>4) = `CHn` (patch custom aman).

**WebUI (firmware HTML/JS/CSS):**
- `labelOf()` + `fitLabels()`: chart di atas.
- Panel `#faderPanel` (Fader Bank terpisah) DIHAPUS. `#channelPanel` jadi
  "Mixer" berisi `<details class="type-sec">` per tipe: summary
  (nama · jumlah unit · rentang DMX), fader grup tipe itu (id `g<i>`
  dipertahankan — `syncGroups()` tetap jalan), lalu grid fixture
  `repeat(auto-fill,minmax(300px,1fr))` — wrap otomatis ke bawah.
- `buildSections()` menggantikan `buildFixes()`+`buildGroups()`.
- PAR default terbuka; tipe lain terlipat (details .open=false).
- Media query mobile: faderPanel dikeluarkan dari daftar order.

**Desktop (`desktop/state.py`, `desktop/ui/mixer_tab.py`):**
- `channel_labels()`: chart sama persis (paritas).
- `mixer_tab.py` ditulis ulang: baris "FADER GRUP" horizontal lama dihapus;
  tiap tipe = QFrame section + QToolButton toggle collapse (panah be/b8,
  teks nama · unit · DMX range) + baris group fader tipe itu + QGridLayout
  fixture `FIX_PER_ROW=4` (wrap ke bawah, tanpa scroll horizontal).
- `build_groups()` jadi data-only (LISTG/LISTF bisa datang beda urutan;
  re-render idempotent via `build_fixtures()`; cache `_fixtures_data`).
- `group_faders` kini list `(idx, fader)` — `apply_state()` disesuaikan.
- Semua handler/sync tidak berubah: SET/GRP, aturan seragam-grup, proteksi
  activeKeys.

### Validasi
- `py_compile` mixer_tab.py + state.py: PASS.
- JS WebUI: `node --check` ekstrak script block: PASS (syntax OK).
- BOM yang tak sengaja masuk saat edit PowerShell dihapus (Arduino-safe).
- Firmware compile via Arduino IDE (tidak di-build di sesi ini).

### Sisa catatan
- Label Moving/Beam = chart standar; kalau chart fixture fisik berbeda,
  edit array di `labelOf()` (WebUI) dan `channel_labels()` (desktop) —
  keduanya harus tetap sinkron.
- `__SCNDATA__` fix + NVS transactional + COW v46 tetap utuh; BUILD_TAG v47.


## Session 57 - 2026-08-28 - File import preset+scene 3 PARLED

### Konteks user
3 PARLED: kanan d001 (DMX ch 1-3), tengah d010 (ch 10-12), kiri d019 (ch 19-21).
Minta 5 scene keren (chase dll) dalam format siap import.

### Hasil
- `parled-3-scene.json` (root repo, 31 KB): format export firmware ver 6
  (`{"app":"DMX-RGB","ver":6,"presets":[{u,f,h,c[512]},...]}`), 30 slot,
  preset 1-20 terisi, 21-30 hidden kosong.
  - P1-9: RGB dasar per PAR (merah/hijau/biru x kanan/tengah/kiri), f=50ms h=400ms (snap chase).
  - P10 full white (f=1000), P11-12 CMY trio (f=800), P13 blackout (f=300 h=200),
    P14 deep purple ambient (f=1200), P15-16 amber/mint snap, P17-20 hot pink/
    electric blue/amber/cyan all-PAR.
- `setup-parled-scenes.ps1` (root repo): isi scene 1-5 via HTTP `/spush`
  setelah JSON diimport (import file hanya memuat preset; scene via API).
  - Scene 1: chase RGB 3-way klasik (1,4,7,2,5,8,3,6,9)x5.
  - Scene 2: chase RGB zig-zag (3,5,7,2,4,9,1,6,8)x5.
  - Scene 3: color cycle halus (10,11,12,10,14)x4 fade panjang.
  - Scene 4: chase warm/cool (15,16,17,18,19,20,13,17,18)x5.
  - Scene 5: strobe blip (10,13)x8+10.
- Asumsi channel: R,G,B pada 3 ch pertama tiap PAR. Dimmer ch0 (offset 0)
  tidak diset di preset; jika PAR butuh dimmer, naikkan via fader group
  "PAR Dim" (GRP 0) — preset hanya menulis warna.

### Validasi
- Python assert: 512 ch per preset, scene 50 step, nomor preset 1-30,
  tanpa duplikat berurutan (rule `/spush` 409), tanpa hole.
- Semua key (`"presets":[`, `u`, `f`, `h`, `c`) ada dalam format yang
  dibaca parser `importJson()` (indexOf-based). Ukuran 31 KB < 48 KB IMPORT_MAX.
- Tidak ada perubahan firmware. Tidak build C++ (aturan proyek).

### Cara pakai
1. Import `parled-3-scene.json` via DMX512Controller (System > IMPORT) atau WebUI.
2. `powershell -ExecutionPolicy Bypass -File setup-parled-scenes.ps1` (isi IP ESP32).
3. Mainkan: `/splay?s=1`..`/splay?s=5` (atau tombol scene desktop/WebUI).


## Session 56 - 2026-08-28 - v46 fix: brownout loop saat boot (migrasi gen)

### Gejala (user report)
Serial log: `E BOD: Brownout detector was triggered`, `rst:0x3`, garbage
baud, `rst:0x7 (TG0WDT_SYS_RESET)`, lalu boot ulang v46, berhenti tepat
setelah "Fixture: pakai patch default". ESP "nyala sedikit lebih lama"
tiap siklus. (Baris "E BOD" terpotong "E BOD" = log brownout @74880 baud.)

### Akar masalah
- Brownout = tegangan VDD jatuh di bawah ~2,43 V → hardware reset. Ini
  MASALAH DAYA (PSU/kabel marginal), bukan bug logika firmware.
- Pemicu spesifik v46: migrasi v45→v46 menulis ~11 KB ke flash SAAT BOOT
  (persis setelah "Fixture: pakai patch default" — log mati di situ).
  Tulis flash = lonjakan arus; boot = momen arus paling kritis; WiFi
  radio sudah menyala → PSU marginal drop → BOD → reset → ulangi.

### Fix (mengurangi pemicu, bukan mengganti PSU)
- Migrasi gen kini DEFERRED: `pendingGenMigration` flag → `loop()`
  menjalankan `persistAll()` setelah 10 dtk berjalan stabil.
- Retry otomatis bila persist gagal (`lastSaveOk` false).
- CATATAN: ini mengurangi beban saat boot, TIDAK menyembuhkan PSU
  marginal. Solusi permanen tetap sisi daya (lihat rekomendasi).

### Rekomendasi hardware (urutan prioritas)
1. Ganti kabel USB (kabel charge-only/petak = penyebab #1 ESP32 brownout).
2. PSU 5V ≥ 2 A yang stabil; hindari port USB hub/charger lemah.
3. Tambah kapasitor elektrolit 470-1000 µF di jalur 5V ESP32.
4. Jika MAX485/W5500 dipasang: pastikan VCC mereka dari rail terpisah
   yang memadai, bukan mengikuti drop ESP32.
5. Ukur 5V dengan multimeter saat boot — drop < 4,5 V = PSU/kabel gagal.

### Validasi
- Firmware compile via Arduino IDE (tidak di-build di sesi ini).

## Session 55 - 2026-08-28 - v46 fix: ReferenceError __SCNDATA__ di WebUI

### Bug (user report)
`Uncaught ReferenceError: __SCNDATA__ is not defined` di browser.

### Akar masalah
- `sendUi()` memakai `page.reserve(48000)` — dihitung untuk HTML v44.
- HTML base v45 (setelah panel Patch ditambahkan) = 46.301 byte; total
  dengan injeksi FIX/GRP/SCN/NP ≈ 52 KB > 48 KB.
- Akibatnya `String::replace()` harus realloc ke ~52 KB. Saat heap ESP32
  terfragmentasi (WS aktif, JSON besar, import), realloc gagal SENYAP →
  token `__SCNDATA__` tertinggal di HTML → JS browser crash.
- Komentar kode lama sudah memprediksi failure mode ini, tapi angka reserve
  tidak pernah diperbarui setelah HTML tumbuh.

### Fix
- `sendUi()` kini: hitung dulu `fixJson()/grpJson()/scnJson()` →
  `reserve(base + semua injeksi + 256)` = alokasi TEPAT sekali, tanpa realloc.
- Verifikasi pasca-substitusi: bila token masih ada → HTTP 500 dengan pesan
  teks jelas ("gagal alokasi heap saat substitusi"), bukan JS rusak.
- ponytail: bila error 500 ini sering muncul (heap sangat terfragmentasi),
  upgrade path = kirim HTML polos + muat data via fetch /scenes /fixes
  /groups setelah load (tanpa injeksi inline).

### Validasi
- Ukuran literal HTML diukur: 46.301 byte (script PowerShell) — membenarkan
  diagnosis melebihi reserve 48 KB.
- Firmware compile via Arduino IDE (tidak di-build di sesi ini).

## Session 54 - 2026-08-27 - v46: Fix edge-case COW pada slot bayangan

### Bug
Proteksi COW memakai syarat `presets[idx][0]` (flag used). Slot BAYANGAN
(used=0) yang masih dirujuk scene tidak terlindungi: merekam ulang /
mengubah fade-hold / import ke slot itu menimpa data scene tanpa COW —
persis bug asli user, lewat pintu belakang.

### Fix
- Hapus syarat `used` di semua gerbang COW; `presetSceneRefCount(idx)>0`
  saja yang menentukan perlunya proteksi:
  - `capturePreset()` (REKAM WebUI, `/psave`, serial `REC`)
  - `/psetfade` dan serial `PFH`
  - `importJson()` commit
- Semantik hasil: rekam ke slot dirujuk scene (visible atau bayangan) =
  data lama dipindah ke bayangan baru untuk scene, slot menjadi preset
  baru. Data scene tak pernah tertimpa.
- Catatan: pad bayangan di WebUI tampil kosong + border oranye; user bisa
  memilih merekam ke situ — kini aman (COW jalan, bukan menimpa).

### Validasi
- Grep: tidak ada lagi `presets[i][0] &&` di gerbang COW (6 gerbang semua
  pakai refcount murni).
- Firmware tidak di-build sesuai aturan proyek.

## Session 53 - 2026-08-27 - v46: NVS transactional + hint shadow_full desktop

### (2) NVS transactional (firmware)
- Masalah: `persistAll()` menulis 6 key berurutan; listrik mati di tengah =
  boot memuat campuran data baru/lama tanpa terdeteksi.
- Skema commit-marker (tanpa double-buffer — partisi NVS ~20 KB tak muat
 2×5,4 KB blob, pelajaran v27): 1. `persistAll()` tulis `sver2/pc0/pc1/sc/selP/selS`.
 2. Read-back semua blob + `memcmp` terhadap snapshot RAM.
 3. Semua verified → tulis `gen` (uint32, monotonic naik) PALING AKHIR.
 4. Gagal verify → `gen` tidak dinaikkan, log Serial
     "NVS: persist GAGAL verify", `lastSaveOk=false` (terlihat di UI).
- `loadAll()` (boot): muat hanya bila blob utuh; `gen==0` = data v45 lama →
  muat sekali + `persistAll()` migrasi (menulis gen pertama). Blob rusak →
  mulai bersih + log.
- `loadData()` (tombol Load): tolak bila `gen==0` (snapshot tak pernah
  terkomit valid).
- Nilai kunci: snapshot yang terpotong listrik TIDAK PERNAH punya gen naik,
  jadi tidak akan pernah dimuat. Kehilangan data setelah crash dibatasi ke
  snapshot sebelumnya yang terkomit.
- Catatan: `gen` = key NVS baru; `STORAGE_VER` tetap 8 (blob format tidak
  berubah; gen adalah proteksi tambahan, bukan perubahan layout).

### (3) Pesan shadow_full di desktop
- `desktop/main.py`: `_ERR_HINTS` map err-code → penjelasan operator.
  `shadow_full` → "Slot preset habis: data lama dipertahankan untuk scene.
  Kosongkan scene/preset yang tak terpakai, lalu ulangi."
- `_on_cmd_done()` menampilkan hint di status bar + log di tab sistem.
- Berlaku untuk REC/PFH via serial (JSON `{"ok":false,"err":"shadow_full"}`)
  maupun WiFi (HTTP 507 body `{"ok":false,"code":"shadow_full",...}`).

### Validasi
- `py_compile` desktop (main/worker/transport/state): PASS.
- Firmware tidak di-build sesuai aturan proyek (compile via Arduino IDE).

## Session 52 - 2026-08-27 - Desain AI controller (konsultasi, belum diimplementasi)

### Scope
Dua diskusi desain AI lighting control; tidak ada perubahan firmware/desktop.

### Kesimpulan utama
- AI cloud (OpenRouter dll): cocok untuk keputusan artistik lambat (pilih
  scene/preset/palette, 0.3-5 s), TIDAK untuk kontrol per-frame. Arsitektur
  bridge di PC + function calling ke endpoint HTTP/WS yang sudah ada.
- AI lokal realtime (sound card mixer -> laptop): arsitektur dua-tier.
  Tier cepat = DSP audio (FFT, onset, energi band) + model kecil
  (inference <1 ms) mengirim via WS `{"t":"s"}` / `/grp` / `/ctrl`
  10-30 cmd/s; firmware Core 0 yang menghaluskan fade. Tier lambat =
  LLM lokal opsional sebagai "direktur" per menit.
- Latensi estimasi end-to-end 60-90 ms (buffer 512 + hop + LAN + frame DMX).
- Rencana 4 fase: (1) DSP rule-based + logging data, (2) imitation learning
  dari koreksi operator, (3) model sklearn -> LSTM kecil opsional,
  (4) firmware v47 opsional: dead-man switch + migrasi mutasi GET->POST.
- Output model = kosakata diskrit (nomor preset/scene) + parameter kontinu
  (intensity, tempo ratio), BUKAN 512 channel mentah.
- Preset/scene yang ada dipakai sebagai action vocabulary, bukan target
  regresi. Scene snapshot independence (v46 COW) membuat hasil training
  stabil terhadap re-record preset.

### Rencana detail: rute API (OpenRouter dll)
- Posisi API: operator semi-otonom (keputusan artistik per 2-10 s),
  bukan per-frame. Latensi LLM 0.5-3 s.
- Fase 0 firmware: migrasi mutasi GET->POST + auth token `X-DMX-Key`
  + rate limit ~20 cmd/s.
- Fase 1 bridge: modul Python `ai_bridge` di desktop app; function
  calling dengan tool map 1:1 ke endpoint yang ada (`/set`,`/grp`,
  `/ctrl`,`/pload`,`/psetfade`,`/splay`,`/spush`,`/chase`, read-only
  `/cur`,`/fixes`,`/groups`,`/presets`,`/scenes`).
- Fase 2: antrean serial + validasi bridge + audit log + dead-man
  switch (hold state saat API timeout) + manual override lock 10 s.
- Fase 3: manual-assist (prompt->tool calls) dulu, lalu semi-auto
  (keputusan tiap 10-30 s, prompt kecil). Biaya ~3M token/show 3 jam.
- Fase 4 opsional hybrid: DSP lokal tangkap beat, API arah artistik
  per segmen lagu.

### Penyempitan kebutuhan: AI hanya ganti scene saat drop/transisi
- LLM/API keluar dari jalur pelatuk (drop butuh <300 ms, API 0.5-3 s).
- Arsitektur arm-fire: DSP deteksi buildup (5-30 s lead time) -> arm
  (pilih scene berikut: tabel statis / model lokal / LLM API opsional
  dgn fallback), DSP deteksi drop -> fire `POST /splay` langsung
  (fade transisi via presetFadeMs di firmware).
- Detektor: RMS per band + spectral flux + onset density; drop = bass
  jump >X median + flux tinggi, wajib didahului buildup; transisi lagu
  = silence >1 s / tempo delta >15% / spectral novelty. Cooldown 8-15 s.
- Fase 1 tanpa AI (tabel siklus scene, tanpa perubahan firmware),
  Fase 2 model lokal dari log + koreksi operator, Fase 3 LLM hanya
  untuk arm saat buildup. Dead-man switch + POST migration tetap
  daftar v47 opsional.

## Session 51 - 2026-08-27 - v46: Copy-on-write preset untuk proteksi scene

### Scope
User melaporkan scene ikut berubah ketika preset yang direferensikannya
direkam ulang (warna baru) atau fade/hold-nya diubah. Audit menemukan:
scene menyimpan NOMOR preset (1 byte/step), playback membaca data preset
LIVE. Kasus "hapus preset" sudah aman (used=0, data utuh). Kasus "ganti
data/fade/hold" masih menular ke scene.

### Fix: copy-on-write slot bayangan
- `presetSceneRefCount(idx)`: hitung referensi scene ke slot preset.
- `cowShadowPreset(idx)`: salin chunk lama ke slot bayangan (used=0, tidak
  direferensikan scene lain), alihkan semua referensi scene ke slot
  bayangan, naikkan `sceneRev`. Return `COW_FULL` bila slot bebas habis.
- `capturePreset()` (REKAM WebUI, `/psave`, serial `REC`): COW dulu bila slot
  direferensikan scene; gagal COW -> rekam DITOLAK (data lama utuh), HTTP 507
  `shadow_full` / serial `{"ok":false,"err":"shadow_full"}`.
- `/psetfade` dan serial `PFH`: COW dulu sebelum menulis fade/hold baru.
- `importJson()`: COW per-slot sebelum menimpa data dari file.
- `capturePreset()` kini mengembalikan `bool`; semua pemanggil diperbarui.

### Fix: sinkronisasi scene lintas-client
- Variabel baru `sceneRev` di firmware, di-increment di semua mutasi scene:
  COW, SPUSH/SPOP/SCLR (HTTP + serial).
- `buildStateJson()` menyertakan `"sceneRev"`; WebUI `syncFromServer()`
  membandingkan `lastSceneRev` dan me-reload `/scenes` saat berubah.
- Ini menutup stale scene data ketika client lain (desktop) memicu COW.

### Catatan
- `BUILD_TAG` naik ke `v46`.
- Format NVS tidak berubah (`STORAGE_VER` tetap 8): slot bayangan = preset
  biasa ber-flag used=0; scene menyimpan nomornya seperti biasa.
- `pclear`/`PDEL` tetap aman tanpa COW (data tidak dihapus).
- Batas: maksimum slot bayangan = jumlah slot kosong; penuh -> operasi
  ditolak dengan pesan jelas, tidak ada korupsi senyap.
- Desktop app tidak butuh perubahan protokol (scene tetap array nomor
  preset; respons error baru `shadow_full` opsional untuk ditampilkan).
- Tidak ada build C++ sesuai aturan proyek. Compile + upload via Arduino IDE.

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
