#include <WiFi.h>
#include <FirebaseESP32.h>
#include <DHT.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include "LittleFS.h"

// Firebase
#define FIREBASE_HOST "https://projekmbkm-default-rtdb.firebaseio.com/"
#define FIREBASE_AUTH "Ep4yNIG8bFk1io8hCzoYMZtdL35zBjVI2SS0MxYs"

// DHT Sensor
#define DHTPIN 4
#define DHTTYPE DHT11
#define SOIL_MOISTURE_PIN 34

DHT dht(DHTPIN, DHTTYPE);

// Firebase configuration
FirebaseConfig firebaseConfig;
FirebaseAuth firebaseAuth;
FirebaseData firebaseData;

// LittleFS configuration
const char* ssidPath = "/ssid.txt";
const char* passPath = "/pass.txt";

// WiFi Manager
AsyncWebServer server(80);
String ssid, pass;
const char* PARAM_INPUT_1 = "ssid";
const char* PARAM_INPUT_2 = "pass";

// Soil moisture thresholds
int dryValue = 2600;
int wetValue = 1000;

// Functions for LittleFS
void initLittleFS() {
  if (!LittleFS.begin(true)) {
    Serial.println("Failed to mount LittleFS");
    return;
  }
  Serial.println("LittleFS mounted successfully");
}

String readFile(fs::FS &fs, const char *path) {
  File file = fs.open(path);
  if (!file || file.isDirectory()) return String();
  String content = file.readStringUntil('\n');
  file.close();
  return content;
}

void writeFile(fs::FS &fs, const char *path, const char *message) {
  File file = fs.open(path, FILE_WRITE);
  if (file) file.print(message);
  file.close();
}

// WiFi initialization with DHCP
bool initWiFi() {
  if (ssid == "") return false;  // Pastikan SSID ada
  WiFi.mode(WIFI_STA);          // Set ESP32 sebagai Station
  WiFi.begin(ssid.c_str(), pass.c_str()); // Hubungkan ke WiFi dengan SSID dan Password

  unsigned long startMillis = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startMillis >= 5000) { // Timeout setelah 5 detik
      return false; 
    }
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.println("IP Address: " + WiFi.localIP().toString()); // Tampilkan IP DHCP
  return true;
}

void setupWiFiManager() {
  WiFi.softAP("TANDUR_INDOBOT");
  
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(LittleFS, "/wifimanager.html", "text/html", false);
  });

  server.on("/", HTTP_POST, [](AsyncWebServerRequest *request) {
    int params = request->params();
    for (int i = 0; i < params; i++) {
      const AsyncWebParameter* p = request->getParam(i);
      if (p->name() == PARAM_INPUT_1) writeFile(LittleFS, ssidPath, p->value().c_str());
      if (p->name() == PARAM_INPUT_2) writeFile(LittleFS, passPath, p->value().c_str());
    }
    request->send(200, "text/plain", "Done. Restarting...");
    delay(3000);
    ESP.restart();
  });

  server.serveStatic("/style.css", LittleFS, "/style.css"); // Tambahkan baris ini
  server.begin();
}

// Setup
void setup() {
  Serial.begin(115200);
  initLittleFS();

  ssid = readFile(LittleFS, ssidPath);
  pass = readFile(LittleFS, passPath);

  if (!initWiFi()) {
    Serial.println("Setting up WiFi Manager");
    setupWiFiManager();
    return;
  }

  // Firebase initialization
  firebaseConfig.host = FIREBASE_HOST;
  firebaseConfig.signer.tokens.legacy_token = FIREBASE_AUTH;
  Firebase.begin(&firebaseConfig, &firebaseAuth);

  // DHT initialization
  dht.begin();
}

// Loop
void loop() {
  static unsigned long lastWiFiCheck = millis();
  static bool wifiManagerActive = false;

  // Periksa koneksi WiFi setiap 5 detik
  if (millis() - lastWiFiCheck >= 5000) {
    lastWiFiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      if (!wifiManagerActive) {
        Serial.println("WiFi disconnected. Activating WiFi Manager...");
        setupWiFiManager();
        wifiManagerActive = true;
      }
    } else {
      wifiManagerActive = false;
    }
  }

  // Jika WiFi terhubung, baca data sensor dan kirim ke Firebase
  if (WiFi.status() == WL_CONNECTED) {
    float temperature = dht.readTemperature();
    float humidity = dht.readHumidity();
    int soilMoistureValue = analogRead(SOIL_MOISTURE_PIN);
    int soilMoisturePercent = map(soilMoistureValue, dryValue, wetValue, 0, 100);
    soilMoisturePercent = constrain(soilMoisturePercent, 0, 100);
    String soilStatus;
    String recommendedPlant;

    if (soilMoisturePercent < 30) {
      soilStatus = "Kering";
      recommendedPlant = "Tidak ada rekomendasi";
    } else if (soilMoisturePercent >= 30 && soilMoisturePercent < 50) {
      soilStatus = "Cukup kering";
      recommendedPlant = "Tomat";
    } else if (soilMoisturePercent >= 50 && soilMoisturePercent <= 60) {
      soilStatus = "Lembab";
      recommendedPlant = "Kecipir, Buncis";
    } else if (soilMoisturePercent > 60 && soilMoisturePercent <= 70) {
      soilStatus = "Lembab";
      recommendedPlant = "Gambas/Oyong";
    } else if (soilMoisturePercent > 70 && soilMoisturePercent <= 80) {
      soilStatus = "Basah";
      recommendedPlant = "Terong, Kacang Tanah";
    } else if (soilMoisturePercent > 80 && soilMoisturePercent <= 90) {
      soilStatus = "Sangat Basah";
      recommendedPlant = "Kubis";
    } else {
      soilStatus = "Jenuh Air";
      recommendedPlant = "Tidak ada rekomendasi";
    }

    if (!isnan(temperature) && !isnan(humidity)) {
      Firebase.setFloat(firebaseData, "/SensorData/Temperature", temperature);
      Firebase.setFloat(firebaseData, "/SensorData/Humidity", humidity);
      Firebase.setFloat(firebaseData, "/SensorData/SoilMoisture", soilMoisturePercent);
      Firebase.setString(firebaseData, "/SensorData/SoilStatus", "\\\"" + soilStatus + "\\\"");
      Firebase.setString(firebaseData, "/SensorData/RecommendedPlant", "\\\"" + recommendedPlant + "\\\"");

      Serial.println("Data berhasil dikirim ke Firebase!");
      Serial.println("Suhu: " + String(temperature) + " C");
      Serial.println("Kelembapan: " + String(humidity) + " %");
      Serial.println("Kelembapan Tanah: " + String(soilMoisturePercent) + " %");
      Serial.println("Status Tanah: " + soilStatus);
      Serial.println("Rekomendasi Tanaman: " + recommendedPlant);
    } else {
      Serial.println("Gagal membaca data dari sensor.");
    }
  }
  delay(5000);
}

