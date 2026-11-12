// Liên kết ESP-NOW tới Node B: quét channel hotspot, ghép cặp, gửi unicast.
// CONTRACT mục 3. Node A KHÔNG kết nối WiFi, chỉ quét để biết channel của hotspot.
#pragma once
#include <Arduino.h>
#include "espnow_packet.h"

// WiFi STA + esp_now_init + đăng ký callback + thêm peer broadcast. Gọi 1 lần trong setup().
bool espnow_link_init();

// Quét tới khi thấy WIFI_SSID (secrets.h), đặt radio sang channel đó. Chặn tới khi thấy.
void espnow_link_scan_channel();

// Broadcast PKT_PAIR_REQ, chờ PKT_PAIR_RESP tối đa timeout_ms.
// Thành công: học MAC Node B, thêm peer, trả true.
bool espnow_link_pair(uint32_t timeout_ms);

// Gửi unicast 1 gói tới Node B, CHỜ callback onSent. true = Node B đã ACK.
bool espnow_link_send(const PktData *pkt);

// Xóa peer Node B (khi mất Node B, trước khi ghép lại).
void espnow_link_unpair();

int espnow_link_channel();
