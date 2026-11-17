#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

// === НАСТРОЙКИ ТОЧКИ ДОСТУПА ===
const char* AP_SSID = "Jimmy_Radar";
const char* AP_PASSWORD = "sensing123";
const IPAddress AP_IP(192, 168, 4, 1);
const IPAddress SUBNET_MASK(255, 255, 255, 0);

WiFiUDP udp;
unsigned long packetsSent = 0;
unsigned long lastSendTime = 0;

void sendUdpPacket() {
  uint8_t data[32];
  for (int i = 0; i < 32; i++) data[i] = 0xAA;
  udp.beginPacket(IPAddress(192, 168, 4, 2), 12345);
  udp.write(data, 32);
  if (udp.endPacket() == 1) {
    packetsSent++;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n🚀 Джимми (передатчик)");

  // Принудительно включаем 802.11n для точки доступа (HT)
  WiFi.mode(WIFI_AP);
  esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G | WIFI_PROTOCOL_11N);
  
  // Фиксируем канал (например, 6) через esp_wifi
  esp_wifi_set_channel(6, WIFI_SECOND_CHAN_NONE);

  // Настройка IP и запуск AP
  WiFi.softAPConfig(AP_IP, AP_IP, SUBNET_MASK);
  WiFi.softAP(AP_SSID, AP_PASSWORD);

  Serial.print("MAC точки доступа: ");
  Serial.println(WiFi.softAPmacAddress());
  Serial.print("IP точки доступа: ");
  Serial.println(WiFi.softAPIP());
  
  // Получение текущего канала (для отладки)
  uint8_t primary;
  wifi_second_chan_t second;
  esp_wifi_get_channel(&primary, &second);
  Serial.print("Канал: ");
  Serial.println(primary);
  
  Serial.println("Начинаем отправку UDP-пакетов на 192.168.4.2:12345, 10 пакетов/сек");
}

void loop() {
  unsigned long now = millis();
  if (now - lastSendTime >= 100) {
    sendUdpPacket();
    lastSendTime = now;
    if (packetsSent % 100 == 0) {
      Serial.print("📤 Отправлено пакетов: ");
      Serial.println(packetsSent);
    }
  }
  delay(1);
}