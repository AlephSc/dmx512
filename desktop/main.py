# DMX512 ESP32 Desktop Controller — jendela utama.
# Firmware yang dibutuhkan: v38+ (protokol serial paritas penuh).
import json
import sys

from PySide6.QtCore import Qt, QThread
from PySide6.QtGui import QKeySequence, QShortcut
from PySide6.QtWidgets import (QApplication, QComboBox, QFileDialog,
                               QHBoxLayout, QLabel, QMainWindow, QPushButton,
                               QTabWidget, QVBoxLayout, QWidget)

from state import DeviceState
from transport import SerialTransport, list_candidate_ports
from ui.mixer_tab import MixerTab
from ui.presets_tab import PresetsTab
from ui.scenes_tab import ScenesTab
from ui.system_tab import SystemTab
from worker import SerialWorker


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("DMX512 Controller — ESP32")
        self.resize(1180, 720)
        self.state = DeviceState()
        self.transport = SerialTransport()
        self._worker = None
        self._thread = None
        self.active_keys = set()
        self._build_ui()
        self._refresh_ports()

    # ---- UI statis ---------------------------------------------------------
    def _build_ui(self):
        # toolbar koneksi
        bar = QWidget()
        h = QHBoxLayout(bar)
        h.addWidget(QLabel("Port:"))
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(280)
        self.btn_refresh = QPushButton("Cari Port")
        self.btn_refresh.clicked.connect(self._refresh_ports)
        self.btn_conn = QPushButton("SAMBUNG")
        self.btn_conn.setCheckable(True)
        self.btn_conn.setObjectName("goBtn")
        self.btn_conn.clicked.connect(self._toggle_conn)
        self.conn_lbl = QLabel("tidak terhubung")
        self.conn_lbl.setObjectName("connLbl")
        h.addWidget(self.port_combo)
        h.addWidget(self.btn_refresh)
        h.addWidget(self.btn_conn)
        h.addWidget(self.conn_lbl)
        h.addStretch(1)
        self.rev_lbl = QLabel("")
        h.addWidget(self.rev_lbl)

        # tabs
        self.tabs = QTabWidget()
        self.tab_mixer = MixerTab()
        self.tab_presets = PresetsTab()
        self.tab_scenes = ScenesTab()
        self.tab_system = SystemTab()
        self.tabs.addTab(self.tab_mixer, "Mixer")
        self.tabs.addTab(self.tab_presets, "Preset")
        self.tabs.addTab(self.tab_scenes, "Scene")
        self.tabs.addTab(self.tab_system, "Sistem")

        # wiring: semua perintah serial dari tab -> antrean worker
        for t in (self.tab_mixer, self.tab_presets, self.tab_scenes, self.tab_system):
            t.cmd.connect(self.send_cmd)
        self.tab_mixer.active.connect(self._on_active)
        self.tab_system.export_requested.connect(self._do_export)
        self.tab_system.import_requested.connect(self._do_import)
        # EDIT MODE scene -> klik pad preset menambah langkah (bukan play)
        self.tab_scenes.edit_mode_changed.connect(self.tab_presets.set_scene_edit)

        # status bar
        self.status_lbl = QLabel("Siap.")
        sb = self.statusBar()
        sb.addWidget(self.status_lbl)

        central = QWidget()
        v = QVBoxLayout(central)
        v.setContentsMargins(8, 8, 8, 8)
        v.addWidget(bar)
        v.addWidget(self.tabs, 1)
        self.setCentralWidget(central)

        # shortcut keyboard global
        QShortcut(QKeySequence(Qt.Key_Escape), self).activated.connect(
            lambda: self.send_cmd("SSTOP"))
        QShortcut(QKeySequence(Qt.Key_Space), self).activated.connect(
            lambda: self.send_cmd("ALL off"))

    # ---- koneksi -----------------------------------------------------------
    def _refresh_ports(self):
        self.port_combo.clear()
        for dev, desc, known in list_candidate_ports():
            tag = "  [kemungkinan ESP32]" if known else ""
            self.port_combo.addItem(f"{dev} — {desc}{tag}", dev)
        if self.port_combo.count() == 0:
            self.port_combo.addItem("(tidak ada COM port)")

    def _toggle_conn(self, on):
        if on:
            port = self.port_combo.currentData()
            if not port:
                self._set_status("Pilih COM port dulu.")
                self.btn_conn.setChecked(False)
                return
            if not self.transport.open(port):
                self.btn_conn.setChecked(False)
                return
            self._thread = QThread(self)
            self._worker = SerialWorker(self.transport)
            self._worker.moveToThread(self._thread)
            self._thread.started.connect(self._worker.run)
            self._worker.state_received.connect(self._on_state)
            self._worker.data_received.connect(self._on_data)
            self._worker.command_done.connect(self._on_cmd_done)
            self.transport.error_occurred.connect(self._on_serial_error)
            self._worker.start()
            self._thread.start()
            # ambil metadata dari ESP32
            for kind in ("LISTF", "LISTG", "LISTP", "LISTS"):
                self._worker.cmd_queue.put((kind, kind))
            self.btn_conn.setText("PUTUS")
            self.btn_conn.setObjectName("dangerBtn")
            self.conn_lbl.setText(f"terhubung: {port}")
            self._set_status(f"Terhubung ke {port}.")
            self.tab_system.log(f"== Terhubung ke {port} ==")
        else:
            self._disconnect()

    def _disconnect(self):
        if self._worker is not None:
            self._worker.stop()
        if self._thread is not None:
            self._thread.quit()
            self._thread.wait(1500)
        self.transport.close()
        self._worker = None
        self._thread = None
        self.btn_conn.setChecked(False)
        self.btn_conn.setText("SAMBUNG")
        self.btn_conn.setObjectName("goBtn")
        self.conn_lbl.setText("tidak terhubung")
        self._set_status("Terputus.")
        self.tab_system.log("== Terputus ==")

    # ---- kirim perintah ----------------------------------------------------
    def send_cmd(self, cmd):
        if self._worker is None:
            self._set_status("Belum terhubung — perintah diabaikan.")
            return
        self._worker.cmd_queue.put(("cmd", cmd))
        # perintah yg mengubah data -> minta daftar terbaru utk sinkron UI
        op = cmd.split(" ")[0].upper()
        if op in ("REC", "PDEL", "PFH"):
            self._worker.cmd_queue.put(("LISTP", "LISTP"))
        elif op in ("SPUSH", "SPOP", "SCLR"):
            self._worker.cmd_queue.put(("LISTS", "LISTS"))
        elif op == "LOAD":
            self._worker.cmd_queue.put(("LISTP", "LISTP"))
            self._worker.cmd_queue.put(("LISTS", "LISTS"))

    def _do_export(self):
        if self._worker is None:
            self._set_status("Belum terhubung.")
            return
        self._set_status("Meminta EXPORT dari ESP32 (±4 detik)...")
        self._worker.cmd_queue.put(("EXPORT", "EXPORT"))

    def _do_import(self):
        """Import file hasil EXPORT (.json) ke ESP32 via protokol batch serial.

        Alur: IMPORT_BEGIN -> per preset: IMPORT_P + 8x IMPORT_C (64 ch/baris)
        -> IMPORT_END (commit + persist NVS) -> refresh LISTP.
        Round-trip penuh ±146 perintah ≈ 3-6 detik di 115200 baud.
        """
        if self._worker is None:
            self._set_status("Belum terhubung.")
            return
        path, _ = QFileDialog.getOpenFileName(
            self, "Pilih file export preset", "", "JSON (*.json)")
        if not path:
            return
        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except Exception as e:  # noqa: BLE001
            self._set_status(f"File tidak valid: {e}")
            return
        presets = data.get("presets") if isinstance(data, dict) else None
        if not isinstance(presets, list) or not presets:
            self._set_status("Format bukan file export DMX-RGB (kurang 'presets').")
            return
        q = self._worker.cmd_queue
        q.put(("cmd", "IMPORT_BEGIN"))
        n_total = 0
        CHUNK = 64
        for idx, pr in enumerate(presets[:16]):
            if not isinstance(pr, dict):
                continue
            n = idx + 1
            u = 1 if pr.get("u") else 0
            f_ms = int(pr.get("f", 600))
            h_ms = int(pr.get("h", 1500))
            q.put(("cmd", f"IMPORT_P {n} {u} {f_ms} {h_ms}"))
            chans = pr.get("c", [])
            for off in range(0, 512, CHUNK):
                seg = chans[off:off + CHUNK]
                if not seg:
                    break
                vals = ",".join(str(int(v) & 0xFF) for v in seg)
                q.put(("cmd", f"IMPORT_C {n} {off} {vals}"))
            n_total += 1
        q.put(("cmd", "IMPORT_END"))
        q.put(("LISTP", "LISTP"))
        self._set_status(f"Import {n_total} preset dikirim ke ESP32 (±5 detik)...")
        self.tab_system.log(f"Import dari {path} ({n_total} preset) dikirim.")

    def _on_active(self, key, pressed):
        if pressed:
            self.active_keys.add(key)
        else:
            self.active_keys.discard(key)

    # ---- respons dari worker ----------------------------------------------
    def _on_state(self, j):
        self.state.live = j
        self.tab_mixer.apply_state(self.state, self.active_keys)
        self.tab_presets.apply_state(self.state, self.active_keys)
        self.tab_scenes.apply_state(self.state, self.active_keys)
        self.tab_system.apply_state(self.state, self.active_keys)
        self.rev_lbl.setText(f"rev {j.get('revision','?')}"
                             + (" • belum disimpan" if j.get("nvsDirty") else ""))

    def _on_data(self, kind, payload):
        if payload is None:
            self._set_status(f"{kind}: tidak ada respons.")
            return
        if kind == "LISTF":
            self.state.fixtures = payload
            self.tab_mixer.build_fixtures(payload)
        elif kind == "LISTG":
            self.state.groups = payload
            self.tab_mixer.build_groups(payload)
        elif kind == "LISTP":
            self.state.presets = payload
        elif kind == "LISTS":
            self.state.scenes = payload
        elif kind == "EXPORT":
            self._save_export(payload)

    def _on_cmd_done(self, cmd, resp):
        if isinstance(resp, dict) and resp.get("ok") is False:
            self._set_status(f"GAGAL [{cmd}]: {resp.get('err','?')}")
            self.tab_system.log(f"[{cmd}] gagal: {resp.get('err')}")

    def _on_serial_error(self, msg):
        self._set_status(msg)
        self.tab_system.log("ERROR: " + msg)

    def _save_export(self, payload):
        path, _ = QFileDialog.getSaveFileName(
            self, "Simpan export preset", "dmx-presets.json", "JSON (*.json)")
        if not path:
            return
        try:
            with open(path, "w", encoding="utf-8") as f:
                json.dump(payload, f)
            self._set_status(f"Export tersimpan: {path}")
            self.tab_system.log("Export tersimpan: " + path)
        except Exception as e:  # noqa: BLE001
            self._set_status(f"Gagal menyimpan export: {e}")

    def _set_status(self, msg):
        self.status_lbl.setText(msg)

    # ---- tutup ---------------------------------------------------------------
    def closeEvent(self, ev):
        self._disconnect()
        ev.accept()


def main():
    app = QApplication(sys.argv)
    app.setStyleSheet(APP_QSS)
    w = MainWindow()
    w.show()
    sys.exit(app.exec())


APP_QSS = """
* { font-family: 'Segoe UI', sans-serif; }
QMainWindow, QWidget { background:#1b1e24; color:#dfe3ea; }
QLabel#sectLbl { color:#8ab4f8; font-weight:bold; letter-spacing:1px; margin-top:6px; }
QLabel#fName { color:#9aa4b2; font-size:10px; }
QLabel#fVal { color:#dfe3ea; font-size:10px; }
QLabel#connLbl { color:#9aa4b2; margin-left:8px; }
QPushButton { background:#2a2e35; color:#dfe3ea; border:1px solid #3a3f4b;
              border-radius:6px; padding:6px 12px; }
QPushButton:hover { background:#343a44; }
QPushButton:pressed { background:#22262d; }
QPushButton#goBtn { background:#1b5e20; border-color:#2e7d32; }
QPushButton#goBtn:hover { background:#2e7d32; }
QPushButton#editBtn { background:#457b9d; color:#fff; }
QPushButton#editBtn:hover { background:#1d3557; }
QPushButton#showBtn { background:#2e7d32; color:#fff; }
QPushButton#sceneTab { font-weight:bold; }
QPushButton#dangerBtn { background:#7f1d1d; border-color:#b91c1c; }
QPushButton#dangerBtn:hover { background:#b91c1c; }
QComboBox, QSpinBox { background:#232733; color:#dfe3ea; border:1px solid #3a3f4b;
                      border-radius:4px; padding:3px 6px; }
QTabWidget::pane { border:1px solid #3a3f4b; background:#1b1e24; }
QTabBar::tab { background:#232733; color:#9aa4b2; padding:8px 18px;
               border:1px solid #3a3f4b; }
QTabBar::tab:selected { background:#1b1e24; color:#8ab4f8; }
QFrame#fixBox { background:#20242c; border:1px solid #333a45; border-radius:8px;
                padding:4px; }
QLabel#fixName { color:#8ab4f8; font-size:11px; font-weight:bold; }
QScrollArea { border:1px solid #333a45; border-radius:6px; }
QTextEdit { background:#14161b; color:#9fb3c8; border:1px solid #333a45; }
QStatusBar { background:#14161b; color:#9aa4b2; }
"""


if __name__ == "__main__":
    main()
