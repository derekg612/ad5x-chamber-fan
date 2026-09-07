#include <Arduino.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <Preferences.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <Wire.h>

// Generic ESP32-C3 board with an onboard 0.42" 72x40 SSD1306 OLED.
//
// Only four pins on this board are free of strapping/JTAG/UART duty:
// IO0, IO1, IO3 and IO10, and all four are used here. The OLED sits on
// IO8/IO9, which are themselves strapping pins -- the I2C pull-ups hold them
// in the states the bootloader needs, which is why they work, but it also
// means neither can be reused.
constexpr uint8_t OLED_SDA_PIN = 8;
constexpr uint8_t OLED_SCL_PIN = 9;
constexpr uint8_t THERMISTOR_PIN = 1;   // ADC1_CH1
constexpr uint8_t FAN_PIN = 10;
constexpr uint8_t BUTTON_UP_PIN = 0;
constexpr uint8_t BUTTON_DOWN_PIN = 3;

constexpr uint8_t DISPLAY_WIDTH = 72;
constexpr uint8_t DISPLAY_HEIGHT = 40;

// ---------------------------------------------------------------------------
// Fan drive configuration -- identical to the touchscreen build.
//
// The fan is a 2-wire 24 V unit (0.29 A @ 24 V, 16-26.4 V range) whose supply
// is chopped by a 2N2222A (or BC337) on its ground return, with a 1N5819
// Schottky across the fan. This drive is NOT inverted: driving FAN_PIN high
// turns the transistor on and powers the fan, so duty maps straight through.
//
// Set FAN_PWM_ENABLED to 0 to fall back to plain on/off switching.
// ---------------------------------------------------------------------------
#define FAN_PWM_ENABLED 1
// 25 kHz keeps switching above the audible band. Lower it (e.g. 1000) if the
// transistor runs hot -- switching loss scales with frequency.
#define FAN_PWM_FREQUENCY_HZ 25000
#define FAN_PWM_RESOLUTION_BITS 8
// Small fans stall or stutter below roughly this duty, so once the fan is
// asked to run at all it ramps from this floor rather than from zero.
#define FAN_MIN_DUTY_PERCENT 25.0f
#define FAN_MAX_DUTY_PERCENT 100.0f
// Bounds for the user-configurable speed cap on the settings page.
#define FAN_SPEED_CAP_MIN_PERCENT 25.0f
#define FAN_SPEED_CAP_DEFAULT_PERCENT 100.0f

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
constexpr unsigned long BUTTON_DEBOUNCE_MS = 250;

AsyncWebServer server(80);
DNSServer dns;
Preferences preferences;
U8G2_SSD1306_72X40_ER_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE, OLED_SCL_PIN, OLED_SDA_PIN);

float setpointC = DEFAULT_SETPOINT_C;
float topOffsetC = DEFAULT_TOP_OFFSET_C;
float bottomOffsetC = DEFAULT_BOTTOM_OFFSET_C;
float maxFanSpeedPercent = FAN_SPEED_CAP_DEFAULT_PERCENT;
float temperatureC = NAN;
uint8_t fanDutyPercent = 0;
unsigned long lastSampleMs = 0;
unsigned long lastButtonUpMs = 0;
unsigned long lastButtonDownMs = 0;

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
  float requestedPercent;

  if (isnan(temperatureC)) {
    // Sensor fault: ignore the configured cap and force full ventilation.
    requestedPercent = FAN_MAX_DUTY_PERCENT;
  } else if (temperatureC <= bottomC) {
    requestedPercent = 0.0f;
  } else if (temperatureC >= topC) {
    requestedPercent = maxFanSpeedPercent;
  } else {
    const float floorPercent = min(static_cast<float>(FAN_MIN_DUTY_PERCENT), maxFanSpeedPercent);
    const float fraction = (temperatureC - bottomC) / (topC - bottomC);
    requestedPercent = floorPercent + fraction * (maxFanSpeedPercent - floorPercent);
  }

  fanDutyPercent = static_cast<uint8_t>(
      lroundf(constrain(requestedPercent, 0.0f, FAN_MAX_DUTY_PERCENT)));

#if FAN_PWM_ENABLED
  constexpr uint32_t fullScale = (1u << FAN_PWM_RESOLUTION_BITS) - 1u;
  ledcWrite(FAN_PIN, static_cast<uint32_t>(lroundf(fanDutyPercent * fullScale / 100.0f)));
#else
  digitalWrite(FAN_PIN, fanDutyPercent > 0 ? HIGH : LOW);
#endif
}

void persistSettings() {
  preferences.putFloat("setpoint", setpointC);
  preferences.putFloat("topOffset", topOffsetC);
  preferences.putFloat("bottomOffset", bottomOffsetC);
  preferences.putFloat("maxFanSpeed", maxFanSpeedPercent);
}

// The panel is only 72x40, so the layout is four tight lines: setpoint,
// current temperature, fan duty, and the IP address in a smaller font.
void updateDisplay() {
  display.firstPage();
  do {
    display.setFont(u8g2_font_6x10_tf);
    display.drawStr(0, 9, ("Set " + String(setpointC, 1)).c_str());
    display.drawStr(0, 19, (isnan(temperatureC) ? "Now fault" : "Now " + String(temperatureC, 1)).c_str());
    display.drawStr(0, 29, (fanDutyPercent == 0 ? String("Fan idle")
                                                : "Fan " + String(fanDutyPercent) + "%").c_str());
    display.setFont(u8g2_font_5x8_tf);
    display.drawStr(0, 39, WiFi.localIP().toString().c_str());
  } while (display.nextPage());
}

void handleButtons() {
  const unsigned long now = millis();
  if (digitalRead(BUTTON_UP_PIN) == LOW && now - lastButtonUpMs >= BUTTON_DEBOUNCE_MS) {
    lastButtonUpMs = now;
    setpointC = constrain(setpointC + SETPOINT_STEP_C, SETPOINT_MIN_C, SETPOINT_MAX_C);
    persistSettings();
    updateFan();
    updateDisplay();
  }
  if (digitalRead(BUTTON_DOWN_PIN) == LOW && now - lastButtonDownMs >= BUTTON_DEBOUNCE_MS) {
    lastButtonDownMs = now;
    setpointC = constrain(setpointC - SETPOINT_STEP_C, SETPOINT_MIN_C, SETPOINT_MAX_C);
    persistSettings();
    updateFan();
    updateDisplay();
  }
}

String page() {
  const String temperature = isnan(temperatureC) ? "Sensor fault" : String(temperatureC, 1) + " &deg;C";
  const String state = fanDutyPercent == 0 ? String("IDLE") : String(fanDutyPercent) + "% speed";
  return String(F("<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
           "<title>AD5X Chamber</title><style>body{font-family:system-ui;max-width:34rem;margin:2rem auto;padding:0 1rem;color:#17212b}"
           "main{border:1px solid #ccd5dc;border-radius:8px;padding:1.25rem}h1{margin-top:0}label{display:block;margin-top:1rem}"
           "input,button{font:inherit;padding:.55rem;margin-top:.3rem;width:100%;box-sizing:border-box}button{margin-top:1.25rem;background:#1769aa;color:white;border:0;border-radius:4px}"
           ".reading{font-size:2rem;font-weight:700}.state{font-weight:700;color:#1769aa}</style></head><body><main><h1>AD5X Chamber</h1><div class='reading'>"))
      + temperature + F("</div><p>Fan: <span class='state'>") + state
      + F("</span></p><form method='post' action='/settings'><label>Target temperature (&deg;C)<input name='setpoint' type='number' step='0.5' min='10' max='60' value='")
      + String(setpointC, 1) + F("'></label><label>Upper threshold above target (&deg;C)<input name='top' type='number' step='0.5' min='0.5' max='20' value='")
      + String(topOffsetC, 1) + F("'></label><label>Lower threshold below target (&deg;C)<input name='bottom' type='number' step='0.5' min='0.5' max='20' value='")
      + String(bottomOffsetC, 1) + F("'></label><label>Maximum fan speed (%)<input name='maxfan' type='number' step='5' min='")
      + String(FAN_SPEED_CAP_MIN_PERCENT, 0) + F("' max='") + String(FAN_MAX_DUTY_PERCENT, 0) + F("' value='")
      + String(maxFanSpeedPercent, 0) + F("'></label><button type='submit'>Save settings</button></form></main></body></html>");
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
  if (request->hasParam("maxfan", true)) {
    maxFanSpeedPercent = constrain(request->getParam("maxfan", true)->value().toFloat(),
        FAN_SPEED_CAP_MIN_PERCENT, FAN_MAX_DUTY_PERCENT);
  }
  persistSettings();
  updateFan();
  updateDisplay();
  request->redirect("/");
}

void setup() {
  Serial.begin(115200);
#if FAN_PWM_ENABLED
  ledcAttach(FAN_PIN, FAN_PWM_FREQUENCY_HZ, FAN_PWM_RESOLUTION_BITS);
  ledcWrite(FAN_PIN, 0);
#else
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);
#endif
  analogReadResolution(12);

  pinMode(BUTTON_UP_PIN, INPUT_PULLUP);
  pinMode(BUTTON_DOWN_PIN, INPUT_PULLUP);

  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  display.begin();
  display.setFont(u8g2_font_6x10_tf);
  display.firstPage();
  do {
    display.drawStr(0, 12, "AD5X");
    display.drawStr(0, 26, "Wi-Fi...");
  } while (display.nextPage());

  preferences.begin("chamber", false);
  setpointC = preferences.getFloat("setpoint", DEFAULT_SETPOINT_C);
  topOffsetC = preferences.getFloat("topOffset", DEFAULT_TOP_OFFSET_C);
  bottomOffsetC = preferences.getFloat("bottomOffset", DEFAULT_BOTTOM_OFFSET_C);
  maxFanSpeedPercent = preferences.getFloat("maxFanSpeed", FAN_SPEED_CAP_DEFAULT_PERCENT);

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

  updateDisplay();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", page());
  });
  server.on("/settings", HTTP_POST, handleSettings);
  server.begin();
}

void loop() {
  handleButtons();

  if (millis() - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = millis();
    temperatureC = readTemperatureC();
    updateFan();
    updateDisplay();
    Serial.printf("Temperature: %.1f C, fan: %u%%\n", temperatureC, fanDutyPercent);
  }
}
