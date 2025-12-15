#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ---------- TFT pin definitions ----------
#define TFT_DC 16
#define TFT_CS 17
#define TFT_SCLK 18
#define TFT_MOSI 23
#define TFT_RST 5
#define TFT_BKL 32

// ---------- Backlight PWM ----------
#define TFT_BKL_CH 0
#define TFT_BKL_FREQ 5000
#define TFT_BKL_RES 8

// ---------- 5-way switch pins ----------
#define SW_UP 36
#define SW_DOWN 35
#define SW_LEFT 13
#define SW_RIGHT 39
#define SW_CENTER 34

#define TFT_WIDTH 240
#define TFT_HEIGHT 240

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// ---------- WiFi and MQTT Configuration ----------
const char *ssid = "Xiaomi 14T";
const char *password = "password";
const char *mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char *topic_sensor_json = "enviroscout/data";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ---------- Sensor data ----------
float temperature = 25.3;
float humidity = 65.2;
float pressure = 1013.25;
uint32_t gas_resistance = 125000;
float altitude = 150.5;
float tvoc = 450.0;
float eco2 = 800.0;

// ---------- Enhanced UI variables ----------
int scrollOffset = 0;
int targetScrollOffset = 0;
int cardHeight = 36;
int cardSpacing = 8;
int topMargin = 45;
int maxScroll = 0;
int totalContentHeight = 0;

// Smooth scrolling
float smoothScroll = 0;
const float scrollSmoothing = 0.3;

// ---------- Status variables ----------
bool wifiConnected = false;
bool mqttConnected = false;

// ---------- RTOS Handles ----------
TaskHandle_t TaskDisplay;
TaskHandle_t TaskMQTT;
SemaphoreHandle_t sensorDataMutex;
volatile bool needsRedraw = false;

// ---------- Color Palette ----------
#define COLOR_BG 0x1082     // Dark blue-gray
#define COLOR_CARD 0x2124   // Slightly lighter card background
#define COLOR_ACCENT 0x05FF // Cyan accent
#define COLOR_TEXT_PRIMARY 0xFFFF
#define COLOR_TEXT_SECONDARY 0x8410
#define COLOR_TEMP 0xFFFF  // Red
#define COLOR_HUMID 0x07FF // Cyan
#define COLOR_PRESS 0xFFE0 // Yellow
#define COLOR_GAS 0xF81F   // Magenta
#define COLOR_ALT 0x07E0   // Green

// -----------------------------------------------------------------------------
// Backlight
// -----------------------------------------------------------------------------
void setBacklight(uint8_t level)
{
  ledcWrite(TFT_BKL_CH, level);
}

void backlightSetup()
{
  ledcSetup(TFT_BKL_CH, TFT_BKL_FREQ, TFT_BKL_RES);
  ledcAttachPin(TFT_BKL, TFT_BKL_CH);
  setBacklight(200);
}

// -----------------------------------------------------------------------------
// WiFi
// -----------------------------------------------------------------------------
void setupWiFi()
{
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    wifiConnected = true;
    Serial.println("\nWiFi connected");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  }
}

// -----------------------------------------------------------------------------
// MQTT Callback
// -----------------------------------------------------------------------------
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  String jsonString;
  for (unsigned int i = 0; i < length; i++)
  {
    jsonString += (char)payload[i];
  }

  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, jsonString);

  if (!error && xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE)
  {
    temperature = doc["temperature"] | temperature;
    humidity = doc["humidity"] | humidity;
    pressure = doc["pressure"] | pressure;
    gas_resistance = doc["gas_resistance"] | gas_resistance;
    altitude = doc["altitude"] | altitude;
    tvoc = doc["tvoc"] | tvoc;
    eco2 = doc["eco2"] | eco2;

    xSemaphoreGive(sensorDataMutex);
    needsRedraw = true;
  }
}

// -----------------------------------------------------------------------------
// MQTT Connection
// -----------------------------------------------------------------------------
bool reconnectMQTT()
{
  if (mqttClient.connect("ESP32_Display_Client"))
  {
    mqttConnected = true;
    Serial.println("MQTT Connected");
    mqttClient.subscribe(topic_sensor_json);
    needsRedraw = true;
    return true;
  }
  mqttConnected = false;
  needsRedraw = true;
  return false;
}

// -----------------------------------------------------------------------------
// Enhanced Drawing Functions
// -----------------------------------------------------------------------------

// Draw rounded rectangle
void drawRoundRect(int x, int y, int w, int h, int r, uint16_t color)
{
  tft.fillRect(x + r, y, w - 2 * r, h, color);
  tft.fillRect(x, y + r, w, h - 2 * r, color);
  tft.fillCircle(x + r, y + r, r, color);
  tft.fillCircle(x + w - r - 1, y + r, r, color);
  tft.fillCircle(x + r, y + h - r - 1, r, color);
  tft.fillCircle(x + w - r - 1, y + h - r - 1, r, color);
}

// Draw modern status indicator
void drawStatusIndicator(int x, int y, bool connected, const char *label)
{
  uint16_t color = connected ? COLOR_ACCENT : 0x7BEF;
  tft.fillCircle(x, y, 4, color);
  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT_SECONDARY);
  tft.setCursor(x - 10, y + 8);
  tft.print(label);
}

// Draw sensor icon with modern style
void drawModernTempIcon(int x, int y)
{
  tft.fillRoundRect(x, y, 12, 20, 2, COLOR_TEMP);
  tft.fillCircle(x + 6, y + 16, 4, COLOR_TEMP);
  tft.fillRect(x + 4, y + 6, 4, 8, COLOR_BG);
}

void drawModernHumidIcon(int x, int y)
{
  for (int i = 0; i < 3; i++)
  {
    tft.fillCircle(x + 6, y + 12 - i, 6 - i * 2, COLOR_HUMID);
  }
  tft.fillTriangle(x + 6, y, x, y + 8, x + 12, y + 8, COLOR_HUMID);
}

void drawModernPressIcon(int x, int y)
{
  tft.drawCircle(x + 6, y + 8, 7, COLOR_PRESS);
  tft.drawCircle(x + 6, y + 8, 5, COLOR_PRESS);
  tft.fillCircle(x + 6, y + 8, 2, COLOR_PRESS);
  tft.drawLine(x + 6, y + 8, x + 11, y + 4, COLOR_PRESS);
}

void drawModernGasIcon(int x, int y)
{
  for (int i = 0; i < 4; i++)
  {
    tft.fillCircle(x + 3 + i * 2, y + 8 - abs(i - 1.5) * 2, 2, COLOR_GAS);
  }
  tft.fillRect(x + 2, y + 8, 9, 4, COLOR_GAS);
}

void drawModernAltIcon(int x, int y)
{
  tft.fillTriangle(x, y + 12, x + 6, y, x + 12, y + 12, COLOR_ALT);
  tft.fillRect(x, y + 12, 12, 3, COLOR_ALT);
}

// Draw enhanced bar with gradient effect
void drawEnhancedBar(int x, int y, int w, int h, float value, float minVal, float maxVal, uint16_t color)
{
  // Background with border
  tft.fillRoundRect(x, y, w, h, 3, 0x2104);
  tft.drawRoundRect(x, y, w, h, 3, 0x4208);

  // Calculate fill
  int fillWidth = map(constrain(value, minVal, maxVal), minVal, maxVal, 0, w - 4);
  if (fillWidth > 4)
  {
    tft.fillRoundRect(x + 2, y + 2, fillWidth, h - 4, 2, color);
  }
}

// Draw sensor card with modern design
void drawSensorCard(int y, const char *label, float value, const char *unit,
                    void (*iconFunc)(int, int), uint16_t color,
                    float minVal, float maxVal)
{
  int cardX = 8;
  int cardW = TFT_WIDTH - 16;

  // Card background with shadow effect
  drawRoundRect(cardX + 2, y + 2, cardW, cardHeight, 6, 0x0000);
  drawRoundRect(cardX, y, cardW, cardHeight, 6, COLOR_CARD);

  // Draw icon
  iconFunc(cardX + 8, y + 10);

  // Label
  tft.setTextSize(1);
  tft.setTextColor(COLOR_TEXT_SECONDARY);
  tft.setCursor(cardX + 28, y + 6);
  tft.print(label);

  // Value
  tft.setTextSize(2);
  tft.setTextColor(color);
  tft.setCursor(cardX + 28, y + 16);

  char valStr[16];
  if (strcmp(unit, "k") == 0)
  {
    sprintf(valStr, "%.0f", value / 1000);
  }
  else if (strcmp(unit, "hPa") == 0)
  {
    sprintf(valStr, "%.0f", value);
  }
  else
  {
    sprintf(valStr, "%.1f", value);
  }
  tft.print(valStr);
  tft.print(" ");
  tft.setTextColor(COLOR_TEXT_SECONDARY);
  tft.print(unit);

  // Progress bar
  int barX = cardX + 135;
  int barW = 70;
  drawEnhancedBar(barX, y + 12, barW, 12, value, minVal, maxVal, color);
}

// -----------------------------------------------------------------------------
// Main Display Drawing
// -----------------------------------------------------------------------------
void drawEnhancedSensorData()
{
  tft.fillScreen(COLOR_BG);

  // Modern header with gradient
  for (int i = 0; i < topMargin - 5; i++)
  {
    uint16_t gradColor = tft.color565(0, 60 + i, 100 + i);
    tft.drawFastHLine(0, i, TFT_WIDTH, gradColor);
  }

  // Header title
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(12, 12);
  tft.print("Sensor Monitor");

  // Status indicators
  drawStatusIndicator(TFT_WIDTH - 50, 18, wifiConnected, "WiFi");
  drawStatusIndicator(TFT_WIDTH - 20, 18, mqttConnected, "MQTT");

  // Separator line
  tft.drawFastHLine(0, topMargin - 5, TFT_WIDTH, COLOR_ACCENT);

  // Calculate scrolling
  int numCards = 5;
  totalContentHeight = numCards * (cardHeight + cardSpacing) + 10;
  int visibleHeight = TFT_HEIGHT - topMargin;
  maxScroll = max(0, totalContentHeight - visibleHeight);
  scrollOffset = constrain(scrollOffset, 0, maxScroll);

  // Smooth scrolling animation
  smoothScroll += (scrollOffset - smoothScroll) * scrollSmoothing;

  // Lock and draw sensor cards
  if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE)
  {
    int y = topMargin + 5 - (int)smoothScroll;

    if (y > topMargin - cardHeight && y < TFT_HEIGHT)
    {
      drawSensorCard(y, "Temperature", temperature, "C",
                     drawModernTempIcon, COLOR_TEMP, 0, 50);
    }
    y += cardHeight + cardSpacing;

    if (y > topMargin - cardHeight && y < TFT_HEIGHT)
    {
      drawSensorCard(y, "Humidity", humidity, "%",
                     drawModernHumidIcon, COLOR_HUMID, 0, 100);
    }
    y += cardHeight + cardSpacing;

    if (y > topMargin - cardHeight && y < TFT_HEIGHT)
    {
      drawSensorCard(y, "Pressure", pressure, "hPa",
                     drawModernPressIcon, COLOR_PRESS, 950, 1050);
    }
    y += cardHeight + cardSpacing;

    if (y > topMargin - cardHeight && y < TFT_HEIGHT)
    {
      drawSensorCard(y, "Gas", gas_resistance, "k",
                     drawModernGasIcon, COLOR_GAS, 0, 300000);
    }
    y += cardHeight + cardSpacing;

    if (y > topMargin - cardHeight && y < TFT_HEIGHT)
    {
      drawSensorCard(y, "Altitude", altitude, "m",
                     drawModernAltIcon, COLOR_ALT, 0, 500);
    }

    xSemaphoreGive(sensorDataMutex);
  }

  // Modern scroll indicator
  if (maxScroll > 0)
  {
    int scrollBarH = max(10, (visibleHeight * visibleHeight) / totalContentHeight);
    int scrollBarY = topMargin + (scrollOffset * (visibleHeight - scrollBarH)) / maxScroll;

    tft.fillRoundRect(TFT_WIDTH - 6, scrollBarY, 4, scrollBarH, 2, COLOR_ACCENT);
  }
}

// -----------------------------------------------------------------------------
// RTOS Task: Display & Input
// -----------------------------------------------------------------------------
void TaskDisplayCode(void *parameter)
{
  Serial.println("Display Task on core " + String(xPortGetCoreID()));

  bool lastUpState = false;
  bool lastDownState = false;

  for (;;)
  {
    bool up = (digitalRead(SW_UP) == LOW);
    bool down = (digitalRead(SW_DOWN) == LOW);
    bool center = (digitalRead(SW_CENTER) == LOW);

    bool upPressed = up && !lastUpState;
    bool downPressed = down && !lastDownState;

    lastUpState = up;
    lastDownState = down;

    if (upPressed && scrollOffset > 0)
    {
      targetScrollOffset = scrollOffset - (cardHeight + cardSpacing);
      scrollOffset = max(0, targetScrollOffset);
      needsRedraw = true;
      Serial.println("Scroll UP");
    }
    else if (downPressed && scrollOffset < maxScroll)
    {
      targetScrollOffset = scrollOffset + (cardHeight + cardSpacing);
      scrollOffset = min(maxScroll, targetScrollOffset);
      needsRedraw = true;
      Serial.println("Scroll DOWN");
    }
    else if (center)
    {
      scrollOffset = 0;
      smoothScroll = 0;
      needsRedraw = true;
      Serial.println("Reset");
      vTaskDelay(300 / portTICK_PERIOD_MS);
    }

    // Always update for smooth animation
    if (abs(smoothScroll - scrollOffset) > 0.5 || needsRedraw)
    {
      drawEnhancedSensorData();
      needsRedraw = false;
    }

    vTaskDelay(20 / portTICK_PERIOD_MS); // 50 FPS
  }
}

// -----------------------------------------------------------------------------
// RTOS Task: MQTT
// -----------------------------------------------------------------------------
void TaskMQTTCode(void *parameter)
{
  Serial.println("MQTT Task on core " + String(xPortGetCoreID()));

  unsigned long lastReconnect = 0;

  for (;;)
  {
    if (wifiConnected)
    {
      if (!mqttClient.connected())
      {
        unsigned long now = millis();
        if (now - lastReconnect > 5000)
        {
          lastReconnect = now;
          Serial.println("Reconnecting MQTT...");
          if (reconnectMQTT())
          {
            lastReconnect = 0;
          }
        }
      }
      else
      {
        mqttClient.loop();
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// -----------------------------------------------------------------------------
// Setup
// -----------------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);
  Serial.println("Enhanced Sensor Display Starting...");

  pinMode(SW_UP, INPUT_PULLUP);
  pinMode(SW_DOWN, INPUT_PULLUP);
  pinMode(SW_LEFT, INPUT_PULLUP);
  pinMode(SW_RIGHT, INPUT_PULLUP);
  pinMode(SW_CENTER, INPUT_PULLUP);

  tft.init(TFT_WIDTH, TFT_HEIGHT);
  tft.setRotation(90);
  tft.fillScreen(COLOR_BG);

  backlightSetup();

  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(40, 100);
  tft.print("Connecting...");

  sensorDataMutex = xSemaphoreCreateMutex();

  setupWiFi();

  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);

  if (wifiConnected)
  {
    reconnectMQTT();
  }

  drawEnhancedSensorData();

  xTaskCreatePinnedToCore(TaskDisplayCode, "Display", 10000, NULL, 1, &TaskDisplay, 1);
  xTaskCreatePinnedToCore(TaskMQTTCode, "MQTT", 10000, NULL, 1, &TaskMQTT, 0);

  Serial.println("Setup complete!");
}

void loop()
{
  vTaskDelay(1000 / portTICK_PERIOD_MS);
}