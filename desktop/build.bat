@echo off
:: Build DMX512 Controller (.exe) dari source Python.
:: Butuh Python 3.9+ di PATH. Lihat requirements.txt untuk pin versi PySide6.
cd /d "%~dp0"
python -m pip install -r requirements.txt pyinstaller --quiet
python -m PyInstaller --noconfirm --clean DMX512Controller.spec
if errorlevel 1 goto :error
echo.
echo ==================================================
echo BERHASIL: dist\DMX512Controller\DMX512Controller.exe
echo (salin SELURUH folder DMX512Controller, bukan cuma exe-nya)
echo.
pause
goto :end
:error
echo.
echo BUILD GAGAL! Cek pesan error di atas.
pause
:end
