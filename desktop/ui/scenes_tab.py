# Tab SCENE: bank 20 scene + EDIT/SHOW mode + ▶ Cek + editor langkah.
# Paritas Web UI:
# - EDIT MODE: klik scene = pilih saja (SELS), TIDAK ada output; editor enabled;
#              masuk EDIT otomatis menghentikan scene & chase (safety, paritas stopAuto).
# - SHOW MODE: klik scene = langsung PLAY; klik scene yg sedang main = STOP.
# - ▶ Cek: play/stop scene terpilih utk tes, berlaku di EDIT maupun SHOW.
from PySide6.QtCore import Signal, Qt
from PySide6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QGridLayout,
                               QLabel, QPushButton)

from ui.widgets import PadButton


class ScenesTab(QWidget):
    cmd = Signal(str)
    edit_mode_changed = Signal(bool)   # True = masuk EDIT MODE
    local_sel = -1                     # scene terpilih (utk editing & Cek)
    sel_preset = -1                    # preset terpilih (utk SPUSH)

    def __init__(self):
        super().__init__()
        self.pads = []
        self.scene_edit = False        # default SHOW MODE (paritas web)
        self._playing_scene = -1
        self._scene_on = False
        self._build()

    def _build(self):
        root = QVBoxLayout(self)
        lbl = QLabel("SCENE BANK")
        lbl.setObjectName("sectLbl")
        root.addWidget(lbl)

        # --- toggle EDIT / SHOW ---
        mrow = QHBoxLayout()
        self.b_edit = QPushButton("EDIT MODE")
        self.b_edit.setCheckable(True)
        self.b_edit.setObjectName("editBtn")
        self.b_show = QPushButton("SHOW MODE")
        self.b_show.setCheckable(True)
        self.b_show.setChecked(True)
        self.b_show.setObjectName("showBtn")
        self.b_edit.clicked.connect(lambda: self._set_mode(edit=True))
        self.b_show.clicked.connect(lambda: self._set_mode(edit=False))
        mrow.addWidget(self.b_edit)
        mrow.addWidget(self.b_show)
        mrow.addStretch(1)
        root.addLayout(mrow)

        # --- grid scene ---
        grid = QGridLayout()
        grid.setSpacing(4)
        for i in range(20):
            p = PadButton(f"S{i+1}", small=True)
            p.clicked.connect(self._pad_handler(i))
            grid.addWidget(p, i // 5, i % 5)
            self.pads.append(p)
        root.addLayout(grid)

        # --- ▶ Cek + info ---
        ctrl = QHBoxLayout()
        self.b_cek = QPushButton("▶ Cek")
        self.b_cek.setObjectName("goBtn")
        self.b_cek.setToolTip("Mainkan/hentikan scene terpilih untuk tes (kedua mode)")
        self.b_cek.clicked.connect(self._on_cek)
        ctrl.addWidget(self.b_cek)
        self.info_lbl = QLabel("SHOW MODE: klik scene = langsung mainkan.")
        self.info_lbl.setWordWrap(True)
        ctrl.addWidget(self.info_lbl, 1)
        root.addLayout(ctrl)

        # --- editor langkah ---
        ed = QHBoxLayout()
        self.steps_lbl = QLabel("Langkah: -")
        self.steps_lbl.setWordWrap(True)
        ed.addWidget(self.steps_lbl, 1)
        col = QVBoxLayout()
        self.b_add = QPushButton("+ Preset terpilih")
        self.b_add.setObjectName("editBtn")
        self.b_add.clicked.connect(self._on_push)
        self.b_pop = QPushButton("Hapus akhir")
        self.b_pop.setObjectName("editBtn")
        self.b_pop.clicked.connect(self._on_pop)
        self.b_clr = QPushButton("Kosongkan")
        self.b_clr.setObjectName("dangerBtn")
        self.b_clr.clicked.connect(self._on_clear)
        col.addWidget(self.b_add)
        col.addWidget(self.b_pop)
        col.addWidget(self.b_clr)
        ed.addLayout(col)
        root.addLayout(ed)
        root.addStretch(1)

        self._apply_mode_ui()   # set enable/disable awal (SHOW -> editor off)

    # ---- mode ---------------------------------------------------------------
    def _set_mode(self, edit):
        if self.scene_edit == edit:
            return
        self.scene_edit = edit
        if edit:
            # SAFETY: masuk EDIT MODE menghentikan semua output otomatis
            # (paritas stopAuto() di web: scene tidak boleh menghasilkan output saat diedit)
            self.cmd.emit("SSTOP")
            self.cmd.emit("CHASE off")
        self._apply_mode_ui()
        self.edit_mode_changed.emit(edit)

    def _apply_mode_ui(self):
        self.b_edit.setChecked(self.scene_edit)
        self.b_show.setChecked(not self.scene_edit)
        self.b_add.setEnabled(self.scene_edit)
        self.b_pop.setEnabled(self.scene_edit)
        self.b_clr.setEnabled(self.scene_edit)
        if self.scene_edit:
            self.info_lbl.setText(
                "EDIT MODE: klik scene hanya memilih (aman, tanpa output). "
                "Klik pad di tab PRESET untuk menambah langkah.")
        else:
            self.info_lbl.setText("SHOW MODE: klik scene = langsung mainkan; klik lagi = stop.")

    # ---- aksi ---------------------------------------------------------------
    def _pad_handler(self, i):
        # Factory: lambda *a menelan argumen 'clicked' (bool checked) yang
        # dikirim tidak konsisten oleh PySide6 6.6 -> indeks tangkapan aman.
        return lambda *a: self._on_pad(i)

    def _on_pad(self, i):
        self.local_sel = i
        if self.scene_edit:
            # EDIT MODE: hanya memilih utk diedit, TIDAK menghasilkan output
            self.cmd.emit(f"SELS {i+1}")
        else:
            # SHOW MODE: klik scene = play; klik scene yg sedang main = stop
            if self._scene_on and self._playing_scene == i:
                self.cmd.emit("SSTOP")
            else:
                self.cmd.emit("CHASE off")     # paritas web: chase dimatikan dulu
                self.cmd.emit(f"SPLAY {i+1}")
        self._update_checked()

    def _on_cek(self):
        # ▶ Cek: play/stop scene TERPILIH utk dicek — berlaku di EDIT maupun SHOW
        if self.local_sel < 0:
            return
        if self._scene_on and self._playing_scene == self.local_sel:
            self.cmd.emit("SSTOP")
        else:
            self.cmd.emit("CHASE off")
            self.cmd.emit(f"SPLAY {self.local_sel+1}")

    def _on_push(self):
        if self.local_sel < 0 or self.sel_preset < 0:
            return
        self.cmd.emit(f"SPUSH {self.local_sel+1} {self.sel_preset+1}")

    def _on_pop(self):
        if self.local_sel >= 0:
            self.cmd.emit(f"SPOP {self.local_sel+1}")

    def _on_clear(self):
        if self.local_sel >= 0:
            self.cmd.emit(f"SCLR {self.local_sel+1}")

    def _update_checked(self):
        for i, p in enumerate(self.pads):
            p.setChecked(i == self.local_sel)

    # ---- sinkron dari state GET & LISTS --------------------------------------
    def apply_state(self, st, active_keys):
        j = st.live
        self.sel_preset = st.selected_preset()
        sel = st.selected_scene()
        if sel >= 0 and sel != self.local_sel:
            self.local_sel = sel
            self._update_checked()
        self._scene_on = bool(j.get("sceneOn", False))
        scn = int(j.get("scn", -1))
        self._playing_scene = scn if self._scene_on else -1
        stp = int(j.get("stp", -1))

        for i, p in enumerate(self.pads):
            has_steps = False
            if i < len(st.scenes):
                has_steps = any(v > 0 for v in st.scenes[i])
            is_playing = self._scene_on and scn == i
            if is_playing:
                p.setStyleSheet("QPushButton{background:#2e7d32;color:#fff;border:2px solid #66bb6a;border-radius:8px;font-weight:bold;}")
            elif has_steps:
                bd = "#ffd54f" if i == self.local_sel else "#546e7a"
                p.setStyleSheet(f"QPushButton{{background:#37474f;color:#eceff1;border:2px solid {bd};border-radius:8px;font-weight:bold;}}")
            else:
                p.clear_color(i == self.local_sel)

        # teks langkah + indikator mode
        present = []
        if 0 <= self.local_sel < len(st.scenes):
            present = [v for v in st.scenes[self.local_sel] if v > 0]
        if present:
            steps_text = ", ".join(f"P{v}" for v in present)
            if self._scene_on and scn == self.local_sel and stp >= 0:
                steps_text += f"   ▶ langkah {stp+1}"
        else:
            steps_text = "(kosong)"
        self.steps_lbl.setText("Langkah: " + steps_text)

        if self.scene_edit:
            nama = f"S{self.local_sel+1}" if self.local_sel >= 0 else "-"
            self.info_lbl.setText(
                f"EDIT MODE: Scene {nama} ({len(present)} langkah) — klik pad PRESET utk menambah langkah.")
        elif self._scene_on and self.local_sel >= 0:
            self.info_lbl.setText(f"SHOW MODE: S{self.local_sel+1} siap; klik ▶ Cek utk tes.")
        else:
            self.info_lbl.setText("SHOW MODE: klik scene = langsung mainkan; klik lagi = stop.")
