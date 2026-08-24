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

// Tag build: tampil di header UI & Serial. Kalau tag lama masih tampil di
// browser setelah upload -> berarti cache/upload bermasalah, bukan kodenya.
#define BUILD_TAG "v29"

// ---------------------------------------------------------------
// WIFI - Station (konek ke router), fallback AP darurat
// ---------------------------------------------------------------
const char* WIFI_SSID = "SIGMA";
const char* WIFI_PASS = "1ngantos12";

// AP darurat bila WiFi SIGMA gagal tersambung (supaya tidak terkunci dari device)
const char* AP_SSID = "DMX-RGB";
const char* AP_PASS = "12345678";

// IP aktif sesuai mode (STA atau AP fallback)
IPAddress activeIP(){
  if((WiFi.getMode() & WIFI_STA) && WiFi.status()==WL_CONNECTED) return WiFi.localIP();
  return WiFi.softAPIP();
}

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
  const char* name;
  uint8_t type;
  uint16_t start;
  uint16_t foot;
  uint8_t  hasMove;      // 1 = fixture bergerak (pan/tilt di ch0 & ch2)
};

#define N_FIX 18
Fixture fix[N_FIX] = {
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

// ---------------------------------------------------------------
// PRESET & STATE
// ---------------------------------------------------------------
#define N_PRESETS 16
// chunk: [0]=used, [1..512]=nilai channel, [513]=fade/10ms, [514]=hold/20ms
#define PRESET_CHUNK 515

static uint8_t want[513];         // target (dari slider/preset/chase)
static uint8_t out[513];          // nilai tampilan (hasil fade)
static volatile uint8_t masterOut = 255; static volatile uint8_t masterWant = 255;
static volatile uint32_t fadeMs = 600;
static volatile bool chaseOn = false; static volatile uint32_t chaseMs = 1500; static volatile int chaseIdx = -1;

static uint8_t presets[N_PRESETS][PRESET_CHUNK];   // chunk: [0]=used, [1..512]=nilai

// Blackout-on-move: batas waktu (ms) saat dimmer fixture dipaksa 0 karena
// pan/tilt bergerak jauh (LTP). Diisi saat apply preset.
static volatile uint32_t blackoutEnd[N_FIX] = {0};

// ---------------------------------------------------------------
// SCENE: rangkaian hingga 30 langkah preset (referensi nomor, bukan salinan)
// 0 = langkah kosong; 1..N_PRESETS = nomor preset. Disimpan terpisah di NVS.
// ---------------------------------------------------------------
#define N_SCENES 20
#define SCENE_STEPS 30
static uint8_t scenes[N_SCENES][SCENE_STEPS];
static volatile bool sceneOn = false;
static volatile int sceneIdx = -1;     // scene yang sedang diputar
static volatile int sceneStep = -1;    // posisi langkah terakhir
static volatile uint32_t sceneMs = 1500;

// State UI yang authoritative di ESP32, bukan hanya di browser.
static volatile int selectedPreset = -1;
static volatile int selectedScene = -1;
static volatile uint32_t stateRevision = 1;
static volatile bool nvsDirty = false;
static volatile bool lastSaveOk = true;
static volatile uint32_t lastSaveAt = 0;
static volatile uint32_t dmxHeartbeat = 0;

// Deadline playback di-reset ketika PLAY/CHASE dimulai. Ini mencegah timer
// static lama membuat langkah pertama kadang terlambat atau tidak konsisten.
static volatile uint32_t chaseNextAt = 0;
static volatile uint32_t sceneNextAt = 0;
static volatile uint8_t sceneError = 0;

// Sinkronisasi antar-core (Core0 DMX <-> Core1 WiFi).
SemaphoreHandle_t dmxMutex = NULL;

WebServer server(80);

// ---------------------------------------------------------------
// WEBSOCKET PUSH (port 81, path /ws)
// State dikirim ke browser saat berubah -> UI tidak lagi polling /cur
// tiap detik (hemat CPU Core 1 & alokasi String). Semua KONTROL tetap
// lewat HTTP REST port 80: tidak ada handler lama yang diubah.
// ---------------------------------------------------------------
AsyncWebServer wsSrv(81);
AsyncWebSocket ws("/ws");
void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient* c, AwsEventType t, void*, uint8_t*, size_t){
  if(t==WS_EVT_CONNECT)         Serial.printf("WS: client %u tersambung\n",(unsigned)c->id());
  else if(t==WS_EVT_DISCONNECT) Serial.printf("WS: client %u putus\n",(unsigned)c->id());
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
static uint8_t compactPresets[N_PRESETS][COMPACT_CHUNK];  // buffer staging NVS
static uint8_t compactScenes[sizeof(scenes)];             // buffer staging NVS
const uint8_t STORAGE_VER = 7;             // naikkan bila layout berubah lagi

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
  bool ok=true;
  nvs.begin(NVS_NS,false);
  ok = nvs.putUChar("sver2",STORAGE_VER) > 0 && ok;
  ok = nvs.putBytes("pc",(uint8_t*)compactPresets,sizeof(compactPresets)) == sizeof(compactPresets) && ok;
  ok = nvs.putBytes("sc",compactScenes,sizeof(compactScenes)) == sizeof(compactScenes) && ok;
  ok = nvs.putInt("selP",selP) > 0 && ok;
  ok = nvs.putInt("selS",selS) > 0 && ok;
  nvs.end();
  lastSaveOk=ok;
  if(ok){ nvsDirty=false; lastSaveAt=millis(); }
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
  nvs.begin(NVS_NS,false);
  if(nvs.getUChar("sver2",0)!=STORAGE_VER){
    // Format lama (v27 ke bawah) atau NVS kosong: buang SEMUA key namespace
    // ini untuk merebut kembali ruang yang sudah habis/terfragmentasi,
    // lalu mulai bersih dengan default. Rekam ulang preset lalu Save Data.
    Serial.println("NVS: format lama terdeteksi -> clear() untuk reclaim ruang");
    nvs.clear();
    nvs.end();
    return;
  }
  bool ok = nvs.getBytes("pc",(uint8_t*)compactPresets,sizeof(compactPresets)) == sizeof(compactPresets)
         && nvs.getBytes("sc",compactScenes,sizeof(compactScenes)) == sizeof(compactScenes);
  if(ok){
    unpackCompactIntoRam();
    selectedPreset=nvs.getInt("selP",-1);
    selectedScene=nvs.getInt("selS",-1);
  }
  nvs.end();
}

// Muat ulang data tersimpan dari NVS ke RAM (tombol Load Data).
bool loadData(){
  nvs.begin(NVS_NS,true);
  bool ok=(nvs.getUChar("sver2",0)==STORAGE_VER);
  if(ok){
    ok = nvs.getBytes("pc",(uint8_t*)compactPresets,sizeof(compactPresets)) == sizeof(compactPresets)
      && nvs.getBytes("sc",compactScenes,sizeof(compactScenes)) == sizeof(compactScenes);
  }
  if(ok){
    unpackCompactIntoRam();
    selectedPreset=nvs.getInt("selP",-1);
    selectedScene=nvs.getInt("selS",-1);
    stateRevision++;
  }
  nvs.end();
  return ok;
}

void markStateChanged(){
  stateRevision++;
  nvsDirty=true;
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
    for(uint16_t c=0; c<fix[f].foot; c++)
      want[fix[f].start+c] = presets[idx][fix[f].start+c];
  }
  xSemaphoreGive(dmxMutex);
}
void capturePreset(int idx, bool ignoreDimmer, uint16_t fMs, uint16_t hMs){
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
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
    int r=row[2], g=row[3], b=row[4];   // pratinjau PAR1 (Dim,R,G,B)
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
  memset(tmp,0,sizeof(tmp));
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
    found++;
    pos = objEnd+1;                              // lanjut ke objek preset berikutnya
  }
  if(found==0) return false;                     // tidak ada preset valid -> gagal, data lama utuh
  // Commit: timpa hanya baris yang ditemukan di file (sisanya tidak disentuh)
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  for(int i=0;i<found;i++) memcpy(presets[i],tmp[i],PRESET_CHUNK);
  xSemaphoreGive(dmxMutex);
  markStateChanged();
  persistAll();
  return true;
}

// ---------------------------------------------------------------
// SCENE handlers
// ---------------------------------------------------------------
String scnJson(){
  String j="[";
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
  String j="{\"ok\":true,\"revision\":"+String(stateRevision)+"}";
  server.send(200,"application/json",j);
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
  j+="\"revision\":"+String(stateRevision)+",";
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
  for(int k=SCENE_STEPS-1;k>=0;k--){ if(scenes[s][k]!=0){ scenes[s][k]=0; break; } }
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
  uint8_t val=cv(v);
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  for(int f=0;f<N_FIX;f++){
    if(fix[f].type!=grp[i].typeFilter) continue;
    if(grp[i].offset>=fix[f].foot) continue;
    uint16_t ch=fix[f].start+grp[i].offset;
    want[ch]=val; out[ch]=val;
  }
  xSemaphoreGive(dmxMutex);
  stateRevision++; nvsDirty=true;
  sendApiOk();
}

String grpJson(){
  String j="[";
  for(int i=0;i<N_GROUPS;i++){
    j+="{\"name\":\""+String(grp[i].name)+"\",";
    j+="\"type\":"+String(grp[i].typeFilter)+",";
    j+="\"offset\":"+String(grp[i].offset)+"}";
    if(i<N_GROUPS-1) j+=",";
  }
  j+="]";
  return j;
}

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
  #faderPanel{grid-column:2;grid-row:3}
  #channelPanel{grid-column:1 / -1;grid-row:4}
  .fixgrd{display:grid;grid-template-columns:1fr;gap:12px}
  .fix{padding-top:10px;border-top:1px dashed var(--edge)}.fix:first-child{border-top:0;padding-top:0}
  .fix-name{font-size:12px;font-weight:700;color:var(--muted);letter-spacing:.3px;margin-bottom:2px}
  .io{display:flex;gap:8px;flex-wrap:wrap}
  .io button{flex:1;min-height:40px;background:#3a414b;color:#e8eaee}
  @media(min-width:520px){.fixgrd{grid-template-columns:1fr 1fr}}
  @media(max-width:760px){
    .wrap{display:flex;flex-direction:column;max-width:680px}
    header{order:0}
    #masterPanel{order:1}
    #scenePanel{order:2}
    #presetPanel{order:3}
    #faderPanel{order:4}
    #channelPanel{order:5}
    .panel{margin-bottom:14px}
  }
  @media(max-width:380px){.panel{padding:14px}.bank{grid-template-columns:repeat(4,1fr)}label{grid-template-columns:auto 1fr 36px}}
</style></head><body>
<div class="wrap">
  <header><h1>DMX<span class="dot">.</span>Console</h1><div class="meta"><span id="buildtag">__BUILD__</span><span>__IP__</span></div></header>

  <section class="panel" id="masterPanel">
    <h3>Master <span style="color:var(--muted);font-weight:400">global</span></h3>
    <label><span class="lab">Master</span><input type="range" id="master" min="0" max="255" value="255"><span class="val" id="masterv">255</span></label>
    <div class="actions"><button class="btn-off act" id="btnBlack">Blackout</button><button class="btn-go act" id="btnChase">Chase OFF</button><button class="btn-reset act" id="btnSaveData">Save Data</button><button class="btn-off act" id="btnLoadData">Load Data</button></div>
    <div class="status" id="saveStatus">Data tersimpan</div>
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
    <div class="bank" id="sbank"></div>
    <div class="steps" id="steps"></div>
    <p class="sub">durasi tiap langkah = Hold preset masing-masing</p>
  </section>

  <section class="panel" id="faderPanel">
    <h3>Fader Bank <span style="color:var(--muted);font-weight:400">1 fader &middot; banyak lampu</span></h3>
    <p class="sub">fader menulis channel yang sama di semua fixture grupnya</p>
    <div id="groups"></div>
  </section>

  <section class="panel" id="channelPanel">
    <h3>Channel</h3><p class="sub">geser = realtime &middot; dimmer channel pertama mengikuti master</p>
    <div class="fixgrd" id="fixes"></div>
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
window.addEventListener('error',e=>{
  const el=document.getElementById('statTxt');
  if(el){ el.textContent='JS ERROR: '+e.message; el.style.color='#e74c3c'; }
});
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
const NSCN=SCN.length, NSTEPS=SCN[0].length;
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
  if(t===4) return ['Fog','Fan'];
  if(t===3) return ['Mode','Strobe','Dim','Color'];
  const a=[]; for(let k=1;k<=f;k++)a.push('CH'+k); return a;
}
function buildFixes(){
  const box=$('fixes'); box.innerHTML='';
  for(let i=0;i<N;i++){
    const f=FIX[i]; const wrap=document.createElement('div'); wrap.className='fix';
    const nm=document.createElement('div'); nm.className='fix-name';
    nm.textContent=f.name+'   '+f.start+'-'+(f.start+f.foot-1);
    wrap.appendChild(nm);
    const labels=labelOf(i);
    for(let c=0;c<f.foot;c++){
      const lbl=document.createElement('label');
      const lab=document.createElement('span');lab.className='lab';lab.textContent=labels[c]||('CH'+(c+1));
      const inp=document.createElement('input');inp.type='range';inp.min=0;inp.max=255;inp.value=0;
      const idi=keyOf(i,c); inp.id=idi; inp.dataset.fi=i; inp.dataset.ch=c;
      const val=document.createElement('span');val.className='val';val.id=idi+'v';val.textContent='0';
      inp.addEventListener('input',()=>onInput(inp));
      inp.addEventListener('change',()=>onRelease(inp));
      inp.addEventListener('blur',()=>onRelease(inp));
      sliders[idi]=inp; allKeys.push(idi);
      lbl.appendChild(lab);lbl.appendChild(inp);lbl.appendChild(val);
      wrap.appendChild(lbl);
    }
    box.appendChild(wrap);
  }
}
function buildGroups(){
  const box=$('groups'); box.innerHTML='';
  GRP.forEach((g,i)=>{
    let n=0;
    FIX.forEach(f=>{ if(f.type===g.type && g.offset<f.foot) n++; });
    const lbl=document.createElement('label');
    const lab=document.createElement('span');lab.className='lab';lab.textContent=g.name+' \u00d7'+n;
    const inp=document.createElement('input');inp.type='range';inp.min=0;inp.max=255;inp.value=0;inp.id='g'+i;
    const val=document.createElement('span');val.className='val';val.id='g'+i+'v';val.textContent='0';
    inp.addEventListener('input',()=>{
      document.getElementById('g'+i+'v').textContent=inp.value;paintFill(inp);
      stopAuto();
      api('/grp?i='+i+'&v='+inp.value).catch(e=>showError(e.message));
    });
    lbl.appendChild(lab);lbl.appendChild(inp);lbl.appendChild(val);
    box.appendChild(lbl);
  });
}
// Posisi fader grup saat halaman dibuka = nilai member pertamanya (jujur, tidak palsu).
function syncGroups(j){
  if(!j.cur) return;
  GRP.forEach((g,i)=>{
    for(let fi=0;fi<FIX.length;fi++){
      if(FIX[fi].type===g.type && g.offset<FIX[fi].foot){
        const k=fi+'_'+g.offset;
        if(j.cur[k]!==undefined){
          const s=$('g'+i); s.value=j.cur[k];
          document.getElementById('g'+i+'v').textContent=j.cur[k]; paintFill(s);
        }
        break;
      }
    }
  });
}
let chaseOn=false;
let sceneOn=false, selScene=-1, sceneEdit=false;
let activeKey=null;   // slider yang sedang digeser user (dilewati polling)
// Throttle: maks 1 request /set beredar; nilai terakhir dikirim setelahnya.
let setInFlight=false,setPending=null;
function pushOne(fi,c,v){
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
$('master').addEventListener('input',()=>{activeKey='master';$('masterv').textContent=$('master').value;paintFill($('master'));pushCtrl('mast='+$('master').value);});
$('master').addEventListener('change',()=>onRelease($('master')));
$('master').addEventListener('blur',()=>onRelease($('master')));
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
$('btnBlack').addEventListener('click',()=>{$('master').value=0;$('masterv').textContent=0;pushCtrl('mast=0');});
// Aman perangkat: "Penuh" hanya untuk PAR (dimmer+RGB); moving/fog/strobe tetap 0.
$('btnWhite').addEventListener('click',()=>{allKeys.forEach(k=>{const fi=+k.split('_')[0];sliders[k].value=(FIX[fi].type===0)?255:0;});paintAll();pushCtrl('all=on');});
$('btnOff').addEventListener('click',()=>{allKeys.forEach(k=>sliders[k].value=0);paintAll();pushCtrl('all=off');});
$('btnChase').addEventListener('click',()=>setChase(!chaseOn));
let editMode=false,ignoreDimmer=false;const $idim=$('idim');$idim.addEventListener('change',()=>{ignoreDimmer=$idim.checked;$('idimTag').classList.toggle('on',ignoreDimmer);});
let presetsData=[],selPreset=-1;
function rgbStr(p){if(!p||!p.used)return'';return 'rgb('+p.r+','+p.g+','+p.b+')';}
function renderBank(){const bank=$('bank');bank.innerHTML='';presetsData.forEach((p,i)=>{const pad=document.createElement('button');pad.className='pad'+(p.used?'':' empty')+(selPreset===i?' sel':'');pad.setAttribute('aria-label','Preset '+(i+1));const csw=document.createElement('span');csw.className='csw';if(p.used)csw.style.background=rgbStr(p);const num=document.createElement('span');num.className='num';num.textContent=i+1;pad.appendChild(num);pad.appendChild(csw);pad.addEventListener('click',()=>onPad(i));bank.appendChild(pad);});}
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
  allKeys.forEach(k=>{
    if(skipActive && k===activeKey) return;
    const v=(j.cur&&j.cur[k]!==undefined)?j.cur[k]:0;
    sliders[k].value=v; document.getElementById(k+'v').textContent=v; paintFill(sliders[k]);
  });
  if(j.chaseOn!==undefined && j.chaseOn!==chaseOn){ chaseOn=j.chaseOn; applyChaseBtn(); }
  if(j.sceneOn!==undefined && j.sceneOn!==sceneOn){ sceneOn=j.sceneOn; applySceneBtn(); if(!sceneOn) renderSteps(); }
  syncSelectedState(j);
  if(j.selectedScene!==undefined && j.selectedScene>=0 && j.selectedScene<NSCN){
    if(selScene!==j.selectedScene){ selScene=j.selectedScene; renderSceneBank(); renderSteps(); }
  }
  serverScene=(j.scn!==undefined && j.scn>=0)?j.scn:-1;
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
buildFixes();buildGroups();buildSceneBank();renderSteps();updatePinfo();refreshPresets();
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
</script>
</body></html>
)HTML";

// JSON fixture
String fixJson(){
  String j="[";
  for(int i=0;i<N_FIX;i++){
    j+="{\"name\":\""+String(fix[i].name)+"\",";
    j+="\"type\":"+String(fix[i].type)+",";
    j+="\"start\":"+String(fix[i].start)+",";
    j+="\"foot\":"+String(fix[i].foot)+"}";
    if(i<N_FIX-1) j+=",";
  }
  j+="]";
  return j;
}

void sendUi(){
  String page = FPSTR(INDEX_HTML);
  page.replace("__IP__", activeIP().toString());
  page.replace("__BUILD__", BUILD_TAG);
  page.replace("__FIXDATA__", fixJson());
  page.replace("__GRPDATA__", grpJson());
  page.replace("__SCNDATA__", scnJson());
  // Cegah cache browser: setelah firmware di-update, UI lama bisa bertahan
  // dan bentrok dengan endpoint baru (sumber bug "fitur hilang" yang aneh).
  server.sendHeader("Cache-Control","no-store, must-revalidate");
  server.send(200,"text/html",page);
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
        want[ch]=cv(v); out[ch]=want[ch];
      }
    }
  }
  xSemaphoreGive(dmxMutex);
  stateRevision++;
  sendApiOk();
}
void onCtrl(){
  // fade/hold kini milik preset (di-set saat preset dimuat); /ctrl hanya master & all.
  if(server.hasArg("mast")){ masterWant=cv(server.arg("mast").toInt()); masterOut=masterWant; }
  if(server.hasArg("all")){
    bool on = server.arg("all")=="on";
    xSemaphoreTake(dmxMutex,portMAX_DELAY);
    for(int f=0;f<N_FIX;f++){
      // Aman perangkat: "Penuh" hanya untuk PAR. Moving head (pan/tilt bisa
      // menyentak ke end-stop), fog (output penuh), dan strobe di-nol-kan.
      bool safe = (fix[f].type==FX_PAR);
      for(uint16_t c=0;c<fix[f].foot;c++){
        uint16_t ch=fix[f].start+c;
        uint8_t v = on ? (safe?255:0) : 0;
        want[ch]=v; out[ch]=v;
      }
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
  static uint8_t snapOut[513];
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
  memcpy(snapOut,out,sizeof(snapOut));
  uint8_t m=masterOut;
  int si=sceneIdx, st=sceneStep;
  bool so=sceneOn;
  xSemaphoreGive(dmxMutex);
  String j="{";
  j+="\"master\":"+String(m)+",\"fade\":"+String(fadeMs)+",\"chase\":"+String(chaseMs)+",\"chaseOn\":"+(chaseOn?"true":"false")+",\"sceneOn\":"+(so?"true":"false")+",\"scenesp\":"+String(sceneMs)+",\"scn\":"+String(si)+",\"stp\":"+String(st)+",\"selectedPreset\":"+String(selectedPreset)+",\"selectedScene\":"+String(selectedScene)+",\"revision\":"+String(stateRevision)+",\"nvsDirty\":"+(nvsDirty?"true":"false")+",\"lastSaveOk\":"+(lastSaveOk?"true":"false")+",\"cur\":{";
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
  capturePreset(n,idim,(uint16_t)f,(uint16_t)h);
  selectedPreset=n;
  sendApiOk();
}
// GET /psetfade?n=X&f=&h= -> ubah fade/hold preset X tanpa merekam ulang
void onPresetFade(){
  int n=server.arg("n").toInt()-1;
  if(n<0||n>=N_PRESETS||!presets[n][0]){ sendApiError(404,"preset_missing","Preset belum tersedia"); return; }
  long f=server.arg("f").toInt(); if(f<0) f=0; if(f>2550) f=2550;
  long h=server.arg("h").toInt(); if(h<100) h=100; if(h>5000) h=5000;
  xSemaphoreTake(dmxMutex,portMAX_DELAY);
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
#define IMPORT_MAX 65536            // 64KB cukup utk 16 preset; cegah OOM
String importBuf; volatile bool importDone=false; volatile bool importTooBig=false;
void onImportFinal(){
  if(importTooBig)      server.send(413,"text/plain","File terlalu besar (maks 64KB)");
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
// SETUP / LOOP
// ---------------------------------------------------------------
void setup(){
  Serial.begin(115200); delay(300);
  Serial.println(); Serial.println("=== DMX Web Console " BUILD_TAG " ===");

  dmxMutex = xSemaphoreCreateMutex();

  // DMX UART + break sesuai standar (88-176us; set 100us)
  DMX.configure();
  DMX.setBreakLength(100);

  memset(want,0,sizeof(want)); memset(out,0,sizeof(out));
  masterWant=255; masterOut=255;
  loadAll();

  // WiFi STA: konek ke router "SIGMA"; bila gagal -> fallback AP darurat.
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);            // respons web lebih responsif
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi: menyambung ke "); Serial.print(WIFI_SSID);
  uint32_t t0=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-t0<15000){
    delay(250); Serial.print(".");
  }
  Serial.println();
  if(WiFi.status()==WL_CONNECTED){
    Serial.print("Tersambung. Buka browser: http://");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Gagal tersambung. Fallback ke AP darurat.");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);
    Serial.print("AP: "); Serial.print(AP_SSID);
    Serial.print(" | Buka browser: http://"); Serial.println(WiFi.softAPIP());
  }

  server.on("/",      HTTP_GET, sendUi);
  server.on("/set",   HTTP_GET, onSet);
  server.on("/grp",   HTTP_GET, onGroup);
  server.on("/scenes",HTTP_GET, onScenesGet);
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
void wsBroadcastTick(){
  static uint32_t lastAt=0, lastRev=0;
  uint32_t now=millis();
  if(stateRevision==lastRev && now-lastAt<1000) return;
  if(ws.count()==0){ lastAt=now; return; }   // tanpa klien -> hemat CPU; lastRev sengaja tidak diupdate
  lastRev=stateRevision; lastAt=now;
  ws.textAll(buildStateJson());
}
void loop(){
  server.handleClient();
  ws.cleanupClients();      // housekeeping AsyncWebSocket
  wsBroadcastTick();
}
