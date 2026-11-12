// Webserver Node B, bản đứng riêng (issue #33): nối WiFi rồi tạo task webserver.
#include <Arduino.h>
#include <WiFi.h>
#include "secrets.h"
#include "web.h"

void setup() {
  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Dang noi WiFi %s", WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }
  Serial.printf("\nIP: %s  -> mo http://%s/ tren dien thoai cung hotspot\n",
                WiFi.localIP().toString().c_str(), WiFi.localIP().toString().c_str());

  web_start();
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
