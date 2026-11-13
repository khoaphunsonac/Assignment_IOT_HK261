// Liên kết ESP-NOW tới Node B: quét channel hotspot, ghép cặp, gửi unicast.
// CONTRACT mục 3. Node A KHÔNG kết nối WiFi, chỉ quét để biết channel của hotspot.
//
// Vì sao phải quan tâm channel: ESP-NOW chỉ nói chuyện được khi 2 board cùng channel.
// Node B nối hotspot (để lên MQTT) nên radio của B bị kéo theo channel của hotspot.
// Node A vì vậy phải đứng đúng channel đó, và khi hotspot bật lại có thể đổi channel
// thì A phải quét lại (đã thử ở lab 11/11).
//
// Vì sao ghép cặp thay vì ghi cứng MAC: board mượn mỗi buổi khác nhau. A broadcast
// "xin ghép cặp", B trả lời, A học MAC của B lúc chạy rồi chuyển sang unicast.
// Unicast có ACK ở tầng MAC nên biết gói tới chưa để gửi lại (lab 11/11 ở 7 m:
// unicast mất 0%, broadcast mất 7,58%).
#pragma once
#include <Arduino.h>
#include "espnow_packet.h"

// espnow_link_init: WiFi STA + esp_now_init + đăng ký 2 callback + thêm peer broadcast.
//   Ra: false nếu tạo queue hoặc esp_now_init lỗi. Gọi 1 lần trong setup().
bool espnow_link_init();

// espnow_link_scan_channel: quét WiFi tới khi thấy WIFI_SSID (secrets.h), đặt radio sang
// channel đó. CHẶN tới khi thấy (hotspot tắt thì đứng chờ, in "Quet tim..." mỗi lần).
void espnow_link_scan_channel();

// espnow_link_pair: broadcast PKT_PAIR_REQ, chờ PKT_PAIR_RESP tối đa timeout_ms.
//   Ra: true = học được MAC Node B, đã thêm peer, đã chỉnh channel theo B.
//       false = hết giờ không ai trả lời (hoặc add peer lỗi).
bool espnow_link_pair(uint32_t timeout_ms);

// espnow_link_send: gửi unicast 1 gói tới Node B rồi CHỜ callback onSent (tối đa 100 ms).
//   Ra: true = Node B đã ACK. false = esp_now_send lỗi, không ACK, hoặc hết giờ chờ.
bool espnow_link_send(const PktData *pkt);

// espnow_link_unpair: xóa peer Node B (khi mất Node B, trước khi ghép lại).
void espnow_link_unpair();

// espnow_link_channel: channel radio hiện tại (-1 nếu chưa quét). Chỉ để in thống kê.
int espnow_link_channel();
