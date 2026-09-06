#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <Preferences.h>
#include <WiFi.h>
#include <Wire.h>

#include "esp_lcd_touch_axs5106l.h"

// Waveshare ESP32-S3-Touch-LCD-1.47 pins, confirmed from the vendor's
// Arduino example and community demo code for this exact board.
constexpr uint8_t LCD_MOSI_PIN = 39;
constexpr uint8_t LCD_SCK_PIN = 38;
constexpr uint8_t LCD_CS_PIN = 21;
constexpr uint8_t LCD_DC_PIN = 45;
constexpr uint8_t LCD_RST_PIN = 47;
constexpr uint8_t LCD_BL_PIN = 46;
constexpr uint8_t TOUCH_SDA_PIN = 42;
constexpr uint8_t TOUCH_SCL_PIN = 41;
constexpr uint8_t TOUCH_RST_PIN = 47;
constexpr uint8_t TOUCH_INT_PIN = 48;

// Free header pins for our own peripherals (thermistor ADC, BC337 fan
// switch), chosen to avoid the display/touch/USB/strapping pins above.
constexpr uint8_t THERMISTOR_PIN = 4;
constexpr uint8_t FAN_PIN = 5;

constexpr uint16_t DISPLAY_WIDTH = 172;
constexpr uint16_t DISPLAY_HEIGHT = 320;
// 0 = USB-C at the top (vendor demo default). 2 = rotated 180 degrees, USB-C
// at the bottom, as requested. Verify on first boot: if the UI renders
// upside down, the board's physical "up" didn't match this assumption --
// change this to 0 and re-flash.
constexpr uint16_t DISPLAY_ROTATION = 2;

constexpr char SETUP_AP_NAME[] = "AD5X-Chamber-Setup";
constexpr char SETUP_AP_PASSWORD[] = "chamber123";
constexpr float SERIES_RESISTOR_OHMS = 10000.0f;
constexpr float THERMISTOR_NOMINAL_OHMS = 10000.0f;
constexpr float NOMINAL_TEMPERATURE_C = 25.0f;
constexpr float THERMISTOR_BETA = 3950.0f;
constexpr float ADC_REFERENCE_VOLTS = 3.3f;
constexpr float SETPOINT_MIN_C = 10.0f;
constexpr float SETPOINT_MAX_C = 60.0f;
constexpr float DEFAULT_SETPOINT_C = 35.0f;
constexpr float DEFAULT_TOP_OFFSET_C = 3.0f;
constexpr float DEFAULT_BOTTOM_OFFSET_C = 1.0f;
constexpr unsigned long SAMPLE_INTERVAL_MS = 2000;
constexpr float SETPOINT_STEP_C = 0.5f;
constexpr unsigned long TOUCH_DEBOUNCE_MS = 250;

// On-screen setpoint button geometry.
constexpr int16_t BTN_Y = 190;
constexpr int16_t BTN_W = 76;
constexpr int16_t BTN_H = 90;
constexpr int16_t MINUS_BTN_X = 6;
constexpr int16_t PLUS_BTN_X = DISPLAY_WIDTH - BTN_W - 6;
constexpr int16_t NOW_Y = 50;
constexpr int16_t SET_Y = 112;
constexpr int16_t FAN_Y = 150;
constexpr int16_t FOOTER_Y = 302;

AsyncWebServer server(80);
DNSServer dns;
Preferences preferences;
Arduino_DataBus *displayBus = new Arduino_ESP32SPI(LCD_DC_PIN, LCD_CS_PIN, LCD_SCK_PIN, LCD_MOSI_PIN);
Arduino_GFX *gfx = new Arduino_ST7789(displayBus, LCD_RST_PIN, DISPLAY_ROTATION, false /* IPS */,
    DISPLAY_WIDTH, DISPLAY_HEIGHT, 34, 0, 34, 0);

float setpointC = DEFAULT_SETPOINT_C;
float topOffsetC = DEFAULT_TOP_OFFSET_C;
float bottomOffsetC = DEFAULT_BOTTOM_OFFSET_C;
float temperatureC = NAN;
bool fanOn = false;
unsigned long lastSampleMs = 0;
unsigned long lastTouchMs = 0;

float readTemperatureC() {
  const int raw = analogRead(THERMISTOR_PIN);
  if (raw <= 0 || raw >= 4095) {
    return NAN;
  }

  const float voltage = (static_cast<float>(raw) / 4095.0f) * ADC_REFERENCE_VOLTS;
  const float resistance = SERIES_RESISTOR_OHMS * voltage / (ADC_REFERENCE_VOLTS - voltage);
  const float steinhart = log(resistance / THERMISTOR_NOMINAL_OHMS) / THERMISTOR_BETA
      + 1.0f / (NOMINAL_TEMPERATURE_C + 273.15f);
  return 1.0f / steinhart - 273.15f;
}

void updateFan() {
  const float topC = setpointC + topOffsetC;
  const float bottomC = setpointC - bottomOffsetC;

  if (isnan(temperatureC) || temperatureC >= topC) {
    fanOn = true;
  } else if (temperatureC <= bottomC) {
    fanOn = false;
  }
  digitalWrite(FAN_PIN, fanOn ? HIGH : LOW);
}

void persistSettings() {
  preferences.putFloat("setpoint", setpointC);
  preferences.putFloat("topOffset", topOffsetC);
  preferences.putFloat("bottomOffset", bottomOffsetC);
}

void drawStaticUI() {
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(10, 8);
  gfx->print("AD5X Chamber");
  gfx->drawFastHLine(0, 32, DISPLAY_WIDTH, RGB565_WHITE);

  gfx->fillRoundRect(MINUS_BTN_X, BTN_Y, BTN_W, BTN_H, 8, RGB565_RED);
  gfx->drawRoundRect(MINUS_BTN_X, BTN_Y, BTN_W, BTN_H, 8, RGB565_WHITE);
  gfx->setTextSize(4);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setCursor(MINUS_BTN_X + BTN_W / 2 - 8, BTN_Y + BTN_H / 2 - 14);
  gfx->print("-");

  gfx->fillRoundRect(PLUS_BTN_X, BTN_Y, BTN_W, BTN_H, 8, RGB565_GREEN);
  gfx->drawRoundRect(PLUS_BTN_X, BTN_Y, BTN_W, BTN_H, 8, RGB565_WHITE);
  gfx->setCursor(PLUS_BTN_X + BTN_W / 2 - 12, BTN_Y + BTN_H / 2 - 14);
  gfx->print("+");
}

void updateDisplay() {
  gfx->fillRect(0, NOW_Y - 4, DISPLAY_WIDTH, 42, RGB565_BLACK);
  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setCursor(10, NOW_Y);
  gfx->print("Now");
  gfx->setTextSize(3);
  gfx->setCursor(10, NOW_Y + 12);
  if (isnan(temperatureC)) {
    gfx->setTextColor(RGB565_RED);
    gfx->print("fault");
  } else {
    gfx->setTextColor(RGB565_WHITE);
    gfx->print(temperatureC, 1);
    gfx->print("C");
  }

  gfx->fillRect(0, SET_Y - 4, DISPLAY_WIDTH, 26, RGB565_BLACK);
  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setCursor(10, SET_Y);
  gfx->print("Set");
  gfx->setTextSize(2);
  gfx->setCursor(50, SET_Y - 2);
  gfx->print(setpointC, 1);
  gfx->print(" C");

  gfx->fillRect(0, FAN_Y - 4, DISPLAY_WIDTH, 26, RGB565_BLACK);
  gfx->setTextSize(2);
  gfx->setCursor(10, FAN_Y);
  gfx->setTextColor(fanOn ? RGB565_CYAN : RGB565_WHITE);
  gfx->print(fanOn ? "VENTING" : "IDLE");

  gfx->fillRect(0, FOOTER_Y, DISPLAY_WIDTH, 18, RGB565_BLACK);
  gfx->setTextSize(1);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setCursor(6, FOOTER_Y);
  gfx->print(WiFi.localIP().toString());
}

void handleTouch() {
  bsp_touch_read();
  touch_data_t touchData;
  if (!bsp_touch_get_coordinates(&touchData)) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastTouchMs < TOUCH_DEBOUNCE_MS) {
    return;
  }

  const uint16_t x = touchData.coords[0].x;
  const uint16_t y = touchData.coords[0].y;
  if (y < BTN_Y || y > BTN_Y + BTN_H) {
    return;
  }

  if (x >= MINUS_BTN_X && x <= MINUS_BTN_X + BTN_W) {
    setpointC = constrain(setpointC - SETPOINT_STEP_C, SETPOINT_MIN_C, SETPOINT_MAX_C);
  } else if (x >= PLUS_BTN_X && x <= PLUS_BTN_X + BTN_W) {
    setpointC = constrain(setpointC + SETPOINT_STEP_C, SETPOINT_MIN_C, SETPOINT_MAX_C);
  } else {
    return;
  }

  lastTouchMs = now;
  persistSettings();
  updateFan();
  updateDisplay();
}

String page() {
  const String temperature = isnan(temperatureC) ? "Sensor fault" : String(temperatureC, 1) + " &deg;C";
  const String state = fanOn ? "VENTING" : "IDLE";
  return String(F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>AD5X Chamber</title><style>body{font-family:system-ui;max-width:34rem;margin:2rem auto;padding:0 1rem;color:#17212b}"
           "main{border:1px solid #ccd5dc;border-radius:8px;padding:1.25rem}h1{margin-top:0}label{display:block;margin-top:1rem}"
           "input,button{font:inherit;padding:.55rem;margin-top:.3rem;width:100%;box-sizing:border-box}button{margin-top:1.25rem;background:#1769aa;color:white;border:0;border-radius:4px}"
           ".reading{font-size:2rem;font-weight:700}.state{font-weight:700;color:#1769aa}</style></head><body><main><h1>AD5X Chamber</h1><div class='reading'>"))
      + temperature + F("</div><p>Fan: <span class='state'>") + state
      + F("</span></p><form method='post' action='/settings'><label>Target temperature (&deg;C)<input name='setpoint' type='number' step='0.5' min='10' max='60' value='")
      + String(setpointC, 1) + F("'></label><label>Upper threshold above target (&deg;C)<input name='top' type='number' step='0.5' min='0.5' max='20' value='")
      + String(topOffsetC, 1) + F("'></label><label>Lower threshold below target (&deg;C)<input name='bottom' type='number' step='0.5' min='0.5' max='20' value='")
      + String(bottomOffsetC, 1) + F("'></label><button type='submit'>Save settings</button></form></main></body></html>");
}

void handleSettings(AsyncWebServerRequest *request) {
  if (request->hasParam("setpoint", true)) {
    setpointC = constrain(request->getParam("setpoint", true)->value().toFloat(), SETPOINT_MIN_C, SETPOINT_MAX_C);
  }
  if (request->hasParam("top", true)) {
    topOffsetC = constrain(request->getParam("top", true)->value().toFloat(), 0.5f, 20.0f);
  }
  if (request->hasParam("bottom", true)) {
    bottomOffsetC = constrain(request->getParam("bottom", true)->value().toFloat(), 0.5f, 20.0f);
  }
  persistSettings();
  updateFan();
  updateDisplay();
  request->redirect("/");
}

void setup() {
  Serial.begin(115200);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);
  analogReadResolution(12);

  pinMode(LCD_BL_PIN, OUTPUT);
  digitalWrite(LCD_BL_PIN, HIGH);
  gfx->begin();
  gfx->fillScreen(RGB565_BLACK);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(2);
  gfx->setCursor(10, 140);
  gfx->print("AD5X Chamber");
  gfx->setTextSize(1);
  gfx->setCursor(10, 170);
  gfx->print("Connecting Wi-Fi...");

  Wire.begin(TOUCH_SDA_PIN, TOUCH_SCL_PIN);
  bsp_touch_init(&Wire, TOUCH_RST_PIN, TOUCH_INT_PIN, DISPLAY_ROTATION, DISPLAY_WIDTH, DISPLAY_HEIGHT);

  preferences.begin("chamber", false);
  setpointC = preferences.getFloat("setpoint", DEFAULT_SETPOINT_C);
  topOffsetC = preferences.getFloat("topOffset", DEFAULT_TOP_OFFSET_C);
  bottomOffsetC = preferences.getFloat("bottomOffset", DEFAULT_BOTTOM_OFFSET_C);

  WiFi.setHostname("AD5X-Chamber");
  AsyncWiFiManager wifiManager(&server, &dns);
  if (!wifiManager.autoConnect(SETUP_AP_NAME, SETUP_AP_PASSWORD)) {
    Serial.println("Wi-Fi setup failed; restarting.");
    delay(1000);
    ESP.restart();
  }
  WiFi.setAutoReconnect(true);
  Serial.print("Open http://");
  Serial.println(WiFi.localIP());

  drawStaticUI();
  updateDisplay();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", page());
  });
  server.on("/settings", HTTP_POST, handleSettings);
  server.begin();
}

void loop() {
  handleTouch();

  if (millis() - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = millis();
    temperatureC = readTemperatureC();
    updateFan();
    updateDisplay();
    Serial.printf("Temperature: %.1f C, fan: %s\n", temperatureC, fanOn ? "ON" : "OFF");
  }
}
