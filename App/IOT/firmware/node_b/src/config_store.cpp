// config_store.cpp - xem config_store.h.
#include "config_store.h"

#include <Arduino.h>
#include <Preferences.h>

#define NVS_NAMESPACE "node_b"  // tên "ngăn" trong NVS, tối đa 15 ký tự
#define NVS_KEY_TH "th"         // key chứa nguyên struct Thresholds dạng byte

static SemaphoreHandle_t s_mutex;
static Thresholds s_th;

// In bộ ngưỡng 1 dòng (minh chứng PR: đổi ngưỡng trên dashboard -> thấy dòng này).
static void print_th(const char *prefix, const Thresholds *th) {
  Serial.printf("%s fan_on_1=%.1f fan_off_1=%.1f fan_on_2=%.1f fan_off_2=%.1f "
                "light_dark=%u light_bright=%u empty=%lus manual=%lus\n",
                prefix, th->fan_on_1, th->fan_off_1, th->fan_on_2, th->fan_off_2,
                th->light_dark, th->light_bright, (unsigned long)th->empty_timeout_s,
                (unsigned long)th->manual_timeout_s);
}

Thresholds config_store_get() {
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  Thresholds th = s_th;
  xSemaphoreGive(s_mutex);
  return th;
}

void config_store_init() {
  s_mutex = xSemaphoreCreateMutex();
  Thresholds th;
  size_t n = 0;  // số byte đọc được, 0 = NVS chưa có gì
  Preferences p;
  p.begin(NVS_NAMESPACE, true);  // true = chỉ đọc
  n = p.getBytes(NVS_KEY_TH, &th, sizeof(th));
  p.end();

  if (n == sizeof(th) && thresholds_valid(&th)) {
    s_th = th; 
    print_th("[CFG] NVS:", &s_th);
  } else {
    // NVS trống (board mới) hoặc dữ liệu hỏng -> mặc định
    thresholds_default(&s_th);
    print_th("[CFG] Mac dinh:", &s_th);
  }
}

// Log mẫu:
//   bộ mới                              -> [CFG] Moi: fan_on_1=31.0 ... empty=60s manual=120s
//   nối lại MQTT, broker gửi lại bản cũ -> [CFG] Khong doi   (không ghi flash)
// Ghi flash NGOÀI khóa (putBytes chậm), chỉ giữ khóa lúc gán s_th.
bool config_store_apply_json(const char *payload, size_t len) {
  Thresholds cu = config_store_get();  // bộ đang dùng
  Thresholds th = cu;                  // config_parse ghi bộ mới vào đây

  if (!config_parse(payload, len, &th)) {
    Serial.println("[CFG] room1/config sai, giu bo cu");
    return false;
  }
  if (memcmp(&th, &cu, sizeof(th)) == 0) {  // giống hệt bộ cũ
    Serial.println("[CFG] Khong doi");
    return true;
  }
  xSemaphoreTake(s_mutex, portMAX_DELAY); 
  s_th = th; 
  xSemaphoreGive(s_mutex);
  Preferences p;
  p.begin(NVS_NAMESPACE, false);
  p.putBytes(NVS_KEY_TH, &th, sizeof(th));
  p.end();
  print_th("[CFG] Moi:", &th);
  // #43: KHÔNG gửi PKT_CONFIG ở đây (hàm này chạy trong task_net). task_espnow tự đọc
  // config_store_get() mỗi giây và gửi khi empty_timeout_s khác lần trước (espnow_rx.cpp).
  return true;
}

// memcmp so 2 struct từng byte. Chỉ an toàn khi struct không có byte đệm (padding):
// 4 float + 2 uint16 + 2 uint32 = 16 + 4 + 8 = 28 byte, đúng bằng sizeof -> không có đệm.
static_assert(sizeof(Thresholds) == 28, "Thresholds co byte dem, memcmp khong an toan");
