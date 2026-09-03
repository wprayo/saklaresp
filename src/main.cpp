#include <Arduino.h>
#include <ESP8266WiFi.h>  // Menggunakan library WiFi khusus ESP8266
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Ticker.h>       // Untuk kedip LED saat mode AP (non-blocking, berbasis interrupt timer)

// --- Inisialisasi ---
Preferences preferences;
WiFiClient espClient;
PubSubClient mqtt(espClient);
Ticker ledTicker;

// --- Variabel Global ---
String deviceCode = "";
char mqttBroker[40] = "broker.hivemq.com";

// Pin default disesuaikan untuk ESP8266: GPIO 5(D1), 4(D2), 14(D5), 12(D6)
char pinS1_str[3] = "5", pinS2_str[3] = "4", pinS3_str[3] = "14", pinS4_str[3] = "12";
char pinLed_str[3] = "2"; // Default pin LED indikator: D4 (GPIO2)
char newDevCode[20] = ""; 
int pinS1, pinS2, pinS3, pinS4, pinLed;
bool stateS1 = false, stateS2 = false, stateS3 = false, stateS4 = false;

// Ubah dua baris ini jika LED anda aktif-LOW (mis. LED bawaan board NodeMCU/D1 Mini)
const uint8_t LED_ON  = HIGH;
const uint8_t LED_OFF = LOW;

// Variabel untuk blink LED saat WiFi tersambung tapi MQTT belum (non-blocking, pakai millis)
unsigned long ledLastToggle = 0;
volatile bool ledBlinkState = false;

// Variabel untuk fitur Reset WiFi
unsigned long waktuTekanMulai = 0;
bool sedangDitekan = false;

// --- Topic MQTT ---
String topicStatusCek, topicSet, topicStatus;

// --- Deklarasi Fungsi ---
void saveConfigCallback();
void configModeCallback(WiFiManager *myWiFiManager);
void tickLedAP();
void updateLedStatus();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void reconnectMQTT();
void publishStatus(int targetSaklar);
void applyPins();
void cekTombolResetWiFi();

void setup() {

  Serial.begin(115200);

  // Set pin 0 (Tombol FLASH) sebagai input
  pinMode(0, INPUT_PULLUP);
  
  
  // 1. Load Data dari NVS (Preferences)
  preferences.begin("config", false);
  deviceCode = preferences.getString("devCode", "");
  
  // Jika Device Code kosong, buat acak
  if (deviceCode == "") {
    deviceCode = "ESP-" + String(random(10000, 99999));
    preferences.putString("devCode", deviceCode);
  }
  Serial.println("=====================================");
  Serial.print("DEVICE CODE ANDA: ");
  Serial.println(deviceCode);
  Serial.println("=====================================");

  String savedBroker = preferences.getString("broker", "broker.hivemq.com");
  savedBroker.toCharArray(mqttBroker, 40);
  
  String savedP1 = preferences.getString("p1", "5"); savedP1.toCharArray(pinS1_str, 3);
  String savedP2 = preferences.getString("p2", "4"); savedP2.toCharArray(pinS2_str, 3);
  String savedP3 = preferences.getString("p3", "14"); savedP3.toCharArray(pinS3_str, 3);
  String savedP4 = preferences.getString("p4", "12"); savedP4.toCharArray(pinS4_str, 3);
  String savedPLed = preferences.getString("pled", "2"); savedPLed.toCharArray(pinLed_str, 3);
  
  applyPins();

  // 2. Konfigurasi WiFi Manager
  WiFiManager wm;
  
  wm.setConnectTimeout(30);
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setAPCallback(configModeCallback); // Dipanggil saat portal AP terbuka -> LED kedip cepat

  // Menambahkan parameter kustom di Web WiFi Manager (Error StringSumHelper diperbaiki)
  String infoTeks = "<p><b>Device Code Saat Ini:</b> " + deviceCode + "</p>";
  WiFiManagerParameter custom_text(infoTeks.c_str());
  WiFiManagerParameter custom_devcode("devcode", "Perbarui Device Code (Kosongkan jika tetap)", newDevCode, 20);
  WiFiManagerParameter custom_broker("broker", "MQTT Broker", mqttBroker, 40);
  
  // Batas maksimum pin diubah ke 16 karena pin GPIO ESP8266 terbatas
  const char* numProps = "type=\"number\" min=\"0\" max=\"16\"";
  WiFiManagerParameter custom_p1("p1", "Pin Saklar 1 (Contoh: D1 = 5)", pinS1_str, 3, numProps);
  WiFiManagerParameter custom_p2("p2", "Pin Saklar 2 (Contoh: D2 = 4)", pinS2_str, 3, numProps);
  WiFiManagerParameter custom_p3("p3", "Pin Saklar 3 (Contoh: D5 = 14)", pinS3_str, 3, numProps);
  WiFiManagerParameter custom_p4("p4", "Pin Saklar 4 (Contoh: D6 = 12)", pinS4_str, 3, numProps);
  WiFiManagerParameter custom_pled("pled", "Pin LED Indikator (Contoh: D4 = 2)", pinLed_str, 3, numProps);

  wm.addParameter(&custom_text);
  wm.addParameter(&custom_devcode);
  wm.addParameter(&custom_broker);
  wm.addParameter(&custom_p1);
  wm.addParameter(&custom_p2);
  wm.addParameter(&custom_p3);
  wm.addParameter(&custom_p4);
  wm.addParameter(&custom_pled);

  // Mulai WiFi Manager
  if (!wm.autoConnect(deviceCode.c_str())) {
    Serial.println("Gagal terhubung dan timeout tercapai. Restarting...");
    delay(3000);
    ESP.restart();
  }

  // Portal AP (jika sempat terbuka) sudah selesai -> hentikan kedip cepat
  ledTicker.detach();
  digitalWrite(pinLed, LED_OFF);

  // 3. Simpan Konfigurasi Baru (jika ada perubahan dari Web)
  strcpy(mqttBroker, custom_broker.getValue());
  strcpy(pinS1_str, custom_p1.getValue());
  strcpy(pinS2_str, custom_p2.getValue());
  strcpy(pinS3_str, custom_p3.getValue());
  strcpy(pinS4_str, custom_p4.getValue());
  strcpy(pinLed_str, custom_pled.getValue());
  
  String inputDevCode = String(custom_devcode.getValue());
  if (inputDevCode != "") {
    deviceCode = inputDevCode;
    preferences.putString("devCode", deviceCode);
  }

  preferences.putString("broker", mqttBroker);
  preferences.putString("p1", pinS1_str);
  preferences.putString("p2", pinS2_str);
  preferences.putString("p3", pinS3_str);
  preferences.putString("p4", pinS4_str);
  preferences.putString("pled", pinLed_str);
  applyPins();

  // 4. Setup MQTT
  topicStatusCek = "saklar/keminter/" + deviceCode + "/status/cek";
  topicSet = "saklar/keminter/" + deviceCode + "/set";
  topicStatus = "saklar/keminter/" + deviceCode + "/status";

  mqtt.setServer(mqttBroker, 1883);
  mqtt.setCallback(mqttCallback);
}

void loop() {
  cekTombolResetWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    if (!mqtt.connected()) {
      reconnectMQTT();
    }
    mqtt.loop();
  }

  updateLedStatus();
}

// --- Fungsi Pendukung ---

void applyPins() {
  pinS1 = String(pinS1_str).toInt();
  pinS2 = String(pinS2_str).toInt();
  pinS3 = String(pinS3_str).toInt();
  pinS4 = String(pinS4_str).toInt();
  pinLed = String(pinLed_str).toInt();

  pinMode(pinS1, OUTPUT); digitalWrite(pinS1, HIGH);
  pinMode(pinS2, OUTPUT); digitalWrite(pinS2, HIGH);
  pinMode(pinS3, OUTPUT); digitalWrite(pinS3, HIGH);
  pinMode(pinS4, OUTPUT); digitalWrite(pinS4, HIGH);

  pinMode(pinLed, OUTPUT); digitalWrite(pinLed, LED_OFF);
}

void saveConfigCallback() {
  Serial.println("Konfigurasi baru disimpan dari WiFi Manager.");
}

// Dipanggil otomatis oleh WiFiManager saat portal Access Point dibuka
void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("Masuk mode Access Point - LED berkedip tiap 0.5 detik");
  // Ticker dipakai karena wm.autoConnect() bersifat blocking (loop() tidak jalan selama portal AP aktif)
  ledTicker.attach(0.5, tickLedAP);
}

// Dipanggil oleh Ticker setiap 0.5 detik selama mode AP aktif
void tickLedAP() {
  ledBlinkState = !ledBlinkState;
  digitalWrite(pinLed, ledBlinkState ? LED_ON : LED_OFF);
}

// Dipanggil terus-menerus dari loop() setelah setup selesai (di luar mode AP)
void updateLedStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    if (mqtt.connected()) {
      // Terhubung ke broker MQTT -> LED menyala terus
      digitalWrite(pinLed, LED_ON);
    } else {
      // WiFi tersambung, MQTT belum -> berkedip tiap 1 detik (non-blocking)
      if (millis() - ledLastToggle >= 1000) {
        ledLastToggle = millis();
        ledBlinkState = !ledBlinkState;
        digitalWrite(pinLed, ledBlinkState ? LED_ON : LED_OFF);
      }
    }
  } else {
    // Tidak dalam mode AP dan WiFi belum/tidak tersambung -> LED mati
    digitalWrite(pinLed, LED_OFF);
  }
}

// Logika tombol FLASH (GPIO0) ditahan 3 detik -> hapus memori WiFi & restart ke mode AP.
// Dipisah jadi fungsi sendiri (bukan langsung di loop()) supaya bisa tetap dipanggil
// dari dalam reconnectMQTT() -- yang isinya while() blocking -- sehingga tombol tetap
// responsif walaupun board sedang macet mencoba konek ke server MQTT yang mati.
void cekTombolResetWiFi() {
  if (digitalRead(0) == LOW) {
    if (!sedangDitekan) {
      waktuTekanMulai = millis();
      sedangDitekan = true;
    } else if (millis() - waktuTekanMulai > 3000) { // Jika ditahan lebih dari 3000ms (3 detik)
      Serial.println("\nTombol FLASH ditahan 3 detik! Menghapus memori WiFi...");
      WiFiManager wm;
      wm.resetSettings();
      Serial.println("Memori terhapus. Merestart perangkat...");
      delay(2000);
      ESP.restart(); 
    }
  } else {
    sedangDitekan = false; // Reset status jika tombol dilepas sebelum 3 detik
  }
}

void reconnectMQTT() {
  while (!mqtt.connected()) {
    if (WiFi.status() != WL_CONNECTED) return;
    cekTombolResetWiFi(); // <-- kunci perbaikan: tombol tetap dicek walau MQTT server mati terus
    Serial.print("Menghubungkan ke MQTT...");
    if (mqtt.connect(deviceCode.c_str())) {
      Serial.println("Terhubung!");
      mqtt.subscribe(topicStatusCek.c_str());
      mqtt.subscribe(topicSet.c_str());
    } else {
      Serial.print("Gagal, rc=");
      Serial.print(mqtt.state());
      Serial.println(" Coba lagi dalam 5 detik.");
      // Jeda 5 detik dipecah jadi langkah kecil agar LED tetap berkedip 1 detik
      // dan tombol FLASH tetap responsif selama menunggu
      unsigned long mulaiTunggu = millis();
      while (millis() - mulaiTunggu < 5000) {
        updateLedStatus();
        cekTombolResetWiFi();
        delay(50);
      }
    }
  }
}

void publishStatus(int targetSaklar) {
  JsonDocument doc;
  
  if(targetSaklar == 0 || targetSaklar == 1) doc["s1"] = stateS1;
  if(targetSaklar == 0 || targetSaklar == 2) doc["s2"] = stateS2;
  if(targetSaklar == 0 || targetSaklar == 3) doc["s3"] = stateS3;
  if(targetSaklar == 0 || targetSaklar == 4) doc["s4"] = stateS4;

  String output;
  serializeJson(doc, output);
  
  // Tampilkan proses pengiriman di Serial Monitor
  Serial.println("<<< MENGIRIM BALASAN <<<");
  Serial.print("Topic Tujuan: "); Serial.println(topicStatus);
  Serial.print("Isi JSON: "); Serial.println(output);
  
  if (mqtt.publish(topicStatus.c_str(), output.c_str())) {
    Serial.println("Status: BERHASIL DIKIRIM KE BROKER!");
  } else {
    Serial.println("Status: GAGAL MENGIRIM!");
  }
  Serial.println("<<<<<<<<<<<<<<<<<<<<<<<<");
}
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  message.trim(); 
  String topicStr = String(topic);

  Serial.println("====== PESAN MQTT MASUK ======");
  Serial.print("Topic: "); Serial.println(topicStr);
  Serial.print("Pesan: "); Serial.println(message);
  Serial.println("==============================");

  if (topicStr == topicStatusCek && message == "GET_STATUS") {
    publishStatus(0); 
  } 
  else if (topicStr == topicSet) {
    // Logika dibalik: LOW = Nyala (true), HIGH = Mati (false)
    if (message == "s1_ON") { digitalWrite(pinS1, LOW); stateS1 = true; publishStatus(1); }
    else if (message == "s1_OFF") { digitalWrite(pinS1, HIGH); stateS1 = false; publishStatus(1); }
    
    else if (message == "s2_ON") { digitalWrite(pinS2, LOW); stateS2 = true; publishStatus(2); }
    else if (message == "s2_OFF") { digitalWrite(pinS2, HIGH); stateS2 = false; publishStatus(2); }
    
    else if (message == "s3_ON") { digitalWrite(pinS3, LOW); stateS3 = true; publishStatus(3); }
    else if (message == "s3_OFF") { digitalWrite(pinS3, HIGH); stateS3 = false; publishStatus(3); }
    
    else if (message == "s4_ON") { digitalWrite(pinS4, LOW); stateS4 = true; publishStatus(4); }
    else if (message == "s4_OFF") { digitalWrite(pinS4, HIGH); stateS4 = false; publishStatus(4); }
  }
}