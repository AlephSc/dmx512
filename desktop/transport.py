# Transport ke ESP32 DMX console. Dua jalur dengan interface IDENTIK:
# 1) SerialTransport  — USB serial (protokol baris, firmware v35+)
# 2) HttpTransport    — WiFi HTTP (endpoint REST yang sama dengan Web UI, v40+)
# Worker/UI tidak perlu tahu transport mana yang aktif.
#
# v43-latency: dua metode kirim di kedua transport:
#   request(cmd) = kirim + tunggu balasan   (perintah data: GET/LIST*/EXPORT)
#   send(cmd)    = kirim tanpa menunggu     (kontrol fader: SET/MAST/STRB/GRP)
# Fire-and-forget menghapus antrean round-trip yang membuat fader terasa delay.
import http.client
import json
import threading
import urllib.parse
import urllib.request
import uuid

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

    @property
    def is_http(self) -> bool:
        return False

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

    def send(self, cmd: str):
        """Fire-and-forget: tulis perintah tanpa menunggu balasan.
        Dipakai utk kontrol kontinu (fader) agar tidak ada delay round-trip."""
        if not self.connected:
            return
        with self._lock:
            try:
                self._ser.write((cmd + "\n").encode())
            except Exception as e:  # noqa: BLE001
                self.error_occurred.emit(f"Serial write error: {e}")


class HttpTransport(QObject):
    """Transport WiFi (HTTP) — paritas penuh dengan Web UI + serial v35+.

    v43-latency: koneksi keep-alive persisten (http.client) — tanpa handshake
    TCP baru per request, memangkas 30-80 ms latensi WiFi per perintah.
    Perintah serial-style diterjemahkan ke URL GET/POST.
    """

    connection_changed = Signal(bool)
    error_occurred = Signal(str)

    def __init__(self):
        super().__init__()
        self.host = None           # "192.168.0.12"
        self.base = None           # "http://192.168.0.12" (kompatibilitas lama)
        self._connected = False
        self._conn = None          # HTTPConnection keep-alive persisten
        self._conn_lock = threading.Lock()

    # ---- lifecycle -------------------------------------------------------
    def open(self, ip: str) -> bool:
        ip = ip.strip()
        if not ip:
            return False
        self.host = ip
        self.base = f"http://{ip}"
        try:
            status, body = self._http("GET", "/health", timeout=3)
            data = json.loads(body.decode())
            if status != 200 or not data.get("ok"):
                raise RuntimeError("health tidak ok")
        except Exception as e:  # noqa: BLE001
            self.error_occurred.emit(f"Tidak bisa terhubung ke {ip}: {e}")
            self._conn = None
            return False
        self._connected = True
        self.connection_changed.emit(True)
        return True

    def close(self):
        self._connected = False
        with self._conn_lock:
            if self._conn is not None:
                try:
                    self._conn.close()
                except Exception:  # noqa: BLE001
                    pass
                self._conn = None
        self.connection_changed.emit(False)

    @property
    def connected(self) -> bool:
        return self._connected

    @property
    def is_http(self) -> bool:
        return True

    # ---- http inti (keep-alive, sekali retry bila koneksi basi) -----------
    def _http(self, method, path, timeout=5.0, body=None, headers=None):
        with self._conn_lock:
            if self._conn is None:
                self._conn = http.client.HTTPConnection(self.host, timeout=timeout)
            for attempt in (1, 2):
                try:
                    self._conn.request(method, path, body=body, headers=headers or {})
                    r = self._conn.getresponse()
                    return r.status, r.read()
                except Exception:  # noqa: BLE001
                    if attempt == 2:
                        raise
                    try:
                        self._conn.close()
                    except Exception:  # noqa: BLE001
                        pass
                    self._conn = http.client.HTTPConnection(self.host, timeout=timeout)

    # ---- request/response ----------------------------------------------
    def request(self, cmd: str, timeout: float = 3.0):
        """Kirim satu perintah, tunggu balasan JSON (atau terakhir bila multi-step)."""
        if not self._connected:
            return None
        steps = self._translate(cmd)
        if steps is None:
            self.error_occurred.emit(f"Perintah tidak didukung via WiFi: {cmd}")
            return None
        last = None
        for method, path in steps:
            try:
                status, body = self._http(method, path, timeout=timeout)
                last = json.loads(body.decode())
            except Exception as e:  # noqa: BLE001
                self.error_occurred.emit(f"HTTP {path}: {e}")
                return None
        return last

    def send(self, cmd: str):
        """Fire-and-forget via WiFi: jalankan di thread latar singkat agar
        UI/worker tidak menunggu round-trip HTTP sama sekali."""
        if not self._connected:
            return
        steps = self._translate(cmd)
        if steps is None:
            return

        def _fire():
            for method, path in steps:
                try:
                    self._http(method, path, timeout=2.5)
                except Exception:  # noqa: BLE001
                    return   # kontrol sesaat gagal = tidak fatal; state sync menyusul

        threading.Thread(target=_fire, daemon=True).start()

    def _translate(self, cmd):
        """Translate → [(method, path), ...]. Return None bila tidak didukung."""
        parts = cmd.strip().split(" ")
        op = parts[0].upper()
        if op == "GET": return [("GET", "/cur")]
        if op == "LISTF": return [("GET", "/fixes")]
        if op == "LISTG": return [("GET", "/groups")]
        if op == "LISTP": return [("GET", "/presets")]
        if op == "LISTS": return [("GET", "/scenes")]
        if op == "EXPORT": return [("GET", "/export")]
        if op == "SAVE": return [("POST", "/save")]
        if op == "LOAD": return [("GET", "/loaddata")]
        if op == "MAST": return [("GET", f"/ctrl?mast={parts[1]}")]
        if op == "STRB": return [("GET", f"/ctrl?strb={parts[1]}")]
        if op == "ALL": return [("GET", f"/ctrl?all={parts[1].lower()}")]
        if op == "SET": return [("GET", f"/set?{parts[1]}")]
        if op == "GRP": return [("GET", f"/grp?i={parts[1]}&v={parts[2]}")]
        if op == "PSL": return [("GET", f"/select?p={parts[1]}"), ("GET", f"/pload?n={parts[1]}")]
        if op == "SELP": return [("GET", f"/select?p={parts[1]}")]
        if op == "SELS": return [("GET", f"/select?s={parts[1]}")]
        if op == "REC": return [("GET", f"/psave?n={parts[1]}&idim={parts[2]}&f={parts[3]}&h={parts[4]}")]
        if op == "PFH": return [("GET", f"/psetfade?n={parts[1]}&f={parts[2]}&h={parts[3]}")]
        if op == "PDEL": return [("GET", f"/pclear?n={parts[1]}")]
        if op == "SPLAY": return [("GET", f"/splay?s={parts[1]}")]
        if op == "SSTOP": return [("GET", "/splay?off=1")]
        if op == "SPUSH": return [("GET", f"/spush?s={parts[1]}&p={parts[2]}")]
        if op == "SPOP": return [("GET", f"/spop?s={parts[1]}")]
        if op == "SCLR": return [("GET", f"/sclear?s={parts[1]}")]
        if op == "CHASE": return [("GET", "/chase?on=1" if parts[1].lower() == "on" else "/chase?off=1")]
        if op == "WIFIST": return [("GET", "/wifistat")]
        if op == "WIFIS":
            ssid = urllib.parse.quote(cmd.split(" ", 2)[1] if len(cmd.split(" ", 2)) > 1 else "")
            rest = cmd.split(" ", 2)
            pw = urllib.parse.quote(rest[2]) if len(rest) > 2 else ""
            return [("GET", f"/wifiset?ssid={ssid}&pass={pw}")]
        return None

    # ---- import file via HTTP multipart POST ---------------------------
    def import_file(self, path) -> bool:
        """Upload file JSON lewat /import (multipart) — lebih cepat daripada batch serial."""
        try:
            with open(path, "rb") as f:
                content = f.read()
        except Exception as e:  # noqa: BLE001
            self.error_occurred.emit(f"Buka file gagal: {e}")
            return False
        boundary = uuid.uuid4().hex
        crlf = "\r\n"
        body = (
            f"--{boundary}{crlf}"
            f'Content-Disposition: form-data; name="file"; filename="presets.json"{crlf}'
            f"Content-Type: application/json{crlf}{crlf}"
        ).encode() + content + f"{crlf}--{boundary}--{crlf}".encode()
        try:
            status, _ = self._http(
                "POST", "/import", timeout=15, body=body,
                headers={"Content-Type": f"multipart/form-data; boundary={boundary}"})
            return status == 200
        except Exception as e:  # noqa: BLE001
            self.error_occurred.emit(f"Import HTTP gagal: {e}")
            return False
