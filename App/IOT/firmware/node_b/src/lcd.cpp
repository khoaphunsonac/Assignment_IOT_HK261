// LCD 16x2 I2C trên Node B (#60). Xem lcd.h.
//
// Không gọi lcd.clear() mỗi lần vẽ (clear mất ~2 ms và làm màn nháy): mỗi dòng được đệm
// đủ 16 ký tự bằng dấu cách rồi ghi đè từ cột 0.
#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

#include "control.h"
#include "lcd.h"

static LiquidCrystal_I2C lcd(LCD_ADDR, 16, 2);

// Ghi 1 dòng, đệm dấu cách cho đủ 16 ký tự (xóa ký tự thừa của nội dung cũ).
static void lcd_line(uint8_t row, const char *text) {
  char buf[17];
  snprintf(buf, sizeof(buf), "%-16s", text);
  lcd.setCursor(0, row);
  lcd.print(buf);
}

// Màn 1: nhiệt độ / độ ẩm. 0xDF là ký tự độ trong bảng mã HD44780.
static void draw_sensors(const SensorSnapshot &s) {
  char l1[17], l2[17];
  if (!s.valid) {
    lcd_line(0, "Node A:");
    lcd_line(1, "mat ket noi");
    return;
  }
  snprintf(l1, sizeof(l1), "Nhiet do:%5.1f" "\xDF" "C", s.temperature);
  snprintf(l2, sizeof(l2), "Do am   :%5.1f %%", s.humidity);
  lcd_line(0, l1);
  lcd_line(1, l2);
}

// Màn 2: quạt, đèn / chế độ.
static void draw_state(const ControlState &st) {
  char l1[17], l2[17];
  snprintf(l1, sizeof(l1), "Quat: %u  Den:%s", st.fan_level, st.light_on ? "BAT" : "TAT");
  snprintf(l2, sizeof(l2), "Che do: %s", mode_to_str(st.mode));
  lcd_line(0, l1);
  lcd_line(1, l2);
}

static void task_lcd(void *) {
  // Wire.begin(11, 12) trước: lcd.init() gọi Wire.begin() không tham số, bus đã chạy thì
  // core ESP32 giữ nguyên chân cũ.
  Wire.begin(LCD_PIN_SDA, LCD_PIN_SCL);
  Wire.beginTransmission(LCD_ADDR);
  if (Wire.endTransmission() != 0) {
    Serial.printf("[LCD] khong thay LCD o 0x%02X (kiem day cong I2C do), van chay tiep\n", LCD_ADDR);
  } else {
    Serial.printf("[LCD] OK 0x%02X\n", LCD_ADDR);
  }
  lcd.init();
  lcd.backlight();
  lcd.clear();

  uint8_t page = 0;
  uint32_t page_start = millis();
  bool last_valid = true;
  for (;;) {
    if (millis() - page_start >= LCD_PAGE_MS) {
      page ^= 1;
      page_start = millis();
    }
    SensorSnapshot s = control_get_sensors();
    if (s.valid != last_valid) {
      Serial.printf("[LCD] Node A %s\n", s.valid ? "co lai" : "mat ket noi");
      last_valid = s.valid;
    }
    if (page == 0) draw_sensors(s);
    else draw_state(control_get_state());
    vTaskDelay(pdMS_TO_TICKS(LCD_REFRESH_MS));
  }
}

// Ưu tiên 1 như task_web: LCD chậm vài trăm ms không sao, không được chiếm CPU của
// task_espnow (3). Stack 4096 (bài học LAB3: 2048 tràn).
void lcd_start() {
  xTaskCreate(task_lcd, "task_lcd", 4096, nullptr, 1, nullptr);
}
