# Model state perangkat: hasil parse LISTF/LISTG/LISTP/LISTS/GET.
# Label channel per tipe fixture (mirror dari labelOf() di firmware web UI).


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
        """Nilai channel anggota pertama grup (sama dgn syncGroups web)."""
        if gi >= len(self.groups):
            return None
        g = self.groups[gi]
        cur = self.live.get("cur", {})
        for fi, fx in enumerate(self.fixtures):
            if fx.get("type") == g.get("type") and g.get("offset", 0) < fx.get("foot", 0):
                return cur.get(f"{fi}_{g['offset']}")
        return None

    def selected_preset(self):
        return int(self.live.get("selectedPreset", -1))

    def selected_scene(self):
        return int(self.live.get("selectedScene", -1))
