// config_store: bộ ngưỡng room1/config (CONTRACT mục 7), lưu NVS để mất mạng vẫn có.
//
// NVS (Non-Volatile Storage): vùng flash nhỏ của ESP32 lưu cặp key/value, còn nguyên sau
// khi rút điện. Thư viện Preferences của Arduino-ESP32 bọc NVS cho dễ dùng.
// Flash chịu được số lần ghi có hạn (khoảng 100 nghìn lần mỗi ô), nên CHỈ ghi khi bộ
// ngưỡng thật sự đổi. room1/config là retained: mỗi lần Node B nối lại MQTT broker gửi
// lại bản cũ; ghi lại mỗi lần như vậy là phí.
#pragma once
#include <stddef.h>
#include "node_b_logic.h"

// config_store_init: tạo mutex, đọc bộ ngưỡng từ NVS. NVS trống / hỏng / sai ràng buộc
// thì dùng thresholds_default. In bộ đang dùng ra Serial. Gọi 1 lần trong setup().
void config_store_init();

// config_store_apply_json: xử lý 1 tin room1/config (gọi từ callback MQTT).
//   Ra: true nếu bộ mới hợp lệ (đã áp dụng; đã ghi NVS nếu khác bộ cũ).
//       false nếu sai -> giữ bộ cũ, in lý do ra Serial.
bool config_store_apply_json(const char *payload, size_t len);

// config_store_get: bản sao bộ ngưỡng đang dùng (an toàn đa task).
Thresholds config_store_get();
