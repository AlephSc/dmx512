# DMX512 Controller — Desktop .exe

Kontroler GUI Windows untuk ESP32 DMX512 console.

## Prasyarat

- Windows 10/11 64-bit
- Python 3.10 atau lebih baru (pastikan ada di PATH)
- Firmware ESP32 versi **v38** ke atas

## Cara pakai (langsung pakai exe)

1. Jalankan `DMX512Controller.exe` (atau hasil build folder/dist).
2. Pilih COM port USB ke ESP32 → tombol **SAMBUNG**.
3. Gunakan tab:
   - **Mixer:** master, strobe, blackout, fader grup/fixture.
   - **Preset:** rekam/hapus preset, atur fade/hold.
   - **Scene:** pilih, mainkan, tambahkan langkah (step) dari preset.
   - **Sistem:** SAVE DATA (NVS), LOAD DATA, EXPORT preset ke file JSON.
4. Shortcut keyboard:
   - **Space** = BLACKOUT (ALL off)
   - **Esc** = STOP SCENE (SSTOP)
5. Sinkron dua arah: jika Anda menggeser fader di browser (WiFi), tampilan `.exe` ikut berubah dalam ≤250 ms — dan sebaliknya.

## Cara build sendiri

```bat
cd desktop
build.bat
```

Atau manual:
```
pip install PySide6 pyserial pyinstaller
python -m PyInstaller --noconfirm --onefile --windowed --name DMX512Controller main.py
```

Hasil: `dist\DMX512Controller.exe`.

## Protokol serial (ESP32 firmware v38+)

Perintah dikirim sebagai satu baris teks dengan `\n`. Semua respons adalah JSON.

| Perintah | Contoh | Deskripsi |
|---|---|---|
| GET | `GET` | Balas state realtime (master,strb,chase,selectedPreset,dll) |
| LISTF | `LISTF` | Daftar fixture (untuk render mixer otomatis) |
| LISTG | `LISTG` | Daftar grup fader |
| LISTP | `LISTP` | Metadata preset (used, warna preview, f/h) |
| LISTS | `LISTS` | Array scene 20×30 langkah |
| MAST \<v\> | `MAST 200` | Master dimmer |
| STRB \<v\> | `STRB 128` | Strobe master |
| SET \<fi\_c\>=\<v\> | `SET 0_1=255` | Set channel tunggal |
| GRP \<i\> \<v\> | `GRP 3 192` | Grup fader (tipe+offset) |
| PSL \<n\> | `PSL 5` | Play/load preset n |
| SELP \<n\> | `SELP 3` | Select preset (tanpa apply) |
| REC \<n\> idim f h | `REC 7 0 600 1500` | Rekam output saat ini ke preset n (fade/hold ms) |
| PFH \<n\> f h | `PFH 5 500 1200` | Ubah fade/hold preset n saja |
| PDEL \<n\> | `PDEL 4` | Sembunyikan preset (used=0) |
| SPLAY \<n\> | `SPLAY 2` | Mainkan scene n |
| SELS \<n\> | `SELS 8` | Select scene n |
| SSTOP | `SSTOP` | Hentikan scene |
| SPUSH \<s\> \<p\> | `SPUSH 1 5` | Tambah step ke scene s dengan preset p |
| SPOP \<s\> | `SPOP 3` | Hapus langkah terakhir scene s |
| SCLR \<s\> | `SCLR 5` | Kosongkan scene s |
| CHASE on/off | `CHASE on` | Aktifkan/hentikan chase |
| ALL on/off | `ALL off` | Blackout (0) / PAR Full (PAR only 255) |
| SAVE | `SAVE` | Simpan NVS (setelah perubahan >60s juga auto-save) |
| LOAD | `LOAD` | Muat ulang snapshot NVS |
| EXPORT | `EXPORT` | Export semua preset lengkap (JSON besar) |

Catatan penting:

- Semua perintah menulis layer `manualWant[]` atau `pbWant[]`, lalu mixer HTP/LTP menentukan siapa menang.
- Snap `out[ch]=want[ch]` diterapkan pada `SET` & `GRP` agar slider terasa langsung (tanpa fade).
- Firmware tidak menyimpan setiap gerakan slider; hanya saat tombol **SAVE**, REC/PDEL/scene edit, atau auto-save 60 detik.

## Catatan teknis

- Aplikasi desktop dibuat dengan **PySide6** (Qt for Python).
- Thread worker melakukan polling GET setiap 250 ms untuk sinkronisasi real-time dua arah.
- Active control tracking (keyboard/fader yang sedang digeser) menghindari "perang" update server.
- Protocol serial sangat ringan — tidak perlu library tambahan kecuali `pyserial`.

## Repositori

- Source code: https://github.com/AlephSc/dmx512
- Firmware utama: `dmx_web_rgb/dmx_web_rgb.ino`
- Desktop app: folder `desktop/`

## Versi firmware minimal: v38

Karena fitur seperti `ALL` (blackout/par full), paritas presisi REC/PFH (ms), serta metadata command (`LISTF/LISTG`) tersedia sejak v38. Pastikan firmware terupdate sebelum membangun desktop atau menggunakan .exe.
