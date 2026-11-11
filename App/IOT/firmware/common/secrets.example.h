// Mẫu file bí mật dùng chung cho Node A và Node B.
// Cách dùng: chép file này thành secrets.h (cùng thư mục) rồi điền giá trị thật.
// secrets.h đã có trong .gitignore, KHÔNG commit.
#pragma once

#define WIFI_SSID "ten_hotspot"
#define WIFI_PASS "mat_khau_hotspot"

// IP laptop chạy Mosquitto (xem bằng ipconfig, có thể đổi mỗi lần bật hotspot)
#define MQTT_HOST "192.168.1.100"
