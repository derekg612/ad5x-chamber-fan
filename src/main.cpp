#include <Arduino.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <ESPAsyncWiFiManager.h>
#include <Preferences.h>
#include <WiFi.h>

// Xiao ESP32-C3 pins. Use a 3.3 V thermistor divider and a MOSFET for the fan.
constexpr uint8_t THERMISTOR_PIN = D0;
constexpr uint8_t FAN_PIN = D1;

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

AsyncWebServer server(80);
DNSServer dns;
Preferences preferences;
float setpointC = DEFAULT_SETPOINT_C;
float topOffsetC = DEFAULT_TOP_OFFSET_C;
float bottomOffsetC = DEFAULT_BOTTOM_OFFSET_C;
float temperatureC = NAN;
bool fanOn = false;
unsigned long lastSampleMs = 0;

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
  request->redirect("/");
}

void setup() {
  Serial.begin(115200);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);
  analogReadResolution(12);

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

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", page());
  });
  server.on("/settings", HTTP_POST, handleSettings);
  server.begin();
}

void loop() {
  if (millis() - lastSampleMs >= SAMPLE_INTERVAL_MS) {
    lastSampleMs = millis();
    temperatureC = readTemperatureC();
    updateFan();
    Serial.printf("Temperature: %.1f C, fan: %s\n", temperatureC, fanOn ? "ON" : "OFF");
  }
}