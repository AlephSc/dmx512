# Worker thread: satu-satunya pihak yang menyentuh port serial.
# Urutan kerja tiap iterasi: (1) proses antrean perintah UI, (2) polling GET.
import queue
import time

from PySide6.QtCore import QObject, Signal


class SerialWorker(QObject):
    state_received = Signal(dict)        # hasil GET (state realtime)
    data_received = Signal(str, object)  # LISTF/LISTG/LISTP/LISTS/EXPORT
    command_done = Signal(str, object)   # cmd, respons JSON (atau None)

    POLL_INTERVAL = 0.25  # detik antar GET

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
        while self._running and self.transport.connected:
            # 1) perintah dari UI dulu (prioritas)
            try:
                kind, cmd = self.cmd_queue.get_nowait()
            except queue.Empty:
                kind, cmd = None, None
            if cmd is not None:
                timeout = 10.0 if kind == "EXPORT" else 2.5
                resp = self.transport.request(cmd, timeout=timeout)
                if kind in ("LISTF", "LISTG", "LISTP", "LISTS", "EXPORT"):
                    self.data_received.emit(kind, resp)
                else:
                    self.command_done.emit(cmd, resp)
                continue
            # 2) polling GET berkala utk sinkron dua arah
            now = time.monotonic()
            if now - last_poll >= self.POLL_INTERVAL:
                last_poll = now
                st = self.transport.request("GET", timeout=1.0)
                # None bisa berarti ESP32 sibuk (mis. tulis NVS/EXPORT) -> lewati
                if isinstance(st, dict) and "master" in st:
                    self.state_received.emit(st)
            else:
                time.sleep(0.02)
