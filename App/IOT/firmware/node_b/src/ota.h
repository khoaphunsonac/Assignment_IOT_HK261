// ota: cập nhật firmware Node B qua WiFi (#58, T14). CONTRACT mục 9.
//
// Luồng (Gateway = laptop chạy ota_publish.py của #45, hoặc tạm mosquitto_pub + http.server):
//   1. Gateway publish room1/ota {"version","url","size","sha256"}.
//   2. task_net nhận tin trong callback MQTT -> gọi ota_on_mqtt(): CHỈ kiểm lệnh rồi đánh
//      thức task_ota. KHÔNG tải file trong callback: tải 700 KB mất vài giây, trong lúc đó
//      task_net bị chặn -> không mqtt.loop() -> broker ngắt kết nối (keepalive 15 s), lệnh
//      room1/cmd không ai xử lý (TASKS_W3 #58: "OTA chạy trong task riêng").
//   3. task_ota: tải HTTP từng khối 1 KB -> vừa ghi vào phân vùng OTA còn trống (Update)
//      vừa tính sha256 -> gửi "downloading" mỗi 10% -> "verifying" -> so sha256:
//        sai : Update.abort() (KHÔNG đổi phân vùng khởi động), gửi failed checksum_mismatch.
//        đúng: Update.end() (đặt phân vùng mới làm phân vùng khởi động), ghi NVS "pending",
//              gửi "rebooting", khởi động lại.
//   4. Bản mới khởi động: task_ota kiểm (ota_boot_case): ảnh đang "chờ xác nhận" thì chờ
//      WiFi + MQTT tối đa 60 s:
//        nối được: đánh dấu bản mới tốt, gửi "success" (room1/status đã có fw_version mới
//                  vì net.cpp gửi FW_VERSION mỗi lần nối MQTT).
//        không   : yêu cầu bootloader quay về bản cũ + khởi động lại. Bản cũ chạy lên, thấy
//                  NVS "pending" khác bản đang chạy -> gửi failed rolled_back.
//
// Phân vùng: board yolo_uno.json dùng default_8MB.csv: app0, app1 (mỗi cái 3,3 MB) + otadata.
// Update tự chọn phân vùng KHÔNG đang chạy để ghi.
// Rollback: Arduino core 2.0.17 (espressif32@6.9.0) build sẵn bootloader có
// CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y (Claude đã kiểm sdkconfig esp32s3 của core 2.0.17).
#pragma once
#include <Arduino.h>

// ota_start: tạo task_ota (ưu tiên 1, stack 8192). Gọi trong setup() SAU khi tạo task_net.
// task_ota việc đầu tiên là kiểm "vừa OTA xong không" (có thể chờ tới 60 s), rồi ngủ chờ lệnh.
void ota_start();

// ota_on_mqtt: CHỈ gọi từ callback MQTT (task_net) khi topic = room1/ota.
//   Vào : payload, len = nội dung tin (không có '\0' cuối).
//   Làm : parse + quyết định (ota_logic), hợp lệ thì chép lệnh và đánh thức task_ota.
//         Không publish gì (lệnh hỏng / trùng bản / đang bận chỉ in Serial).
void ota_on_mqtt(const uint8_t *payload, unsigned int len);

// ota_busy: true khi đang tải/ghi (web/LCD có thể hiện "Dang OTA..." nếu muốn).
bool ota_busy();
