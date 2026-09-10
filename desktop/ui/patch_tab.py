# Tab PATCH (v45): konfigurasi alamat DMX & jumlah fixture.
# Paritas WebUI Patch panel: edit nama/tipe/alamat/foot, validasi <=512,
# tidak tumpang tindih. Kirim FIXSET (serial) atau POST /fixes (HTTP).
# v50 desktop parity: EDITOR TIPE CUSTOM (slot 5-15) — label per channel,
# mode Fader/Switch — paritas "Custom Type Editor" Web UI (CTSET/POST /ctypes).
import json

from PySide6.QtCore import Signal, Qt
from PySide6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel,
                                QPushButton, QScrollArea, QTableWidget,
                                QTableWidgetItem, QHeaderView, QComboBox,
                                QSpinBox, QLineEdit, QCheckBox, QMessageBox,
                                QGroupBox, QRadioButton, QGridLayout)

FIX_TYPES = ["PAR", "Moving Head", "Beam", "Strobe", "Fog"]
MAX_FIX = 32
CT_MIN, CT_MAX, CT_CH = 5, 15, 32   # mirror firmware: slot 5-15, maks 32 ch


class PatchTab(QWidget):
    cmd = Signal(str)              # perintah serial (FIXSET <json>)
    http_fixtures = Signal(list)   # kirim daftar fixture via HTTP POST /fixes

    def __init__(self):
        super().__init__()
        self.fixtures = []         # daftar fixture aktif (dict)
        self.is_http = False       # True bila transport HTTP (dipakai main.py)
        self.custom_types = {}     # {slot: {name,channels,mode[],labels[]}}
        self._build()

    def _build(self):
        root = QVBoxLayout(self)
        root.setSpacing(8)

        lbl = QLabel("PATCH FIXTURE (alamat DMX & jumlah channel)")
        lbl.setObjectName("sectLbl")
        root.addWidget(lbl)

        hint = QLabel(
            "Alamat akhir tiap fixture tidak boleh melebihi 512 dan tidak boleh "
            "tumpang tindih. Setelah selesai, klik Simpan Patch.")
        hint.setWordWrap(True)
        hint.setStyleSheet("color:#9aa4b2;font-size:11px;")
        root.addWidget(hint)

        # Tabel editable
        self.table = QTableWidget(0, 6)
        self.table.setHorizontalHeaderLabels(
            ["Nama", "Tipe", "Alamat Awal", "Jumlah Ch", "Akhir", "Pan/Tilt"])
        self.table.horizontalHeader().setSectionResizeMode(0, QHeaderView.Stretch)
        for c in (1, 2, 3, 4, 5):
            self.table.horizontalHeader().setSectionResizeMode(c, QHeaderView.ResizeToContents)
        self.table.verticalHeader().setVisible(False)
        self.table.setSelectionBehavior(QTableWidget.SelectRows)
        self.table.setSelectionMode(QTableWidget.SingleSelection)
        self.table.itemChanged.connect(self._on_cell_changed)
        root.addWidget(self.table, 1)

        # Tombol aksi
        btnrow = QHBoxLayout()
        self.b_add = QPushButton("+ Tambah Fixture")
        self.b_add.clicked.connect(self._add_fixture)
        btnrow.addWidget(self.b_add)
        self.b_del = QPushButton("Hapus Baris Terpilih")
        self.b_del.setObjectName("dangerBtn")
        self.b_del.clicked.connect(self._del_fixture)
        btnrow.addWidget(self.b_del)
        btnrow.addStretch(1)
        self.b_save = QPushButton("Simpan Patch")
        self.b_save.setObjectName("goBtn")
        self.b_save.clicked.connect(self._save_patch)
        btnrow.addWidget(self.b_save)
        root.addLayout(btnrow)

        self.status_lbl = QLabel("Status: -")
        self.status_lbl.setWordWrap(True)
        root.addWidget(self.status_lbl)

        # ---- v50: EDITOR TIPE CUSTOM (slot 5-15) — paritas Web UI ------
        ct_toggle = QHBoxLayout()
        self.b_ct_toggle = QPushButton("Editor Tipe Custom")
        self.b_ct_toggle.setObjectName("editBtn")
        self.b_ct_toggle.setCheckable(True)
        self.b_ct_toggle.clicked.connect(self._on_ct_toggle)
        ct_toggle.addWidget(self.b_ct_toggle)
        ct_toggle.addStretch(1)
        ct_hint = QLabel("Tipe tambahan (slot 5-15) untuk fixture dengan channel "
                         "sendiri: label per channel + mode Fader/Switch (⚡).")
        ct_hint.setStyleSheet("color:#9aa4b2;font-size:11px;")
        ct_hint.setWordWrap(True)
        ct_toggle.addWidget(ct_hint)
        root.addLayout(ct_toggle)

        self.ct_box = QGroupBox("TIPE CUSTOM (slot 5-15)")
        self.ct_box.setVisible(False)
        ct_root = QVBoxLayout(self.ct_box)
        ct_root.setSpacing(6)

        ct_top = QHBoxLayout()
        ct_top.addWidget(QLabel("Slot:"))
        self.ct_slot = QComboBox()
        self.ct_slot.currentIndexChanged.connect(self._ct_load_slot)
        ct_top.addWidget(self.ct_slot)
        ct_top.addWidget(QLabel("Nama:"))
        self.ct_name = QLineEdit()
        self.ct_name.setMaxLength(16)
        self.ct_name.setMaximumWidth(140)
        ct_top.addWidget(self.ct_name)
        ct_top.addWidget(QLabel("Jumlah Ch:"))
        self.ct_ch = QSpinBox()
        self.ct_ch.setRange(1, CT_CH)
        self.ct_ch.setValue(8)
        self.ct_ch.valueChanged.connect(self._ct_channels_changed)
        ct_top.addWidget(self.ct_ch)
        ct_top.addStretch(1)
        self.b_ct_load = QPushButton("Muat Slot")
        self.b_ct_load.clicked.connect(self._ct_load_slot)
        ct_top.addWidget(self.b_ct_load)
        self.b_ct_save = QPushButton("Simpan Tipe")
        self.b_ct_save.setObjectName("goBtn")
        self.b_ct_save.clicked.connect(self._ct_save)
        ct_top.addWidget(self.b_ct_save)
        ct_root.addLayout(ct_top)

        # editor per-channel: scrollable (maks 32 baris)
        self.ct_chan_scroll = QScrollArea()
        self.ct_chan_scroll.setWidgetResizable(True)
        self.ct_chan_host = QWidget()
        self.ct_chan_grid = QGridLayout(self.ct_chan_host)
        self.ct_chan_grid.setSpacing(3)
        self.ct_chan_scroll.setWidget(self.ct_chan_host)
        ct_root.addWidget(self.ct_chan_scroll)

        self.ct_status = QLabel("Pilih slot lalu klik Muat Slot.")
        self.ct_status.setStyleSheet("color:#9aa4b2;")
        root.addWidget(self.ct_box)

    # ---- isi dari LISTF ----------------------------------------------------
    def build_from_listf(self, fixtures):
        """Isi tabel dari metadata fixture ESP32 (LISTF / /fixes)."""
        self.fixtures = fixtures
        # blok sinyal dilakukan per-baris di _append_row; blok luar TIDAK
        # dipakai karena blockSignals boolean (bukan counter) — unblock di
        # _append_row baris pertama akan menetralkan blok ini.
        self.table.setRowCount(0)
        for fx in fixtures:
            self._append_row(fx)
        self._update_end_column()
        self._validate()

    def _append_row(self, fx):
        r = self.table.rowCount()
        self.table.insertRow(r)
        # Tipe (combobox; tipe custom slot 5-15 ikut terdaftar — paritas web)
        combo = QComboBox()
        combo.addItem("— pilih tipe —", -1)
        for i, t in enumerate(FIX_TYPES):
            combo.addItem(t, i)
        for slot in sorted(self.custom_types):
            combo.addItem(f"* {self.custom_types[slot].get('name', f'CUSTOM{slot}')}", slot)
        ftype = fx.get("type", 0)
        if ftype >= 5 and ftype not in self.custom_types:
            # tipe custom belum ada di daftar (LISTCT belum datang) — tetap
            # tampilkan agar tidak "menghapus diam-diam" tipenya saat save
            combo.addItem(f"* TIPE {ftype}", ftype)
        idx = combo.findData(ftype)
        combo.setCurrentIndex(idx if idx >= 0 else 1)
        # blok sinyal hanya selama setItem (memicu itemChanged -> validasi);
        # setCellWidget TIDAK memicu itemChanged, jadi aman di luar blok.
        self.table.blockSignals(True)
        name_item = QTableWidgetItem(fx.get("name", ""))
        self.table.setItem(r, 0, name_item)
        self.table.setCellWidget(r, 1, combo)
        start_item = QTableWidgetItem(str(fx.get("start", 1)))
        self.table.setItem(r, 2, start_item)
        # Jumlah ch
        foot_item = QTableWidgetItem(str(fx.get("foot", 1)))
        self.table.setItem(r, 3, foot_item)
        # Akhir (read-only, dihitung)
        end_item = QTableWidgetItem("-")
        end_item.setFlags(end_item.flags() & ~Qt.ItemIsEditable)
        self.table.setItem(r, 4, end_item)
        self.table.blockSignals(False)
        # hasMove (checkbox)
        chk = QCheckBox()
        chk.setChecked(bool(fx.get("hasMove", False)))
        holder = QWidget()
        hl = QHBoxLayout(holder)
        hl.addWidget(chk)
        hl.setAlignment(Qt.AlignCenter)
        hl.setContentsMargins(0, 0, 0, 0)
        self.table.setCellWidget(r, 5, holder)
        combo.currentIndexChanged.connect(lambda _=0: self._validate())
        chk.stateChanged.connect(lambda _=0: self._validate())

    # ---- baca tabel -> daftar fixture -------------------------------------
    def _read_table(self):
        rows = []
        for r in range(self.table.rowCount()):
            name = (self.table.item(r, 0).text() if self.table.item(r, 0) else f"FIX{r+1}").strip()
            combo = self.table.cellWidget(r, 1)
            ftype = combo.currentData() if combo else 0
            if ftype is None or ftype < 0:
                ftype = 0
            try:
                start = int(self.table.item(r, 2).text()) if self.table.item(r, 2) else 1
            except ValueError:
                start = 1
            try:
                foot = int(self.table.item(r, 3).text()) if self.table.item(r, 3) else 1
            except ValueError:
                foot = 1
            holder = self.table.cellWidget(r, 5)
            chk = holder.findChild(QCheckBox) if holder else None
            has_move = 1 if (chk and chk.isChecked()) else 0
            rows.append({"name": name[:24], "type": ftype, "start": start,
                         "foot": foot, "hasMove": has_move})
        return rows

    def _update_end_column(self):
        rows = self._read_table()
        for r, fx in enumerate(rows):
            end = fx["start"] + fx["foot"] - 1
            item = self.table.item(r, 4)
            if item:
                item.setText(str(end))
                item.setForeground(Qt.red if end > 512 else Qt.gray)

    def _validate(self):
        rows = self._read_table()
        self.fixtures = list(rows)
        errors = []
        for i, f in enumerate(rows):
            end = f["start"] + f["foot"] - 1
            if f["start"] < 1 or f["foot"] < 1:
                errors.append(f"{f['name']}: alamat/jumlah tidak valid")
                continue
            if end > 512:
                errors.append(f"{f['name']}: alamat akhir {end} melebihi 512")
                continue
            for j in range(i):
                g = rows[j]
                if f["start"] <= g["start"] + g["foot"] - 1 and g["start"] <= end:
                    errors.append(f"{f['name']} tumpang tindih dengan {g['name']}")
                    break
        if not errors and rows:
            total = max(f["start"] + f["foot"] - 1 for f in rows)
            self.status_lbl.setText(
                f"Valid. {len(rows)} fixture, channel tertinggi: {total}/512.")
            self.status_lbl.setStyleSheet("color:#7bd88f;")
        elif errors:
            self.status_lbl.setText("Error: " + "; ".join(errors))
            self.status_lbl.setStyleSheet("color:#e74c3c;")
        else:
            self.status_lbl.setText("Minimal 1 fixture harus ada.")
            self.status_lbl.setStyleSheet("color:#ffd54f;")
        return len(errors) == 0 and len(rows) > 0

    # ---- event -------------------------------------------------------------
    def _on_cell_changed(self, item):
        # Kolom 2 (alamat) atau 3 (foot) berubah -> update kolom akhir + validasi
        if item.column() in (2, 3):
            self._update_end_column()
            self._validate()

    def _add_fixture(self):
        if self.table.rowCount() >= MAX_FIX:
            self.status_lbl.setText(f"Maksimal {MAX_FIX} fixture.")
            return
        rows = self._read_table()
        next_start = 1
        for f in rows:
            next_start = max(next_start, f["start"] + f["foot"])
        if next_start + 1 > 512:
            self.status_lbl.setText("Tidak ada ruang alamat tersisa (maks 512).")
            return
        self._append_row({"name": f"FIX{self.table.rowCount()+1}", "type": 0,
                          "start": next_start, "foot": 3, "hasMove": 0})
        self._update_end_column()
        self._validate()

    def _del_fixture(self):
        r = self.table.currentRow()
        if r < 0:
            return
        if self.table.rowCount() <= 1:
            self.status_lbl.setText("Minimal 1 fixture harus ada.")
            return
        self.table.removeRow(r)
        self._update_end_column()
        self._validate()

    # ---- v50: editor tipe custom (slot 5-15) ------------------------------
    def set_custom_types(self, ctypes):
        """Isi editor dari LISTCT. Dipanggil main.py saat data tiba."""
        self.custom_types = {c.get("slot"): c for c in (ctypes or []) if c.get("slot")}
        self._ct_fill_slot_select()
        self._ct_load_slot()
        # dropdown tipe fixture di tabel perlu memuat tipe baru -> rebuild
        # hanya bila tabel sudah terisi (build_from_listf belum pernah jalan)
        if self.table.rowCount() > 0:
            self.build_from_listf(self._read_table())

    def _on_ct_toggle(self, on):
        self.ct_box.setVisible(on)
        if on:
            self._ct_fill_slot_select()
            self._ct_load_slot()

    def _ct_fill_slot_select(self):
        self.ct_slot.blockSignals(True)
        self.ct_slot.clear()
        for s in range(CT_MIN, CT_MAX + 1):
            d = self.custom_types.get(s)
            self.ct_slot.addItem(f"Slot {s}" + (f" · {d.get('name', '')}" if d else " (kosong)"), s)
        self.ct_slot.blockSignals(False)

    def _ct_current_def(self):
        s = self.ct_slot.currentData()
        return s, self.custom_types.get(s)

    def _ct_load_slot(self):
        """Muat definisi slot terpilih ke editor (idempotent, aman kosong)."""
        s, d = self._ct_current_def()
        if s is None:
            self._ct_render_channels()
            return
        if d:
            self.ct_name.setText(d.get("name", f"CUSTOM{s}"))
            self.ct_ch.setValue(int(d.get("channels", 8)))
        else:
            self.ct_name.setText(f"CUSTOM{s}")
            self.ct_ch.setValue(8)
        self._ct_render_channels()
        self.ct_status.setText(f"Slot {s} dimuat.")
        self.ct_status.setStyleSheet("color:#9aa4b2;")

    def _ct_channels_changed(self, _v):
        self._ct_render_channels()

    def _ct_render_channels(self):
        """Bangun baris editor per-channel: nomor, label (8 char), radio
        Fader/Switch. Koneksi dibuat per-render (elemen dibuang ulang)."""
        # buang baris lama
        while self.ct_chan_grid.count():
            it = self.ct_chan_grid.takeAt(0)
            w = it.widget()
            if w:
                w.deleteLater()
        s, d = self._ct_current_def()
        if s is None:
            return
        n = self.ct_ch.value()
        labels = (d or {}).get("labels", [])
        modes = (d or {}).get("mode", [])
        for k in range(n):
            row = QHBoxLayout()
            row.setSpacing(6)
            num = QLabel(f"{k+1}.")
            num.setStyleSheet("color:#9aa4b2;")
            num.setMinimumWidth(24)
            edit = QLineEdit((labels[k] if k < len(labels) and labels[k] else f"CH{k+1}"))
            edit.setMaxLength(8)
            edit.setMaximumWidth(90)
            # radio Fader/Switch (default Fader)
            r_f = QRadioButton("Fader")
            r_s = QRadioButton("Switch")
            if k < len(modes) and modes[k]:
                r_s.setChecked(True)
            else:
                r_f.setChecked(True)
            # simpan langsung ke dict def lokal saat berubah
            edit.textChanged.connect(lambda t, k=k: self._ct_set_label(k, t))
            r_f.toggled.connect(lambda on, k=k: self._ct_set_mode(k, 0) if on else None)
            r_s.toggled.connect(lambda on, k=k: self._ct_set_mode(k, 1) if on else None)
            wrap = QWidget()
            row.addWidget(num)
            row.addWidget(edit)
            row.addWidget(r_f)
            row.addWidget(r_s)
            row.addStretch(1)
            wrap.setLayout(row)
            self.ct_chan_grid.addWidget(wrap, k, 0)
        self.ct_chan_grid.setRowStretch(n, 1)

    def _ct_editing(self, create=True):
        """Dict def slot yang sedang diedit (mutasi lokal hingga Simpan)."""
        s, d = self._ct_current_def()
        if s is None:
            return None
        if d is None and create:
            d = {"slot": s, "name": f"CUSTOM{s}", "channels": self.ct_ch.value(),
                 "mode": [0] * self.ct_ch.value(), "labels": []}
            self.custom_types[s] = d
        return d

    def _ct_set_label(self, k, text):
        d = self._ct_editing()
        if d is None:
            return
        while len(d.setdefault("labels", [])) <= k:
            d["labels"].append("")
        d["labels"][k] = text

    def _ct_set_mode(self, k, m):
        d = self._ct_editing()
        if d is None:
            return
        while len(d.setdefault("mode", [])) <= k:
            d["mode"].append(0)
        d["mode"][k] = m

    def _ct_save(self):
        s, d = self._ct_current_def()
        if s is None:
            return
        name = self.ct_name.text().strip()
        if not name:
            self.ct_status.setText("Nama tipe wajib diisi.")
            self.ct_status.setStyleSheet("color:#e74c3c;")
            return
        n = self.ct_ch.value()
        # kumpulkan label & mode dari dict editing (diisi oleh signal editor)
        d = self._ct_editing() or {}
        labels = list(d.get("labels", []))
        modes = list(d.get("mode", []))
        while len(labels) < n:
            labels.append("")
        while len(modes) < n:
            modes.append(0)
        payload = {"types": [{"slot": s, "used": 1, "name": name[:16],
                              "channels": n,
                              "mode": modes[:n],
                              "labels": [x if x else f"CH{i+1}" for i, x in enumerate(labels[:n])]}]}
        self.cmd.emit("CTSET " + json.dumps(payload, separators=(",", ":")))
        self.ct_status.setText(f"Menyimpan tipe slot {s}...")
        self.ct_status.setStyleSheet("color:#ffd54f;")

    def _save_patch(self):
        if not self._validate():
            self.status_lbl.setText("Perbaiki error validasi dulu.")
            self.status_lbl.setStyleSheet("color:#e74c3c;")
            return
        rows = self._read_table()
        payload = {"count": len(rows), "fixtures": rows}
        if self.is_http:
            # Kirim via signal -> main.py melakukan POST /fixes
            self.http_fixtures.emit(rows)
        else:
            # Serial: FIXSET <json satu baris>
            self.cmd.emit("FIXSET " + json.dumps(payload, separators=(",", ":")))
        self.status_lbl.setText("Menyimpan patch ke ESP32...")
        self.status_lbl.setStyleSheet("color:#ffd54f;")

    # ---- sinkron dari state ------------------------------------------------
    def apply_state(self, st, active_keys):
        # Patch tidak berubah via polling state; hanya diisi dari LISTF.
        pass
