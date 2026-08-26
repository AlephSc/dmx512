# Model state perangkat: hasil parse LISTF/LISTG/LISTP/LISTS/GET.
# Label channel per tipe fixture (mirror dari labelOf() di firmware web UI).
import json


def channel_labels(ftype, foot):
    if ftype == 0:  # PAR
        base = ["Dim", "R", "G", "B"]
        extra = ["Strobe", "Mode", "Auto", "Speed", "Aux", "R2", "G2", "B2"]
        return base + extra[: max(0, foot - 4)]
    if ftype == 4:  # FOG
        return ["Fog", "Fan"][:foot]
    if ftype == 3:  # STROBE
        return ["Mode", "Strobe", "Dim", "Color"][:foot]
    return [f"CH{i+1}" for i in range(foot)]


class DeviceState:
    """Cermin state ESP32. Semua data mentah (dict/list) dari serial."""

    def __init__(self):
        self.fixtures = []   # LISTF: [{name,type,start,foot}]
        self.groups = []     # LISTG: [{name,type,offset}]
        self.presets = []    # LISTP: [{n,used,r,g,b,f,h}]
        self.scenes = []     # LISTS: 20 x 50 int (0=kosong, 1..30=preset)
        self.live = {}       # GET  : state realtime

    # ---- akses nyaman ----------------------------------------------------
    def preset(self, idx0):
        """Preset utk index 0-based; None bila belum ada data."""
        if idx0 < len(self.presets):
            return self.presets[idx0]
        return None

    def group_value(self, gi):
        """Nilai channel anggota pertama grup (kompatibilitas lama)."""
        if gi >= len(self.groups):
            return None
        g = self.groups[gi]
        cur = self.live.get("cur", {})
        for fi, fx in enumerate(self.fixtures):
            if fx.get("type") == g.get("type") and g.get("offset", 0) < fx.get("foot", 0):
                return cur.get(f"{fi}_{g['offset']}")
        return None

    def group_values(self, gi):
        """Semua nilai member grup (untuk aturan 'seragam baru sync')."""
        vals = []
        if gi >= len(self.groups):
            return vals
        g = self.groups[gi]
        cur = self.live.get("cur", {})
        for fi, fx in enumerate(self.fixtures):
            if fx.get("type") == g.get("type") and g.get("offset", 0) < fx.get("foot", 0):
                v = cur.get(f"{fi}_{g['offset']}")
                if v is not None:
                    vals.append(v)
        return vals

    def selected_preset(self):
        return int(self.live.get("selectedPreset", -1))

    def selected_scene(self):
        return int(self.live.get("selectedScene", -1))

    def set_scenes(self, raw):
        """Normalisasi data LISTS menjadi list-of-list-of-int.

        Toleran terhadap variasi bentuk dari berbagai versi firmware:
        scene bisa berupa list int, atau string JSON ("[1,2,0]"). Nilai
        yang tidak bisa dikonversi dianggap 0. Tanpa ini, ScenesTab bisa
        crash membandingkan str dengan int ('>' not supported...).
        """
        out = []
        if not isinstance(raw, list):
            self.scenes = []
            return
        for sc in raw:
            if isinstance(sc, str):
                try:
                    sc = json.loads(sc)
                except Exception:  # noqa: BLE001
                    sc = []
            if isinstance(sc, list):
                row = []
                for v in sc:
                    try:
                        row.append(int(v))
                    except (TypeError, ValueError):
                        row.append(0)
                out.append(row)
            else:
                out.append([])
        self.scenes = out
