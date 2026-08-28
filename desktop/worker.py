# Worker thread: satu-satunya pihak yang menyentuh port serial.
# Urutan kerja tiap iterasi: (1) proses antrean perintah UI, (2) polling GET.
import queue
import time

from PySide6.QtCore import QObject, Signal


class SerialWorker(QObject):
    state_received = Signal(dict)        # hasil GET (state realtime)
    data_received = Signal(str, object)  # LISTF/LISTG/LISTP/LISTS/EXPORT
    command_done = Signal(str, object)   # cmd, respons JSON (atau None)

    DATA_INTERVAL = 1.5   # detik antar CEK refresh LISTP/LISTS (data struktural)
    # v45: dipercepat dari 3.0 -> 1.5 detik agar perubahan preset/scene dari
    # WebUI lebih cepat terlihat di desktop (mengurangi "tidak sinkron" antar-app).
    # v47: cek kini CONDITIONAL — LISTP/LISTS benar-benar dikirim hanya bila
    # `revision`/`sceneRev` berubah sejak fetch struktural terakhir. Sebelumnya
    # fetch penuh tiap 1,5 dtk walau tak ada perubahan (parse ~4 KB percuma).

    # Perintah kontrol kontinu = FIRE-AND-FORGET (tanpa tunggu balasan).
    # Inilah inti perbaikan delay fader desktop: sebelumnya tiap geseran
    # mengantre round-trip 5-15 ms (bisa 2,5 dtk bila ESP32 sibuk tulis NVS).
    # Sinkronisasi nilai tetap dijamin polling GET berkala.
    # Hanya kontrol kontinu boleh fire-and-forget. Aksi preset/scene harus
    # menunggu ACK agar error seperti "preset kosong" terlihat operator.
    FIRE_OPS = {"SET", "MAST", "STRB", "GRP", "ALL"}

    def __init__(self, transport):
        super().__init__()
        self.transport = transport
        self.cmd_queue = queue.Queue()
        self._running = False
        # v47: revision/sceneRev terakhir dari GET, dan nilai keduanya saat
        # fetch struktural terakhir dikirim. Beda = data baru butuh diambil.
        self._last_rev = None
        self._last_scene_rev = None
        self._fetched_rev = None
        self._fetched_scene_rev = None

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
                if kind in ("LISTF", "LISTG", "LISTP", "LISTS", "LISTCT", "EXPORT", "WIFIST"):
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
                    if "revision" in st:
                        self._last_rev = st["revision"]
                    if "sceneRev" in st:
                        self._last_scene_rev = st["sceneRev"]
            # 3) kirim LISTP/LISTS hanya bila revision/sceneRev bergeser dari
            #    nilai yang sudah pernah kita fetch (v47).
            if now - last_data >= self.DATA_INTERVAL:
                last_data = now
                if self._structural_stale():
                    self._fetched_rev = self._last_rev
                    self._fetched_scene_rev = self._last_scene_rev
                    self.cmd_queue.put(("LISTP", "LISTP"))
                    self.cmd_queue.put(("LISTS", "LISTS"))
                continue
            time.sleep(0.02)

    def _structural_stale(self):
        """True bila data preset/scene mungkin berubah sejak fetch terakhir.

        None di salah satu sisi = belum pernah fetch -> anggap stale.
        Catatan: revision naik juga karena slider/kontrol (bukan hanya preset/
        scene), jadi ini konservatif — fetch sesekali lebih sering dari perlu
        itu murah dan aman; yang dihindari adalah fetch abadi tiap 1,5 dtk.
        """
        if self._last_rev != self._fetched_rev:
            return True
        if self._last_scene_rev != self._fetched_scene_rev:
            return True
        return False
