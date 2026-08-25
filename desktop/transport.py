# Transport serial ke ESP32 DMX console (protokol baris, respons JSON).
# Firmware v35+: semua perintah = satu baris teks diakhiri '\n';
# ESP32 selalu membalas tepat satu baris JSON.
import json
import threading

import serial
import serial.tools.list_ports

from PySide6.QtCore import QObject, Signal

# VID chip USB-UART yang umum dipakai ESP32 dev board
_KNOWN_VIDS = {0x1A86, 0x10C4, 0x0403, 0x303A, 0x2341, 0x04D8, 0x10C4}


def list_candidate_ports():
    """Daftar COM port; port dengan VID dikenal diurutkan paling atas.

    Return: list of (device, description, is_known)
    """
    found = []
    for p in serial.tools.list_ports.comports():
        found.append((p.device, p.description or "", (p.vid or 0) in _KNOWN_VIDS))
    found.sort(key=lambda x: (not x[2], x[0]))
    return found


class SerialTransport(QObject):
    """Satu koneksi serial + request/response ber-lock.

    Thread pembaca menangkap baris demi baris; request() mana pun yang sedang
    menunggu akan menerima baris JSON berikutnya. Aman untuk satu produsen
    perintah (worker/UI) karena semua request diserialkan lewat lock.
    """

    connection_changed = Signal(bool)
    error_occurred = Signal(str)

    def __init__(self):
        super().__init__()
        self._ser = None
        self._running = False
        self._reader = None
        self._lock = threading.Lock()
        self._resp_event = threading.Event()
        self._resp = None

    # ---- lifecycle -------------------------------------------------------
    def open(self, port: str) -> bool:
        try:
            self._ser = serial.Serial(port, 115200, timeout=0.05)
        except Exception as e:  # noqa: BLE001
            self.error_occurred.emit(f"Tidak bisa buka {port}: {e}")
            return False
        self._running = True
        self._resp_event.clear()
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()
        self.connection_changed.emit(True)
        return True

    def close(self):
        self._running = False
        if self._reader is not None:
            self._reader.join(timeout=1.0)
            self._reader = None
        if self._ser is not None:
            try:
                self._ser.close()
            except Exception:  # noqa: BLE001
                pass
            self._ser = None
        self.connection_changed.emit(False)

    @property
    def connected(self) -> bool:
        return self._ser is not None and self._running

    # ---- reader thread ---------------------------------------------------
    def _read_loop(self):
        buf = ""
        while self._running and self._ser is not None:
            try:
                data = self._ser.read(512)
            except Exception as e:  # noqa: BLE001
                if self._running:
                    self.error_occurred.emit(f"Serial read error: {e}")
                    self._running = False
                    self.connection_changed.emit(False)
                return
            if not data:
                continue
            buf += data.decode("utf-8", "ignore")
            while "\n" in buf:
                line, buf = buf.split("\n", 1)
                line = line.strip()
                if not line:
                    continue
                try:
                    parsed = json.loads(line)
                except Exception:  # noqa: BLE001
                    continue  # banner boot / teks non-JSON -> abaikan
                self._resp = parsed
                self._resp_event.set()

    # ---- request/response --------------------------------------------------
    def request(self, cmd: str, timeout: float = 3.0):
        """Kirim satu perintah, tunggu balasan JSON. Return dict/list/None."""
        if not self.connected:
            return None
        with self._lock:
            self._resp = None
            self._resp_event.clear()
            try:
                self._ser.write((cmd + "\n").encode())
            except Exception as e:  # noqa: BLE001
                self.error_occurred.emit(f"Serial write error: {e}")
                return None
            if self._resp_event.wait(timeout):
                return self._resp
        return None
