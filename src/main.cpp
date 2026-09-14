#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <Ticker.h>     

// --- Konstanta Broker Bawaan ---
const char* DEFAULT_BROKER = "172.16.100.36"; // Ganti dengan broker sistem Anda
const int   DEFAULT_PORT   = 8883;            // Port broker bawaan
const char* DEFAULT_USER   = "seseorang";              
const char* DEFAULT_PASS   = "password";           

// --- Inisialisasi ---
Preferences preferences;
WiFiClient normalClient;
WiFiClientSecure secureClient;
PubSubClient mqtt;
Ticker ledTicker;

// --- Variabel Global ---
String deviceCode = "";
char apName_str[32] = "Setting_Saklar"; 
char apPass_str[32] = "";               

// Variabel MQTT Kustom & Keamanan
char brokerType[2] = "1";        // "1" = Bawaan, "2" = Kustom
char customBroker[40] = "";      
char customPort[6] = "1883";     // Port MQTT kustom (Default 1883)
char customUser[32] = "";        
char customPass[32] = "";        
char apiToken[32] = "ffgg";      // Token API default

// Pin default disesuaikan untuk ESP8266
char pinS1_str[3] = "5", pinS2_str[3] = "4", pinS3_str[3] = "14", pinS4_str[3] = "12";
char pinLed_str[3] = "2"; 
char newDevCode[20] = ""; 
int pinS1, pinS2, pinS3, pinS4, pinLed;
bool stateS1 = false, stateS2 = false, stateS3 = false, stateS4 = false;

// Konstanta Active-Low LED bawaan ESP8266
const uint8_t LED_ON  = LOW;
const uint8_t LED_OFF = HIGH;

// Variabel untuk timer dan state
unsigned long ledLastToggle = 0;
volatile bool ledBlinkState = false;
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
  pinMode(0, INPUT_PULLUP); // Tombol FLASH untuk Reset WiFi
  
  // 1. Load Data dari NVS (Preferences)
  preferences.begin("config", false);
  deviceCode = preferences.getString("devCode", "");
  
  if (deviceCode == "") {
    deviceCode = "ESP-" + String(random(10000, 99999));
    preferences.putString("devCode", deviceCode);
  }
  
  Serial.println("\n=====================================");
  Serial.print("DEVICE CODE ANDA: "); Serial.println(deviceCode);
  Serial.println("=====================================");

  String savedP1 = preferences.getString("p1", "5"); savedP1.toCharArray(pinS1_str, 3);
  String savedP2 = preferences.getString("p2", "4"); savedP2.toCharArray(pinS2_str, 3);
  String savedP3 = preferences.getString("p3", "14"); savedP3.toCharArray(pinS3_str, 3);
  String savedP4 = preferences.getString("p4", "12"); savedP4.toCharArray(pinS4_str, 3);
  String savedPLed = preferences.getString("pled", "2"); savedPLed.toCharArray(pinLed_str, 3);
  
  String savedApName = preferences.getString("apName", "Setting_Saklar"); savedApName.toCharArray(apName_str, 32);
  String savedApPass = preferences.getString("apPass", ""); savedApPass.toCharArray(apPass_str, 32);

  String savedBType = preferences.getString("btype", "1"); savedBType.toCharArray(brokerType, 2);
  String savedCBroker = preferences.getString("cbroker", ""); savedCBroker.toCharArray(customBroker, 40);
  String savedCPort = preferences.getString("cport", "1883"); savedCPort.toCharArray(customPort, 6);
  String savedCUser = preferences.getString("cuser", ""); savedCUser.toCharArray(customUser, 32);
  String savedCPass = preferences.getString("cpass", ""); savedCPass.toCharArray(customPass, 32);
  String savedToken = preferences.getString("token", "ffgg"); savedToken.toCharArray(apiToken, 32);

  // Load Status Terakhir Saklar
  stateS1 = preferences.getBool("st1", false);
  stateS2 = preferences.getBool("st2", false);
  stateS3 = preferences.getBool("st3", false);
  stateS4 = preferences.getBool("st4", false);

  applyPins();

  // 2. Konfigurasi WiFi Manager & Injeksi CSS Modern
  WiFiManager wm;
  
  String css = "<style>"
               "body { background-color: #f4f7f6; font-family: 'Segoe UI', Roboto, Helvetica, sans-serif; color: #333; }"
               ".c { background: white; padding: 25px; border-radius: 15px; box-shadow: 0 8px 15px rgba(0,0,0,0.05); }"
               "button { background-color: #2563eb; color: white; border: none; border-radius: 8px; padding: 12px 20px; font-weight: bold; font-size: 16px; cursor: pointer; transition: all 0.3s ease; box-shadow: 0 4px 6px rgba(37,99,235,0.2); }"
               "button:hover { background-color: #1d4ed8; transform: translateY(-2px); }"
               "input[type='text'], input[type='password'], input[type='number'] { border-radius: 8px; border: 1px solid #cbd5e1; padding: 10px; margin-bottom: 15px; font-size: 15px; width: 100%; box-sizing: border-box; transition: border 0.3s; }"
               "input:focus { outline: none; border-color: #2563eb; box-shadow: 0 0 5px rgba(37,99,235,0.3); }"
               "h1 { color: #1e293b; font-size: 24px; margin-bottom: 20px; }"
               ".msg { padding: 10px; border-radius: 8px; background-color: #e0f2fe; color: #0369a1; }"
               "</style>"
               "<script>"
               "document.addEventListener('DOMContentLoaded', function() {"
               "  var btn = document.querySelector('form[action=\"/wifi\"] button');"
               "  if(btn) btn.innerHTML = 'Configure Saklar';"
               "});"
               "</script>";

  wm.setCustomHeadElement(css.c_str());
  wm.setConnectTimeout(30);
  wm.setSaveConfigCallback(saveConfigCallback);
  wm.setAPCallback(configModeCallback); 
  wm.setTitle("Configure Saklar");

  // Parameter Kustom WiFi Manager
  String infoTeks = "<div class='msg'><b>Device Code:</b> " + deviceCode + "</div><br/>";
  WiFiManagerParameter custom_text(infoTeks.c_str());
  
  WiFiManagerParameter custom_ap_name("apname", "Nama WiFi Alat (SSID)", apName_str, 32);
  WiFiManagerParameter custom_ap_pass("appass", "Password Alat (Min 8 huruf / Kosongkan)", apPass_str, 32, "type=\"password\"");
  WiFiManagerParameter custom_devcode("devcode", "Perbarui Device Code (Kosongkan jika tetap)", newDevCode, 20);
  
  WiFiManagerParameter custom_btype("btype", "Tipe Broker (1=Bawaan, 2=Kustom)", brokerType, 2, "type=\"number\" min=\"1\" max=\"2\"");
  WiFiManagerParameter custom_cbroker("cbroker", "Host Broker Kustom", customBroker, 40);
  WiFiManagerParameter custom_cport("cport", "Port Broker Kustom (Default: 1883)", customPort, 6, "type=\"number\"");
  WiFiManagerParameter custom_cuser("cuser", "User Broker Kustom (Opsional)", customUser, 32);
  WiFiManagerParameter custom_cpass("cpass", "Pass Broker Kustom (Opsional)", customPass, 32, "type=\"password\"");
  WiFiManagerParameter custom_token("token", "Token API (Keamanan Perintah)", apiToken, 32);
  
  const char* numProps = "type=\"number\" min=\"0\" max=\"16\"";
  WiFiManagerParameter custom_p1("p1", "Pin Saklar 1 (Contoh: D1 = 5)", pinS1_str, 3, numProps);
  WiFiManagerParameter custom_p2("p2", "Pin Saklar 2 (Contoh: D2 = 4)", pinS2_str, 3, numProps);
  WiFiManagerParameter custom_p3("p3", "Pin Saklar 3 (Contoh: D5 = 14)", pinS3_str, 3, numProps);
  WiFiManagerParameter custom_p4("p4", "Pin Saklar 4 (Contoh: D6 = 12)", pinS4_str, 3, numProps);
  WiFiManagerParameter custom_pled("pled", "Pin LED Indikator (Contoh: D4 = 2)", pinLed_str, 3, numProps);

  wm.addParameter(&custom_text);
  wm.addParameter(&custom_ap_name);
  wm.addParameter(&custom_ap_pass);
  wm.addParameter(&custom_devcode);
  wm.addParameter(&custom_btype);
  wm.addParameter(&custom_cbroker);
  wm.addParameter(&custom_cport);
  wm.addParameter(&custom_cuser);
  wm.addParameter(&custom_cpass);
  wm.addParameter(&custom_token);
  wm.addParameter(&custom_p1);
  wm.addParameter(&custom_p2);
  wm.addParameter(&custom_p3);
  wm.addParameter(&custom_p4);
  wm.addParameter(&custom_pled);

  bool res;
  if (String(apPass_str) == "") {
    res = wm.autoConnect(apName_str);
  } else {
    res = wm.autoConnect(apName_str, apPass_str);
  }

  if (!res) {
    Serial.println("Gagal terhubung dan timeout tercapai. Restarting...");
    delay(3000);
    ESP.restart();
  }

  ledTicker.detach();
  digitalWrite(pinLed, LED_OFF);

  // 3. Simpan Konfigurasi Baru
  strcpy(apName_str, custom_ap_name.getValue());
  strcpy(apPass_str, custom_ap_pass.getValue());
  strcpy(brokerType, custom_btype.getValue());
  strcpy(customBroker, custom_cbroker.getValue());
  strcpy(customPort, custom_cport.getValue());
  strcpy(customUser, custom_cuser.getValue());
  strcpy(customPass, custom_cpass.getValue());
  strcpy(apiToken, custom_token.getValue());
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

  preferences.putString("apName", apName_str);
  preferences.putString("apPass", apPass_str);
  preferences.putString("btype", brokerType);
  preferences.putString("cbroker", customBroker);
  preferences.putString("cport", customPort);
  preferences.putString("cuser", customUser);
  preferences.putString("cpass", customPass);
  preferences.putString("token", apiToken);
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

  if (String(brokerType) == "1") {
    Serial.println("Mode Broker: DEFAULT TLS");
    secureClient.setInsecure();
    mqtt.setClient(secureClient);
    mqtt.setServer(DEFAULT_BROKER, DEFAULT_PORT);
  } else {
    int port = String(customPort).toInt();
    Serial.println("Mode Broker: CUSTOM");
    if (port == 8883) {
      secureClient.setInsecure();
      mqtt.setClient(secureClient);
    } else {
      mqtt.setClient(normalClient);
    }
    mqtt.setServer(customBroker, port);
  }

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

  // Kembalikan posisi fisik relay sesuai status terakhirk
  pinMode(pinS1, OUTPUT); digitalWrite(pinS1, stateS1 ? LOW : HIGH);
  pinMode(pinS2, OUTPUT); digitalWrite(pinS2, stateS2 ? LOW : HIGH);
  pinMode(pinS3, OUTPUT); digitalWrite(pinS3, stateS3 ? LOW : HIGH);
  pinMode(pinS4, OUTPUT); digitalWrite(pinS4, stateS4 ? LOW : HIGH);
  pinMode(pinLed, OUTPUT); digitalWrite(pinLed, LED_OFF);
}

void saveConfigCallback() {
  Serial.println("Konfigurasi baru disimpan dari WiFi Manager.");
}

void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("Masuk mode Access Point - LED berkedip tiap 0.5 detik");
  ledTicker.attach(0.5, tickLedAP);
}

void tickLedAP() {
  ledBlinkState = !ledBlinkState;
  digitalWrite(pinLed, ledBlinkState ? LED_ON : LED_OFF);
}

void updateLedStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    if (mqtt.connected()) {
      digitalWrite(pinLed, LED_ON);
    } else {
      if (millis() - ledLastToggle >= 1000) {
        ledLastToggle = millis();
        ledBlinkState = !ledBlinkState;
        digitalWrite(pinLed, ledBlinkState ? LED_ON : LED_OFF);
      }
    }
  } else {
    digitalWrite(pinLed, LED_OFF);
  }
}

void cekTombolResetWiFi() {
  if (digitalRead(0) == LOW) {
    if (!sedangDitekan) {
      waktuTekanMulai = millis();
      sedangDitekan = true;
    } else if (millis() - waktuTekanMulai > 3000) { 
      Serial.println("\nTombol FLASH ditahan 3 detik! Menghapus memori WiFi...");
      WiFiManager wm;
      wm.resetSettings();
      Serial.println("Memori terhapus. Merestart perangkat...");
      delay(2000);
      ESP.restart(); 
    }
  } else {
    sedangDitekan = false; 
  }
}

void reconnectMQTT() {
  while (!mqtt.connected()) {
    if (WiFi.status() != WL_CONNECTED) return;

    cekTombolResetWiFi(); 
    Serial.print("Menghubungkan ke MQTT...");
    
    bool connected = false;
    if (String(brokerType) == "1") {
      connected = mqtt.connect(deviceCode.c_str(), DEFAULT_USER, DEFAULT_PASS);
    } else {
      if (String(customUser) != "") {
        connected = mqtt.connect(deviceCode.c_str(), customUser, customPass);
      } else {
        connected = mqtt.connect(deviceCode.c_str());
      }
    }

    if (connected) {
      Serial.println("Terhubung!");
      mqtt.subscribe(topicStatusCek.c_str());
      mqtt.subscribe(topicSet.c_str());
    } else {
      Serial.print("Gagal, rc=");
      Serial.print(mqtt.state());
      Serial.println(" Coba lagi dalam 5 detik.");
      
      unsigned long mulaiTunggu = millis();
      while (millis() - mulaiTunggu < 5000) {
        updateLedStatus();
        cekTombolResetWiFi();
        if (WiFi.status() != WL_CONNECTED) return;
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

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, message);

  if (error) {
    Serial.print("Bukan JSON: ");
    Serial.println(error.c_str());
    if (topicStr == topicStatusCek && message == "GET_STATUS") publishStatus(0); 
    return;
  }

  String cmd = doc["command"] | "";
  String token = doc["token"] | "";

  if (token != String(apiToken)) {
    Serial.println("WARNING: Token API tidak valid! Akses ditolak.");
    return;
  }

  if (topicStr == topicStatusCek && cmd == "GET_STATUS") {
    publishStatus(0); 
  } 
  else if (topicStr == topicSet) {
    if (cmd == "s1_ON")       { digitalWrite(pinS1, LOW);  stateS1 = true;  preferences.putBool("st1", true);  publishStatus(1); }
    else if (cmd == "s1_OFF") { digitalWrite(pinS1, HIGH); stateS1 = false; preferences.putBool("st1", false); publishStatus(1); }
    
    else if (cmd == "s2_ON")  { digitalWrite(pinS2, LOW);  stateS2 = true;  preferences.putBool("st2", true);  publishStatus(2); }
    else if (cmd == "s2_OFF") { digitalWrite(pinS2, HIGH); stateS2 = false; preferences.putBool("st2", false); publishStatus(2); }
    
    else if (cmd == "s3_ON")  { digitalWrite(pinS3, LOW);  stateS3 = true;  preferences.putBool("st3", true);  publishStatus(3); }
    else if (cmd == "s3_OFF") { digitalWrite(pinS3, HIGH); stateS3 = false; preferences.putBool("st3", false); publishStatus(3); }
    
    else if (cmd == "s4_ON")  { digitalWrite(pinS4, LOW);  stateS4 = true;  preferences.putBool("st4", true);  publishStatus(4); }
    else if (cmd == "s4_OFF") { digitalWrite(pinS4, HIGH); stateS4 = false; preferences.putBool("st4", false); publishStatus(4); }
  }
}