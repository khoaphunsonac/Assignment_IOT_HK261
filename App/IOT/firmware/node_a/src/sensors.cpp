// Đọc DHT20, PIR, cảm biến ánh sáng cho Node A.
// DHT20: thư viện của môn (LAB3 Support Files, lib/DHT20).
#include "sensors.h"
#include "DHT20.h"
#include "node_a_logic.h"

DHT20 dht20;
static PresenceState presence_state;   // nhớ lần cuối PIR thấy chuyển động
static uint16_t dht_fail_count = 0;     // tổng số lần DHT20 lỗi từ lúc boot
// #43: thời gian chờ phòng trống (giây). task_espnow_tx GHI, task_sensor ĐỌC.
// uint16_t ghi/đọc 1 lệnh trên ESP32 (căn lề 2 byte) nên không cần mutex; volatile để
// compiler không giữ bản cũ trong thanh ghi. Mất điện -> về 60 tới khi B gửi PKT_CONFIG.
static volatile uint16_t s_empty_timeout_s = EMPTY_TIMEOUT_DEFAULT_S;

// Thứ tự như LAB3: Wire.begin(SDA, SCL) rồi dht20.begin().
// DHT20 không trả lời lúc boot thì chỉ in cảnh báo, KHÔNG dừng: các lần đọc sau tự thử lại.
void sensors_init() {
  Wire.begin(PIN_SDA, PIN_SCL);  // như LAB3: Wire.begin(11, 12) rồi dht20.begin()
  if (!dht20.begin()) Serial.println("[A] DHT20 khong tra loi (kiem day I2C)");
  pinMode(PIN_PIR, INPUT);
  presence_init(&presence_state, millis());
}

// T/H đi theo 3 nhánh, sau đó LUÔN đọc light + presence:
//   1. DHT20 mất kết nối hoặc read() lỗi   -> dht_fail++, valid = false.
//   2. Đọc được nhưng NaN / ngoài khoảng   -> dht_fail++, valid = false.
//   3. Đọc được và hợp lệ                  -> valid = true.
// Nhánh nào cũng gán valid, để không giữ lại giá trị của lần đọc trước.
//
// Vì sao kiểm isConnected() TRƯỚC read(): thư viện DHT20 có lỗi, rút cảm biến thì read()
// treo vô hạn (requestData() trả lỗi I2C là số dương nên read() không bắt, rồi kẹt ở
// `while (isMeasuring()) yield();` vì Wire.read() = -1 -> 0xFF -> luôn "đang đo").
// `||` đánh giá ngắn mạch: isConnected() false thì read() KHÔNG được gọi.
// Cắm lại thì lần read() sau tự resetSensor() nên tự hồi phục (lab 13/11, L4 PASS).
// Còn khe hở rất nhỏ: rút đúng trong vài ms giữa isConnected() và read() vẫn có thể treo.
void sensors_read(SensorSample *out) {
  if(!dht20.isConnected() || dht20.read() != DHT20_OK) {
    dht_fail_count++;
    out->humidity = 0;
    out->temperature = 0;
    out->valid = false;
  } else {
    out->humidity = dht20.getHumidity();
    out->temperature = dht20.getTemperature();
    if(!reading_valid(out->temperature, out->humidity)) {
      dht_fail_count++;
      out->valid = false;
    }
    else out->valid = true;
  }

  // Ánh sáng và PIR không phụ thuộc DHT20 nên luôn đọc.
  out->light = analogRead(PIN_LIGHT);
  out->presence = presence_update(&presence_state, digitalRead(PIN_PIR) == HIGH, millis(), (uint32_t)s_empty_timeout_s * 1000u);
  out->dht_fail = dht_fail_count;
}

// #43. Ở sensors_read ép (uint32_t) trước khi nhân 1000: 65535 * 1000 = 65.535.000
// vượt uint16_t, phải nhân trong kiểu 32 bit (đúng kiểu tham số timeout_ms).
void sensors_set_empty_timeout_s(uint16_t s) { s_empty_timeout_s = s; }
uint16_t sensors_get_empty_timeout_s() { return s_empty_timeout_s; }
