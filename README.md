# 🌱 SmartFarm v3.5

**IoT-Based Smart Agriculture & Irrigation System**

---

## 📌 Overview

SmartFarm v3.5 is an advanced IoT-enabled smart agriculture system designed to automate irrigation, monitor environmental conditions, and provide real-time alerts and remote control.

The system intelligently manages water usage, protects crops from environmental stress, and improves farming efficiency using sensor data and automation logic.

---

## 🚀 Features

### 🌱 Intelligent Irrigation

* Soil moisture-based automatic watering
* Rain-aware irrigation control
* Water level safety protection
* Pump auto shut-off (timeout protection)

### ☔ Rain Detection System

* Detects **clear / light rain / heavy rain**
* Prevents overwatering during rainfall
* Smart pump control based on rain intensity

### 🌡️ Climate Control

* Dual fan system with temperature thresholds
* Automatic cooling based on environment
* Critical temperature protection mode

### 📊 Real-Time Monitoring

* Temperature & Humidity
* Soil Moisture
* Water Level
* Light Intensity
* Rain Status

### 📡 IoT & Cloud Integration

* ThingSpeak for data logging
* Telegram Bot for:

  * Alerts 🚨
  * Remote control 📱
  * System status reports

### ⚠️ Fault Detection System

* High/low temperature alerts
* Low/critical water level warnings
* Soil condition alerts
* Pump malfunction detection
* WiFi disconnection alert
* Sensor failure detection

### 🔄 Control Modes

* **AUTO Mode** → Fully automatic
* **MANUAL Mode** → User control via:

  * Button
  * Telegram commands

---

## 🛠️ Hardware Components

* ESP32
* DHT22 Temperature & Humidity Sensor
* Soil Moisture Sensor (Capacitive)
* Rain Sensor Module
* Water Level Sensor
* LDR (Light Sensor)
* 16x2 LCD (I2C)
* Relay Module (Pump + Fans)
* Water Pump
* Cooling Fans
* Push Button
* Power Supply (SMPS + Battery)

---

## 🔌 Pin Configuration

| Component     | ESP32 Pin |
| ------------- | --------- |
| DHT22         | 13        |
| Soil Sensor   | 34        |
| LDR           | 35        |
| Rain Sensor   | 32        |
| Water Level   | 33        |
| Pump Relay    | 16        |
| Fan 1 Relay   | 26        |
| Fan 2 Relay   | 27        |
| Manual Button | 4         |
| WiFi LED      | 2         |

---

## ⚙️ System Logic

### 💧 Pump Control

Pump operation depends on:

* Soil moisture
* Water level
* Rain condition

| Condition       | Action                |
| --------------- | --------------------- |
| Very Dry Soil   | Pump ON               |
| Dry Soil        | Pump ON (if water OK) |
| Heavy Rain      | Pump OFF              |
| Light Rain      | Limited operation     |
| Low Water Level | Pump OFF              |
| Critical Water  | Emergency STOP        |

---

### 🌡️ Fan Control

| Temperature | Action                       |
| ----------- | ---------------------------- |
| > 22°C      | Fan 1 ON                     |
| > 24°C      | Fan 2 ON                     |
| > 35°C      | Both Fans ON (Critical Mode) |

---

## 📲 Telegram Commands

| Command     | Description           |
| ----------- | --------------------- |
| `/start`    | Show help menu        |
| `/status`   | Get system status     |
| `/pump_on`  | Turn pump ON          |
| `/pump_off` | Turn pump OFF         |
| `/auto`     | Switch to AUTO mode   |
| `/manual`   | Switch to MANUAL mode |

---

## ☁️ ThingSpeak Integration

* Sends sensor data every **30 seconds**
* Fields used:

  * Field1 → Temperature
  * Field2 → Humidity
  * Field3 → Soil Moisture
  * Field4 → Light
  * Field5 → Rain
  * Field6 → Water Level
  * Field7 → Pump Status
  * Field8 → Rain Status

---

## 🖥️ LCD Display

The LCD cycles through multiple screens showing:

* System uptime
* Temperature & humidity
* Soil & water status
* Rain condition
* Pump & fan status
* Plant health

---

## ⚠️ Fault Handling

The system automatically detects and alerts:

* 🔥 High temperature
* ❄️ Low temperature
* 💧 Low/critical water
* 🌱 Soil too dry or too wet
* ☔ Heavy rain
* 🔄 Pump running too long
* 📡 WiFi disconnection
* 🌡️ Sensor failure

---

## 🔧 Installation

### 1. Install Libraries

* WiFi
* HTTPClient
* DHT
* LiquidCrystal_I2C
* ArduinoJson
* UniversalTelegramBot

### 2. Setup Credentials

Update in code:

```
const char *ssid = "YOUR_WIFI";
const char *password = "YOUR_PASSWORD";

#define BOT_TOKEN "YOUR_BOT_TOKEN"
#define CHAT_ID "YOUR_CHAT_ID"
```

### 3. Upload Code

* Select **ESP32 board**
* Upload via Arduino IDE

---

## 🔋 Power Setup

* ESP32 + Sensors → 5V regulated supply
* Pump & Fans → Separate SMPS
* **Important:** Common Ground required

---

## 📈 Future Improvements

* Mobile App Dashboard
* AI-based irrigation prediction
* Solar power integration
* Camera-based plant monitoring
* Weather API integration

---

## 👨‍💻 Author

**md.jamil**

---

## 📄 License

This project is for educational and research purposes.

---

## ⭐ Final Note

SmartFarm v3.5 is a complete smart agriculture solution that combines
automation, IoT, and real-time monitoring to create a reliable and efficient farming system.

