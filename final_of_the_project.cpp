#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

#define DHTPIN 13
#define DHTTYPE DHT22
#define SOIL_PIN 34
#define LDR_PIN 35
#define RAIN_PIN 32
#define WATER_LEVEL_PIN 33
#define PUMP_RELAY 16
#define FAN1_RELAY 26
#define FAN2_RELAY 27
#define MANUAL_BUTTON 4
#define WIFI_STATUS_LED 2

#define PROJECT_NAME "SmartFarm"
#define VERSION "v3.6" // safety update

#define TEMP_FAN1_ON 22
#define TEMP_FAN1_OFF 18
#define TEMP_FAN2_ON 24
#define TEMP_FAN2_OFF 20
#define TEMP_CRITICAL 35

#define WATER_PUMP_ON 1200
#define WATER_PUMP_OFF 600
#define WATER_CRITICAL 400

#define SOIL_VERY_DRY 3000
#define SOIL_DRY 2800
#define SOIL_OPTIMAL_MAX 2200
#define SOIL_WET 2500
#define SOIL_VERY_WET 1200

#define RAIN_CLEAR 3000
#define RAIN_LIGHT 2000
#define RAIN_HEAVY 1000

#define RAIN_THRESHOLD 2000
#define RAIN_HEAVY_THRESH 1000

#define THINGSPEAK_WRITE_KEY "NC47N46ACJUARDCK"
const char *server = "http://api.thingspeak.com/update";
const char *ssid = "Kuet21";
const char *password = "89777145!!";

#define BOT_TOKEN "8435066468:AAGJiyvhtlUxYoXluewx7oV0zcy53vYaXaM"
#define CHAT_ID "6436948731"
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

#define UPLOAD_INTERVAL 30000
#define SENSOR_READ_INTERVAL 2000
#define LCD_UPDATE_INTERVAL 2000
#define TELEGRAM_ALERT_INTERVAL 60000
#define TELEGRAM_STATUS_INTERVAL 600000 // 10 minutes

// SAFETY: system ready flag
bool systemReady = false;
const unsigned long SAFETY_STARTUP_DELAY = 5000; // 5 seconds

DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

unsigned long lastUpload = 0;
unsigned long lastSensorRead = 0;
unsigned long lastLCDUpdate = 0;
unsigned long lastTelegramAlert = 0;
unsigned long lastTelegramStatus = 0;
unsigned long pumpStartTime = 0;

float temperature = 0;
float humidity = 0;
int soilMoisture = 0;
int lightLevel = 0;
int rainLevel = 0;
int waterLevel = 0;

bool pumpStatus = false;
bool fan1Status = false;
bool fan2Status = false;
bool manualMode = false;
bool manualPumpRequest = false;
bool isRaining = false;
bool isHeavyRain = false;

int lcdScreen = 0;
const int TOTAL_SCREENS = 7;

bool criticalTempAlertSent = false;
bool criticalWaterAlertSent = false;
bool criticalSoilAlertSent = false;
bool heavyRainAlertSent = false;
bool pumpStuckAlertSent = false;
bool wifiDisconnectAlertSent = false;
bool dhtFaultAlertSent = false;

String pumpReason = "";
String activeFaults = "";
unsigned long faultStartTime = 0;

// Function prototypes
String getTimeString();
String getSoilCondition();
String getRainCondition();
String getPlantGrowthStatus();
void updateRainStatus();
bool isSoilDry();
bool isSoilVeryDry();
bool isSoilWet();
bool shouldPumpRun();
void sendTelegramMessage(String message, bool isAlert = false);
void checkAndReportFaults();
void sendStatusUpdate();
void readSensors();
void controlActuators();
void checkButton();
void updateLCD();
void printData();
void sendToThingSpeak();

String getTimeString()
{
  unsigned long seconds = millis() / 1000;
  int hours = (seconds / 3600) % 24;
  int minutes = (seconds / 60) % 60;
  int secs = seconds % 60;

  char timeStr[9];
  sprintf(timeStr, "%02d:%02d:%02d", hours, minutes, secs);
  return String(timeStr);
}

String getSoilCondition()
{
  if (soilMoisture > SOIL_VERY_DRY)
    return "VERY DRY";
  if (soilMoisture > SOIL_DRY)
    return "DRY";
  if (soilMoisture > SOIL_OPTIMAL_MAX)
    return "OPTIMAL";
  if (soilMoisture > SOIL_WET)
    return "WET";
  return "VERY WET";
}

bool isSoilDry()
{
  return (soilMoisture > SOIL_DRY);
}

bool isSoilVeryDry()
{
  return (soilMoisture > SOIL_VERY_DRY);
}

bool isSoilWet()
{
  return (soilMoisture < SOIL_WET);
}

String getRainCondition()
{
  if (rainLevel < RAIN_HEAVY_THRESH)
    return "HEAVY";
  if (rainLevel < RAIN_THRESHOLD)
    return "LIGHT";
  if (rainLevel > RAIN_CLEAR)
    return "CLEAR";
  return "DAMP";
}

String getPlantGrowthStatus()
{
  if (soilMoisture == 0 || lightLevel == 0)
  {
    return "SENSOR ERR";
  }

  bool soilGood = (soilMoisture >= SOIL_WET && soilMoisture <= SOIL_DRY);
  bool lightGood = (lightLevel > 1500);

  if (soilMoisture > SOIL_VERY_DRY)
    return "WILTING!";
  if (soilMoisture < SOIL_VERY_WET)
    return "ROT!";
  if (lightLevel < 500)
    return "NO LIGHT!";
  if (soilGood && lightGood)
    return "THRIVING!";
  if (!soilGood && !lightGood)
    return "CRITICAL";
  if (!soilGood)
    return "SOIL BAD";
  if (!lightGood)
    return "LIGHT BAD";
  return "FAIR";
}

void updateRainStatus()
{
  bool wasRaining = isRaining;
  bool wasHeavyRain = isHeavyRain;

  isHeavyRain = (rainLevel < RAIN_HEAVY_THRESH);
  isRaining = (rainLevel < RAIN_THRESHOLD);

  if (isHeavyRain)
    isRaining = true;

  if (isRaining != wasRaining || isHeavyRain != wasHeavyRain)
  {
    if (isHeavyRain)
    {
      Serial.print("☔☔ HEAVY RAIN DETECTED! (Sensor: ");
      sendTelegramMessage("HEAVY RAIN STARTED!\nPump disabled to prevent overwatering.\nRain level: " + String(rainLevel), true);
    }
    else if (isRaining && !wasRaining)
    {
      Serial.print("☔ RAIN STARTED! (Sensor: ");
      sendTelegramMessage("RAIN DETECTED!\nPump will only run if soil is very dry.\nRain level: " + String(rainLevel), true);
    }
    else if (!isRaining && wasRaining)
    {
      Serial.print("☀️ RAIN STOPPED! (Sensor: ");
      sendTelegramMessage("RAIN STOPPED!\nResuming normal pump operation.\nRain level: " + String(rainLevel), true);
    }
    else
    {
      Serial.print("☔ LIGHT RAIN DETECTED! (Sensor: ");
    }
    Serial.print(rainLevel);
    Serial.println(")");
  }
}

bool shouldPumpRun()
{
  // SAFETY: Do not allow pump until system is ready
  if (!systemReady)
  {
    pumpReason = "System starting";
    return false;
  }

  if (waterLevel < WATER_CRITICAL)
  {
    pumpReason = "EMERGENCY:Low water";
    return false;
  }

  if (manualMode)
    return manualPumpRequest;

  if (isHeavyRain)
  {
    pumpReason = "Heavy rain";
    return false;
  }

  if (isRaining)
  {
    if (isSoilVeryDry())
    {
      pumpReason = "Light rain,soil dry";
      return true;
    }
    else
    {
      pumpReason = "Raining,soil ok";
      return false;
    }
  }

  if (isSoilVeryDry())
  {
    if (waterLevel > WATER_PUMP_ON)
    {
      pumpReason = "Soil VERY dry";
      return true;
    }
    else
    {
      pumpReason = "Soil dry,no water";
      return false;
    }
  }

  if (isSoilDry())
  {
    if (waterLevel > WATER_PUMP_ON)
    {
      pumpReason = "Soil dry";
      return true;
    }
    else
    {
      pumpReason = "Soil dry,low H2O";
      return false;
    }
  }

  if (soilMoisture <= SOIL_OPTIMAL_MAX)
  {
    pumpReason = "Soil optimal";
    return false;
  }

  if (isSoilWet())
  {
    pumpReason = "Soil wet";
    return false;
  }

  pumpReason = "Conditions not met";
  return false;
}

void sendTelegramMessage(String message, bool isAlert)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Cannot send Telegram: WiFi disconnected");
    return;
  }

  String prefix = isAlert ? "⚠️ ALERT: " : "📱 ";
  String fullMessage = prefix + message + "\n\n⏰ Time: " + getTimeString();

  int attempts = 0;
  bool sent = false;

  while (!sent && attempts < 3)
  {
    if (bot.sendMessage(CHAT_ID, fullMessage, ""))
    {
      Serial.println("Telegram message sent: " + message);
      sent = true;
    }
    else
    {
      Serial.println("Failed to send Telegram message, attempt " + String(attempts + 1));
      delay(1000);
    }
    attempts++;
  }
}

void checkAndReportFaults()
{
  String currentFaults = "";
  bool hasFault = false;

  if (temperature > TEMP_CRITICAL)
  {
    currentFaults += "🔥 CRITICAL TEMPERATURE: " + String(temperature, 1) + "°C\n";
    hasFault = true;
    if (!criticalTempAlertSent)
    {
      sendTelegramMessage("⚠️ CRITICAL TEMPERATURE FAULT!\nTemperature: " + String(temperature, 1) + "°C\nBoth fans activated automatically!", true);
      criticalTempAlertSent = true;
    }
  }
  else if (temperature < 5 && temperature > 0)
  {
    currentFaults += "❄️ VERY LOW TEMPERATURE: " + String(temperature, 1) + "°C\n";
    hasFault = true;
    if (!criticalTempAlertSent)
    {
      sendTelegramMessage("⚠️ LOW TEMPERATURE FAULT!\nTemperature: " + String(temperature, 1) + "°C\nPlant frost risk detected!", true);
      criticalTempAlertSent = true;
    }
  }
  else
  {
    criticalTempAlertSent = false;
  }

  if (waterLevel < WATER_CRITICAL)
  {
    currentFaults += "💧 CRITICAL WATER LEVEL: " + String(waterLevel) + "\n";
    hasFault = true;
    if (!criticalWaterAlertSent)
    {
      sendTelegramMessage("⚠️ CRITICAL WATER FAULT!\nWater level critically low: " + String(waterLevel) + "\nPump emergency stopped!\nPlease refill water tank immediately!", true);
      criticalWaterAlertSent = true;
    }
  }
  else if (waterLevel < WATER_PUMP_OFF)
  {
    currentFaults += "💧 LOW WATER LEVEL: " + String(waterLevel) + "\n";
    hasFault = true;
    if (!criticalWaterAlertSent)
    {
      sendTelegramMessage("⚠️ LOW WATER WARNING!\nWater level low: " + String(waterLevel) + "\nPlease refill water tank soon.", true);
      criticalWaterAlertSent = true;
    }
  }
  else
  {
    criticalWaterAlertSent = false;
  }

  if (soilMoisture > SOIL_VERY_DRY)
  {
    currentFaults += "🌱 SOIL CRITICALLY DRY: " + String(soilMoisture) + "\n";
    hasFault = true;
    if (!criticalSoilAlertSent)
    {
      sendTelegramMessage("⚠️ CRITICAL SOIL FAULT!\nSoil is critically dry!\nMoisture level: " + String(soilMoisture) + "\nPlant may wilt soon!", true);
      criticalSoilAlertSent = true;
    }
  }
  else if (soilMoisture < SOIL_VERY_WET && soilMoisture > 0)
  {
    currentFaults += "💧 SOIL OVERWATERED: " + String(soilMoisture) + "\n";
    hasFault = true;
    if (!criticalSoilAlertSent)
    {
      sendTelegramMessage("⚠️ SOIL OVERWATERING FAULT!\nSoil is too wet!\nMoisture level: " + String(soilMoisture) + "\nRoot rot risk!", true);
      criticalSoilAlertSent = true;
    }
  }
  else
  {
    criticalSoilAlertSent = false;
  }

  if (isHeavyRain)
  {
    currentFaults += "☔ HEAVY RAIN DETECTED!\n";
    hasFault = true;
    if (!heavyRainAlertSent)
    {
      sendTelegramMessage("⚠️ HEAVY RAIN ALERT!\nHeavy rain detected!\nPump automatically disabled.\nSensor value: " + String(rainLevel), true);
      heavyRainAlertSent = true;
    }
  }
  else
  {
    heavyRainAlertSent = false;
  }

  if (pumpStatus && (millis() - pumpStartTime > 1800000))
  {
    currentFaults += "🔄 PUMP TIMEOUT!\n";
    hasFault = true;
    if (!pumpStuckAlertSent)
    {
      sendTelegramMessage("⚠️ PUMP STUCK FAULT!\nPump running for >30 minutes!\nAuto-shutdown activated!\nCheck for leaks or sensor failure!", true);
      pumpStuckAlertSent = true;
    }
  }
  else
  {
    pumpStuckAlertSent = false;
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    currentFaults += "📡 WIFI DISCONNECTED!\n";
    hasFault = true;
    if (!wifiDisconnectAlertSent)
    {
      sendTelegramMessage("⚠️ WIFI DISCONNECTION FAULT!\nWiFi connection lost!\nData upload to ThingSpeak stopped!\nAttempting to reconnect...", true);
      wifiDisconnectAlertSent = true;
    }
  }
  else
  {
    wifiDisconnectAlertSent = false;
  }

  if (isnan(temperature) || isnan(humidity))
  {
    currentFaults += "🌡️ DHT SENSOR FAILURE!\n";
    hasFault = true;
    if (!dhtFaultAlertSent)
    {
      sendTelegramMessage("⚠️ SENSOR FAULT!\nDHT22 sensor not responding!\nTemperature/Humidity readings unavailable!\nCheck sensor connection!", true);
      dhtFaultAlertSent = true;
    }
  }
  else
  {
    dhtFaultAlertSent = false;
  }

  if (hasFault)
  {
    if (activeFaults != currentFaults)
    {
      if (activeFaults != "")
      {
        sendTelegramMessage("📋 FAULT SUMMARY:\n" + currentFaults, true);
      }
      activeFaults = currentFaults;
      faultStartTime = millis();
    }
  }
  else
  {
    if (activeFaults != "")
    {
      sendTelegramMessage("✅ All faults cleared!\nSystem restored to normal operation.", true);
      activeFaults = "";
    }
  }
}

void sendStatusUpdate()
{
  String status = "📊 SYSTEM STATUS REPORT\n\n";
  status += "🌡️ Temperature: " + String(temperature, 1) + "°C\n";
  status += "💧 Humidity: " + String(humidity, 1) + "%\n";
  status += "🌱 Soil Moisture: " + String(soilMoisture) + " (" + getSoilCondition() + ")\n";
  status += "💧 Water Level: " + String(waterLevel);
  if (waterLevel < WATER_CRITICAL)
    status += " (CRITICAL!)";
  else if (waterLevel < WATER_PUMP_OFF)
    status += " (LOW)";
  else
    status += " (OK)";
  status += "\n";
  status += "☀️ Light Level: " + String(lightLevel) + "\n";
  status += "☔ Rain: " + getRainCondition() + " (" + String(rainLevel) + ")\n";
  status += "💧 Pump: " + String(pumpStatus ? "ON" : "OFF");
  if (pumpStatus)
    status += " (" + pumpReason + ")";
  status += "\n";
  status += "🌀 Fan1: " + String(fan1Status ? "ON" : "OFF") + "\n";
  status += "🌀 Fan2: " + String(fan2Status ? "ON" : "OFF") + "\n";
  status += "⚙️ Mode: " + String(manualMode ? "MANUAL" : "AUTO") + "\n";
  status += "🌱 Plant Status: " + getPlantGrowthStatus() + "\n";

  sendTelegramMessage(status, false);
}

void readSensors()
{
  soilMoisture = analogRead(SOIL_PIN);
  lightLevel = analogRead(LDR_PIN);
  rainLevel = analogRead(RAIN_PIN);
  waterLevel = analogRead(WATER_LEVEL_PIN);

  float newTemp = dht.readTemperature();
  float newHum = dht.readHumidity();

  if (!isnan(newTemp))
    temperature = newTemp;
  if (!isnan(newHum))
    humidity = newHum;

  updateRainStatus();
}

void controlActuators()
{
  bool newPumpState;

  if (manualMode)
  {
    newPumpState = manualPumpRequest;
    if (newPumpState != pumpStatus)
      pumpReason = manualPumpRequest ? "Manual ON" : "Manual OFF";
  }
  else
  {
    newPumpState = shouldPumpRun();
  }

  if (newPumpState != pumpStatus)
  {
    pumpStatus = newPumpState;
    if (pumpStatus)
    {
      pumpStartTime = millis();
      Serial.print("💧 Pump ON - ");
      Serial.println(pumpReason);
      sendTelegramMessage("💧 PUMP TURNED ON\nReason: " + pumpReason, false);
    }
    else
    {
      Serial.print("💧 Pump OFF - ");
      Serial.println(pumpReason);
      if (pumpReason != "Conditions not met" && pumpReason != "Soil optimal" && pumpReason != "Soil wet")
      {
        sendTelegramMessage("💧 PUMP TURNED OFF\nReason: " + pumpReason, false);
      }
    }
  }

  if (pumpStatus && (millis() - pumpStartTime > 1800000))
  {
    pumpStatus = false;
    digitalWrite(PUMP_RELAY, HIGH);
    Serial.println("⚠️ Pump timeout - Auto OFF after 30min");
    pumpReason = "Timeout";
    sendTelegramMessage("⚠️ PUMP AUTO-SHUTDOWN\nPump ran for 30 minutes!\nCheck for leaks or sensor issues!", true);
  }

  // SAFETY: Only write to relay if system is ready (otherwise keep OFF)
  if (systemReady)
  {
    digitalWrite(PUMP_RELAY, pumpStatus ? LOW : HIGH);
  }
  else
  {
    digitalWrite(PUMP_RELAY, HIGH); // force OFF during startup
  }

  bool newFan1 = fan1Status;
  if (temperature > TEMP_FAN1_ON)
    newFan1 = true;
  else if (temperature < TEMP_FAN1_OFF)
    newFan1 = false;

  if (newFan1 != fan1Status)
  {
    fan1Status = newFan1;
    digitalWrite(FAN1_RELAY, fan1Status ? LOW : HIGH);
    Serial.print("🌀 Fan1: ");
    Serial.println(fan1Status ? "ON" : "OFF");
    if (fan1Status)
    {
      sendTelegramMessage("🌀 FAN 1 ACTIVATED\nTemperature: " + String(temperature, 1) + "°C", false);
    }
  }

  bool newFan2 = fan2Status;
  if (temperature > TEMP_FAN2_ON)
    newFan2 = true;
  else if (temperature < TEMP_FAN2_OFF)
    newFan2 = false;

  if (newFan2 != fan2Status)
  {
    fan2Status = newFan2;
    digitalWrite(FAN2_RELAY, fan2Status ? LOW : HIGH);
    Serial.print("🌀🌀 Fan2: ");
    Serial.println(fan2Status ? "ON" : "OFF");
    if (fan2Status)
    {
      sendTelegramMessage("🌀🌀 FAN 2 ACTIVATED\nTemperature: " + String(temperature, 1) + "°C", false);
    }
  }

  if (temperature > TEMP_CRITICAL)
  {
    if (!fan1Status)
    {
      fan1Status = true;
      digitalWrite(FAN1_RELAY, LOW);
    }
    if (!fan2Status)
    {
      fan2Status = true;
      digitalWrite(FAN2_RELAY, LOW);
    }
    Serial.println("🌡️ CRITICAL TEMP - Both fans ON");
  }
}

void checkButton()
{
  static unsigned long pressStart = 0;
  static bool buttonWasPressed = false;
  static bool longPressHandled = false;

  bool buttonIsPressed = (digitalRead(MANUAL_BUTTON) == LOW);

  if (buttonIsPressed && !buttonWasPressed)
  {
    pressStart = millis();
    longPressHandled = false;
    buttonWasPressed = true;
  }
  else if (!buttonIsPressed && buttonWasPressed)
  {
    unsigned long pressDuration = millis() - pressStart;
    if (pressDuration < 2000 && !longPressHandled)
    {
      manualMode = !manualMode;
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print(PROJECT_NAME);
      lcd.print(" ");
      lcd.print(VERSION);
      lcd.setCursor(0, 1);
      lcd.print("Mode: ");
      lcd.print(manualMode ? "MANUAL" : "AUTO");
      delay(1000);
      lcd.clear();
      lcdScreen = -1;
      sendTelegramMessage("🔄 SYSTEM MODE CHANGED\nMode: " + String(manualMode ? "MANUAL" : "AUTO"), false);
    }
    buttonWasPressed = false;
  }

  if (manualMode && buttonIsPressed && !longPressHandled && (millis() - pressStart >= 2000))
  {
    longPressHandled = true;
    manualPumpRequest = !manualPumpRequest;
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(PROJECT_NAME);
    lcd.print(" ");
    lcd.print(VERSION);
    lcd.setCursor(0, 1);
    lcd.print("Pump ");
    lcd.print(manualPumpRequest ? "ON" : "OFF");
    delay(1000);
    lcd.clear();
    lcdScreen = -1;
    sendTelegramMessage("💧 MANUAL PUMP CONTROL\nPump: " + String(manualPumpRequest ? "ON" : "OFF"), false);
  }
}

void updateLCD()
{
  if (millis() - lastLCDUpdate < LCD_UPDATE_INTERVAL)
    return;
  lastLCDUpdate = millis();

  lcdScreen = (lcdScreen + 1) % TOTAL_SCREENS;
  lcd.clear();

  switch (lcdScreen)
  {
  case 0:
    lcd.setCursor(0, 0);
    lcd.print(PROJECT_NAME);
    lcd.print(" ");
    lcd.print(VERSION);
    lcd.setCursor(0, 1);
    lcd.print("Uptime: ");
    lcd.print(getTimeString());
    break;

  case 1:
    lcd.setCursor(0, 0);
    lcd.print("Temp:");
    lcd.setCursor(0, 1);
    lcd.print(temperature, 1);
    lcd.print("C ");
    lcd.print(getTimeString());
    break;

  case 2:
    lcd.setCursor(0, 0);
    lcd.print("Humidity:");
    lcd.setCursor(0, 1);
    lcd.print(humidity, 1);
    lcd.print("%");
    break;

  case 3:
    lcd.setCursor(0, 0);
    lcd.print("Water:");
    lcd.setCursor(0, 1);
    lcd.print(waterLevel);
    lcd.print(" (");
    if (waterLevel > WATER_PUMP_ON)
      lcd.print("HIGH");
    else if (waterLevel < WATER_PUMP_OFF)
      lcd.print("LOW");
    else
      lcd.print("MID");
    lcd.print(")");
    break;

  case 4:
    lcd.setCursor(0, 0);
    lcd.print("Soil:");
    lcd.print(soilMoisture);
    lcd.print(" ");
    lcd.print(getSoilCondition());
    lcd.setCursor(0, 1);
    lcd.print("Rain:");
    lcd.print(getRainCondition());
    lcd.print(" ");
    lcd.print(rainLevel);
    break;

  case 5:
    lcd.setCursor(0, 0);
    lcd.print("Plant: ");
    {
      String plantStat = getPlantGrowthStatus();
      lcd.print(plantStat);
      for (int i = plantStat.length(); i < 10; i++)
        lcd.print(" ");
    }
    lcd.setCursor(0, 1);
    lcd.print("Soil:");
    lcd.print(soilMoisture);
    lcd.print(" L:");
    lcd.print(lightLevel);
    break;

  case 6:
    lcd.setCursor(0, 0);
    lcd.print("P:");
    lcd.print(pumpStatus ? "ON " : "OFF");
    lcd.print("F1:");
    lcd.print(fan1Status ? "ON " : "OFF");
    lcd.print("F2:");
    lcd.print(fan2Status ? "ON" : "OFF");
    lcd.print(" M:");
    lcd.print(manualMode ? "M" : "A");
    lcd.setCursor(0, 1);
    lcd.print("Light:");
    lcd.print(lightLevel);
    break;
  }
}

void printData()
{
  Serial.println("\n=== " + String(PROJECT_NAME) + " " + String(VERSION) + " ===");
  Serial.print("Uptime: ");
  Serial.println(getTimeString());
  Serial.print("Temp: ");
  Serial.print(temperature);
  Serial.println(" C");
  Serial.print("Humidity: ");
  Serial.print(humidity);
  Serial.println(" %");
  Serial.print("Soil: ");
  Serial.print(soilMoisture);
  Serial.print(" (");
  Serial.print(getSoilCondition());
  Serial.println(")");
  Serial.print("Light: ");
  Serial.println(lightLevel);
  Serial.print("Water: ");
  Serial.println(waterLevel);
  Serial.print("Rain: ");
  Serial.print(rainLevel);
  Serial.print(" - ");
  Serial.println(getRainCondition());

  Serial.println("\n💧 PUMP:");
  Serial.print("  State: ");
  Serial.println(pumpStatus ? "ON" : "OFF");
  Serial.print("  Reason: ");
  Serial.println(pumpReason);

  Serial.println("\n🌱 PLANT:");
  Serial.print("  Growth: ");
  Serial.println(getPlantGrowthStatus());

  Serial.println("\n⚙️ SYSTEM:");
  Serial.print("Fan1: ");
  Serial.println(fan1Status ? "ON" : "OFF");
  Serial.print("Fan2: ");
  Serial.println(fan2Status ? "ON" : "OFF");
  Serial.print("Mode: ");
  Serial.println(manualMode ? "MANUAL" : "AUTO");
  Serial.print("WiFi: ");
  Serial.println(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
  Serial.print("System ready: ");
  Serial.println(systemReady ? "YES" : "NO");
  Serial.println("================================");
}

void sendToThingSpeak()
{
  if (WiFi.status() != WL_CONNECTED)
    return;

  HTTPClient http;
  http.setTimeout(5000);

  String url = String(server) +
               "?api_key=" + THINGSPEAK_WRITE_KEY +
               "&field1=" + String(temperature) +
               "&field2=" + String(humidity) +
               "&field3=" + String(soilMoisture) +
               "&field4=" + String(lightLevel) +
               "&field5=" + String(rainLevel) +
               "&field6=" + String(waterLevel) +
               "&field7=" + String(pumpStatus ? 1 : 0) +
               "&field8=" + String(isRaining ? 1 : 0);

  http.begin(url);
  int httpCode = http.GET();

  if (httpCode == 200)
    Serial.println("✓ Data sent to ThingSpeak");
  else
    Serial.println("✗ ThingSpeak upload failed");

  http.end();
}

void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("Smart Farming System Starting...");

  // SAFETY: Force pump OFF immediately
  pinMode(PUMP_RELAY, OUTPUT);
  digitalWrite(PUMP_RELAY, HIGH);
  delay(100); // extra safety delay

  pinMode(FAN1_RELAY, OUTPUT);
  pinMode(FAN2_RELAY, OUTPUT);
  pinMode(MANUAL_BUTTON, INPUT_PULLUP);
  pinMode(WIFI_STATUS_LED, OUTPUT);

  digitalWrite(FAN1_RELAY, HIGH);
  digitalWrite(FAN2_RELAY, HIGH);
  digitalWrite(WIFI_STATUS_LED, LOW);

  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();

  lcd.setCursor(0, 0);
  lcd.print(PROJECT_NAME);
  lcd.print(" ");
  lcd.print(VERSION);
  lcd.setCursor(0, 1);
  lcd.print("Initializing...");
  delay(2000);

  dht.begin();

  secured_client.setInsecure();

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(PROJECT_NAME);
  lcd.print(" ");
  lcd.print(VERSION);
  lcd.setCursor(0, 1);
  lcd.print("WiFi Connecting");

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20)
  {
    Serial.print(".");
    lcd.setCursor(attempts % 16, 1);
    lcd.print(".");
    delay(500);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\nWiFi Connected!");
    digitalWrite(WIFI_STATUS_LED, HIGH);
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(PROJECT_NAME);
    lcd.print(" ");
    lcd.print(VERSION);
    lcd.setCursor(0, 1);
    lcd.print("WiFi OK");
    delay(1500);

    sendTelegramMessage("🌱 SMART FARM SYSTEM STARTED!\n\nSystem online and monitoring.\nVersion: " + String(VERSION) + "\nMode: AUTO", false);
  }
  else
  {
    Serial.println("\nWiFi Failed!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(PROJECT_NAME);
    lcd.print(" ");
    lcd.print(VERSION);
    lcd.setCursor(0, 1);
    lcd.print("WiFi Failed");
    delay(1500);
  }

  lcd.clear();
  Serial.println("System Ready!");
  Serial.println("Pump control: SOIL + WATER + RAIN");
  Serial.println("Rain sensor: 3500=clear, 1000=raining");
  Serial.println("Telegram alerts enabled");

  // SAFETY: Wait before enabling pump control
  Serial.print("Waiting ");
  Serial.print(SAFETY_STARTUP_DELAY / 1000);
  Serial.println(" seconds before enabling pump...");
  delay(SAFETY_STARTUP_DELAY);
  systemReady = true;
  Serial.println("Pump control enabled.");
}

void loop()
{
  if (millis() - lastSensorRead > SENSOR_READ_INTERVAL)
  {
    lastSensorRead = millis();
    readSensors();
    checkButton();
    controlActuators();
    printData();
  }

  updateLCD();

  if (millis() - lastTelegramAlert > TELEGRAM_ALERT_INTERVAL)
  {
    lastTelegramAlert = millis();
    checkAndReportFaults();
  }

  if (millis() - lastTelegramStatus > TELEGRAM_STATUS_INTERVAL)
  {
    lastTelegramStatus = millis();
    sendStatusUpdate();
  }

  if (millis() - lastUpload > UPLOAD_INTERVAL)
  {
    lastUpload = millis();
    sendToThingSpeak();
  }

  delay(50);
}
