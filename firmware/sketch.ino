#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <PNGdec.h>

// ── Display (P1) ──────────────────────────────────────────
#define TFT_CS  P1_IO0
#define TFT_RST P1_IO1
#define TFT_DC  P1_IO2

#ifndef FSPI
#define FSPI 0
#endif
SPIClass       mySPI(FSPI);
Adafruit_ST7735 tft = Adafruit_ST7735(&mySPI, TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16    canvas(160, 80);

// ── PNG decoder ───────────────────────────────────────────
PNG  png;
File pngFile;

void* pngOpen(const char* filename, int32_t* size) {
  pngFile = LittleFS.open(filename, "r");
  if (!pngFile) return nullptr;
  *size = pngFile.size();
  return (void*)&pngFile;
}
void pngClose(void* handle) {
  if (handle) ((File*)handle)->close();
}
int32_t pngRead(PNGFILE* /*handle*/, uint8_t* buf, int32_t len) {
  return pngFile.read(buf, len);
}
int32_t pngSeek(PNGFILE* /*handle*/, int32_t pos) {
  return pngFile.seek(pos) ? pos : -1;
}
void pngDraw(PNGDRAW* pDraw) {
  uint16_t line[160];
  png.getLineAsRGB565(pDraw, line, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
  int w = min((int)pDraw->iWidth, 160);
  for (int x = 0; x < w; x++) {
    uint16_t c = line[x];
    uint8_t r = (c >> 11) & 0x1F;
    uint8_t g = (c >>  5) & 0x3F;
    uint8_t b =  c        & 0x1F;
    line[x] = (b << 11) | (g << 5) | r;
  }
  int y = pDraw->y;
  if (y < 80) memcpy(canvas.getBuffer() + y * 160, line, w * 2);
}

// ── WiFi / web server ─────────────────────────────────────
const char* ssid     = "%%WIFI_SSID%%";
const char* password = "%%WIFI_PASSWORD%%";
WebServer server(80);

// ── Animation state ───────────────────────────────────────
bool          playing      = false;
int           frameCount   = 0;
int           currentFrame = 0;
unsigned long lastFrameMs  = 0;
const unsigned long FRAME_INTERVAL = 100; // 10 fps

// ── Helpers ───────────────────────────────────────────────
int countFrames() {
  char name[32];
  int n = 0;
  while (true) {
    snprintf(name, sizeof(name), "/frame%03d.png", n);
    if (!LittleFS.exists(name)) break;
    n++;
  }
  return n;
}

void showStatus(const char* line1, const char* line2 = nullptr) {
  canvas.fillScreen(0x0000);
  canvas.setTextColor(0xFFFF);
  canvas.setTextSize(1);
  canvas.setCursor(4, line2 ? 26 : 34);
  canvas.print(line1);
  if (line2) {
    canvas.setCursor(4, 44);
    canvas.print(line2);
  }
  tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 160, 80);
}

void playFrame(int idx) {
  char name[32];
  snprintf(name, sizeof(name), "/frame%03d.png", idx);
  if (png.open(name, pngOpen, pngClose, pngRead, pngSeek, pngDraw) == PNG_SUCCESS) {
    png.decode(nullptr, 0);
    png.close();
    tft.drawRGBBitmap(0, 0, canvas.getBuffer(), 160, 80);
  }
}

// ── Web server — embedded upload UI ──────────────────────
const char UPLOAD_HTML[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html><head>
<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>
<title>PNG Animation Loader</title>
<style>
*{box-sizing:border-box}
body{font-family:sans-serif;background:#111;color:#eee;padding:24px;max-width:480px;margin:auto}
h2{color:#7cf;margin-top:0}p{color:#aaa;font-size:.9em}
button{padding:10px 20px;border:none;border-radius:6px;font-size:.95em;cursor:pointer;margin:4px}
.up{background:#4af;color:#000}.play{background:#4f8;color:#000}
.stop{background:#f84;color:#000}.clr{background:#555;color:#eee}
input[type=file]{display:block;margin:12px 0;width:100%;color:#ccc}
#status{margin-top:14px;padding:10px;background:#1a1a1a;border-radius:6px;font-size:.9em;color:#8f8}
</style></head><body>
<h2>PNG Animation Loader</h2>
<p>Upload 160x80 px PNG frames named <b>frame000.png, frame001.png, ...</b></p>
<input type='file' id='files' accept='.png' multiple>
<button class='up' onclick='uploadAll()'>Upload Frames</button>
<hr style='border-color:#333;margin:16px 0'>
<button class='play' onclick='cmd("play")'>Play</button>
<button class='stop' onclick='cmd("stop")'>Stop</button>
<button class='clr'  onclick='cmd("clear")'>Clear All</button>
<div id='status'>Ready.</div>
<script>
async function uploadAll(){
  const files=[...document.getElementById('files').files];
  if(!files.length){status('No files selected.');return;}
  status('Uploading...');
  let ok=0;
  for(const f of files){
    const fd=new FormData();fd.append('file',f,f.name);
    try{const r=await fetch('/upload',{method:'POST',body:fd});if(r.ok)ok++;}
    catch(e){}
  }
  status(`Uploaded ${ok}/${files.length} files.`);
}
async function cmd(route){
  try{
    const r=await fetch('/'+route);
    const t=await r.text();
    status(t);
  }catch(e){status('Error: '+e);}
}
function status(msg){document.getElementById('status').textContent=msg;}
</script></body></html>
)rawhtml";

void handleRoot() {
  server.send_P(200, "text/html", UPLOAD_HTML);
}

void handleUpload() {
  HTTPUpload& upload = server.upload();
  static File f;
  if (upload.status == UPLOAD_FILE_START) {
    String fname = upload.filename;
    int sl = fname.lastIndexOf('/');
    if (sl >= 0) fname = fname.substring(sl + 1);
    f = LittleFS.open("/" + fname, "w");
    Serial.printf("Upload start: /%s\n", fname.c_str());
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (f) f.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (f) f.close();
    frameCount = countFrames();
    Serial.printf("Upload done: %d frames on flash\n", frameCount);
  }
}

void handlePlay() {
  playing      = true;
  currentFrame = 0;
  frameCount   = countFrames();
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "Playing " + String(frameCount) + " frames at 10 fps");
}

void handleStop() {
  playing = false;
  showStatus("Stopped.", WiFi.localIP().toString().c_str());
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "Stopped");
}

void handleClear() {
  playing = false;
  for (int i = 0; i < 999; i++) {
    char name[32];
    snprintf(name, sizeof(name), "/frame%03d.png", i);
    if (!LittleFS.exists(name)) break;
    LittleFS.remove(name);
  }
  frameCount = 0;
  showStatus("Cleared.", WiFi.localIP().toString().c_str());
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "text/plain", "Cleared all frames");
}

void handleStatus() {
  char buf[80];
  snprintf(buf, sizeof(buf),
    "{\"frames\":%d,\"playing\":%s,\"current\":%d}",
    frameCount, playing ? "true" : "false", currentFrame);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", buf);
}

// ── Setup ─────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  mySPI.begin(SCK, MISO, MOSI);
  tft.initR(INITR_MINI160x80);
  tft.setRotation(3);
  showStatus("Starting...");

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed");
    showStatus("LittleFS error");
    return;
  }

  showStatus("Connecting WiFi...");
  WiFi.begin(ssid, password);
  Serial.printf("Connecting to WiFi '%s'", ssid);
  unsigned long t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(500); Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    String ip = WiFi.localIP().toString();
    Serial.printf("\nConnected! IP: %s\n", ip.c_str());
    showStatus("Upload PNGs at:", ip.c_str());
  } else {
    Serial.printf("\nWiFi failed for '%s'\n", ssid);
    showStatus("WiFi failed.", "Check credentials.");
  }

  server.on("/", handleRoot);
  server.on("/upload", HTTP_POST,
    []() { server.send(200, "text/plain", "OK"); },
    handleUpload
  );
  server.on("/play",   handlePlay);
  server.on("/stop",   handleStop);
  server.on("/clear",  handleClear);
  server.on("/status", handleStatus);
  server.begin();

  frameCount = countFrames();
  Serial.printf("Found %d existing frames on flash\n", frameCount);
}

// ── Loop ──────────────────────────────────────────────────
void loop() {
  server.handleClient();

  if (playing && frameCount > 0) {
    unsigned long now = millis();
    if (now - lastFrameMs >= FRAME_INTERVAL) {
      lastFrameMs = now;
      playFrame(currentFrame);
      currentFrame = (currentFrame + 1) % frameCount;
    }
  }
}
