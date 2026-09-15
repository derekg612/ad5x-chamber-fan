#include <Arduino.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <Preferences.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <Wire.h>

// Generic ESP32-C3 board with an onboard 0.42" 72x40 SSD1306 OLED.
//
// Only four pins on this board are free of strapping/JTAG/UART duty:
// IO0, IO1, IO3 and IO10, and all four are used here. The OLED sits on
// IO5/IO6, which are themselves strapping pins -- the I2C pull-ups hold them
// in the states the bootloader needs, which is why they work, but it also
// means neither can be reused.
constexpr uint8_t OLED_SDA_PIN = 5;//8;
constexpr uint8_t OLED_SCL_PIN = 6;//9;
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
// A single button only acts once it has been down this long on its own, so
// that a two-button press whose halves land a few milliseconds apart toggles
// the printer LED instead of first nudging the setpoint.
constexpr unsigned long BUTTON_COMBO_GUARD_MS = 120;

// ---------------------------------------------------------------------------
// Printer LED control -- pressing both buttons at once toggles the AD5X's
// chamber light.
//
// Stock AD5X firmware does not expose Moonraker; what it does expose is
// Flashforge's legacy command socket on TCP 8899, which speaks a fixed G/M
// code dialect. Commands are framed with a leading '~' and CRLF, and every
// reply ends with a line reading "ok". A remote client has to take control
// (M601 S1) before the firmware will accept anything, and should hand it back
// (M602) afterwards so the printer's own touchscreen keeps working.
// This mirrors ad5x_send_command.py.
// ---------------------------------------------------------------------------
constexpr char DEFAULT_PRINTER_HOST[] = "ad5x.lan";
constexpr uint16_t DEFAULT_PRINTER_PORT = 8899;
constexpr int32_t PRINTER_CONNECT_TIMEOUT_MS = 1500;
constexpr unsigned long PRINTER_REPLY_TIMEOUT_MS = 1200;
constexpr char PRINTER_LED_ON_COMMAND[] = "M146 r255 g255 b255 F0";
constexpr char PRINTER_LED_OFF_COMMAND[] = "M146 r0 g0 b0 F0";
constexpr char PRINTER_TAKE_CONTROL_COMMAND[] = "M601 S1";
constexpr char PRINTER_RELEASE_CONTROL_COMMAND[] = "M602";
constexpr char PRINTER_REPLY_TERMINATOR[] = "ok";
// How long the two-line confirmation stays on the panel before the normal
// readout comes back.
constexpr unsigned long FLASH_MESSAGE_MS = 1500;

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
unsigned long buttonUpSinceMs = 0;
unsigned long buttonDownSinceMs = 0;
bool buttonUpHeld = false;
bool buttonDownHeld = false;
// Set when both buttons are down together, cleared only once both are
// released, so one long two-button press toggles the LED exactly once.
bool buttonComboHandled = false;

String printerHost = DEFAULT_PRINTER_HOST;
uint16_t printerPort = DEFAULT_PRINTER_PORT;
// The printer will not report its light state back, so this tracks what we
// last told it. It is persisted so the toggle stays in step across reboots.
bool printerLedOn = true;

String flashLine1;
String flashLine2;
unsigned long flashUntilMs = 0;

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
  preferences.putString("printerHost", printerHost);
  preferences.putUShort("printerPort", printerPort);
}

// The panel is only 72x40, so the layout is four tight lines: setpoint,
// current temperature, fan duty, and the IP address in a smaller font.
void updateDisplay() {
  display.firstPage();
  do {
    display.setFont(u8g2_font_6x10_tf);
    if (flashUntilMs != 0) {
      display.drawStr(0, 16, flashLine1.c_str());
      display.drawStr(0, 30, flashLine2.c_str());
      continue;
    }
    display.drawStr(0, 9, ("Set " + String(setpointC, 1)).c_str());
    display.drawStr(0, 19, (isnan(temperatureC) ? "Now fault" : "Now " + String(temperatureC, 1)).c_str());
    display.drawStr(0, 29, (fanDutyPercent == 0 ? String("Fan idle")
                                                : "Fan " + String(fanDutyPercent) + "%").c_str());
    display.setFont(u8g2_font_5x8_tf);
    display.drawStr(0, 39, WiFi.localIP().toString().c_str());
  } while (display.nextPage());
}

void showFlash(const char *line1, const char *line2) {
  flashLine1 = line1;
  flashLine2 = line2;
  flashUntilMs = millis() + FLASH_MESSAGE_MS;
  updateDisplay();
}

// Send one framed command and wait for the terminating "ok".
bool sendPrinterCommand(WiFiClient &client, const char *command) {
  client.printf("~%s\r\n", command);
  String reply;
  const unsigned long deadline = millis() + PRINTER_REPLY_TIMEOUT_MS;
  while (static_cast<long>(millis() - deadline) < 0) {
    while (client.available() > 0) {
      reply += static_cast<char>(client.read());
    }
    reply.trim();
    if (reply.endsWith(PRINTER_REPLY_TERMINATOR)) {
      return true;
    }
    if (!client.connected() && client.available() == 0) {
      break;
    }
    delay(5);
  }
  Serial.printf("Printer did not acknowledge %s (reply: %s)\n", command, reply.c_str());
  return false;
}

// Flip the printer's chamber light. Blocks for up to a few seconds in the
// worst case; the async web server runs on its own task, so only the button
// and sampling loop waits.
bool togglePrinterLed() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Cannot toggle the printer LED: no Wi-Fi.");
    return false;
  }

  WiFiClient client;
  if (!client.connect(printerHost.c_str(), printerPort, PRINTER_CONNECT_TIMEOUT_MS)) {
    Serial.printf("Cannot reach %s:%u\n", printerHost.c_str(), printerPort);
    return false;
  }

  const bool turnOn = !printerLedOn;
  bool ok = sendPrinterCommand(client, PRINTER_TAKE_CONTROL_COMMAND);
  if (ok) {
    ok = sendPrinterCommand(client, turnOn ? PRINTER_LED_ON_COMMAND : PRINTER_LED_OFF_COMMAND);
  }
  // Hand control back either way, so a failed toggle does not leave the
  // printer's own touchscreen locked out.
  sendPrinterCommand(client, PRINTER_RELEASE_CONTROL_COMMAND);
  client.stop();

  if (ok) {
    printerLedOn = turnOn;
    preferences.putBool("printerLedOn", printerLedOn);
    Serial.printf("Printer LED %s\n", printerLedOn ? "on" : "off");
  }
  return ok;
}

void adjustSetpoint(float deltaC) {
  setpointC = constrain(setpointC + deltaC, SETPOINT_MIN_C, SETPOINT_MAX_C);
  persistSettings();
  updateFan();
  updateDisplay();
}

void handleButtons() {
  const unsigned long now = millis();
  const bool upDown = digitalRead(BUTTON_UP_PIN) == LOW;
  const bool downDown = digitalRead(BUTTON_DOWN_PIN) == LOW;

  if (upDown && !buttonUpHeld) {
    buttonUpSinceMs = now;
  }
  if (downDown && !buttonDownHeld) {
    buttonDownSinceMs = now;
  }
  buttonUpHeld = upDown;
  buttonDownHeld = downDown;

  // Both buttons: toggle the printer's chamber light, once per press.
  if (upDown && downDown) {
    if (!buttonComboHandled) {
      buttonComboHandled = true;
      showFlash("Printer", "LED...");
      if (togglePrinterLed()) {
        showFlash("Printer LED", printerLedOn ? "ON" : "OFF");
      } else {
        showFlash("Printer", "no reply");
      }
    }
    return;
  }
  if (buttonComboHandled) {
    // Ignore whichever button is still down after the combo, so releasing
    // them a moment apart does not move the setpoint.
    if (!upDown && !downDown) {
      buttonComboHandled = false;
      lastButtonUpMs = now;
      lastButtonDownMs = now;
    }
    return;
  }

  if (upDown && now - buttonUpSinceMs >= BUTTON_COMBO_GUARD_MS
      && now - lastButtonUpMs >= BUTTON_DEBOUNCE_MS) {
    lastButtonUpMs = now;
    adjustSetpoint(SETPOINT_STEP_C);
  }
  if (downDown && now - buttonDownSinceMs >= BUTTON_COMBO_GUARD_MS
      && now - lastButtonDownMs >= BUTTON_DEBOUNCE_MS) {
    lastButtonDownMs = now;
    adjustSetpoint(-SETPOINT_STEP_C);
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
      + String(maxFanSpeedPercent, 0) + F("'></label><label>Printer host<input name='printerhost' type='text' value='")
      + printerHost + F("'></label><label>Printer port<input name='printerport' type='number' min='1' max='65535' value='")
      + String(printerPort) + F("'></label><p>Press both buttons together to toggle the printer LED (last set <span class='state'>")
      + (printerLedOn ? F("ON") : F("OFF")) + F("</span>).</p><button type='submit'>Save settings</button></form></main></body></html>");
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
  if (request->hasParam("printerhost", true)) {
    String host = request->getParam("printerhost", true)->value();
    host.trim();
    if (host.length() > 0) {
      printerHost = host;
    }
  }
  if (request->hasParam("printerport", true)) {
    const long port = request->getParam("printerport", true)->value().toInt();
    if (port > 0 && port <= 65535) {
      printerPort = static_cast<uint16_t>(port);
    }
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
  printerHost = preferences.getString("printerHost", DEFAULT_PRINTER_HOST);
  printerPort = preferences.getUShort("printerPort", DEFAULT_PRINTER_PORT);
  printerLedOn = preferences.getBool("printerLedOn", true);

  WiFi.setHostname("AD5X-Chamber");
  AsyncWiFiManager wifiManager(&server, &dns);
  wifiManager.autoConnect(SETUP_AP_NAME, SETUP_AP_PASSWORD);
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

  if (flashUntilMs != 0 && static_cast<long>(millis() - flashUntilMs) >= 0) {
    flashUntilMs = 0;
    updateDisplay();
  }

  if (millis() - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = millis();
    temperatureC = readTemperatureC();
    updateFan();
    updateDisplay();
    Serial.printf("Temperature: %.1f C, fan: %u%%\n", temperatureC, fanDutyPercent);
  }
}
