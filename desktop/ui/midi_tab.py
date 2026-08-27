# Tab MIDI: perangkat, aktivitas realtime, tabel mapping, MIDI-learn.
# v43-polish: layout 3 section jelas, riwayat aktivitas (bukan satu baris),
# tabel warna selang-seling, kontrol learn nonaktif saat belum tersambung.
from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (QComboBox, QGridLayout, QHBoxLayout, QHeaderView,
                               QLabel, QPushButton, QSpinBox, QTableWidget,
                               QTableWidgetItem, QTextEdit, QVBoxLayout, QWidget)

from midi_handler import ACTIONS, MIDI_AVAILABLE

ACTION_KEYS = [k for k, _ in ACTIONS]
ACTION_LABELS = {k: lbl for k, lbl in ACTIONS}


class MidiTab(QWidget):
    connect_requested = Signal(str)       # nama port
    disconnect_requested = Signal()
    refresh_requested = Signal()
    save_requested = Signal()
    defaults_requested = Signal()
    delete_requested = Signal(str, int)   # (kind, number)
    learn_requested = Signal(str, object, object)  # (action, p1, p2)

    def __init__(self):
        super().__init__()
        self._act_lines = []
        self._build()

    def _build(self):
        root = QVBoxLayout(self)
        root.setSpacing(10)

        if not MIDI_AVAILABLE:
            warn = QLabel("Library MIDI tidak tersedia — install: pip install mido python-rtmidi")
            warn.setStyleSheet("color:#ff8a65;font-weight:bold;")
            root.addWidget(warn)

        # ============ SECTION 1: PERANGKAT ============
        s1 = QLabel("1 · PERANGKAT")
        s1.setObjectName("sectLbl")
        root.addWidget(s1)
        dev = QHBoxLayout()
        dev.addWidget(QLabel("Device:"))
        self.device_combo = QComboBox()
        self.device_combo.setMinimumWidth(300)
        dev.addWidget(self.device_combo)
        self.btn_refresh = QPushButton("Cari Device")
        self.btn_refresh.clicked.connect(self.refresh_requested)
        dev.addWidget(self.btn_refresh)
        self.btn_conn = QPushButton("SAMBUNG MIDI")
        self.btn_conn.setCheckable(True)
        self.btn_conn.setObjectName("goBtn")
        self.btn_conn.clicked.connect(self._on_conn)
        dev.addWidget(self.btn_conn)
        dev.addStretch(1)
        root.addLayout(dev)

        st = QHBoxLayout()
        self.status_lbl = QLabel("tidak terhubung")
        self.status_lbl.setStyleSheet("color:#9aa4b2;")
        st.addWidget(self.status_lbl)
        st.addStretch(1)
        root.addLayout(st)

        root.addWidget(QLabel("Aktivitas terakhir (realtime):"))
        self.activity_box = QTextEdit()
        self.activity_box.setReadOnly(True)
        self.activity_box.setFixedHeight(74)
        self.activity_box.setPlaceholderText("belum ada event MIDI...")
        root.addWidget(self.activity_box)

        # ============ SECTION 2: MAPPING ============
        s2 = QLabel("2 · MAPPING (CC = fader/knob · Note = pad/tombol)")
        s2.setObjectName("sectLbl")
        root.addWidget(s2)
        self.table = QTableWidget(0, 4)
        self.table.setHorizontalHeaderLabels(["Tipe", "Nomor", "Aksi", "Param"])
        self.table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self.table.setSelectionBehavior(QTableWidget.SelectRows)
        self.table.setEditTriggers(QTableWidget.NoEditTriggers)
        self.table.setAlternatingRowColors(True)
        self.table.verticalHeader().setVisible(False)
        root.addWidget(self.table, 1)

        tbl_btns = QHBoxLayout()
        self.btn_del = QPushButton("Hapus Terpilih")
        self.btn_del.setObjectName("dangerBtn")
        self.btn_del.clicked.connect(self._on_delete)
        self.btn_save = QPushButton("Simpan Mapping")
        self.btn_save.setObjectName("goBtn")
        self.btn_save.clicked.connect(self.save_requested)
        self.btn_defaults = QPushButton("Kembali ke Default")
        self.btn_defaults.clicked.connect(self.defaults_requested)
        tbl_btns.addWidget(self.btn_del)
        tbl_btns.addWidget(self.btn_save)
        tbl_btns.addWidget(self.btn_defaults)
        tbl_btns.addStretch(1)
        root.addLayout(tbl_btns)

        # ============ SECTION 3: MIDI-LEARN ============
        s3 = QLabel("3 · MIDI-LEARN")
        s3.setObjectName("sectLbl")
        root.addWidget(s3)
        learn = QHBoxLayout()
        learn.addWidget(QLabel("Aksi:"))
        self.action_combo = QComboBox()
        for k, lbltxt in ACTIONS:
            self.action_combo.addItem(lbltxt, k)
        learn.addWidget(self.action_combo)
        learn.addWidget(QLabel("P1:"))
        self.spin_p1 = QSpinBox()
        self.spin_p1.setRange(0, 512)
        learn.addWidget(self.spin_p1)
        learn.addWidget(QLabel("P2:"))
        self.spin_p2 = QSpinBox()
        self.spin_p2.setRange(0, 512)
        learn.addWidget(self.spin_p2)
        self.btn_learn = QPushButton("LEARN")
        self.btn_learn.setCheckable(True)
        self.btn_learn.setObjectName("editBtn")
        self.btn_learn.setEnabled(False)   # aktif setelah MIDI tersambung
        self.btn_learn.clicked.connect(self._on_learn)
        learn.addWidget(self.btn_learn)
        learn.addStretch(1)
        root.addLayout(learn)

        hint = QLabel(
            "Cara pakai: pilih aksi → klik LEARN → gerakkan fader/tekan pad di controller → "
            "mapping langsung tersimpan. P1/P2 dipakai sesuai aksi: group=P1(0-7), preset=P1(0-29), "
            "scene_play=P1(0-19), chan=P1=fixture & P2=channel. Aksi tanpa param mengabaikan P1/P2.")
        hint.setStyleSheet("color:#9aa4b2;font-size:11px;")
        hint.setWordWrap(True)
        root.addWidget(hint)

    # ---- aksi lokal ----
    def _on_conn(self, on):
        if on:
            name = self.device_combo.currentData() or self.device_combo.currentText()
            if not name:
                self.btn_conn.setChecked(False)
                return
            self.connect_requested.emit(name)
        else:
            self.disconnect_requested.emit()

    def _on_delete(self):
        row = self.table.currentRow()
        if row < 0:
            return
        it = self.table.item(row, 0)
        if it is None:
            return
        kind = it.data(Qt.UserRole)
        try:
            num = int(self.table.item(row, 1).text())
        except (ValueError, AttributeError):
            return
        self.delete_requested.emit(kind, num)

    def _on_learn(self, on):
        if on:
            action = self.action_combo.currentData()
            needs_p = action in ("group", "preset", "scene_play", "chan")
            p1 = self.spin_p1.value() if needs_p else None
            p2 = self.spin_p2.value() if action == "chan" else None
            self.learn_requested.emit(action, p1, p2)
            self.btn_learn.setText("MENUNGGU MIDI...")
        else:
            self.learn_requested.emit("", None, None)  # batalkan
            self.btn_learn.setText("LEARN")

    # ---- API utk main window ----
    def set_devices(self, names):
        self.device_combo.clear()
        for n in names:
            self.device_combo.addItem(n, n)
        if not names:
            self.device_combo.addItem("(tidak ada device MIDI)")

    def set_status(self, text, connected=False):
        col = "#7bd88f" if connected else "#9aa4b2"
        self.status_lbl.setText(("● " if connected else "○ ") + text)
        self.status_lbl.setStyleSheet(f"color:{col};font-weight:bold;")
        self.btn_conn.setChecked(connected)
        self.btn_conn.setText("PUTUS MIDI" if connected else "SAMBUNG MIDI")
        self.btn_conn.setObjectName("dangerBtn" if connected else "goBtn")
        self.btn_conn.style().unpolish(self.btn_conn)
        self.btn_conn.style().polish(self.btn_conn)
        self.btn_conn.update()
        self.btn_learn.setEnabled(connected)
        if not connected:
            self.set_learn_active(False)

    def set_activity(self, text):
        # Riwayat bergulir: simpan 8 event terakhir, terbaru paling bawah.
        self._act_lines.append(text)
        self._act_lines = self._act_lines[-8:]
        self.activity_box.setPlainText("\n".join(self._act_lines))
        sb = self.activity_box.verticalScrollBar()
        sb.setValue(sb.maximum())

    def set_learn_active(self, on):
        self.btn_learn.setChecked(on)
        self.btn_learn.setText("MENUNGGU MIDI..." if on else "LEARN")

    def refresh_table(self, m):
        """m = MidiMapper.map dict {cc:{...}, note:{...}}"""
        rows = []
        for kind in ("cc", "note"):
            for num, e in sorted(m.get(kind, {}).items(), key=lambda x: int(x[0])):
                a = e.get("action", "?")
                if a == "chan":
                    param = f"fix={e.get('index', 0)} ch={e.get('ch', 0)}"
                elif a in ("group", "preset", "scene_play"):
                    param = str(e.get("index", 0))
                else:
                    param = "-"
                rows.append((kind, int(num), ACTION_LABELS.get(a, a), param))
        self.table.setRowCount(len(rows))
        for r, (kind, num, albl, param) in enumerate(rows):
            it = QTableWidgetItem(kind.upper())
            it.setData(Qt.UserRole, kind)
            self.table.setItem(r, 0, it)
            self.table.setItem(r, 1, QTableWidgetItem(str(num)))
            self.table.setItem(r, 2, QTableWidgetItem(albl))
            self.table.setItem(r, 3, QTableWidgetItem(param))
