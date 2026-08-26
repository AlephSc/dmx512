# Worker thread: satu-satunya pihak yang menyentuh port serial.
# Urutan kerja tiap iterasi: (1) proses antrean perintah UI, (2) polling GET.
import queue
import time

from PySide6.QtCore import QObject, Signal


class SerialWorker(QObject):
    state_received = Signal(dict)        # hasil GET (state realtime)
    data_received = Signal(str, object)  # LISTF/LISTG/LISTP/LISTS/EXPORT
    command_done = Signal(str, object)   # cmd, respons JSON (atau None)

    DATA_INTERVAL = 3.0   # detik antar refresh LISTP/LISTS (data struktural)

    # Perintah kontrol kontinu = FIRE-AND-FORGET (tanpa tunggu balasan).
    # Inilah inti perbaikan delay fader desktop: sebelumnya tiap geseran
    # mengantre round-trip 5-15 ms (bisa 2.5 dtk bila ESP32 sibuk tulis NVS).
    # Sinkronisasi nilai tetap dijamin polling GET berkala.
    FIRE_OPS = {"SET", "MAST", "STRB", "GRP", "ALL", "CHASE", "SELP", "SELS", "SSTOP"}

    def __init__(self, transport):
        super().__init__()
        self.transport = transport
        self.cmd_queue = queue.Queue()
        self._running = False

    def start(self):
        self._running = True

    def stop(self):
        self._running = False

    def run(self):
        last_poll = 0.0
        last_data = 0.0
        # WiFi keep-alive cepat -> polling rapat; serial berbagi bandwidth
        # dengan payload GET ~2 KB (~170 ms @115200) -> polling lebih renggang.
        poll_interval = 0.25 if self.transport.is_http else 0.4
        while self._running and self.transport.connected:
            # 1) perintah dari UI dulu (prioritas)
            try:
                kind, cmd = self.cmd_queue.get_nowait()
            except queue.Empty:
                kind, cmd = None, None
            if cmd is not None:
                op = cmd.split(" ", 1)[0].upper()
                if op in self.FIRE_OPS:
                    self.transport.send(cmd)     # tanpa round-trip
                    continue
                timeout = 10.0 if kind == "EXPORT" else 2.5
                resp = self.transport.request(cmd, timeout=timeout)
                if kind in ("LISTF", "LISTG", "LISTP", "LISTS", "EXPORT"):
                    self.data_received.emit(kind, resp)
                else:
                    self.command_done.emit(cmd, resp)
                continue
            # 2) polling GET berkala utk sinkron dua arah
            now = time.monotonic()
            if now - last_poll >= poll_interval:
                last_poll = now
                st = self.transport.request("GET", timeout=1.0)
                # None bisa berarti ESP32 sibuk (mis. tulis NVS/EXPORT) -> lewati;
                # respons basi (mis. sisa fire-and-forget) tidak punya "master" -> lewati.
                if isinstance(st, dict) and "master" in st:
                    self.state_received.emit(st)
            # 3) refresh data struktural (preset/scene) tiap DATA_INTERVAL
            #    -> perubahan yang dibuat dari Web UI/device lain ikut
            #    terdeteksi desktop tanpa perlu reconnect.
            if now - last_data >= self.DATA_INTERVAL:
                last_data = now
                self.cmd_queue.put(("LISTP", "LISTP"))
                self.cmd_queue.put(("LISTS", "LISTS"))
                continue
            time.sleep(0.02)
