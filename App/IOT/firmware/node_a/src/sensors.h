// Cảm biến Node A: DHT20 (I2C 0x38), PIR, ánh sáng (ADC).
// Chân đã chốt ở lab 10/11 bằng pin_finder (App/IOT/README.md):
//   ánh sáng = cổng Grove vàng A1-A0, PIR = cổng trắng D4-D3, DHT20 = cổng đỏ I2C.
// Đo ở lab 13/11: phòng sáng light ~800-1100, che kín ~25-100, rọi đèn ~3000
// -> TỐI thì số NHỎ, SÁNG thì số LỚN.
#pragma once
#include <Arduino.h>

#define PIN_LIGHT 1    // cổng A1-A0, ADC1 (bật WiFi/ESP-NOW vẫn đọc được; ADC2 thì không)
#define PIN_PIR   6    // cổng D4-D3
#define PIN_SDA   11
#define PIN_SCL   12

#define EMPTY_TIMEOUT_S 60  // W1: hằng số demo (CONTRACT mục 3). W2 nhận PKT_CONFIG.

// 1 mẫu đo: task_sensor tạo mỗi 2 s rồi đưa qua sample_queue cho task_espnow_tx.
typedef struct {
  float    temperature;  // °C, chỉ có nghĩa khi valid = true
  float    humidity;     // %
  uint16_t light;        // ADC thô 0..4095
  uint8_t  presence;     // 0/1, đã tính EMPTY_TIMEOUT_S
  bool     valid;        // false: DHT20 lỗi hoặc ngoài khoảng -> KHÔNG gửi mẫu này
  uint16_t dht_fail;     // tổng số lần DHT20 lỗi từ lúc khởi động
} SensorSample;

// sensors_init: khởi tạo I2C + DHT20, chân PIR, trạng thái presence. Gọi 1 lần trong setup().
void sensors_init();

// sensors_read: đọc 1 mẫu vào *out.
//   LUÔN điền light, presence, dht_fail (kể cả khi DHT20 lỗi);
//   valid cho biết T/H dùng được không.
//   Không gọi nhanh hơn 1 lần/giây (thư viện DHT20 sẽ trả DHT20_ERROR_LASTREAD).
void sensors_read(SensorSample *out);
