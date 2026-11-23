// Node B (#42): nhận ESP-NOW từ Node A, đẩy lên MQTT local, nhận lệnh, điều khiển quạt/đèn.
// Thay fake_node_b.py: tắt fake, cắm Node B thật, Gateway/CoreIOT/UI không sửa gì (CONTRACT 1).
//
// 3 task FreeRTOS:
//   task_espnow  (ưu tiên 3) : gói ESP-NOW -> seq/nhiệt TB -> control -> xin gửi telemetry
//   task_net     (ưu tiên 2) : WiFi watchdog, MQTT (lệnh, ngưỡng, telemetry). Chỉ task này
//                              được gọi PubSubClient (net.h).
//   task_control (ưu tiên 2) : 200 ms/lần: Node A timeout, phòng trống, hết giờ manual.
// Vì sao task_espnow ưu tiên cao nhất: callback ESP-NOW đẩy vào queue 20 phần tử; task
// xử lý chậm thì queue đầy và mất gói (đếm ở qfull). Việc của nó lại rất ngắn.
//
// task_lcd (#60, ưu tiên 1): LCD 16x2, xem lcd.h.
// Chưa làm: webserver (#59), OTA (#58), chế độ offline (W4).
#include <Arduino.h>

#include "config_store.h"
#include "control_ext.h"
#include "espnow_rx.h"
#include "lcd.h"
#include "net.h"
#include "version.h"

#define CONTROL_PERIOD_MS 200

// task_control: áp luật CONTRACT 13 theo thời gian (không cần lệnh nào tới).
void task_control(void *pvParameters) {
  (void)pvParameters;
  while (1) {
    Thresholds th = config_store_get();
    bool changed = false;
    changed = control_tick(th.manual_timeout_s * 1000UL);
    if (changed) {
      net_request_telemetry();
      uint8_t f;
      bool l;
      LMode m;
      control_get_lstate(&f, &l, &m);
      Serial.printf("[CTRL] fan=%u light=%d mode=%s node_a_online=%d\n", f, l, lmode_str(m), control_get_node_a().online);
    }
    vTaskDelay(CONTROL_PERIOD_MS);
  }
}

// Thứ tự khởi động:
//   1. Serial + chờ USB CDC (2 s, như Node A).
//   2. config_store_init: có ngưỡng (NVS hoặc mặc định) TRƯỚC khi luật chạy.
//   3. control_init: mutex + quạt/đèn tắt hết.
//   4. net_init: WiFi STA (ESP-NOW cần radio ở STA, và PAIR_RESP cần channel của hotspot).
//   5. espnow_rx_init: sau WiFi.
//   6. Tạo 3 task. Stack 4096: bài học LAB3 14/11 (2048 tràn stack, board reset liên tục);
//      task_net lớn hơn vì có bộ đệm JSON 512 byte + ArduinoJson trên stack.
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.printf("===== NODE B fw %s =====\n", FW_VERSION);

  config_store_init();
  control_init();
  net_init();
  if (!espnow_rx_init()) {
    Serial.println("[B] FAIL espnow_rx_init");
    return;
  }

  xTaskCreate(task_espnow, "task_espnow", 4096, NULL, 3, NULL);
  xTaskCreate(task_net, "task_net", 8192, NULL, 2, NULL);
  xTaskCreate(task_control, "task_control", 4096, NULL, 2, NULL);
  lcd_start();  // #60: task_lcd, LCD 16x2 I2C 0x21
}

void loop() {}
