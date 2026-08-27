# Tab PATCH (v45): konfigurasi alamat DMX & jumlah fixture.
# Paritas WebUI Patch panel: edit nama/tipe/alamat/foot, validasi <=512,
# tidak tumpang tindih. Kirim FIXSET (serial) atau POST /fixes (HTTP).
from PySide6.QtCore import Signal, Qt
from PySide6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel,
                               QPushButton, QScrollArea, QTableWidget,
                               QTableWidgetItem, QHeaderView, QComboBox,
                               QSpinBox, QLineEdit, QCheckBox, QMessageBox)

FIX_TYPES = ["PAR", "Moving Head", "Beam", "Strobe", "Fog"]
MAX_FIX = 32


class PatchTab(QWidget):
    cmd = Signal(str)              # perintah serial (FIXSET <json>)
    http_fixtures = Signal(list)   # kirim daftar fixture via HTTP POST /fixes

    def __init__(self):
        super().__init__()
        self.fixtures = []         # daftar fixture aktif (dict)
        self.is_http = False       # True bila transport HTTP (dipakai main.py)
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

    # ---- isi dari LISTF ----------------------------------------------------
    def build_from_listf(self, fixtures):
        """Isi tabel dari metadata fixture ESP32 (LISTF / /fixes)."""
        self.fixtures = fixtures
        self.table.blockSignals(True)
        self.table.setRowCount(0)
        for fx in fixtures:
            self._append_row(fx)
        self.table.blockSignals(False)
        self._update_end_column()
        self._validate()

    def _append_row(self, fx):
        r = self.table.rowCount()
        self.table.insertRow(r)
        # Nama
        name_item = QTableWidgetItem(fx.get("name", ""))
        self.table.setItem(r, 0, name_item)
        # Tipe (combobox)
        combo = QComboBox()
        combo.addItems(FIX_TYPES)
        combo.setCurrentIndex(min(fx.get("type", 0), len(FIX_TYPES) - 1))
        self.table.setCellWidget(r, 1, combo)
        # Alamat awal
        start_item = QTableWidgetItem(str(fx.get("start", 1)))
        self.table.setItem(r, 2, start_item)
        # Jumlah ch
        foot_item = QTableWidgetItem(str(fx.get("foot", 1)))
        self.table.setItem(r, 3, foot_item)
        # Akhir (read-only, dihitung)
        end_item = QTableWidgetItem("-")
        end_item.setFlags(end_item.flags() & ~Qt.ItemIsEditable)
        self.table.setItem(r, 4, end_item)
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
            ftype = combo.currentIndex() if combo else 0
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
        self.table.blockSignals(True)
        self._append_row({"name": f"FIX{self.table.rowCount()+1}", "type": 0,
                          "start": next_start, "foot": 3, "hasMove": 0})
        self.table.blockSignals(False)
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

    def _save_patch(self):
        if not self._validate():
            self.status_lbl.setText("Perbaiki error validasi dulu.")
            self.status_lbl.setStyleSheet("color:#e74c3c;")
            return
        rows = self._read_table()
        import json
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
