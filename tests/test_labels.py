# Test channel_labels (desktop/state.py) — dijalankan manual: python tests/test_labels.py
# Aturan: foot < chart = dipotong; foot > chart = sisa CHn; tipe unknown = CHn.
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "desktop"))
from state import channel_labels  # noqa: E402

def check(name, got, want):
    assert got == want, f"{name}: got {got}, want {want}"
    print(f"  ok: {name}")

# PAR: base 4 + extra dipotong sesuai foot
check("PAR foot=9", channel_labels(0, 9),
      ["Dim", "R", "G", "B", "Strobe", "Mode", "Auto", "Speed", "Aux"])
check("PAR foot=4", channel_labels(0, 4), ["Dim", "R", "G", "B"])

# MOVING: chart 18 label; foot=20 -> sisa CH19, CH20
mv = channel_labels(1, 20)
assert len(mv) == 20 and mv[0] == "Pan" and mv[4] == "P/T Spd" and mv[5] == "Dim", mv
print("  ok: MOVING foot=20 (20 label, Pan..CH20)")

# MOVING foot pendek: dipotong
mv6 = channel_labels(1, 6)
assert mv6 == ["Pan", "PanF", "Tilt", "TiltF", "P/T Spd", "Dim"], mv6
print("  ok: MOVING foot=6 dipotong")

# BEAM: chart 16; foot=16 pas
bm = channel_labels(2, 16)
assert len(bm) == 16 and bm[7] == "Color" and bm[-1] == "Reset", bm
print("  ok: BEAM foot=16 pas")

# BEAM foot > chart tidak mungkin (chart 16, foot custom 18 -> CH17 CH18)
bm18 = channel_labels(2, 18)
assert len(bm18) == 18 and bm18[16] == "CH17" and bm18[17] == "CH18", bm18
print("  ok: BEAM foot=18 sisa CHn")

# Tipe unknown -> CHn
check("UNKNOWN t=9 foot=3", channel_labels(9, 3), ["CH1", "CH2", "CH3"])

# STROBE / FOG tetap
check("STROBE foot=4", channel_labels(3, 4), ["Mode", "Strobe", "Dim", "Color"])
check("FOG foot=2", channel_labels(4, 2), ["Fog", "Fan"])

print("ALL LABEL TESTS PASSED")
