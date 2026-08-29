# Aturan Proyek DMX512

1. Tidak boleh build program C++ sendiri (compile/upload via Arduino IDE oleh user).
2. Repo ini adalah repositori GitHub → **setiap selesai satu fitur atau
   perbaikan (dan sudah diverifikasi/di-review), WAJIB commit + push ke
   GitHub**, berfungsi sebagai backup dan rollback point:
   - Commit tematik per fitur/perbaikan (bukan tumpukan multi-sesi).
   - Pesan commit ringkas + daftar perubahan (gaya konvensional yang sudah
     dipakai: `feat(...)`, `fix`, `docs`).
   - Push segera setelah commit; jangan biarkan pekerjaan menumpuk lokal.
   - Jangan commit file rahasia/kredensial; file kerja sementara
     (artefak uji) konfirmasi dulu ke user atau tambahkan ke .gitignore.
3. Sebelum commit: cek `git status`, `git diff --check`, pastikan hanya file
   yang memang berubah yang di-stage (tidak menyeret file unknown milik user).
