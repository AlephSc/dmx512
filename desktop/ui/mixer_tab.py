# Tab MIXER: master, strobe, blackout, chase + section per tipe fixture.
# v47: tiap tipe (PAR/Moving/Beam/Strobe/Fog) punya section collapsible
# berisi (1) fader grup milik tipe itu dan (2) fader per-fixture-nya.
# Fixture dalam section ditata grid multi-baris (wrap ke bawah, tanpa
# scroll horizontal).
from collections import OrderedDict

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel,
                                QPushButton, QScrollArea, QFrame, QGridLayout,
                                QToolButton)

from state import channel_labels
from ui.widgets import VFader

# Nama tipe fixture (mirror enum FX di firmware). Dipakai utk label section.
FIX_TYPE_NAMES = {0: "PAR LED", 1: "MOVING HEAD", 2: "BEAM", 3: "STROBE", 4: "FOG"}

# Jumlah fixture per baris dalam section (wrap ke bawah, bukan scroll samping).
FIX_PER_ROW = 4


class _ClickLabel(QLabel):
    """QLabel yang bisa diklik (v48: pemilihan fixture utk bank).
    Subclass, bukan monkey-patch mousePressEvent — monkey-patch tidak
    dijamin bekerja di semua binding/versi PySide."""

    def __init__(self, text, clicked, parent=None):
        super().__init__(text, parent)
        self._clicked = clicked
        self.setCursor(Qt.PointingHandCursor)

    def mousePressEvent(self, ev):
        if ev.button() == Qt.LeftButton and self._clicked:
            self._clicked()
        super().mousePressEvent(ev)


class MixerTab(QWidget):
    cmd = Signal(str)            # perintah serial keluar
    active = Signal(str, bool)   # (key, pressed/released) utk proteksi sync

    def __init__(self):
        super().__init__()
        self.groups = []          # LISTG: [{name,type,offset}] (data-only)
        self.custom_types = {}    # v48: {slot: {name,channels,mode[],labels[]}}
        self.group_faders = []    # semua VFader grup (utk apply_state)
        self.chan_faders = {}     # "fi_c" -> VFader
        self._fixtures_data = []  # cache terakhir LISTF (instance attr, bukan
                                  # class attr — mutable class attr terbagi
                                  # antar instance; review v47 finding #1)
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
        # v50 desktop parity: toggle Art-Net LOCAL <-> NETWORK (serial ARTNET /
        # web /artnet?mode=). Mode aktif ikut state GET ("artnet" field).
        self.b_artnet = QPushButton("Art-Net: LOCAL")
        self.b_artnet.setCheckable(True)
        self.b_artnet.setToolTip("NETWORK: ESP32 mendengar ArtDmx UDP 6454 dan "
                                 "menimpa output DMX (input jaringan).")
        self.b_artnet.clicked.connect(self._on_artnet)
        # v51: switch deck tombol fisik (serial HWON/HWOFF / web /hw?on|off).
        # Default MATI tiap boot (perilaku v50 OFF); mode aktif ikut state
        # GET (field "hw") seperti toggle Art-Net di atas.
        self.b_hw = QPushButton("Tombol Fisik: OFF")
        self.b_hw.setCheckable(True)
        self.b_hw.setToolTip("ON: tap/hold B1-B4 + encoder memainkan scene. "
                             "OFF: tap tombol diabaikan (aman saat show).")
        self.b_hw.clicked.connect(self._on_hw)
        side.addWidget(self.b_black)
        side.addWidget(self.b_allon)
        side.addWidget(self.b_chase)
        side.addWidget(self.b_artnet)
        side.addWidget(self.b_hw)
        side.addStretch(1)
        top.addLayout(side)
        top.addStretch(1)
        root.addLayout(top)

        # --- section per tipe fixture (dibangun ulang saat LISTF/LISTG datang)
        self.fix_scroll = QScrollArea()
        self.fix_scroll.setWidgetResizable(True)
        self.fix_host = QWidget()
        self.fix_layout = QVBoxLayout(self.fix_host)
        self.fix_layout.setSpacing(10)
        self.fix_scroll.setWidget(self.fix_host)
        root.addWidget(self.fix_scroll, 1)

    def _on_chase(self, on):
        self.b_chase.setText("CHASE ON" if on else "CHASE OFF")
        self.cmd.emit("CHASE on" if on else "CHASE off")

    def _on_artnet(self, on):
        self.b_artnet.setText("Art-Net: NETWORK" if on else "Art-Net: LOCAL")
        self.cmd.emit("ARTNET network" if on else "ARTNET local")

    def _on_hw(self, on):
        self.b_hw.setText("Tombol Fisik: ON" if on else "Tombol Fisik: OFF")
        self.cmd.emit("HWON" if on else "HWOFF")

    # ---- data masuk dari ESP32 ---------------------------------------------
    def build_groups(self, groups):
        """v47: data-only. Rendering grup terjadi di build_fixtures() agar
        fader grup tampil DI DALAM section tipenya (LISTG dan LISTF bisa
        datang dalam urutan berbeda; re-render idempotent)."""
        self.groups = list(groups or [])
        if self._fixtures_data:
            self.build_fixtures(self._fixtures_data)

    def set_custom_types(self, ctypes):
        """v48: definisi custom type [{slot,name,channels,mode[],labels[]}].
        Data-only; re-render section karena label/mode bisa berubah."""
        self.custom_types = {c.get("slot"): c for c in (ctypes or []) if c.get("slot")}
        if self._fixtures_data:
            self.build_fixtures(self._fixtures_data)

    def _type_name(self, ftype):
        """Nama tipe utk header section — custom slot memakai namanya."""
        if ftype in FIX_TYPE_NAMES:
            return FIX_TYPE_NAMES[ftype]
        ct = self.custom_types.get(ftype)
        return ct.get("name", f"TIPE {ftype}") if ct else f"TIPE {ftype}"

    def _channel_meta(self, fx):
        """(labels, mode_fn) utk fixture — chart bawaan ATAU custom type."""
        ftype = fx.get("type", 0)
        foot = fx.get("foot", 0)
        if 5 <= ftype <= 15:
            ct = self.custom_types.get(ftype)
            if ct:
                labels = [(ct.get("labels", [])[c] if c < len(ct.get("labels", [])) and ct.get("labels", [])[c]
                           else f"CH{c+1}") for c in range(foot)]
                modes = [(ct.get("mode", [])[c] if c < len(ct.get("mode", [])) else 0) for c in range(foot)]
                return labels, (lambda c, m=modes: m[c] if c < len(m) else 0)
            return [f"CH{c+1}" for c in range(foot)], (lambda c: 0)
        return channel_labels(ftype, foot), (lambda c: 0)

    def build_fixtures(self, fixtures):
        """Bangun section per tipe: header collapsible + fader grup tipe itu
        + fixture dalam grid yang wrap ke bawah (v47)."""
        self._fixtures_data = list(fixtures or [])
        self._clear_layout(self.fix_layout)
        self.group_faders = []
        self.chan_faders = {}

        # Kelompokkan fixture berdasarkan tipe, pertahankan urutan kemunculan
        type_groups = OrderedDict()
        for fi, fx in enumerate(self._fixtures_data):
            ftype = fx.get("type", 0)
            type_groups.setdefault(ftype, []).append((fi, fx))

        for ftype, members in type_groups.items():
            type_name = self._type_name(ftype)
            first_start = members[0][1].get("start", 0)
            last = members[-1][1]
            addr_end = last.get("start", 0) + last.get("foot", 0) - 1
            sec = self._build_type_section(
                ftype, type_name, members, first_start, addr_end)
            self.fix_layout.addWidget(sec)

        self.fix_layout.addStretch(1)

    def _build_type_section(self, ftype, type_name, members, addr_lo, addr_hi):
        """Satu section: header (toggle) + group fader + grid fixture."""
        sec = QFrame()
        sec.setObjectName("typeSec")
        v = QVBoxLayout(sec)
        v.setSpacing(6)
        v.setContentsMargins(8, 6, 8, 8)

        # Header: tombol lipat + nama + jumlah + rentang alamat
        head = QHBoxLayout()
        toggle = QToolButton()
        toggle.setCheckable(True)
        toggle.setChecked(True)
        body = QWidget()

        def _on_toggle(on, t=toggle, b=body, nm=type_name, n=len(members),
                       lo=addr_lo, hi=addr_hi):
            b.setVisible(on)
            arrow = "\u25be" if on else "\u25b8"   # be=turun, b8=kanan
            t.setText(f"{arrow} {nm} \u00b7 {n} unit \u00b7 DMX {lo}-{hi}")

        toggle.toggled.connect(_on_toggle)
        _on_toggle(True)   # set teks awal
        toggle.setStyleSheet(
            "QToolButton{background:transparent;border:none;color:#78909c;"
            "font-size:12px;font-weight:bold;padding:2px 0;text-align:left;}"
            "QToolButton:checked{color:#b0bec5;}")
        head.addWidget(toggle)
        head.addStretch(1)
        v.addLayout(head)

        inner = QVBoxLayout(body)
        inner.setSpacing(6)

        # (1) v48: fader GRUP dihapus — digantikan BANK (pane kanan) yang
        # mencakup SEMUA channel (GRUP hanya 8 channel tetap). Duplikasi
        # kontrol membingungkan operator. Perintah GRP tetap didukung
        # firmware utk kompatibilitas; group_faders kosong (apply_state
        # loop-nya jadi no-op otomatis).

        # (2) DUAL PANE (v48): kiri grid fixture individu, kanan fader BANK.
        # Bank menulis channel yang sama ke SEMUA fixture tipe ini (satu
        # perintah GRP-style loop via cmd). Klik nama fixture = pilih; bank
        # mengikuti channel-count fixture terpilih.
        dual = QHBoxLayout()
        dual.setSpacing(10)

        grid_host = QWidget()
        grid = QGridLayout(grid_host)
        grid.setSpacing(6)
        bank_faders = []                            # (ch, VFader) utk section ini

        def _build_bank(sel_fi):
            """Render ulang bank sesuai channel fixture terpilih.
            Semua referensi LOKAL per-section (bank_title/bank_col lokal —
            review: self.* ditimpa antar section). on_bank pakai throttle
            30 ms — drag bank 32 fixture via serial = 32×SET ≈ 640 B/tick;
            tanpa throttle itu membanjiri antrean serial."""
            import time as _time
            bank_state = {"last": 0.0}
            for _, f in bank_faders:
                f.setParent(None)
                f.deleteLater()
            bank_faders.clear()
            # buang stretch lama
            while bank_col.count():
                it = bank_col.takeAt(0)
                w = it.widget()
                if w:
                    w.deleteLater()
            sel_fx = self._fixtures_data[sel_fi]
            foot = sel_fx.get("foot", 0)
            labels, modes = self._channel_meta(sel_fx)
            bank_title.setText(
                f"BANK: {type_name} x{len(members)} — semua fixture tipe ini")
            for c in range(foot):
                sw = bool(modes(c))
                f = VFader((labels[c] if c < len(labels) else f"C{c}") + (" ⚡" if sw else ""),
                           color="#4fc3f7", small=True, switch_mode=sw)
                f.set_key(f"bank{ftype}_{c}")

                def _flush_pending():
                    p = bank_state.get("pending")
                    if not p:
                        return
                    bank_state["pending"] = None
                    bank_state["last"] = _time.monotonic()
                    for fi2, fx2 in members:
                        if p[0] < fx2.get("foot", 0):
                            self.cmd.emit(f"SET {fi2}_{p[0]}={p[1]}")

                def on_bank(v, c=c):
                    # throttle 30 ms (paritas WebUI): batasi banjir SET saat drag
                    now = _time.monotonic()
                    if now - bank_state["last"] < 0.03:
                        bank_state["pending"] = (c, v)
                        return
                    bank_state["last"] = now
                    bank_state["pending"] = None
                    # kirim SET ke semua fixture tipe ini (loop kecil N<=32)
                    for fi2, fx2 in members:
                        if c < fx2.get("foot", 0):
                            self.cmd.emit(f"SET {fi2}_{c}={v}")
                def _bank_press(c=c):
                    # anti-bounce: tandai semua "fi_c" member channel ini
                    # agar polling GET tidak menimpa saat drag
                    for fi2, fx2 in members:
                        if c < fx2.get("foot", 0):
                            self._bank_drag_keys.add(f"{fi2}_{c}")
                    self.active.emit(f"bank{ftype}_{c}", True)

                def _bank_release(c=c):
                    for fi2, fx2 in members:
                        if c < fx2.get("foot", 0):
                            self._bank_drag_keys.discard(f"{fi2}_{c}")
                    self.active.emit(f"bank{ftype}_{c}", False)
                    _flush_pending()

                f.slider.valueChanged.connect(on_bank)
                f.pressed.connect(_bank_press)
                f.released.connect(_bank_release)
                bank_col.addWidget(f)
                bank_faders.append((c, f))
            bank_col.addStretch(1)

        def _select_fixture(fi):
            _build_bank(fi)

        for cell, (fi, fx) in enumerate(members):
            box = QFrame()
            box.setObjectName("fixBox")
            bv = QVBoxLayout(box)
            bv.setSpacing(2)
            addr_start = fx.get("start", 0)
            foot = fx.get("foot", 0)
            addr_end = addr_start + foot - 1 if foot > 0 else addr_start
            nm = _ClickLabel(f"{fx.get('name', f'F{fi}')}  {addr_start}-{addr_end}  \u2193",
                             clicked=lambda fi=fi: _select_fixture(fi))
            nm.setObjectName("fixName")
            nm.setAlignment(Qt.AlignHCenter)
            labels, modes = self._channel_meta(fx)
            bv.addWidget(nm)
            row = QHBoxLayout()
            row.setSpacing(2)
            for c in range(foot):
                lab = labels[c] if c < len(labels) else f"C{c}"
                sw = bool(modes(c))
                f = VFader((lab + (" ⚡" if sw else "")), color="#f5a623", small=True, switch_mode=sw)
                key = f"{fi}_{c}"
                f.set_key(key)
                f.slider.valueChanged.connect(lambda v, key=key: self.cmd.emit(f"SET {key}={v}"))
                f.pressed.connect(lambda key=key: self.active.emit(key, True))
                f.released.connect(lambda key=key: self.active.emit(key, False))
                row.addWidget(f)
                self.chan_faders[key] = f
            bv.addLayout(row)
            grid.addWidget(box, cell // FIX_PER_ROW, cell % FIX_PER_ROW)

        grid.setColumnStretch(FIX_PER_ROW, 1)
        dual.addWidget(grid_host, 1)

        # --- kolom bank (kanan) — LOKAL per-section (review: self.* ditimpa
        # antar section sebelumnya; bank multi-section kini independen)
        bank_frame = QFrame()
        bank_frame.setObjectName("fixBox")
        bv2 = QVBoxLayout(bank_frame)
        bv2.setSpacing(2)
        bank_title = QLabel("BANK — klik nama fixture di kiri")
        bank_title.setObjectName("fixName")
        bank_title.setAlignment(Qt.AlignHCenter)
        bv2.addWidget(bank_title)
        bank_col = QHBoxLayout()
        bank_col.setSpacing(2)
        bv2.addLayout(bank_col)
        dual.addWidget(bank_frame, 0)

        inner.addLayout(dual)
        # pilih fixture pertama agar bank langsung terisi
        if members:
            _select_fixture(members[0][0])
        v.addWidget(body)
        return sec

    @staticmethod
    def _clear_layout(layout):
        """Kosongkan layout rekursif (dipakai saat rebuild section)."""
        while layout.count():
            it = layout.takeAt(0)
            w = it.widget()
            if w:
                w.deleteLater()
            elif it.layout():
                MixerTab._clear_layout(it.layout())

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
        # v50: mode Art-Net dari state GET ("artnet": "local"/"network")
        if "artnet" in j:
            an = j["artnet"] == "network"
            if self.b_artnet.isChecked() != an:
                self.b_artnet.setChecked(an)
                self.b_artnet.setText("Art-Net: NETWORK" if an else "Art-Net: LOCAL")
        # v51: switch deck tombol fisik dari state GET ("hw": true/false).
        # Otoritas = server, jadi switch di WebUI / serial HWON/HWOFF ikut
        # tampil di sini dan sebaliknya.
        if "hw" in j:
            hon = bool(j["hw"])
            if self.b_hw.isChecked() != hon:
                self.b_hw.setChecked(hon)
                self.b_hw.setText("Tombol Fisik: ON" if hon else "Tombol Fisik: OFF")
        cur = j.get("cur", {})
        # Fader grup: hanya di-set bila SEMUA member seragam. Kalau member
        # berbeda (satu fixture digeser manual), posisi fader grup dibiarkan
        # -> tidak ada lompatan visual (paritas syncGroups web v43).
        for i, f in self.group_faders:
            if f"grp{i}" in active_keys:
                continue
            vals = st.group_values(i)
            if vals and all(v == vals[0] for v in vals):
                f.set_value(vals[0])
        for key, f in self.chan_faders.items():
            if key in active_keys:
                continue
            # v48 anti-bounce: channel sedang di-drag BANK (tombol mouse masih
            # ditahan di fader bank tipe ini) -> skip. Echo server (polling
            # 0,25-0,4 s) bisa membawa nilai basi yang menimpa nilai drag.
            if self._bank_drag_keys and key in self._bank_drag_keys:
                continue
            if key in cur:
                f.set_value(cur[key])

    # v48: kumpulan "fi_c" yang sedang dikendalikan fader BANK saat ini.
    # Diisi _build_bank saat mouse ditekan/digeser; dikosongkan saat release.
    _bank_drag_keys = set()
