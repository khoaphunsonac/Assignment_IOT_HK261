// Cảm biến Node A: DHT20 (I2C 0x38), PIR, ánh sáng (ADC).
// Chân đã chốt ở lab 10/11 (App/IOT/README.md).
#pragma once
#include <Arduino.h>

#define PIN_LIGHT 1    // cổng A1-A0, ADC1
#define PIN_PIR   6    // cổng D4-D3
#define PIN_SDA   11
#define PIN_SCL   12

#define EMPTY_TIMEOUT_S 60  // W1: hằng số demo (CONTRACT mục 3). W2 nhận PKT_CONFIG.

typedef struct {
  float    temperature;  // °C, chỉ có nghĩa khi valid = true
  float    humidity;     // %
  uint16_t light;        // ADC thô 0..4095
  uint8_t  presence;     // 0/1, đã tính EMPTY_TIMEOUT_S
  bool     valid;        // false: DHT20 lỗi hoặc ngoài khoảng -> KHÔNG gửi mẫu này
  uint16_t dht_fail;     // tổng số lần DHT20 lỗi từ lúc khởi động
} SensorSample;

void sensors_init();
// Đọc 1 mẫu. Luôn điền light, presence, dht_fail; valid cho biết T/H dùng được không.
void sensors_read(SensorSample *out);
