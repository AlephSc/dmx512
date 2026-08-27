# Tab PRESET: bank 30 pad + rekam/hapus + fade/hold per preset.
# Integrasi scene-edit mode (paritas Web): saat EDIT MODE aktif, klik pad preset menambah langkah ke scene terpilih (SPUSH).
from PySide6.QtCore import Signal
from PySide6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QGridLayout, QLabel, QSpinBox, QCheckBox, QPushButton

from ui.widgets import PadButton


class PresetsTab(QWidget):
    cmd = Signal(str)

    def __init__(self):
        super().__init__()
        self.pads = []
        self.scene_edit = False            # False = normal play mode; True = edit mode scene (klik pad = SPUSH)
        self.sel_scene = -1                # scene ter-pilih utk SPUSH (dari ScenesTab)
        self._last_sel = -2
        self.hint_lbl = None
        self._build()

    def _build(self):
        root = QVBoxLayout(self)
        lbl = QLabel("PRESET BANK")
        lbl.setObjectName("sectLbl")
        root.addWidget(lbl)
        self.hint_lbl = QLabel("(klik pad = pilih + mainkan preset)")
        self.hint_lbl.setObjectName("hintLbl")
        self.hint_lbl.setStyleSheet("color:#9aa4b2;font-size:11px;")
        root.addWidget(self.hint_lbl)

        grid = QGridLayout()
        grid.setSpacing(6)
        for i in range(30):
            p = PadButton(str(i + 1), small=True)
            p.clicked.connect(self._pad_handler(i))
            grid.addWidget(p, i // 6, i % 6)
            self.pads.append(p)
        root.addLayout(grid)

        row = QHBoxLayout()
        b_fh = QPushButton("Update F/H")
        b_fh.clicked.connect(self._on_fh)
        row.addWidget(b_fh)
        b_del = QPushButton("HAPUS")
        b_del.setObjectName("dangerBtn")
        b_del.clicked.connect(self._on_del)
        row.addWidget(b_del)
        row.addStretch(1)
        root.addLayout(row)

        self.info_lbl = QLabel("Terpilih: -")
        self.info_lbl.setObjectName("infoLbl")
        root.addWidget(self.info_lbl)

        rec = QHBoxLayout()
        rec.addWidget(QLabel("REKAM output saat ini ke preset:"))
        self.sp_rec = QSpinBox()
        self.sp_rec.setRange(1, 30); self.sp_rec.setValue(1)
        rec.addWidget(self.sp_rec)
        self.chk_idim = QCheckBox("tanpa dimmer")
        rec.addWidget(self.chk_idim)
        rec.addWidget(QLabel("Fade"))
        self.sp_f = QSpinBox()
        self.sp_f.setRange(0, 2550); self.sp_f.setSingleStep(50)
        self.sp_f.setSuffix(" ms"); self.sp_f.setValue(600)
        rec.addWidget(self.sp_f)
        rec.addWidget(QLabel("Hold"))
        self.sp_h = QSpinBox()
        self.sp_h.setRange(100, 5000); self.sp_h.setSingleStep(100)
        self.sp_h.setSuffix(" ms"); self.sp_h.setValue(1500)
        rec.addWidget(self.sp_h)
        b_rec = QPushButton("REKAM")
        b_rec.setObjectName("goBtn")
        b_rec.clicked.connect(self._on_rec)
        rec.addWidget(b_rec)
        rec.addStretch(1)
        root.addLayout(rec)
        root.addStretch(1)

    def set_scene_edit(self, on):
        self.scene_edit = on
        if on:
            self.hint_lbl.setText("(mode EDIT SCENE: klik pad = tambah langkah ke scene)")
            self.chk_idim.setEnabled(False)
        else:
            self.hint_lbl.setText("(klik pad = pilih + mainkan preset)")
            self.chk_idim.setEnabled(True)
        self._update_info_label()

    def _pad_handler(self, i):
        # Factory: PySide6 6.6 mengirim argumen sinyal 'clicked' (bool checked)
        # secara tidak konsisten ke lambda default-arg (bisa menggantikan nilai
        # tangkapan, bisa tidak mengirim sama sekali). lambda *a menelan
        # 0 atau 1 argumen -> indeks tangkapan selalu benar di semua versi.
        return lambda *a: self._on_pad(i)

    def _on_pad(self, i):
        if self.scene_edit:
            # Mode EDIT SCENE: klik preset = tambahkan langkah ke scene terpilih
            if self.sel_scene >= 0:
                self.cmd.emit(f"SPUSH {self.sel_scene+1} {i+1}")
            return
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
        self.cmd.emit(f"REC {self.sp_rec.value()} {idim} {self.sp_f.value()} {self.sp_h.value()}")

    def _selected(self):
        for i, p in enumerate(self.pads):
            if p.isChecked():
                return i
        return -1

    def _update_info_label(self):
        sel = self._selected()
        info = f"Terpilih: #{sel+1}" if sel >= 0 else "Terpilih: -"
        self.info_lbl.setText(info)

    # ---- sinkron ------------------------------------------------------------
    def apply_state(self, st, active_keys):
        sel = st.selected_preset()
        self.sel_scene = st.selected_scene()

        # Kumpulkan slot preset yang dirujuk scene (0-based). Preset "terhapus"
        # (used=0) yang masih dirujuk scene tetap dimainkan -> tandai agar
        # operator tahu slot itu tidak benar-benar kosong (v45, Bug 2).
        scene_refs = set()
        for sc in st.scenes:
            for v in sc:
                if 1 <= v <= 30:
                    scene_refs.add(v - 1)

        for i, p in enumerate(self.pads):
            pr = st.preset(i)
            p.setChecked(i == sel)
            if pr and pr.get("used"):
                p.set_used(pr.get("r", 0), pr.get("g", 0), pr.get("b", 0), i == sel)
            elif i in scene_refs:
                # Disembunyikan tapi masih dirujuk scene -> border putus-putus
                p.set_hidden_in_scene(i == sel)
            else:
                p.clear_color(i == sel)
        # Info label updated on each change; but don't overwrite user's spinbox edits when changing selection
        if sel != self._last_sel:
            self._last_sel = sel
            self._update_info_label()
            if 0 <= sel < 30:
                pr = st.preset(sel)
                if pr:
                    self.sp_f.setValue(int(pr.get("f", 600)))
                    self.sp_h.setValue(int(pr.get("h", 1500)))
        elif 0 <= sel < 30:
            # nilai timing diubah dari sisi lain (web) -> ikut update selama tidak sedang diedit
            pr = st.preset(sel)
            if pr:
                if not self.sp_f.hasFocus() and self.sp_f.value() != int(pr.get("f", 600)):
                    self.sp_f.setValue(int(pr.get("f", 600)))
                if not self.sp_h.hasFocus() and self.sp_h.value() != int(pr.get("h", 1500)):
                    self.sp_h.setValue(int(pr.get("h", 1500)))
