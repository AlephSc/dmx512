# Tab MIXER: master, strobe, blackout, chase, fader grup, fader per-fixture.
from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel,
                               QPushButton, QScrollArea, QFrame)

from state import channel_labels
from ui.widgets import VFader

# Nama tipe fixture (mirror enum FX di firmware). Dipakai utk label baris.
FIX_TYPE_NAMES = {0: "PAR LED", 1: "MOVING HEAD", 2: "BEAM", 3: "STROBE", 4: "FOG"}


class MixerTab(QWidget):
    cmd = Signal(str)            # perintah serial keluar
    active = Signal(str, bool)   # (key, pressed/released) utk proteksi sync

    def __init__(self):
        super().__init__()
        self.group_faders = []
        self.chan_faders = {}    # "fi_c" -> VFader
        self._build()

    def _build(self):
        root = QVBoxLayout(self)
        root.setSpacing(8)

        # --- baris atas: master, strobe, tombol aksi
        top = QHBoxLayout()
        self.f_master = VFader("Master", color="#4fc3f7")
        self.f_master.slider.valueChanged.connect(lambda v: self.cmd.emit(f"MAST {v}"))
        self.f_master.pressed.connect(lambda: self.active.emit("master", True))
        self.f_master.released.connect(lambda: self.active.emit("master", False))
        top.addWidget(self.f_master)

        self.f_strb = VFader("Strobe", color="#ff8a65")
        self.f_strb.slider.valueChanged.connect(lambda v: self.cmd.emit(f"STRB {v}"))
        self.f_strb.pressed.connect(lambda: self.active.emit("strb", True))
        self.f_strb.released.connect(lambda: self.active.emit("strb", False))
        top.addWidget(self.f_strb)

        side = QVBoxLayout()
        self.b_black = QPushButton("BLACKOUT")
        self.b_black.setObjectName("dangerBtn")
        self.b_black.clicked.connect(lambda: self.cmd.emit("ALL off"))
        self.b_allon = QPushButton("PAR FULL")
        self.b_allon.clicked.connect(lambda: self.cmd.emit("ALL on"))
        self.b_chase = QPushButton("CHASE OFF")
        self.b_chase.setCheckable(True)
        self.b_chase.clicked.connect(self._on_chase)
        side.addWidget(self.b_black)
        side.addWidget(self.b_allon)
        side.addWidget(self.b_chase)
        side.addStretch(1)
        top.addLayout(side)
        top.addStretch(1)
        root.addLayout(top)

        # --- fader grup
        glbl = QLabel("FADER GRUP")
        glbl.setObjectName("sectLbl")
        root.addWidget(glbl)
        self.grp_row = QHBoxLayout()
        root.addLayout(self.grp_row)

        # --- fader per-fixture: baris per tipe (v45, Saran 1) ---
        # Layout vertikal: tiap tipe fixture (PAR/Moving/Beam/Strobe/Fog)
        # mendapat satu baris horizontal berisi semua fixture tipe itu.
        flbl = QLabel("FADER FIXTURE (per channel)")
        flbl.setObjectName("sectLbl")
        root.addWidget(flbl)
        self.fix_scroll = QScrollArea()
        self.fix_scroll.setWidgetResizable(True)
        self.fix_host = QWidget()
        self.fix_layout = QVBoxLayout(self.fix_host)   # baris per tipe
        self.fix_layout.setSpacing(6)
        self.fix_scroll.setWidget(self.fix_host)
        root.addWidget(self.fix_scroll, 1)

    def _on_chase(self, on):
        self.b_chase.setText("CHASE ON" if on else "CHASE OFF")
        self.cmd.emit("CHASE on" if on else "CHASE off")

    # ---- bangun ulang dari metadata ESP32 --------------------------------
    def build_groups(self, groups):
        while self.grp_row.count():
            it = self.grp_row.takeAt(0)
            w = it.widget()
            if w:
                w.deleteLater()
        self.group_faders = []
        for i, g in enumerate(groups):
            f = VFader(g.get("name", f"G{i}"), color="#aed581", small=True)
            f.set_key(f"grp{i}")
            f.slider.valueChanged.connect(lambda v, i=i: self.cmd.emit(f"GRP {i} {v}"))
            f.pressed.connect(lambda i=i: self.active.emit(f"grp{i}", True))
            f.released.connect(lambda i=i: self.active.emit(f"grp{i}", False))
            self.grp_row.addWidget(f)
            self.group_faders.append(f)
        self.grp_row.addStretch(1)

    def build_fixtures(self, fixtures):
        """Bangun ulang fader per-fixture, dikelompokkan per baris tipe (v45).

        Saran 1: tiap tipe fixture (PAR/Moving/Beam/Strobe/Fog) tampil sebagai
        satu baris horizontal. Layout vertikal (QVBoxLayout) menumpuk baris.
        """
        # Bersihkan widget lama
        while self.fix_layout.count():
            it = self.fix_layout.takeAt(0)
            w = it.widget()
            if w:
                w.deleteLater()
            elif it.layout():
                # bersihkan sub-layout (baris tipe) beserta isinya
                self._clear_sub_layout(it.layout())
        self.chan_faders = {}

        # Kelompokkan fixture berdasarkan tipe, pertahankan urutan kemunculan
        from collections import OrderedDict
        type_groups = OrderedDict()
        for fi, fx in enumerate(fixtures):
            ftype = fx.get("type", 0)
            type_groups.setdefault(ftype, []).append((fi, fx))

        for ftype, members in type_groups.items():
            type_name = FIX_TYPE_NAMES.get(ftype, f"TIPE {ftype}")
            # Label baris tipe
            row_lbl = QLabel(f"— {type_name} ({len(members)} unit) —")
            row_lbl.setObjectName("fixTypeLbl")
            row_lbl.setStyleSheet("color:#78909c;font-size:11px;font-weight:bold;padding-top:4px;")
            self.fix_layout.addWidget(row_lbl)

            # Baris horizontal berisi fixture tipe ini
            row_widget = QWidget()
            row_layout = QHBoxLayout(row_widget)
            row_layout.setSpacing(6)
            row_layout.setContentsMargins(0, 0, 0, 0)

            for fi, fx in members:
                box = QFrame()
                box.setObjectName("fixBox")
                v = QVBoxLayout(box)
                v.setSpacing(2)
                addr_start = fx.get("start", 0)
                foot = fx.get("foot", 0)
                addr_end = addr_start + foot - 1 if foot > 0 else addr_start
                nm = QLabel(f"{fx.get('name', f'F{fi}')}\n{addr_start}-{addr_end}")
                nm.setObjectName("fixName")
                nm.setAlignment(Qt.AlignHCenter)
                v.addWidget(nm)
                row = QHBoxLayout()
                row.setSpacing(2)
                labels = channel_labels(fx.get("type", 0), foot)
                for c in range(foot):
                    lab = labels[c] if c < len(labels) else f"C{c}"
                    f = VFader(lab, color="#f5a623", small=True)
                    key = f"{fi}_{c}"
                    f.set_key(key)
                    f.slider.valueChanged.connect(lambda v, key=key: self.cmd.emit(f"SET {key}={v}"))
                    f.pressed.connect(lambda key=key: self.active.emit(key, True))
                    f.released.connect(lambda key=key: self.active.emit(key, False))
                    row.addWidget(f)
                    self.chan_faders[key] = f
                v.addLayout(row)
                row_layout.addWidget(box)

            row_layout.addStretch(1)
            self.fix_layout.addWidget(row_widget)

        self.fix_layout.addStretch(1)

    @staticmethod
    def _clear_sub_layout(layout):
        """Kosongkan sub-layout rekursif (dipakai saat rebuild baris tipe)."""
        while layout.count():
            it = layout.takeAt(0)
            w = it.widget()
            if w:
                w.deleteLater()
            elif it.layout():
                MixerTab._clear_sub_layout(it.layout())

    # ---- sinkron dari GET --------------------------------------------------
    def apply_state(self, st, active_keys):
        j = st.live
        if "master" not in active_keys and "master" in j:
            self.f_master.set_value(j["master"])
        if "strb" not in active_keys and "strb" in j:
            self.f_strb.set_value(j["strb"])
        if "chaseOn" in j:
            on = bool(j["chaseOn"])
            if self.b_chase.isChecked() != on:
                self.b_chase.setChecked(on)
                self.b_chase.setText("CHASE ON" if on else "CHASE OFF")
        cur = j.get("cur", {})
        # Fader grup: hanya di-set bila SEMUA member seragam. Kalau member
        # berbeda (satu fixture digeser manual), posisi fader grup dibiarkan
        # -> tidak ada lompatan visual (paritas syncGroups web v43).
        for i, f in enumerate(self.group_faders):
            if f"grp{i}" in active_keys:
                continue
            vals = st.group_values(i)
            if vals and all(v == vals[0] for v in vals):
                f.set_value(vals[0])
        for key, f in self.chan_faders.items():
            if key in active_keys:
                continue
            if key in cur:
                f.set_value(cur[key])
