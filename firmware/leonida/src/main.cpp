#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include <math.h>

// === НАСТРОЙКИ ===
const char* ssid = "Jimmy_Radar";
const char* password = "sensing123";
uint8_t jimmyMAC[] = {0xD4, 0xE9, 0xF4, 0xBD, 0x26, 0xD1};

// Пин кнопки (подтяжка к GND, нажатие = LOW)
const int BUTTON_PIN = 0;

// Временные интервалы (мс)
const unsigned long PREPARE_DURATION = 15000;  // 15 секунд подготовки
const unsigned long RECORD_DURATION = 120000;  // 2 минуты записи

// Состояния системы
enum State {
  STATE_IDLE,       // ждём нажатия кнопки
  STATE_PREPARE,    // подготовка
  STATE_RECORD      // запись данных
};

State currentState = STATE_IDLE;
unsigned long stateStartTime = 0;
unsigned long totalPackets = 0;

// CSI структура
struct CSIPacket {
  int8_t buf[128];
  int len;
  int rssi;
  uint8_t mac[6];
  bool valid;
};

volatile CSIPacket incomingPacket;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// === КОЛБЭК CSI ===
void wifi_csi_cb(void *ctx, wifi_csi_info_t *data) {
  if (!data || !data->buf || data->len == 0) return;
  for (int i = 0; i < 6; i++) if (data->mac[i] != jimmyMAC[i]) return;

  portENTER_CRITICAL_ISR(&mux);
  incomingPacket.len = (data->len < 128) ? data->len : 128;
  memcpy((void*)incomingPacket.buf, data->buf, incomingPacket.len);
  incomingPacket.rssi = data->rx_ctrl.rssi;
  memcpy((void*)incomingPacket.mac, data->mac, 6);
  incomingPacket.valid = true;
  portEXIT_CRITICAL_ISR(&mux);
}

// === ОБРАБОТКА ПАКЕТА (ВЫВОД В SERIAL) ===
void processPacket(CSIPacket &pkt) {
  totalPackets++;
  // Выводим данные только в состоянии RECORD
  if (currentState != STATE_RECORD) return;

  int numSub = pkt.len / 2;
  if (numSub > 52) numSub = 52;
  unsigned long now = millis() - stateStartTime; // время от начала записи

  for (int i = 0; i < numSub; i++) {
    int8_t imag = pkt.buf[i*2];
    int8_t real = pkt.buf[i*2+1];
    float amp = sqrt(imag*imag + real*real);
    float phase = atan2(imag, real);
    int rssi = pkt.rssi;

    // Формат CSV: время_мс,номер_пакета,номер_поднесущей,амплитуда,фаза,rssi
    Serial.print(now);
    Serial.print(',');
    Serial.print(totalPackets);
    Serial.print(',');
    Serial.print(i);
    Serial.print(',');
    Serial.print(amp, 4);
    Serial.print(',');
    Serial.print(phase, 4);
    Serial.print(',');
    Serial.println(rssi);
  }
}

// === УПРАВЛЕНИЕ СОСТОЯНИЯМИ ===
void updateState() {
  if (currentState == STATE_IDLE) {
    // Проверка нажатия кнопки
    if (digitalRead(BUTTON_PIN) == LOW) {
      // маленькая антидребезговая задержка
      delay(50);
      if (digitalRead(BUTTON_PIN) == LOW) {
        currentState = STATE_PREPARE;
        stateStartTime = millis();
        Serial.println("\n🔵 ПОДГОТОВКА 15 СЕКУНД...");
        // ждём, пока кнопка отпущена
        while (digitalRead(BUTTON_PIN) == LOW) delay(10);
      }
    }
  }
  else if (currentState == STATE_PREPARE) {
    if (millis() - stateStartTime >= PREPARE_DURATION) {
      currentState = STATE_RECORD;
      stateStartTime = millis();
      totalPackets = 0;
      Serial.println("\n🟢 НАЧАЛО ЗАПИСИ (2 МИНУТЫ)");
      Serial.println("time_ms,packet_id,subcarrier,amplitude,phase_rad,rssi_dbm");
    }
  }
  else if (currentState == STATE_RECORD) {
    if (millis() - stateStartTime >= RECORD_DURATION) {
      currentState = STATE_IDLE;
      Serial.println("\n🔴 ЗАПИСЬ ЗАВЕРШЕНА");
      Serial.println("МОЖНО СКОПИРОВАТЬ ДАННЫЕ ИЗ ЭТОГО ОКНА В ФАЙЛ .CSV");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n🚀 ПРИЁМНИК");

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  WiFi.begin(ssid, password);
  Serial.print("ПОДКЛЮЧЕНИЕ К "); Serial.println(ssid);
  while (WiFi.status() != WL_CONNECTED) delay(500);
  Serial.println("\n✅ ПОДКЛЮЧЕНО! IP: " + WiFi.localIP().toString());

  wifi_csi_config_t cfg = {
    .lltf_en = 1, .htltf_en = 1, .stbc_htltf2_en = 0,
    .ltf_merge_en = 1, .channel_filter_en = 0,
    .manu_scale = 0, .shift = 0
  };
  esp_wifi_set_csi(&cfg);
  esp_wifi_set_csi_rx_cb(&wifi_csi_cb, NULL);
  esp_wifi_set_csi(1);

  Serial.println("📡 CSI ЗАПУЩЕН");
  Serial.println("\n⏸️ НАЖМИТЕ КНОПКУ (GPIO0) ДЛЯ НАЧАЛА ЭКСПЕРИМЕНТА");
  Serial.println("   -> 15 секунд подготовки, затем 2 минуты записи");
  currentState = STATE_IDLE;
}

void loop() {
  if (incomingPacket.valid) {
    portENTER_CRITICAL(&mux);
    CSIPacket pkt;
    pkt.len = incomingPacket.len;
    pkt.rssi = incomingPacket.rssi;
    pkt.valid = incomingPacket.valid;
    memcpy(pkt.buf, (void*)incomingPacket.buf, pkt.len);
    memcpy(pkt.mac, (void*)incomingPacket.mac, 6);
    incomingPacket.valid = false;
    portEXIT_CRITICAL(&mux);
    processPacket(pkt);
  }
  updateState();
  delay(10);
}