# Entry point build .exe. Membungkus import main() dengan crash logger
# supaya kegagalan startup terekam ke file, bukan hilang di popup kosong.
import os
import sys
import traceback


def _log_path():
    try:
        base = os.path.join(os.environ["LOCALAPPDATA"], "DMX512Controller")
    except KeyError:
        base = os.path.dirname(os.path.abspath(__file__))
    return os.path.join(base, "crash.log")


def _write_crash(text):
    path = _log_path()
    try:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "a", encoding="utf-8") as f:
            f.write(text)
    except OSError:
        pass


def main():
    try:
        import main as app
    except Exception:
        tb = traceback.format_exc()
        _write_crash("\n=== CRASH import (%s) ===\n%s\n" % (sys.version, tb))
        if getattr(sys, "frozen", False):
            # windowed exe: tanpa log, error ini tak terlihat sama sekali
            try:
                from PySide6.QtWidgets import QMessageBox

                _ = QApplication  # noqa: F821 - pastikan gagal jelas jika runtime pun rusak
            except Exception:
                pass
            raise SystemExit(
                "Startup gagal (import). Lihat %s" % _log_path()
            )
        raise
    try:
        app.main()
    except Exception:
        _write_crash("\n=== CRASH runtime ===\n%s\n" % traceback.format_exc())
        raise


if __name__ == "__main__":
    main()
