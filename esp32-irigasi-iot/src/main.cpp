#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>

const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long lastReconnectAttempt = 0;

const int PIN_VALVE_1 = 12;
const int PIN_VALVE_2 = 11;
const int PIN_VALVE_3 = 10;
const int PIN_POMPA = 13;

const int PIN_SOIL_1 = 1;
const int PIN_SOIL_2 = 2;
const int PIN_SOIL_3 = 5;
const int PIN_SOIL_4 = 6;

const int AMBANG_KERING = 2500;

// Konstanta Waktu (dalam milidetik)
const unsigned long DURASI_POMPA_AWAL = 30000; 
const unsigned long DURASI_VALVE_AKTIF = 60000; 
const unsigned long JEDA_PASCA_SIRAM = 10000;   

unsigned long waktuSekarang = 0;
unsigned long waktuMulaiMekanisme = 0;
unsigned long waktuMulaiSolenoid = 0;
unsigned long waktuSelesaiSiram = 0;

const int RELAY_ON = LOW;
const int RELAY_OFF = HIGH;

enum TahapIrigasi {
  IDLE,
  POMPA_AWAL,
  VALVE_1_AKTIF,
  VALVE_2_AKTIF,
  VALVE_3_AKTIF,
  COOLDOWN
};

TahapIrigasi statusSaatIni = IDLE;
bool isAutoMode = true; 

void matikanSemua() {
  digitalWrite(PIN_VALVE_1, RELAY_OFF);
  digitalWrite(PIN_VALVE_2, RELAY_OFF);
  digitalWrite(PIN_VALVE_3, RELAY_OFF);
  digitalWrite(PIN_POMPA, RELAY_OFF);
}

// DETEKSI SENSOR TERPUTUS
bool cekSensorTerhubung(int pin) {
  // pull up pin secara internal
  pinMode(pin, INPUT_PULLUP);
  delay(2); // Beri waktu tegangan stabil
  int valUp = analogRead(pin);

  // pull down pin secara internal
  pinMode(pin, INPUT_PULLDOWN);
  delay(2);
  int valDown = analogRead(pin);

  // Kembalikan ke mode normal agar tidak mengganggu pembacaan asli
  pinMode(pin, INPUT);
  delay(2);

  // Jika selisihnya sangat besar (> 2000), berarti pin "menyerah" pada tarikan internal, 
  // yang artinya tidak ada sensor fisik yang menahan tegangannya (Terputus)
  if (abs(valUp - valDown) > 2000) {
    return false;
  }
  
  // Jika selisihnya kecil, berarti tegangan ditahan oleh sensor fisik (Terhubung)
  return true;
}

int bacaSensorSampling(int pin) {
  long total = 0;
  const int jumlahSampling = 10;

  for (int i = 0; i < jumlahSampling; i++) {
    total += analogRead(pin);
    delayMicroseconds(100);
  }
  return total / jumlahSampling;
}

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.println("Menghubungkan WiFi via WiFiManager...");

  // Inisialisasi WiFiManager
  WiFiManager wm;

  // Membuka AP "ESP32_Irigasi_Setup" jika gagal terhubung ke jaringan tersimpan
  bool res = wm.autoConnect("ESP32_Irigasi_Setup");

  if (!res) {
    Serial.println("Gagal terhubung ke WiFi!");
    // Restart ESP jika gagal koneksi setelah timeout
    ESP.restart();
  }

  // Jika berhasil terhubung
  Serial.println("\nWiFi terhubung");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  
  String strTopic = String(topic);
  Serial.printf("=> Pesan masuk [%s]: %s\n", topic, msg.c_str());

  if (strTopic == "tgtv_ridho/kontrol/mode") {
    if (msg == "manual") {
      isAutoMode = false;
      Serial.println("Sistem beralih ke MANUAL");
      matikanSemua();
      statusSaatIni = IDLE; 
    } 
    else if (msg == "auto") {
      isAutoMode = true;
      Serial.println("Sistem beralih ke AUTO");
    }
  } 
  else if (!isAutoMode) {
    if (strTopic == "tgtv_ridho/kontrol/pompa") {
      digitalWrite(PIN_POMPA, (msg == "ON") ? RELAY_ON : RELAY_OFF);
    } 
    else if (strTopic == "tgtv_ridho/kontrol/valve1") {
      digitalWrite(PIN_VALVE_1, (msg == "ON") ? RELAY_ON : RELAY_OFF);
    } 
    else if (strTopic == "tgtv_ridho/kontrol/valve2") {
      digitalWrite(PIN_VALVE_2, (msg == "ON") ? RELAY_ON : RELAY_OFF);
    } 
    else if (strTopic == "tgtv_ridho/kontrol/valve3") {
      digitalWrite(PIN_VALVE_3, (msg == "ON") ? RELAY_ON : RELAY_OFF);
    }
  }
}

boolean reconnect() {
  Serial.print("Mencoba koneksi MQTT...");
  String clientId = "ESP32S3Client-";
  clientId += String(random(0xffff), HEX);
  
  if (client.connect(clientId.c_str())) {
    Serial.println("Terhubung ke HiveMQ");
    client.subscribe("tgtv_ridho/kontrol/#");
    return true;
  } else {
    Serial.print("Gagal, rc=");
    Serial.print(client.state());
    Serial.println(" coba lagi dalam 5 detik");
    return false;
  }
}

void setup() {
  Serial.begin(115200);

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback); 

  pinMode(PIN_VALVE_1, OUTPUT);
  pinMode(PIN_VALVE_2, OUTPUT);
  pinMode(PIN_VALVE_3, OUTPUT);
  pinMode(PIN_POMPA, OUTPUT);

  matikanSemua();
}

void loop() {
  waktuSekarang = millis();

  if (!client.connected()) {
    if (waktuSekarang - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = waktuSekarang;
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    client.loop();
  }

  // Cek status konektivitas kabel setiap sensor
  bool s1_ok = cekSensorTerhubung(PIN_SOIL_1);
  bool s2_ok = cekSensorTerhubung(PIN_SOIL_2);
  bool s3_ok = cekSensorTerhubung(PIN_SOIL_3);
  bool s4_ok = cekSensorTerhubung(PIN_SOIL_4);

  // Hanya baca sensor jika dipastikan kabelnya terhubung
  int nilaiSoil1 = s1_ok ? bacaSensorSampling(PIN_SOIL_1) : 0;
  int nilaiSoil2 = s2_ok ? bacaSensorSampling(PIN_SOIL_2) : 0;
  int nilaiSoil3 = s3_ok ? bacaSensorSampling(PIN_SOIL_3) : 0;
  int nilaiSoil4 = s4_ok ? bacaSensorSampling(PIN_SOIL_4) : 0;

  // Syarat irigasi, Sensor harus terhubung AND nilainya >= KERING
  bool adaKering = (s1_ok && nilaiSoil1 >= AMBANG_KERING) ||
                   (s2_ok && nilaiSoil2 >= AMBANG_KERING) ||
                   (s3_ok && nilaiSoil3 >= AMBANG_KERING) || 
                   (s4_ok && nilaiSoil4 >= AMBANG_KERING);

  static unsigned long waktuLog = 0;
  if (waktuSekarang - waktuLog >= 1000) {
    
    // Log Serial dengan indikator status terhubung (OK) atau terputus (DIS)
    Serial.printf("S1:%d(%s) S2:%d(%s) S3:%d(%s) S4:%d(%s) | Mode: %s\n", 
                  nilaiSoil1, s1_ok ? "OK" : "DIS",
                  nilaiSoil2, s2_ok ? "OK" : "DIS",
                  nilaiSoil3, s3_ok ? "OK" : "DIS",
                  nilaiSoil4, s4_ok ? "OK" : "DIS",
                  isAutoMode ? "AUTO" : "MANUAL");
    
    if (client.connected()) {
      // Hanya publish topik jika kabel sensor benar-benar terhubung
      if (s1_ok) client.publish("tgtv_ridho/sensor/soil1", String(nilaiSoil1).c_str());
      if (s2_ok) client.publish("tgtv_ridho/sensor/soil2", String(nilaiSoil2).c_str());
      if (s3_ok) client.publish("tgtv_ridho/sensor/soil3", String(nilaiSoil3).c_str());
      if (s4_ok) client.publish("tgtv_ridho/sensor/soil4", String(nilaiSoil4).c_str());
      
      client.publish("tgtv_ridho/status/pompa", (digitalRead(PIN_POMPA) == RELAY_ON) ? "ON" : "OFF");
      client.publish("tgtv_ridho/status/valve1", (digitalRead(PIN_VALVE_1) == RELAY_ON) ? "ON" : "OFF");
      client.publish("tgtv_ridho/status/valve2", (digitalRead(PIN_VALVE_2) == RELAY_ON) ? "ON" : "OFF");
      client.publish("tgtv_ridho/status/valve3", (digitalRead(PIN_VALVE_3) == RELAY_ON) ? "ON" : "OFF");
      client.publish("tgtv_ridho/status/mode", isAutoMode ? "auto" : "manual");
    }

    waktuLog = waktuSekarang;
  }

  if (isAutoMode) {
    switch (statusSaatIni) {
      
      case IDLE:
        if (adaKering) {
          digitalWrite(PIN_POMPA, RELAY_ON);
          waktuMulaiMekanisme = waktuSekarang;
          statusSaatIni = POMPA_AWAL;
          Serial.println("[AUTO] Mulai Irigasi: Pompa utama menyala (Menunggu 30 detik)...");
        }
        break;

      case POMPA_AWAL:
        if (waktuSekarang - waktuMulaiMekanisme >= DURASI_POMPA_AWAL) {
          digitalWrite(PIN_VALVE_1, RELAY_ON); 
          waktuMulaiSolenoid = waktuSekarang;
          statusSaatIni = VALVE_1_AKTIF;
          Serial.println("[AUTO] Valve 1 Terbuka (Selama 1 Menit)");
        }
        break;

      case VALVE_1_AKTIF:
        if (waktuSekarang - waktuMulaiSolenoid >= DURASI_VALVE_AKTIF) {
          digitalWrite(PIN_VALVE_1, RELAY_OFF); 
          digitalWrite(PIN_VALVE_2, RELAY_ON);  
          waktuMulaiSolenoid = waktuSekarang;
          statusSaatIni = VALVE_2_AKTIF;
          Serial.println("[AUTO] Valve 1 Tertutup -> Valve 2 Terbuka (Selama 1 Menit)");
        }
        break;

      case VALVE_2_AKTIF:
        if (waktuSekarang - waktuMulaiSolenoid >= DURASI_VALVE_AKTIF) {
          digitalWrite(PIN_VALVE_2, RELAY_OFF); 
          digitalWrite(PIN_VALVE_3, RELAY_ON);  
          waktuMulaiSolenoid = waktuSekarang;
          statusSaatIni = VALVE_3_AKTIF;
          Serial.println("[AUTO] Valve 2 Tertutup -> Valve 3 Terbuka (Selama 1 Menit)");
        }
        break;

      case VALVE_3_AKTIF:
        if (waktuSekarang - waktuMulaiSolenoid >= DURASI_VALVE_AKTIF) {
          digitalWrite(PIN_VALVE_3, RELAY_OFF); 
          digitalWrite(PIN_POMPA, RELAY_OFF);   
          waktuSelesaiSiram = waktuSekarang;
          statusSaatIni = COOLDOWN;
          Serial.println("[AUTO] Irigasi Selesai -> Masuk fase Cooldown");
        }
        break;

      case COOLDOWN:
        if (waktuSekarang - waktuSelesaiSiram >= JEDA_PASCA_SIRAM) {
          statusSaatIni = IDLE;
          Serial.println("[AUTO] Cooldown selesai, kembali ke IDLE memantau sensor");
        }
        break;
    }
  }
}