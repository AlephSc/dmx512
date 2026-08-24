# DESAIN — DMX Web RGB Controller

## Arah visual

Konsol pencahayaan (lighting console) adalah alat kreatif yang wajar ber-tema gelap. Dark default bukan tren, melainkan alasan fungsi: mengurangi silau di panggung/ruang gelap saat operator membaca layar. Ini alasan sah di luar "biar terlihat tech".

- **Tone:** gelap netral hangat (`#15171a`), bukan hitam murni. Lapisan melayang (`#1e2227`) lebih terang.
- **Aksen:** satu warna untuk seluruh interaksi pengeditan, kuning lampu `#ffb400`. Kawasan "ON/OFF" memakai penghuni (interactive) hijau/merah laba-laba terpisah.
- **MOTION minimal:** hanya hover + transisi warna slider. Tanpa animasi onboarding.

## Dial

ENERGY 1 / RHYTHM 2 / MOTION 1

## Fokus per layar

Satu tugas utama: atur warna + kecerahan PAR LED. Slider adalah subjek utama. Status koneksi DMX + IP kecil di header, tidak bersaing.

## Bahasa

UI dalam Bahasa Indonesia (pengguna menulis dalam bahasa Indonesia).

## Alasan pilihan kunci

- **Slider besar & warna per channel:** beats; pada layar kecil target sentuh ≥44px, di desktop slider lebar untuk presisi.
- **Live preview swatch:** umpan balik warna RGB yang segera, membantu operator sebelum melihat lampu.
- **Dimmer terpisah 0-100%:** mencerminkan footprint nyata CH1=Master Dimmer.
- **Situs tanpa framework:** teks + CSS vanilla untuk alasan simpel: muat cepat di HP, tanpa dependensi build.