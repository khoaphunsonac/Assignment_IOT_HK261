// LCD 16x2 I2C trên Node B (#60). Chỉ đọc số liệu qua control.h (control_get_state /
// control_get_sensors), không đọc biến toàn cục của module khác.
//
// Màn hình đổi mỗi LCD_PAGE_MS:
//   Màn 1: nhiệt độ / độ ẩm         ("Nhiet do: 29.1*C" / "Do am   : 65.0 %")
//   Màn 2: quạt, đèn / chế độ       ("Quat: 2  Den:BAT" / "Che do: manual")
// Mất Node A (valid = false) thì màn 1 in "Node A:" / "mat ket noi" thay số đo.
#pragma once

#define LCD_ADDR     0x21   // README pinout: LCD 16x2 cổng I2C đỏ, địa chỉ 0x21
#define LCD_PIN_SDA  11
#define LCD_PIN_SCL  12
#define LCD_PAGE_MS  3000   // đổi màn mỗi 3 s
#define LCD_REFRESH_MS 500  // vẽ lại nội dung màn hiện tại (số đo đổi thì thấy ngay)

// lcd_start: tạo task FreeRTOS "task_lcd" (ưu tiên 1). Mọi lệnh I2C (Wire) chỉ chạy trong
// task này. Gọi 1 lần cuối setup().
void lcd_start();
