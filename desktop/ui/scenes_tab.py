# Tab SCENE: bank 20 scene + play/stop + step editor (push/pop/clear).
from PySide6.QtCore import Signal
from PySide6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QGridLayout, QLabel, QPushButton

from ui.widgets import PadButton


class ScenesTab(QWidget):
    cmd = Signal(str)
    local_sel = -1   # seleksi lokal (berubah saat klik pad / sync GET)
    sel_preset = -1  # preset terpilih (untuk fitur +preset ke scene)

    def __init__(self):
        super().__init__()
        self.pads = []
        self._build()

    def _build(self):
        root = QVBoxLayout(self)
        lbl = QLabel("SCENE BANK")
        lbl.setObjectName("sectLbl")
        root.addWidget(lbl)
        grid = QGridLayout()
        grid.setSpacing(4)
        for i in range(20):
            p = PadButton(f"S{i+1}", small=True)
            p.clicked.connect(lambda _, i=i: self._on_pad(i))
            grid.addWidget(p, i // 5, i % 5)
            self.pads.append(p)
        root.addLayout(grid)

        ctrl = QHBoxLayout()
        self.sel_lbl = QLabel("Scene: -")
        ctrl.addWidget(self.sel_lbl)
        b_play = QPushButton("PLAY")
        b_play.setObjectName("goBtn")
        b_play.clicked.connect(self._on_play)
        b_stop = QPushButton("STOP")
        b_stop.setObjectName("dangerBtn")
        b_stop.clicked.connect(lambda: self.cmd.emit("SSTOP"))
        ctrl.addWidget(b_play)
        ctrl.addWidget(b_stop)
        ctrl.addStretch(1)
        root.addLayout(ctrl)

        ed = QHBoxLayout()
        self.steps_lbl = QLabel("Langkah: -")
        self.steps_lbl.setWordWrap(True)
        ed.addWidget(self.steps_lbl, 1)
        col = QVBoxLayout()
        b_add = QPushButton("+ Preset terpilih")
        b_add.clicked.connect(self._on_push)
        b_pop = QPushButton("Hapus akhir")
        b_pop.clicked.connect(self._on_pop)
        b_clr = QPushButton("Kosongkan")
        b_clr.setObjectName("dangerBtn")
        b_clr.clicked.connect(self._on_clear)
        col.addWidget(b_add)
        col.addWidget(b_pop)
        col.addWidget(b_clr)
        ed.addLayout(col)
        root.addLayout(ed)
        root.addStretch(1)

    def _on_pad(self, i):
        self.local_sel = i
        self.cmd.emit(f"SELS {i+1}")

    def _on_play(self):
        if self.local_sel >= 0:
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

    # ---- sinkron dari state GET & LISTS ----------------------------------
    def apply_state(self, st, active_keys):
        j = st.live
        self.sel_preset = st.selected_preset()
        sel = st.selected_scene()
        if sel >= 0:
            self.local_sel = sel
        playing = bool(j.get("sceneOn", False))
        scn = int(j.get("scn", -1))
        stp = int(j.get("stp", -1))

        for i, p in enumerate(self.pads):
            p.setChecked(i == self.local_sel)
            has_steps = False
            if i < len(st.scenes):
                has_steps = any(v > 0 for v in st.scenes[i])
            is_playing = playing and scn == i

            if is_playing:
                p.setStyleSheet("QPushButton{background:#2e7d32;color:#fff;border:2px solid #66bb6a;border-radius:8px;font-weight:bold;}")
            elif has_steps:
                bd = "#ffd54f" if i == self.local_sel else "#546e7a"
                p.setStyleSheet(f"QPushButton{{background:#37474f;color:#eceff1;border:2px solid {bd};border-radius:8px;font-weight:bold;}}")
            else:
                p.clear_color(i == self.local_sel)

        self.sel_lbl.setText(f"Scene: S{self.local_sel+1}" if self.local_sel >= 0 else "Scene: -")
        # Tampilkan langkah-langkah scene terpilih
        steps_text = "-"
        if 0 <= self.local_sel < len(st.scenes):
            steps_list = st.scenes[self.local_sel]
            present = [(k + 1, v) for k, v in enumerate(steps_list) if v > 0]
            if present:
                parts = [f"P{v}" for (k, v) in present]
                steps_text = ", ".join(parts)
                if playing and scn == self.local_sel and stp >= 0:
                    steps_text += f"   ▶ langkah {(stp+1)}"
            else:
                steps_text = "(kosong)"
        self.steps_lbl.setText("Langkah: " + steps_text)
