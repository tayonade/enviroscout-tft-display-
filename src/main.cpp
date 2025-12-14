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
#define TFT_BKL_RES 8 // 0..255

// ---------- 5-way switch pins (active LOW) ----------
#define SW_UP 36
#define SW_DOWN 35
#define SW_LEFT 13
#define SW_RIGHT 39
#define SW_CENTER 34

// Change to your actual ST7789 resolution
#define TFT_WIDTH 240
#define TFT_HEIGHT 240

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// ---------- WiFi and MQTT Configuration ----------
const char *ssid = "Xiaomi 14T";               // Change this
const char *password = "password";             // Change this
const char *mqtt_server = "broker.hivemq.com"; // Change to your MQTT broker
const int mqtt_port = 1883;

// MQTT Topics - adjust these to match your sensor publisher
// const char *topic_temperature = "sensor/temperature";
// const char *topic_humidity = "sensor/humidity";
// const char *topic_pressure = "sensor/pressure";
// const char *topic_gas = "sensor/gas";
// const char *topic_altitude = "sensor/altitude";
const char *topic_sensor_json = "test/topic";

WiFiClient espClient;
PubSubClient mqttClient(espClient);

// ---------- Sensor data variables ----------
float temperature = 25.3; // Example values
float humidity = 65.2;
float pressure = 1013.25;
uint32_t gas_resistance = 125000;
float altitude = 150.5;

// ---------- Scrolling variables ----------
int scrollOffset = 0;       // Current scroll position
int lineHeight = 20;        // Height of each line
int topMargin = 30;         // Space for title
int maxScroll = 0;          // Maximum scroll value
int totalContentHeight = 0; // Total height of all content

// ---------- Status variables ----------
bool wifiConnected = false;
bool mqttConnected = false;

// ---------- RTOS Handles ----------
TaskHandle_t TaskDisplay;
TaskHandle_t TaskMQTT;

// ---------- Mutex for sensor data protection ----------
SemaphoreHandle_t sensorDataMutex;

// ---------- Flag to trigger display update ----------
volatile bool needsRedraw = false;

// -----------------------------------------------------------------------------
// Backlight helpers
// -----------------------------------------------------------------------------
void setBacklight(uint8_t level)
{
  ledcWrite(TFT_BKL_CH, level);
}

void backlightSetup()
{
  ledcSetup(TFT_BKL_CH, TFT_BKL_FREQ, TFT_BKL_RES);
  ledcAttachPin(TFT_BKL, TFT_BKL_CH);
  setBacklight(200); // start at ~80% brightness
}

// -----------------------------------------------------------------------------
// WiFi Connection
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
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }
}

// -----------------------------------------------------------------------------
// MQTT Callback - receives sensor data
// -----------------------------------------------------------------------------
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message received [");
  Serial.print(topic);
  Serial.println("]");

  // Convert payload to string
  String jsonString;
  for (unsigned int i = 0; i < length; i++)
  {
    jsonString += (char)payload[i];
  }

  Serial.println(jsonString);

  // Parse JSON
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, jsonString);

  if (error)
  {
    Serial.print("JSON parse failed: ");
    Serial.println(error.c_str());
    return;
  }

  // Lock mutex
  if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE)
  {
    temperature = doc["temperature"] | temperature;
    humidity = doc["humidity"] | humidity;
    pressure = doc["pressure"] | pressure;
    gas_resistance = doc["gas_resistance"] | gas_resistance;
    altitude = doc["altitude"] | altitude;

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

    // Subscribe to all sensor topics
    mqttClient.subscribe(topic_sensor_json);

    Serial.println("Subscribed to sensor topics");
    needsRedraw = true;
    return true;
  }
  mqttConnected = false;
  needsRedraw = true;
  return false;
}

// -----------------------------------------------------------------------------
// Draw mini icon helpers
// -----------------------------------------------------------------------------
void drawTempIcon(int x, int y)
{
  // Thermometer icon
  tft.fillCircle(x + 4, y + 12, 3, ST77XX_RED);
  tft.fillRect(x + 3, y, 3, 10, ST77XX_RED);
  tft.drawRect(x + 2, y, 5, 10, ST77XX_WHITE);
}

void drawHumidityIcon(int x, int y)
{
  // Water droplet
  tft.fillTriangle(x + 4, y, x, y + 8, x + 8, y + 8, ST77XX_CYAN);
  tft.fillCircle(x + 4, y + 7, 3, ST77XX_CYAN);
}

void drawPressureIcon(int x, int y)
{
  // Gauge/dial icon
  tft.drawCircle(x + 5, y + 6, 5, ST77XX_YELLOW);
  tft.drawLine(x + 5, y + 6, x + 8, y + 3, ST77XX_YELLOW);
}

void drawGasIcon(int x, int y)
{
  // Cloud/gas icon
  tft.fillCircle(x + 3, y + 5, 3, ST77XX_MAGENTA);
  tft.fillCircle(x + 7, y + 5, 3, ST77XX_MAGENTA);
  tft.fillRect(x + 3, y + 5, 5, 3, ST77XX_MAGENTA);
}

void drawAltitudeIcon(int x, int y)
{
  // Mountain icon
  tft.fillTriangle(x, y + 8, x + 4, y, x + 8, y + 8, ST77XX_GREEN);
  tft.fillTriangle(x + 4, y + 8, x + 8, y + 3, x + 12, y + 8, ST77XX_GREEN);
}

// Draw bar graph for a value
void drawBarGraph(int x, int y, int w, int h, float value, float minVal, float maxVal, uint16_t color)
{
  // Background
  tft.drawRect(x, y, w, h, ST77XX_WHITE);
  // Filled portion
  int fillWidth = map(constrain(value, minVal, maxVal), minVal, maxVal, 0, w - 2);
  if (fillWidth > 0)
  {
    tft.fillRect(x + 1, y + 1, fillWidth, h - 2, color);
  }
}

// -----------------------------------------------------------------------------
// Sensor data display with scrolling
// -----------------------------------------------------------------------------
void drawSensorData()
{
  tft.fillScreen(ST77XX_BLACK);

  // Draw title bar with gradient effect
  tft.fillRect(0, 0, TFT_WIDTH, topMargin - 5, ST77XX_BLUE);
  tft.drawLine(0, topMargin - 5, TFT_WIDTH, topMargin - 5, ST77XX_CYAN);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 8);
  tft.print("Sensor Data");

  // Show connection status
  tft.setTextSize(1);
  if (wifiConnected)
  {
    tft.fillCircle(TFT_WIDTH - 30, 10, 3, ST77XX_GREEN);
  }
  else
  {
    tft.fillCircle(TFT_WIDTH - 30, 10, 3, ST77XX_RED);
  }
  if (mqttConnected)
  {
    tft.fillCircle(TFT_WIDTH - 15, 10, 3, ST77XX_GREEN);
  }
  else
  {
    tft.fillCircle(TFT_WIDTH - 15, 10, 3, ST77XX_RED);
  }
  tft.setTextSize(2);

  // Calculate total content height
  int numLines = 5;                                // Number of data lines
  totalContentHeight = numLines * lineHeight + 20; // +20 for padding
  int visibleHeight = TFT_HEIGHT - topMargin;

  // Calculate max scroll
  maxScroll = max(0, totalContentHeight - visibleHeight);

  // Constrain scroll offset
  scrollOffset = constrain(scrollOffset, 0, maxScroll);

  // Set text parameters
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);

  // Lock mutex before reading sensor data
  if (xSemaphoreTake(sensorDataMutex, portMAX_DELAY) == pdTRUE)
  {
    // Starting Y position (adjusted for scroll)
    int y = topMargin + 10 - scrollOffset;
    int iconX = 5;
    int textX = 25;
    int barX = 150;
    int barW = 65;

    // Temperature
    if (y > topMargin - lineHeight && y < TFT_HEIGHT)
    {
      drawTempIcon(iconX, y);
      tft.setCursor(textX, y);
      tft.print(temperature, 1);
      tft.print("C");
      drawBarGraph(barX, y + 2, barW, 12, temperature, 0, 50, ST77XX_RED);
    }
    y += lineHeight;

    // Humidity
    if (y > topMargin - lineHeight && y < TFT_HEIGHT)
    {
      drawHumidityIcon(iconX, y);
      tft.setCursor(textX, y);
      tft.print(humidity, 1);
      tft.print("%");
      drawBarGraph(barX, y + 2, barW, 12, humidity, 0, 100, ST77XX_CYAN);
    }
    y += lineHeight;

    // Pressure
    if (y > topMargin - lineHeight && y < TFT_HEIGHT)
    {
      drawPressureIcon(iconX, y);
      tft.setCursor(textX, y);
      tft.print(pressure, 0);
      tft.print("hPa");
      drawBarGraph(barX, y + 2, barW, 12, pressure, 950, 1050, ST77XX_YELLOW);
    }
    y += lineHeight;

    // Gas
    if (y > topMargin - lineHeight && y < TFT_HEIGHT)
    {
      drawGasIcon(iconX, y);
      tft.setCursor(textX, y);
      tft.print(gas_resistance / 1000);
      tft.print("k");
      drawBarGraph(barX, y + 2, barW, 12, gas_resistance, 0, 300000, ST77XX_MAGENTA);
    }
    y += lineHeight;

    // Altitude
    if (y > topMargin - lineHeight && y < TFT_HEIGHT)
    {
      drawAltitudeIcon(iconX, y);
      tft.setCursor(textX, y);
      tft.print(altitude, 1);
      tft.print("m");
      drawBarGraph(barX, y + 2, barW, 12, altitude, 0, 500, ST77XX_GREEN);
    }

    // Release mutex
    xSemaphoreGive(sensorDataMutex);
  }

  // Draw scroll indicator if content overflows
  if (maxScroll > 0)
  {
    int scrollBarHeight = (visibleHeight * visibleHeight) / totalContentHeight;
    int scrollBarPos = topMargin + (scrollOffset * (visibleHeight - scrollBarHeight)) / maxScroll;

    tft.fillRect(TFT_WIDTH - 5, scrollBarPos, 3, scrollBarHeight, ST77XX_GREEN);
  }
}

// -----------------------------------------------------------------------------
// Drawing helpers (kept for other functionality)
// -----------------------------------------------------------------------------
void clearWithLabel(const char *text)
{
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 10);
  tft.print(text);
}

void drawArrowUp()
{
  clearWithLabel("UP");
  int cx = tft.width() / 2;
  int cy = tft.height() / 2;
  tft.fillTriangle(cx, cy - 50, cx - 30, cy + 20, cx + 30, cy + 20, ST77XX_GREEN);
}

void drawArrowDown()
{
  clearWithLabel("DOWN");
  int cx = tft.width() / 2;
  int cy = tft.height() / 2;
  tft.fillTriangle(cx, cy + 50, cx - 30, cy - 20, cx + 30, cy - 20, ST77XX_GREEN);
}

void drawArrowLeft()
{
  clearWithLabel("LEFT");
  int cx = tft.width() / 2;
  int cy = tft.height() / 2;
  tft.fillTriangle(cx - 50, cy, cx + 20, cy - 30, cx + 20, cy + 30, ST77XX_GREEN);
}

void drawArrowRight()
{
  clearWithLabel("RIGHT");
  int cx = tft.width() / 2;
  int cy = tft.height() / 2;
  tft.fillTriangle(cx + 50, cy, cx - 20, cy - 30, cx - 20, cy + 30, ST77XX_GREEN);
}

void drawCenterOK()
{
  clearWithLabel("CENTER");
  int cx = tft.width() / 2;
  int cy = tft.height() / 2;
  tft.fillCircle(cx, cy, 35, ST77XX_BLUE);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(cx - 20, cy - 8);
  tft.print("OK");
}

// -----------------------------------------------------------------------------
// RTOS Task: Display & Button Handling
// -----------------------------------------------------------------------------
void TaskDisplayCode(void *parameter)
{
  Serial.println("Display Task started on core " + String(xPortGetCoreID()));

  bool lastUpState = false;
  bool lastDownState = false;

  for (;;)
  {
    // Read button states
    bool up = (digitalRead(SW_UP) == LOW);
    bool down = (digitalRead(SW_DOWN) == LOW);
    bool left = (digitalRead(SW_LEFT) == LOW);
    bool right = (digitalRead(SW_RIGHT) == LOW);
    bool center = (digitalRead(SW_CENTER) == LOW);

    // Detect rising edge for scrolling
    bool upPressed = up && !lastUpState;
    bool downPressed = down && !lastDownState;

    lastUpState = up;
    lastDownState = down;

    // Handle scrolling
    if (upPressed && scrollOffset > 0)
    {
      scrollOffset -= lineHeight;
      if (scrollOffset < 0)
        scrollOffset = 0;
      needsRedraw = true;
      Serial.println("Scroll UP");
    }
    else if (downPressed && scrollOffset < maxScroll)
    {
      scrollOffset += lineHeight;
      if (scrollOffset > maxScroll)
        scrollOffset = maxScroll;
      needsRedraw = true;
      Serial.println("Scroll DOWN");
    }
    else if (left)
    {
      drawArrowLeft();
      Serial.println("LEFT");
      vTaskDelay(300 / portTICK_PERIOD_MS); // Debounce
    }
    else if (right)
    {
      drawArrowRight();
      Serial.println("RIGHT");
      vTaskDelay(300 / portTICK_PERIOD_MS); // Debounce
    }
    else if (center)
    {
      // Reset scroll and redraw
      scrollOffset = 0;
      needsRedraw = true;
      Serial.println("CENTER - Reset View");
      vTaskDelay(300 / portTICK_PERIOD_MS); // Debounce
    }

    // Redraw if needed
    if (needsRedraw)
    {
      drawSensorData();
      needsRedraw = false;
    }

    vTaskDelay(50 / portTICK_PERIOD_MS); // 50ms update rate
  }
}

// -----------------------------------------------------------------------------
// RTOS Task: MQTT Handling
// -----------------------------------------------------------------------------
void TaskMQTTCode(void *parameter)
{
  Serial.println("MQTT Task started on core " + String(xPortGetCoreID()));

  unsigned long lastReconnectAttempt = 0;

  for (;;)
  {
    if (wifiConnected)
    {
      if (!mqttClient.connected())
      {
        unsigned long now = millis();
        if (now - lastReconnectAttempt > 5000)
        { // Try reconnecting every 5 seconds
          lastReconnectAttempt = now;
          Serial.println("Attempting MQTT reconnection...");
          if (reconnectMQTT())
          {
            lastReconnectAttempt = 0;
          }
        }
      }
      else
      {
        mqttClient.loop(); // Process incoming MQTT messages
      }
    }

    vTaskDelay(10 / portTICK_PERIOD_MS); // 10ms update rate for MQTT
  }
}

// -----------------------------------------------------------------------------
// Setup & Main Loop
// -----------------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);

  Serial.println("Starting ESP32 Sensor Display with RTOS...");

  // Switch pins as inputs with internal pull-ups
  pinMode(SW_UP, INPUT_PULLUP);
  pinMode(SW_DOWN, INPUT_PULLUP);
  pinMode(SW_LEFT, INPUT_PULLUP);
  pinMode(SW_RIGHT, INPUT_PULLUP);
  pinMode(SW_CENTER, INPUT_PULLUP);

  // TFT
  tft.init(TFT_WIDTH, TFT_HEIGHT);
  tft.setRotation(90);
  tft.fillScreen(ST77XX_BLACK);

  // Backlight
  backlightSetup();

  // Show connecting message
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(10, 100);
  tft.print("Connecting...");

  // Create mutex for sensor data protection
  sensorDataMutex = xSemaphoreCreateMutex();

  // Setup WiFi
  setupWiFi();

  // Setup MQTT
  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttCallback);

  // Try to connect to MQTT
  if (wifiConnected)
  {
    reconnectMQTT();
  }

  // Display sensor data initially
  drawSensorData();

  // Create RTOS Tasks
  // Task 1: Display & Button handling on Core 1
  xTaskCreatePinnedToCore(
      TaskDisplayCode, /* Task function */
      "TaskDisplay",   /* Name of task */
      10000,           /* Stack size (bytes) */
      NULL,            /* Parameter passed to task */
      1,               /* Task priority */
      &TaskDisplay,    /* Task handle */
      1);              /* Core where task runs (Core 1) */

  // Task 2: MQTT handling on Core 0
  xTaskCreatePinnedToCore(
      TaskMQTTCode, /* Task function */
      "TaskMQTT",   /* Name of task */
      10000,        /* Stack size (bytes) */
      NULL,         /* Parameter passed to task */
      1,            /* Task priority */
      &TaskMQTT,    /* Task handle */
      0);           /* Core where task runs (Core 0) */

  Serial.println("RTOS Tasks created successfully!");
}

void loop()
{
  // Empty - all work is done in RTOS tasks
  vTaskDelay(1000 / portTICK_PERIOD_MS);
}