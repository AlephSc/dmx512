/*
  DMX Web Console - versi FINAL (industrial)
  ESP32 + MAX485, library Dmx_ESP32 (Little Art Bear), Arduino-ESP32 core.

  FITUR
  - Patch table generik: 10 PAR (foot 9) + 8 fixture (moving/beam/strobe/fog).
  - UI Web responsif (PC/HP): slider realtime, master dimmer, fade, chase,
    blackout, 16 bank preset.
  - Persistensi NVS (flash): preset tetap tersimpan saat listrik mati.
    (NVS = non-volatile storage di flash, prinsip sama dgn HDD/SSD, tahan reboot.)
  - Import/Export preset dalam format JSON (portabel antar device buatan sendiri).
  - DMX timing berjalan pada Core 0 (task realtime), WebServer pada Core 1 (loop).
    Dipisah core supaya WiFi tidak mengganggu timing DMX dan tidak hang/freeze.
  - Sinkronisasi antar-core memakai FreeRTOS mutex -> aman dari race.
  - CATATAN KEAMANAN: endpoint web tanpa autentikasi (siapa pun di jaringan AP
    bisa mengontrol). Untuk venue publik, ganti password AP di bawah.

  STANDAR INDUSTRI (DMX512-A / ANSI E1.11)
  - RS-485 250 kbaud, 8N2, unidirectional.
  - Break di-set 100us (rentang standar 88-176us), MAB dihasilkan library.
  - Frame dikirim ~40x/detik (interval 25ms) -> lampu tidak timeout.
  - Terminator 120 Ohm WAJIB di ujung daisy-chain (antara Data+ dan Data-).
  - Kabel DMX dedicated 120 Ohm shielded pair (bukan kabel audio).

  LAYOUT UNIVERSE (blok per tipe, sisa spare)
    PAR1-10 : foot 9    -> 1-90
    MOVING1 : 91-110 (foot 20)   MOVING2 : 111-130
    BEAM1   : 131-146 (foot 16)  BEAM2   : 147-162
    STROBE1 : 163-166 (foot 4)   STROBE2 : 167-170
    FOG1    : 171-172 (foot 2)   FOG2    : 173-174
    (>174 = spare; tambah fixture cukup edit array fix[])

  OVERVIEW PERSISTENCE & WIRE
  - ESP32 menyimpan preset di NVS flash -> tidak hilang saat power off.
  - Simpan NVS hanya saat user merekam (bukan tiap frame) -> flash awet.
  - Catu: MAX485 perlu 5V stabil; signal ESP32 ke DI aman (VIH ~2V).
    Jika menggunakan RX (RO) dgn MAX485 5V, pasang voltage divider 1k+2k
    ke GPIO16 supaya tidak makan pin 3.3V.

  KONEKSI
  Terhubung ke WiFi "SIGMA" (mode station). IP tampil di Serial Monitor (115200).
  Bila SIGMA gagal tersambung, ESP32 otomatis membuat AP darurat
  "DMX-RGB" (password 12345678) -> buka http://192.168.4.1
 */
#include <WiFi.h>
#include <WebServer.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <Arduino.h>
#include "Dmx_ESP32.h"
#include <ETH.h>           // W5500 & TCP/IP stack (core 3.x+)
#include <atomic>          // v47: stateRevision/sceneRev lintas-core atomic

// v45: forward declaration utk Arduino IDE auto-prototype generation.
// Tanpa ini, prototype fungsi yang memakai `Fixture*` di-generate sebelum
// struct didefinisikan -> error "'Fixture' does not name a type".
struct Fixture;

// =============================================================
// PIN ETHERNET W5500 (VSPI default ESP32) — sambungkan ke modul W5500:
//   W5500 VCC → 3.3V  (modul ada regulator -> bisa terima 5V input via DC jack)
//   W5500 GND → GND
//   SCLK    → GPIO 18 (VSPI MISO: 19, MOSI: 23)
//   CS      → GPIO 5  (chip select; tidak dipakai pin boot)
//   RST     → tie to 3.3V via 10k pull-up (GPIO -1 = auto)
// =============================================================
#define ETH_CS    5
#define ETH_RST   -1    // -1 = tidak gunakan GPIO reset
#define ETH_IRQ   -1    // -1 = tidak gunakan IRQ (polling mode lebih stabil untuk kasus ini)

// Tag build: tampil di header UI & Serial. Kalau tag lama masih tampil di
// browser setelah upload -> berarti cache/upload bermasalah, bukan kodenya.
#define BUILD_TAG "v49"

// ---------------------------------------------------------------
// WIFI - Station (konek ke router), fallback AP darurat
// ---------------------------------------------------------------
const char* WIFI_SSID = "SIGMA";
const char* WIFI_PASS = "1ngantos12";

// AP darurat bila WiFi SIGMA gagal tersambung (supaya tidak terkunci dari device)
const char* AP_SSID = "DMX-RGB";
const char* AP_PASS = "12345678";

// IP aktif sesuai mode (Ethernet > WiFi STA > AP fallback)
IPAddress activeIP(){
  if(ETH.linkUp() && ETH.localIP()!=IPAddress(0,0,0,0)) return ETH.localIP();   // Ethernet menang bila ada
  if((WiFi.getMode() & WIFI_STA) && WiFi.status()==WL_CONNECTED) return WiFi.localIP();
  return WiFi.softAPIP();
}

// v43: kredensial WiFi kustom (diatur dari Web UI / desktop, persist di NVS
// namespace terpisah "dmxwifi" agar tidak ikut terhapus migrasi storage).
Preferences wifiNvs;
String customSsid, customPass;        // kosong = pakai default bawaan
bool     wifiPending=false;           // reconnect kustom sedang berlangsung
uint32_t wifiTryAt=0;
int      wifiTryCount=0;
void loadWifiCreds(){
  if(!wifiNvs.begin("dmxwifi",true)){
    customSsid="";
    customPass="";
    return;
  }
  customSsid = wifiNvs.getString("ssid","");
  customPass = wifiNvs.getString("pass","");
  wifiNvs.end();
}
const char* effSsid(){ return customSsid.length()>0 ? customSsid.c_str() : WIFI_SSID; }
const char* effPass(){ return customSsid.length()>0 ? customPass.c_str() : WIFI_PASS; }

// ---------------------------------------------------------------
// PIN ESP32 -> MAX485
// ---------------------------------------------------------------
#define DMX_TX_PIN      17
#define DMX_RX_PIN      16
#define DMX_ENABLE_PIN  4

// ---------------------------------------------------------------
// DMX (library Dmx_ESP32)
// ---------------------------------------------------------------
HardwareSerial DMXSerial(2);
dmxTx DMX(&DMXSerial, DMX_TX_PIN, DMX_ENABLE_PIN);

// ---------------------------------------------------------------
// PATCH TABLE
// ---------------------------------------------------------------
enum FX { FX_PAR, FX_MOVING, FX_BEAM, FX_STROBE, FX_FOG };
struct Fixture {
  char name[25];         // v45: mutable (bisa diedit via Patch panel)
  uint8_t type;
  uint16_t start;
  uint16_t foot;
  uint8_t  hasMove;      // 1 = fixture bergerak (pan/tilt di ch0 & ch2)
};

// v45: MAX_FIX = kapasitas maksimum array; N_FIX = jumlah aktif (runtime,
// bisa diubah via Patch panel). Default 18 sesuai patch bawaan.
#define MAX_FIX 32
#define DEFAULT_N_FIX 18
static uint8_t N_FIX = DEFAULT_N_FIX;   // jumlah fixture aktif (runtime)

// Inisialisasi default dipanggil saat NVS kosong / pertama kali boot.
static const Fixture FIX_DEFAULT[MAX_FIX] = {
  { "PAR 1",    FX_PAR,     1,  9, 0 },
  { "PAR 2",    FX_PAR,    10,  9, 0 },
  { "PAR 3",    FX_PAR,    19,  9, 0 },
  { "PAR 4",    FX_PAR,    28,  9, 0 },
  { "PAR 5",    FX_PAR,    37,  9, 0 },
  { "PAR 6",    FX_PAR,    46,  9, 0 },
  { "PAR 7",    FX_PAR,    55,  9, 0 },
  { "PAR 8",    FX_PAR,    64,  9, 0 },
  { "PAR 9",    FX_PAR,    73,  9, 0 },
  { "PAR 10",   FX_PAR,    82,  9, 0 },
  { "MOVING 1", FX_MOVING, 91, 20, 1 },
  { "MOVING 2", FX_MOVING,111, 20, 1 },
  { "BEAM 1",   FX_BEAM,  131, 16, 1 },
  { "BEAM 2",   FX_BEAM,  147, 16, 1 },
  { "STROBE 1", FX_STROBE,163,  4, 0 },
  { "STROBE 2", FX_STROBE,167,  4, 0 },
  { "FOG 1",    FX_FOG,   171,  2, 0 },
  { "FOG 2",    FX_FOG,   173,  2, 0 },
};
Fixture fix[MAX_FIX];   // patch table aktif (runtime, bisa diubah)

// ---------------------------------------------------------------
// v48: CUSTOM FIXTURE TYPE — tipe fixture yang didefinisikan user
// Slot tipe 5..15 (FX_PAR..FX_FOG = 0..4; t>=5 merujuk customSlots[t-5]).
// Mode per-channel: 0=fader (0-255 bebas), 1=switch (binary 0/255 — untuk
// beban on/off seperti relay 12V yang rusak jika dapat nilai antara).
// ---------------------------------------------------------------
#define CUSTOM_TYPE_MIN 5
#define CUSTOM_TYPE_MAX 15              // 11 slot custom (5..15)
#define N_CUSTOM_TYPES (CUSTOM_TYPE_MAX - CUSTOM_TYPE_MIN + 1)
#define CUSTOM_IDX(t) ((t)-CUSTOM_TYPE_MIN)   // type -> index array
#define CUSTOM_MAX_CH 32               // label/mode per tipe, maks 32 channel
struct CustomType {
  uint8_t  used;                // slot terpakai?
  char     name[17];            // nama tipe, maks 16 char
  uint8_t  channels;            // 1..CUSTOM_MAX_CH
  uint8_t  mode[CUSTOM_MAX_CH]; // 0=fader, 1=switch, per channel
  char     labels[CUSTOM_MAX_CH][9]; // nama slider per channel (8 char + NUL)
};
static CustomType customSlots[N_CUSTOM_TYPES];

// helper: mode channel (0 fader / 1 switch) utk tipe custom; tipe bawaan = 0
bool customTypeMode(uint8_t type, uint16_t ch){
  if(type<CUSTOM_TYPE_MIN||type>CUSTOM_TYPE_MAX) return 0;
  CustomType &c=customSlots[CUSTOM_IDX(type)];
  if(!c.used || ch>=CUSTOM_MAX_CH) return 0;
  return c.mode[ch];
}
// v48: snap binary utk channel mode-switch (relay/on-off). FIRMWARE = sumber
// kebenaran — desktop/API lama yang kirim nilai tengah tetap di-snap di sini,
// mencegah beban on-off (relay) menerima tegangan antara.
static inline uint8_t snapSwitchMode(uint8_t type, uint16_t localCh, int v){
  if(v>=0 && customTypeMode(type,(uint16_t)localCh)) return (v<128)?0:255;
  return cv(v);
}

void loadDefaultFixtures(){
  N_FIX = DEFAULT_N_FIX;
  for(int i=0; i<DEFAULT_N_FIX; i++) fix[i] = FIX_DEFAULT[i];
  for(int i=DEFAULT_N_FIX; i<MAX_FIX; i++){ fix[i].name[0]=0; fix[i].type=0; fix[i].start=0; fix[i].foot=0; fix[i].hasMove=0; }
}

// ---------------------------------------------------------------
// PRESET & STATE
// ---------------------------------------------------------------
#define N_PRESETS 30
// chunk: [0]=used, [1..512]=nilai channel, [513]=fade/10ms, [514]=hold/20ms
#define PRESET_CHUNK 515

static uint8_t want[513];         // target hasil mix (diproses fadeTick -> out)
static uint8_t out[513];          // nilai tampilan (hasil fade)

// =============================================================
// HTP/LTP MIXER (sejak v32)
// Sumber channel DMX dipisah jadi dua layer lalu di-mix per channel:
//   manualWant[] <- slider/fader manual (onSet, onGroup, onCtrl all)
//   pbWant[]     <- playback: preset, scene, chase (applyPresetToWant)
// Hasil mix masuk want[] (diproses fadeTick -> out -> buildFrame).
//   * HTP (Highest Takes Precedence): channel LEVEL/intensitas.
//     Nilai tertinggi menang -> dua look tidak saling meredupkan.
//   * LTP (Latest Takes Precedence): channel ATRIBUT (pan/tilt/gobo/dll).
//     Yang disentuh terakhir menang, berapa pun nilainya.
// =============================================================
static uint8_t manualWant[513];
static uint8_t pbWant[513];
static volatile uint32_t manualTouched[513];   // millis tulis manual terakhir
static volatile uint32_t pbTouched[513];       // millis tulis playback terakhir

// Gabung manual+playback ke want[] sesuai aturan HTP/LTP.
// Panggil SETELAH salah satu layer ditulis (tetap di dalam mutex DMX).
// Note (v34): semua channel dipakai LTP berdasarkan timestamp sentuh terakhir;
//   manualWant/pbWant tidak lagi dipisahkan via max(). Dengan ini manual
//   bisa meredupkan/mematikan playback, dan playback baru otomatis mengganti
//   posisi fader manual yang sudah tidak digarap lagi.
static inline bool timestampNewer(uint32_t a, uint32_t b){
  // Aman terhadap wrap millis() selama selisih timestamp < 2^31 ms.
  return (int32_t)(a-b)>0;
}
void recomputeWant(){
  // v49: mix TIGA layer LTP timestamp: manual (fader) > playback (preset/
  // scene/chase) > network (Art-Net). Semua timestamp millis — paket ArtDmx
  // menang bila datang SETELAH sentuhan lokal terakhir. Tie memihak
  // playback (state boot deterministik).
  for(int f=0;f<N_FIX;f++)for(uint16_t c=0;c<fix[f].foot;c++){
    uint16_t ch=fix[f].start+c;
    uint32_t mT=manualTouched[ch], pT=pbTouched[ch], nT=netTouched[ch];
    uint8_t  mV=manualWant[ch], pV=pbWant[ch], nV=netWant[ch];
    // pilih sumber dengan timestamp terbaru (tie: playback > manual > net)
    uint32_t bestT=pT; uint8_t bestV=pV;
    if(timestampNewer(mT,bestT)){ bestT=mT; bestV=mV; }
    if(timestampNewer(nT,bestT)){ bestT=nT; bestV=nV; }
    want[ch]=bestV;
  }
}
static volatile uint8_t masterOut = 255; static volatile uint8_t masterWant = 255;

// STROBE MASTER: 0=nonaktif; >0 = SELURUH output di-gate kotak on/off.
// Nilai besar = kedip cepat: half-period 2000ms (v=1) .. 40ms (v=255).
// Efek global sesaat: menimpa tampilan scene/chase/fader tanpa mengubah datanya.
static volatile uint8_t strobeWant = 0;
static uint32_t strobeNextAt = 0;      // hanya disentuh buildFrame (Core0)
static bool strobePhase = true;        // true=fase ON
static volatile uint32_t fadeMs = 600;
static volatile bool chaseOn = false; static volatile uint32_t chaseMs = 1500; static volatile int chaseIdx = -1;

static uint8_t presets[N_PRESETS][PRESET_CHUNK];   // chunk: [0]=used, [1..512]=nilai

// Blackout-on-move: batas waktu (ms) saat dimmer fixture dipaksa 0 karena
// pan/tilt bergerak jauh (LTP). Diisi saat apply preset.
// v45: ukuran MAX_FIX agar aman saat N_FIX berubah runtime.
static volatile uint32_t blackoutEnd[MAX_FIX] = {0};
// ---------------------------------------------------------------
// SCENE: rangkaian hingga 50 langkah preset (referensi nomor, bukan salinan)
// 0 = langkah kosong; 1..N_PRESETS = nomor preset. Disimpan terpisah di NVS.
// ---------------------------------------------------------------
#define N_SCENES 20
#define SCENE_STEPS 50
static uint8_t scenes[N_SCENES][SCENE_STEPS];
static volatile bool sceneOn = false;
static volatile int sceneIdx = -1;     // scene yang sedang diputar
static volatile int sceneStep = -1;    // posisi langkah terakhir
static volatile uint32_t sceneMs = 1500;

// State UI yang authoritative di ESP32, bukan hanya di browser.
// v47: stateRevision & sceneRev kini std::atomic — dua-satunya counter yang
// di-increment dari Core 0 (dmxTask: applyPresetToWant) DAN dibaca Core 1
// (web/ws/serial). `volatile` tidak menjamin atomicity ++ di Xtensa.
static volatile int selectedPreset = -1;
static volatile int selectedScene = -1;
static std::atomic<uint32_t> stateRevision{1};
static volatile bool nvsDirty = false;
static volatile bool lastSaveOk = true;
static volatile uint32_t lastSaveAt = 0;
static volatile uint32_t dmxHeartbeat = 0;

// v46: sceneRev ikut naik saat isi scenes[] berubah (termasuk alih referensi
// COW dari client lain) -> client tahu kapan harus reload /scenes.
static std::atomic<uint32_t> sceneRev{1};
// Deadline playback di-reset ketika PLAY/CHASE dimulai. Ini mencegah timer
// static lama membuat langkah pertama kadang terlambat atau tidak konsisten.
static volatile uint32_t chaseNextAt = 0;
static volatile uint32_t sceneNextAt = 0;

// ---------------------------------------------------------------

static volatile uint8_t sceneError = 0;

// Sinkronisasi antar-core (Core0 DMX <-> Core1 WiFi).
SemaphoreHandle_t dmxMutex = NULL;


// v49: ART-NET INPUT (node)
 — sumber kontrol dari QLC+/xLights/Resolume.
// Layer ketiga "netWant" di mixer LTP-timestamp (paritas manual/pbWant).
// Standar yang diikuti (Art-Net 4, spec 1.4):
//   - Header "Art-Net\0" + OpCode 0x5000 OpDmx (LSB-first)
//   - ProtVer Lo=14
//   - Sequence 0 = non-sequenced; selisih 1..20 = out-of-order window
//     (spec 1.4) — duplikat(0) / selisih>20 di-drop
//   - SubUni/SubNet: hanya universe 0:0 diterima (1 universe fisik,
//     padanan node Art-Net 1-port standar QLC+)
// Mode operasi (UI): LOCAL (default) / NETWORK (aktif).
// Merge: LTP timestamp — paritas perilaku fader manual kita.
// ---------------------------------------------------------------
#include <WiFiUdp.h>
static WiFiUDP artnetUdp;
static bool     artnetMode = false;         // false=LOCAL, true=NETWORK
static uint8_t  netWant[513];
static volatile uint32_t netTouched[513];
static volatile uint32_t artnetLastAt = 0;   // ms paket terakhir (indikator)
static volatile uint32_t artnetPktCount = 0; // total paket diterima
static uint8_t  artnetLastSeq = 0;           // sequence tracking
static uint32_t artnetBindMs = 0;            // mulai listen

void artnetBegin(){
  artnetUdp.stop();
  if(artnetUdp.begin(6454)==0){
    Serial.println("Art-Net: GAGAL bind UDP 6454 (socket penuh?)");
    return;
  }
  artnetBindMs = millis();
  Serial.println("Art-Net: listening UDP 6454 (mode NETWORK)");
}

// v49: ArtPollReply — identitas node utk discovery QLC+/xLights.
// Layout 208 byte (spec 1.4). Field penting:
//   NodeReport status 0x0 = ready; PortTypes bit7=output DMX; GoodOutput
//   bit7=data receiving; SwIn/SwOut = universe port (0). NetSwitch/SubSwitch
//   = 0. Panjang IP = 4 byte. Versi firmware di NodeReport teks.
// Ini yang membuat node muncul di input map QLC+ secara OTOMATIS.
void artnetSendPollReply(IPAddress to){
  uint8_t r[208];
  memset(r,0,sizeof(r));
  memcpy(r,"Art-Net\0",8);
  r[8]=0x00; r[9]=0x21;                    // OpCode 0x2100 ArtPollReply (LSB)
  r[10]=0; r[11]=14;                       // ProtVer
  IPAddress ip = activeIP();
  memcpy(r+12, &ip[0], 4);                 // IP
  r[16]=0x36; r[17]=0x19;                  // Port 0x1936 (6454, hi-lo)
  r[18]=0;                                // NetSwitch = 0 (default Art-Net 4 port-address rendah)
  r[19]=0;                                 // SubSwitch
  r[20]=0x05;                              // OEM hi (placeholder; OEM code 0x05FF-class custom)
  r[21]=0xFF;
  r[22]=0xA1;                              // Ubea + status1 ready
  memcpy(r+26,"DMX Web Console",15);       // ShortName (18 b)
  String rep=String("DMX Web Console ")+String(BUILD_TAG)+" #"+String(artnetPktCount);
  rep.toCharArray((char*)r+44,60);         // LongName (64 b)
  strncpy((char*)r+108,"OK",32);           // NodeReport (64 b)
  r[172]=3;                                // NumPortsLo = 1 port input (kita node INPUT utk software)
  r[173]=0;                                // NumPortsHi
  r[174]=0xC0;                             // PortTypes b0: bit7=DMX + bit6=input
  r[175]=0x80;                             // GoodInput b0: bit7=data receiving (mode network)
  r[176]=0x80;                             // GoodOutput b0: bit7=transmitting (utk software ini = output kita)
  r[177]=0; r[178]=0;                      // SwIn b0 / SwOut b0 = universe 0
  r[179]=0; r[180]=0;
  r[181]=0; r[182]=0; r[183]=0; r[184]=0;  // SwIn/Out 1..3 kosong
  r[185]=0; r[186]=0; r[187]=0; r[188]=0;
  r[190]=0; r[191]=3;                      // Status2: bit0=webbrowser config, bit1=artnet
  artnetUdp.beginPacket(to, 6454);
  artnetUdp.write(r, sizeof(r));
  artnetUdp.endPacket();
}

// Parse & apply satu datagram ArtDmx. Dipanggil dari loop() (Core 1).
// Buffer stack 530 byte — nol alokasi heap (pelajaran fragmentasi).
void artnetTask(){
  if(!artnetMode) return;
  int n = artnetUdp.parsePacket();
  if(n < 18) return;                       // header ArtDmx min 18 byte
  uint8_t p[530];
  int len = artnetUdp.read(p, sizeof(p));
  if(len < 18) return;
  if(memcmp(p, "Art-Net\0", 8) != 0) return;
  uint16_t op = (uint16_t)p[9] << 8 | p[8];           // OpCode LSB-first
  if(op == 0x2000){                                    // ArtPoll -> balas ArtPollReply
    artnetSendPollReply(artnetUdp.remoteIP());
    artnetLastAt = millis();
    return;
  }
  if(op != 0x5000) return;                             // hanya OpDmx
  if(p[10] != 0 || p[11] < 14) return;                 // ProtVer 14
  uint8_t seq = p[12];
  uint8_t subUni = p[14];
  uint8_t subNet = p[15];
  if(seq != 0 && artnetPktCount > 0){
    uint16_t diff = (uint16_t)(seq - artnetLastSeq);
    if(diff == 0 || diff > 20){ artnetLastSeq = seq; return; }    // drop
  }
  artnetLastSeq = seq;
  if(subUni != 0 || subNet != 0) return;                // universe 0 saja
  uint16_t dlen = ((uint16_t)p[16] << 8) | p[17];
  if(dlen < 2) dlen = 2;
  if(dlen > 512) dlen = 512;
  if((int)(18 + dlen) > len) dlen = (uint16_t)(len - 18);
  xSemaphoreTake(dmxMutex, portMAX_DELAY);
  uint32_t now = millis();
  memcpy(netWant, p + 18, dlen);
  for(int i = 0; i < dlen; i++) netTouched[i+1] = now;  // DMX ch1 = data[0]
  xSemaphoreGive(dmxMutex);
  artnetLastAt = now;
  artnetPktCount++;
}

// ---------------------------------------------------------------

// v49: INPUT FISIK — rotary encoder + 4 tombol scene (hardware button deck)
// Wiring (semua INPUT_PULLUP, aktif LOW, tombol/encoder ke GND):
//   Encoder EC11: CLK GPIO25, DT GPIO26, (SW GPIO13 opsional = STOP)
//   Tombol scene: B1 GPIO32, B2 GPIO33, B3 GPIO27, B4 GPIO14
// Pin dipilih yang aman (bukan boot-strapping 0/2/12/15, bukan flash 6-11,
// bukan DMX 16/17/4, bukan SPI 18/19/23/5) dan SEMUA mendukung pull-up
// internal -> nol resistor eksternal.
// Perilaku (paritas /splay + SPUSH):
//   B1-B4   = play scene (bank aktif + 0..3), debounce 30 ms
//   Encoder = geser "bank scene" kelipatan 4 (0,4,8,16) — B1 = scene bank+1
//   SW      = stop playback (scene & chase off)
// Nilai terekspos di state JSON (hwB1..B4, hwBank, hwEnc) -> website bisa
// menampilkan deck fisik; tombol web vs fisik = sumber state yang sama.
// Polling (bukan ISR): debounce sederhana, tak menyentuh mutex dari
// interrupt context. Encoder quadrature via tabel 16-state standar.
// ---------------------------------------------------------------
#define HW_ENC_CLK 25
#define HW_ENC_DT  26
#define HW_ENC_SW  13
#define HW_BTN1    32
#define HW_BTN2    33
#define HW_BTN3    27
#define HW_BTN4    14
static uint8_t  hwBank = 0;             // bank scene aktif (kelipatan 4)
static int8_t   hwEncDelta = 0;         // akumulasi state encoder
static uint8_t  hwEncLast = 0;          // state quadrature terakhir
static uint32_t hwBtnLastAt[6] = {0};   // debounce per tombol (+SW)
static volatile uint8_t hwBtnState[4] = {0};   // 1=ditekan (state JSON)
static volatile int16_t hwEncCount = 0;        // total detent (state JSON)

// tabel transisi quadrature: index = (last<<2)|now -> delta
static const int8_t HW_ENC_TAB[16] = {
  0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0
};

void hwBankAdjust(int dir){
  // bank scene 0..N_SCENES-4 step 4; wrap-around (fraksional bila <4 scene)
  int nb = (int)hwBank + dir*4;
  int maxBank = ((int)N_SCENES/4 - 1)*4;
  if(maxBank < 0) maxBank = 0;
  if(nb < 0) nb = maxBank;
  if(nb > maxBank) nb = 0;
  hwBank = (uint8_t)nb;
  stateRevision++;
}

// Play scene fisik: paritas logika onSPlay (tanpa HTTP — langsung state).
void hwPlayScene(int s){
  if(s<0||s>=N_SCENES) return;
  bool playable=false;
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  for(int k=0;k<SCENE_STEPS;k++){
    uint8_t p=scenes[s][k];
    if(p>=1 && p<=N_PRESETS){ playable=true; break; }
  }
  xSemaphoreGive(dmxMutex);
  if(!playable){ sceneError=1; return; }
  chaseOn=false; chaseIdx=-1;
  sceneOn=true; sceneIdx=s; sceneStep=-1; sceneNextAt=millis(); sceneError=0;
  selectedScene=s;
  stateRevision++;
}

void hwInputBegin(){
  pinMode(HW_ENC_CLK, INPUT_PULLUP);
  pinMode(HW_ENC_DT,  INPUT_PULLUP);
  pinMode(HW_ENC_SW,  INPUT_PULLUP);
  pinMode(HW_BTN1, INPUT_PULLUP);
  pinMode(HW_BTN2, INPUT_PULLUP);
  pinMode(HW_BTN3, INPUT_PULLUP);
  pinMode(HW_BTN4, INPUT_PULLUP);
  hwEncLast = (uint8_t)((digitalRead(HW_ENC_CLK)<<1) | digitalRead(HW_ENC_DT));
}

void hwInputTask(){
  // --- encoder quadrature (EC11: 4 state per detent, /4 = 1 detent)
  uint8_t now = (uint8_t)((digitalRead(HW_ENC_CLK)<<1) | digitalRead(HW_ENC_DT));
  if(now != hwEncLast){
    int8_t d = HW_ENC_TAB[(hwEncLast<<2)|now];
    hwEncDelta += d;
    hwEncLast = now;
    if(hwEncDelta >= 4){ hwEncDelta -= 4; hwEncCount++; hwBankAdjust(1); }
    else if(hwEncDelta <= -4){ hwEncDelta += 4; hwEncCount--; hwBankAdjust(-1); }
  }
  // --- 4 tombol scene (debounce 30 ms, trigger saat baru ditekan)
  const uint8_t pins[4] = {HW_BTN1, HW_BTN2, HW_BTN3, HW_BTN4};
  uint32_t ms = millis();
  for(int i=0;i<4;i++){
    bool pressed = (digitalRead(pins[i]) == LOW);
    if(pressed){
      if(!hwBtnState[i] && ms - hwBtnLastAt[i] > 30){
        hwBtnState[i] = 1;
        hwPlayScene(hwBank + i);
      }
      hwBtnLastAt[i] = ms;               // refresh window selama ditahan
    } else {
      hwBtnState[i] = 0;
    }
  }
  // --- encoder switch = STOP playback (hold-guard 400 ms)
  if(digitalRead(HW_ENC_SW) == LOW){
    if(ms - hwBtnLastAt[4] > 400){
      hwBtnLastAt[4] = ms;
      sceneOn=false; sceneIdx=-1; sceneStep=-1; sceneNextAt=0;
      chaseOn=false; chaseIdx=-1;
      stateRevision++;
    }
  }
}

// ---------------------------------------------------------------


WebServer server(80);

// ---------------------------------------------------------------
// WEBSOCKET PUSH (port 81, path /ws)
// State dikirim ke browser saat berubah -> UI tidak lagi polling /cur
// tiap detik (hemat CPU Core 1 & alokasi String). Semua KONTROL tetap
// lewat HTTP REST port 80: tidak ada handler lama yang diubah.
// ---------------------------------------------------------------
AsyncWebServer wsSrv(81);
AsyncWebSocket ws("/ws");

// v43: KONTROL via WebSocket. Slider UI mengirim pesan kecil (fire-and-forget,
// tanpa overhead & antrean HTTP) sehingga fader terasa realtime. Format:
//   {"t":"s","k":"<fi>_<c>","v":n}   set satu channel fixture
//   {"t":"mast","v":n}               master dimmer
//   {"t":"strb","v":n}               strobe master
//   {"t":"all","v":0|1}              blackout / PAR penuh
static int wsInt(const char* s, const char* key, int dflt){
  String pat = String("\"") + key + "\":";
  const char* p = strstr(s, pat.c_str());
  return p ? atoi(p + pat.length()) : dflt;
}
static bool wsKeyStr(const char* s, String& out){
  const char* p = strstr(s, "\"k\":\"");
  if(!p) return false;
  p += 5;
  const char* e = strchr(p, '"');
  if(!e || e-p > 12) return false;
  out = "";
  for(const char* c=p; c<e; c++) out += *c;
  return true;
}
void wsHandleCtl(const uint8_t* data, size_t len){
  if(len==0 || len>95) return;
  char buf[96]; memcpy(buf, data, len); buf[len]=0;
  if(strstr(buf, "\"t\":\"s\"")){
    String k; if(!wsKeyStr(buf,k)) return;
    int v=wsInt(buf,"v",-1); if(v<0||v>255) return;
    int us=k.indexOf('_'); if(us<=0) return;
    int fi=k.substring(0,us).toInt(); int c=k.substring(us+1).toInt();
    if(fi<0||fi>=N_FIX||c<0||c>=fix[fi].foot) return;
    xSemaphoreTake(dmxMutex,portMAX_DELAY);
    uint16_t ch=fix[fi].start+c;
    // v48: snap binary utk channel custom mode-switch (relay)
    manualWant[ch]=snapSwitchMode(fix[fi].type,(uint16_t)c,v);
    manualTouched[ch]=millis();
    recomputeWant();
    out[ch]=want[ch];                      // snap: tanpa fade utk geseran manual
    xSemaphoreGive(dmxMutex);
    stateRevision++;
  } else if(strstr(buf, "\"t\":\"mast\"")){
    masterWant=cv(wsInt(buf,"v",0)); masterOut=masterWant;
    stateRevision++; nvsDirty=true;
  } else if(strstr(buf, "\"t\":\"strb\"")){
    strobeWant=cv(wsInt(buf,"v",0));
    stateRevision++;
  } else if(strstr(buf, "\"t\":\"all\"")){
    bool on = wsInt(buf,"v",0)==1;
    xSemaphoreTake(dmxMutex,portMAX_DELAY);
     uint32_t touched=millis();
     for(int f=0;f<N_FIX;f++){
       bool safe=(fix[f].type==FX_PAR);     // aman perangkat: hanya PAR yg dipenuhkan
       for(uint16_t c=0;c<fix[f].foot;c++){
         uint16_t ch=fix[f].start+c;
         manualWant[ch]=on ? (safe?255:0) : 0;
         manualTouched[ch]=touched;
       }
     }
     recomputeWant();
     for(int f=0;f<N_FIX;f++)
       for(uint16_t c=0;c<fix[f].foot;c++){
         uint16_t ch=fix[f].start+c;
         out[ch]=want[ch];
       }
    xSemaphoreGive(dmxMutex);
    stateRevision++; nvsDirty=true;
  } else if(strstr(buf, "\"t\":\"b\"")){
    // v48: BANK — tulis channel yang sama di SEMUA fixture tipe tsb.
    // Satu pesan WS menggantikan N request /set (latensi & heap lebih baik).
    int ty=wsInt(buf,"ty",-1), c=wsInt(buf,"c",-1), v=wsInt(buf,"v",-1);
    if(ty<0||c<0||v<0||v>255) return;
    xSemaphoreTake(dmxMutex,portMAX_DELAY);
    uint32_t touched=millis();
    for(int f=0;f<N_FIX;f++){
      if(fix[f].type!=(uint8_t)ty || c>=fix[f].foot) continue;
      uint16_t ch=fix[f].start+c;
      // snap binary utk channel custom mode-switch (relay)
      manualWant[ch]=snapSwitchMode(fix[f].type,(uint16_t)c,v);
      manualTouched[ch]=touched;
    }
    recomputeWant();
    for(int f=0;f<N_FIX;f++){
      if(fix[f].type!=(uint8_t)ty || c>=fix[f].foot) continue;
      uint16_t ch=fix[f].start+c;
      out[ch]=want[ch];                     // snap: bank terasa langsung
    }
    xSemaphoreGive(dmxMutex);
    stateRevision++;
  }
}
void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient* c, AwsEventType t, void* arg, uint8_t* data, size_t len){
  if(t==WS_EVT_CONNECT){ Serial.printf("WS: client %u tersambung\n",(unsigned)c->id()); return; }
  if(t==WS_EVT_DISCONNECT){ Serial.printf("WS: client %u putus\n",(unsigned)c->id()); return; }
  if(t==WS_EVT_DATA){
    AwsFrameInfo* info=(AwsFrameInfo*)arg;
    // hanya terima frame teks tunggal utuh berukuran kecil (pesan kontrol)
    if(info->final && info->index==0 && info->len==len && info->opcode==WS_TEXT){
      wsHandleCtl(data,len);
    }
  }
}

Preferences nvs;
const char* NVS_NS = "dmxrgb";
const uint8_t PRESET_VER = 6;   // versi format preset untuk file export JSON

// fade/hold milik preset (ms). Dipakai saat preset dimuat (fade) dan
// sebagai durasi tayang sebelum auto-run (chase/scene) melangkah.
static inline uint16_t presetFadeMs(int idx){ return (uint16_t)presets[idx][513]*10; }
static inline uint16_t presetHoldMs(int idx){ return (uint16_t)presets[idx][514]*20; }
static inline bool timeReached(uint32_t now, uint32_t deadline){
  return (int32_t)(now - deadline) >= 0;
}

uint8_t cv(int v){ return (uint8_t)constrain(v,0,255); }
static inline uint8_t mulScale(uint8_t a,uint8_t b){ return (uint8_t)((((uint16_t)a)*(uint16_t)b + 127)/255); }

// ---------------------------------------------------------------
// FADER BANK (ala konsol: 1 fader -> banyak fixture)
// Metode soft-patch: fader terikat ke (tipe fixture + offset channel
// lokal). Geser fader = tulis channel itu di SEMUA fixture anggota
// grup (snap realtime). Offset sesuaikan dgn chart DMX fixture Anda.
// ---------------------------------------------------------------
struct FaderGroup { const char* name; uint8_t typeFilter; uint8_t offset; };
#define N_GROUPS 8
FaderGroup grp[N_GROUPS] = {
  { "PAR Dim",   FX_PAR,    0 },
  { "PAR Red",   FX_PAR,    1 },
  { "PAR Green", FX_PAR,    2 },
  { "PAR Blue",  FX_PAR,    3 },
  { "MH Dim",    FX_MOVING, 5 },   // umumnya dimmer MH di ch6 (offset 5)
  { "Beam Dim",  FX_BEAM,   6 },
  { "Strobe",    FX_STROBE, 1 },
  { "Fog",       FX_FOG,    0 },
};

// ---------------------------------------------------------------
// PRESET & SCENE NVS (persistensi flash) — FORMAT KOMPAK sejak v28
// ---------------------------------------------------------------
// Akar masalah v27: blob utuh presets = 16 x 515 = 8.240 byte ditulis setiap
// simpan -> partisi NVS (~20 KB) penuh/terfragmentasi sehingga muncul
// ESP_ERR_NVS_NOT_ENOUGH_SPACE (phy_init 0x1105 di Serial saat boot) dan
// data kembali default setelah reboot.
// Solusi: simpan hanya channel yang benar-benar ter-patch + metadata.
// Layout per preset: [0]=used [1]=fade/10ms [2]=hold/20ms [3..]=CH1..CH_PATCH_END
#define PATCH_CH_TOTAL 176                 // channel tertinggi patch: FOG2=174 (+margin)
#define COMPACT_CHUNK (3+PATCH_CH_TOTAL)   // 179 byte/preset vs 515 byte format lama
// NVS membatasi satu value <= 4000 byte. 30 preset x 179 = 5370 byte ->
// preset dipecah ke 2 key (pc0/pc1), masing-masing 15 preset (2685 byte).
#define PRESETS_PER_KEY 15
#define COMPACT_KEY_BYTES (PRESETS_PER_KEY*COMPACT_CHUNK)
static uint8_t compactPresets[N_PRESETS][COMPACT_CHUNK];  // buffer staging NVS
static uint8_t compactScenes[sizeof(scenes)];             // buffer staging NVS
const uint8_t STORAGE_VER = 8;             // naikkan bila layout berubah lagi

// v46: TRANSAKSIONAL COMMIT. persistAll() lama menulis 6 key berurutan; kalau
// listrik mati di tengah, boot bisa memuat kombinasi data baru/lama tanpa ada
// yang menyadarinya. Skema commit-marker:
//   1. snapshot RAM -> compact* (di bawah mutex)
//   2. tulis sver2/pc0/pc1/sc/selP/selS
//   3. read-back semua blob -> harus identik (memcmp, lebih kuat dari CRC)
//   4. tulis "gen" = generation number naik (marker komit paling akhir)
// Boot memuat data HANYA bila gen valid; selain itu mulai bersih.
// Tidak dipakai double-buffer (2x5,4 KB blob tak muat di partisi
// NVS ~20 KB ini — persis masalah v27 NOT_ENOUGH_SPACE).
// ponytail: gen+read-back cukup untuk prototipe closed-network; upgrade ke
// A/B slot saat butuh survive tanpa kehilangan data apa pun pada tegangan
// jatuh di tengah tulis.
static uint32_t storageGen = 0;
// v46: migrasi data v45 (gen==0) ditunda keluar dari setup() — tulis NVS
// saat boot adalah momen arus paling kritis; pada PSU marginal hal ini
// memicu brownout loop (laporan user: "E BOD ... rst:0x3" berulang).
static bool pendingGenMigration = false;
static uint32_t bootAtMs = 0;

bool persistAll(){
  // Snapshot konsisten diambil di bawah mutex DMX; penulisan flash dilakukan
  // DI LUAR mutex agar timing frame Core 0 tidak terganggu operasi NVS.
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  for(int i=0;i<N_PRESETS;i++){
    compactPresets[i][0]=presets[i][0];
    compactPresets[i][1]=presets[i][513];
    compactPresets[i][2]=presets[i][514];
    memcpy(compactPresets[i]+3,presets[i]+1,PATCH_CH_TOTAL);
  }
  memcpy(compactScenes,(uint8_t*)scenes,sizeof(scenes));
  int selP=selectedPreset, selS=selectedScene;
  xSemaphoreGive(dmxMutex);
  if(!nvs.begin(NVS_NS,false)){
    lastSaveOk=false;
    return false;
  }
  bool ok=true;
  ok = nvs.putUChar("sver2",STORAGE_VER) > 0 && ok;
  ok = nvs.putBytes("pc0",(uint8_t*)compactPresets,COMPACT_KEY_BYTES) == COMPACT_KEY_BYTES && ok;
  ok = nvs.putBytes("pc1",(uint8_t*)compactPresets+COMPACT_KEY_BYTES,COMPACT_KEY_BYTES) == COMPACT_KEY_BYTES && ok;
  ok = nvs.putBytes("sc",compactScenes,sizeof(compactScenes)) == sizeof(compactScenes) && ok;
  ok = nvs.putInt("selP",selP) > 0 && ok;
  ok = nvs.putInt("selS",selS) > 0 && ok;
  // v46: read-back verify blob -> tangkap tulisan flash gagal senyap.
  static uint8_t rb[COMPACT_KEY_BYTES];
  if(ok){
    ok = nvs.getBytes("pc0",rb,COMPACT_KEY_BYTES)==COMPACT_KEY_BYTES
      && memcmp(rb,compactPresets,COMPACT_KEY_BYTES)==0;
    if(ok) ok = nvs.getBytes("pc1",rb,COMPACT_KEY_BYTES)==COMPACT_KEY_BYTES
      && memcmp(rb,(uint8_t*)compactPresets+COMPACT_KEY_BYTES,COMPACT_KEY_BYTES)==0;
    if(ok) ok = nvs.getBytes("sc",rb,sizeof(compactScenes))==sizeof(compactScenes)
      && memcmp(rb,compactScenes,sizeof(compactScenes))==0;
  }
  // Commit marker PALING AKHIR: gen hanya naik bila seluruh tulisan verified.
  // Boot memuat data hanya bila gen tersimpan valid -> snapshot yang terpotong
  // listrik di tengah tidak pernah dikomit.
  if(ok) ok = nvs.putUInt("gen",++storageGen) > 0;
  nvs.end();
  lastSaveOk=ok;
  if(ok){ nvsDirty=false; lastSaveAt=millis(); }
  else Serial.println("NVS: persist GAGAL verify -> commit tidak dinaikkan");
  return ok;
}

void unpackCompactIntoRam(){
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  memset(presets,0,sizeof(presets));
  for(int i=0;i<N_PRESETS;i++){
    presets[i][0]=compactPresets[i][0];
    presets[i][513]=compactPresets[i][1];
    presets[i][514]=compactPresets[i][2];
    memcpy(presets[i]+1,compactPresets[i]+3,PATCH_CH_TOTAL);
  }
  memcpy((uint8_t*)scenes,compactScenes,sizeof(scenes));
  xSemaphoreGive(dmxMutex);
}

void loadAll(){
  memset(presets,0,sizeof(presets));
  memset(scenes,0,sizeof(scenes));
  selectedPreset=-1; selectedScene=-1;
  if(!nvs.begin(NVS_NS,false)){
    Serial.println("NVS: gagal membuka namespace");
    return;
  }
  if(nvs.getUChar("sver2",0)!=STORAGE_VER){
    // Format lama (v27 ke bawah) atau NVS kosong: buang SEMUA key namespace
    // ini untuk merebut kembali ruang yang sudah habis/terfragmentasi,
    // lalu mulai bersih dengan default. Rekam ulang preset lalu Save Data.
    Serial.println("NVS: format lama terdeteksi -> clear() untuk reclaim ruang");
    nvs.clear();
    nvs.end();
    return;
  }
  bool ok = nvs.getBytes("pc0",(uint8_t*)compactPresets,COMPACT_KEY_BYTES) == COMPACT_KEY_BYTES
         && nvs.getBytes("pc1",(uint8_t*)compactPresets+COMPACT_KEY_BYTES,COMPACT_KEY_BYTES) == COMPACT_KEY_BYTES
         && nvs.getBytes("sc",compactScenes,sizeof(compactScenes)) == sizeof(compactScenes);
  uint32_t gen = nvs.getUInt("gen",0);
  if(ok && gen==0)
    Serial.println("NVS: data v45 tanpa gen -> muat sekali, komit migrasi ditunda 10 dtk");
  if(ok && gen>0) storageGen = gen;
  if(ok){
    unpackCompactIntoRam();
    selectedPreset=nvs.getInt("selP",-1);
    selectedScene=nvs.getInt("selS",-1);
    if(gen==0) pendingGenMigration = true;   // v46: tulis setelah daya stabil (loop)
  } else {
    Serial.println("NVS: blob rusak/tak lengkap -> mulai bersih (commit tidak valid)");
  }
  nvs.end();
}

// Muat ulang data tersimpan dari NVS ke RAM (tombol Load Data).
bool loadData(){
  if(!nvs.begin(NVS_NS,true)) return false;
  bool ok=(nvs.getUChar("sver2",0)==STORAGE_VER);
  if(ok){
    ok = nvs.getBytes("pc0",(uint8_t*)compactPresets,COMPACT_KEY_BYTES) == COMPACT_KEY_BYTES
      && nvs.getBytes("pc1",(uint8_t*)compactPresets+COMPACT_KEY_BYTES,COMPACT_KEY_BYTES) == COMPACT_KEY_BYTES
      && nvs.getBytes("sc",compactScenes,sizeof(compactScenes)) == sizeof(compactScenes);
  }
  if(ok && nvs.getUInt("gen",0)==0){
    // v46: tanpa commit marker = snapshot tak pernah terkomit valid -> tolak.
    nvs.end();
    return false;
  }
  if(ok){
    storageGen = nvs.getUInt("gen",0);
    unpackCompactIntoRam();
    selectedPreset=nvs.getInt("selP",-1);
    selectedScene=nvs.getInt("selS",-1);
    stateRevision++;
    sceneRev++;      // v46: isi scene bisa berbeda dari state RAM -> paksa reload client
  }
  nvs.end();
  if(ok) nvsDirty=false;
  return ok;
}

void markStateChanged(){
  stateRevision++;
  nvsDirty=true;
}

// ---------------------------------------------------------------
// v45: KONFIGURASI FIXTURE (Patch table runtime)
// ---------------------------------------------------------------
// Format NVS key "fixcfg" (binary kompak):
//   [0] = versi format (1)
//   [1] = jumlah fixture (N_FIX)
//   per fixture (31 byte): name[25] type[1] start[2] foot[2] hasMove[1]
// Total maks: 2 + 32*31 = 994 byte (muat satu NVS value, batas 4000).
#define FIXCFG_VER 1
#define FIXCFG_ENTRY 31
#define FIXCFG_MAX_BYTES (2 + MAX_FIX*FIXCFG_ENTRY)

// Validasi patch: alamat 1..512, foot >=1, tidak tumpang tindih.
// Return true bila valid. errIdx diisi index fixture pertama yang salah.
bool validateFixtures(const Fixture* fx, uint8_t count, int& errIdx, const char*& errCode){
  errIdx=-1; errCode=nullptr;
  if(count==0 || count>MAX_FIX){ errCode="count_out_of_range"; return false; }
  for(int i=0;i<count;i++){
    // v48: tipe custom 5..15 sah (customSlots harus used — dicek terpisah
    // di applyFixtures agar pesan error lebih jelas).
    if(fx[i].hasMove>1){ errIdx=i; errCode="move_invalid"; return false; }
    if(fx[i].name[0]==0){ errIdx=i; errCode="name_empty"; return false; }
    if(fx[i].start<1 || fx[i].foot<1 || (uint32_t)fx[i].start+fx[i].foot-1 > 512){
      errIdx=i; errCode="address_range"; return false;
    }
    for(int j=0;j<i;j++){
      uint16_t a1=fx[i].start, a2=fx[i].start+fx[i].foot-1;
      uint16_t b1=fx[j].start, b2=fx[j].start+fx[j].foot-1;
      if(a1<=b2 && b1<=a2){ errIdx=i; errCode="address_overlap"; return false; }
    }
  }
  return true;
}

// ---------------------------------------------------------------
// v48: PERSISTENSI CUSTOM TYPE (NVS)
// Layout key "ctcfg" (binary kompak, satu key):
//   [0]      = versi format (1)
//   per slot (18 + 32*10 = 338 byte):
//     [0]     used
//     [1..16] name[16]
//     [17]    channels
//     [18..49]  mode[32]
//     [50..]    labels[32][8] (tanpa NUL per item — panjang tetap 8, padded 0)
// Total: 1 + 11*338 = 3.719 byte (muat satu NVS value, batas ~4.000).
// ---------------------------------------------------------------
#define CTCFG_VER 1
#define CTCFG_SLOT (18 + CUSTOM_MAX_CH*10)     // 338 byte/slot
#define CTCFG_MAX_BYTES (1 + N_CUSTOM_TYPES*CTCFG_SLOT)
static_assert(CTCFG_MAX_BYTES <= 4000, "ctcfg blob melebihi batas NVS value");

bool persistCustomTypes(){
  uint8_t buf[CTCFG_MAX_BYTES];
  memset(buf,0,sizeof(buf));
  buf[0]=CTCFG_VER;
  for(int s=0;s<N_CUSTOM_TYPES;s++){
    uint8_t* e=buf+1+(size_t)s*CTCFG_SLOT;
    CustomType &c=customSlots[s];
    e[0]=c.used?1:0;
    strncpy((char*)e+1, c.name, 16); ((char*)e)[16]=0;
    e[17]=c.channels;
    memcpy(e+18, c.mode, CUSTOM_MAX_CH);
    for(int k=0;k<CUSTOM_MAX_CH;k++)
      memcpy(e+18+CUSTOM_MAX_CH+(size_t)k*8, c.labels[k], 8);
  }
  if(!nvs.begin(NVS_NS,false)){ lastSaveOk=false; return false; }
  bool ok = nvs.putBytes("ctcfg",buf,sizeof(buf))==sizeof(buf);
  nvs.end();
  if(!ok) lastSaveOk=false;
  return ok;
}

bool loadCustomTypes(){
  if(!nvs.begin(NVS_NS,true)) return false;
  size_t len=nvs.getBytesLength("ctcfg");
  if(len!=CTCFG_MAX_BYTES){ nvs.end(); return false; }   // belum ada -> default kosong
  uint8_t buf[CTCFG_MAX_BYTES];
  if(nvs.getBytes("ctcfg",buf,len)!=len){ nvs.end(); return false; }
  nvs.end();
  if(buf[0]!=CTCFG_VER) return false;
  for(int s=0;s<N_CUSTOM_TYPES;s++){
    uint8_t* e=buf+1+(size_t)s*CTCFG_SLOT;
    CustomType &c=customSlots[s];
    memset(&c,0,sizeof(CustomType));
    c.used=e[0];
    strncpy(c.name,(char*)e+1,16); c.name[16]=0;
    c.channels=e[17];
    if(c.channels>CUSTOM_MAX_CH) c.channels=CUSTOM_MAX_CH;
    memcpy(c.mode, e+18, CUSTOM_MAX_CH);
    for(int k=0;k<CUSTOM_MAX_CH;k++)
      memcpy(c.labels[k], e+18+CUSTOM_MAX_CH+(size_t)k*8, 8);
  }
  return true;
}

// Simpan konfigurasi fixture ke NVS.
bool persistFixtures(){
  uint8_t buf[FIXCFG_MAX_BYTES];
  buf[0]=FIXCFG_VER; buf[1]=N_FIX;
  for(int i=0;i<N_FIX;i++){
    uint8_t* e=buf+2+i*FIXCFG_ENTRY;
    memset(e,0,FIXCFG_ENTRY);
    strncpy((char*)e, fix[i].name, 24); e[24]=0;
    e[25]=fix[i].type;
    e[26]=(uint8_t)(fix[i].start & 0xFF); e[27]=(uint8_t)(fix[i].start>>8);
    e[28]=(uint8_t)(fix[i].foot  & 0xFF); e[29]=(uint8_t)(fix[i].foot>>8);
    e[30]=fix[i].hasMove;
  }
  if(!nvs.begin(NVS_NS,false)){ lastSaveOk=false; return false; }
  size_t len=2+(size_t)N_FIX*FIXCFG_ENTRY;
  bool ok = nvs.putBytes("fixcfg",buf,len)==len;
  nvs.end();
  if(!ok) lastSaveOk=false;
  return ok;
}

// Muat konfigurasi fixture dari NVS. Return false bila belum ada / rusak
// (pemanggil harus memanggil loadDefaultFixtures() sebagai fallback).
bool loadFixtures(){
  if(!nvs.begin(NVS_NS,true)) return false;
  size_t len=nvs.getBytesLength("fixcfg");
  if(len<2 || len>FIXCFG_MAX_BYTES){ nvs.end(); return false; }
  uint8_t buf[FIXCFG_MAX_BYTES];
  if(nvs.getBytes("fixcfg",buf,len)!=len){ nvs.end(); return false; }
  nvs.end();
  if(buf[0]!=FIXCFG_VER) return false;
  uint8_t count=buf[1];
  if(count==0 || count>MAX_FIX || len < 2+(size_t)count*FIXCFG_ENTRY) return false;
  int errIdx; const char* errCode;
  Fixture tmp[MAX_FIX];
  for(int i=0;i<count;i++){
    uint8_t* e=buf+2+i*FIXCFG_ENTRY;
    memset(&tmp[i],0,sizeof(Fixture));
    strncpy(tmp[i].name,(char*)e,24); tmp[i].name[24]=0;
    tmp[i].type=e[25];
    tmp[i].start=(uint16_t)e[26] | ((uint16_t)e[27]<<8);
    tmp[i].foot =(uint16_t)e[28] | ((uint16_t)e[29]<<8);
    tmp[i].hasMove=e[30];
  }
  if(!validateFixtures(tmp,count,errIdx,errCode)) return false;
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  N_FIX=count;
  for(int i=0;i<count;i++) fix[i]=tmp[i];
  for(int i=count;i<MAX_FIX;i++){ fix[i].name[0]=0; fix[i].type=0; fix[i].start=0; fix[i].foot=0; fix[i].hasMove=0; }
  xSemaphoreGive(dmxMutex);
  return true;
}

// Terapkan konfigurasi fixture baru (dari HTTP/serial). Validasi dulu.
// Return true bila sukses; errIdx/errCode diisi bila gagal.
bool applyFixtures(const Fixture* newFix, uint8_t newCount, int& errIdx, const char*& errCode){
  if(!validateFixtures(newFix,newCount,errIdx,errCode)) return false;
  // v48: tipe custom harus slot-nya used
  for(int i=0;i<newCount;i++){
    if(newFix[i].type>=CUSTOM_TYPE_MIN && newFix[i].type<=CUSTOM_TYPE_MAX){
      if(!customSlots[CUSTOM_IDX(newFix[i].type)].used){
        errIdx=i; errCode="custom_type_not_defined"; return false;
      }
    } else if(newFix[i].type>FX_FOG){
      errIdx=i; errCode="type_invalid"; return false;
    }
  }
  Fixture oldFix[MAX_FIX]; uint8_t oldCount;
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  oldCount=N_FIX; memcpy(oldFix,fix,sizeof(fix));
  N_FIX=newCount;
  for(int i=0;i<newCount;i++) fix[i]=newFix[i];
  for(int i=newCount;i<MAX_FIX;i++){ fix[i].name[0]=0; fix[i].type=0; fix[i].start=0; fix[i].foot=0; fix[i].hasMove=0; }
  for(int i=0;i<MAX_FIX;i++) blackoutEnd[i]=0;
  recomputeWant();
  xSemaphoreGive(dmxMutex);
  if(!persistFixtures()){
    xSemaphoreTake(dmxMutex,portMAX_DELAY);
    N_FIX=oldCount; memcpy(fix,oldFix,sizeof(fix));
    recomputeWant();
    xSemaphoreGive(dmxMutex);
    errIdx=-1; errCode="nvs_write_failed";
    return false;
  }
  markStateChanged();
  return true;
}

// ---------------------------------------------------------------
// OUTPUT (dipanggil task DMX di Core 0; uga dari handler utk respons segera)
// ---------------------------------------------------------------
void buildFrame(){
  uint8_t frame[513];
  uint8_t m;
  uint32_t now = millis();
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  {
    memset(frame,0,sizeof(frame));
    m = masterOut;
    for(int f=0; f<N_FIX; f++){
      uint16_t base = fix[f].start;
      // Blackout-on-move: saat pan/tilt bergerak jauh, matikan dimmer sementara
      // agar tidak "menggambar" lintasan kotor selama gerak (LTP).
      bool bo = (now < blackoutEnd[f]);
      frame[base] = bo ? 0 : mulScale(out[base], m);
      for(uint16_t c=1; c<fix[f].foot; c++)
        frame[base+c] = out[base+c];                  // channel lain salin mentah
    }
  }
  xSemaphoreGive(dmxMutex);
  // STROBE MASTER: gate kotak seluruh frame (lampu mati-nyala global).
  // Timing di sini (40fps) cukup presisi utk half-period >=40ms.
  uint8_t sv = strobeWant;
  if(sv>0){
    uint32_t half = 40 + (uint32_t)(255 - sv) * 1960 / 255;
    if(strobeNextAt==0 || (int32_t)(now - strobeNextAt) >= 0){
      strobePhase = (strobeNextAt==0) ? true : !strobePhase;  // aktif kembali mulai ON
      strobeNextAt = now + half;
    }
    if(!strobePhase) memset(frame,0,sizeof(frame));
  } else {
    strobeNextAt = 0;   // arm ulang: saat dinyalakan lagi mulai dari fase ON
  }
  DMX.writeBytes(frame+1,512,1);
  DMX.transmit();
}

// ---------------------------------------------------------------
// FADE (interpolasi out -> want; dipanggil per tick DMX)
// ---------------------------------------------------------------
void fadeTick(float dtSec){
  bool snap = fadeMs<=20;
  float step = snap?1.0f:min(1.0f,(dtSec*1000.0f)/(float)fadeMs);
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  {
    int dm = (int)masterWant - (int)masterOut;
    if(abs(dm)<2) masterOut = masterWant;
    else masterOut = (uint8_t)((int)masterOut + (int)(dm*(snap?1.0f:step)));
    for(int f=0;f<N_FIX;f++)
      for(uint16_t c=0;c<fix[f].foot;c++){
        uint16_t ch=fix[f].start+c;
        int diff=(int)want[ch]-(int)out[ch];
        if(abs(diff)<2) out[ch]=want[ch];
        else out[ch]=(uint8_t)((int)out[ch]+(int)((float)diff*(snap?1.0f:step)));
      }
  }
  xSemaphoreGive(dmxMutex);
}

// ---------------------------------------------------------------
// CHASE
// ---------------------------------------------------------------
void applyPresetToWant(int idx);   // forward decl (disebut dari chaseTick)
int nextUsedPreset(int from){
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  int r=-1;
  for(int s=1;s<=N_PRESETS;s++){ int i=(from+s)%N_PRESETS; if(presets[i][0]){ r=i; break; } }
  xSemaphoreGive(dmxMutex);
  return r;
}
void chaseTick(uint32_t now){
  if(!chaseOn) return;
  if(!timeReached(now,chaseNextAt)) return;
  int n=nextUsedPreset(chaseIdx);
  if(n<0){ chaseOn=false; return; }
  chaseIdx=n;
  applyPresetToWant(n);
  chaseNextAt=now+chaseMs;
}

// Scene playback: maju ke langkah non-kosong berikutnya (wrap), terapkan
// presetnya lewat applyPresetToWant (fade + blackout-on-move otomatis jalan).
void sceneTick(uint32_t now){
  if(!sceneOn || sceneIdx<0 || sceneIdx>=N_SCENES) return;
  if(!timeReached(now,sceneNextAt)) return;
  int chosen=-1; uint8_t pnum=0;
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  for(int s=1;s<=SCENE_STEPS;s++){
    int k=(sceneStep+s)%SCENE_STEPS;
    // used TIDAK dipersyaratkan: preset "terhapus"(disembunyikan) tetap dimainkan
    uint8_t p=scenes[sceneIdx][k];
    if(p>=1 && p<=N_PRESETS){ chosen=k; pnum=p; break; }
  }
  xSemaphoreGive(dmxMutex);
  if(chosen<0){ sceneOn=false; sceneError=1; return; }
  sceneStep=chosen;
  applyPresetToWant(pnum-1);               // mutex diambil di dalamnya
  sceneNextAt=now+sceneMs;
}

// ---------------------------------------------------------------
// PRESET OPERATION
// ---------------------------------------------------------------
void applyPresetToWant(int idx){
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  uint32_t now = millis();
  // fade & hold milik preset ini menjadi aktif (dipakai fadeTick & auto-run)
  fadeMs  = presetFadeMs(idx);
  chaseMs = presetHoldMs(idx);
  sceneMs = chaseMs;
  for(int f=0; f<N_FIX; f++){
    // Deteksi gerakan besar pada pan/tilt (ch0 & ch2) -> aktifkan blackout sementara.
    if(fix[f].hasMove && fix[f].foot>=3){
      int d0 = abs((int)presets[idx][fix[f].start+0] - (int)want[fix[f].start+0]);
      int d2 = abs((int)presets[idx][fix[f].start+2] - (int)want[fix[f].start+2]);
      if(d0>30 || d2>30) blackoutEnd[f] = now + 350;   // 350ms dimmer mati saat bergerak
    }
    for(uint16_t c=0; c<fix[f].foot; c++){
      uint16_t ch=fix[f].start+c;
      pbWant[ch]=presets[idx][ch];
      pbTouched[ch]=now;
    }
  }
  recomputeWant();
  xSemaphoreGive(dmxMutex);
}
// v46: COPY-ON-WRITE preset — scene menyimpan REFERENSI nomor preset, bukan
// salinan. Tanpa proteksi ini, merekam ulang preset (warna/fade/hold baru)
// menular ke semua scene yang merujuk slot itu. Solusi: sebelum data lama
// ditimpa, pindahkan chunk lama ke slot BAYANGAN (used=0) lalu alihkan
// referensi scene ke slot bayangan. Scene tetap memainkan data lama, slot
// asli bebas direkam ulang. sceneTick sudah mengabaikan flag used sehingga
// slot bayangan tetap diputar. Return: index bayangan, atau COW_FULL bila
// tidak ada slot bebas.
#define COW_FULL -2
int presetSceneRefCount(int idx){
  // caller wajib pegang dmxMutex
  int n=0;
  for(int s=0;s<N_SCENES;s++)
    for(int k=0;k<SCENE_STEPS;k++)
      if(scenes[s][k]==idx+1) n++;
  return n;
}
int cowShadowPreset(int idx){
  // caller wajib pegang dmxMutex. Cari slot bebas (used=0 dan TIDAK
  // direferensikan scene mana pun — jangan pakai slot bayangan lain).
  for(int sh=0; sh<N_PRESETS; sh++){
    if(sh==idx) continue;
    if(presets[sh][0]) continue;                 // slot dipakai preset visible
    if(presetSceneRefCount(sh)>0) continue;      // slot = bayangan scene lain
    memcpy(presets[sh],presets[idx],PRESET_CHUNK);
    presets[sh][0]=0;                            // bayangan: hidden by design
    for(int s=0;s<N_SCENES;s++)
      for(int k=0;k<SCENE_STEPS;k++)
        if(scenes[s][k]==idx+1) scenes[s][k]=sh+1;
    sceneRev++;
    return sh;
  }
  return COW_FULL;
}
// return true bila rekam sukses; false bila COW gagal (slot bayangan habis,
// data lama tidak tersentuh sehingga scene tetap utuh).
bool capturePreset(int idx, bool ignoreDimmer, uint16_t fMs, uint16_t hMs){
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  // COW: bila slot lama masih dirujuk scene (visible ATAU bayangan), salin
  // dulu ke slot bayangan. Syarat used=0 TIDAK dipakai: slot bayangan yang
  // masih dirujuk scene wajib terlindungi juga (data scene milik scene).
  if(presetSceneRefCount(idx)>0){
    if(cowShadowPreset(idx)==COW_FULL){
      xSemaphoreGive(dmxMutex);
      return false;  // gagal: data lama TIDAK ditimpa (scene tetap utuh)
    }
  }
  memset(presets[idx],0,PRESET_CHUNK);
  presets[idx][0]=1;
  presets[idx][513]=(uint8_t)constrain(fMs/10,0,255);
  presets[idx][514]=(uint8_t)constrain(hMs/20,5,250);
  for(int f=0; f<N_FIX; f++)
    for(uint16_t c=0; c<fix[f].foot; c++){
      uint16_t ch=fix[f].start+c;
      presets[idx][ch] = (c==0 && ignoreDimmer) ? 255 : out[ch];
  }
  xSemaphoreGive(dmxMutex);
  markStateChanged();
  persistAll();   // tulis NVS hanya di sini (awet flash)
  return true;
}

String presetsJson(){
  String j="[";
  uint8_t row[PRESET_CHUNK];   // snapshot per baris -> bebas tearing saat import/clear
  for(int i=0;i<N_PRESETS;i++){
    xSemaphoreTake(dmxMutex,portMAX_DELAY);
    memcpy(row,presets[i],PRESET_CHUNK);
    xSemaphoreGive(dmxMutex);
    j+="{";
    j+="\"n\":"+String(i+1)+",\"used\":"+String(row[0]?"true":"false");
    // Pratinjau warna: ambil dari fixture PAR pertama (posisi dinamis, bukan
    // hardcode row[2..4]). Jika tidak ada PAR atau footprint <4, warna = 0.
    // Ini memastikan pratinjau tetap benar saat alamat fixture diubah (v45).
    int r=0, g=0, b=0;
    for(int pf=0; pf<N_FIX; pf++){
      if(fix[pf].type==FX_PAR && fix[pf].foot>=4){
        uint16_t base=fix[pf].start;
        if(base+3 <= 512){
          r=row[base+1]; g=row[base+2]; b=row[base+3];
        }
        break;
      }
    }
    j+=",\"r\":"+String(r)+",\"g\":"+String(g)+",\"b\":"+String(b);
    j+=",\"f\":"+String((int)row[513]*10)+",\"h\":"+String((int)row[514]*20);
    j+="}";
    if(i<N_PRESETS-1) j+=",";
  }
  j+="]";
  return j;
}

// ---------------------------------------------------------------
// EXPORT / IMPORT preset JSON (portabel antar device)
// Format: {"app":"DMX-RGB","ver":6,"presets":[{"u":0/1,"f":ms,"h":ms,"c":[512 nilai]},...]}
// Import aman: parse ke buffer sementara; preset lama hanya ditimpa bila
// file berhasil diparse (tidak ada lagi "hapus semua karena file rusak").
// ---------------------------------------------------------------
String exportJson(){
  String j="{";
  j.reserve(42000);            // alokasi sekali -> hindari realloc berulang
  j+="\"app\":\"DMX-RGB\",\"ver\":"+String(PRESET_VER)+",\"presets\":[";
  uint8_t row[PRESET_CHUNK];
  for(int i=0;i<N_PRESETS;i++){
    xSemaphoreTake(dmxMutex,portMAX_DELAY);
    memcpy(row,presets[i],PRESET_CHUNK);
    xSemaphoreGive(dmxMutex);
    j+="{\"u\":"+String(row[0]?1:0);
    j+=",\"f\":"+String((int)row[513]*10)+",\"h\":"+String((int)row[514]*20);
    // Channel SELALU diekspor, termasuk preset tersembunyi (used=0),
    // supaya scene yang merujuknya tetap utuh setelah import di device lain.
    j+=",\"c\":[";
    for(int k=0;k<512;k++){ j+=String(row[k+1]); if(k<511) j+=','; }
    j+="]";
    j+="}";
    if(i<N_PRESETS-1) j+=",";
  }
  j+="]}";
  return j;
}

bool importJson(const String& s){
  static uint8_t tmp[N_PRESETS][PRESET_CHUNK];   // buffer parse (8KB, di BSS)
  static bool parsed[N_PRESETS];
  memset(tmp,0,sizeof(tmp));
  memset(parsed,0,sizeof(parsed));
  int pi = s.indexOf("\"presets\":[");
  if(pi<0) return false;
  int pos = pi+11;
  int found = 0;
  for(int i=0;i<N_PRESETS;i++){
    int up = s.indexOf("\"u\":", pos);
    if(up<0) break;
    int objEnd = s.indexOf('}', up);             // akhir objek preset ini
    if(objEnd<0) break;
    // flag used (toleransi spasi setelah ':')
    int qp = up+4;
    while(qp<(int)s.length() && s.charAt(qp)==' ') qp++;
    tmp[i][0] = (s.charAt(qp)=='1')?1:0;
    // fade & hold (ms); default 600/1500 bila tidak ada di file
    tmp[i][513]=60; tmp[i][514]=75;
    int fp = s.indexOf("\"f\":", up);
    if(fp>=0 && fp<objEnd){
      int a=fp+4, b2=a;
      while(b2<(int)s.length() && s.charAt(b2)!=',') b2++;
      int fv=0; for(int p=a;p<b2;p++){ char ch=s.charAt(p); if(ch>='0'&&ch<='9') fv=fv*10+(ch-'0'); }
      tmp[i][513]=(uint8_t)constrain(fv/10,0,255);
    }
    int hp = s.indexOf("\"h\":", up);
    if(hp>=0 && hp<objEnd){
      int a=hp+4, b2=a;
      while(b2<(int)s.length() && s.charAt(b2)!=',') b2++;
      int hv=0; for(int p=a;p<b2;p++){ char ch=s.charAt(p); if(ch>='0'&&ch<='9') hv=hv*10+(ch-'0'); }
      tmp[i][514]=(uint8_t)constrain(hv/20,5,250);
    }
    // channel hanya dicari DI DALAM objek ini (cegah salah tangkap milik preset lain)
    int cp = s.indexOf("\"c\":[", up);
    if(cp>=0 && cp<objEnd){
      int q = cp+5;
      uint8_t* dst = tmp[i]+1;
      for(int k=0;k<512;k++){
        while(q<(int)s.length() && (s.charAt(q)==','||s.charAt(q)==' '||s.charAt(q)=='\n'||s.charAt(q)=='\r')) q++;
        if(q<(int)s.length() && s.charAt(q)==']') break;      // array ditutup lebih awal
        int start=q;
        while(q<(int)s.length() && s.charAt(q)!=',' && s.charAt(q)!=']') q++;
        int v=0;                                // parse digit manual (tanpa alloc String)
        for(int p=start;p<q;p++){ char ch=s.charAt(p); if(ch>='0'&&ch<='9') v=v*10+(ch-'0'); }
        dst[k]=(uint8_t)constrain(v,0,255);
        if(q<(int)s.length() && s.charAt(q)!=',') break;       // ']'
        q++;
      }
    }
    parsed[i]=true;
    found++;
    pos = objEnd+1;                              // lanjut ke objek preset berikutnya
  }
  if(found==0) return false;                     // tidak ada preset valid -> gagal, data lama utuh
  // Commit hanya setelah parsing selesai. Index file tetap dipertahankan.
  // Parser kompatibel dengan export desktop: 30 slot, field c/f/h/u.
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  bool shadowFailed=false;
  for(int i=0;i<N_PRESETS;i++){
    if(!parsed[i]) continue;
    // v46 COW: file import menimpa slot yang direferensikan scene -> salin
    // data lama ke slot bayangan supaya scene tetap memainkan data lamanya.
    // Syarat used=0 tidak dipakai: slot bayangan dirujuk scene juga wajib
    // terlindungi. Catatan: file export menyimpan SEMUA slot (incl.
    // tersembunyi), jadi scene di device tujuan biasanya sudah tercakup;
    // proteksi ini untuk file yang datanya berbeda dari scene lokal.
    if(presetSceneRefCount(i)>0){
      if(cowShadowPreset(i)==COW_FULL) shadowFailed=true;
    }
    memcpy(presets[i],tmp[i],PRESET_CHUNK);
  }
  xSemaphoreGive(dmxMutex);
  if(shadowFailed) sceneRev++;   // ada perubahan referensi (parsial)
  markStateChanged();
  persistAll();
  return true;
}

// ---------------------------------------------------------------
// SCENE handlers
// ---------------------------------------------------------------
String scnJson(){
  // v47: reserve — 20 scene x 50 langkah x maks 2 digit + koma ≈ 1,3 KB.
  // Tanpa reserve, ~1000 konkatenasi memicu puluhan realloc heap.
  String j="[";
  j.reserve(1400);
  for(int s=0;s<N_SCENES;s++){
    j+="[";
    for(int k=0;k<SCENE_STEPS;k++){ j+=String(scenes[s][k]); if(k<SCENE_STEPS-1) j+=','; }
    j+="]";
    if(s<N_SCENES-1) j+=",";
  }
  j+="]";
  return j;
}
void onScenesGet(){ server.send(200,"application/json",scnJson()); }

void sendApiError(int code, const char* reason, const char* message){
  String j="{\"ok\":false,\"code\":\""+String(reason)+"\",\"message\":\""+String(message)+"\"}";
  server.send(code,"application/json",j);
}
void sendApiOk(){
  String j="{\"ok\":true,\"revision\":"+String(stateRevision.load())+"}";
  server.send(200,"application/json",j);
}

// ---------------------------------------------------------------
// v43: KONFIGURASI WIFI KUSTOM (Web UI & desktop)
// GET  /wifistat          -> status koneksi saat ini (JSON)
// POST /wifiset?ssid&pass -> simpan kredensial ke NVS + reconnect non-blocking
// Reconnect dicoba bertahap di loop() (tidak memblok DMX); bila 6x gagal,
// perangkat kembali ke kredensial bawaan lalu AP darurat supaya operator
// TIDAK PERNAH terkunci di luar perangkat.
// ---------------------------------------------------------------
void onWifiStat(){
  bool sta=(WiFi.getMode() & WIFI_STA) && WiFi.status()==WL_CONNECTED;
  String j="{\"ok\":true";
  j+=",\"connected\":"; j+= sta?"true":"false";
  j+=",\"ssid\":\"";    j+= sta?WiFi.SSID():String(effSsid()); j+="\"";
  j+=",\"custom\":";    j+= customSsid.length()>0?"true":"false";
  j+=",\"pending\":";   j+= wifiPending?"true":"false";
  j+=",\"ip\":\"";      j+= activeIP().toString(); j+="\"";
  j+=",\"rssi\":";      j+= String(sta?WiFi.RSSI():0);
  j+=",\"apActive\":";  j+= (WiFi.getMode() & WIFI_AP)?"true":"false";
  j+="}";
  server.send(200,"application/json",j);
}
void onWifiSet(){
  String ssid=server.arg("ssid"); ssid.trim();
  String pass=server.arg("pass");
  if(ssid.length()==0 || ssid.length()>32){ sendApiError(400,"wifi_no_ssid","SSID kosong atau terlalu panjang"); return; }
  if(pass.length()>63){ sendApiError(400,"wifi_pass_long","Password terlalu panjang"); return; }
  if(!wifiNvs.begin("dmxwifi",false)){
    sendApiError(500,"wifi_nvs_begin_failed","Gagal membuka namespace WiFi");
    return;
  }
  bool ok = wifiNvs.putString("ssid",ssid)>0 && wifiNvs.putString("pass",pass)>0;
  wifiNvs.end();
  if(!ok){ sendApiError(500,"wifi_nvs_fail","Gagal menyimpan kredensial ke flash"); return; }
  customSsid=ssid; customPass=pass;
  wifiPending=true; wifiTryCount=0;
  // GRACE 800 ms sebelum tick pertama: biar respons HTTP/serial ini sempat
  // terkirim dulu; pemutusan koneksi lama dilakukan wifiReconnectTick().
  wifiTryAt=millis()+800;
  sendApiOk();
}
void wifiReconnectTick(){
  if(!wifiPending) return;
  if((WiFi.getMode() & WIFI_STA) && WiFi.status()==WL_CONNECTED){
    wifiPending=false;
    Serial.print("WiFi kustom tersambung: "); Serial.print(customSsid);
    Serial.print(" IP: http://"); Serial.println(WiFi.localIP());
    stateRevision++;                       // UI ikut refresh status
    return;
  }
  if(millis()-wifiTryAt < 2000) return;    // jeda antar percobaan
  wifiTryAt=millis(); wifiTryCount++;
  if(wifiTryCount>6){
    wifiPending=false;
    Serial.println("WiFi kustom GAGAL setelah 6 percobaan -> kembali ke kredensial bawaan + AP darurat");
    // URUTAN PENTING: aktifkan AP dulu (bila tanpa Ethernet) agar operator
    // tidak pernah terkunci, BARU coba kredensial bawaan sebagai STA.
    if(!ETH.linkUp()){ WiFi.mode(WIFI_AP_STA); WiFi.softAP(AP_SSID, AP_PASS); }
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    return;
  }
  Serial.printf("WiFi kustom: percobaan %d/6 -> %s\n", wifiTryCount, customSsid.c_str());
  if(wifiTryCount==1) WiFi.disconnect();   // lepas koneksi lama (grace sudah lewat)
  // Pertahankan AP bila sedang aktif (operator konfigurasi lewat AP darurat
  // tidak kehilangan akses selama percobaan berlangsung).
  WiFi.mode((WiFi.getMode() & WIFI_AP) ? WIFI_AP_STA : WIFI_STA);
  WiFi.begin(customSsid.c_str(), customPass.c_str());
}

void onSelect(){
  if(server.hasArg("p")){
    int p=server.arg("p").toInt()-1;
    if(p<0||p>=N_PRESETS||!presets[p][0]){ sendApiError(404,"preset_missing","Preset belum tersedia"); return; }
    selectedPreset=p;
  }
  if(server.hasArg("s")){
    int s=server.arg("s").toInt()-1;
    if(s<0||s>=N_SCENES){ sendApiError(400,"scene_invalid","Nomor scene tidak valid"); return; }
    selectedScene=s;
  }
  stateRevision++;
  nvsDirty=true;
  sendApiOk();
}

void onSaveData(){
  // persistAll mengambil snapshot di bawah mutex lalu menulis NVS di luar mutex.
  bool ok=persistAll();
  if(!ok){ sendApiError(500,"nvs_write_failed","Gagal menulis data ke NVS"); return; }
  sendApiOk();
}
void onLoadData(){
  if(!loadData()){ sendApiError(404,"no_saved_data","Tidak ada data tersimpan yang valid"); return; }
  sendApiOk();
}

void onHealth(){
  String j="{";
  j+="\"ok\":true,\"build\":\""+String(BUILD_TAG)+"\",";
  j+="\"uptime\":"+String(millis())+",";
  j+="\"heartbeat\":"+String(dmxHeartbeat)+",";
  j+="\"revision\":"+String(stateRevision.load())+",";
  j+="\"selectedPreset\":"+String(selectedPreset)+",\"selectedScene\":"+String(selectedScene)+",";
  j+="\"sceneError\":"+String(sceneError)+",";
  j+="\"nvsDirty\":"+(String(nvsDirty?"true":"false"))+",";
  j+="\"lastSaveOk\":"+(String(lastSaveOk?"true":"false"))+",";
  j+="\"wifi\":"+(String(WiFi.status()==WL_CONNECTED?"true":"false"));
  j+="}";
  server.send(200,"application/json",j);
}

// GET /spush?s=S&p=P -> tambahkan preset P sebagai langkah berikutnya
void onSPush(){
  int s=server.arg("s").toInt()-1, p=server.arg("p").toInt();
  if(s<0||s>=N_SCENES||p<1||p>N_PRESETS){ sendApiError(400,"scene_input_invalid","Data scene tidak valid"); return; }
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  int slot=-1, lastFilled=-1;
  for(int k=0;k<SCENE_STEPS;k++){
    if(scenes[s][k]!=0) lastFilled=k;
    else if(slot<0) slot=k;
  }
  // Tolak duplikat berurutan: langkah sama dgn sebelumnya = redundan
  // (hold per-preset, nilai identik tidak menghasilkan transisi apa pun).
  if(lastFilled>=0 && scenes[s][lastFilled]==(uint8_t)p){
    xSemaphoreGive(dmxMutex);
    sendApiError(409,"scene_duplicate","Preset sama dengan langkah terakhir");
    return;
  }
  if(slot>=0) scenes[s][slot]=(uint8_t)p;
  sceneRev++;
  xSemaphoreGive(dmxMutex);
  if(slot<0){ sendApiError(409,"scene_full","Scene sudah penuh"); return; }
  stateRevision++; nvsDirty=true;
  persistAll();
  sendApiOk();
}
// GET /spop?s=S -> hapus langkah terakhir yang terisi
void onSPop(){
  int s=server.arg("s").toInt()-1;
  if(s<0||s>=N_SCENES){ sendApiError(400,"scene_invalid","Nomor scene tidak valid"); return; }
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  for(int k=SCENE_STEPS-1;k>=0;k--){ if(scenes[s][k]!=0){ scenes[s][k]=0; sceneRev++; break; } }
  xSemaphoreGive(dmxMutex);
  stateRevision++; nvsDirty=true; persistAll();
  sendApiOk();
}
// GET /sclear?s=S -> kosongkan seluruh scene
void onSClear(){
  int s=server.arg("s").toInt()-1;
  if(s<0||s>=N_SCENES){ sendApiError(400,"scene_invalid","Nomor scene tidak valid"); return; }
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  memset(scenes[s],0,SCENE_STEPS);
  sceneRev++;
  xSemaphoreGive(dmxMutex);
  stateRevision++; nvsDirty=true; persistAll();
  sendApiOk();
}
// GET /splay?s=S -> mainkan scene (hentikan chase); /splay?off=1 -> stop
void onSPlay(){
  if(server.hasArg("off")){
    sceneOn=false; sceneIdx=-1; sceneStep=-1;
    sceneNextAt=0; sceneError=0; sendApiOk(); return;
  }
  int s=server.arg("s").toInt()-1;
  if(s<0||s>=N_SCENES){ sendApiError(400,"scene_invalid","Nomor scene tidak valid"); return; }
  // Validasi: scene harus punya >=1 langkah yang menunjuk preset yang ADA.
  // Kalau tidak, jawab jelas (jangan 'on' lalu berhenti diam-diam).
  bool playable=false;
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  for(int k=0;k<SCENE_STEPS;k++){
    // nomor preset valid = playable (data channel tetap ada walau disembunyikan)
    uint8_t p=scenes[s][k];
    if(p>=1 && p<=N_PRESETS){ playable=true; break; }
  }
  xSemaphoreGive(dmxMutex);
  if(!playable){
    sceneError=1;
    sendApiError(409,"scene_no_valid_presets","Scene tidak memiliki preset yang tersedia");
    return;
  }
  chaseOn=false; chaseIdx=-1;              // hanya satu sistem auto aktif
  sceneOn=true; sceneIdx=s; sceneStep=-1; sceneNextAt=millis(); sceneError=0;
  selectedScene=s;
  sendApiOk();
}

// ---------------------------------------------------------------
// FADER BANK handler
// GET /grp?i=X&v=N -> tulis channel offset grup X di semua fixture
// anggotanya (snap realtime, LTP tetap berlaku utk slider per-channel).
// ---------------------------------------------------------------
void onGroup(){
  int i=server.arg("i").toInt();
  int v=server.arg("v").toInt();
  if(i<0||i>=N_GROUPS){ sendApiError(400,"group_invalid","Nomor grup tidak valid"); return; }
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
   uint32_t touched=millis();
    for(int f=0;f<N_FIX;f++){
      if(fix[f].type!=grp[i].typeFilter) continue;
      if(grp[i].offset>=fix[f].foot) continue;
      uint16_t ch=fix[f].start+grp[i].offset;
      // v48: snap binary per-fixture utk channel custom mode-switch
      manualWant[ch]=snapSwitchMode(fix[f].type,grp[i].offset,v);
      manualTouched[ch]=touched;
    }
   recomputeWant();
   for(int f=0;f<N_FIX;f++){
     if(fix[f].type!=grp[i].typeFilter) continue;
     if(grp[i].offset>=fix[f].foot) continue;
     out[fix[f].start+grp[i].offset]=want[fix[f].start+grp[i].offset];
   }
  xSemaphoreGive(dmxMutex);
  stateRevision++; nvsDirty=true;
  sendApiOk();
}

String grpJson(){
  // v47: reserve — 8 grup x ±60 byte kecil, tapi bebas realloc
  String j="[";
  j.reserve(512);
  for(int i=0;i<N_GROUPS;i++){
    j+="{\"name\":\""+String(grp[i].name)+"\",";
    j+="\"type\":"+String(grp[i].typeFilter)+",";
    j+="\"offset\":"+String(grp[i].offset)+"}";
    if(i<N_GROUPS-1) j+=",";
  }
  j+="]";
  return j;
}
// fixJson() didefinisikan setelah blok HTML UI (~baris 1413); deklarasi
// eksplisit supaya handler ini tidak bergantung pada auto-prototype .ino.
String fixJson();

void onGroupsGet(){ server.send(200,"application/json",grpJson()); }

// ---------------------------------------------------------------
// v48: CUSTOM TYPE API — GET /ctypes (daftar), POST /ctypes (commit penuh)
// POST body: {"types":[{"slot":5,"used":1,"name":"RELAY","channels":8,
//   "mode":[0,1,1,0,0,0,0,0],"labels":["PWR","RST1",...]}]}
// Slot di luar daftar tetap dipertahankan (patch-by-slot).
// ---------------------------------------------------------------
String customTypesJson(){
  String j="[";
  j.reserve(64+N_CUSTOM_TYPES*64);
  bool first=true;
  for(int s=0;s<N_CUSTOM_TYPES;s++){
    CustomType &c=customSlots[s];
    if(!c.used) continue;
    if(!first) j+=",";
    first=false;
    j+="{\"slot\":"+String(CUSTOM_TYPE_MIN+s);
    j+=",\"name\":\""+String(c.name)+"\"";
    j+=",\"channels\":"+String(c.channels)+",\"mode\":[";
    for(int k=0;k<c.channels;k++){ j+=String(c.mode[k]); if(k<c.channels-1) j+=','; }
    j+="],\"labels\":[";
    for(int k=0;k<c.channels;k++){ j+="\""+String(c.labels[k])+"\""; if(k<c.channels-1) j+=','; }
    j+="]}";
  }
  j+="]";
  return j;
}

void onCtypesGet(){ server.send(200,"application/json",customTypesJson()); }

// Commit custom types dari JSON body. Return true sukses (sudah persist).
// body = elemen "types":[...] dari POST /ctypes ATAU arg CTSET serial.
bool commitCustomTypes(const String& body){
  // Parse manual sederhana: cari tiap {"slot":N,...} lalu field di dalamnya.
  // Batas: 11 slot; field opsional → slot tak disebut tetap utuh.
  CustomType tmp[N_CUSTOM_TYPES];
  memcpy(tmp,customSlots,sizeof(tmp));        // mulai dari state aktif
  int pos=body.indexOf('[');
  if(pos<0) return false;
  int parsed=0;
  while(true){
    int objStart=body.indexOf('{',pos);
    if(objStart<0) break;
    int objEnd=body.indexOf('}',objStart);
    if(objEnd<0) break;
    String obj=body.substring(objStart,objEnd+1);
    pos=objEnd+1;
    int slot=obj.indexOf("\"slot\":");
    if(slot<0) continue;
    int slotN=obj.substring(slot+7).toInt();
    if(slotN<CUSTOM_TYPE_MIN||slotN>CUSTOM_TYPE_MAX) continue;
    CustomType &t=tmp[CUSTOM_IDX((uint8_t)slotN)];
    memset(&t,0,sizeof(CustomType));
    t.used=1;
    // name
    int nm=obj.indexOf("\"name\":\"");
    if(nm>=0){ int ns=nm+8, ne=obj.indexOf('"',ns); if(ne>ns){ String s=obj.substring(ns,min(ne,ns+16)); s.toCharArray(t.name,17);} }
    if(t.name[0]==0) strncpy(t.name,"CUSTOM",16);
    // channels
    int chp=obj.indexOf("\"channels\":");
    int channels=chp>=0?obj.substring(chp+11).toInt():0;
    if(channels<1||channels>CUSTOM_MAX_CH) channels=CUSTOM_MAX_CH;
    t.channels=(uint8_t)channels;
    // mode array
    int mp=obj.indexOf("\"mode\":[");
    if(mp>=0){
      int ep=obj.indexOf(']',mp);
      String arr=obj.substring(mp+8, ep);
      int idx=0, from=0;
      while(idx<CUSTOM_MAX_CH){
        int comma=arr.indexOf(',',from);
        String tok=(comma<0)?arr.substring(from):arr.substring(from,comma);
        tok.trim();
        t.mode[idx]= (uint8_t)(tok.toInt()?1:0);
        idx++;
        if(comma<0) break;
        from=comma+1;
      }
    }
    // labels array
    int lp=obj.indexOf("\"labels\":[");
    if(lp>=0){
      int ep=obj.indexOf(']',lp);
      String arr=obj.substring(lp+10, ep);
      int idx=0, from=0;
      while(idx<CUSTOM_MAX_CH){
        int q1=arr.indexOf('"',from);
        if(q1<0) break;
        int q2=arr.indexOf('"',q1+1);
        if(q2<0) break;
        String s=arr.substring(q1+1,q2);
        s.toCharArray(t.labels[idx],9);
        idx++;
        from=q2+1;
      }
      // label kosong -> default CHn
      for(int k=0;k<idx;k++) if(t.labels[k][0]==0){ String d="CH"+String(k+1); d.toCharArray(t.labels[k],9); }
      for(int k=idx;k<channels;k++){ String d="CH"+String(k+1); d.toCharArray(t.labels[k],9); }
    } else {
      for(int k=0;k<channels;k++){ String d="CH"+String(k+1); d.toCharArray(t.labels[k],9); }
    }
    parsed++;
  }
  if(parsed==0) return false;
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  memcpy(customSlots,tmp,sizeof(tmp));
  xSemaphoreGive(dmxMutex);
  if(!persistCustomTypes()) return false;
  markStateChanged();
  return true;
}

void onCtypesPost(){
  if(!server.hasArg("plain")){ sendApiError(400,"no_body","Body JSON kosong"); return; }
  String body=server.arg("plain");
  if(body.length()>4200){ sendApiError(413,"body_too_large","Body terlalu besar"); return; }
  if(!commitCustomTypes(body)){ sendApiError(400,"ctypes_invalid","Format custom type tidak valid"); return; }
  sendApiOk();
}
void onFixesGet(){ server.send(200,"application/json",fixJson()); }

// ---------------------------------------------------------------
// HTML UI
// ---------------------------------------------------------------
const char INDEX_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="id"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<title>DMX Console</title>
<style>
  :root{--bg:#15171a;--panel:#1e2227;--edge:#2a2f36;--txt:#e8eaee;--muted:#9aa3ad;--accent:#ffb400;--ok:#2ecc71;--bad:#e74c3c}
  *{box-sizing:border-box}
  body{margin:0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;background:var(--bg);color:var(--txt);-webkit-font-smoothing:antialiased;display:flex;min-height:100dvh}
  .wrap{width:100%;max-width:1440px;margin:0 auto;padding:clamp(12px,2vw,28px);display:grid;grid-template-columns:minmax(0,1fr) minmax(280px,420px);gap:14px;align-items:start}
  header{grid-column:1 / -1;grid-row:1}
  header{display:flex;justify-content:space-between;align-items:baseline;padding:0 2px 14px}
  h1{font-size:clamp(18px,4vw,24px);margin:0;font-weight:650}h1 .dot{color:var(--accent)}
  .meta{font-size:12px;color:var(--muted);text-align:right;line-height:1.4}.meta span{display:block}
  .dotlive{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px}
  .live .dotlive{background:var(--ok);box-shadow:0 0 0 3px rgba(46,204,113,.15)}.down .dotlive{background:var(--bad)}
  .panel{background:var(--panel);border:1px solid var(--edge);border-radius:14px;padding:18px;margin-bottom:14px}
  .panel h3{margin:0 0 4px;font-size:15px;font-weight:600}.sub{color:var(--muted);font-size:12.5px;margin:0 0 12px}
  label{display:grid;grid-template-columns:auto 1fr 40px;align-items:center;gap:10px;padding:5px 0}
  .lab{font-size:13px;font-weight:600;min-width:56px;text-align:right}
  .val{font-size:13px;font-variant-numeric:tabular-nums;text-align:right;color:var(--muted)}
  input[type=range]{-webkit-appearance:none;appearance:none;width:100%;height:30px;background:transparent;cursor:pointer}
  input[type=range]:focus-visible{outline:2px solid var(--accent);outline-offset:2px;border-radius:6px}
  input[type=range]::-webkit-slider-runnable-track{height:10px;border-radius:6px;background:linear-gradient(to right,var(--fill,#666) 0%,var(--fill,#666) var(--p,50%),#333940 var(--p,50%),#333940 100%)}
  input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:22px;height:30px;margin-top:-10px;border-radius:7px;background:#f5f7fa;border:2px solid #aab}
  input[type=range]::-moz-range-track{height:10px;border-radius:6px;background:#333940}
  input[type=range]::-moz-range-progress{height:10px;border-radius:6px;background:var(--fill,#666)}
  input[type=range]::-moz-range-thumb{width:22px;height:30px;border-radius:7px;background:#f5f7fa;border:2px solid #aab}
  .actions{display:flex;gap:10px;flex-wrap:wrap}
  button{min-height:48px;border:0;border-radius:10px;font-size:14px;font-weight:600;cursor:pointer;color:#13161a;-webkit-tap-highlight-color:transparent;padding:0 12px}
  button:focus-visible{outline:2px solid #fff;outline-offset:2px}button.act{flex:1}
  .btn-off{background:var(--bad);color:#fff}.btn-reset{background:#3a414b;color:#e8eaee}
  .btn-go{background:var(--accent)}.btn-go.on{background:#fff;color:#13161a}
  .status{font-size:13px;color:var(--muted);padding-top:10px}
  .bankhead{display:flex;justify-content:space-between;align-items:center;margin-bottom:10px;flex-wrap:wrap;gap:8px}
  .bankhead h3{margin:0}.ctrlgroup{display:flex;gap:8px;align-items:center;flex-wrap:wrap}
  .editmode{min-height:40px;padding:0 14px;background:#3a414b;color:#e8eaee;font-size:13px;font-weight:700;border-radius:8px}
  .editmode.on{background:var(--bad);color:#fff}
  #btnModeEdit.on{background:var(--bad);color:#fff}
  #btnModeShow.on{background:var(--ok);color:#06200f}
  button[disabled]{opacity:.4;cursor:not-allowed}
  .idimtag{font-size:12px;color:var(--muted)}.idimtag.on{color:var(--accent);font-weight:700}
  .bank{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}
  .pad{position:relative;min-height:64px;border-radius:10px;border:1px solid var(--edge);background:#262b33;display:flex;flex-direction:column;justify-content:center;align-items:center;cursor:pointer;overflow:hidden;transition:border-color .12s,background .12s;-webkit-tap-highlight-color:transparent}
  .pad:focus-visible{outline:2px solid var(--accent);outline-offset:2px}.pad .num{font-size:11px;color:var(--muted);position:absolute;top:5px;left:8px}.pad .csw{width:30px;height:22px;border-radius:5px;border:1px solid rgba(255,255,255,.14);display:block}
  .pad.empty .csw{background:repeating-linear-gradient(45deg,#2c313a,#2c313a 4px,#262b33 4px,#262b33 8px)}
  .pad.hidden-scene{border:2px dashed #e67e22;background:#1e2126}
  .pad.hidden-scene .num{color:#e67e22;font-weight:700}
  .pad.hidden-scene .csw{background:repeating-linear-gradient(45deg,#3a2a1a,#3a2a1a 4px,#1e2126 4px,#1e2126 8px)}
  .pad.sel{background:var(--accent);border-color:#fff;color:#13161a;box-shadow:0 0 0 2px rgba(255,180,0,.35)}
  .pad.sel .num{color:#13161a;font-weight:800}
  .pad.sel .csw{border-color:rgba(0,0,0,.4)}
  .pad.playing{background:var(--ok);border-color:#fff;color:#06200f;box-shadow:0 0 0 2px rgba(46,204,113,.4)}
  .pad.playing .num{color:#06200f;font-weight:800}
  .bank.deleting .pad.used{border-color:var(--bad)}
  .state-line{font-size:13px;color:var(--muted);margin:8px 0 10px;min-height:18px}
  .state-line strong{color:var(--txt)}
  .toast{position:fixed;left:50%;bottom:18px;transform:translateX(-50%);max-width:92vw;
         background:#2e7d32;color:#fff;padding:10px 16px;border-radius:8px;font-size:13px;
         font-weight:600;opacity:0;pointer-events:none;transition:opacity .25s;z-index:9;text-align:center}
  .toast.show{opacity:.97}
  .steps{display:grid;grid-template-columns:repeat(10,1fr);gap:4px;margin-top:8px}
  .step{min-height:26px;border-radius:5px;background:#262b33;border:1px solid var(--edge);
        color:var(--muted);font-size:12px;display:flex;align-items:center;justify-content:center;
        font-variant-numeric:tabular-nums}
  .step.fill{color:var(--accent);border-color:var(--accent)}
  #masterPanel{grid-column:2;grid-row:2}
  #scenePanel{grid-column:1;grid-row:2}
  #presetPanel{grid-column:1;grid-row:3}
  #channelPanel{grid-column:1 / -1;grid-row:4}
  /* v47: section per tipe fixture — collapsible, group fader embedded */
  .type-sec{border:1px solid var(--edge);border-radius:8px;margin:10px 0;padding:10px;background:#20242c}
  .type-sec>summary{cursor:pointer;font-size:13px;font-weight:700;color:#b0bec5;user-select:none;list-style:none}
  .type-sec>summary::-webkit-details-marker{display:none}
  .type-sec>summary::before{content:'\25BE ';color:var(--muted)}
  .type-sec:not([open])>summary::before{content:'\25B8 '}
  .type-sec .grouphdr{font-size:11px;color:var(--muted);font-weight:700;margin:6px 0 2px;letter-spacing:.4px}
  .fixgrd{display:grid;grid-template-columns:repeat(auto-fill,minmax(300px,1fr));gap:12px}
  /* v48: dual pane — kiri fixture individu, kanan fader bank */
  .dualfx{display:grid;grid-template-columns:minmax(0,1fr) 280px;gap:14px;align-items:start}
  .dualfx>[data-role="bank"]{border-left:1px dashed var(--edge);padding-left:14px}
  @media(max-width:900px){.dualfx{grid-template-columns:1fr}.dualfx>[data-role="bank"]{border-left:0;padding-left:0;border-top:1px dashed var(--edge);padding-top:10px}}
  /* v48: channel mode SWITCH — thumb persegi + fill beda, ⚡ di label */
  input.switchmode{accent-color:#ffd54f}
  input.switchmode::-webkit-slider-thumb{border-radius:3px;background:#ffd54f}
  input.switchmode::-moz-range-thumb{border-radius:3px;background:#ffd54f}
  .fix{padding-top:10px;border-top:1px dashed var(--edge)}.fix:first-child{border-top:0;padding-top:0}
  .fix-name{font-size:12px;font-weight:700;color:var(--muted);letter-spacing:.3px;margin-bottom:2px}
  .io{display:flex;gap:8px;flex-wrap:wrap}
  .io button{flex:1;min-height:40px;background:#3a414b;color:#e8eaee}
  /* v45: Patch panel — tabel fixture editable */
  #patchTable table{width:100%;border-collapse:collapse;font-size:13px;min-width:560px}
  #patchTable th{text-align:left;color:var(--muted);font-weight:600;padding:4px 6px;border-bottom:1px solid var(--edge);font-size:12px}
  #patchTable td{padding:4px 6px;border-bottom:1px solid #232733}
  #patchTable input[type=number]{width:70px;background:#14161b;color:#dfe3ea;border:1px solid #3a3f4b;border-radius:4px;padding:4px 6px;font-size:13px}
  #patchTable input[type=text]{width:110px;background:#14161b;color:#dfe3ea;border:1px solid #3a3f4b;border-radius:4px;padding:4px 6px;font-size:13px}
  #patchTable select{background:#14161b;color:#dfe3ea;border:1px solid #3a3f4b;border-radius:4px;padding:4px 6px;font-size:13px}
  #patchTable .del{background:#7f1d1d;color:#fff;border:0;border-radius:5px;padding:3px 8px;cursor:pointer;font-size:12px;min-height:28px}
  #patchTable .err{border-color:#e74c3c !important;background:#2a1215}
  #patchPanel{grid-column:1 / -1;grid-row:5}
  @media(max-width:760px){
    .wrap{display:flex;flex-direction:column;max-width:680px}
    header{order:0}
    #masterPanel{order:1}
    #scenePanel{order:2}
    #presetPanel{order:3}
    #channelPanel{order:4}
    #patchPanel{order:5}
    .panel{margin-bottom:14px}
  }
  @media(max-width:380px){.panel{padding:14px}.bank{grid-template-columns:repeat(4,1fr)}label{grid-template-columns:auto 1fr 36px}}
</style></head><body>
<div class="wrap">
  <header><h1>DMX<span class="dot">.</span>Console</h1><div class="meta"><span id="buildtag">__BUILD__</span><span>__IP__</span></div></header>

  <section class="panel" id="masterPanel">
    <h3>Master <span style="color:var(--muted);font-weight:400">global</span></h3>
    <label><span class="lab">Master</span><input type="range" id="master" min="0" max="255" value="255"><span class="val" id="masterv">255</span></label>
    <label><span class="lab">Strobe</span><input type="range" id="mstrb" min="0" max="255" value="0"><span class="val" id="mstrbv">0</span></label>
    <div class="actions"><button class="btn-off act" id="btnBlack">Blackout</button><button class="act" id="btnArtnet">Art-Net: LOCAL</button><button class="btn-go act" id="btnChase">Chase OFF</button><button class="btn-reset act" id="btnSaveData">Save Data</button><button class="btn-off act" id="btnLoadData">Load Data</button></div>
    <div class="status" id="saveStatus">Data tersimpan</div>
  </section>

  <section class="panel" id="wifiPanel">
    <h3>WiFi <span style="color:var(--muted);font-weight:400">status &amp; kredensial kustom</span></h3>
    <div class="status" id="wifiStat">memuat status...</div>
    <label><span class="lab">SSID</span><input type="text" id="wssid" maxlength="32" placeholder="nama WiFi tujuan" style="background:#14161b;color:#dfe3ea;border:1px solid #3a3f4b;border-radius:4px;padding:6px;grid-column:2/4"></label>
    <label><span class="lab">Sandi</span><input type="password" id="wpass" maxlength="63" placeholder="password WiFi" style="background:#14161b;color:#dfe3ea;border:1px solid #3a3f4b;border-radius:4px;padding:6px;grid-column:2/4"></label>
    <div class="actions"><button class="btn-go act" id="btnWifiSet">Sambungkan &amp; Simpan</button></div>
    <p class="sub">Kredensial tersimpan di flash &amp; dipakai tiap boot. Gagal 6x percobaan = otomatis kembali ke bawaan + AP darurat.</p>
  </section>

  <section class="panel" id="patchPanel">
    <h3>Patch <span style="color:var(--muted);font-weight:400">alamat DMX &amp; jumlah fixture (maks 512 ch)</span></h3>
    <p class="sub">Ubah alamat/jumlah fixture lalu Simpan Patch. Alamat akhir tiap fixture tidak boleh melebihi 512 dan tidak boleh tumpang tindih. Tipe custom (slot 5-15) dibuat dulu di &quot;Tipe Custom&quot;.</p>
    <div id="patchTable" style="overflow-x:auto;margin:8px 0"></div>
    <div class="actions">
      <button class="btn-go act" id="btnPatchSave">Simpan Patch</button>
      <button class="btn-off act" id="btnPatchAdd">+ Tambah Fixture</button>
      <button class="btn-reset act" id="btnPatchReset">Reset Default</button>
      <button class="act" id="btnCType">Tipe Custom</button>
    </div>
    <div class="status" id="patchStatus" style="margin-top:8px"></div>
    <!-- v48: dialog editor custom fixture type (dibangun JS; hidden default) -->
    <div id="ctypeEditor" style="display:none;margin-top:12px;border:1px solid var(--edge);border-radius:8px;padding:10px;background:#20242c">
      <div style="display:flex;gap:8px;align-items:center;flex-wrap:wrap">
        <strong style="color:#b0bec5">Tipe Custom (slot 5-15)</strong>
        <select id="ctSlot" style="width:auto"></select>
        <input type="text" id="ctName" placeholder="Nama tipe" maxlength="16" style="width:140px">
        <input type="number" id="ctChannels" min="1" max="32" value="8" style="width:70px">
        <span style="color:var(--muted);font-size:12px">channel</span>
        <button class="act" id="ctLoad" style="margin-left:auto">Muat Slot</button>
        <button class="btn-go act" id="ctSave">Simpan Tipe</button>
        <button class="btn-off act" id="ctClose">Tutup</button>
      </div>
      <p class="sub" style="margin:6px 0 2px">Mode: <b>FADER</b> = 0-255 kontinu &middot; <b>SWITCH</b> = hanya 0 / 255 (relay &amp; beban on-off). Radio di bawah menentukan mode tiap channel.</p>
      <div id="ctChannels" style="margin-top:6px;display:grid;grid-template-columns:repeat(auto-fill,minmax(190px,1fr));gap:6px"></div>
      <div class="status" id="ctStatus" style="margin-top:8px"></div>
    </div>
  </section>

  <section class="panel" id="presetPanel">
    <h3>Preset <span style="color:var(--muted);font-weight:400">fade &amp; hold per preset</span></h3>
    <div class="bankhead"><div class="ctrlgroup">
      <button class="editmode" id="btnEdit">REKAM OFF</button>
      <button class="editmode" id="btnDel">HAPUS OFF</button>
      <span class="idimtag" id="idimTag">tanpa dimmer</span><input type="checkbox" id="idim" style="width:auto">
    </div><div class="io">
      <button id="btnExport">Ekspor</button>
      <button id="btnImport">Impor</button>
      <input type="file" id="fileIn" accept=".json,application/json" style="display:none">
    </div></div>
    <div class="bank" id="bank"></div>
    <p class="state-line" id="pinfo"></p>
    <label><span class="lab">Fade</span><input type="range" id="pfade" min="0" max="2000" step="50" value="600"><span class="val" id="pfadev">0.6s</span></label>
    <label><span class="lab">Hold</span><input type="range" id="phold" min="100" max="5000" step="100" value="1500"><span class="val" id="pholdv">1.5s</span></label>
    <p class="sub" id="pfhint">fade &amp; hold milik preset terpilih; dipakai juga saat REKAM</p>
  </section>

  <section class="panel" id="scenePanel">
    <h3>Scene <span style="color:var(--muted);font-weight:400">rantai preset &middot; auto-play</span></h3>
    <div class="bankhead"><div class="ctrlgroup">
      <button class="editmode" id="btnModeEdit">EDIT MODE</button>
      <button class="btn-go" id="btnModeShow">SHOW MODE</button>
      <button class="editmode" id="btnSPop" disabled>&#8630; Akhir</button>
      <button class="editmode" id="btnSClear" disabled>KOSONGKAN</button>
      <button class="btn-go" id="btnSPlay" title="Cek scene terpilih">&#9654; Cek</button>
    </div></div>
    <p class="state-line" id="sinfo">pilih scene (S1-S20), lalu EDIT utk merangkai preset</p>
    <p class="state-line" id="hwDeck" style="color:#b0bec5">DECK FISIK: pasang 4 tombol (GPIO 32/33/27/14) + encoder (25/26, SW 13) — status muncul di sini</p>
    <div class="bank" id="sbank"></div>
    <div class="steps" id="steps"></div>
    <p class="sub">durasi tiap langkah = Hold preset masing-masing</p>
  </section>

  <section class="panel" id="channelPanel">
    <h3>Mixer <span style="color:var(--muted);font-weight:400">section per tipe &middot; klik judul utk lipat</span></h3>
    <p class="sub">geser = realtime &middot; dimmer channel pertama mengikuti master</p>
    <div id="fixes"></div>
    <div class="actions" style="margin-top:12px">
      <button class="btn-off act" id="btnOff">Mati Semua</button>
      <button class="btn-reset act" id="btnWhite">Semua Warna Penuh</button>
    </div>
    <div class="status" id="status"><span class="dotlive"></span><span id="statTxt">menghubungkan...</span></div>
  </section>
</div>
<div class="toast" id="toast"></div>
<script>
const $=id=>document.getElementById(id);
// Self-diagnostik: error JS ditampilkan langsung di layar (bukan diam-diam).
// v44: banner merah menonjol di atas layar + unhandledrejection, supaya
// kegagalan (mis. token substitusi tak terganti spt __SCNDATA__) langsung
// terlihat saat perform tanpa harus membuka console browser. Handler ini
// dipasang SEBELUM semua const data agar tetap aktif walau script mati di tengah.
(function(){
  function showBanner(msg){
    try{
      var b=document.getElementById('errBanner');
      if(!b){
        b=document.createElement('div'); b.id='errBanner';
        b.style.cssText='position:fixed;top:0;left:0;right:0;z-index:9999;background:#b71c1c;color:#fff;font:12px/1.4 monospace;padding:8px 12px;border-bottom:2px solid #ff5252;';
        (document.body||document.documentElement).appendChild(b);
      }
      b.textContent='JS ERROR: '+msg;
    }catch(_){}
    var el=document.getElementById('statTxt');
    if(el){ el.textContent='JS ERROR: '+msg; el.style.color='#e74c3c'; }
  }
  window.addEventListener('error',function(e){
    showBanner(e.message+' (baris '+(e.lineno||'?')+')');
  });
  window.addEventListener('unhandledrejection',function(e){
    var r=e.reason, m=(r&&r.message)?r.message:String(r||'unknown');
    showBanner('Promise: '+m);
  });
})();
// WebServer Arduino melayani koneksi satu per satu. Serialkan semua fetch
// agar polling, slider, preset, dan scene tidak saling berebut koneksi.
const nativeFetch=window.fetch.bind(window);
const httpQueue=[];
let httpBusy=false;
function pumpHttp(){
  if(httpBusy||httpQueue.length===0) return;
  const req=httpQueue.shift();
  httpBusy=true;
  const options=Object.assign({cache:'no-store'},req.options||{});
  nativeFetch(req.url,options)
    .then(req.resolve)
    .catch(req.reject)
    .finally(()=>{ httpBusy=false; pumpHttp(); });
}
window.fetch=function(url,options){
  return new Promise((resolve,reject)=>{
    if(httpQueue.length>=32){ reject(new Error('request queue full')); return; }
    httpQueue.push({url:url,options:options,resolve:resolve,reject:reject});
    pumpHttp();
  });
};
function api(url,options){
  return fetch(url,options).then(async response=>{
    const text=await response.text();
    let data={};
    try{ data=text?JSON.parse(text):{}; }catch(_){ data={message:text}; }
    if(!response.ok){
      const error=new Error(data.message||data.code||text||('HTTP '+response.status));
      error.code=data.code||''; error.status=response.status; throw error;
    }
    return data;
  });
}
const FIX=__FIXDATA__;
const GRP=__GRPDATA__;
const SCN=__SCNDATA__;
const CT=__CTDATA__;   // v48: custom fixture types [{slot,name,channels,mode[],labels[]}]
const NSCN=SCN.length, NSTEPS=SCN[0].length;
const N_PRESETS=__NP__;   // jumlah pad preset (diinjeksi server; selaras firmware)
const N=FIX.length, COLS=['#ffb400','#e0463f','#3fae57','#3f7fd4','#9b59b6','#e67e22','#1abc9c','#95a5a6'];
const sliders={}; let allKeys=[];
function keyOf(fi,c){return fi+'_'+c;}
function paintFill(el){const pct=(el.value-el.min)/(el.max-el.min)*100;el.style.setProperty('--p',pct+'%');const col=COLS[(+el.dataset.fi)%COLS.length];el.style.setProperty('--fill',col||'#ffb400');}
function labelOf(i){
  const t=FIX[i].type, f=FIX[i].foot;
  if(t===0){
    const base=['Dim','R','G','B'];
    const extra=['Strobe','Mode','Auto','Speed','Aux','R2','G2','B2'];
    return base.concat(extra.slice(0,Math.max(0,f-4)));
  }
  if(t===1){   // v47: chart standar moving head
    const a=['Pan','PanF','Tilt','TiltF','P/T Spd','Dim','Strobe','ColorSpd',
             'Gobo','GoboRot','PrismRot','Focus','Zoom','Shutter','Func','Reset','CH19','CH20'];
    return fitLabels(a,f);
  }
  if(t===2){   // v47: chart standar beam
    const a=['Pan','PanF','Tilt','TiltF','P/T Spd','Dim','Strobe','Color',
             'Gobo','GoboRot','Prism','Focus','Zoom','Shutter','Func','Reset'];
    return fitLabels(a,f);
  }
  if(t===4) return ['Fog','Fan'];
  if(t===3) return ['Mode','Strobe','Dim','Color'];
  const a=[]; for(let k=1;k<=f;k++)a.push('CH'+k); return a;
}
function fitLabels(a,f){   // v47: potong bila foot<chart; sisa = CHn
  const out=a.slice(0,f);
  for(let k=out.length;k<f;k++) out.push('CH'+(k+1));
  return out;
}
// ---- v48: custom type helpers --------------------------------------------
function ctFind(t){ return CT.find(c=>c.slot===t)||null; }
function isCustom(t){ return t>=5 && t<=15; }
// label channel fixture i: chart bawaan ATAU custom type definition
function channelLabelsFor(i){
  const t=FIX[i].type, f=FIX[i].foot;
  if(isCustom(t)){
    const c=ctFind(t);
    if(c){
      const out=[];
      for(let k=0;k<f;k++){
        if(k<c.labels.length && c.labels[k]) out.push(c.labels[k]);
        else out.push('CH'+(k+1));
      }
      return {labels:out, modes:(k)=> (k<c.mode.length?c.mode[k]:0)};
    }
    return {labels:fitLabels([],f), modes:()=>0};
  }
  return {labels:labelOf(i), modes:()=>0};
}
// v47: bangun section per tipe — tiap section berisi fader GRUP tipe itu
// + fader per fixture tipenya. Menggantikan buildFixes()+buildGroups() lama
// (panel Fader Bank terpisah dihapus).
const TYPE_NAMES={0:'PAR LED',1:'MOVING HEAD',2:'BEAM',3:'STROBE',4:'FOG'};
function buildSections(){
  const box=$('fixes'); box.innerHTML='';
  allKeys=[];   // dibangun ulang (slider per-channel)
  // kelompokkan fixture per tipe, urutan kemunculan dipertahankan
  const byType=[];
  for(let i=0;i<N;i++){
    const t=FIX[i].type;
    let g=byType.find(x=>x.t===t);
    if(!g){ g={t,items:[]}; byType.push(g); }
    g.items.push(i);
  }
  byType.forEach(g=>{
    const sec=document.createElement('details');
    sec.className='type-sec';
    sec.open = g.t===0;   // PAR default terbuka; tipe lain dilipat
    const sum=document.createElement('summary');
    const first=FIX[g.items[0]], last=FIX[g.items[g.items.length-1]];
    sum.textContent=(TYPE_NAMES[g.t]||('TIPE '+g.t))+' \u00b7 '+g.items.length+
                    ' unit \u00b7 DMX '+first.start+'-'+(last.start+last.foot-1);
    sec.appendChild(sum);
    // (1) v48: fader GRUP lama DIHAPUS dari section — fungsinya tergantikan
    // oleh BANK (pane kanan): bank menulis channel sama ke semua fixture
    // tipe ini, mencakup semua channel (GRUP hanya subset 8 channel tetap).
    // Duplikasi kontrol = membingungkan operator (laporan user v48).
    // Endpoint /grp + syncGroups tetap ada utk kompatibilitas desktop lama.
    // (2) fader per-fixture tipe ini — DUAL PANE (v48):
    //     kiri = fixture individu (klik nama utk pilih), kanan = BANK fader
    //     yang menulis channel sama ke SEMUA fixture tipe itu.
    const dual=document.createElement('div');
    dual.className='dualfx';
    // --- pane kiri: fixture individu
    const grd=document.createElement('div'); grd.className='fixgrd';
    grd.dataset.role='individual';
    // --- pane kanan: fader bank
    const bankPane=document.createElement('div');
    bankPane.className='fix';
    bankPane.dataset.role='bank';
    bankPane.style.display='none';   // muncul setelah fixture dipilih
    const bankTitle=document.createElement('div'); bankTitle.className='fix-name';
    bankPane.appendChild(bankTitle);
    dual.appendChild(grd);
    dual.appendChild(bankPane);
    sec.appendChild(dual);
    // helper render bank utk tipe ini (dipanggil saat fixture dipilih)
    const renderBank=(selIdx)=>{
      const f=FIX[selIdx];
      const cl=channelLabelsFor(selIdx);
      const def=ctFind(g.t);
      const typeName=TYPE_NAMES[g.t]||(def?def.name:('TIPE '+g.t));
      bankTitle.textContent='BANK: '+typeName+' \u00d7'+g.items.length+' \u00b7 mempegaruhi SEMUA fixture tipe ini';
      // bersihkan fader bank lama (kecuali judul)
      while(bankPane.children.length>1) bankPane.removeChild(bankPane.lastChild);
      for(let c=0;c<f.foot;c++){
        const lbl=document.createElement('label');
        const lab=document.createElement('span');lab.className='lab';
        lab.textContent=(cl.labels[c]||('CH'+(c+1)))+(cl.modes(c)?' \u26a1':'');
        const inp=document.createElement('input');inp.type='range';inp.min=0;inp.max=255;inp.value=0;
        inp.dataset.bankTy=g.t; inp.dataset.bankCh=c;
        if(cl.modes(c)){ inp.classList.add('switchmode'); inp.title='SWITCH: nilai dibatasi 0 / 255'; }
        const val=document.createElement('span');val.className='val';
        const vv=document.createElement('span');vv.textContent='0'; vv.id='bk'+g.t+'_'+c+'v';
        val.appendChild(vv);
        inp.addEventListener('input',()=>{
          // v48: snap client-side utk switch mode (firmware snap juga — dua lapis)
          let v=+inp.value;
          if(cl.modes(c)) v=(v<128)?0:255;
          inp.value=v; vv.textContent=v; paintFill(inp);
          stopAuto();
          // tandai channel sedang di-drag bank -> slider member dikecualikan
          // dari sync server (anti echo-bounce)
          bankDragMark(g.t+'_'+c);
          // kirim SET ke semua fixture tipe ini (loop kecil, N<=32)
          g.items.forEach(fi=>{
            const key=fi+'_'+c;
            const s=sliders[key];
            if(s){ s.value=v; document.getElementById(key+'v').textContent=v; paintFill(s); }
          });
          bankSend(g.t,c,v);   // v48: satu pesan agregat, bukan N request
        });
        inp.addEventListener('change',()=>bankDragUntil=Date.now()+600);
        lbl.appendChild(lab);lbl.appendChild(inp);lbl.appendChild(val);
        bankPane.appendChild(lbl);
      }
      bankPane.style.display='';
    };
    // klik nama fixture = pilih fixture utk tampilan bank
    g.items.forEach(i=>{
      const f=FIX[i]; const wrap=document.createElement('div'); wrap.className='fix';
      const nm=document.createElement('div'); nm.className='fix-name';
      nm.style.cursor='pointer';
      nm.textContent=f.name+'   '+f.start+'-'+(f.start+f.foot-1);
      nm.addEventListener('click',()=>{
        // highlight pilihan di pane kiri
        grd.querySelectorAll('.fix-name').forEach(x=>x.style.color='');
        nm.style.color='#4fc3f7';
        renderBank(i);
      });
      wrap.appendChild(nm);
      const cl=channelLabelsFor(i);
      for(let c=0;c<f.foot;c++){
        const lbl=document.createElement('label');
        const lab=document.createElement('span');lab.className='lab';
        lab.textContent=(cl.labels[c]||('CH'+(c+1)))+(cl.modes(c)?' \u26a1':'');
        const inp=document.createElement('input');inp.type='range';inp.min=0;inp.max=255;inp.value=0;
        const idi=keyOf(i,c); inp.id=idi; inp.dataset.fi=i; inp.dataset.ch=c;
        if(cl.modes(c)){ inp.classList.add('switchmode'); inp.title='SWITCH: nilai dibatasi 0 / 255'; }
        const val=document.createElement('span');val.className='val';val.id=idi+'v';val.textContent='0';
        inp.addEventListener('input',()=>{
          // v48: snap client-side switch mode
          if(cl.modes(c)) inp.value=(+inp.value<128)?0:255;
          onInput(inp);
        });
        inp.addEventListener('change',()=>onRelease(inp));
        inp.addEventListener('blur',()=>onRelease(inp));
        sliders[idi]=inp; allKeys.push(idi);
        lbl.appendChild(lab);lbl.appendChild(inp);lbl.appendChild(val);
        wrap.appendChild(lbl);
      }
      grd.appendChild(wrap);
    });
    // pilih fixture pertama tipe ini sebagai default bank saat load
    if(g.items.length) renderBank(g.items[0]);
    box.appendChild(sec);
  });
  // v48: group yatim tidak lagi dirender — fader GRUP dihapus dari UI
  // (digantikan BANK). Group tanpa member tidak punya padanan bank; tampilan
  // "GRUP LAIN ×0" hanya membingungkan. Endpoint /grp tetap utuh (kompat).
}
// Sinkron fader grup dari state server. Aturan: fader grup hanya di-set bila
// SEMUA membernya bernilai sama. Kalau member berbeda (mis. satu fixture
// diubah lewat fader per-channel), posisi fader grup dibiarkan -> tidak ada
// lompatan visual saat menggeser satu channel fixture.
function syncGroups(j){
  if(!j.cur) return;
  GRP.forEach((g,i)=>{
    let vals=[];
    for(let fi=0;fi<FIX.length;fi++){
      if(FIX[fi].type===g.type && g.offset<FIX[fi].foot){
        const v=j.cur[fi+'_'+g.offset];
        if(v!==undefined) vals.push(v);
      }
    }
    if(vals.length===0) return;
    const same=vals.every(v=>v===vals[0]);
    if(!same) return;                       // member berbeda -> jangan sentuh fader grup
    // v48: fader grup dihapus dari UI (digantikan BANK) — guard null.
    const s=$('g'+i); if(!s) return;
    s.value=vals[0];
    document.getElementById('g'+i+'v').textContent=vals[0]; paintFill(s);
  });
}
let chaseOn=false;
let sceneOn=false, selScene=-1, sceneEdit=false;
let activeKey=null;   // slider yang sedang digeser user (dilewati polling)
// v48: channel yang sedang dikendalikan BANK (drag). Slider member-nya
// dikecualikan dari sinkronisasi server SELAMA drag + 600 ms setelahnya —
// mencegah echo lama (broadcast 10 Hz kalah cepat dari drag) menimpa nilai
// baru dengan nilai basi -> fader "memantul-mantul" (laporan user v48).
let bankDragUntil=0;
function bankDragMark(ch){ bankDragUntil=Date.now()+600; }
function bankDragActive(){
  return Date.now()<bankDragUntil;
}
// Throttle: maks 1 request /set beredar; nilai terakhir dikirim setelahnya.
let setInFlight=false,setPending=null;
// v43: kontrol dikirim via WebSocket bila tersambung (tanpa overhead & antrean
// HTTP -> fader realtime). HTTP /set tetap jadi fallback otomatis.
function wsCtl(obj){
  if(wsOk && ws && ws.readyState===1){ try{ ws.send(JSON.stringify(obj)); return true; }catch(_){} }
  return false;
}
function pushLive(obj,fallbackQ){ if(!wsCtl(obj)) pushCtrl(fallbackQ); }
function pushOne(fi,c,v){
  if(wsCtl({t:'s',k:fi+'_'+c,v:+v})) return;   // instan tanpa round-trip
  if(setInFlight){ setPending=[fi,c,v]; return; }
  setInFlight=true;
  api('/set?'+fi+'_'+c+'='+v).catch(e=>showError(e.message)).finally(()=>{
    setInFlight=false;
    if(setPending){ const p=setPending; setPending=null; pushOne(p[0],p[1],p[2]); }
  });
}
function onInput(inp){
  activeKey=inp.id;
  document.getElementById(inp.id+'v').textContent=inp.value;paintFill(inp);
  stopAuto();
  if(selPreset!==-1){ selPreset=-1; updateBankSel(); updatePinfo(); }   // cukup update class, tanpa rebuild DOM
  pushOne(inp.dataset.fi,inp.dataset.ch,inp.value);
}
function onRelease(inp){activeKey=null;}
// ---- v48: BANK fader — satu pesan agregat untuk semua fixture tipe itu
// {"t":"b","ty":<type>,"c":<ch>,"v":<val>} — firmware menulis channel sama
// di semua fixture tipe tsb dalam SATU operasi (nol polling, nol N-request).
// Throttle: maks 1 pesan per 30 ms (paritas frame DMX 25ms) saat drag.
let bankLastAt=0, bankPending=null, bankTimer=null;
function bankSend(ty,c,v){
  const now=Date.now();
  const fire=()=>{
    bankLastAt=Date.now(); bankPending=null; bankTimer=null;
    const msg={t:'b',ty:ty,c:c,v:v};
    if(!wsCtl(msg)){
      // fallback HTTP: loop /set — jarang terpakai (WS nyaris selalu up)
      FIX.forEach((f,fi)=>{
        if(f.type===ty && c<f.foot) api('/set?'+fi+'_'+c+'='+v).catch(()=>{});
      });
    }
  };
  if(now-bankLastAt>=30){ fire(); return; }
  bankPending=[ty,c,v];
  if(!bankTimer) bankTimer=setTimeout(fire,30-(now-bankLastAt));
}
// Umpan balik non-blocking (pengganti alert utk sukses)
let toastT=null;
function toast(msg){
  const t=$('toast'); if(!t) return;
  t.textContent=msg; t.classList.add('show');
  clearTimeout(toastT); toastT=setTimeout(()=>t.classList.remove('show'),2400);
}
function showError(msg){ toast('Error: '+msg); const s=$('statTxt'); if(s){s.textContent='Error: '+msg;s.style.color='#e74c3c';} }
// Status preset terpilih -> selalu terlihat preset mana yang sedang diedit
function updatePinfo(){
  const el=$('pinfo'); if(!el) return;
  if(selPreset<0) el.textContent='Preset terpilih: belum ada \u2014 ketuk pad untuk memuat (dan memilih)';
  else el.textContent='\u25b8 Preset terpilih: #'+(selPreset+1)+' \u00b7 fade '+pfade+' ms \u00b7 hold '+phold+' ms';
}
function syncSelectedState(j){
  if(j.selectedPreset!==undefined && j.selectedPreset>=0 && j.selectedPreset<N_PRESETS){
    if(selPreset!==j.selectedPreset){ selPreset=j.selectedPreset; renderBank(); applyFadeOfSelected(); }
  }
  if(j.selectedScene!==undefined && j.selectedScene>=0 && j.selectedScene<NSCN){
    if(selScene!==j.selectedScene){ selScene=j.selectedScene; renderSceneBank(); renderSteps(); }
  }
  const save=$('saveStatus');
  if(save && j.nvsDirty!==undefined){ save.textContent=j.nvsDirty?'Perubahan belum disimpan':'Data tersimpan'; }
}
function paintAll(){paintFill($('master'));$('masterv').textContent=$('master').value;allKeys.forEach(k=>{const s=sliders[k];paintFill(s);document.getElementById(k+'v').textContent=s.value;});}
function setChase(on){chaseOn=on;const b=$('btnChase');b.textContent=on?'Chase ON':'Chase OFF';b.classList.toggle('on',on);api('/chase?'+(on?'on=1':'off=1')).catch(e=>showError(e.message));}
function applyChaseBtn(){const b=$('btnChase');b.textContent=chaseOn?'Chase ON':'Chase OFF';b.classList.toggle('on',chaseOn);}
function applySceneBtn(){
  // Tombol PLAY sudah diganti mode EDIT/SHOW; status playback kini tampil di #sinfo.
  const b=$('btnSPlay'); if(b){ b.textContent=sceneOn?'STOP':'PLAY'; b.classList.toggle('on',sceneOn); }
}
// Hentikan semua auto-run (chase & scene) — dipanggil saat operator mengambil alih manual.
function stopAuto(){
  if(chaseOn) setChase(false);
  if(sceneOn){ sceneOn=false; applySceneBtn(); api('/splay?off=1').catch(e=>showError(e.message)); }
}
function pushCtrl(q){api('/ctrl?'+q).catch(e=>showError(e.message));}
function setSaveStatus(text,error){
  const el=$('saveStatus'); if(!el) return;
  el.textContent=text; el.style.color=error?'var(--bad)':'var(--muted)';
}
$('btnSaveData').addEventListener('click',()=>{
  setSaveStatus('Menyimpan...');
  api('/save',{method:'POST'})
    .then(()=>{setSaveStatus('Data tersimpan');toast('Preset dan scene tersimpan ke NVS');})
    .catch(e=>{setSaveStatus('Gagal menyimpan',true);showError(e.message);});
});
$('btnLoadData').addEventListener('click',()=>{
  if(!confirm('Muat ulang data tersimpan dari NVS? Perubahan yang belum di-Save akan hilang.'))return;
  api('/loaddata')
    .then(()=>{setSaveStatus('Data tersimpan');toast('Data dimuat dari NVS');refreshPresets();reloadScenes();})
    .catch(e=>{setSaveStatus('Gagal memuat',true);showError(e.message);});
});
// Fade & Hold: milik preset terpilih (atau nilai awal utk REKAM berikutnya).
let pfade=600, phold=1500;
function setPFadeLabel(v){$('pfadev').textContent=(v/1000).toFixed(1)+'s';}
function setPHoldLabel(v){$('pholdv').textContent=(v/1000).toFixed(1)+'s';}
function applyPFadeUI(){ $('pfade').value=pfade; setPFadeLabel(pfade); paintFill($('pfade'));
                         $('phold').value=phold; setPHoldLabel(phold); paintFill($('phold')); }
$('master').addEventListener('input',()=>{activeKey='master';$('masterv').textContent=$('master').value;paintFill($('master'));pushLive({t:'mast',v:+$('master').value},'mast='+$('master').value);});
$('master').addEventListener('change',()=>onRelease($('master')));
$('master').addEventListener('blur',()=>onRelease($('master')));
// STROBE MASTER: 0=off; >0 = kedip global, makin besar makin cepat.
$('mstrb').addEventListener('input',()=>{activeKey='mstrb';$('mstrbv').textContent=$('mstrb').value;paintFill($('mstrb'));pushLive({t:'strb',v:+$('mstrb').value},'strb='+$('mstrb').value);});
$('mstrb').addEventListener('change',()=>onRelease($('mstrb')));
$('mstrb').addEventListener('blur',()=>onRelease($('mstrb')));
// input = update label saja; persist ke NVS hanya saat lepas (change) -> hindari tulis flash tiap tick
// Jalur simpan Fade & Hold SAMA PERSIS lewat satu fungsi persistTiming().
function persistTiming(){
  if(selPreset===-1){ toast('Belum ada preset terpilih \u2014 ketuk pad dulu'); return; }
  api('/psetfade?n='+(selPreset+1)+'&f='+pfade+'&h='+phold)
    .then(()=>{
      refreshPresets();   // reconcile otoritatif: baca balik dari server
      toast('Fade/Hold disimpan ke preset #'+(selPreset+1)); updatePinfo();
    })
    .catch(e=>showError(e.message));
}
$('pfade').addEventListener('input',()=>{pfade=+$('pfade').value;setPFadeLabel(pfade);paintFill($('pfade'));});
$('pfade').addEventListener('change',persistTiming);
$('phold').addEventListener('input',()=>{phold=+$('phold').value;setPHoldLabel(phold);paintFill($('phold'));});
$('phold').addEventListener('change',persistTiming);
$('btnBlack').addEventListener('click',()=>{$('master').value=0;$('masterv').textContent=0;pushLive({t:'all',v:0},'all=off');});
// v49: Art-Net mode toggle — LOCAL (default) / NETWORK (dengar UDP 6454)
let artnetMode=false;
function applyArtnetBtn(){const b=$('btnArtnet');b.textContent='Art-Net: '+(artnetMode?'NETWORK':'LOCAL');b.classList.toggle('on',artnetMode);}
$('btnArtnet').addEventListener('click',()=>{
  artnetMode=!artnetMode; applyArtnetBtn();
  api('/artnet?mode='+(artnetMode?'network':'local')).catch(e=>showError(e.message));
});
// Aman perangkat: "Penuh" hanya untuk PAR (dimmer+RGB); moving/fog/strobe tetap 0.
$('btnWhite').addEventListener('click',()=>{allKeys.forEach(k=>{const fi=+k.split('_')[0];sliders[k].value=(FIX[fi].type===0)?255:0;});paintAll();pushLive({t:'all',v:1},'all=on');});
$('btnOff').addEventListener('click',()=>{allKeys.forEach(k=>sliders[k].value=0);paintAll();pushLive({t:'all',v:0},'all=off');});
$('btnChase').addEventListener('click',()=>setChase(!chaseOn));
let editMode=false,ignoreDimmer=false;const $idim=$('idim');$idim.addEventListener('change',()=>{ignoreDimmer=$idim.checked;$('idimTag').classList.toggle('on',ignoreDimmer);});
let presetsData=[],selPreset=-1;
function rgbStr(p){if(!p||!p.used)return'';return 'rgb('+p.r+','+p.g+','+p.b+')';}
// Kumpulan slot preset yang dirujuk scene (0-based). Preset "terhapus"
// (used=0) yang masih dirujuk scene tetap dimainkan -> tandai (v45, Bug 2).
function sceneRefSet(){
  const s=new Set();
  SCN.forEach(row=>row.forEach(v=>{ if(v>=1&&v<=N_PRESETS) s.add(v-1); }));
  return s;
}
function renderBank(){const bank=$('bank');bank.innerHTML='';const refs=sceneRefSet();presetsData.forEach((p,i)=>{const pad=document.createElement('button');let cls='pad'+(p.used?'':' empty')+(selPreset===i?' sel':'');if(!p.used&&refs.has(i))cls+=' hidden-scene';pad.className=cls;pad.setAttribute('aria-label','Preset '+(i+1));const csw=document.createElement('span');csw.className='csw';if(p.used)csw.style.background=rgbStr(p);const num=document.createElement('span');num.className='num';num.textContent=i+1;pad.appendChild(num);pad.appendChild(csw);pad.addEventListener('click',()=>onPad(i));bank.appendChild(pad);});}
function updateBankSel(){const bank=$('bank');for(let i=0;i<bank.children.length;i++)bank.children[i].classList.toggle('sel',i===selPreset);}
// ---------------------------------------------------------------
// SCENE UI
// ---------------------------------------------------------------
function buildSceneBank(){
  const box=$('sbank'); box.innerHTML='';
  for(let i=0;i<NSCN;i++){
    const used=SCN[i].some(v=>v>0);
    const pad=document.createElement('button');
    pad.className='pad'+(used?'':' empty')+(selScene===i?' sel':'')+(sceneOn&&serverScene===i?' playing':'');
    pad.setAttribute('aria-label','Scene '+(i+1));
    const num=document.createElement('span');num.className='num';num.textContent='S'+(i+1);
    pad.appendChild(num);
    pad.addEventListener('click',()=>{
      selScene=i; renderSceneBank(); renderSteps();
      if(sceneEdit){
        // EDIT MODE: hanya memilih utk diedit, TIDAK menghasilkan output apa pun
        api('/select?s='+(i+1)).catch(e=>showError(e.message));
        return;
      }
      // SHOW MODE: klik scene = langsung play; klik scene yg sedang main = stop
      if(sceneOn && serverScene===i){ stopAuto(); renderSteps(); return; }
      if(chaseOn) setChase(false);
      api('/splay?s='+(i+1)).then(()=>{ sceneOn=true; applySceneBtn(); })
        .catch(e=>showError(e.message));
    });
    box.appendChild(pad);
  }
}
// Update visual pad scene TANPA rebuild DOM (dipanggil seleksi/play/reload).
function renderSceneBank(){
  const box=$('sbank');
  for(let i=0;i<NSCN && i<box.children.length;i++){
    const pad=box.children[i];
    const used=SCN[i].some(v=>v>0);
    pad.classList.toggle('empty',!used);
    pad.classList.toggle('sel',selScene===i);
    pad.classList.toggle('playing',sceneOn&&serverScene===i);
  }
}
function renderSteps(){
  const box=$('steps'); box.innerHTML='';
  const arr=(selScene>=0)?SCN[selScene]:null;
  for(let k=0;k<NSTEPS;k++){
    const c=document.createElement('div'); c.className='step';
    const v=arr?arr[k]:0;
    if(v>0){ c.textContent=v; c.classList.add('fill'); } else { c.textContent='\u00b7'; }
    box.appendChild(c);
  }
  const info=$('sinfo');
  if(selScene<0) info.textContent='pilih scene (S1-S20), lalu EDIT utk merangkai preset';
  else{
    const filled=arr.filter(v=>v>0).length;
    info.textContent='Scene '+(selScene+1)+': '+filled+'/'+NSTEPS+' langkah terisi'
      +(sceneEdit?' \u2014 ketuk pad PRESET utk menambah langkah':'');
  }
}
function reloadScenes(){
  api('/scenes').then(j=>{
    for(let i=0;i<j.length;i++) SCN[i]=j[i];
    renderSceneBank(); renderSteps();
  }).catch(e=>showError(e.message));
}
let lastSceneRev=null;   // v46: deteksi sceneRev dari server
let serverScene=-1;
function onPad(i){
  if(sceneEdit){
    // Mode EDIT scene: ketuk pad preset = tambahkan sebagai langkah berikutnya
    if(selScene<0){ alert('pilih scene dulu (S1-S20)'); return; }
    const arr=SCN[selScene];
    let last=-1, slot=-1;
    for(let k=0;k<NSTEPS;k++){
      if(arr[k]>0) last=k;
      else if(slot<0) slot=k;
    }
    if(slot<0){ alert('Scene penuh (maks '+NSTEPS+' langkah)'); return; }
    if(last>=0 && arr[last]===i+1){
      $('sinfo').textContent='preset '+(i+1)+' sama dgn langkah terakhir (tidak ditambah)';
      return;
    }
    arr[slot]=i+1; renderSteps();            // feedback instan (optimistic UI)
    api('/spush?s='+(selScene+1)+'&p='+(i+1)).then(()=>reloadScenes())
      .catch(e=>{ reloadScenes(); showError(e.message); });
    return;
  }
  if(delMode){
    const p=presetsData[i];
    if(!p.used) return;                                   // pad kosong: tak ada yang dihapus
    if(!confirm('Hapus preset '+(i+1)+'?')){ exitDelMode(); return; }
    api('/pclear?n='+(i+1)).then(()=>refreshPresets()).catch(e=>showError(e.message));
    exitDelMode();
  } else if(editMode){
    api('/psave?n='+(i+1)+(ignoreDimmer?'&idim=1':'')+'&f='+pfade+'&h='+phold)
      .then(()=>{ refreshPresets(); toast('Tersimpan ke preset #'+(i+1)); })
      .catch(e=>showError(e.message));
    selPreset=i; updateBankSel(); updatePinfo();     // hasil rekam langsung terpilih & terlihat
    editMode=false;$('btnEdit').classList.remove('on');$('btnEdit').textContent='REKAM OFF';
  }else{
    const p=presetsData[i];
    if(!p.used)return;
    selPreset=i;renderBank();
    stopAuto();
    // fade & hold milik preset ini (0 ms = snap, sah); fallback hanya utk data tak valid
    pfade=(typeof p.f==='number'&&p.f>=0)?p.f:600;
    phold=(typeof p.h==='number'&&p.h>=100)?p.h:1500;
    applyPFadeUI(); updatePinfo();
    api('/pload?n='+(i+1)).catch(e=>showError(e.message));
  }
}
function exitDelMode(){delMode=false;$('btnDel').classList.remove('on');$('btnDel').textContent='HAPUS OFF';$('bank').classList.remove('deleting');}
function refreshPresets(){
  api('/presets').then(j=>{
    presetsData=j; renderBank();
    // Setelah data preset masuk, pulihkan Fade/Hold dari preset yang sedang terpilih
    applyFadeOfSelected();
  }).catch(e=>showError(e.message));
}
// Pulihkan nilai slider Fade/Hold dari preset terpilih (dipakai saat buka/refresh halaman)
function applyFadeOfSelected(){
  if(selPreset>=0 && presetsData[selPreset]){
    const p=presetsData[selPreset];
    pfade=(typeof p.f==='number'&&p.f>=0)?p.f:600;
    phold=(typeof p.h==='number'&&p.h>=100)?p.h:1500;
    applyPFadeUI(); updatePinfo();
  }
}
$('btnEdit').addEventListener('click',()=>{editMode=!editMode;if(editMode&&delMode){delMode=false;$('btnDel').classList.remove('on');$('btnDel').textContent='HAPUS OFF';$('bank').classList.remove('deleting');}$('btnEdit').classList.toggle('on',editMode);$('btnEdit').textContent=editMode?'REKAM ON':'REKAM OFF';});
let delMode=false;
$('btnDel').addEventListener('click',()=>{
  delMode=!delMode;
  if(delMode&&editMode){editMode=false;$('btnEdit').classList.remove('on');$('btnEdit').textContent='REKAM OFF';}
  $('btnDel').classList.toggle('on',delMode);
  $('btnDel').textContent=delMode?'HAPUS ON':'HAPUS OFF';
  $('bank').classList.toggle('deleting',delMode);
});
// --- Scene controls: dua mode (EDIT = tanpa output, SHOW = klik langsung main) ---
let sceneMode='show';
function setSceneMode(m){
  sceneMode=m;
  sceneEdit=(m==='edit');
  $('btnModeEdit').classList.toggle('on',sceneEdit);
  $('btnModeShow').classList.toggle('on',m==='show');
  $('btnSPop').disabled=!sceneEdit;
  $('btnSClear').disabled=!sceneEdit;
  if(sceneEdit) stopAuto();          // EDIT MODE: scene tidak boleh menghasilkan output
  renderSteps();
}
$('btnModeEdit').addEventListener('click',()=>setSceneMode('edit'));
$('btnModeShow').addEventListener('click',()=>setSceneMode('show'));
setSceneMode('show');
$('btnSPop').addEventListener('click',()=>{
  if(!sceneEdit){ toast('Aktifkan EDIT MODE dulu'); return; }
  if(selScene<0){ alert('pilih scene dulu'); return; }
  api('/spop?s='+(selScene+1)).then(()=>reloadScenes()).catch(e=>showError(e.message));
});
$('btnSClear').addEventListener('click',()=>{
  if(!sceneEdit){ toast('Aktifkan EDIT MODE dulu'); return; }
  if(selScene<0){ alert('pilih scene dulu'); return; }
  if(!confirm('Kosongkan scene '+(selScene+1)+'?')) return;
  api('/sclear?s='+(selScene+1)).then(()=>reloadScenes()).catch(e=>showError(e.message));
});
// Tombol Cek/Play: memutar scene TERPILIH utk dicek, di mode EDIT maupun SHOW.
// Ketika sedang main, tombol ini menjadi STOP.
$('btnSPlay').addEventListener('click',()=>{
  if(sceneOn){ stopAuto(); return; }   // stopAuto kirim /splay?off=1 + update tombol
  if(selScene<0){ toast('pilih scene dulu (klik pad S1-S20)'); return; }
  if(chaseOn) setChase(false);
  api('/splay?s='+(selScene+1)).then(()=>{ sceneOn=true; applySceneBtn(); })
    .catch(e=>showError(e.message));
});
$('btnExport').addEventListener('click',()=>{window.location='/export';});
$('btnImport').addEventListener('click',()=>$('fileIn').click());
$('fileIn').addEventListener('change',e=>{const file=e.target.files[0];if(!file)return;const fd=new FormData();fd.append('file',file);api('/import',{method:'POST',body:fd}).then(()=>{toast('Data import berhasil');refreshPresets();reloadScenes();}).catch(err=>showError(err.message));e.target.value='';});
// Terapkan keadaan server ke UI. Lewati slider yang sedang digeser user.
function syncFromServer(j, skipActive){
  if(j.master===undefined) return;
  if(!skipActive || activeKey!=='master'){ $('master').value=j.master; $('masterv').textContent=j.master; paintFill($('master')); }
  if(j.strb!==undefined && (!skipActive || activeKey!=='mstrb')){ $('mstrb').value=j.strb; $('mstrbv').textContent=j.strb; paintFill($('mstrb')); }
  allKeys.forEach(k=>{
    if(skipActive && k===activeKey) return;
    // v48 anti-bounce: channel sedang di-drag BANK -> jangan ditimpa echo
    // server (broadcast 10 Hz bisa membawa nilai basi yang lebih tua dari
    // drag; menimpanya = fader memantul, mis. 255 turun ke 226).
    if(bankDragActive()) return;
    const v=(j.cur&&j.cur[k]!==undefined)?j.cur[k]:0;
    sliders[k].value=v; document.getElementById(k+'v').textContent=v; paintFill(sliders[k]);
  });
  if(j.chaseOn!==undefined && j.chaseOn!==chaseOn){ chaseOn=j.chaseOn; applyChaseBtn(); }
  if(j.artnet!==undefined){ const an=(j.artnet==='network'); if(an!==artnetMode){ artnetMode=an; applyArtnetBtn(); } }   // v49
  // v49: deck fisik — indikator bank + tombol di panel scene
  if(j.hwBank!==undefined){
    const el=$('hwDeck'); if(el){
      el.textContent='DECK FISIK \u00b7 Bank '+(j.hwBank+1)+'-'+(Math.min(j.hwBank+4,NSCN))+
        ' \u00b7 encoder '+(j.hwEnc||0)+' detent \u00b7 '+
        ((j.hwB||[]).map(b=>b?'#':'-').join(''));
    }
  }
  if(j.sceneOn!==undefined && j.sceneOn!==sceneOn){ sceneOn=j.sceneOn; applySceneBtn(); if(!sceneOn) renderSteps(); }
  syncSelectedState(j);
  if(j.selectedScene!==undefined && j.selectedScene>=0 && j.selectedScene<NSCN){
    if(selScene!==j.selectedScene){ selScene=j.selectedScene; renderSceneBank(); renderSteps(); }
  }
  serverScene=(j.scn!==undefined && j.scn>=0)?j.scn:-1;
  // v46: isi scene berubah di server (COW / edit dari client lain) -> reload
  if(j.sceneRev!==undefined && j.sceneRev!==lastSceneRev){
    if(lastSceneRev!==null) reloadScenes();   // skip saat init pertama
    lastSceneRev=j.sceneRev;
  }
  if(sceneOn){ renderSceneBank(); }
  // Indikator playback: scene & langkah yang sedang main selalu terlihat
  const si=$('sinfo');
  if(si && sceneOn){
    if(j.scn!==undefined && j.scn>=0){
      const st=(j.stp!==undefined && j.stp>=0)?(j.stp+1):1;
      si.textContent='\u25b6 MEMUTAR S'+(j.scn+1)+' \u00b7 langkah '+st+'/'+NSTEPS;
    }
  }
}
buildSections();buildSceneBank();renderSteps();updatePinfo();refreshPresets();
// Init penuh (tidak melewati apa pun)
api('/cur').then(j=>{syncFromServer(j,false);syncGroups(j);$('status').classList.add('live');$('statTxt').textContent='tersimpan';}).catch(e=>{$('statTxt').textContent='server tidak menjawab: '+e.message;});
// WebSocket realtime (port 81): ESP32 push state saat berubah -> tanpa polling.
let ws=null,wsOk=false,wsTries=0;
function startWs(){
  try{ ws=new WebSocket('ws://'+location.hostname+':81/ws'); }catch(e){ scheduleWsRetry(); return; }
  ws.onopen =()=>{ wsOk=true; wsTries=0; $('status').classList.add('live'); };
  ws.onmessage=ev=>{ try{ syncFromServer(JSON.parse(ev.data),true); $('status').classList.add('live'); }catch(_){} };
  ws.onclose=()=>{ wsOk=false; scheduleWsRetry(); };
  ws.onerror=()=>{ try{ws.close();}catch(_){} };
}
function scheduleWsRetry(){
  if(++wsTries>5) return;                       // gagal terus -> cukup andalkan polling fallback
  setTimeout(startWs,Math.min(8000,500*wsTries));
}
startWs();
// Fallback polling: hanya jalan saat WebSocket belum/gagal tersambung.
setInterval(()=>{ if(wsOk||document.hidden||httpBusy||httpQueue.length>0) return; api('/cur').then(j=>syncFromServer(j,true)).catch(()=>{}); },1000);

// =============================================================
// v45: PATCH PANEL — edit alamat DMX & jumlah fixture
// =============================================================
// Data fixture aktif diambil dari FIX (diinjeksi server saat load page).
// Perubahan dikirim via POST /fixes. Validasi klien: alamat akhir <= 512,
// tidak tumpang tindih. Server tetap otoritatif (validasi ulang).
const FIX_TYPES=[{v:0,l:'PAR'},{v:1,l:'Moving Head'},{v:2,l:'Beam'},{v:3,l:'Strobe'},{v:4,l:'Fog'}];
let patchData=[];   // salinan editable dari FIX
function patchClone(){ patchData=FIX.map(f=>({name:f.name,type:f.type,start:f.start,foot:f.foot,hasMove:f.hasMove||0})); }
function patchValidate(){
  const errs=[];
  for(let i=0;i<patchData.length;i++){
    const f=patchData[i];
    const end=f.start+f.foot-1;
    if(f.start<1||f.foot<1){ errs.push({i,code:'range'}); continue; }
    if(end>512){ errs.push({i,code:'over512'}); continue; }
    for(let j=0;j<i;j++){
      const g=patchData[j];
      if(f.start<=g.start+g.foot-1 && g.start<=end){ errs.push({i,code:'overlap',j}); break; }
    }
  }
  return errs;
}
function renderPatchTable(){
  const box=$('patchTable'); if(!box) return;
  const errs=patchValidate();
  const errSet=new Set(errs.map(e=>e.i));
  let h='<table><thead><tr><th>Nama</th><th>Tipe</th><th>Alamat Awal</th><th>Jumlah Ch</th><th>Akhir</th><th>Pan/Tilt</th><th></th></tr></thead><tbody>';
  patchData.forEach((f,i)=>{
    const end=f.start+f.foot-1;
    const isErr=errSet.has(i);
    h+='<tr>';
    // v48 BUGFIX: sel turunan diberi id (pstart{i}/pfoot{i}/pend{i}) supaya
    // ketikan TIDAK memicu rebuild tabel penuh — dulu tiap huruf memanggil
    // renderPatchTable() -> innerHTML dibangun ulang -> fokus hilang dan
    // halaman scroll ke atas ("cursor menghilang"). Lihat patchUpdateDerived().
    h+='<td><input type="text" data-i="'+i+'" data-f="name" value="'+f.name.replace(/"/g,'&quot;')+'" maxlength="24"></td>';
    h+='<td><select data-i="'+i+'" data-f="type">';
    FIX_TYPES.forEach(t=>{ h+='<option value="'+t.v+'"'+(f.type===t.v?' selected':'')+'>'+t.l+'</option>'; });
    // v48: opsi custom type (slot 5-15 yang sudah didefinisikan)
    CT.forEach(c=>{ h+='<option value="'+c.slot+'"'+(f.type===c.slot?' selected':'')+'>* '+c.name+'</option>'; });
    h+='</select></td>';
    h+='<td><input type="number" id="pstart'+i+'" data-i="'+i+'" data-f="start" min="1" max="512" value="'+f.start+'" class="'+(isErr?'err':'')+'"></td>';
    h+='<td><input type="number" id="pfoot'+i+'" data-i="'+i+'" data-f="foot" min="1" max="512" value="'+f.foot+'" class="'+(isErr?'err':'')+'"></td>';
    h+='<td id="pend'+i+'" style="color:'+(end>512?'var(--bad)':'var(--muted)')+'">'+end+'</td>';
    h+='<td><input type="checkbox" data-i="'+i+'" data-f="hasMove" '+(f.hasMove?'checked':'')+' style="width:auto"></td>';
    h+='<td><button class="del" data-del="'+i+'">Hapus</button></td>';
    h+='</tr>';
  });
  h+='</tbody></table>';
  box.innerHTML=h;
  patchUpdateStatus(patchValidate());
}
// v48: update TANPA rebuild DOM — fokus & posisi scroll input tetap.
// Hanya sel turunan yang berubah (akhir, kelas err, teks status).
function patchUpdateDerived(){
  const errs=patchValidate();
  const errSet=new Set(errs.map(e=>e.i));
  patchData.forEach((f,i)=>{
    const end=f.start+f.foot-1;
    const ec=document.getElementById('pend'+i);
    if(ec){ ec.textContent=end; ec.style.color=(end>512)?'var(--bad)':'var(--muted)'; }
    const se=document.getElementById('pstart'+i);
    if(se) se.classList.toggle('err',errSet.has(i));
    const fe=document.getElementById('pfoot'+i);
    if(fe) fe.classList.toggle('err',errSet.has(i));
  });
  patchUpdateStatus(errs);
}
function patchUpdateStatus(errs){
  const st=$('patchStatus'); if(!st) return;
  if(errs.length===0){
    const total=patchData.reduce((s,f)=>Math.max(s,f.start+f.foot-1),0);
    st.textContent='Valid. '+patchData.length+' fixture, channel tertinggi: '+total+'/512.';
    st.style.color='#7bd88f';
  } else {
    const msgs=errs.map(e=>{
      const nm=patchData[e.i].name||('#'+(e.i+1));
      if(e.code==='over512') return nm+': alamat akhir melebihi 512';
      if(e.code==='overlap') return nm+' tumpang tindih dengan '+(patchData[e.j]?patchData[e.j].name:'#'+(e.j+1));
      return nm+': alamat/jumlah tidak valid';
    });
    st.textContent='Error: '+msgs.join('; ');
    st.style.color='var(--bad)';
  }
}
function bindPatchEvents(){
  const box=$('patchTable'); if(!box) return;
  box.addEventListener('input',e=>{
    const el=e.target, i=+el.dataset.i, f=el.dataset.f;
    if(f===undefined||isNaN(i)) return;
    if(f==='name') patchData[i].name=el.value;
    else if(f==='type') patchData[i].type=+el.value;
    else if(f==='start') patchData[i].start=+el.value||1;
    else if(f==='foot') patchData[i].foot=+el.value||1;
    else if(f==='hasMove') patchData[i].hasMove=el.checked?1:0;
    // v48 BUGFIX: jangan renderPatchTable() saat 'input' — rebuild innerHTML
    // menghancurkan elemen yang sedang diketik (fokus hilang, scroll lompat).
    // Cukup perbarui sel turunan. Rebuild penuh hanya utk add/delete row.
    patchUpdateDerived();
  });
  box.addEventListener('click',e=>{
    const del=e.target.dataset.del;
    if(del!==undefined){
      const i=+del;
      if(patchData.length<=1){ toast('Minimal 1 fixture harus ada'); return; }
      if(confirm('Hapus fixture "'+(patchData[i].name||'#'+(i+1))+'"?')){
        patchData.splice(i,1);
        renderPatchTable();
      }
    }
  });
}
$('btnPatchSave').addEventListener('click',()=>{
  const errs=patchValidate();
  if(errs.length>0){ toast('Perbaiki error validasi dulu'); return; }
  const payload={count:patchData.length,fixtures:patchData};
  const st=$('patchStatus'); st.textContent='Menyimpan patch...'; st.style.color='var(--muted)';
  api('/fixes',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)})
    .then(()=>{
      st.textContent='Patch tersimpan. Memuat ulang UI...'; st.style.color='#7bd88f';
      toast('Patch tersimpan ke NVS');
      // Reload agar FIX terbaru dari server langsung dipakai
      setTimeout(()=>location.reload(),800);
    })
    .catch(e=>{ st.textContent='Gagal: '+e.message; st.style.color='var(--bad)'; showError(e.message); });
});
$('btnPatchAdd').addEventListener('click',()=>{
  if(patchData.length>=32){ toast('Maksimal 32 fixture'); return; }
  // Cari alamat bebas pertama setelah fixture terakhir
  let nextStart=1;
  patchData.forEach(f=>{ nextStart=Math.max(nextStart,f.start+f.foot); });
  if(nextStart+1>512){ toast('Tidak ada ruang alamat tersisa (maks 512)'); return; }
  patchData.push({name:'FIX '+(patchData.length+1),type:0,start:nextStart,foot:3,hasMove:0});
  renderPatchTable();
});
$('btnPatchReset').addEventListener('click',()=>{
  if(!confirm('Reset patch ke default bawaan (18 fixture)? Perubahan yang belum disimpan hilang.')) return;
  location.reload();
});
patchClone();
renderPatchTable();
bindPatchEvents();

// =============================================================
// v48: EDITOR TIPE CUSTOM (slot 5-15)
// =============================================================
const CT_MIN=5, CT_MAX=15, CT_CH=32;
let ctEditing=null;   // {slot,name,channels,mode[],labels[]}
function ctFillSlotSelect(){
  const sel=$('ctSlot'); sel.innerHTML='';
  for(let s=CT_MIN;s<=CT_MAX;s++){
    const o=document.createElement('option'); o.value=s;
    const def=ctFind(s);
    o.textContent='Slot '+s+(def?(' · '+def.name):' (kosong)');
    sel.appendChild(o);
  }
}
function ctRenderChannels(){
  const box=$('ctChannels'); box.innerHTML='';
  if(!ctEditing) return;
  for(let k=0;k<ctEditing.channels;k++){
    const row=document.createElement('div');
    row.style.cssText='display:flex;gap:6px;align-items:center';
    const num=document.createElement('span'); num.textContent=(k+1)+'.'; num.style.cssText='color:var(--muted);min-width:22px';
    const name=document.createElement('input'); name.type='text'; name.maxLength=8;
    name.value=ctEditing.labels[k]||('CH'+(k+1)); name.style.cssText='flex:1;min-width:70px';
    name.addEventListener('input',()=>{ ctEditing.labels[k]=name.value; });
    const rf=document.createElement('label'); rf.style.cssText='display:flex;gap:3px;align-items:center;white-space:nowrap';
    const rF=document.createElement('input'); rF.type='radio'; rF.name='ctm'+k; rF.checked=!ctEditing.mode[k];
    const rS=document.createElement('input'); rS.type='radio'; rS.name='ctm'+k; rS.checked=!!ctEditing.mode[k];
    rF.addEventListener('change',()=>{ if(rF.checked) ctEditing.mode[k]=0; });
    rS.addEventListener('change',()=>{ if(rS.checked) ctEditing.mode[k]=1; });
    rf.appendChild(rF); rf.appendChild(document.createTextNode('Fader'));
    const rs=document.createElement('label'); rs.style.cssText='display:flex;gap:3px;align-items:center;white-space:nowrap';
    rs.appendChild(rS); rs.appendChild(document.createTextNode('Switch'));
    row.appendChild(num); row.appendChild(name); row.appendChild(rf); row.appendChild(rs);
    box.appendChild(row);
  }
}
function ctLoadSlot(){
  const slot=+$('ctSlot').value;
  const def=ctFind(slot);
  ctEditing = def ? {slot,name:def.name,channels:def.channels,mode:def.mode.slice(),labels:def.labels.slice()}
                  : {slot,name:'CUSTOM'+slot,channels:+$('ctChannels').value||8,mode:new Array(CT_CH).fill(0),labels:[]};
  for(let k=0;k<CT_CH;k++){ if(ctEditing.mode[k]===undefined) ctEditing.mode[k]=0; }
  for(let k=0;k<ctEditing.channels;k++) if(!ctEditing.labels[k]) ctEditing.labels[k]='CH'+(k+1);
  $('ctName').value=ctEditing.name;
  $('ctChannels').value=ctEditing.channels;
  ctRenderChannels();
  $('ctStatus').textContent='Slot '+slot+' dimuat.';
  $('ctStatus').style.color='var(--muted)';
}
$('btnCType').addEventListener('click',()=>{
  const ed=$('ctypeEditor');
  ed.style.display=ed.style.display==='none'?'':'none';
  if(ed.style.display!=='none'){ ctFillSlotSelect(); ctLoadSlot(); }
});
$('ctSlot').addEventListener('change',ctLoadSlot);
$('ctLoad').addEventListener('click',ctLoadSlot);
$('ctClose').addEventListener('click',()=>{ $('ctypeEditor').style.display='none'; });
$('ctName').addEventListener('input',()=>{ if(ctEditing) ctEditing.name=$('ctName').value; });
$('ctChannels').addEventListener('change',()=>{
  if(!ctEditing) return;
  let n=+$('ctChannels').value||8;
  if(n<1)n=1; if(n>CT_CH)n=CT_CH;
  ctEditing.channels=n; $('ctChannels').value=n;
  ctRenderChannels();
});
$('ctSave').addEventListener('click',()=>{
  if(!ctEditing) return;
  ctEditing.name=($('ctName').value||('CUSTOM'+ctEditing.slot)).trim();
  if(!ctEditing.name){ $('ctStatus').textContent='Nama tipe wajib'; $('ctStatus').style.color='var(--bad)'; return; }
  const payload={types:[{slot:ctEditing.slot,used:1,name:ctEditing.name,
    channels:ctEditing.channels,
    mode:ctEditing.mode.slice(0,ctEditing.channels),
    labels:(ctEditing.labels.length?ctEditing.labels:[]).slice(0,ctEditing.channels)}]};
  const st=$('ctStatus'); st.textContent='Menyimpan tipe...'; st.style.color='var(--muted)';
  api('/ctypes',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(payload)})
    .then(()=>{ st.textContent='Tipe custom tersimpan ke NVS.'; st.style.color='#7bd88f';
      // update cache lokal CT supaya label/mode langsung dipakai mixer
      const i=CT.findIndex(c=>c.slot===ctEditing.slot);
      const rec={slot:ctEditing.slot,name:ctEditing.name,channels:ctEditing.channels,
        mode:ctEditing.mode.slice(0,ctEditing.channels),labels:ctEditing.labels.slice(0,ctEditing.channels)};
      if(i>=0) CT[i]=rec; else CT.push(rec);
      toast('Tipe custom tersimpan');
      ctFillSlotSelect();
    })
    .catch(e=>{ st.textContent='Gagal: '+e.message; st.style.color='var(--bad)'; showError(e.message); });
});

// v43: panel WiFi kustom — status live + simpan kredensial baru.
function wifiRefresh(){
  api('/wifistat').then(j=>{
    const st=$('wifiStat'); if(!st) return;
    if(j.connected){ st.textContent='Terhubung ke "'+j.ssid+'" \u00b7 IP '+j.ip+' \u00b7 '+j.rssi+' dBm'+(j.custom?' \u00b7 kustom':''); st.style.color='#7bd88f'; }
    else if(j.pending){ st.textContent='Mencoba menyambung ke "'+j.ssid+'"...'; st.style.color='#ffd54f'; }
    else { st.textContent=(j.apActive?'Mode AP darurat \u00b7 IP '+j.ip:'Tidak terhubung'); st.style.color='#ff8a65'; }
  }).catch(()=>{});
}
$('btnWifiSet').addEventListener('click',()=>{
  const ssid=$('wssid').value.trim(), pass=$('wpass').value;
  if(!ssid){ toast('Isi SSID dulu'); return; }
  $('wifiStat').textContent='Menyimpan kredensial & mencoba menyambung...'; $('wifiStat').style.color='#ffd54f';
  api('/wifiset?ssid='+encodeURIComponent(ssid)+'&pass='+encodeURIComponent(pass))
    .then(()=>{
      toast('Tersimpan. Mencoba koneksi baru...');
      let n=0; const t=setInterval(()=>{ n++; wifiRefresh(); if(n>=15) clearInterval(t); },2000);
      setTimeout(()=>toast('Bila IP berubah, buka alamat IP baru (lihat Serial Monitor)'),3500);
    })
    .catch(e=>{ showError(e.message); wifiRefresh(); });
});
wifiRefresh();
</script>
</body></html>
)HTML";

// JSON fixture (v45: tambah field hasMove utk paritas desktop)
String fixJson(){
  // v47: reserve — 32 fixture x ±70 byte ≈ 2,2 KB
  String j="[";
  j.reserve(2300);
  for(int i=0;i<N_FIX;i++){
    j+="{\"name\":\""+String(fix[i].name)+"\",";
    j+="\"type\":"+String(fix[i].type)+",";
    j+="\"start\":"+String(fix[i].start)+",";
    j+="\"foot\":"+String(fix[i].foot)+",";
    j+="\"hasMove\":"+String(fix[i].hasMove?"true":"false")+"}";
    if(i<N_FIX-1) j+=",";
  }
  j+="]";
  return j;
}

// v45: POST /fixes -> terima konfigurasi fixture baru (JSON body), validasi,
// terapkan, simpan ke NVS. Format body:
//   {"count":N,"fixtures":[{"name":"...","type":0,"start":1,"foot":9,"hasMove":0},...]}
void onFixesPost(){
  if(!server.hasArg("plain")){ sendApiError(400,"no_body","Body JSON kosong"); return; }
  String body=server.arg("plain");
  if(body.length()>6000){ sendApiError(413,"body_too_large","Body terlalu besar"); return; }

  // Parse "count"
  int countIdx=body.indexOf("\"count\":");
  if(countIdx<0){ sendApiError(400,"missing_count","Field count tidak ada"); return; }
  int count=body.substring(countIdx+8).toInt();
  if(count<1||count>MAX_FIX){ sendApiError(400,"count_invalid","Jumlah fixture harus 1-32"); return; }

  // Parse array fixtures secara manual (tanpa library JSON)
  Fixture tmp[MAX_FIX];
  memset(tmp,0,sizeof(tmp));
  int pos=body.indexOf("\"fixtures\":[");
  if(pos<0){ sendApiError(400,"missing_fixtures","Array fixtures tidak ada"); return; }
  pos+=12;
  int parsed=0;
  for(int i=0;i<count;i++){
    int objStart=body.indexOf('{',pos);
    if(objStart<0) break;
    int objEnd=body.indexOf('}',objStart);
    if(objEnd<0) break;
    String obj=body.substring(objStart,objEnd+1);
    // name
    int nm=obj.indexOf("\"name\":\"");
    if(nm>=0){
      int ns=nm+8, ne=obj.indexOf('"',ns);
      if(ne>ns){ String s=obj.substring(ns,min(ne,ns+24)); s.toCharArray(tmp[i].name,25); }
    }
    // type
    int tp=obj.indexOf("\"type\":");
    if(tp>=0) tmp[i].type=(uint8_t)obj.substring(tp+7).toInt();
    // start
    int st=obj.indexOf("\"start\":");
    if(st>=0) tmp[i].start=(uint16_t)obj.substring(st+8).toInt();
    // foot
    int ft=obj.indexOf("\"foot\":");
    if(ft>=0) tmp[i].foot=(uint16_t)obj.substring(ft+7).toInt();
    // hasMove: terima boolean (true/false) maupun integer (0/1)
    int hm=obj.indexOf("\"hasMove\":");
    if(hm>=0){
      String hv=obj.substring(hm+10, hm+16);
      tmp[i].hasMove=(hv.indexOf("true")>=0 || hv.toInt()==1)?1:0;
    }
    pos=objEnd+1;
    parsed++;
  }
  if(parsed<count){ sendApiError(400,"parse_incomplete","Tidak semua fixture bisa di-parse"); return; }

  int errIdx; const char* errCode;
  if(!applyFixtures(tmp,(uint8_t)count,errIdx,errCode)){
    String msg=String("Validasi gagal: ")+(errCode?errCode:"unknown");
    if(errIdx>=0){ msg+=" (fixture #"; msg+=String(errIdx+1); msg+=")"; }
    sendApiError(409,"fixture_invalid",msg.c_str());
    return;
  }
  // Reload UI agar patch baru langsung terlihat
  sendApiOk();
}

// v48 fix: STREAMING UI. Versi v46 masih menyusun String halaman utuh
// (~54 KB utk HTML v48 + injeksi) — alokasi kontigu sebesar itu gagal saat
// heap terfragmentasi (WS aktif), sehingga guard 500 "gagal alokasi heap"
// muncul. Solusi definitive: kirim chunked langsung dari PROGMEM (flash
// ESP32 memory-mapped) — puncak heap hanya JSON kecil (≤2,3 KB/JSON),
// nol alokasi halaman. Token diganti on-the-fly; SEMUA kemunculan
// (per iterasi strstr) diganti, paritas perilaku String::replace() lama.
void sendUi(){
  String fx=fixJson(), gr=grpJson(), sc=scnJson(), ct=customTypesJson();
  String ip=activeIP().toString(), np=String(N_PRESETS);
  struct TokRep { const char* tok; const String* val; };
  TokRep toks[] = {
    {"__IP__",     &ip},
    {"__BUILD__",  nullptr},   // diisi bawah (BUILD_TAG literal)
    {"__FIXDATA__",&fx},
    {"__GRPDATA__",&gr},
    {"__SCNDATA__",&sc},
    {"__CTDATA__", &ct},
    {"__NP__",     &np},
  };
  String build=String(BUILD_TAG);
  toks[1].val=&build;
  const int NTOK=(int)(sizeof(toks)/sizeof(toks[0]));
  const char* html=INDEX_HTML;          // ESP32: flash memory-mapped, bisa dibaca
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  // Cegah cache browser: setelah firmware di-update, UI lama bisa bertahan
  // dan bentrok dengan endpoint baru (sumber bug "fitur hilang" yang aneh).
  server.sendHeader("Cache-Control","no-store, must-revalidate");
  server.send(200,"text/html","");
  const char* p=html;
  while(*p){
    // cari token paling awal dari posisi sekarang
    const char* best=nullptr; const String* bestVal=nullptr; int bestLen=0;
    for(int i=0;i<NTOK;i++){
      const char* f=strstr(p,toks[i].tok);
      if(f && (!best || f<best)){
        best=f; bestVal=toks[i].val; bestLen=(int)strlen(toks[i].tok);
      }
    }
    if(!best){
      server.sendContent_P(p);          // sisa HTML tanpa token
      break;
    }
    if(best>p) server.sendContent_P(p,(size_t)(best-p));  // potongan sebelum token
    server.sendContent(*bestVal);       // nilai pengganti (JSON kecil)
    p=best+bestLen;
  }
  server.sendContent("");               // akhir chunked
}

// ---------------------------------------------------------------
// WEB HANDLERS
// ---------------------------------------------------------------
void onSet(){
  if(server.args()==0){ sendUi(); return; }
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  for(int i=0;i<server.args();i++){
    String key=server.argName(i); int v=server.arg(i).toInt();
    if(key=="master"){ masterWant=cv(v); masterOut=masterWant; continue; }
    int us=key.indexOf('_');
    if(us>0){
      int fi=key.substring(0,us).toInt(); int c=key.substring(us+1).toInt();
      if(fi>=0&&fi<N_FIX&&c>=0&&c<fix[fi].foot){
        uint16_t ch=fix[fi].start+c;
        // v48: snap binary utk channel custom mode-switch (relay)
        manualWant[ch]=snapSwitchMode(fix[fi].type,(uint16_t)c,v);
        manualTouched[ch]=millis();
        recomputeWant();
        out[ch]=want[ch];
      }
    }
  }
  xSemaphoreGive(dmxMutex);
  stateRevision++;
  sendApiOk();
}
void onCtrl(){
  // fade/hold kini milik preset (di-set saat preset dimuat); /ctrl hanya master, strobe & all.
  if(server.hasArg("mast")){ masterWant=cv(server.arg("mast").toInt()); masterOut=masterWant; }
  if(server.hasArg("strb")){ strobeWant=cv(server.arg("strb").toInt()); }   // ephemeral: tidak persisten NVS
   if(server.hasArg("all")){
     bool on = server.arg("all")=="on";
     xSemaphoreTake(dmxMutex,portMAX_DELAY);
     uint32_t touched=millis();
     for(int f=0;f<N_FIX;f++){
       // Aman perangkat: "Penuh" hanya untuk PAR. Moving head (pan/tilt bisa
       // menyentak ke end-stop), fog (output penuh), dan strobe di-nol-kan.
       bool safe = (fix[f].type==FX_PAR);
       for(uint16_t c=0;c<fix[f].foot;c++){
         uint16_t ch=fix[f].start+c;
         manualWant[ch]=on ? (safe?255:0) : 0;
         manualTouched[ch]=touched;
       }
     }
     recomputeWant();
     for(int f=0;f<N_FIX;f++)
       for(uint16_t c=0;c<fix[f].foot;c++){
         uint16_t ch=fix[f].start+c;
         out[ch]=want[ch];
       }
     xSemaphoreGive(dmxMutex);
   }
  stateRevision++;
  if(server.hasArg("mast")||server.hasArg("all")) nvsDirty=true;
  sendApiOk();
}
void onChase(){
  chaseOn = server.hasArg("on");
  if(chaseOn) sceneOn=false;              // hanya satu sistem auto aktif
  if(!chaseOn) chaseIdx=-1; else chaseNextAt=millis();
  sendApiOk();
}
String buildStateJson(){
  // Snapshot cepat di bawah mutex, bangun JSON DI LUAR mutex ->
  // task DMX (Core0) tidak pernah menunggu lama gara-gara pembentukan String.
  // Dipakai oleh HTTP /cur (fallback) DAN WebSocket push (jalur utama).
  // v47: reserve — builder ini dipanggil wsBroadcastTick tiap detik (dan tiap
  // revision change); tanpa reserve, ±150 konkatenasi x frekuensi tinggi =
  // ratusan realloc heap/detik -> fragmentasi (akar masalah __SCNDATA__).
  static uint8_t snapOut[513];
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  memcpy(snapOut,out,sizeof(snapOut));
  uint8_t m=masterOut;
  int si=sceneIdx, st=sceneStep;
  bool so=sceneOn;
  xSemaphoreGive(dmxMutex);
  String j="{";
  j.reserve(2048);
  j+="\"build\":\""+String(BUILD_TAG)+"\",";
  j+="\"sceneRev\":"+String(sceneRev.load())+",";   // v46: client reload /scenes saat berubah
  j+="\"artnet\":\""+String(artnetMode?"network":"local")+"\",";   // v49: indikator mode
  // v49: deck fisik — nilai button/encoder terekspos utk website
  j+="\"hwBank\":"+String(hwBank)+",\"hwEnc\":"+String(hwEncCount)+",\"hwB\":["
    +String(hwBtnState[0])+","+String(hwBtnState[1])+","
    +String(hwBtnState[2])+","+String(hwBtnState[3])+"],";
  j+="\"master\":"+String(m)+",\"strb\":"+String((int)strobeWant)+",\"fade\":"+String(fadeMs)+",\"chase\":"+String(chaseMs)+",\"chaseOn\":"+(chaseOn?"true":"false")+",\"sceneOn\":"+(so?"true":"false")+",\"scenesp\":"+String(sceneMs)+",\"scn\":"+String(si)+",\"stp\":"+String(st)+",\"selectedPreset\":"+String(selectedPreset)+",\"selectedScene\":"+String(selectedScene)+",\"revision\":"+String(stateRevision.load())+",\"nvsDirty\":"+(nvsDirty?"true":"false")+",\"lastSaveOk\":"+(lastSaveOk?"true":"false")+",\"cur\":{";
  bool first=true;
  for(int f=0;f<N_FIX;f++)for(uint16_t c=0;c<fix[f].foot;c++){
    if(!first) j+=","; first=false;
    j+="\""+String(f)+"_"+String(c)+"\":"+String(snapOut[fix[f].start+c]);
  }
  j+="}}";
  return j;
}
void onCur(){ server.send(200,"application/json",buildStateJson()); }
void onPresets(){ server.send(200,"application/json",presetsJson()); }
void onPresetLoad(){
  int n=server.arg("n").toInt()-1;
  if(n<0||n>=N_PRESETS||!presets[n][0]){ sendApiError(404,"preset_missing","Preset belum tersedia"); return; }
  selectedPreset=n;
  applyPresetToWant(n);
  sendApiOk();
}
void onPresetSave(){
  int n=server.arg("n").toInt()-1;
  if(n<0||n>=N_PRESETS){ server.send(400,"text/plain","bad"); return; }
  bool idim=server.hasArg("idim")&&server.arg("idim")=="1";
  long f=server.arg("f").toInt(); if(f<0) f=0; if(f>2550) f=2550;
  long h=server.arg("h").toInt(); if(h<100) h=100; if(h>5000) h=5000;
  if(!capturePreset(n,idim,(uint16_t)f,(uint16_t)h)){
    // Slot bayangan penuh: scene lama masih memegang data ini, tolak rekaman
    // agar scene tidak rusak. Operator boleh hapus scene tak terpakai dulu.
    sendApiError(507,"shadow_full","Slot preset habis untuk melindungi scene; kosongkan scene/preset lain"); return;
  }
  selectedPreset=n;
  sendApiOk();
}
// ---------------------------------------------------------------
// v49: Art-Net mode — GET /artnet?mode=local|network (toggle operator)
// Ephemeral (bukan NVS): mode jaringan = keputusan sesi operator, bukan
// konfigurasi persisten (flash awet; konsisten strobe ephemeral).
// GET /artnet (tanpa arg) = status.
// ---------------------------------------------------------------
void onArtnet(){
  if(server.hasArg("mode")){
    bool wantNet = server.arg("mode")=="network";
    if(wantNet && !artnetMode) artnetBegin();
    if(!wantNet && artnetMode){
      artnetUdp.stop();
      Serial.println("Art-Net: stop (mode LOCAL)");
    }
    artnetMode = wantNet;
    stateRevision++;
    sendApiOk();
    return;
  }
  String j="{\"ok\":true,\"mode\":\""+String(artnetMode?"network":"local")+"\"";
  j+=",\"lastAt\":"+String(artnetLastAt);
  j+=",\"pkt\":"+String(artnetPktCount)+"}";
  server.send(200,"application/json",j);
}
// GET /psetfade?n=X&f=&h= -> ubah fade/hold preset X tanpa merekam ulang
void onPresetFade(){
  int n=server.arg("n").toInt()-1;
  if(n<0||n>=N_PRESETS||!presets[n][0]){ sendApiError(404,"preset_missing","Preset belum tersedia"); return; }
  long f=server.arg("f").toInt(); if(f<0) f=0; if(f>2550) f=2550;
  long h=server.arg("h").toInt(); if(h<100) h=100; if(h>5000) h=5000;
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  // COW: ubah fade/hold preset yang dirujuk scene (visible ATAU bayangan) ->
  // scene harus tetap pegang timing lama. Salin chunk lama ke slot bayangan
  // + alihkan referensi scene.
  if(presetSceneRefCount(n)>0 && cowShadowPreset(n)==COW_FULL){
    xSemaphoreGive(dmxMutex);
    sendApiError(507,"shadow_full","Slot preset habis untuk melindungi scene; kosongkan scene/preset lain");
    return;
  }
  presets[n][513]=(uint8_t)(f/10);
  presets[n][514]=(uint8_t)(h/20);
  xSemaphoreGive(dmxMutex);
  markStateChanged();
  persistAll();
  sendApiOk();
}
void onPresetClear(){
  int n=server.arg("n").toInt()-1;
  if(n<0||n>=N_PRESETS){ server.send(400,"text/plain","bad"); return; }
  // HAPUS = sembunyikan dari bank, data channel TETAP UTUH.
  // Scene yang merujuk preset ini tetap memainkan datanya; slot hanya
  // benar-benar diganti saat user merekam ulang (REKAM) di slot yang sama.
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  presets[n][0]=0;
  xSemaphoreGive(dmxMutex);
  if(selectedPreset==n) selectedPreset=-1;
  markStateChanged();
  persistAll(); sendApiOk();
}
void onExport(){
  server.sendHeader("Content-Disposition","attachment; filename=dmx-presets.json");
  server.send(200,"application/json",exportJson());
}

// Import: multipart upload -> kumpulkan ke buffer (berbatas) -> parse -> commit
#define IMPORT_MAX 49152            // batasi heap; export v44 biasanya <42KB
String importBuf; volatile bool importDone=false; volatile bool importTooBig=false;
void onImportFinal(){
  if(importTooBig)      server.send(413,"text/plain","File terlalu besar (maks 48KB)");
  else if(importDone)   server.send(200,"text/plain","Impor OK: preset dimuat & tersimpan");
  else                  server.send(400,"text/plain","Gagal parse file JSON (preset lama tetap utuh)");
}
void onImportUpload(){
  HTTPUpload& u = server.upload();
  if(u.status==UPLOAD_FILE_START){ importBuf=""; importDone=false; importTooBig=false; }
  else if(u.status==UPLOAD_FILE_WRITE){
    if(importTooBig) return;                       // buang sisa chunk
    if(importBuf.length() + u.currentSize > IMPORT_MAX){ importTooBig=true; importBuf=""; return; }
    importBuf.concat((char*)u.buf, u.currentSize);
  }
  else if(u.status==UPLOAD_FILE_END){
    if(importTooBig) return;
    importDone = importJson(importBuf);
  }
  else if(u.status==UPLOAD_FILE_ABORTED){          // upload dibatalkan klien
    importBuf=""; importDone=false; importTooBig=false;
  }
}

// ---------------------------------------------------------------
// DMX TASK (Core 0) - timing realtime, terpisah dari WiFi/Web (Core 1)
// ---------------------------------------------------------------
void dmxTask(void* arg){
  TickType_t lastWake = xTaskGetTickCount();
  for(;;){
    uint32_t now = millis();
    chaseTick(now);
    sceneTick(now);
    fadeTick(0.025f);
    buildFrame();
    dmxHeartbeat++;
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(25));   // ~40fps presisi
  }
}
TaskHandle_t dmxTaskHandle = NULL;

// ---------------------------------------------------------------
// SERIAL CONTROL (v33) — jalur kendali kedua selain Web UI
// ---------------------------------------------------------------
// Protokol teks per baris (bisa JSON opsional di app desktop nanti).
// Semua perintah menulis ke layer yang SAMA dengan Web UI:
//   SET/GRP/MAST/STRB/ALL -> manualWant[]  (layer manual, HTP/LTP)
//   PSL/PREC              -> pbWant[]       (layer playback)
// Tidak ada logika khusus serial; hanya menerjemahkan teks ke layer.
static String serialLine = "";

// --- Staging buffer untuk SERIAL IMPORT BATCH (v39) ---
static uint8_t  serImport[N_PRESETS][PRESET_CHUNK];
static bool     serImportActive=false;
static bool     serImportProvided[N_PRESETS]={false};

// Helper: ambil argumen integer ke-n dari string (split by space)
static int serArgInt(const String& args, int idx){
  String s=args; s.trim();   // trim() mengubah in-place: butuh salinan non-const
  int i=0,tok=0,len=s.length();
  while(i<len){
    int sp=s.indexOf(' ',i);
    if(sp==-1) sp=len;
    String t=s.substring(i,sp);
    t.trim();
    if(t.length()>0 && tok==idx) return t.toInt();
    tok++; i=sp+1;
  }
  return 0;
}

void handleSerialCmd(String cmd){
  cmd.trim();
  if(cmd.length()==0) return;
  String C=cmd; C.toUpperCase();

  // GET -> balas state JSON saat ini (untuk sinkron aplikasi desktop)
  if(C=="GET"){
    Serial.println(buildStateJson());
    return;
  }
  // SAVE -> paksa persist ke NVS (auto-save 60s tetap berlaku)
  if(C=="SAVE"){
    bool ok=persistAll();
    Serial.println(ok?"{\"ok\":true}":"{\"ok\":false}");
    return;
  }

  // ambil kata pertama sebagai perintah
  int sp=C.indexOf(' ');
  String op=(sp<0)?C:C.substring(0,sp);
  String args=(sp<0)?"":C.substring(sp+1);

  if(op=="MAST"){
    int v=args.toInt(); masterWant=cv(v); masterOut=masterWant;
    stateRevision++;
    Serial.println("{\"ok\":true}");
    return;
  }
  if(op=="STRB"){
    strobeWant=cv(args.toInt());
    stateRevision++;
    Serial.println("{\"ok\":true}");
    return;
  }
  if(op=="SET"){                      // SET <fi>_<ch>=<val>
    int eq=args.indexOf('=');
    if(eq>0){
      String key=args.substring(0,eq); int us=key.indexOf('_');
      int v=args.substring(eq+1).toInt();
      if(us>0){
        int fi=key.substring(0,us).toInt(); int c=key.substring(us+1).toInt();
        if(fi>=0&&fi<N_FIX&&c>=0&&c<fix[fi].foot){
          xSemaphoreTake(dmxMutex,portMAX_DELAY);
          uint16_t ch=fix[fi].start+c;
          // v48: snap binary utk channel custom mode-switch (relay)
          manualWant[ch]=snapSwitchMode(fix[fi].type,(uint16_t)c,v);
          manualTouched[ch]=millis();
          recomputeWant();
          out[ch]=want[ch];                    // snap: fader manual terasa langsung
          xSemaphoreGive(dmxMutex);
          stateRevision++;
          Serial.println("{\"ok\":true}");
          return;
        }
      }
    }
    Serial.println("{\"ok\":false,\"err\":\"SET <fi>_<ch>=<val>\"}");
    return;
  }
  if(op=="PSL"){                      // playback preset -> layer pbWant
    int n=args.toInt()-1;
    if(n>=0&&n<N_PRESETS&&presets[n][0]){
      selectedPreset=n;
      applyPresetToWant(n);            // mutex+recompute di dalamnya
      stateRevision++;
      Serial.println("{\"ok\":true}");
    } else Serial.println("{\"ok\":false,\"err\":\"preset kosong\"}");
    return;
  }
  if(op=="SPLAY"){                    // mulai scene
    int s=args.toInt()-1;
    if(s>=0&&s<N_SCENES){
      sceneOn=true; sceneIdx=s; sceneStep=-1; sceneError=0;
      sceneNextAt=millis();
      stateRevision++;
      Serial.println("{\"ok\":true}");
    } else Serial.println("{\"ok\":false,\"err\":\"scene invalid\"}");
    return;
  }
   if(op=="SSTOP"){
     sceneOn=false; sceneIdx=-1; sceneStep=-1;
     stateRevision++;
     Serial.println("{\"ok\":true}");
     return;
   }

   // --- Parity commands (v35) untuk desktop .exe ---

   // LISTP -> daftar preset JSON (untuk populate panel)
   if(op=="LISTP"){ Serial.println(presetsJson()); return; }
   // LISTS -> daftar scene JSON
   if(op=="LISTS"){ Serial.println(scnJson()); return; }

   // GRP <i> <v> -> fader grup
   if(op=="GRP"){
     int i=serArgInt(args,0); int v=serArgInt(args,1);
      if(i>=0&&i<N_GROUPS&&v>=0&&v<=255){
        xSemaphoreTake(dmxMutex,portMAX_DELAY);
        for(int f=0;f<N_FIX;f++){
          if(fix[f].type!=grp[i].typeFilter || grp[i].offset>=fix[f].foot) continue;
          uint16_t ch=fix[f].start+grp[i].offset;
          // v48: snap binary per-fixture utk channel custom mode-switch
          manualWant[ch]=snapSwitchMode(fix[f].type,grp[i].offset,v);
          manualTouched[ch]=millis();
        }
       recomputeWant();
       for(int f=0;f<N_FIX;f++){              // snap agar grup terasa langsung
         if(fix[f].type!=grp[i].typeFilter || grp[i].offset>=fix[f].foot) continue;
         uint16_t ch=fix[f].start+grp[i].offset;
         out[ch]=want[ch];
       }
       xSemaphoreGive(dmxMutex);
       stateRevision++; nvsDirty=true;
       Serial.println("{\"ok\":true}");
     } else Serial.println("{\"ok\":false,\"err\":\"GRP <i> <v>\"}");
     return;
   }

   // REC <n> <idim> <f_ms> <h_ms> -> rekam preset n (fade/hold dalam ms, sama seperti web)
   if(op=="REC"){
     int n=serArgInt(args,0)-1; bool idim=serArgInt(args,1)==1;
     long f=serArgInt(args,2); if(f<0)f=0; if(f>2550)f=2550;
     long h=serArgInt(args,3); if(h<100)h=100; if(h>5000)h=5000;
      if(n>=0&&n<N_PRESETS){
        if(!capturePreset(n,idim,(uint16_t)f,(uint16_t)h)){
          Serial.println("{\"ok\":false,\"err\":\"shadow_full\"}");
        } else {
          selectedPreset=n;
          Serial.println("{\"ok\":true}");
        }
      } else Serial.println("{\"ok\":false,\"err\":\"REC <n>\"}");
     return;
   }

   // PFH <n> <f_ms> <h_ms> -> ubah fade/hold preset n tanpa mengubah data
   if(op=="PFH"){
     int n=serArgInt(args,0)-1;
     long f=serArgInt(args,1); if(f<0)f=0; if(f>2550)f=2550;
     long h=serArgInt(args,2); if(h<100)h=100; if(h>5000)h=5000;
      if(n>=0&&n<N_PRESETS&&presets[n][0]){
         xSemaphoreTake(dmxMutex,portMAX_DELAY);
         // COW: proteksi timing lama milik scene (sama seperti /psetfade);
         // berlaku juga untuk slot bayangan yang dirujuk scene.
         if(presetSceneRefCount(n)>0 && cowShadowPreset(n)==COW_FULL){
          xSemaphoreGive(dmxMutex);
          Serial.println("{\"ok\":false,\"err\":\"shadow_full\"}");
          return;
        }
        presets[n][513]=(uint8_t)constrain(f/10,0,255); presets[n][514]=(uint8_t)constrain(h/20,5,250);
        xSemaphoreGive(dmxMutex);
        persistAll(); stateRevision++;
        Serial.println("{\"ok\":true}");
      } else Serial.println("{\"ok\":false,\"err\":\"PFH <n> <f_ms> <h_ms>\"}");
     return;
   }

   // PDEL <n> -> sembunyikan preset (used=0)
   if(op=="PDEL"){
     int n=serArgInt(args,0)-1;
     if(n>=0&&n<N_PRESETS){
       xSemaphoreTake(dmxMutex,portMAX_DELAY);
       presets[n][0]=0;
       if(selectedPreset==n) selectedPreset=-1;
       xSemaphoreGive(dmxMutex);
       persistAll(); stateRevision++;
       Serial.println("{\"ok\":true}");
     } else Serial.println("{\"ok\":false,\"err\":\"PDEL <n>\"}");
     return;
   }

   // SPUSH <s> <p> -> tambahkan step ke scene s dengan preset p
   if(op=="SPUSH"){
     int s=serArgInt(args,0)-1, p=serArgInt(args,1);
     if(s>=0&&s<N_SCENES&&p>=1&&p<=N_PRESETS){
       xSemaphoreTake(dmxMutex,portMAX_DELAY);
       int slot=-1,lastFilled=-1;
       for(int k=0;k<SCENE_STEPS;k++){
         if(scenes[s][k]!=0) lastFilled=k;
         else if(slot<0) slot=k;
       }
       if(lastFilled>=0 && scenes[s][lastFilled]==(uint8_t)p){
         xSemaphoreGive(dmxMutex);
         Serial.println("{\"ok\":false,\"err\":\"scene_duplicate\"}");
         return;
       }
        if(slot>=0){ scenes[s][slot]=(uint8_t)p; sceneRev++; }
        xSemaphoreGive(dmxMutex);
        if(slot<0){ Serial.println("{\"ok\":false,\"err\":\"scene_full\"}"); return; }
       persistAll(); stateRevision++;
       Serial.println("{\"ok\":true}");
     } else Serial.println("{\"ok\":false,\"err\":\"SPUSH <s> <p>\"}");
     return;
   }

   // SPOP <s> -> hapus langkah terakhir scene s
   if(op=="SPOP"){
     int s=serArgInt(args,0)-1;
     if(s>=0&&s<N_SCENES){
       xSemaphoreTake(dmxMutex,portMAX_DELAY);
        for(int k=SCENE_STEPS-1;k>=0;k--){ if(scenes[s][k]!=0){ scenes[s][k]=0; sceneRev++; break; } }
        xSemaphoreGive(dmxMutex);
        persistAll(); stateRevision++;
        Serial.println("{\"ok\":true}");
      } else Serial.println("{\"ok\":false,\"err\":\"SPOP <s>\"}");
     return;
   }

   // SCLR <s> -> kosongkan scene s
   if(op=="SCLR"){
     int s=serArgInt(args,0)-1;
     if(s>=0&&s<N_SCENES){
        xSemaphoreTake(dmxMutex,portMAX_DELAY);
        memset(scenes[s],0,sizeof(scenes[s]));
        sceneRev++;
        xSemaphoreGive(dmxMutex);
        persistAll(); stateRevision++;
        Serial.println("{\"ok\":true}");
      } else Serial.println("{\"ok\":false,\"err\":\"SCLR <s>\"}");
     return;
   }

   // SELP <n> / SELS <s> -> select tanpa apply
   if(op=="SELP"){
     int n=serArgInt(args,0)-1;
     if(n>=0&&n<N_PRESETS&&presets[n][0]) selectedPreset=n;
     stateRevision++; nvsDirty=true;
     Serial.println("{\"ok\":true}");
     return;
   }
   if(op=="SELS"){
     int s=serArgInt(args,0)-1;
     if(s>=0&&s<N_SCENES) selectedScene=s;
     stateRevision++; nvsDirty=true;
     Serial.println("{\"ok\":true}");
     return;
   }

    // v49: ARTNET local|network -> mode input Art-Net
    if(op=="ARTNET"){
      bool net = args.indexOf("network",0)>=0 || args.indexOf("on",0)>=0;
      if(net && !artnetMode) artnetBegin();
      if(!net && artnetMode){ artnetUdp.stop(); Serial.println("Art-Net: stop (mode LOCAL)"); }
      artnetMode = net;
      stateRevision++;
      Serial.println(String("{\"ok\":true,\"mode\":\"")+(net?"network":"local")+"\"}");
      return;
    }
    if(op=="ARTSTAT"){
      Serial.println(String("{\"mode\":\"")+(artnetMode?"network":"local")
        +"\",\"lastAt\":"+String(artnetLastAt)
        +",\"pkt\":"+String(artnetPktCount)+"}");
      return;
    }

    // CHASE on/off -> toggle chase
    if(op=="CHASE"){
     chaseOn = args.indexOf("on",0)>=0;
     if(chaseOn){ sceneOn=false; sceneIdx=-1; sceneStep=-1; chaseNextAt=millis(); }
     else { chaseOn=false; chaseIdx=-1; }
     stateRevision++;
     Serial.println("{\"ok\":true}");
     return;
   }

   // ALL on/off -> paritas /ctrl?all= (Blackout / PAR Full aman perangkat)
   if(op=="ALL"){
     bool on = args.indexOf("on")>=0;
     xSemaphoreTake(dmxMutex,portMAX_DELAY);
     uint32_t touched=millis();
     for(int f=0;f<N_FIX;f++){
       bool safe = (fix[f].type==FX_PAR);   // hanya PAR yg boleh "full"
       for(uint16_t c=0;c<fix[f].foot;c++){
         uint16_t ch=fix[f].start+c;
         manualWant[ch]=on ? (safe?255:0) : 0;
         manualTouched[ch]=touched;
       }
     }
     recomputeWant();
     for(int f=0;f<N_FIX;f++)for(uint16_t c=0;c<fix[f].foot;c++){
       uint16_t ch=fix[f].start+c; out[ch]=want[ch];   // snap langsung
     }
     xSemaphoreGive(dmxMutex);
     stateRevision++;
     Serial.println("{\"ok\":true}");
     return;
   }

   // LOAD -> muat ulang dari NVS (restore snapshot)
   if(op=="LOAD"){
     bool ok=loadData();
     stateRevision++;
     Serial.println(ok?"{\"ok\":true}":"{\"ok\":false}");
     return;
   }

    // LISTF -> daftar fixture JSON (untuk render mixer desktop tanpa hardcode)
    if(op=="LISTF"){ Serial.println(fixJson()); return; }
    // LISTG -> daftar grup fader JSON
    if(op=="LISTG"){ Serial.println(grpJson()); return; }
    if(op=="LISTCT"){ Serial.println(customTypesJson()); return; }   // v48

    // v48: CTSET <json> -> commit custom type (paritas POST /ctypes)
    // Format body sama: {"types":[{"slot":5,...}]} — JSON asli (case-sens).
    if(op=="CTSET"){
      String body=cmd;
      int b0=body.indexOf(' ');
      body=(b0<0)?String(""):body.substring(b0+1);
      body.trim();
      if(body.length()==0 || !commitCustomTypes(body)){
        Serial.println("{\"ok\":false,\"err\":\"CTSET <json>\"}");
      } else {
        Serial.println("{\"ok\":true}");
      }
      return;
    }

    // v45: FIXSET <json> -> terapkan konfigurasi fixture baru (paritas POST /fixes)
    // Format JSON sama dengan body POST /fixes: {"count":N,"fixtures":[...]}
    // PENTING: body diambil dari `cmd` ASLI (case-sensitive), bukan `args`
    // yang sudah di-toUpperCase() — field JSON seperti "name"/"type" harus
    // tetap lowercase agar bisa di-parse.
    if(op=="FIXSET"){
      String body=cmd;                     // cmd asli, belum uppercase
      int b0=body.indexOf(' ');            // lompat kata "FIXSET"
      body=(b0<0)?String(""):body.substring(b0+1);
      body.trim();
      if(body.length()==0){ Serial.println("{\"ok\":false,\"err\":\"FIXSET <json>\"}"); return; }
      // Parse count
      int ci=body.indexOf("\"count\":");
      if(ci<0){ Serial.println("{\"ok\":false,\"err\":\"missing count\"}"); return; }
      int count=body.substring(ci+8).toInt();
      if(count<1||count>MAX_FIX){ Serial.println("{\"ok\":false,\"err\":\"count 1-32\"}"); return; }
      Fixture tmp[MAX_FIX]; memset(tmp,0,sizeof(tmp));
      int pos=body.indexOf("\"fixtures\":[");
      if(pos<0){ Serial.println("{\"ok\":false,\"err\":\"missing fixtures\"}"); return; }
      pos+=12; int parsed=0;
      for(int i=0;i<count;i++){
        int os=body.indexOf('{',pos); if(os<0) break;
        int oe=body.indexOf('}',os); if(oe<0) break;
        String obj=body.substring(os,oe+1);
        int nm=obj.indexOf("\"name\":\"");
        if(nm>=0){ int ns=nm+8,ne=obj.indexOf('"',ns); if(ne>ns) obj.substring(ns,min(ne,ns+24)).toCharArray(tmp[i].name,25); }
        int tp=obj.indexOf("\"type\":");  if(tp>=0) tmp[i].type=(uint8_t)obj.substring(tp+7).toInt();
        int st=obj.indexOf("\"start\":"); if(st>=0) tmp[i].start=(uint16_t)obj.substring(st+8).toInt();
        int ft=obj.indexOf("\"foot\":");  if(ft>=0) tmp[i].foot=(uint16_t)obj.substring(ft+7).toInt();
        int hm=obj.indexOf("\"hasMove\":");
        if(hm>=0){ String hv=obj.substring(hm+10,hm+16); tmp[i].hasMove=(hv.indexOf("true")>=0||hv.toInt()==1)?1:0; }
        pos=oe+1; parsed++;
      }
      if(parsed<count){ Serial.println("{\"ok\":false,\"err\":\"parse incomplete\"}"); return; }
      int errIdx; const char* errCode;
      if(!applyFixtures(tmp,(uint8_t)count,errIdx,errCode)){
        Serial.println(String("{\"ok\":false,\"err\":\"")+String(errCode?errCode:"invalid")+String("\"}"));
        return;
      }
      Serial.println("{\"ok\":true}");
      return;
    }
    // EXPORT -> dump lengkap preset (paritas /export web, utk backup desktop)
    if(op=="EXPORT"){ Serial.println(exportJson()); return; }

    // WIFIST -> status koneksi WiFi (paritas /wifistat web)
    if(op=="WIFIST"){
      bool sta=(WiFi.getMode() & WIFI_STA) && WiFi.status()==WL_CONNECTED;
      String j="{\"ok\":true,\"connected\":"; j+= sta?"true":"false";
      j+=",\"ssid\":\""; j+= sta?WiFi.SSID():String(effSsid()); j+="\"";
      j+=",\"custom\":"; j+= customSsid.length()>0?"true":"false";
      j+=",\"pending\":"; j+= wifiPending?"true":"false";
      j+=",\"ip\":\""; j+= activeIP().toString(); j+="\"";
      j+=",\"rssi\":"; j+= String(sta?WiFi.RSSI():0);
      j+=",\"apActive\":"; j+= (WiFi.getMode() & WIFI_AP)?"true":"false";
      j+="}";
      Serial.println(j);
      return;
    }
    // WIFIS <ssid> <pass> -> simpan kredensial kustom + reconnect (SSID tanpa spasi)
    if(op=="WIFIS"){
      args.trim();
      int sp=args.indexOf(' ');
      String ssid = (sp<0) ? args : args.substring(0,sp);
      String pass = (sp<0) ? String("") : args.substring(sp+1);
      ssid.trim(); pass.trim();
      if(ssid.length()==0 || ssid.length()>32){ Serial.println("{\"ok\":false,\"err\":\"SSID kosong/panjang\"}"); return; }
      wifiNvs.begin("dmxwifi",false);
      bool ok = wifiNvs.putString("ssid",ssid)>0 && wifiNvs.putString("pass",pass)>0;
      wifiNvs.end();
      if(!ok){ Serial.println("{\"ok\":false,\"err\":\"gagal simpan NVS\"}"); return; }
      customSsid=ssid; customPass=pass;
      wifiPending=true; wifiTryAt=millis(); wifiTryCount=0;
      WiFi.disconnect();
      Serial.println("{\"ok\":true}");
      return;
    }

   // --- IMPORT BATCH via serial (v39, paritas /import web) ---
   // Alur: IMPORT_BEGIN -> (IMPORT_P + IMPORT_C per preset) -> IMPORT_END
   if(op=="IMPORT_BEGIN"){
     memset(serImport,0,sizeof(serImport));
     memset(serImportProvided,0,sizeof(serImportProvided));
     serImportActive=true;
     Serial.println("{\"ok\":true}");
     return;
   }
   if(op=="IMPORT_P"){            // IMPORT_P <n> <u> <f_ms> <h_ms>
     if(!serImportActive){ Serial.println("{\"ok\":false,\"err\":\"IMPORT_BEGIN dulu\"}"); return; }
     int n=serArgInt(args,0); int u=serArgInt(args,1);
     long f=serArgInt(args,2); if(f<0)f=0; if(f>2550)f=2550;
     long h=serArgInt(args,3); if(h<100)h=100; if(h>5000)h=5000;
     if(n>=1&&n<=N_PRESETS){
       uint8_t* row=serImport[n-1];
       row[0]=u?1:0;
       row[513]=(uint8_t)constrain(f/10,0,255);
       row[514]=(uint8_t)constrain(h/20,5,250);
       serImportProvided[n-1]=true;
       Serial.println("{\"ok\":true}");
     } else Serial.println("{\"ok\":false,\"err\":\"n 1-16\"}");
     return;
   }
   if(op=="IMPORT_C"){            // IMPORT_C <n> <off> v1,v2,... (maks 64 nilai)
     if(!serImportActive){ Serial.println("{\"ok\":false,\"err\":\"IMPORT_BEGIN dulu\"}"); return; }
     int n=serArgInt(args,0); int off=serArgInt(args,1);
     if(n<1||n>N_PRESETS||off<0||off>=512){ Serial.println("{\"ok\":false,\"err\":\"IMPORT_C <n> <off> ...\"}"); return; }
     int p1=args.indexOf(' '); int p2=(p1>=0)?args.indexOf(' ',p1+1):-1;
     if(p2<0){ Serial.println("{\"ok\":false,\"err\":\"tanpa nilai\"}"); return; }
     String vals=args.substring(p2+1);
     uint8_t* row=serImport[n-1];
      int idx=off, start=0, count=0;
      while(start<=(int)vals.length() && idx<512){
        int comma=vals.indexOf(',',start);
        String t=(comma<0)?vals.substring(start):vals.substring(start,comma);
        t.trim();
        if(t.length()>0){
          if(count>=64){ Serial.println("{\"ok\":false,\"err\":\"maks 64 nilai\"}"); return; }
          int value=t.toInt();
          row[1+idx]=(uint8_t)constrain(value,0,255); idx++; count++;
        }
        if(comma<0) break;
        start=comma+1;
      }
      Serial.println("{\"ok\":true}");
     return;
   }
   if(op=="IMPORT_END"){
     if(!serImportActive){ Serial.println("{\"ok\":false,\"err\":\"IMPORT_BEGIN dulu\"}"); return; }
     int found=0;
     for(int i=0;i<N_PRESETS;i++) if(serImportProvided[i]) found++;
     if(found==0){ Serial.println("{\"ok\":false,\"err\":\"tidak ada preset\"}"); return; }
     // Commit: timpa HANYA baris yang dikirim (sisanya tidak disentuh),
     // sama persis dengan semantik importJson() di web.
     xSemaphoreTake(dmxMutex,portMAX_DELAY);
     for(int i=0;i<N_PRESETS;i++)
       if(serImportProvided[i]) memcpy(presets[i],serImport[i],PRESET_CHUNK);
     xSemaphoreGive(dmxMutex);
     serImportActive=false;
     markStateChanged();
     persistAll();
     Serial.println("{\"ok\":true}");
     return;
   }

   // --- End parity commands ---

   Serial.println("{\"ok\":false,\"err\":\"unknown cmd\"}");
}

// Parsing serial non-blocking: kumpulkan karakter sampai '\n', lalu proses.
// Dipanggil dari loop() (Core 1) -> tidak pernah memblok task DMX (Core 0).
void processSerialIn(){
  while(Serial.available()){
    char c=(char)Serial.read();
    if(c=='\n'){
      if(serialLine.length()>0) handleSerialCmd(serialLine);
      serialLine="";
    } else if(c!='\r' && serialLine.length()<4096){   // v45: buffer diperbesar utk FIXSET JSON (32 fixture)
      serialLine+=c;
    }
  }
}

// ---------------------------------------------------------------
// SETUP / LOOP
// ---------------------------------------------------------------
void setup(){
  Serial.begin(115200); delay(300);
  bootAtMs = millis();
  Serial.println(); Serial.println("=== DMX Web Console " BUILD_TAG " ===");

  dmxMutex = xSemaphoreCreateMutex();
  if(!dmxMutex){
    Serial.println("FATAL: gagal membuat mutex DMX");
    while(true) delay(1000);
  }

  // DMX UART + break sesuai standar (88-176us; set 100us)
  DMX.configure();
  DMX.setBreakLength(100);

  hwInputBegin();          // v49: tombol scene fisik + rotary encoder
  memset(want,0,sizeof(want)); memset(out,0,sizeof(out));
  masterWant=255; masterOut=255;
  // v45: muat konfigurasi fixture dari NVS; fallback ke default bila belum ada
  if(!loadCustomTypes()){
    memset(customSlots,0,sizeof(customSlots));   // v48: custom type kosong = normal
  }
  if(!loadFixtures()){
    loadDefaultFixtures();
    Serial.println("Fixture: pakai patch default (18 fixture)");
  } else {
    Serial.print("Fixture: dimuat dari NVS, "); Serial.print(N_FIX); Serial.println(" fixture aktif");
  }
  loadAll();

  // v47: URUTAN KONEKSI DIBALIK per permintaan operator:
  //   1. Ethernet W5500 (prioritas utama — kabel, latensi rendah)
  //   2. WiFi STA (cadangan)
  //   3. AP darurat (hanya bila keduanya gagal)
  // STAGED BOOT DELAY: jeda bertahap sebelum tiap tahap berat (SPI/radio)
  // agar PSU marginal tidak kena lonjakan arus sekaligus (pelajaran brownout:
  // boot + radio TX + NVS bersamaan = drop tegangan). Tiap tahap dipisah
  // 1 dtk; total boot bertambah ~2 dtk — murah untuk stabilitas daya.

  // ---- TAHAP 1: Ethernet W5500 ----
  delay(1000);   // v47: rail 3V3 stabil dulu setelah loadAll() baca NVS
  SPI.begin(18,19,23);                    // VSPI: SCLK=18, MISO=19, MOSI=23
  // API core esp32 3.x untuk SPI PHY: begin(tipe, phy_addr, cs, irq, rst, SPI)
  // ETH_PHY_ADDR_AUTO = deteksi alamat PHY W5500 otomatis. IRQ/RST = -1 sah
  // di core ini (ETH_SPI_SUPPORTS_NO_IRQ), jadi tidak perlu kabel INT.
  ETH.begin(ETH_PHY_W5500, ETH_PHY_ADDR_AUTO, ETH_CS, ETH_IRQ, ETH_RST, SPI);
  Serial.println("Ethernet W5500: inisialisasi... (prioritas 1)");

  // Tunggu Ethernet dapat link (maks 5 detik)
  uint32_t ethStart=millis();
  while(!ETH.linkUp() && millis()-ethStart<5000){ delay(100); }
  bool ethHasIP=false;
  if(ETH.linkUp()){
    // link aktif; tunggu DHCP/IP sampai 3 detik lagi
    uint32_t ipStart=millis();
    while(ETH.localIP()==IPAddress(0,0,0,0) && millis()-ipStart<3000){ delay(100); }
    if(ETH.localIP()!=IPAddress(0,0,0,0)){
      ethHasIP=true;
      Serial.print("Ethernet tersambung. IP: http://");
      Serial.println(ETH.localIP());
    } else {
      Serial.println("Ethernet link aktif tapi belum dapat IP (DHCP).");
    }
  } else {
    Serial.println("Ethernet: tidak terdeteksi link (kabel tidak dicolok?).");
  }

  // ---- TAHAP 2: WiFi STA ----
  delay(1000);   // v47: jeda sebelum radio WiFi on — hindari spike arus TX
                 // bertepatan dengan init SPI yang barusan selesai
  loadWifiCreds();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);            // respons web lebih responsif
  WiFi.begin(effSsid(), effPass());
  Serial.print("WiFi: menyambung ke "); Serial.print(effSsid());
  if(customSsid.length()>0) Serial.print(" (kustom)");
  uint32_t t0=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-t0<15000){
    delay(250); Serial.print(".");
  }
  Serial.println();

  if(WiFi.status()==WL_CONNECTED){
    Serial.print("WiFi tersambung. IP: http://");
    Serial.println(WiFi.localIP());
  } else if(ethHasIP){
    // Ethernet sudah jadi jalur utama; WiFi gagal bukan masalah.
    Serial.println("WiFi gagal, tapi Ethernet aktif -- pakai Ethernet saja.");
  } else {
    // ---- TAHAP 3: AP darurat (hanya bila Ethernet & WiFi keduanya gagal) ----
    Serial.println("WiFi gagal & Ethernet tidak ada. Fallback ke AP darurat.");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.print("AP: "); Serial.print(AP_SSID);
    Serial.print(" | Buka browser: http://"); Serial.println(WiFi.softAPIP());
  }

  server.on("/",      HTTP_GET, sendUi);
  server.on("/set",   HTTP_GET, onSet);
  server.on("/grp",   HTTP_GET, onGroup);
   server.on("/scenes",HTTP_GET, onScenesGet);
   server.on("/fixes", HTTP_GET, onFixesGet);    // v40: metadata fixture (paritas LISTF serial)
   server.on("/fixes", HTTP_POST, onFixesPost);  // v45: update konfigurasi fixture
   server.on("/groups",HTTP_GET, onGroupsGet);   // v40: metadata grup fader (paritas LISTG serial)
  server.on("/ctypes", HTTP_GET,  onCtypesGet); // v48: daftar custom type
  server.on("/ctypes", HTTP_POST, onCtypesPost);// v48: commit custom type
  server.on("/artnet", HTTP_GET,  onArtnet);   // v49: mode Art-Net input
  server.on("/wifistat",HTTP_GET, onWifiStat);   // v43: status koneksi WiFi
  server.on("/wifiset", HTTP_POST, onWifiSet);   // v43: simpan kredensial + reconnect
  server.on("/wifiset", HTTP_GET, onWifiSet);    // kompatibilitas desktop v44
  server.on("/spush", HTTP_GET, onSPush);
  server.on("/spop",  HTTP_GET, onSPop);
  server.on("/sclear",HTTP_GET, onSClear);
  server.on("/splay", HTTP_GET, onSPlay);
  server.on("/ctrl",  HTTP_GET, onCtrl);
  server.on("/chase", HTTP_GET, onChase);
  server.on("/cur",   HTTP_GET, onCur);
  server.on("/select",HTTP_GET, onSelect);
  server.on("/save",  HTTP_POST, onSaveData);
  server.on("/loaddata",HTTP_GET, onLoadData);
  server.on("/health",HTTP_GET, onHealth);
  server.on("/presets",HTTP_GET,onPresets);
  server.on("/pload", HTTP_GET, onPresetLoad);
  server.on("/psave", HTTP_GET, onPresetSave);
  server.on("/psetfade", HTTP_GET, onPresetFade);
  server.on("/pclear",HTTP_GET, onPresetClear);
  server.on("/export",HTTP_GET, onExport);
  server.on("/import",HTTP_POST, onImportFinal, onImportUpload);
  server.begin();

  // WebSocket realtime push di port terpisah (81): kontrol tetap HTTP port 80.
  ws.onEvent(onWsEvent);
  wsSrv.addHandler(&ws);
  wsSrv.begin();
  Serial.println("WebSocket push -> port 81 (/ws)");

  // Kirim satu frame awal SEBELUM task DMX berjalan.
  // (Dulu dipanggil SETELAH task dibuat -> dua transmit paralel, frame boot bisa rusak.)
  buildFrame();

  // DMX timing di Core 0 (PRO_CPU), WebServer tetap di Core 1 berkat loop().
  xTaskCreatePinnedToCore(dmxTask, "dmx", 8192, NULL, 5, &dmxTaskHandle, 0);
  Serial.println("DMX task -> Core 0 | WebServer -> Core 1");
}
// Broadcast state via WebSocket: segera saat stateRevision berubah,
// heartbeat 1 dtk saat diam (paritas dgn polling lama utk scn/stp playback).
// v47: THROTTLE 100 ms — slider drag memicu puluhan revision++/dtk; tanpa
// throttle itu = puluhan broadcast JSON ~2 KB/dtk ke semua client (heap
// churn + bandwidth). 10 Hz lebih dari cukup utk fader terasa realtime.
void wsBroadcastTick(){
  static uint32_t lastAt=0, lastRev=0;
  uint32_t now=millis();
  uint32_t rev=stateRevision.load();          // v47: atomic -> snapshot sekali
  bool changed = rev!=lastRev;
  if(!changed && now-lastAt<1000) return;          // diam: heartbeat 1 dtk
  if(changed && now-lastAt<100) return;            // perubahan cepat: max 10 Hz
  if(ws.count()==0){ lastAt=now; return; }   // tanpa klien -> hemat CPU; lastRev sengaja tidak diupdate
  lastRev=rev; lastAt=now;
  ws.textAll(buildStateJson());
}
void loop(){
  server.handleClient();
  ws.cleanupClients();      // housekeeping AsyncWebSocket
  wsBroadcastTick();
  wifiReconnectTick();      // v43: reconnect WiFi kustom (non-blocking)
  artnetTask();             // v49: Art-Net input (mode NETWORK saja)
  hwInputTask();            // v49: tombol fisik + encoder (polling 2ms efek)
  processSerialIn();        // v33: kendali via serial (non-blocking, Core 1)
  // v46: migrasi gen v45->v46 dijalankan SETELAH 10 dtk stabil (radio WiFi
  // penuh daya, boot selesai) — bukan saat boot, untuk menghindari brownout.
  if(pendingGenMigration && millis()-bootAtMs > 10000){
    pendingGenMigration = false;
    Serial.println("NVS: migrasi gen (commit marker)...");
    if(persistAll()) Serial.println("NVS: migrasi selesai");
    else Serial.println("NVS: migrasi GAGAL (persistAll) — coba lagi nanti");
    if(!lastSaveOk) pendingGenMigration = true;   // retry siklus berikutnya
  }
  // AUTO-SAVE NVS (sisi server, safety-net): 60 detik setelah simpan terakhir,
  // bila masih ada perubahan (nvsDirty) -> persistAll(). Menutup kasus browser
  // ditutup sebelum timer client 60 s sempat mengirim /save.
  if(nvsDirty && (millis()-lastSaveAt)>=60000) persistAll();
}