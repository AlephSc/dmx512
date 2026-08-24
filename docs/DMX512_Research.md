# Riset Lengkap DMX512 untuk ESP32

## 1. Definisi dan Kegunaan DMX512

**DMX512** (Digital Multiplex 512) adalah standar komunikasi digital untuk mengontrol peralatan pencahayaan dan efek panggung. Dikembangkan oleh USITT (United States Institute for Theatre Technology) tahun 1986, direvisi menjadi standar ANSI E1.11-2008 (DMX512-A).

**Aplikasi:**
- Lighting control stage/teater
- Architectural lighting
- DJ equipment
- Intelligent lights/moving heads
- Fog machines, lasers
- LED strips DMX-compatible
- Stadium/arena lighting

## 2. Spesifikasi Teknis

### Physical Layer
- **Protokol:** RS-485 differential signaling
- **Baudrate:** 250 kbit/s (250000 baud)
- **Format:** 8N2 (8 data bits, no parity, 2 stop bits)
- **Voltage levels:** ±6V differential (RS-485 standard)
- **Impedansi kabel:** 120Ω twisted pair
- **Konektor standar:** XLR-5 (pin 1=GND, 2=Data-, 3=Data+, 4&5=secondary pair)
- **Konektor non-standar:** XLR-3 (umum di consumer equipment, dilarang standar)

### Timing Specifications
- **Break:** 88-176 μs (minimum 88 μs, typical 100-120 μs)
- **Mark After Break (MAB):** 8-16 μs (minimum 8 μs)
- **Slot time:** 44 μs per byte
- **Frame rate:** 1-830 fps (typical 25-44 fps untuk compatibility)
- **Inter-slot time:** 0-8 μs
- **Mark Time Between Frames (MTBF):** variabel, biasanya >1ms

### Packet Structure
```
[BREAK] [MAB] [START CODE] [CH1] [CH2] ... [CH512]
  88μs   8μs      1 byte    1 byte      ... (max 512 bytes)
```

## 3. Cara Kerja Protokol

### Frame Structure
1. **Break:** Sinyal LOW 88-176μs (start-of-packet marker)
2. **Mark After Break:** Sinyal HIGH 8-16μs
3. **Start Code:** 1 byte (0x00 untuk standard DMX, 0xCC untuk RDM)
4. **Data Slots:** 1-512 channels, masing-masing 1 byte (0-255)

### Start Code
- `0x00`: Standard DMX512 data
- `0x17`: ASCII text packet
- `0xCC`: RDM (Remote Device Management)
- `0xCF`: SIP (System Information Packet)
- Lainnya: reserved/manufacturer-specific

### Addressing & Channels
- Channel 1-512 (1-indexed)
- Nilai per channel: 0-255
- Devices set alamat start via DIP switch/menu
- Contoh: Moving head alamat 1, footprint 16 channel → gunakan CH 1-16

### Unidirectional Communication
- Controller (master) → Slave devices
- Continuous packet stream (tidak poll/request-response)
- No error checking/correction (no CRC/checksum)

## 4. Implementasi Hardware ESP32 + MAX485

### MAX485 IC
- RS-485 transceiver untuk half-duplex communication
- Supply: 5V (toleran 3.3V logic dari ESP32)
- Enable pins: DE (Driver Enable), RE (Receiver Enable)

### Koneksi DMX Bus
- Pin A (Data+) → DMX pin 3 (XLR)
- Pin B (Data-) → DMX pin 2 (XLR)
- GND → DMX pin 1

## 5. Pinout ESP32 ke MAX485

```
ESP32              MAX485              DMX XLR-5
-----              ------              ---------
TX2 (GPIO17) ----> DI                  
RX2 (GPIO16) <---- RO
GPIO21 (RTS) ----> DE, RE (tied together)
3.3V/5V ---------> VCC
GND -------------> GND ----------------> Pin 1 (Common)
                   A (Data+) ---------> Pin 3 (Data+)
                   B (Data-) ---------> Pin 2 (Data-)
```

**Mode Switching:**
- DE=HIGH, RE=LOW → Transmit mode
- DE=LOW, RE=HIGH → Receive mode
- Biasanya DE dan RE tied together, invert logic:
  - GPIO HIGH → TX mode
  - GPIO LOW → RX mode

**Termination:**
120Ω resistor antara A dan B di ujung terakhir daisy-chain.

## 6. Library Arduino/ESP32

### a) **esp_dmx** by someweisguy (Recommended)
- **Repo:** https://github.com/someweisguy/esp_dmx
- **Fitur:**
  - DMX512-A compliant
  - RDM support
  - Timing analysis/metadata
  - Synchronous/asynchronous read/write
  - Error checking
- **Install:** Arduino Library Manager atau PlatformIO
- **Framework:** Arduino-ESP32 >=2.0.3 atau ESP-IDF >=4.4.1

### b) **ESP-Dmx** by Rickgg
- **Repo:** https://github.com/Rickgg/ESP-Dmx
- **Fitur:** Basic DMX transmit untuk ESP8266/ESP32
- **Sederhana:** init(), write(), update(), end()
- **Keterbatasan:** TX only, tidak full-featured

### c) Library lain
- **DMXSerial2** (untuk AVR, adaptable)
- **SparkFun DMX Shield** library

## 7. Contoh Kode

### A. DMX Transmitter (esp_dmx)

```cpp
#include <esp_dmx.h>

const dmx_port_t dmxPort = DMX_NUM_1;
const int txPin = 17;
const int rxPin = 16;
const int enPin = 21;

void setup() {
  dmx_config_t config = DMX_CONFIG_DEFAULT;
  
  const int personality_count = 1;
  dmx_personality_t personalities[] = {
    {1, "Default Personality"}
  };
  
  dmx_driver_install(dmxPort, &config, personalities, personality_count);
  dmx_set_pin(dmxPort, txPin, rxPin, enPin);
}

void loop() {
  uint8_t data[DMX_PACKET_SIZE] = {0};
  
  data[0] = 255;
  data[1] = 128;
  data[2] = 0;
  
  dmx_write(dmxPort, data, DMX_PACKET_SIZE);
  dmx_send(dmxPort);
  dmx_wait_sent(dmxPort, DMX_TIMEOUT_TICK);
  
  delay(20);
}
```

### B. DMX Receiver (esp_dmx)

```cpp
#include <esp_dmx.h>

const dmx_port_t dmxPort = DMX_NUM_2;
const int txPin = 17;
const int rxPin = 16;
const int enPin = 21;

void setup() {
  Serial.begin(115200);
  
  dmx_config_t config = DMX_CONFIG_DEFAULT;
  const int personality_count = 1;
  dmx_personality_t personalities[] = {
    {1, "Default"}
  };
  
  dmx_driver_install(dmxPort, &config, personalities, personality_count);
  dmx_set_pin(dmxPort, txPin, rxPin, enPin);
}

void loop() {
  dmx_packet_t packet;
  if (dmx_receive(dmxPort, &packet, DMX_TIMEOUT_TICK)) {
    uint8_t data[DMX_PACKET_SIZE];
    dmx_read(dmxPort, data, packet.size);
    
    Serial.printf("CH1=%d, CH2=%d, CH3=%d\n", 
                  data[0], data[1], data[2]);
    
    if (packet.is_rdm) {
      rdm_send_response(dmxPort);
    }
  }
}
```

### C. Simple Transmitter (ESP-Dmx library)

```cpp
#include <ESPDMX.h>

DMXESPSerial dmx;

void setup() {
  dmx.init(512);
}

void loop() {
  dmx.write(1, 255);
  dmx.write(2, 128);
  dmx.write(3, 0);
  dmx.update();
  delay(20);
}
```

## 8. Topologi Jaringan DMX

### Daisy Chain
```
[Controller] ---> [Device 1] ---> [Device 2] ---> [Device 3] ---> [Terminator]
    OUT            IN   OUT        IN   OUT        IN   OUT          120Ω
```

**Aturan:**
- Max 32 devices per universe (rekomendasi, bisa lebih dengan buffer/splitter)
- Max cable length: 300-400m total
- Termination resistor 120Ω di ujung terakhir (antara Data+ dan Data-)

### DMX Splitter/Buffer
Untuk sistem besar:
- Optically isolated outputs
- Amplify/refresh signal
- Multiple output universes dari 1 input
- Prevent ground loops

### Ground Loops
- Signal common (pin 1) grounded di transmitter
- Receivers isolated (high impedance ke ground)
- Hindari multiple ground points

## 9. Perbedaan Transmitter vs Receiver

| Aspek | Transmitter | Receiver |
|-------|------------|----------|
| **Role** | Master/Controller | Slave/Device |
| **Direction** | Output data | Input data |
| **Timing** | Generate break+MAB | Detect break+MAB |
| **DE/RE control** | DE=HIGH (TX mode) | RE=LOW (RX mode) |
| **Grounding** | Signal common grounded | Signal common isolated |
| **Connector** | Female XLR OUT | Male XLR IN |
| **Continuous TX** | Ya, loop packets | Tidak, listen only |

**Hardware sama**, software berbeda:
- TX: serial.write() dengan timing control
- RX: serial.read() dengan break detection

## 10. Best Practices dan Common Pitfalls

### Best Practices

1. **Kabel:**
   - Gunakan kabel DMX dedicated (120Ω impedance)
   - Hindari kabel audio XLR (impedansi berbeda)
   - Shielded twisted pair
   - Jangan gunakan Cat5/Ethernet cable (tidak differential, noise)

2. **Termination:**
   - Wajib 120Ω resistor di ujung chain
   - Tanpa terminator: reflections, glitches, flickering

3. **Power Supply:**
   - MAX485 lebih stabil dengan 5V (bukan 3.3V)
   - Decouple capacitor 100nF di VCC MAX485
   - Pisahkan ground digital dan DMX ground via optocoupler (optional, untuk galvanic isolation)

4. **Timing:**
   - Jangan terlalu cepat (>44fps bisa crash old devices)
   - Break 100-120μs (aman untuk compatibility)
   - MAB 12μs (aman)

5. **Software:**
   - Gunakan hardware UART, bukan bit-banging
   - ESP32 punya 3 UART (Serial0, Serial1, Serial2)
   - Hindari Serial0 (USB debug)

6. **Testing:**
   - Test dengan DMX tester/analyzer
   - Monitor timing dengan oscilloscope
   - Test dengan different devices (old dimmers, modern LEDs)

### Common Pitfalls

1. **Kabel audio sebagai DMX:**
   - Impedansi salah → signal degradation
   - Range pendek, unreliable

2. **Tidak pakai terminator:**
   - Signal reflection
   - Flickering, intermittent errors
   - Devices skip frames

3. **Ground loops:**
   - Multiple ground points
   - Noise, buzzing
   - Solusi: opto-isolation, single ground point

4. **Timing error:**
   - Break terlalu pendek (<88μs): devices tidak detect start
   - Frame rate terlalu tinggi: old devices crash
   - Solusi: strict timing adherence

5. **DE/RE control salah:**
   - Bus contention jika multiple TX active
   - Garbled data
   - Pastikan hanya 1 transmitter per universe

6. **Power 3.3V untuk MAX485:**
   - MAX485 spec 5V
   - 3.3V marginal, bisa tidak stabil
   - Level shifter atau direct 5V supply

7. **Voltage level mismatch:**
   - ESP32 3.3V logic, MAX485 bisa 5V tolerant
   - DI pin MAX485 biasanya aman 3.3V
   - Cek datasheet untuk VIH threshold

8. **Channel indexing:**
   - DMX channel 1-indexed (1-512)
   - Array C/C++ 0-indexed
   - data[0] = DMX channel 1

9. **Phantom power (XLR-3):**
   - XLR-3 audio bisa carry 48V phantom power
   - Rusak DMX device jika accidental connection
   - Gunakan XLR-5 atau label jelas

10. **No error handling:**
    - DMX tidak punya checksum/CRC
    - Noise → corrupt data
    - Implement timeout detection di receiver
    - Monitor packet interval

### Debugging Tips

- **Oscilloscope:** Check break/MAB timing, voltage levels
- **DMX Tester:** Verify packet format, channel values
- **LED indicator:** Blink per packet sent/received
- **Serial monitor:** Print channel values, timing
- **Known-good device:** Test chain dengan commercial DMX fixture

## Referensi

- ANSI E1.11-2008 (DMX512-A standard)
- ANSI E1.20 (RDM standard)
- Wikipedia: https://en.wikipedia.org/wiki/DMX512
- esp_dmx library: https://github.com/someweisguy/esp_dmx
- ESP-Dmx library: https://github.com/Rickgg/ESP-Dmx

---
**Catatan:** DMX512 tidak cocok untuk aplikasi safety-critical (pyrotechnics, rigging) karena tidak ada error correction. Untuk aplikasi tersebut gunakan protokol dengan ACK/NACK seperti RDM atau protokol industrial.
