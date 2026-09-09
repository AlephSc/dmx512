# Tab SISTEM: Save/Load NVS, Export/Import file, konfigurasi WiFi ESP32, log.
# v50 desktop parity: diagnostik — Stat Frame DMX (serial DMXSTAT) &
# Stat Art-Net (serial ARTSTAT), melebihi Web UI (yang tak punya ini).
from PySide6.QtCore import Signal, QTimer
from PySide6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel,
                                QLineEdit, QPushButton, QTextEdit, QGroupBox)


class SystemTab(QWidget):
    cmd = Signal(str)
    export_requested = Signal()
    import_requested = Signal()
    wifi_refresh_requested = Signal()          # minta WIFIST ke perangkat
    wifi_apply_requested = Signal(str, str)    # (ssid, pass) -> WIFIS

    def __init__(self):
        super().__init__()
        self._build()

    def _build(self):
        root = QVBoxLayout(self)
        lbl = QLabel("SISTEM & PENYIMPANAN")
        lbl.setObjectName("sectLbl")
        root.addWidget(lbl)

        row = QHBoxLayout()
        b_save = QPushButton("SAVE DATA (NVS)")
        b_save.setObjectName("goBtn")
        b_save.clicked.connect(lambda: self.cmd.emit("SAVE"))
        b_load = QPushButton("LOAD DATA (NVS)")
        b_load.clicked.connect(lambda: self.cmd.emit("LOAD"))
        b_exp = QPushButton("EXPORT preset ke file...")
        b_exp.clicked.connect(self.export_requested)
        b_imp = QPushButton("IMPORT preset dari file...")
        b_imp.clicked.connect(self.import_requested)
        row.addWidget(b_save)
        row.addWidget(b_load)
        row.addWidget(b_exp)
        row.addWidget(b_imp)
        row.addStretch(1)
        root.addLayout(row)

        # --- WiFi ESP32 (v43): kredensial kustom tersimpan di flash ----------
        wlbl = QLabel("WIFI ESP32 (kredensial kustom)")
        wlbl.setObjectName("sectLbl")
        root.addWidget(wlbl)
        self.wifi_stat = QLabel("Status: belum diminta — klik Perbarui Status.")
        self.wifi_stat.setStyleSheet("color:#9aa4b2;")
        root.addWidget(self.wifi_stat)
        wrow = QHBoxLayout()
        wrow.addWidget(QLabel("SSID:"))
        self.wifi_ssid = QLineEdit()
        self.wifi_ssid.setPlaceholderText("nama WiFi tujuan")
        self.wifi_ssid.setMaximumWidth(220)
        wrow.addWidget(self.wifi_ssid)
        wrow.addWidget(QLabel("Sandi:"))
        self.wifi_pass = QLineEdit()
        self.wifi_pass.setPlaceholderText("password")
        self.wifi_pass.setEchoMode(QLineEdit.Password)
        self.wifi_pass.setMaximumWidth(220)
        wrow.addWidget(self.wifi_pass)
        b_wset = QPushButton("Terapkan & Sambungkan")
        b_wset.setObjectName("goBtn")
        b_wset.setToolTip("Simpan ke flash ESP32 lalu coba koneksi baru (gagal 6x = kembali ke bawaan + AP darurat)")
        b_wset.clicked.connect(self._on_wifi_apply)
        wrow.addWidget(b_wset)
        b_wref = QPushButton("Perbarui Status")
        b_wref.clicked.connect(self.wifi_refresh_requested)
        wrow.addWidget(b_wref)
        wrow.addStretch(1)
        root.addLayout(wrow)
        whint = QLabel("Catatan: SSID via serial tidak boleh mengandung spasi (pakai Web UI bila spasi). "
                       "Bila IP berubah setelah koneksi baru, sesuaikan alamat di toolbar.")
        whint.setStyleSheet("color:#9aa4b2;font-size:11px;")
        whint.setWordWrap(True)
        root.addWidget(whint)

        self.status_lbl = QLabel("Status: -")
        root.addWidget(self.status_lbl)

        # --- v50: diagnostik DMX/Art-Net -----------------------------------
        dbox = QGroupBox("DIAGNOSTIK (maju terus melebihi Web UI)")
        dv = QVBoxLayout(dbox)
        drow = QHBoxLayout()
        self.b_dmxstat = QPushButton("Stat Frame DMX")
        self.b_dmxstat.setToolTip("Interval frame min/avg/max ms (hanya via USB Serial — "
                                  "firmware tidak mengekspos statistik frame via HTTP).")
        self.b_dmxstat.clicked.connect(lambda: self.cmd.emit("DMXSTAT"))
        self.b_artstat = QPushButton("Stat Art-Net")
        self.b_artstat.clicked.connect(lambda: self.cmd.emit("ARTSTAT"))
        self.b_diag_auto = QPushButton("Auto 2 dtk")
        self.b_diag_auto.setCheckable(True)
        self.b_diag_auto.clicked.connect(self._on_diag_auto)
        drow.addWidget(self.b_dmxstat)
        drow.addWidget(self.b_artstat)
        drow.addWidget(self.b_diag_auto)
        drow.addStretch(1)
        dv.addLayout(drow)
        self.dmx_lbl = QLabel("Frame DMX: -")
        self.dmx_lbl.setStyleSheet("color:#9aa4b2;")
        dv.addWidget(self.dmx_lbl)
        self.art_lbl = QLabel("Art-Net: -")
        self.art_lbl.setStyleSheet("color:#9aa4b2;")
        dv.addWidget(self.art_lbl)
        dhint = QLabel("Normal: interval frame min 24-26 ms, avg ~25 ms, max <50 ms. "
                       "Nilai max besar = task DMX kelaparan (output tidak teratur).")
        dhint.setStyleSheet("color:#9aa4b2;font-size:11px;")
        dhint.setWordWrap(True)
        dv.addWidget(dhint)
        root.addWidget(dbox)
        self._diag_timer = QTimer(self)
        self._diag_timer.setInterval(2000)
        self._diag_timer.timeout.connect(lambda: self.cmd.emit("DMXSTAT"))

        root.addWidget(QLabel("Log:"))
        self.log_box = QTextEdit()
        self.log_box.setReadOnly(True)
        root.addWidget(self.log_box, 1)

    def _on_wifi_apply(self):
        ssid = self.wifi_ssid.text().strip()
        if not ssid:
            self.log("WiFi: SSID belum diisi.")
            return
        self.wifi_apply_requested.emit(ssid, self.wifi_pass.text())

    def _on_diag_auto(self, on):
        if on:
            self._diag_timer.start()
            self.cmd.emit("DMXSTAT")
        else:
            self._diag_timer.stop()

    def set_http_mode(self, is_http):
        """DMXSTAT tidak punya endpoint HTTP — nonaktifkan tombolnya di WiFi.
        ARTSTAT tetap aktif (dipetakan ke GET /artnet)."""
        self.b_dmxstat.setEnabled(not is_http)
        if is_http:
            self._diag_timer.stop()
            self.b_diag_auto.setChecked(False)
            self.dmx_lbl.setText("Frame DMX: hanya via USB Serial (tidak ada endpoint HTTP).")
            self.dmx_lbl.setStyleSheet("color:#9aa4b2;")

    # ---- API utk main window -------------------------------------------------
    def set_dmxstat(self, j):
        """Render respons DMXSTAT: interval frame ms + snapshot runtime."""
        try:
            self.dmx_lbl.setText(
                f"Frame DMX: {j.get('frames','?')} frame · "
                f"min {j.get('min','?')} ms · avg {j.get('avg','?')} ms · "
                f"max {j.get('max','?')} ms · mast {j.get('mast','?')} · "
                f"chase={j.get('chase','?')} · sceneOn={j.get('sceneOn','?')} · "
                f"artnet={j.get('artnet','?')}")
        except Exception:  # noqa: BLE001
            self.dmx_lbl.setText(f"Frame DMX: {j}")
        bad = False
        try:
            if int(j.get("max", 0)) > 60:
                bad = True
        except (TypeError, ValueError):
            pass
        self.dmx_lbl.setStyleSheet("color:#e74c3c;" if bad else "color:#7bd88f;")

    def set_artstat(self, j):
        """Render respons ARTSTAT: mode + paket terakhir."""
        mode = j.get("mode", "?")
        pkt = j.get("pkt", "?")
        last = j.get("lastAt", "?")
        txt = f"Art-Net: mode {mode.upper()} · {pkt} paket · aktivitas terakhir {last}"
        self.art_lbl.setText(txt)
        self.art_lbl.setStyleSheet("color:#4fc3f7;")
    def set_wifi(self, j):
        """Render status dari respons WIFIST//wifistat."""
        if j.get("connected"):
            cust = " · kredensial kustom" if j.get("custom") else ""
            txt = (f"Terhubung ke \"{j.get('ssid','?')}\" · IP {j.get('ip','?')} · "
                   f"{j.get('rssi',0)} dBm{cust}")
            col = "#7bd88f"
        elif j.get("pending"):
            txt = f"Mencoba menyambung ke \"{j.get('ssid','?')}\"..."
            col = "#ffd54f"
        elif j.get("apActive"):
            txt = f"Mode AP darurat · IP {j.get('ip','?')}"
            col = "#ff8a65"
        else:
            txt = "Tidak terhubung."
            col = "#ff8a65"
        self.wifi_stat.setText("Status: " + txt)
        self.wifi_stat.setStyleSheet(f"color:{col};")

    def log(self, msg):
        self.log_box.append(msg)

    def apply_state(self, st, active_keys):
        j = st.live
        txt = (f"revision={j.get('revision','-')} | "
               f"nvsDirty={j.get('nvsDirty','-')} | "
               f"lastSaveOk={j.get('lastSaveOk','-')} | "
               f"master={j.get('master','-')} | "
               f"preset=#{int(j.get('selectedPreset',-1))+1 if j.get('selectedPreset',-1)>=0 else '-'} | "
               f"sceneOn={j.get('sceneOn','-')}")
        self.status_lbl.setText("Status: " + txt)
