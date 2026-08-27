# Widget custom: fader vertikal dengan label & nilai, serta pad button.
from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import QSlider, QPushButton, QWidget, QVBoxLayout, QLabel

SLIDER_QSS = """
QSlider::groove:vertical {{ background:#232733; width:10px; border-radius:5px; }}
QSlider::sub-page:vertical {{ background:%COLOR%; width:10px; border-radius:5px; }}
QSlider::handle:vertical {{ background:#eee; height:14px; margin:-3px -3px; border-radius:7px; }}
"""


class VFader(QWidget):
    """Fader vertikal dengan label, nilai, dan sinyal tekanan/release."""

    valueChanged = Signal(int)
    pressed = Signal()
    released = Signal()

    def __init__(self, label="", color="#f5a623", small=False):
        super().__init__()
        self._key = None
        layout = QVBoxLayout(self)
        layout.setContentsMargins(2, 2, 2, 2)
        layout.setSpacing(2)
        # Slider
        self.slider = QSlider(Qt.Vertical)
        self.slider.setRange(0, 255)
        h = 90 if small else 140
        w = 28 if small else 32
        self.slider.setFixedHeight(h)
        self.slider.setFixedWidth(w)
        self.slider.setStyleSheet(SLIDER_QSS.replace("%COLOR%", color))
        self.slider.valueChanged.connect(self._on_change)
        self.slider.sliderPressed.connect(self.pressed)
        self.slider.sliderReleased.connect(self.released)
        # Nilai teks
        self.val_lbl = QLabel("0")
        self.val_lbl.setAlignment(Qt.AlignCenter)
        self.val_lbl.setObjectName("fVal")
        # Label nama
        self.name_lbl = QLabel(label.upper())
        self.name_lbl.setAlignment(Qt.AlignCenter)
        self.name_lbl.setObjectName("fName")
        layout.addWidget(self.slider, alignment=Qt.AlignHCenter)
        layout.addWidget(self.val_lbl)
        layout.addWidget(self.name_lbl)

    def _on_change(self, v):
        self.val_lbl.setText(str(v))
        self.valueChanged.emit(v)

    def set_value(self, v):
        """Set nilai secara programatik (tanpa emit sinyal valueChanged)."""
        self.slider.blockSignals(True)
        self.slider.setValue(int(v))
        self.val_lbl.setText(str(int(v)))
        self.slider.blockSignals(False)

    def value(self):
        return self.slider.value()

    def key(self):
        return self._key

    def set_key(self, k):
        self._key = k


class PadButton(QPushButton):
    """Tombol kotak besar untuk preset/scene, checkable & bisa dikustom warna."""

    def __init__(self, text="?", small=False):
        super().__init__(text)
        self.setCheckable(True)
        sz = (60, 48) if small else (80, 60)
        self.setMinimumSize(*sz)
        self.setObjectName("padBtn")
        self.style_sheet = "QPushButton#padBtn{background:#2a2e35;color:#ccc;border:1px solid #3a3f4b;border-radius:6px;}"
        self.setStyleSheet(self.style_sheet)

    def set_used(self, r, g, b, selected):
        lum = 0.299 * r + 0.587 * g + 0.114 * b
        fg = "#111" if lum > 140 else "#fff"
        bd = "#ffd54f" if selected else "#3a3f4b"
        self.setStyleSheet(
            f"QPushButton{{background-color:rgb({r},{g},{b});color:{fg};border:2px solid {bd};border-radius:8px;font-weight:bold;}}"
        )

    def clear_color(self, selected):
        self.setStyleSheet(f"QPushButton{{background:#2a2e35;color:#aaa;border:2px solid {'#ffd54f' if selected else '#3a3f4b'};border-radius:8px;}}")

    def set_hidden_in_scene(self, selected):
        """Preset disembunyikan (used=0) tapi masih dirujuk scene.

        Data channel/fade/hold tetap utuh di firmware; scene tetap memainkannya.
        Tampilan: latar gelap + border putus-putus oranye agar operator tahu
        slot ini tidak benar-benar kosong (v45, Bug 2).
        """
        bd = "#ffd54f" if selected else "#e67e22"
        self.setStyleSheet(
            f"QPushButton{{background:#1e2126;color:#e67e22;border:2px dashed {bd};border-radius:8px;font-weight:bold;}}"
        )
