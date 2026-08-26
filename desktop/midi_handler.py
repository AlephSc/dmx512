# MIDI input handler: membaca pesan controller MIDI (mido + python-rtmidi)
# dan menerjemahkannya ke perintah ESP32 lewat mapping yang bisa dikonfigurasi.
# Arsitektur: MIDI adalah INPUT device -> hasil translate dikirim via transport
# aktif (USB Serial / WiFi HTTP), jadi MIDI bekerja di kedua jalur koneksi.
import json
import os
import time

from PySide6.QtCore import QObject, Signal

try:
    import mido
    MIDI_AVAILABLE = True
except Exception:  # noqa: BLE001
    MIDI_AVAILABLE = False


def midi_to_dmx(v):
    """MIDI 0-127 -> DMX 0-255."""
    return int(v) * 255 // 127


# Aksi yang dikenal mapper
ACTIONS = [
    ("master", "Master dimmer (CC)"),
    ("strb", "Strobe master (CC)"),
    ("group", "Fader grup (CC, param=grup 0-7)"),
    ("chan", "Channel fixture (CC, param1=fixture, param2=ch)"),
    ("preset", "Mainkan preset (Note, param=0-15)"),
    ("scene_play", "Mainkan scene (Note, param=0-19)"),
    ("scene_stop", "Stop scene (Note)"),
    ("blackout", "Blackout ALL off (Note)"),
    ("all_full", "PAR Full ALL on (Note)"),
    ("chase_on", "Chase ON (Note)"),
    ("chase_off", "Chase OFF (Note)"),
]


def _default_map():
    """Mapping default ramah controller umum (nanoKONTROL2 / pad grid)."""
    cc = {}
    note = {}
    # CC 0-7 -> fader grup 0-7 (slider nanoKONTROL2 dll.)
    for i in range(8):
        cc[str(i)] = {"action": "group", "index": i}
    # CC 16/17 -> master & strobe (knob pertama)
    cc["16"] = {"action": "master"}
    cc["17"] = {"action": "strb"}
    # Note 36-51 -> preset 1-16 (pad grid 4x4)
    for i in range(16):
        note[str(36 + i)] = {"action": "preset", "index": i}
    # Note 64-67 -> aksi global
    note["64"] = {"action": "blackout"}
    note["65"] = {"action": "all_full"}
    note["66"] = {"action": "chase_on"}
    note["67"] = {"action": "chase_off"}
    # Note 70-89 -> scene 1-20 play; 90 = stop
    for i in range(20):
        note[str(70 + i)] = {"action": "scene_play", "index": i}
    note["90"] = {"action": "scene_stop"}
    return {"cc": cc, "note": note}


class MidiMapper:
    """Konfigurasi mapping MIDI -> perintah. Disimpan sebagai JSON."""

    def __init__(self, path=None):
        if path is None:
            base = os.path.dirname(os.path.abspath(__file__))
            path = os.path.join(base, "midi_map.json")
        self.path = path
        self.map = _default_map()
        self.load()

    def load(self):
        try:
            with open(self.path, "r", encoding="utf-8") as f:
                data = json.load(f)
            self.map = {"cc": data.get("cc", {}), "note": data.get("note", {})}
        except Exception:  # noqa: BLE001  (file belum ada / rusak -> pakai default)
            self.map = _default_map()

    def save(self):
        try:
            with open(self.path, "w", encoding="utf-8") as f:
                json.dump(self.map, f, indent=2)
            return True
        except Exception:  # noqa: BLE001
            return False

    def reset_defaults(self):
        self.map = _default_map()

    # ---- edit mapping ----
    def set_cc(self, num, action, p1=None, p2=None):
        e = {"action": action}
        if p1 is not None:
            e["index"] = int(p1)
        if p2 is not None and action == "chan":
            e["ch"] = int(p2)
        self.map["cc"][str(int(num))] = e

    def set_note(self, num, action, p1=None, p2=None):
        e = {"action": action}
        if p1 is not None:
            e["index"] = int(p1)
        if p2 is not None and action == "chan":
            e["ch"] = int(p2)
        self.map["note"][str(int(num))] = e

    def remove(self, kind, num):
        self.map[kind].pop(str(int(num)), None)

    # ---- translate pesan mido -> daftar perintah ----
    def translate(self, msg):
        cmds = []
        if msg.type == "control_change":
            e = self.map["cc"].get(str(msg.control))
            if not e:
                return []
            v = midi_to_dmx(msg.value)
            a = e["action"]
            if a == "master":
                cmds.append(f"MAST {v}")
            elif a == "strb":
                cmds.append(f"STRB {v}")
            elif a == "group":
                cmds.append(f"GRP {e.get('index', 0)} {v}")
            elif a == "chan":
                cmds.append(f"SET {e.get('index', 0)}_{e.get('ch', 0)}={v}")
        elif msg.type == "note_on" and msg.velocity > 0:
            e = self.map["note"].get(str(msg.note))
            if not e:
                return []
            a = e["action"]
            i = e.get("index", 0)
            if a == "preset":
                cmds += [f"SELP {i + 1}", f"PSL {i + 1}"]
            elif a == "scene_play":
                cmds.append(f"SPLAY {i + 1}")
            elif a == "scene_stop":
                cmds.append("SSTOP")
            elif a == "blackout":
                cmds.append("ALL off")
            elif a == "all_full":
                cmds.append("ALL on")
            elif a == "chase_on":
                cmds.append("CHASE on")
            elif a == "chase_off":
                cmds.append("CHASE off")
        return cmds


class MidiInputWorker(QObject):
    """Worker thread: baca port MIDI, emit perintah hasil translate."""

    command_ready = Signal(str)      # perintah untuk dikirim ke ESP32
    activity = Signal(str)           # teks aktivitas utk indikator UI
    error_occurred = Signal(str)
    started = Signal(str)
    stopped = Signal()
    learned = Signal(str, int)       # (kind 'cc'/'note', number) hasil MIDI-learn

    THROTTLE_MS = 30                 # interval minimum antar CC yang sama

    def __init__(self, mapper):
        super().__init__()
        self.mapper = mapper
        self._running = False
        self._learn = False
        self._port_name = None
        self._last_cc_time = {}

    def set_port(self, name):
        self._port_name = name

    def stop(self):
        self._running = False

    def set_learn(self, on):
        self._learn = bool(on)

    def run(self):
        if not MIDI_AVAILABLE:
            self.error_occurred.emit("Library MIDI (mido) tidak tersedia.")
            self.stopped.emit()
            return
        try:
            port = mido.open_input(self._port_name)
        except Exception as e:  # noqa: BLE001
            self.error_occurred.emit(f"Gagal buka MIDI '{self._port_name}': {e}")
            self.stopped.emit()
            return
        self.started.emit(self._port_name)
        self._running = True
        try:
            while self._running:
                for msg in port.iter_pending():
                    self._handle(msg)
                time.sleep(0.005)
        except Exception as e:  # noqa: BLE001
            if self._running:
                self.error_occurred.emit(f"MIDI error (device dicabut?): {e}")
        finally:
            try:
                port.close()
            except Exception:  # noqa: BLE001
                pass
            self._running = False
            self.stopped.emit()

    def _handle(self, msg):
        # Mode learn: tangkap CC/Note berikutnya lalu lapor ke UI
        if self._learn and msg.type in ("control_change", "note_on"):
            kind = "cc" if msg.type == "control_change" else "note"
            num = msg.control if kind == "cc" else msg.note
            self._learn = False
            self.activity.emit(f"LEARN: {kind.upper()} {num} tertangkap")
            self.learned.emit(kind, int(num))
            return
        cmds = self.mapper.translate(msg)
        if not cmds:
            return
        if msg.type == "control_change":
            self.activity.emit(f"CC {msg.control} = {msg.value}")
            # throttle: hindari banjir perintah saat fader digeser cepat
            now = time.time()
            last = self._last_cc_time.get(msg.control, 0.0)
            if (now - last) * 1000.0 < self.THROTTLE_MS:
                return
            self._last_cc_time[msg.control] = now
        else:
            self.activity.emit(f"Note {msg.note} ON")
        for c in cmds:
            self.command_ready.emit(c)
