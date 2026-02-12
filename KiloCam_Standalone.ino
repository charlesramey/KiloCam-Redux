#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "esp_camera.h"
#include "Arduino.h"
#include "FS.h"
#include "SD_MMC.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "driver/rtc_io.h"
#include <sys/time.h>
#include "index_html.h"

// ================= DEBUG CONFIG =================
// Uncomment to enable Debug Mode (No Deep Sleep, Default to Config, No Hardware Check)
// #define DEBUG_MODE

// ================= PIN DEFINITIONS =================
// Using GPIO 33 (Onboard Red LED) for Reed Switch.
// - Default: HIGH (LED OFF, via internal pullup + LED circuit)
// - Magnet Held: LOW (LED ON)
// - Avoids conflict with SD Card DAT3 (GPIO 13)
#define REED_SWITCH_PIN 33
#define LUMEN_PIN       12 // PWM Output

// Standard ESP32-CAM (AI-Thinker) Camera Pins
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ================= SETTINGS & DEFAULTS =================
Preferences preferences;
WebServer server(80);

String devName = "coralcamone";
int intervalSeconds = 300; // 5 minutes
int lightPwmVal = 1500;    // Default Brightness (1100-1900)
int lightWarmup = 1000;    // Warmup time in ms
long currentOffset = 0;    // Timezone offset in seconds

// PWM Properties for Lumen Light (Servo-like)
const int pwmFreq = 50;
const int pwmChannel = 0;
const int pwmResolution = 16;

// ================= FUNCTION PROTOTYPES =================
void startConfigMode();
void startOperatingMode();
void setupCamera();
void setupSD();
void takePicture();
void deepSleep();
void handleRoot();
void handleStatus();
void handleConfig();
void handleTime();
void handleControl();
void handleList();
void handleDelete();
void handleCapture();
void setLight(int us);

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); // Disable brownout detector

  Serial.begin(115200);
  Serial.println("\n\n--- KiloCam Standalone ---");

  #ifdef DEBUG_MODE
    Serial.println("!!! DEBUG MODE ENABLED !!!");
  #endif

  // Init Pins
  pinMode(REED_SWITCH_PIN, INPUT_PULLUP);

  // Lumen Pin Config (Compatible with ESP32 Arduino Core 3.x)
  // Check if ledcAttach is available (Core 3.x)
  #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttach(LUMEN_PIN, pwmFreq, pwmResolution);
  #else
    ledcSetup(pwmChannel, pwmFreq, pwmResolution);
    ledcAttachPin(LUMEN_PIN, pwmChannel);
  #endif

  setLight(0); // Ensure off

  // Load Settings
  preferences.begin("kilocam", false);
  // Name is now hardcoded or set via other means if needed, but per request removed from UI
  // devName = preferences.getString("name", "coralcamone");
  intervalSeconds = preferences.getInt("interval", 300);
  lightPwmVal = preferences.getInt("lightPwm", 1900); // Default to max brightness
  lightWarmup = preferences.getInt("lightDur", 1000);
  currentOffset = preferences.getLong("offset", 0);
  preferences.end();

  #ifdef DEBUG_MODE
    // Force Config Mode for testing
    startConfigMode();
    return;
  #endif

  // Check Wakeup Reason
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();

  if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Wakeup: TIMER -> Operating Mode");
    startOperatingMode();
  } else if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT0) {
    Serial.println("Wakeup: GPIO (Reed Switch) -> Config Mode");
    startConfigMode();
  } else {
    Serial.println("Wakeup: RESET/POWER -> Config Mode");
    // Optionally check Reed Switch state here. If not held, maybe sleep?
    // For now, default to Config Mode on power up.
    startConfigMode();
  }
}

void loop() {
  server.handleClient();

  #ifndef DEBUG_MODE
  // Auto-sleep if no activity for 10 minutes?
  if (millis() > 600000) {
    Serial.println("Timeout: Shutting down.");
    deepSleep();
  }
  #endif
}

// ================= MODES =================

void startOperatingMode() {
  setupSD();
  setupCamera();

  // Turn on Light
  Serial.println("Light ON");
  setLight(lightPwmVal);
  delay(lightWarmup);

  // Take Picture
  takePicture();

  // Turn off Light
  Serial.println("Light OFF");
  setLight(1100); // Off signal for Lumen
  delay(100);
  setLight(0);    // Kill PWM

  deepSleep();
}

void startConfigMode() {
  Serial.println("Starting Config Mode...");

  // Init SD for File Management
  setupSD();
  // Init Camera for Preview
  setupCamera();

  // Start WiFi AP
  WiFi.softAP(devName.c_str());
  Serial.print("AP Started: ");
  Serial.println(WiFi.softAPIP());

  // Start mDNS
  if (MDNS.begin("coralcam")) { // URL: http://coralcam.local
    Serial.println("mDNS responder started");
  }

  // Web Server Routes
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/save-config", handleConfig);
  server.on("/set-time", handleTime);
  server.on("/control", handleControl);
  server.on("/list", handleList);
  server.on("/delete", handleDelete);
  server.on("/capture", handleCapture);

  // Serve static files (Gallery images)
  server.enableCORS(true);
  // Fix for ESP32 Core 3.x: serveStatic requires 3rd arg for root path on FS
  // server.serveStatic("/", SD_MMC); // Old Core 2.x
  server.serveStatic("/", SD_MMC, "/"); // New Core 3.x

  server.begin();
  Serial.println("Web Server Started");
}

// ================= HELPERS =================

void deepSleep() {
  Serial.printf("Going to sleep for %d seconds...\n", intervalSeconds);
  Serial.flush();

  #ifdef DEBUG_MODE
    Serial.println("DEBUG MODE: Sleeping via delay(5000) instead of Deep Sleep.");
    delay(5000);
    // Simulate wakeup
    ESP.restart();
    return;
  #endif

  // Configure Wakeups
  esp_sleep_enable_timer_wakeup(intervalSeconds * 1000000ULL);

  // Wake if Reed Switch (GPIO 33) is LOW
  // GPIO 33 is RTC_GPIO 8.
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_33, 0); // 0 = Low

  esp_deep_sleep_start();
}

void setLight(int us) {
  // 50Hz = 20000us period. 16-bit resolution (65536).
  // Duty = (us / 20000) * 65536
  if (us == 0) {
    #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
       ledcWrite(LUMEN_PIN, 0);
    #else
       ledcWrite(pwmChannel, 0);
    #endif
    return;
  }
  uint32_t duty = (us * 65536) / 20000;

  #if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
     ledcWrite(LUMEN_PIN, duty);
  #else
     ledcWrite(pwmChannel, duty);
  #endif
}

void setupCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_1; // Use Channel 1 (0 is Light)
  config.ledc_timer = LEDC_TIMER_1;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera Init Failed: 0x%x\n", err);
  }
}

void setupSD() {
  // Use 1-bit mode to free up pins 4, 12, 13
  if(!SD_MMC.begin("/sdcard", true)){
    Serial.println("SD Card Mount Failed");
    return;
  }
  uint8_t cardType = SD_MMC.cardType();
  if(cardType == CARD_NONE){
    Serial.println("No SD Card attached");
    return;
  }
}

void takePicture() {
  camera_fb_t * fb = NULL;

  // Clear buffer (take dummy frame if needed, but not usually)
  // fb = esp_camera_fb_get();
  // esp_camera_fb_return(fb);

  fb = esp_camera_fb_get();
  if(!fb) {
    Serial.println("Camera capture failed");
    return;
  }

  // Generate Filename
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  char path[32];
  strftime(path, sizeof(path), "/IMG_%Y%m%d_%H%M%S.jpg", &timeinfo);

  Serial.printf("Saving file: %s\n", path);

  fs::FS &fs = SD_MMC;
  File file = fs.open(path, FILE_WRITE);
  if(!file){
    Serial.println("Failed to open file in writing mode");
  } else {
    file.write(fb->buf, fb->len);
    Serial.println("File saved.");
  }
  file.close();

  esp_camera_fb_return(fb);
}

// ================= HANDLERS =================

void handleRoot() {
  server.send(200, "text/html", index_html);
}

void handleStatus() {
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  char timeStr[20];
  strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);

  String storage = "Unk";
  if (SD_MMC.cardType() != CARD_NONE) {
    uint64_t total = SD_MMC.totalBytes() / (1024 * 1024);
    uint64_t used = SD_MMC.usedBytes() / (1024 * 1024);
    storage = String(used) + " / " + String(total) + " MB";
  }

  String json = "{";
  json += "\"name\":\"" + devName + "\",";
  json += "\"time\":\"" + String(timeStr) + "\",";
  json += "\"storage\":\"" + storage + "\",";
  json += "\"interval\":" + String(intervalSeconds) + ",";
  json += "\"lightPwm\":" + String(lightPwmVal) + ",";
  json += "\"lightDur\":" + String(lightWarmup);
  json += "}";

  server.send(200, "application/json", json);
}

void handleConfig() {
  if (server.hasArg("interval")) {
    // devName = server.arg("name"); // Removed
    intervalSeconds = server.arg("interval").toInt();
    lightPwmVal = server.arg("lightPwm").toInt();
    lightWarmup = server.arg("lightDur").toInt();

    preferences.begin("kilocam", false);
    // preferences.putString("name", devName);
    preferences.putInt("interval", intervalSeconds);
    preferences.putInt("lightPwm", lightPwmVal);
    preferences.putInt("lightDur", lightWarmup);
    preferences.end();

    server.send(200, "text/plain", "Settings Saved.");
  } else {
    server.send(400, "text/plain", "Bad Args");
  }
}

void handleTime() {
  if (server.hasArg("time")) {
    long t = server.arg("time").toInt(); // Epoch seconds
    long tz = 0;
    if (server.hasArg("tz")) {
      tz = server.arg("tz").toInt();  // Timezone offset in minutes
    }

    // Adjust to Local Time (Cheat: Set system time to Local Time)
    t += (tz * 60);

    struct timeval now = { .tv_sec = t, .tv_usec = 0 };
    settimeofday(&now, NULL);

    Serial.println("Time Updated (Local)");
    server.send(200, "text/plain", "Time Synced");
  } else {
    server.send(400, "text/plain", "Missing time");
  }
}

void handleControl() {
  String action = server.arg("action");
  if (action == "start") {
    server.send(200, "text/plain", "Starting...");
    delay(500);
    startOperatingMode(); // Will deep sleep
  } else if (action == "shutdown") {
    server.send(200, "text/plain", "Shutting down...");
    delay(500);
    deepSleep();
  } else if (action == "light") {
    static bool lightState = false;
    lightState = !lightState;
    if (lightState) setLight(lightPwmVal);
    else setLight(1100); // Off
    server.send(200, "text/plain", lightState ? "Light ON" : "Light OFF");
  }
}

void handleList() {
  String json = "[";
  File root = SD_MMC.open("/");
  File file = root.openNextFile();
  bool first = true;
  while(file){
    if(!file.isDirectory()){
      if(!first) json += ",";
      json += "{\"name\":\"" + String(file.name()) + "\",\"size\":" + String(file.size()) + "}";
      first = false;
    }
    file = root.openNextFile();
  }
  json += "]";
  server.send(200, "application/json", json);
}

void handleDelete() {
  if(server.hasArg("path")) {
    String path = server.arg("path");
    if(SD_MMC.remove(path)) server.send(200, "text/plain", "Deleted");
    else server.send(500, "text/plain", "Delete Failed");
  }
}

void handleCapture() {
  camera_fb_t * fb = esp_camera_fb_get();
  if (!fb) {
    server.send(500, "text/plain", "Capture Failed");
    return;
  }
  server.sendHeader("Content-Disposition", "inline; filename=capture.jpg");
  server.send_P(200, "image/jpeg", (const char *)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}
