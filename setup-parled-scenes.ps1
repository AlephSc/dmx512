# Setup 5 scene PARLED (kanan d001, tengah d010, kiri d019) di ESP32.
# Jalankan SETELAH import parled-3-scene.json lewat DMX512Controller / WebUI.
# Pakai: powershell -ExecutionPolicy Bypass -File setup-parled-scenes.ps1
param(
  [string]$Ip = $(Read-Host "IP ESP32 (mis. 192.168.4.1)")
)
$base = "http://$Ip"
$ErrorActionPreference = "Stop"

function Api($path) {
  try { Invoke-RestMethod -Uri "$base$path" -TimeoutSec 5 | Out-Null }
  catch { Write-Host "GAGAL: $path -> $($_.Exception.Message)" -ForegroundColor Red }
}

Write-Host "== Kosongkan scene 1-5 =="
1..5 | ForEach-Object { Api "/sclear?s=$_" }

Write-Host "== Chase RGB 3-way (Scene 1) =="
$s1 = (1,4,7, 2,5,8, 3,6,9) * 5
$s1 | ForEach-Object { Api "/spush?s=1&p=$_" }

Write-Host "== Chase RGB zig-zag (Scene 2) =="
$s2 = (3,5,7, 2,4,9, 1,6,8) * 5
$s2 | ForEach-Object { Api "/spush?s=2&p=$_" }

Write-Host "== Color cycle halus (Scene 3) =="
$s3 = (10, 11, 12, 10, 14) * 4
$s3 | ForEach-Object { Api "/spush?s=3&p=$_" }

Write-Host "== Chase warm/cool (Scene 4) =="
$s4 = (15,16,17, 18,19,20, 13, 17, 18) * 5
$s4 | ForEach-Object { Api "/spush?s=4&p=$_" }

Write-Host "== Strobe blip (Scene 5) =="
$s5 = (10, 13) * 8 + 10
$s5 | ForEach-Object { Api "/spush?s=5&p=$_" }

Write-Host ""
Write-Host "Selesai. Mainkan: /splay?s=1 .. /splay?s=5 | Stop: /splay?off=1"
Write-Host "Cek isi scene: $base/scenes"
