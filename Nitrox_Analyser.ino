#include <FS.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <EEPROM.h>
#include <esp_bt.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <SimpleKalmanFilter.h>

File fsUploadFile;
SimpleKalmanFilter oxygenKalman(0.02, 1.0, 0.0005);  // R: measurement error, P: estimated error, Q: process noise

// Pin Definition
const uint8_t oxygenPin = A2;  // GPIO 4
const uint8_t groundPin = D6;  // GPIO 21
const uint8_t ledPin = D7;     // GPIO 20

// Calibration
const uint8_t defaultOxygenCalPercentage = 99;  // Oxygen calibration percentage
const float defaultOxygenCalVoltage = 10.0;     // Oxygen voltage in air
const float defaultPureOxygenVoltage = 0.0;     // Oxygen voltage in oxygen
bool isTwoPointCalibrated = false;
bool forceOnePointMode = false;
uint8_t OxygenCalPercentage = defaultOxygenCalPercentage; 
float oxygencalVoltage = defaultOxygenCalVoltage;
float pureoxygenVoltage = defaultPureOxygenVoltage;

// Sampling
const uint8_t samplingRateHz = 240;  // Sampling rate 240 Hz
const uint8_t displayRateHz = 2;     // Display refresh rate 2 Hz
unsigned long lastSampleTime = 0;
unsigned long lastDisplayUpdate = 0;
uint16_t sampleCount = 0;
uint16_t avgSampleCount = 0;
float oxygenVoltage = 0.0;
float avgOxygenVoltage = 0.0;
float filteredOxygenVoltage = 0.0;
float oxygenPercentage = 0.0;
uint16_t mod14 = 0;
uint16_t mod16 = 0;

// LED
const unsigned long ON_TIME = 200;      // On 200ms
const unsigned long OFF_TIME = 200;     // Off 200ms
const unsigned long PAUSE_TIME = 1000;  // Pause 1s between groups
uint8_t blinkTotal = 0;
uint8_t prevBlinkTotal  = 0;
uint8_t blinkCount = 0;
bool inGroupPause = false;
bool ledState = LOW;
bool constantOn = false;
bool prevConstantOn  = false;
unsigned long blinkLastTime = 0;
unsigned long groupPauseTime = 0;

// WiFi Settings
const char *ssid = "Nitrox_Analyser";  // WiFi SSID
const char *password = "12345678";     // WiFi password
WebServer server(80);                  // Web server on port 80

// 21% Oxygen Calibration
void airOxygenCalibration() {
  oxygencalVoltage = filteredOxygenVoltage;
  int index = EEPROM.read(0);
  if (index < 0 || index > 4) index = 0;
  int baseAddr = 4 + index * 12;
  EEPROM.put(baseAddr, OxygenCalPercentage);
  EEPROM.put(baseAddr + 4, oxygencalVoltage);
  EEPROM.put(baseAddr + 8, pureoxygenVoltage);
  EEPROM.write(0, (index + 1) % 5);
  EEPROM.commit();
}

// 100% Oxygen Calibration
void pureOxygenCalibration() {
  pureoxygenVoltage = filteredOxygenVoltage;
  if (pureoxygenVoltage > oxygencalVoltage) {
    isTwoPointCalibrated = true;
    int index = EEPROM.read(0);
    if (index < 0 || index > 4) index = 0;
    int baseAddr = 4 + index * 12;
    EEPROM.put(baseAddr, OxygenCalPercentage);
    EEPROM.put(baseAddr + 4, oxygencalVoltage);
    EEPROM.put(baseAddr + 8, pureoxygenVoltage);
    EEPROM.write(0, (index + 1) % 5);  // update index
    EEPROM.commit();
  }
}

// Read Oxygen Voltage
float getOxygenVoltage() {
  return analogReadMilliVolts(oxygenPin) / 12.8;  // Gain 12.8
}

// Oxygen Percentage Calculation
float getOxygenPercentage() {
  bool isTwoPoint = (!forceOnePointMode && isTwoPointCalibrated);  // Forced one-point calibration
  if (!isTwoPoint) {
    return (avgOxygenVoltage / oxygencalVoltage) * 20.9;  // One-point calibration
  }
  return 20.9 + ((avgOxygenVoltage - oxygencalVoltage) / (pureoxygenVoltage - oxygencalVoltage)) * (OxygenCalPercentage - 20.9);  // Two-point calibration
}

// HTML
const char *htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Nitrox Analyser</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <meta name="apple-mobile-web-app-capable" content="yes">
  <meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
  <meta name="apple-mobile-web-app-title" content="Nitrox Analyser">
  <link rel="apple-touch-icon" href="/icon.png">
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 0; background-color: #f4f4f4; }
    .container { padding: 20px; }
    h1 { font-size: 28px; margin: 0; padding: 6px; background-color: #43B02A; color: #F6EB14; }
    h2 { font-size: 24px; margin: 0; }
    button { padding: 8px 16px; margin-top: 5px; font-size: 20px; }
  </style>
</head>
<body>
  <h1>Nitrox Analyser</h1>
  <div class="container">
    <!-- Oxygen -->
      <div style="font-size: 60px; font-weight: bold;">
        <span id="oxygen">0.0</span><span>%</span>
      </div>
      <div style="margin-top: 20px; font-size: 20px;">
        <span id="avgOxygenVoltage">0.0</span> mV
      </div>

    <!-- MOD -->
      <h2 style="margin-top: 30px;">MOD</h2>
      <div style="display: flex; justify-content: space-around; text-align: center; margin-top: 5px; font-size: 20px;">
        <div>
          <div style="margin: 5px 0;">ppO<sub>2</sub> 1.4</div>
          <div><span id="mod14">0</span> m</div>
        </div>
        <div>
          <div style="margin: 5px 0;">ppO<sub>2</sub> 1.6</div>
          <div><span id="mod16">0</span> m</div>
        </div>
      </div>

    <!-- Calibration -->
      <h2 style="margin-top: 30px;">Calibration</h2>
      <div style="display: flex; justify-content: space-around; text-align: center; margin-top: 5px; font-size: 20px;">
        <div>
          <div style="margin: 5px 0;">21%</div>
          <div><span id="oxygencalVoltage">0.0</span> mV</div>
        </div>
        <div>
          <div style="margin: 5px 0;"><span id="oxygenCalPercentageText">0</span>%</div>
          <div><span id="pureoxygenVoltage">0.0</span> mV</div>
        </div>
      </div>

    <!-- Calibration Buttons -->
      <div style="margin-top: 5px; display: flex; justify-content: center; gap: 60px; flex-wrap: wrap;">
        <button onclick="calibrate('air')">Cal. Low</button>
        <button onclick="calibrate('pure')">Cal. High</button>
      </div>

    <!-- Calibration Status -->
      <div id="calibrationStatus" style="margin-top: 20px; font-size: 18px; color: green;"></div>
      <div id="calibrationMode" style="font-size: 18px; margin-top: 10px;">
        <span id="calibrationType">Calibration: unknown</span>
      </div>
      <div id="bypassButtonContainer" style="margin-top: 10px; display: none;">
        <button id="toggleCalibrationBtn" style="font-size: 18px; padding: 4px 10px;" onclick="toggleCalibrationMode()">Calibration Mode</button>
      </div>

    <!-- Calibration Percentage -->
      <div style="margin: 10px 0; text-align: center;">
        <button style="font-size: 18px; padding: 4px 10px;" onclick="window.location.href='/calibration_percentage'">
          Calibration %
        </button>
      </div>

    <!-- Reset Calibration -->
      <div style="margin: 10px 0; text-align: center;">
        <button style="font-size: 18px; padding: 4px 10px;" onclick="resetCalibration()">
          Reset
        </button>  
      </div>

    <!-- System -->
    <div style="font-size: 16px; margin-top: 10px;">
      Oversampling: <span id="count">0</span>
    </div>

    <!-- Refresh Button -->
    <div style="margin-top: 10px; text-align: center;">
      <button style="font-size: 20px; padding: 4px 10px;" onclick="location.reload();">
        Refresh
      </button>
    </div>
  </div>

  <script>
    function calibrate(type) {
      let url = type === 'air' ? '/calibrate_air' : '/calibrate_pure';
      fetch(url)
        .then(response => response.text())
        .then(data => {
          console.log("Calibration response:", data);
          document.getElementById("calibrationStatus").textContent =
            type === 'air' ? "Low calibration complete!" : "High calibration complete!";
        })
        .catch(error => {
          console.error("Calibration error:", error);
          document.getElementById("calibrationStatus").textContent = "Calibration failed.";
        });
    }

    function toggleCalibrationMode() {
      fetch("/toggle_calibration_mode")
        .then(response => response.text())
        .then(msg => {
          alert(msg);
        })
        .catch(() => alert("Failed to toggle calibration mode."));
    }

    function fetchCalibrationMode() {
      fetch("/data")
        .then(response => response.json())
        .then(data => {
          document.getElementById("calibrationType").textContent = data.calibrationMode;
        });
    }

    function resetCalibration() {
      if (confirm("Reset calibration to default?")) {
        fetch("/reset_calibration")
          .then(() => {
            alert("Rebooting...");
          })
      }
    }

    window.onload = function() {
      setInterval(() => {
        fetch("/data")
          .then(response => response.json())
          .then(data => {
            console.log("Fetched /data:", data);
            document.getElementById("avgOxygenVoltage").textContent = data.avgOxygenVoltage;
            document.getElementById("oxygen").textContent = data.oxygen;
            document.getElementById("mod14").textContent = data.mod14;
            document.getElementById("mod16").textContent = data.mod16;
            document.getElementById("oxygenCalPercentageText").textContent = data.OxygenCalPercentage;
            document.getElementById("oxygencalVoltage").textContent = data.oxygencalVoltage;
            document.getElementById("pureoxygenVoltage").textContent = data.pureoxygenVoltage;
            const isTwoPoint = data.pureoxygenVoltage > 0 && data.pureoxygenVoltage > data.oxygencalVoltage + 10;
            const effectiveMode = (data.calibrationMode === "2-point" && !data.forceOnePointMode) ? "2-point" : "1-point";
            document.getElementById("calibrationType").textContent = "Calibration: " + effectiveMode;
            document.getElementById("bypassButtonContainer").style.display = isTwoPoint ? "block" : "none";
            document.getElementById("count").textContent = data.count;
          })
          .catch(error => {
            console.error("Error fetching /data:", error);
          });
      }, 500);
    };
  </script>
</body>
</html>
)rawliteral";

const char *calibrationPercentagePage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Calibration Percentage</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    body { font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 0; background-color: #f4f4f4; }
    .container { padding: 20px; }
    h1 { margin: 0; padding: 20px; background-color: #333; color: white; }
    .group { margin-top: 20px; padding: 10px; border: 1px solid #ddd; border-radius: 5px; background-color: #fff; }
    .info { font-size: 16px; margin: 10px 0; }
    label { display: inline-block; width: 200px; text-align: right; margin-right: 10px; white-space: nowrap; }
    input { padding: 5px; width: 40px; }
    button { padding: 10px 20px; margin-top: 20px; font-size: 16px; }
  </style>
</head>
<body>
  <h1>Calibration Percentage</h1>
  <div class="container">
    <div class="group">
      <h2>Calibration Percentage</h2>
      <form action="/save" method="GET">
        <div class="info">
          <label for="OxygenCalPercentage">O2 Calibration Percentage:</label>
          <input type="number" id="OxygenCalPercentage" name="OxygenCalPercentage" min="21" max="99" value="99">
        </div>
        <div class="info">
          <button type="submit">Save</button>
        </div>
      </form>
    </div>
    <div class="info">
      <a href="/">Return to Main Page</a>
    </div>
  </div>
</body>
</html>
)rawliteral";

const char *uploadPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Upload Icon</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
</head>
<body>
  <h1>Upload Icon</h1>
  <form method="POST" action="/upload" enctype="multipart/form-data">
    <input type="file" name="upload">
    <input type="submit" value="Upload">
  </form>
  <p><a href="/">Back</a></p>
</body>
</html>
)rawliteral";

// Send Data to Client
void handleData() {
  String json = "{";
  json += "\"avgOxygenVoltage\":\"" + String(avgOxygenVoltage, 2) + "\",";
  json += "\"oxygen\":\"" + String(getOxygenPercentage(), 1) + "\",";
  json += "\"mod14\":\"" + String(mod14) + "\",";
  json += "\"mod16\":\"" + String(mod16) + "\",";
  json += "\"OxygenCalPercentage\":\"" + String(OxygenCalPercentage) + "\",";
  json += "\"oxygencalVoltage\":\"" + String(oxygencalVoltage, 2) + "\",";
  json += "\"pureoxygenVoltage\":\"" + String(pureoxygenVoltage, 2) + "\",";
  json += "\"calibrationMode\":\"" + String((!forceOnePointMode && isTwoPointCalibrated) ? "2-point" : "1-point") + "\",";
  json += "\"count\":\"" + String(avgSampleCount) + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

// Get Calibration Percentage from Client
void handleCalibrationPercentage() {
  if (server.hasArg("OxygenCalPercentage")) {
    OxygenCalPercentage = server.arg("OxygenCalPercentage").toInt();
    int index = EEPROM.read(0);
    if (index < 0 || index > 4) index = 0;
    int baseAddr = 4 + index * 12;
    EEPROM.put(baseAddr, OxygenCalPercentage);
    EEPROM.put(baseAddr + 4, oxygencalVoltage);
    EEPROM.put(baseAddr + 8, pureoxygenVoltage);
    EEPROM.write(0, (index + 1) % 5);  // Update index for next write
    EEPROM.commit();
    String response = "<html><body><h1>Saved!</h1><p>Device is restarting...</p></body></html>";
    server.send(200, "text/html", response);
    esp_restart();
  } else {
    server.send(400, "text/html", "<html><body><h1>Error:</h1><p>Missing parameter.</p></body></html>");
  }
}

// Get File from Client
void handleUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = "/" + upload.filename;
    fsUploadFile = SPIFFS.open(filename, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile) fsUploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) fsUploadFile.close();
    server.send(200, "text/plain", "Upload complete");
  }
}

// Reset Calibration
void handleResetCalibration() {
  int index = EEPROM.read(0);
  if (index < 0 || index > 4) index = 0;
  int baseAddr = 4 + index * 12;
  EEPROM.put(baseAddr, defaultOxygenCalPercentage);
  EEPROM.put(baseAddr + 4, defaultOxygenCalVoltage);
  EEPROM.put(baseAddr + 8, defaultPureOxygenVoltage);
  EEPROM.write(0, (index + 1) % 5);  // Advance index
  EEPROM.commit();
  esp_restart();  // Restart after reset
}


void setup() {
  setCpuFrequencyMhz(80);         // Reduce CPU frequency
  esp_bt_controller_disable();    // Turn off bluetooth
  pinMode(oxygenPin, INPUT);      // Oxygen input
  pinMode(groundPin, OUTPUT);     // LED ground
  digitalWrite(groundPin, LOW);
  pinMode(ledPin, OUTPUT);        // LED pin
  digitalWrite(ledPin, LOW);
  analogReadResolution(12);       // Internal ADC 12-bit
  analogSetAttenuation(ADC_0db);  // Internal ADC 1.1V range 
  EEPROM.begin(64);               // EEPROM start
  SPIFFS.begin(true);             // Mount SPIFFS filesystem
  server.serveStatic("/icon.png", SPIFFS, "/icon.png");

  // Load Calibration Values
  int index = EEPROM.read(0);
  if (index < 0 || index > 4) index = 0;
  int baseAddr = 4 + ((index + 4) % 5) * 12;
  EEPROM.get(baseAddr, OxygenCalPercentage);
  EEPROM.get(baseAddr + 4, oxygencalVoltage);
  EEPROM.get(baseAddr + 8, pureoxygenVoltage);

  // Default Calibration Values
  if (isnan(OxygenCalPercentage) || OxygenCalPercentage <= 0.0) {
    OxygenCalPercentage = defaultOxygenCalPercentage;
  }
  if (isnan(oxygencalVoltage) || oxygencalVoltage <= 0.0) {
    oxygencalVoltage = defaultOxygenCalVoltage;
  }
  if (isnan(pureoxygenVoltage) || pureoxygenVoltage <= 0.0) {
    pureoxygenVoltage = defaultPureOxygenVoltage;
  }

  // Two-point Calibration
  if (pureoxygenVoltage > oxygencalVoltage) {
    isTwoPointCalibrated = true;
  } else {
    isTwoPointCalibrated = false;
  }

  // Wifi
  WiFi.softAP(ssid, password, 1);  // Channel 1
  esp_wifi_set_max_tx_power(20);   // 5 dBm
  server.on("/", []() {
    server.send(200, "text/html", htmlPage);
  });
  server.on("/data", handleData);
  server.on("/save", handleCalibrationPercentage);
  server.on("/calibrate_air", []() {
    airOxygenCalibration();
    server.send(200, "text/html",
      "<html><body><h1>21% Calibrated</h1>"
      "<p><a href='/'>Back to Main Page</a></p></body></html>");
  });
  server.on("/calibrate_pure", []() {
    pureOxygenCalibration();
    server.send(200, "text/html",
      "<html><body><h1>High Point Calibrated</h1>"
      "<p><a href='/'>Back to Main Page</a></p></body></html>");
  });
  server.on("/toggle_calibration_mode", HTTP_GET, []() {
    forceOnePointMode = !forceOnePointMode;
    String mode = forceOnePointMode ? "1-point mode" : "2-point mode";
    server.send(200, "text/plain", mode);
  });
  server.on("/calibration_percentage", []() {
    server.send(200, "text/html", calibrationPercentagePage);
  });
  server.on("/reset_calibration", HTTP_GET, handleResetCalibration);
  server.on("/upload_page", HTTP_GET, []() {
    server.send(200, "text/html", uploadPage);
  });
  server.on("/upload", HTTP_POST, []() {
  }, handleUpload);
  server.begin();
  server.handleClient();
}


void loop() {
  unsigned long currentTime = millis();
  
  // HTML Update
  server.handleClient();

  // Sampling
  if (currentTime - lastSampleTime >= (1000 / samplingRateHz)) {
    lastSampleTime = currentTime;
    oxygenVoltage = getOxygenVoltage();
    filteredOxygenVoltage = oxygenKalman.updateEstimate(oxygenVoltage);  // Kalman filter
    sampleCount++;
  }

  // Refresh
  if (currentTime - lastDisplayUpdate >= (1000 / displayRateHz)) {
    lastDisplayUpdate = currentTime;

    // Calculate oxygen voltage
    avgOxygenVoltage = filteredOxygenVoltage;
    avgSampleCount = sampleCount;
    sampleCount = 0;

    // Calculate oxygen percentage
    oxygenPercentage = getOxygenPercentage();
    if (oxygenPercentage < 0.0) oxygenPercentage = 0.0;  // Minimum oxygen percentage 0%

    // Calculate MOD
    mod14 = (oxygenPercentage > 0) ? (int)((1400.0 / oxygenPercentage) - 10.0) : 0;  // ppO2 1.4
    if (mod14 > 999) mod14 = 999;  // Maximum MOD 999 m
    mod16 = (oxygenPercentage > 0) ? (int)((1600.0 / oxygenPercentage) - 10.0) : 0;  // ppO2 1.6
    if (mod16 > 999) mod16 = 999;  // Maximum MOD 999 m

    // LED reset
    bool newConstantOn = (oxygenPercentage >= 95.0f);
    uint8_t newBlinkTotal;
    if (newConstantOn || oxygenPercentage < 5.0f) {
      newBlinkTotal = 0;
    }
    else {
      float pct = oxygenPercentage - 5.0f;
      uint8_t bucket = (uint8_t)(pct / 10.0f);
      newBlinkTotal = bucket + 1;
    }
    if (newConstantOn != prevConstantOn || newBlinkTotal != prevBlinkTotal) {
      constantOn = newConstantOn;
      blinkTotal = newBlinkTotal;
      blinkCount = 0;
      inGroupPause = false;
      ledState = LOW;
      digitalWrite(ledPin, LOW);
      blinkLastTime = currentTime;
    }
    prevConstantOn = newConstantOn;
    prevBlinkTotal = newBlinkTotal;
  }

  // LED Blink
  if (constantOn) {
    digitalWrite(ledPin, HIGH);
  } 
  else if (blinkTotal > 0) {
    if (!inGroupPause) {
    unsigned long interval = ledState ? OFF_TIME : ON_TIME;
      if (currentTime - blinkLastTime >= interval) {
        ledState = !ledState;
        digitalWrite(ledPin, ledState);
        blinkLastTime = currentTime;
        if (!ledState) {
          blinkCount++;
          if (blinkCount >= blinkTotal) {
            inGroupPause = true;
            groupPauseTime = currentTime;
            digitalWrite(ledPin, LOW);
          }
        }
      }
    }
    else {
      digitalWrite(ledPin, LOW);
      if (currentTime - groupPauseTime >= PAUSE_TIME) {
        inGroupPause = false;
        blinkCount = 0;
        blinkLastTime = currentTime;
      }
    }
  }
  else {
    digitalWrite(ledPin, LOW);
  }
}