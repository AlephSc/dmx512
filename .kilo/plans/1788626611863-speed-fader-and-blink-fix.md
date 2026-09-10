# Plan v51.3: Floor durasi langkah 25ms (frame-safe speed clamp)

## Latar (pertanyaan user + analisis)
User bertanya: bisakah fader speed membuat output melebihi kecepatan DMX, dan apakah
melebihi kecepatan transmit kabel berdampak?

**Kesimpulan analisis (penting, jangan diubah jadi guard yang salah arah):**
- Laju frame DMX dikunci `vTaskDelayUntil(&lastWake, 25ms)` di `dmxTask`
  (dmx_web_rgb.ino ~3752). TIDAK dipengaruhi speedMul — speed hanya memperpendek
  durasi TAHAN langkah (isi frame), bukan laju transmisi. Standar USITT/ESTA
 250kbps / 512ch ≈ 22,5ms + break = 40 fps maks — tidak mungkin terlampaui.
- Kabel/fixture tidak bisa "kecepatan berlebih" oleh fader speed. Tidak ada
  perubahan yang diperlukan untuk melindungi output.

**Satu masalah nyata yang tersisa (dikonfirmasi user untuk diperbaiki):**
- Speed ×5 + hold 100ms → langkah diminta tiap 20ms, tapi dmxTask tick 25ms →
  `timeReached` baru benar di tick berikutnya → langkah efektif 25ms, bukan 20ms.
  Aman (self-limiting, maks 1 langkah per tick) tapi fader "berbohong": ×5 pada
  hold 100ms efektifnya ×4. Lebih buruk: deadline 20ms jatuh DI ANTARA tick →
  langkah efektif bisa 2 tick (50ms = jitter) — floor 25ms juga menghilangkan
  jitter ini.

**Q: apakah frame menumpuk / delay saat pergantian? A: tidak.**
- dmxTask = loop perioda tetap: satu frame per tick, sinkron, tanpa antrean.
  `sceneNextAt` hanya deadline pemilihan preset (isi frame), bukan penundaan
  kirim. Percepatan speed mengubah ISI lebih sering — laju transmisi tetap
 40 fps oleh `vTaskDelayUntil`.
- Tick terlambat (NVS dsb) tidak menimbulkan "utang" tick — berikutnya hanya
  datang terlambat sekali (stutter, terukur DMXSTAT), tidak diakumulasi.
- Latensi pergantian langkah = ≤25ms (satu frame) + fade. Floor 25ms =
  laju langkah tepat laju frame = kasus paling bersih, menghapus tick kosong.

## Keputusan desain (dikonfirmasi user)
- Tambahkan **floor 25ms** pada durasi langkah efektif: fader jujur (tidak ada
  nilai speed yang diam-diam tak tercapai), fade minimal = satu frame.
- Ephemeral, tanpa UI baru, tanpa NVS. BUILD_TAG v51.2 → v51.3.

## Implementasi (dmx_web_rgb.ino saja)

### 1. Helper floor (dekat `clampSpeed`, ~baris 282)
```cpp
// v51.3: floor durasi langkah = 1 periode frame DMX (25ms). Speed ekstrem
// dgn hold pendek tidak "berbohong" — langkah tidak pernah diminta lebih
// cepat dari tick dmxTask (self-limit sebelumnya; kini eksplisit & jujur).
static inline uint32_t stepFloor(uint32_t ms){ return (ms<25)?25:ms; }
```

### 2. `sceneTick` (~baris 1400)
```cpp
sceneNextAt=now+stepFloor((uint32_t)(sceneMs/clampSpeed(speedMul)));
```

### 3. `chaseTick` (~baris 1378)
```cpp
chaseNextAt=now+stepFloor((uint32_t)(chaseMs/clampSpeed(speedMul)));
```

### 4. `applyPresetToWant` — sejajarkan floor fade 20ms → 25ms (~baris 1422)
```cpp
uint32_t effDur = (uint32_t)((chaseOn||sceneOn) ? (chaseMs/sm) : chaseMs);
if(effDur < 25) effDur = 25;                  // floor 1 frame (v51.3, dari 20)
```
(catatan komentar lama "snap threshold fadeTick 20" ikut diganti — fade 20ms
tetap snap oleh `fadeMs<=20` check di fadeTick, floor 25 lebih ketat, aman.)

### 5. BUILD_TAG
`v51.2` → `v51.3`.

### 6. docs/logs.md — Session 70 (prepent di atas)
Ringkas: pertanyaan batas kecepatan → analisis (laju frame konstan, kabel aman),
fix floor 25ms agar fader jujur.

## Validasi
1. Compile Arduino IDE (aturan proyek: tidak di-build agent); Serial harus
   tampil `=== DMX Web Console v51.3 ===`.
2. Test fader jujur: scene hold 100ms, speed ×5 → langkah berganti tiap 25ms
   tepat (bukan 20ms bohong; juga tidak ada jitter 25→50ms dari deadline yang
   jatuh di antara tick). `DMXSTAT` tetap min 24-26 / avg ~25 / max <50
   (bukti laju frame tak berubah, tidak ada stacking).
3. Test ekstrem kombinasi: speed ×5 + hold 100ms + fade 600ms → fade di-clamp
   ke 25ms (snap), tiap langkah warna penuh, tidak ada blink tercampur
   (regresi fix v51.2 tidak muncul).
4. `git diff --check` bersih.

## Out of scope
- Apapun yang "membatasi kecepatan kabel" — tidak diperlukan, laju frame
  sudah konstan 25ms oleh design (dan ditulis eksplisit di jawaban analisis).
- Desktop app, strobe master, NVS persistence speed.

## Urutan task
1. stepFloor helper + 3 call site (sceneTick, chaseTick, applyPresetToWant)
2. BUILD_TAG v51.3
3. docs/logs.md Session 70
4. Commit `fix(firmware) v51.3: floor durasi langkah 25ms — fader speed jujur di ekstrem` + push
   (konvensi commit-per-fitur; desktop tetap tidak disentuh)
