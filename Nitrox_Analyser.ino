#include <Update.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <EEPROM.h>
#include <esp_bt.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <SimpleKalmanFilter.h>

// Firmware Version
#define FIRMWARE_VERSION 1.2.1
#define STRINGIFY(x) #x
#define TOSTRING(x)  STRINGIFY(x)

File fsUploadFile;
WebServer server(80);  // Web server on port 80
SimpleKalmanFilter oxygenKalman(0.02, 1.0, 0.001);  // R: measurement error, P: estimated error, Q: process noise

// Pin Definition
const uint8_t oxygenPin = A2;  // GPIO 4
const uint8_t groundPin = D6;  // GPIO 21
const uint8_t ledPin = D7;     // GPIO 20

// Calibration
const uint8_t defaultOxygenCalPercentage = 99;  // Oxygen calibration percentage
const float defaultOxygenCalVoltage = 9.0;      // Oxygen voltage in air
const float defaultPureOxygenVoltage = 0.0;     // Oxygen voltage in oxygen
const float defaultpgaGain = 13.8;              // PGA gain 
uint8_t OxygenCalPercentage = defaultOxygenCalPercentage; 
float oxygencalVoltage = defaultOxygenCalVoltage;
float pureoxygenVoltage = defaultPureOxygenVoltage;
float pgaGain = defaultpgaGain;
bool isTwoPointCalibrated = false;              // Two-point calibration
bool forceOnePointMode = false;                 // Overide two-point calibration

// EEPROM Addresses
const int ADDR_OXYGEN_CAL_PERCENT = 0;   // uint8_t
const int ADDR_OXYGEN_CAL_VOLTAGE = 4;   // float
const int ADDR_PURE_OXYGEN_VOLTAGE = 8;  // float
const int ADDR_GAIN = 12;                // float

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

// 21% Oxygen Calibration
void airOxygenCalibration() {
  oxygencalVoltage = filteredOxygenVoltage;
  EEPROM.put(ADDR_OXYGEN_CAL_VOLTAGE, oxygencalVoltage);
  EEPROM.commit();
}

// 100% Oxygen Calibration
bool pureOxygenCalibration() {
  float newPureVoltage = filteredOxygenVoltage;
  if (newPureVoltage <= oxygencalVoltage) return false;  // must be higher than 21% calibration
  pureoxygenVoltage = newPureVoltage;
  isTwoPointCalibrated = true;
  EEPROM.put(ADDR_PURE_OXYGEN_VOLTAGE, pureoxygenVoltage);
  EEPROM.commit();
  return true;
}

// Calibration Percentage
void handleCalibrationPercentage() {
  if (server.hasArg("OxygenCalPercentage")) {
    OxygenCalPercentage = server.arg("OxygenCalPercentage").toInt();
    EEPROM.put(ADDR_OXYGEN_CAL_PERCENT, OxygenCalPercentage);
    EEPROM.commit();
    String response = "<html><body><h1>Saved!</h1><p>Rebooting</p></body></html>";
    server.send(200, "text/html", response);
    esp_restart();
  } else {
    server.send(400, "text/html", "<html><body><h1>Error:</h1><p>Missing parameter.</p></body></html>");
  }
}

// PGA Gain
void handleSaveGain() {
  if (!server.hasArg("gain")) {
    server.send(400, "text/html", "<h1>Error: Missing gain value</h1>");
    return;
  }
  float g = server.arg("gain").toFloat();
  if (g < 0.1f || g > 99.9f) {
    server.send(400, "text/html", "<h1>Error: Gain out of range</h1>");
    return;
  }
  pgaGain = g;
  EEPROM.put(ADDR_GAIN, pgaGain);
  EEPROM.commit();
  server.send(200, "text/html", "<html><body><h1>Saved!</h1><p>Rebooting...</p></body></html>");
  esp_restart();
}

// Reset Calibration
void handleResetCalibration() {
  EEPROM.put(ADDR_OXYGEN_CAL_PERCENT, defaultOxygenCalPercentage);
  EEPROM.put(ADDR_OXYGEN_CAL_VOLTAGE, defaultOxygenCalVoltage);
  EEPROM.put(ADDR_PURE_OXYGEN_VOLTAGE, defaultPureOxygenVoltage);
  EEPROM.put(ADDR_GAIN, defaultpgaGain);
  EEPROM.commit();
  esp_restart();
}

// Oxygen Percentage Calculation
float getOxygenPercentage() {
  bool isTwoPoint = (!forceOnePointMode && isTwoPointCalibrated);  // Overide two-point calibration
  if (!isTwoPoint) {
    return (avgOxygenVoltage / oxygencalVoltage) * 20.9;  // One-point calibration
  }
  return 20.9 + ((avgOxygenVoltage - oxygencalVoltage) / (pureoxygenVoltage - oxygencalVoltage)) * (OxygenCalPercentage - 20.9);  // Two-point calibration
}

// HTML Main Page
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
      <div id="bypassButtonContainer" style="margin-top: 10px;">
        <button id="toggleCalibrationBtn" style="font-size: 18px; padding: 4px 10px;" onclick="toggleCalibrationMode()">
          1-Point Calibration
        </button>
      </div>
     <!-- Settings -->
      <div style="margin-top: 30px; text-align: center;">
        <button style="font-size: 18px; padding: 4px 10px;" onclick="window.location.href='/settings'">
          Setting
        </button>
      </div>
    <!-- System Info -->
    <div style="font-size: 16px; margin-top: 20px;">
      Oversampling: <span id="count">0</span>
    </div>
    <div style="font-size: 16px; margin-top: 5px;">
      Gain: <span id="gain">0.0</span>
    </div>
    <!-- Refresh Button -->
    <div style="margin-top: 20px; text-align: center;">
      <button style="font-size: 20px; padding: 4px 10px;" onclick="location.reload();">
        Refresh
      </button>
    </div>
  </div>

  <script>
    function calibrate(type) {
      const url = type === 'air' ? '/calibrate_air' : '/calibrate_pure';
      fetch(url)
        .then(response => response.text().then(data => {
          if (!response.ok) {
            throw new Error(data);
          }
          document.getElementById("calibrationStatus").style.color = "green";
          document.getElementById("calibrationStatus").textContent = 
            type === 'air' ? "low point calibration completed" : "high point calibration completed";
        }))
        .catch(error => {
          console.error("Calibration error:", error);
          document.getElementById("calibrationStatus").style.color = "red";
          document.getElementById("calibrationStatus").textContent =
            type === 'pure' ? "failed: voltage too low" : "Error";
        });
    }
    function toggleCalibrationMode() {
      const btn = document.getElementById("toggleCalibrationBtn");
      const isTwoPointAvailable = btn.dataset.isTwoPointAvailable === "true";
      const currentMode = btn.dataset.currentMode;
      if (currentMode === "1" && !isTwoPointAvailable) {
        alert("2-Point Calibration not available");
        return;
      }
      fetch("/toggle_calibration_mode")
        .then(response => response.text())
        .then(msg => {
          alert("Switched to " + msg);
        })
        .catch(() => alert("Error"));
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
            document.getElementById("avgOxygenVoltage").textContent = data.avgOxygenVoltage;
            document.getElementById("oxygen").textContent = data.oxygen;
            document.getElementById("mod14").textContent = data.mod14;
            document.getElementById("mod16").textContent = data.mod16;
            document.getElementById("oxygenCalPercentageText").textContent = data.OxygenCalPercentage;
            document.getElementById("oxygencalVoltage").textContent = data.oxygencalVoltage;
            document.getElementById("pureoxygenVoltage").textContent = data.pureoxygenVoltage;
            const isTwoPoint = parseFloat(data.pureoxygenVoltage) > parseFloat(data.oxygencalVoltage);
            const currentModeIs2Point = (data.calibrationMode === "2-point" && !data.forceOnePointMode);
            const modeText = currentModeIs2Point ? "2-Point Calibration" : "1-Point Calibration";
            document.getElementById("toggleCalibrationBtn").textContent = modeText;
            document.getElementById("toggleCalibrationBtn").dataset.isTwoPointAvailable = isTwoPoint;
            document.getElementById("toggleCalibrationBtn").dataset.currentMode = currentModeIs2Point ? "2" : "1";
            document.getElementById("gain").textContent = data.gain;
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


// HTML Setting Page
const char *settingsPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Settings</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    body { font-family: Arial, sans-serif; text-align: center; background-color: #f4f4f4; margin:0; padding:0; }
    .container { padding:20px; }
    h1 { margin:0; padding:20px; background:#333; color:#fff; }
    button { padding:10px 20px; margin:30px; font-size:20px; }
  </style>
</head>
<body>
  <h1>Setting</h1>
  <div class="container">
    <!-- Calibrate High % -->
    <div>
      <button onclick="window.location.href='/calibration_percentage'">
        Calibrate High % 
      </button>
    </div>
    <!-- PGA Gain -->
    <div>
      <button onclick="window.location.href='/gain'">
        PGA Gain
      </button>
    </div>
    <!-- Reset Calibration -->
    <div>
      <button onclick="if(confirm('Reset calibration to default?')) window.location.href='/reset_calibration';">
        Reset Calibration
      </button>
    </div>
    <!-- Firmware Update -->
    <div>
      <button onclick="window.location.href='/firmware'">
        Firmware Update
      </button>
    </div>
  </div>
</body>
</html>
)rawliteral";

// HTML Calibration Percentage Setting Page
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
  </div>
</body>
</html>
)rawliteral";

// HTML PGA Gain Setting Page
const char *gainPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>PGA Gain Setting</title>
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
  <h1>PGA Gain Setting</h1>
  <div class="container">
    <div class="group">
      <h2>Gain Value</h2>
      <form action="/save_gain" method="GET">
        <div class="info">
          <label for="gain">PGA Gain:</label>
          <input type="number" id="gain" name="gain" min="0.1" max="99.9" step="0.1" value="13.8">
        </div>
        <div class="info">
          <button type="submit">Save</button>
        </div>
      </form>
    </div>
  </div>
</body>
</html>
)rawliteral";

// HTML Firmware Update Page
const char *firmwarePage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Firmware OTA</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <style>
    .progress-bar { width: 100%; background-color: #ddd; border-radius: 5px; overflow: hidden; margin-top: 10px; }
    .progress { height: 20px; width: 0%; background-color: #4caf50; transition: width 0.4s; }
  </style>
</head>
<body>
  <h1>Firmware Update</h1>
  <p style="font-size:16px; margin-bottom:12px;">
    Current version: )rawliteral" TOSTRING(FIRMWARE_VERSION) R"rawliteral(
  </p>
  <form id="uploadForm" method="POST" action="/update" enctype="multipart/form-data" onsubmit="return checkFile();">
    <input type="file" name="firmware" id="fileInput" accept=".bin" required>
    <div class="progress-bar"><div class="progress" id="progress"></div></div>
    <input type="submit" value="Upload">
    <p id="uploadStatus"></p>
  </form>
  <p><a href="/">Back</a></p>

  <script>
    document.getElementById("fileInput").addEventListener("change", function() {
      let fileName = this.files[0]?.name || "No file selected";
      document.getElementById("uploadStatus").textContent = "Selected: " + fileName;
    });
    function checkFile() {
      let fileInput = document.getElementById("fileInput");
      let file = fileInput.files[0];
      if (!file) {
        alert("Select a bin file.");
        return false;
      }
      if (file.name.split('.').pop().toLowerCase() !== "bin") {
        alert("Invalid file type.");
        return false;
      }
      if (file.size > 1800000) {
        alert("File size exceeded 1.8 MB.");
        return false;
      }
      uploadFirmware(file);
      return false;
    }
    function uploadFirmware(file) {
      let xhr = new XMLHttpRequest();
      let formData = new FormData();
      formData.append("firmware", file);

      xhr.upload.onprogress = function(event) {
        let percent = Math.round((event.loaded / event.total) * 100);
        document.getElementById("progress").style.width = percent + "%";
        document.getElementById("uploadStatus").textContent = "Uploading... " + percent + "%";
      };
      xhr.onload = function() {
        if (xhr.status === 200) {
          document.getElementById("uploadStatus").textContent = "Upload complete. Device rebooting...";
        } else if (xhr.status === 413) {
          document.getElementById("uploadStatus").textContent = "File too large! Upload failed.";
        } else {
          document.getElementById("uploadStatus").textContent = "Upload failed!";
        }
      };
      xhr.onerror = function() {
        document.getElementById("uploadStatus").textContent = "Upload error!";
      };
      xhr.open("POST", "/update", true);
      xhr.send(formData);
    }
  </script> 
</body>
</html>
)rawliteral";

// HTML App Icon Upload Page (no link in UI, must manual input URL)
const char *uploadPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Upload Icon</title>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
</head>
<body>
  <h1>Upload Icon</h1>
  <form id="iconForm" method="POST" action="/upload" enctype="multipart/form-data" onsubmit="return checkIcon();">
    <label for="iconInput">Choose a PNG (max 20KB):</label><br>
    <input type="file" name="upload" id="iconInput" accept=".png" required><br>
    <div id="iconStatus" class="info"></div>
    <input type="submit" value="Upload">
  </form>
  <p><a href="/">Return</a></p>

  <script>
    const iconInput = document.getElementById('iconInput');
    const iconStatus = document.getElementById('iconStatus');
    iconInput.addEventListener('change', () => {
      const file = iconInput.files[0];
      if (file) {
        const sizeKB = (file.size / 1024).toFixed(1);
        iconStatus.textContent = `Selected: ${file.name} (${sizeKB} KB)`;
      } else {
        iconStatus.textContent = '';
      }
    });
    function checkIcon() {
      const file = iconInput.files[0];
      if (!file) {
        alert('Please select a file.');
        return false;
      }
      const ext = file.name.split('.').pop().toLowerCase();
      if (!file.name.toLowerCase().endsWith('.png')) {
        alert('Invalid file type.');
        return false;
      }
      if (file.size > 20 * 1024) {
        alert('File size over 20 kB.');
        return false;
      }
      return true;
    }
  </script>
</body>
</html>
)rawliteral";

// Send Data
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
  json += "\"gain\":\"" + String(pgaGain, 1) + "\",";
  json += "\"count\":\"" + String(avgSampleCount) + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

// Firmware Update
void handleOTAUpload() {
  HTTPUpload& upload = server.upload();
  switch (upload.status) {
    case UPLOAD_FILE_START:
      Serial.printf("OTA: Begin %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
      break;
    case UPLOAD_FILE_WRITE:
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
      break;
    case UPLOAD_FILE_END:
      if (Update.end(true)) {  // true = reboot when done
        Serial.printf("OTA: Success, %u bytes\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
      break;
    default:
      break;
  }
}

void handleOTAFinish() {
  server.sendHeader("Connection", "close");
  if (Update.hasError()) {
    server.send(200, "text/plain", "Update Failed");
  } else {
    server.send(200, "text/plain", "Update Successful. Rebooting...");
  }
  delay(100);
  ESP.restart();
}

// Get Icon File
void handleUpload() {
  HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = "/" + upload.filename;
    fsUploadFile = SPIFFS.open(filename, FILE_WRITE);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (fsUploadFile) fsUploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (fsUploadFile) fsUploadFile.close();
  }
}


void setup() {
  esp_bt_controller_disable();    // Turn off bluetooth
  pinMode(oxygenPin, INPUT);      // Oxygen input
  pinMode(groundPin, OUTPUT);     // Ground for LED
  digitalWrite(groundPin, LOW);   // Digital low
  pinMode(ledPin, OUTPUT);        // LED pin
  digitalWrite(ledPin, LOW);      // LED off
  analogReadResolution(12);       // ADC 12-bit
  analogSetAttenuation(ADC_0db);  // ADC 1.1V range 
  EEPROM.begin(32);               // EEPROM start
  SPIFFS.begin(true);             // SPIFFS filesystem start

  // Load EEPROM
  EEPROM.get(ADDR_OXYGEN_CAL_PERCENT, OxygenCalPercentage);
  EEPROM.get(ADDR_OXYGEN_CAL_VOLTAGE, oxygencalVoltage);
  EEPROM.get(ADDR_PURE_OXYGEN_VOLTAGE, pureoxygenVoltage);
  EEPROM.get(ADDR_GAIN, pgaGain);

  // Set to Default Values at First Initiation
  if (isnan(OxygenCalPercentage) || OxygenCalPercentage <= 0.0) {
    OxygenCalPercentage = defaultOxygenCalPercentage;
  }
  if (isnan(oxygencalVoltage) || oxygencalVoltage <= 0.0) {
    oxygencalVoltage = defaultOxygenCalVoltage;
  }
  if (isnan(pureoxygenVoltage) || pureoxygenVoltage <= 0.0) {
    pureoxygenVoltage = defaultPureOxygenVoltage;
  }
  if (isnan(pgaGain) || pgaGain <= 0.0) {
    pgaGain = defaultpgaGain;
  }

  // Two-point Calibration
  if (pureoxygenVoltage > oxygencalVoltage) {
    isTwoPointCalibrated = true;
  } else {
    isTwoPointCalibrated = false;
  }

  // Wifi
  WiFi.softAP(ssid, password, 1);  // WiFi channel 1
  esp_wifi_set_max_tx_power(40);   // WiFi AP power 10 dBm

  // HTML Server
  server.serveStatic("/icon.png", SPIFFS, "/icon.png");
  server.on("/", []() {
    server.send(200, "text/html", htmlPage);
  });
  server.on("/data", handleData);
  server.on("/calibrate_air", []() {
    airOxygenCalibration();
    server.send(200, "text/html",
      "<html><body><h1>21% Calibrated</h1>"
      "<p><a href='/'>Back to Main Page</a></p></body></html>");
  });
  server.on("/calibrate_pure", []() {
    if (!pureOxygenCalibration()) {
      server.send(400, "text/html",
        "<html><body><h1>Calibration Failed</h1><p><a href='/'>Back to Main Page</a></p></body></html>");
      return;
    }
    server.send(200, "text/html",
      "<html><body><h1>High Point Calibrated</h1><p><a href='/'>Back to Main Page</a></p></body></html>");
  });
  server.on("/toggle_calibration_mode", HTTP_GET, []() {
    forceOnePointMode = !forceOnePointMode;
    String mode = forceOnePointMode ? "1-point mode" : "2-point mode";
    server.send(200, "text/plain", mode);
  });
  server.on("/settings", []() {
    server.send(200, "text/html", settingsPage);
  });
  server.on("/calibration_percentage", []() {
    server.send(200, "text/html", calibrationPercentagePage);
  });
  server.on("/save", HTTP_GET, handleCalibrationPercentage);
  server.on("/reset_calibration", HTTP_GET, handleResetCalibration);
  server.on("/upload_page", HTTP_GET, []() {
    server.send(200, "text/html", uploadPage);
  });
  server.on("/upload", HTTP_POST,
    []() {
      server.send(200, "text/html",
        "<html><body>"
          "<h1>Upload Successful</h1>"
          "<p><a href=\"/settings\">Back to Settings</a></p>"
        "</body></html>"
      );
    },
    handleUpload
  );
  server.on("/gain", HTTP_GET, []() {
    server.send(200, "text/html", gainPage);
  });
  server.on("/save_gain", HTTP_GET, handleSaveGain);
  server.on("/firmware", HTTP_GET, []() {
    server.send(200, "text/html", firmwarePage);
  });
  server.on("/update", HTTP_POST, handleOTAFinish, handleOTAUpload);
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
    oxygenVoltage = analogReadMilliVolts(oxygenPin) / pgaGain;           // Gain corrected oxygen voltage
    filteredOxygenVoltage = oxygenKalman.updateEstimate(oxygenVoltage);  // Kalman filter
    sampleCount++;
  }

  // HTML Refresh
  if (currentTime - lastDisplayUpdate >= (1000 / displayRateHz)) {
    lastDisplayUpdate = currentTime;

    // Calculate oxygen voltage
    avgOxygenVoltage = filteredOxygenVoltage;
    avgSampleCount = sampleCount;
    sampleCount = 0;

    // Calculate oxygen percentage
    oxygenPercentage = getOxygenPercentage();
    if (oxygenPercentage < 0.0) oxygenPercentage = 0.0;  // Minimum 0%

    // Calculate MOD
    mod14 = (oxygenPercentage > 0) ? (int)((1400.0 / oxygenPercentage) - 10.0) : 0;  // ppO2 1.4
    if (mod14 > 999) mod14 = 999;                                                    // Maximum 999 m
    mod16 = (oxygenPercentage > 0) ? (int)((1600.0 / oxygenPercentage) - 10.0) : 0;  // ppO2 1.6
    if (mod16 > 999) mod16 = 999;                                                    // Maximum 999 m

    // LED reset
    bool newConstantOn = (oxygenPercentage >= 95.0);  // LED constant on when O2 > 95%
    uint8_t newBlinkTotal;
    if (newConstantOn || oxygenPercentage < 5.0) {
      newBlinkTotal = 0;                              // LED constant off when O2 < 5%
    }
    else {
      float pct = oxygenPercentage - 5.0;             // Closest 10%
      uint8_t bucket = (uint8_t)(pct / 10.0);
      newBlinkTotal = bucket + 1;                     // Number of blinks
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
