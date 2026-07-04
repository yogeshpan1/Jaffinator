<div align="center">

# ⚡ JAFFINATOR

### Portable Multi-Protocol Wireless Security Research Platform

*A compact ESP32-powered penetration testing and wireless analysis device inspired by modern hardware security research tools.*

---

![ESP32](https://img.shields.io/badge/ESP32-Powered-red)
![Arduino](https://img.shields.io/badge/Arduino-C++-00979D)
![Platform](https://img.shields.io/badge/Platform-Embedded-success)
![Status](https://img.shields.io/badge/Status-Active%20Development-blue)
![License](https://img.shields.io/badge/License-MIT-green)

</div>

---

# 📖 Overview

**Jaffinator** is a portable wireless security research platform built around the **ESP32** microcontroller.

The project was developed to demonstrate how low-cost embedded hardware can be used to perform wireless reconnaissance, protocol analysis, NFC experimentation, Bluetooth research, and packet monitoring from a single standalone device.

Rather than focusing on a single communication protocol, Jaffinator combines multiple wireless technologies into one modular platform, making it suitable for:

- Embedded Systems Research
- IoT Security
- Wireless Networking
- Penetration Testing Education
- Digital Forensics
- Cybersecurity Demonstrations
- Academic Research

---

# ✨ Features

## 📡 WiFi Scanner

Scan nearby WiFi access points and display:

- SSID
- Signal Strength (RSSI)
- Nearby wireless networks
- Quick wireless reconnaissance

---

## 📶 WiFi Beacon Generator

Broadcast multiple configurable WiFi beacon frames for wireless testing.

Features include:

- Fake SSID broadcasting
- Multiple beacon generation
- Custom network names
- Wireless environment simulation

---

## 🌐 Captive Portal

Educational Evil Portal implementation for authorized security awareness demonstrations.

Current demo portal:

- Free Worldlink Login Page

Designed for:

- Security awareness training
- Phishing demonstrations
- Network authentication education

---

## 🔵 Bluetooth Research

### BLE Scanner

Discover nearby Bluetooth Low Energy devices.

Displays:

- Device Name
- RSSI
- Relative Distance
- Nearby Bluetooth peripherals

---

### Windows BLE Pairing Demonstration

Demonstrates Bluetooth pairing request behavior on supported Windows devices using BLE advertisement techniques.

---

## 📱 NFC Reader

Read NFC tags and cards.

Capabilities:

- UID Reading
- Tag Detection
- Card Information
- Multiple scan sessions

---

## 💳 NFC Clone (Compatible Tags)

Educational NFC cloning support for compatible laboratory test cards.

Supports:

- UID Reading
- UID Storage
- Writing to compatible development cards

---

## 📡 Packet Sniffer

Monitor nearby wireless traffic in monitor mode.

Features:

- Packet counting
- Network monitoring
- Channel hopping
- Device tracking
- Wireless traffic analysis

---

# 🖥 User Interface

Jaffinator provides a standalone interface through a TFT display with physical navigation buttons.

Main menu includes:

```
WiFi
 ├── Scan
 ├── Beacon

Bluetooth
 ├── Scan
 └── BLE Research

Portal
 └── Captive Portal

NFC
 ├── Read
 └── Clone

Sniffer
 └── Packet Monitor
```

No computer is required during operation after flashing the firmware.

---

# 🛠 Hardware

The project is built using:

- ESP32 Development Board
- TFT LCD Display
- PN532 NFC Module
- Push Buttons
- LiPo Battery
- Battery Charging Module
- SD Card Module
- Bluetooth (ESP32 Built-in)
- WiFi (ESP32 Built-in)

---

# 🧰 Software Stack

- Arduino IDE
- C++
- ESP-IDF Libraries
- Adafruit GFX
- WiFi Library
- BLE Library
- PN532 Library
- SPI
- I2C

---

# 🚀 Current Capabilities

| Feature | Status |
|----------|--------|
| WiFi Scan | ✅ |
| WiFi Beacon | ✅ |
| Captive Portal | ✅ |
| BLE Scanner | ✅ |
| Windows BLE Demonstration | ✅ |
| NFC Read | ✅ |
| NFC Clone | ✅ |
| Packet Sniffer | ✅ |

---

# 📸 Screenshots

```

/images/menu.jpg

/images/wifi_scan.jpg

/images/sniffer.jpg

/images/nfc.jpg

/images/ble.jpg
```

---

# 🔮 Roadmap

Planned future improvements include:

- USB HID Research (BadUSB)
- Sub-GHz RF Support
- RFID Expansion
- Infrared Module
- GPIO Toolkit
- BLE Improvements
- Better UI Animations
- SD Card Logging
- OTA Firmware Updates
- Plugin Architecture

---

# ⚠ Legal Notice

Jaffinator is intended **solely for educational purposes, laboratory experimentation, defensive security research, and authorized penetration testing**.

Users are solely responsible for ensuring that all testing is performed only on systems, devices, and networks for which they have explicit permission.

The authors assume **no liability** for misuse of this project.

---

# 🤝 Contributing

Contributions are welcome.

If you'd like to improve Jaffinator:

- Fork the repository
- Create a feature branch
- Commit your changes
- Submit a Pull Request

Ideas, bug reports, and feature requests are always appreciated.

---



# 👨‍💻 Members

**Arjan Regmi**
**Jasun Maharjan**
**RIkesh Adhikari**
**Yogesh Pant**


---

<div align="center">

### ⚡ "Portable Security Research, Powered by ESP32."

</div>
