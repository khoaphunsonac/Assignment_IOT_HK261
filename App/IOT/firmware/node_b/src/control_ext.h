// control_ext.h: các hàm NỘI BỘ của module control, chỉ code Node B dùng (espnow_rx, cmd,
// net, main). Webserver KHÔNG dùng file này, chỉ dùng control.h (CONTRACT mục 12).
//
// Vì sao tách 2 file: control.h là "hợp đồng" với Khoa, phải giữ y CONTRACT. Mọi thứ
// Node B cần thêm (nhận gói Node A, áp lệnh MQTT, tick luật) để riêng ở đây, đổi thoải
// mái mà không đụng tới hợp đồng.
//
// Module control giữ TOÀN BỘ trạng thái dùng chung giữa các task:
//   - CtrlLogic (quạt, đèn, mode) -> xuất ra chân PWM quạt + LED RGB.
//   - Bản số liệu Node A mới nhất (NodeAData).
// Mọi hàm ở đây và ở control.h đều khóa cùng 1 mutex, gọi từ task nào cũng được.
#pragma once
#include <stdint.h>
#include "control.h"
#include "espnow_packet.h"
#include "node_b_logic.h"

#define NODE_A_TIMEOUT_MS 10000  // CONTRACT 3: không có PKT_DATA quá 10 s -> Node A mất

// Bản chụp số liệu Node A (đã qua xử lý của Node B), dùng để tạo telemetry.
struct NodeAData {
  bool     online;         // có PKT_DATA trong NODE_A_TIMEOUT_MS gần nhất
  uint32_t seq;
  float    temperature;    // ĐÃ là trung bình 2 lần đo (espnow_rx tính)
  float    humidity;
  uint16_t light;
  bool     presence;
  bool     anomaly;
  float    anomaly_score;  // -1 = Node A chưa chạy TinyML
  uint16_t dht_fail;
  float    espnow_loss;    // %
};

// control_init: tạo mutex, ctrl_init, cấu hình PWM quạt (chân 8) + LED RGB (chân 10),
// xuất trạng thái tắt hết. Gọi 1 lần trong setup() TRƯỚC khi tạo các task.
void control_init();

// control_on_node_a: espnow_rx gọi khi có 1 gói PKT_DATA MỚI (không gọi cho gói trùng).
//   Vào : d = gói vừa nhận, temp_avg = nhiệt độ trung bình 2 lần đo, loss_pct = espnow_loss.
//   Làm : ghi vào NodeAData, online = true, ghi mốc thời gian nhận (cho timeout 10 s).
void control_on_node_a(const PktData *d, float temp_avg, float loss_pct);

// control_get_node_a: bản sao NodeAData (an toàn đa task).
NodeAData control_get_node_a();

// control_apply_cmd: áp 1 lệnh MQTT ĐÃ PARSE OK (cmd.cpp gọi).
//   Vào : c = lệnh (method + level/on/mode). getState thì không đổi gì.
//   Ra  : CMD_OK, CMD_BAD_PARAMS hoặc CMD_IGNORED_NOT_AUTO (từ ctrl_*).
CmdErr control_apply_cmd(const ParsedCmd *c);

// control_get_lstate: trạng thái hiện tại dạng của node_b_logic (để tạo JSON).
void control_get_lstate(uint8_t *fan, bool *light, LMode *mode);

// control_tick: main gọi ~200 ms/lần.
//   Ra : true nếu có gì đổi (Node A online/offline, quạt/đèn/mode) -> cần gửi telemetry.
bool control_tick(uint32_t manual_timeout_ms);
