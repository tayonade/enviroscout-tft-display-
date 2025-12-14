#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

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
    tft.setTextSize(2);
    tft.print(pressure, 1);
    tft.print("hPa");
    tft.setTextSize(2);
    drawBarGraph(barX, y + 2, barW, 12, pressure, 950, 1050, ST77XX_YELLOW);
  }
  y += lineHeight;

  // Gas
  if (y > topMargin - lineHeight && y < TFT_HEIGHT)
  {
    drawGasIcon(iconX, y);
    tft.setCursor(textX, y);
    tft.setTextSize(2);
    tft.print(gas_resistance / 1000);
    tft.print("k");
    tft.setTextSize(2);
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
// Setup & loop
// -----------------------------------------------------------------------------
void setup()
{
  Serial.begin(115200);

  // Switch pins as inputs with internal pull-ups
  // pinMode(SW_UP, INPUT_PULLUP);
  // pinMode(SW_DOWN, INPUT_PULLUP);
  // pinMode(SW_LEFT, INPUT_PULLUP);
  // pinMode(SW_RIGHT, INPUT_PULLUP);
  // pinMode(SW_CENTER, INPUT_PULLUP);

  // TFT
  tft.init(TFT_WIDTH, TFT_HEIGHT);
  tft.setRotation(90);
  tft.fillScreen(ST77XX_BLACK);

  // Backlight
  backlightSetup();

  // Display sensor data initially
  drawSensorData();
}

unsigned long lastCheck = 0;
const unsigned long pollIntervalMs = 50;
bool lastUpState = false;
bool lastDownState = false;

void loop()
{
  if (millis() - lastCheck < pollIntervalMs)
    return;
  lastCheck = millis();

  bool up = (digitalRead(SW_UP) == LOW);
  bool down = (digitalRead(SW_DOWN) == LOW);
  bool left = (digitalRead(SW_LEFT) == LOW);
  bool right = (digitalRead(SW_RIGHT) == LOW);
  bool center = (digitalRead(SW_CENTER) == LOW);

  // Detect rising edge for scrolling (button just pressed)
  bool upPressed = up && !lastUpState;
  bool downPressed = down && !lastDownState;

  lastUpState = up;
  lastDownState = down;

  // Handle scrolling
  // if (upPressed && scrollOffset > 0)
  // {
  //   scrollOffset -= lineHeight;
  //   if (scrollOffset < 0)
  //     scrollOffset = 0;
  //   drawSensorData();
  //   Serial.println("Scroll UP");
  // }
  // else if (downPressed && scrollOffset < maxScroll)
  // {
  //   scrollOffset += lineHeight;
  //   if (scrollOffset > maxScroll)
  //     scrollOffset = maxScroll;
  //   drawSensorData();
  //   Serial.println("Scroll DOWN");
  // }
  // else if (left)
  // {
  //   drawArrowLeft();
  //   Serial.println("LEFT");
  // }
  // else if (right)
  // {
  //   drawArrowRight();
  //   Serial.println("RIGHT");
  // }
  // else if (center)
  // {
  //   // Reset scroll and redraw
  //   scrollOffset = 0;
  //   drawSensorData();
  //   Serial.println("CENTER - Reset View");
  // }

  // OPTIONAL: Update sensor data periodically
  // You would read from your actual sensor here
  // For demo, you can uncomment this to see changing values:
  /*
  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate > 2000) {
    lastUpdate = millis();
    temperature += random(-10, 10) / 10.0;
    humidity += random(-5, 5) / 10.0;
    pressure += random(-2, 2) / 10.0;
    gas_resistance += random(-1000, 1000);
    altitude += random(-5, 5) / 10.0;
    drawSensorData();
  }
  */
}