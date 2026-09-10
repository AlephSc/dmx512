# Plan: Restrukturisasi Mixer UI — Section per Tipe + Label Channel Lengkap

## Konteks

User ingin (WebUI + Desktop, paritas penuh):

1. **Per fixture punya semua fader groupnya sendiri** — PAR: RGB, Dim, Strobe, Mode, dll; Moving Head: Dim, Pan, Tilt, dll.
2. **Fixture di-sekat per grup tipe** — section "PAR LED" berisi fader bank (group) PAR + fader per fixture PAR; section "MOVING HEAD" berisi fader bank-nya + fader per fixture Moving; dst.
3. **Desktop lebih clean** — fixture yang overflow turun ke bawah (wrap), bukan scroll horizontal.

## Temuan audit (mengubah scope)

Kontrol per-channel per fixture **sudah ada** di kedua UI:
- WebUI: `buildFixes()` (dmx_web_rgb.ino ~1517) — fader per channel, realtime WS.
- Desktop: `build_fixtures()` (desktop/ui/mixer_tab.py:101) — fader per channel, baris per tipe (v45).

Yang bikin fitur terasa hilang = **label channel generik**:
- PAR (t=0): label sudah benar (Dim,R,G,B,Strobe,Mode,Auto,Speed,Aux,R2,G2,B2).
- Moving (t=1) & Beam (t=2): label `CH1..CHn` — tidak ada Pan/Tilt/Dimmer.

**Scope jadi: label chart + restrukturisasi section per tipe. Nol perubahan protokol/endpoint firmware.** Fader tetap menulis `SET fi_c` / `{"t":"s"}` / `GRP` seperti sekarang.

## Keputusan (user sudah menyetujui)

- Label Moving/Beam/CH generik: **chart standar yang saya susun** (bisa di-tweak di kode nanti).
- Layout: section per tipe, **collapsible** (WebUI `<details>`, desktop tombol lipat). Master/Strobe/Blackout/Chase tetap bar atas.
- Desktop: fixture dalam section wrap ke bawah (flow layout), tanpa scroll horizontal.
- Panel "Fader Bank" lama WebUI (`#faderPanel`) dihapus — group fader pindah ke section tipenya masing-masing. Desktop: baris `grp_row` horizontal lama dihapus, diganti embedded.

## Label channel chart (satu sumber kebenaran per platform)

**Moving Head (20ch, t=1):**
```
Pan, PanF, Tilt, TiltF, P/T Spd, Dim, Strobe, ColorSpd,
Gobo, GoboRot, PrismRot, Focus, Zoom, Shutter, Func, Reset, CH19, CH20
```
("F" = fine 16-bit.)

**Beam (16ch, t=2):**
```
Pan, PanF, Tilt, TiltF, P/T Spd, Dim, Strobe, Color, Gobo,
GoboRot, Prism, Focus, Zoom, Shutter, Func, Reset
```

**Strobe (t=3):** Mode,Strobe,Dim,Color (sudah ada).
**Fog (t=4):** Fog,Fan (sudah ada).
**PAR (t=0):** sudah ada, tak diubah.
**Fallback tipe tak dikenal (t>4):** `CH1..CHn` (aman untuk patch custom).

Rule: bila foot < jumlah label, potong; bila foot > jumlah label, sisa = `CHn`.

## Task list

### 1. WebUI — firmware `dmx_web_rgb/dmx_web_rgb.ino` (HTML/CSS/JS di INDEX_HTML)

1a. `labelOf(i)` (~1506): ganti case t=1, t=2 dengan chart di atas. Hapus fallback generik untuk t=1/t=2 (t>4 tetap generik).

1b. HTML: hapus section `#faderPanel` (line ~1417-1421). `#channelPanel` jadi kontainer section per tipe.

1c. `buildFixes()` + fungsi baru `buildSections()`: kelompokkan FIX per type (urutan kemunculan, sama seperti desktop v45). Untuk tiap tipe:
   - `<details class="type-sec" open>` + `<summary>`: `PAR LED · 10 unit · addr 1-90`.
   - Di dalam: sub-baris fader GRUP yang `typeFilter`-nya cocok (dari GRP, render seperti `buildGroups()` lama — id `g<i>`, handler `/grp` sama, sinkron `syncGroups()` tetap jalan).
   - Di bawahnya: fixture box per fixture (`buildFixes()` lama, isi tidak berubah: fader per channel + label baru).
   - Kolom fixture: CSS grid `repeat(auto-fill,minmax(280px,1fr))` — wrap otomatis, no horizontal scroll.

1d. CSS (~1323): `.type-sec{border:1px solid var(--edge);border-radius:8px;margin:8px 0;padding:8px}`; `.type-sec summary{cursor:pointer;font-weight:700;color:var(--muted)}`; `.fixgrd` dalam section pakai auto-fill grid di atas; media query lama (1338, 1350) disesuaikan agar grid section tidak bentrok.

1e. `syncGroups()` (~1563): tidak berubah secara logika (id `g<i>` dipertahankan). `buildGroups()` lama dihapus; panggilan init `buildGroups()` di baris init (~1835) diganti `buildSections()` (yang di dalamnya render group). Periksa tidak ada referensi `$('faderPanel')`/`$('groups')` tersisa di JS (grep sebelum commit).

1f. Mobile order CSS: section per tipe otomatis mengikuti `#channelPanel` (tidak perlu aturan order baru kecuali faderPanel dihapus dari daftar).

### 2. Desktop — `desktop/ui/mixer_tab.py`

2a. `state.py::channel_labels(ftype, foot)`: tambahkan chart t=1, t=2 (identik dengan WebUI — 1a). Fallback t>4 tetap CHn.

2b. `mixer_tab.py::_build()`: hapus baris "FADER GRUP" + `self.grp_row` horizontal. Struktur baru di area scroll:
   - Per tipe (OrderedDict, urutan FIX): `QWidget` section + tombol toggle (QToolButton arrow, `setCheckable`) + header label `— PAR LED (10 unit) —`.
   - Section default: PAR open, tipe lain **collapsed** (10 PAR × 12 fader terlalu tinggi jika semua open).
   - Isi section: (i) baris group fader milik tipe itu (`VFader` kecil, key `grp<i>`, handler GRP sama); (ii) flow-layout fixture: `QHBoxLayout` dalam `QWidget` + `setWrap`? Qt tidak punya flow bawaan — pakai grid `QGridLayout` kolom fix per-lebar (~4 fixture/baris) atau hitung wrap manual; pilih **QGridLayout dengan kolom tetap** (paling sederhana, no custom layout class).
   - Fixture box: sama seperti v45 (nama + rentang addr + fader per channel label baru).
   - Toggle collapse: `section_content.setVisible(bool)`.

2c. `build_groups(groups)` → diubah jadi data-only (simpan `self.groups = groups`), tidak lagi membangun `grp_row`. Rendering group pindah ke `build_fixtures()` (butuh FIX+GRP bersamaan). Handler `apply_state()` loop `self.group_faders` tetap jalan — daftar `group_faders` diisi saat build section.
   - NOTE build order: `main.py` saat ini memanggil `build_groups` lalu `build_fixtures` terpisah (LISTG/LISTF datang beda waktu). Solusi: simpan data di atribut, `build_fixtures()` re-render pakai data terbaru yang tersedia; panggil ulang `build_fixtures` juga saat `build_groups` menerima data baru (re-render murah, <32 fixture).

2d. Rebuild flow: setelah LISTF **atau** LISTG datang → re-build sections penuh (idempotent, widget lama dihapus via `_clear_sub_layout` yang sudah ada).

### 3. Paritas & risiko

- **sync grup**: aturan "set fader grup hanya bila semua member seragam" tetap (WebUI `syncGroups()`, desktop `apply_state()` via `st.group_values(i)`) — tidak berubah.
- **activeKeys proteksi** (fader yang sedang digeser jangan ditimpa sync) — mekanik sama, key `grp<i>`/`fi_c` tetap.
- **`:unknown type`**: patch custom t>4 → section "TIPE X" + label CHn, aman.
- **Foot > label length** (custom patch): sisa label `CHn` — sudah rule.
- **WebUI heap**: jumlah DOM node bertumbuh (section + summary) tapi kecil (<100 node tambahan); `sendUi()` substitution tak berubah — token sama.
- **Dekstop build order race**: covered 2c.

### 4. Out of scope

- Autentikasi HTTP/WS (tetap ditunda per user).
- Migrasi GET→POST mutasi.
- Perubahan endpoint/protokol firmware.
- EXE rebuild (user lakukan sendiri via PyInstaller setelah uji).

## Urutan implementasi

1. `state.py::channel_labels` (desktop, unit-testable)
2. `mixer_tab.py` restructure sections + grid wrap + collapse
3. WebUI `labelOf()` chart
4. WebUI CSS + HTML section per tipe (hapus faderPanel)
5. WebUI JS `buildSections()` (gabung group + fixture render)
6. Grep sisa referensi faderPanel/groups lama → bersih
7. `py_compile` desktop files
8. Update `docs/logs.md` (Session 57)
9. User: compile Arduino IDE + upload + uji dua UI

## Validasi

- Desktop: `python -m py_compile desktop/ui/mixer_tab.py desktop/state.py`
- WebUI: load page → JS error banner harus kosong; tiap section tipe collapsible; label Moving menunjukkan Pan/Tilt/Dim; geser fader group di section PAR mengubah semua PAR; geser fader per fixture hanya fixture itu.
- Sync dua arah: ubah dari WebUI → desktop fader ikut (1,5 s polling), dan sebaliknya.
- Patch custom tipe tak dikenal → section generik tanpa crash.
