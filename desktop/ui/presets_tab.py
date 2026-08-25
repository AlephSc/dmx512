# Tab PRESET: bank 16 pad + rekam/hapus + fade/hold per preset.
from PySide6.QtCore import Signal
from PySide6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
                               QLabel, QSpinBox, QCheckBox, QPushButton)

from ui.widgets import PadButton


class PresetsTab(QWidget):
    cmd = Signal(str)

    def __init__(self):
        super().__init__()
        self.pads = []
        self._last_sel = -2   # utk hindari overwrite spinbox saat user mengedit
        self._build()

    def _build(self):
        root = QVBoxLayout(self)
        lbl = QLabel("PRESET BANK (klik = pilih + mainkan)")
        lbl.setObjectName("sectLbl")
        root.addWidget(lbl)

        grid = QGridLayout()
        grid.setSpacing(6)
        for i in range(16):
            p = PadButton(str(i + 1))
            p.clicked.connect(lambda _, i=i: self._on_pad(i))
            grid.addWidget(p, i // 4, i % 4)
            self.pads.append(p)
        root.addLayout(grid)

        # --- preset terpilih: fade/hold + hapus
        sel = QHBoxLayout()
        self.sel_lbl = QLabel("Terpilih: -")
        sel.addWidget(self.sel_lbl)
        sel.addStretch(1)
        sel.addWidget(QLabel("Fade"))
        self.sp_f = QSpinBox()
        self.sp_f.setRange(0, 2550); self.sp_f.setSingleStep(50)
        self.sp_f.setSuffix(" ms"); self.sp_f.setValue(600)
        sel.addWidget(self.sp_f)
        sel.addWidget(QLabel("Hold"))
        self.sp_h = QSpinBox()
        self.sp_h.setRange(100, 5000); self.sp_h.setSingleStep(100)
        self.sp_h.setSuffix(" ms"); self.sp_h.setValue(1500)
        sel.addWidget(self.sp_h)
        b_fh = QPushButton("Update F/H")
        b_fh.clicked.connect(self._on_fh)
        sel.addWidget(b_fh)
        b_del = QPushButton("HAPUS")
        b_del.setObjectName("dangerBtn")
        b_del.clicked.connect(self._on_del)
        sel.addWidget(b_del)
        root.addLayout(sel)

        # --- rekam preset baru
        rec = QHBoxLayout()
        rec.addWidget(QLabel("REKAM output saat ini ke preset:"))
        self.sp_rec = QSpinBox()
        self.sp_rec.setRange(1, 16); self.sp_rec.setValue(1)
        rec.addWidget(self.sp_rec)
        self.chk_idim = QCheckBox("tanpa dimmer")
        rec.addWidget(self.chk_idim)
        rec.addWidget(QLabel("Fade"))
        self.sp_rf = QSpinBox()
        self.sp_rf.setRange(0, 2550); self.sp_rf.setSingleStep(50)
        self.sp_rf.setSuffix(" ms"); self.sp_rf.setValue(600)
        rec.addWidget(self.sp_rf)
        rec.addWidget(QLabel("Hold"))
        self.sp_rh = QSpinBox()
        self.sp_rh.setRange(100, 5000); self.sp_rh.setSingleStep(100)
        self.sp_rh.setSuffix(" ms"); self.sp_rh.setValue(1500)
        rec.addWidget(self.sp_rh)
        b_rec = QPushButton("REKAM")
        b_rec.setObjectName("goBtn")
        b_rec.clicked.connect(self._on_rec)
        rec.addWidget(b_rec)
        rec.addStretch(1)
        root.addLayout(rec)
        root.addStretch(1)

    # ---- aksi ------------------------------------------------------------
    def _on_pad(self, i):
        self.cmd.emit(f"SELP {i+1}")
        self.cmd.emit(f"PSL {i+1}")

    def _on_fh(self):
        n = self._selected()
        if n < 0:
            return
        self.cmd.emit(f"PFH {n+1} {self.sp_f.value()} {self.sp_h.value()}")

    def _on_del(self):
        n = self._selected()
        if n < 0:
            return
        self.cmd.emit(f"PDEL {n+1}")

    def _on_rec(self):
        idim = 1 if self.chk_idim.isChecked() else 0
        self.cmd.emit(f"REC {self.sp_rec.value()} {idim} {self.sp_rf.value()} {self.sp_rh.value()}")

    def _selected(self):
        for i, p in enumerate(self.pads):
            if p.isChecked():
                return i
        return -1

    # ---- sinkron ------------------------------------------------------------
    def apply_state(self, st, active_keys):
        sel = st.selected_preset()
        for i, p in enumerate(self.pads):
            pr = st.preset(i)
            p.setChecked(i == sel)
            if pr and pr.get("used"):
                p.set_used(pr.get("r", 0), pr.get("g", 0), pr.get("b", 0), i == sel)
            else:
                p.clear_color(i == sel)
        if 0 <= sel < 16:
            pr = st.preset(sel)
            # isi spinbox saat pemilihan berubah ATAU saat nilainya diubah dari
            # sisi lain (web) — selama spinbox tidak sedang difokus user.
            if sel != self._last_sel:
                self._last_sel = sel
                self.sel_lbl.setText(f"Terpilih: #{sel+1}")
                if pr:
                    self.sp_f.setValue(int(pr.get("f", 600)))
                    self.sp_h.setValue(int(pr.get("h", 1500)))
            elif pr:
                if not self.sp_f.hasFocus() and self.sp_f.value() != int(pr.get("f", 600)):
                    self.sp_f.setValue(int(pr.get("f", 600)))
                if not self.sp_h.hasFocus() and self.sp_h.value() != int(pr.get("h", 1500)):
                    self.sp_h.setValue(int(pr.get("h", 1500)))
        else:
            if sel != self._last_sel:
                self._last_sel = sel
                self.sel_lbl.setText("Terpilih: -")
