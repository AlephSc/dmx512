# Protokol Uji Lengkap DMX512 Controller ESP32

## A. Persiapan Hardware

### 1. Tanpa W5500 (WiFi saja)
- Colok ESP32 via USB ke laptop
- Router: "SIGMA"/"1ngantos12" (atau ganti sesuai kebutuhan di kode)

### 2. Dengan W5500 Ethernet
- Wiring W5500: CS=GPIO5, SCLK=18, MISO=19, MOSI=23, RST/INT bebas
- VCC 3.3V / GND (modul biasanya ada regulator)
- Colok kabel LAN ? router DHCP

### 3. Desktop App
- Jalankan `desktop/dist/DMX512Controller.exe`
- Pilih USB Serial atau WiFi (HTTP)
- Masukkan IP jika WiFi (Ethernet = otomatis dapat dari router)

---

## B. Firmware Upload & Verifikasi Dasar

1. Upload firmware v41 dari file: `dmx_web_rgb/dmx_web_rgb.ino`
2. Buka Serial Monitor (bauds 115200):
   ```
   === DMX Web Console v41 ===
   WiFi: menyambung ke SIGMA... (jika WiFi)
   Ethernet W5500: inisialisasi... (jika W5500 terpasang)
   Ethernet tersambung. IP: http://x.x.x.x  ? perhatikan IP ini!
   DMX task -> Core 0 | WebServer -> Core 1
   ```

3. Jika menggunakan W5500 tapi tidak muncul "Ethernet tersambung":
   - Cek kabel LAN tercolok ? router DHCP aktif
   - Tunggu maksimal 3 detik untuk DHCP
   - Bisa manual restart/reload dengan tombol reset ESP32

---

## C. UJU WEB UI (Browser ke IP ESP32)

### 1. Kontrol Manual
- Slider Master ? LED indicator harus mengikuti 0–255
- Strobe slider ? output strobe kedip sesuai delay
- Klik blackout ? semua lampu mati
- Klik PAR Full ? hanya PAR yang full (aman untuk moving head)

### 2. Fader Bank
- Gerakkan grup fader (PAR Dim/R/G/B, MH Dim, dll.) ? fixture terkait ikut berubah
- Sinkron OK: geser dari web lalu buka desktop app (via USB/WiFi) ? slider desktop ikut bergerak

### 3. Preset
- Klik pad preset (misal preset #1) ? preview warna muncul di pad
- Geser fader ? klik "Update F/H" untuk simpan timing
- Klik "REKAM" ? preset baru tersimpan di NVS
- Klik "HAPUS" ? preset disembunyikan (data tetap ada)
- Export file `.json`: tombol EXPORT ? download `dmx-presets.json`

### 4. Scene
- Aktifkan EDIT MODE ? klik scene pad ? tidak ada output
- Klik tab PRESET ? klik pad untuk tambah langkah ke scene terpilih
- Tombol "+ Preset terpilih", "Hapus akhir", "Kosongkan" ? edit step
- ? Cek ? play scene terpilih untuk tes tanpa mode SHOW
- Aktifkan SHOW MODE ? klik scene langsung main; klik lagi ? stop

### 5. Save Data
- Setelah ubah banyak (preset/scene) ? klik SAVE DATA (NVS)
- Reboot ESP32 ? load data lama masih ada

### 6. Import JSON
- EXPORT dari device lain ? simpan file
- IMPORT dari file ? upload via browser (multipart POST /import)
- Refresh presets list ? should see imported data

---

## D. UJI DESKTOP APP (`DMX512Controller.exe`)

### 1. Mode USB Serial
- Pilih port COM ? SAMBUNG
- Semua kontrol sama seperti Web UI: master/strobe/fixture sliders
- Shortcut keyboard: Space (blackout), Esc (stop scene)

### 2. Mode WiFi (HTTP/Ethernet)
- Pilih WiFi (HTTP) ? ketik IP ESP32 (bisa dari terminal/firmware boot msg)
- SAMBUNG ? sama seperti mode USB tapi via TCP/IP
- IMPORT via HTTP (multipart) ? lebih cepat (~5 detik vs ~15 detak batch serial)

### 3. Sync Dua Arah
- Ubah fader di Web UI ? slider desktop ikut gerak dalam =250 ms
- Ubah slider desktop ? Web UI ikut update dalam =250 ms
- Edit fade/hold timing ? spinbox desktop dan Web UI sinkron (selama tidak sedang diedit)

### 4. Scene Editing & Testing
- TAB Scene ? EDIT MODE ? klik scene pad (tanpa output)
- TAB Preset ? klik pad ? SPUSH ke scene terpilih
- ? Cek ? play scene terpilih, jadi bisa test tanpa masuk SHOW MODE
- Saat play ? tombol berubah menjadi STOP ? klik untuk stop

---

## E. UJI ETHERNET W5500

### 1. Kabel Langsung Router
- Pasang ESP32 via LAN ke router
- Firmware akan auto-connect via DHCP
- Browser/desktop via HTTP ke IP dari DHCP

### 2. Latensi & Keandalan
- Tes: ubah slider sangat cepat 10x ? pastikan server response konsisten
- Cabut kabel LAN ? device fallback ke WiFi SIGMA
- Cabut keduanya ? fallback AP darurat (SSID: DMX-RGB)

### 3. Jarak Test
- Laptop via WiFi ke router ? kontrol ESP32 via Ethernet ? latensi stabil
- Catat latency dari desktop app ? seharusnya <10ms untuk UDP packet

---

## F. Verifikasi Paritas Full (Web vs Desktop)

| Fitur | Web UI | Desktop | Status |
|---|---|---|---|
| All commands | ? | ? | ? |
| HTP/LTP channel logic | ? | ? | ? |
| EDIT/SHOW mode scene | ? | ? | ? |
| ? Cek scene test | ? | ? | ? |
| Import/Export JSON | ? | ? | ? |
| Save/Load NVS | ? | ? | ? |
| Fade/Hold timing | ? | ? | ? |
| Sync dua arah | ? | ? | ? |
| USB connection | ? | ? | ? |
| WiFi/HTTP connection | ? | ? | ? |
| Batch import serial | ?* | ?* | ? |
| Fast multipart import | ? | ? | ? |

* Via HTTP (web) atau HTTP transport (desktop) ? jauh lebih cepat dari batch serial

---

## G. Troubleshooting Ringkas

| Masalah | Kemungkinan Penyebab | Solusi |
|---|---|---|
| Tidak dapat terhubung via serial | Port salah/USB cable buruk | Pilih port lain/coba cable berbeda |
| IP tidak muncul di Serial | W5500 tidak terdeteksi | Cek wiring/SPI pins, reboot |
| Timeout saat import | File besar/koneksi lambat | Gunakan HTTP multipart, bukan serial batch |
| LED tidak menyala | DMX wiring salah | Cek TX?DI, RX?RO, DE/RE?GPIO4 |
| Web UI tidak refresh setelah firmware update | Browser cache | Hard-refresh (Ctrl+F5) |
| Desktop app crash saat start | DLL Qt/shiboken error | Reinstall PySide6/PyInstaller |
| Ethernet link active tapi no IP | DHCP timeout | Cek router DHCP range/lease time |

---

## H. Checklist Produk Jual

? Firmware siap: v41 (serial parity + HTTP endpoints + W5500)  
? Desktop app ready: executable build + install script  
? Dokumentasi: README, protokol serial, testing guide  
? GUI parity penuh antara Web UI dan Desktop  
? Dual transport: USB (dekat) + Ethernet/WiFi (jarak jauh)  

**Catatan penting untuk produksi**:
- Pastikan casing ESP32 dilindungi + ventilasi cukup
- W5500 module sebaiknya dipasangkan di PCB kecil terpisah untuk reliability
- Power supply ESP32 harus >1A peak untuk avoid brown-out saat many fixtures

@END
