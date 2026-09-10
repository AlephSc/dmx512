# Tab SISTEM: Save/Load NVS, Export/Import file, konfigurasi WiFi ESP32, log.
from PySide6.QtCore import Signal
from PySide6.QtWidgets import (QWidget, QVBoxLayout, QHBoxLayout, QLabel,
                               QLineEdit, QPushButton, QTextEdit, QCheckBox)


class SystemTab(QWidget):
    cmd = Signal(str)
    export_requested = Signal()
    import_requested = Signal()
    wifi_refresh_requested = Signal()          # minta WIFIST ke perangkat
    wifi_apply_requested = Signal(str, str)    # (ssid, pass) -> WIFIS
    netmode_apply_requested = Signal(bool, bool)  # v53: (sta, ap) -> NETMODE

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

        # --- Radio STA/AP independen (v53, firmware v53+) ---------------------
        nrow = QHBoxLayout()
        nrow.addWidget(QLabel("Radio:"))
        self.net_sta = QCheckBox("WiFi STA nyala")
        self.net_sta.setChecked(True)
        self.net_sta.setToolTip("Matikan bila hanya butuh AP lapangan / Ethernet")
        nrow.addWidget(self.net_sta)
        self.net_ap = QCheckBox("AP nyala")
        self.net_ap.setToolTip("Nyalakan agar AP tetap ada saat STA tersambung")
        nrow.addWidget(self.net_ap)
        b_nset = QPushButton("Terapkan Radio")
        b_nset.setObjectName("goBtn")
        b_nset.setToolTip("Kombinasi bebas STA/AP. Mematikan keduanya tanpa Ethernet ditolak firmware.")
        b_nset.clicked.connect(self._on_netmode_apply)
        nrow.addWidget(b_nset)
        nrow.addStretch(1)
        root.addLayout(nrow)
        nhint = QLabel("STA saja / AP saja / keduanya. Channel AP mengikuti WiFi saat keduanya nyala.")
        nhint.setStyleSheet("color:#9aa4b2;font-size:11px;")
        nhint.setWordWrap(True)
        root.addWidget(nhint)

        self.status_lbl = QLabel("Status: -")
        root.addWidget(self.status_lbl)

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

    def _on_netmode_apply(self):
        self.netmode_apply_requested.emit(self.net_sta.isChecked(),
                                          self.net_ap.isChecked())

    # ---- API utk main window -------------------------------------------------
    def _sync_radio_boxes(self, j):
        """Sinkron checkbox STA/AP saja (tanpa sentuh teks status)."""
        if j.get("staEnable") is not None:
            self.net_sta.blockSignals(True)
            self.net_sta.setChecked(bool(j.get("staEnable")))
            self.net_sta.blockSignals(False)
        if j.get("apEnable") is not None:
            self.net_ap.blockSignals(True)
            self.net_ap.setChecked(bool(j.get("apEnable")))
            self.net_ap.blockSignals(False)

    def set_wifi(self, j):
        """Render status dari respons WIFIST//wifistat."""
        # v53: sinkron checkbox radio (tanpa emit; tak ada handler toggled).
        self._sync_radio_boxes(j)
        ap_extra = ""
        if j.get("apActive"):
            ap_extra = f" · AP \"{j.get('apSsid', '?')}\" nyala"
        if j.get("connected"):
            cust = " · kredensial kustom" if j.get("custom") else ""
            txt = (f"Terhubung ke \"{j.get('ssid','?')}\" · IP {j.get('ip','?')} · "
                   f"{j.get('rssi',0)} dBm{cust}{ap_extra}")
            col = "#7bd88f"
        elif j.get("pending"):
            txt = f"Mencoba menyambung ke \"{j.get('ssid','?')}\"...{ap_extra}"
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
        # v53: switch radio ikut live GET (sinkron lintas-client saat WebUI
        # mengubahnya); teks status tetap wewenang WIFIST.
        if "staEnable" in j or "apEnable" in j:
            self._sync_radio_boxes(j)
        txt = (f"revision={j.get('revision','-')} | "
               f"nvsDirty={j.get('nvsDirty','-')} | "
               f"lastSaveOk={j.get('lastSaveOk','-')} | "
               f"master={j.get('master','-')} | "
               f"preset=#{int(j.get('selectedPreset',-1))+1 if j.get('selectedPreset',-1)>=0 else '-'} | "
               f"sceneOn={j.get('sceneOn','-')}")
        self.status_lbl.setText("Status: " + txt)
