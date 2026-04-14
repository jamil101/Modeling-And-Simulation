#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ----------- PIN DEFINITIONS -----------
#define DHTPIN 13
#define DHTTYPE DHT22
#define SOIL_PIN 34
#define LDR_PIN 35
#define RAIN_PIN 32
#define WATER_LEVEL_PIN 33
#define PUMP_RELAY 16
#define FAN1_RELAY 26 // Fan 1 - 22°C
#define FAN2_RELAY 27 // Fan 2 - 24°C
#define MANUAL_BUTTON 4
#define WIFI_STATUS_LED 2

// ----------- PROJECT INFO -----------
#define PROJECT_NAME "SmartFarm"
#define VERSION "v3.4"

// ----------- TEMPERATURE THRESHOLDS -----------
#define TEMP_FAN1_ON 22
#define TEMP_FAN1_OFF 18
#define TEMP_FAN2_ON 24
#define TEMP_FAN2_OFF 20
#define TEMP_CRITICAL 35

// ----------- WATER LEVEL THRESHOLDS -----------
#define WATER_PUMP_ON 1200 // Turn pump ON when water level > this
#define WATER_PUMP_OFF 600 // Turn pump OFF when water level < this
#define WATER_CRITICAL 400 // Critical water level - emergency stop

// ----------- SOIL MOISTURE THRESHOLDS -----------
// For capacitive sensor: higher value = drier soil
#define SOIL_VERY_DRY 3000    // Above this - critical dry
#define SOIL_DRY 2500         // 2500-3000 - needs water
#define SOIL_OPTIMAL_MAX 2200 // Below 2200 - optimal moisture
#define SOIL_WET 1800         // 1800-2200 - slightly wet
#define SOIL_VERY_WET 1200    // Below 1200 - too wet

// ----------- RAIN THRESHOLDS (INVERSE LOGIC) -----------
// Sensor: 3500 = clear/dry, 1000 = raining/wet
#define RAIN_CLEAR 3000 // Above 3000 = no rain, clear sky
#define RAIN_LIGHT 2000 // 1000-2000 = light rain
#define RAIN_HEAVY 1000 // Below 1000 = heavy rain

// Pump control thresholds
#define RAIN_THRESHOLD 2000    // Below this = raining
#define RAIN_HEAVY_THRESH 1000 // Below this = heavy rain

// ----------- THINGSPEAK -----------
#define THINGSPEAK_WRITE_KEY "NC47N46ACJUARDCK"
const char *server = "http://api.thingspeak.com/update";
const char *ssid = "Kuet21";
const char *password = "89777145!!";

// ----------- TIMING -----------
#define UPLOAD_INTERVAL 30000
#define SENSOR_READ_INTERVAL 2000
#define LCD_UPDATE_INTERVAL 2000

// ----------- OBJECTS -----------
DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ----------- VARIABLES -----------
unsigned long lastUpload = 0;
unsigned long lastSensorRead = 0;
unsigned long lastLCDUpdate = 0;
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

// Pump control reasons
String pumpReason = "";

// ----------- GET CURRENT TIME (uptime) -----------
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

// ----------- CHECK RAIN STATUS (Fixed for inverse sensor) -----------
void updateRainStatus()
{
  // Sensor gives HIGH value when dry/clear, LOW value when wet/raining
  bool wasRaining = isRaining;
  bool wasHeavyRain = isHeavyRain;

  isHeavyRain = (rainLevel < RAIN_HEAVY_THRESH); // Below 1000 = heavy rain
  isRaining = (rainLevel < RAIN_THRESHOLD);      // Below 2000 = raining

  // If it's heavy rain, definitely raining
  if (isHeavyRain)
    isRaining = true;

  // Only print when status changes
  if (isRaining != wasRaining || isHeavyRain != wasHeavyRain)
  {
    if (isHeavyRain)
      Serial.print("☔☔ HEAVY RAIN DETECTED! (Sensor: ");
    else if (isRaining)
      Serial.print("☔ LIGHT RAIN DETECTED! (Sensor: ");
    else
      Serial.print("☀️ CLEAR / NO RAIN (Sensor: ");

    Serial.print(rainLevel);
    Serial.println(")");
  }
}

// ----------- GET SOIL CONDITION -----------
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

// ----------- GET RAIN CONDITION STRING -----------
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

// ----------- CHECK IF PUMP SHOULD RUN -----------
bool shouldPumpRun()
{
  // EMERGENCY: Water level critical - NEVER pump
  if (waterLevel < WATER_CRITICAL)
  {
    pumpReason = "EMERGENCY:Low water";
    return false;
  }

  // Manual mode override
  if (manualMode)
    return manualPumpRequest;

  // HEAVY RAIN (sensor < 1000): Never pump during heavy rain
  if (isHeavyRain)
  {
    pumpReason = "Heavy rain";
    return false;
  }

  // LIGHT RAIN (sensor 1000-2000): Only pump if soil is very dry
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

  // NO RAIN: Normal operation based on soil moisture

  // Soil is very dry - MUST pump if water available
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

  // Soil is dry - pump if water level is good
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

  // Soil is optimal or wet - don't pump
  if (soilMoisture <= SOIL_OPTIMAL_MAX)
  {
    pumpReason = "Soil optimal";
    return false;
  }

  // Soil is wet - definitely don't pump
  if (isSoilWet())
  {
    pumpReason = "Soil wet";
    return false;
  }

  pumpReason = "Conditions not met";
  return false;
}

// ----------- PLANT GROWTH STATUS (based on soil & light) -----------
String getPlantGrowthStatus()
{
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

// ----------- SETUP -----------
void setup()
{
  Serial.begin(115200);
  delay(1000);

  Serial.println("Smart Farming System Starting...");

  // Initialize I2C and LCD
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

  // Initialize components
  dht.begin();

  pinMode(PUMP_RELAY, OUTPUT);
  pinMode(FAN1_RELAY, OUTPUT);
  pinMode(FAN2_RELAY, OUTPUT);
  pinMode(MANUAL_BUTTON, INPUT_PULLUP);
  pinMode(WIFI_STATUS_LED, OUTPUT);

  // Turn off all relays (active LOW)
  digitalWrite(PUMP_RELAY, HIGH);
  digitalWrite(FAN1_RELAY, HIGH);
  digitalWrite(FAN2_RELAY, HIGH);
  digitalWrite(WIFI_STATUS_LED, LOW);

  // Connect WiFi
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
}

// ----------- READ SENSORS -----------
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

// ----------- CONTROL ACTUATORS -----------
void controlActuators()
{
  // === PUMP CONTROL ===
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

  // Apply pump state change
  if (newPumpState != pumpStatus)
  {
    pumpStatus = newPumpState;
    if (pumpStatus)
    {
      pumpStartTime = millis();
      Serial.print("💧 Pump ON - ");
      Serial.println(pumpReason);
    }
    else
    {
      Serial.print("💧 Pump OFF - ");
      Serial.println(pumpReason);
    }
  }

  // Pump runtime protection (max 30 minutes)
  if (pumpStatus && (millis() - pumpStartTime > 1800000))
  {
    pumpStatus = false;
    digitalWrite(PUMP_RELAY, HIGH);
    Serial.println("⚠️ Pump timeout - Auto OFF after 30min");
    pumpReason = "Timeout";
  }

  // Apply pump state (active LOW)
  digitalWrite(PUMP_RELAY, pumpStatus ? LOW : HIGH);

  // === FAN 1 CONTROL ===
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
  }

  // === FAN 2 CONTROL ===
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
  }

  // Critical temperature override
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

// ----------- CHECK BUTTON -----------
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
  }
}

// ----------- UPDATE LCD -----------
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
    lcd.print("Plant:");
    lcd.print(getPlantGrowthStatus());
    lcd.setCursor(0, 1);
    lcd.print("Pump:");
    lcd.print(pumpStatus ? "ON " : "OFF");
    lcd.print(" ");
    if (pumpReason.length() > 8)
      lcd.print(pumpReason.substring(0, 8));
    else
      lcd.print(pumpReason);
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

// ----------- SERIAL OUTPUT -----------
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
  Serial.println("================================");
}

// ----------- SEND TO THINGSPEAK -----------
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

// ----------- LOOP -----------
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

  if (millis() - lastUpload > UPLOAD_INTERVAL)
  {
    lastUpload = millis();
    sendToThingSpeak();
  }

  delay(50);
}
