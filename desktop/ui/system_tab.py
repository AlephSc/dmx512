# Tab SISTEM: Save/Load NVS, Export ke file, status sinkronisasi.
from PySide6.QtCore import Signal
from PySide6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QLabel, QPushButton, QTextEdit


class SystemTab(QWidget):
    cmd = Signal(str)
    export_requested = Signal()
    import_requested = Signal()

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

        self.status_lbl = QLabel("Status: -")
        root.addWidget(self.status_lbl)

        root.addWidget(QLabel("Log:"))
        self.log_box = QTextEdit()
        self.log_box.setReadOnly(True)
        root.addWidget(self.log_box, 1)

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
