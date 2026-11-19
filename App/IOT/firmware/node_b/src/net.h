// net: WiFi + NTP + MQTT (PubSubClient) của Node B. CONTRACT mục 4, 5, 8.
//
// QUY TẮC QUAN TRỌNG: PubSubClient KHÔNG an toàn khi 2 task cùng gọi. Mọi lệnh
// mqtt.publish / mqtt.loop chỉ chạy trong task_net (net.cpp). Task khác muốn gửi
// telemetry thì gọi net_request_telemetry() (chỉ bật 1 cờ), task_net sẽ tự gửi.
// Lệnh room1/cmd và room1/config được xử lý NGAY TRONG callback của mqtt.loop(), tức
// cũng đang ở task_net, nên cmd.cpp / config_store.cpp được gọi net_publish().
#pragma once
#include <Arduino.h>

// net_init: WiFi STA, WiFi.setSleep(false), WiFi.begin, chờ tối đa 15 s cho lần đầu,
// bật NTP, cấu hình PubSubClient (server, buffer 1024, callback). KHÔNG tạo task.
// Gọi trong setup() TRƯỚC espnow_rx_init (ESP-NOW cần WiFi đã ở chế độ STA).
void net_init();

// task_net: vòng lặp mạng (watchdog WiFi, nối MQTT, mqtt.loop, gửi telemetry).
// main.cpp tạo task này bằng xTaskCreate.
void task_net(void *pvParameters);

// net_request_telemetry: xin gửi 1 bản telemetry sớm nhất có thể. Gọi từ task nào cũng được.
void net_request_telemetry();

// net_publish: publish 1 chuỗi. CHỈ gọi từ task_net (kể cả callback MQTT).
//   Ra: true nếu PubSubClient nhận gửi (đang kết nối và đủ buffer).
bool net_publish(const char *topic, const char *payload, bool retained);

// net_gateway_online: room1/gateway/status gần nhất là "online" không (W4 dùng cho
// chế độ offline; W2 chỉ ghi nhận và in log).
bool net_gateway_online();
