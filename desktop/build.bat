@echo off
:: Build DMX512 Controller (.exe) dari source Python (butuh Python 3.10+ di PATH).
cd /d "%~dp0"
python -m pip install -r requirements.txt pyinstaller --quiet
python -m PyInstaller --noconfirm --onefile --windowed ^
    --name DMX512Controller ^
    main.py
if errorlevel 1 goto :error
echo.
echo ======================================
echo BERHASIL: dist\DMX512Controller.exe
echo.
pause
goto :end
:error
echo.
echo BUILD GAGAL! Cek pesan error di atas.
pause
:end
