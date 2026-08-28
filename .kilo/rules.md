# Aturan Proyek DMX512

1. Jangan pernah build program C++ kita (compile/upload via Arduino IDE oleh user).
2. Buat 1 file untuk sesi chat, changelogs dan konteks yang sudah dikerjakan
   sebelumnya — namanya `docs/logs.md` (satu file berkelanjutan, entri sesi
   terbaru di atas).
3. Repo ini adalah repositori GitHub → **setiap selesai satu fitur atau
   perbaikan (dan sudah diverifikasi/di-review), WAJIB commit + push ke
   GitHub**, berfungsi sebagai backup dan rollback point:
   - Commit tematik per fitur/perbaikan (bukan tumpukan multi-sesi).
   - Pesan commit ringkas + daftar perubahan (gaya konvensional yang sudah
     dipakai: `feat(...)`, `fix`, `docs`).
   - Push segera setelah commit; jangan biarkan pekerjaan menumpuk lokal.
   - Jangan commit file rahasia/kredensial; file kerja sementara
     (artefak uji) konfirmasi dulu ke user atau tambahkan ke .gitignore.
4. Sebelum commit: cek `git status`, `git diff --check`, pastikan hanya file
   yang memang berubah yang di-stage (tidak menyeret file unknown milik user).
