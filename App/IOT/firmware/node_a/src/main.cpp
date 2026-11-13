// Node A (#26): đọc cảm biến 2 s/lần, gửi PKT_DATA tới Node B bằng ESP-NOW unicast.
// task_sensor --(sample_queue, 1 phần tử, luôn giữ mẫu MỚI NHẤT)--> task_espnow_tx
// TinyML làm sau (#30 xong model): W1 gửi anomaly = 0, anomaly_score = -1 (CONTRACT mục 3).
//
// Vì sao 2 task: đọc cảm biến (nhịp 2 s đều) tách khỏi gửi/ghép cặp/quét channel (có thể
// chặn vài giây). Quét hotspot hay chờ Node B thì dòng cảm biến vẫn in đều (lab 13/11, L1).
// Vì sao queue 1 phần tử + xQueueOverwrite: chỉ cần mẫu mới nhất; lúc task gửi đang
// bận (ghép lại) thì mẫu cũ bị ghi đè thay vì dồn hàng rồi gửi số đã cũ.
#include <Arduino.h>
#include "sensors.h"
#include "espnow_link.h"
#include "node_a_logic.h"

#define LED_PIN GPIO_NUM_48
#define SENSOR_PERIOD_MS 2000
#define PAIR_TIMEOUT_MS 1000
#define PAIR_TRIES_BEFORE_SCAN 5  // ghép cặp hụt 5 lần liền -> quét lại channel

QueueHandle_t sample_queue;

// task_sensor: mỗi 2 s đọc 1 mẫu, in 1 dòng log, đẩy vào queue.
// In cả mẫu lỗi (valid: 0) để thấy được lúc DHT20 hỏng; lọc mẫu lỗi là việc của task gửi.
void task_sensor(void *pvParameters) {
  SensorSample s;
  while (1) {
    sensors_read(&s);
    Serial.printf("[A] T: %.1f H: %.1f light: %u presence: %u valid: %d dht_fail: %u\n",
    s.temperature, s.humidity, s.light, s.presence, s.valid, s.dht_fail);
    xQueueOverwrite(sample_queue, &s);  // queue 1 phần tử: ghi đè mẫu cũ chưa gửi
    vTaskDelay(SENSOR_PERIOD_MS);
  }
}

// task_espnow_tx: ghép cặp, lấy mẫu, đóng gói, gửi (có gửi lại), in thống kê.
// Mỗi vòng while(1) làm theo thứ tự:
//   (1) Chưa ghép cặp -> thử ghép 1 lần rồi quay lại đầu vòng. Hụt 5 lần liền thì quét
//       lại channel (hotspot có thể đã đổi channel). Ghép được thì tx_init.
//   (2) Chờ mẫu mới (portMAX_DELAY: ngủ tới khi có, nhịp 2 s do task_sensor quyết định).
//       Mẫu lỗi (valid = false) -> bỏ, KHÔNG tăng seq. Node B đếm mất gói dựa vào seq
//       nhảy cóc; tăng seq cho mẫu không gửi thì B tưởng mất gói, espnow_loss báo sai.
//   (3) Đóng gói PktData MỘT lần cho mẫu này (gửi lại thì gửi y nguyên, cùng seq).
//   (4) Vòng gửi: gửi -> tx_on_result quyết định. RESEND thì quay lại gửi tiếp;
//       DONE / GIVE_UP / REPAIR thì seq++ và thoát vòng trong. Vòng trong tối đa
//       TX_MAX_RETRY + 1 = 4 lần, con số này do tx_on_result quyết định, không đếm ở đây.
//       GIVE_UP/REPAIR vẫn seq++ để B thấy seq nhảy và tự đếm là mất 1 gói.
//   (5) Khoảng 10 s in 1 dòng thống kê (không dùng vTaskDelay vì sẽ chặn việc gửi).
// seq là trạng thái của cả task (sống từ lúc boot), pd.seq chỉ là bản chụp lúc gửi.
void task_espnow_tx(void *pvParameters) {
  uint32_t seq = 0;
  bool paired = false;
  int pair_fail = 0;
  TxState tx;
  tx_init(&tx);
  uint32_t sent_ok = 0, resend = 0, give_up = 0, repair = 0;  // in ra để làm minh chứng PR
  unsigned long last = millis();  // mốc lần in thống kê trước

  while (1) {
    // (1) Ghép cặp
    if(!paired) {
      if(!espnow_link_pair(PAIR_TIMEOUT_MS)){
        pair_fail++;
        if(pair_fail == PAIR_TRIES_BEFORE_SCAN){
          espnow_link_scan_channel();
          pair_fail = 0;
        }
      } else {
        paired = true;
        tx_init(&tx);
        pair_fail = 0;
      }
      continue;
    }

    // (2) Lấy mẫu, bỏ mẫu lỗi
    SensorSample s;
    xQueueReceive(sample_queue, &s, portMAX_DELAY);
    if(!s.valid) continue;

    // (3) Đóng gói theo CONTRACT mục 3 (PktData 28 byte)
    PktData pd;
    pd.hdr.magic = ESPNOW_MAGIC;
    pd.hdr.version = ESPNOW_VERSION;
    pd.hdr.type = PKT_DATA;
    pd.seq = seq;
    pd.temperature = s.temperature;
    pd.humidity = s.humidity;
    pd.light = s.light;
    pd.presence = s.presence;
    pd.anomaly= 0;
    pd.anomaly_score = -1;  // -1 = chưa chạy TinyML (0 là một điểm số thật)
    pd.dht_fail = s.dht_fail;

    // (4) Gửi, gửi lại theo tx_on_result. Mỗi lần gửi gọi tx_on_result đúng 1 lần.
    while(true){
      bool ok = espnow_link_send(&pd);
      TxAction status = tx_on_result(&tx, ok, millis());
      if(status == TX_DONE) {
        seq++;
        sent_ok++;
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));  // LED nháy theo mỗi gói gửi được
        break;
      } else if (status == TX_RESEND) {
        resend++;  // không break: quay lại gửi đúng gói pd này
      } else if (status == TX_GIVE_UP) {
        seq++;
        give_up++;
        break;
      } else if (status == TX_REPAIR) {
        seq++;
        repair++;
        espnow_link_unpair();
        paired = false;  // vòng sau quay về bước (1) để ghép lại
        break;
      }
    }

    // (5) Thống kê ~10 s/lần. Đặt sau bước (2) nên nếu DHT20 lỗi liên tục thì dòng này
    // cũng tạm không in (chấp nhận ở W1).
    if (millis() - last >= 10000) {
      Serial.printf("[A] ch: %d paired: %d seq: %u sent_ok: %u resend: %u give_up: %u repair: %u free: %u\n",
      espnow_link_channel(), paired, seq, sent_ok, resend, give_up, repair, ESP.getMinFreeHeap());
      last = millis();
    }
  }
}

// setup: Serial -> cảm biến -> ESP-NOW -> quét hotspot -> tạo queue + 2 task.
//   delay(2000) chờ USB CDC của ESP32-S3 sẵn sàng, đồng thời giúp lần đọc DHT20 đầu
//   không dính DHT20_ERROR_LASTREAD (millis() > 1000).
//   espnow_link_scan_channel() chặn tới khi thấy hotspot: hotspot tắt thì chưa có task
//   nào chạy, chỉ thấy "Quet tim...".
//   loop() để trống: mọi việc nằm trong 2 task FreeRTOS.
void setup() {
  Serial.begin(115200);
  delay(2000);  // chờ USB CDC
  pinMode(LED_PIN, OUTPUT);
  Serial.println("===== NODE A =====");

  sensors_init();
  if (!espnow_link_init()) {
    Serial.println("[A] FAIL espnow_link_init");
    return;
  }
  espnow_link_scan_channel();

  sample_queue = xQueueCreate(1, sizeof(SensorSample));
  xTaskCreate(task_sensor, "task_sensor", 4096, NULL, 2, NULL);
  xTaskCreate(task_espnow_tx, "task_espnow_tx", 4096, NULL, 2, NULL);
}

void loop() {}
