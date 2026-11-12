#include "sensors.h"
#include "DHT20.h"
#include "node_a_logic.h"

DHT20 dht20;
static PresenceState presence_state;
static uint16_t dht_fail_count = 0;

void sensors_init() {
  Wire.begin(PIN_SDA, PIN_SCL);  // như LAB3: Wire.begin(11, 12) rồi dht20.begin()
  if (!dht20.begin()) Serial.println("[A] DHT20 khong tra loi (kiem day I2C)");
  pinMode(PIN_PIR, INPUT);
  presence_init(&presence_state, millis());
}

void sensors_read(SensorSample *out) {
  // TODO 4: đọc DHT20.
  //   - dht20.read() trả DHT20_OK (0) khi đọc được; khác 0 là lỗi.
  //   - Đọc được thì lấy getTemperature(), getHumidity() rồi kiểm reading_valid().
  //   - Lỗi HOẶC không hợp lệ: dht_fail_count++, out->valid = false.
  //   Lưu ý: thư viện trả DHT20_ERROR_LASTREAD nếu gọi read() 2 lần cách nhau < 1 s.

  // TODO 5: out->light = analogRead(PIN_LIGHT);
  //         out->presence = presence_update(...) với digitalRead(PIN_PIR) == HIGH,
  //         timeout EMPTY_TIMEOUT_S * 1000.

  out->dht_fail = dht_fail_count;
}
