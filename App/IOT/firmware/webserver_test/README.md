# webserver_test (issue #33)

Webserver của Node B, bản đứng riêng. `control_stub.cpp` giả module điều khiển theo đúng `control.h`
(CONTRACT mục 12). W2 ghép vào Node B: chép `web.cpp`, `web.h`, bỏ `control_stub.cpp`, gọi `web_start()` sau khi nối WiFi.

## Chạy

1. Chép `include/secrets.example.h` thành `include/secrets.h`, điền WiFi hotspot.
2. PlatformIO: Build, Upload, Monitor. Monitor in ra IP.
3. Mở `http://<IP>/` bằng điện thoại cùng hotspot.

**Đường dẫn có dấu tiếng Việt** (vd `D:\HK261\Phát triển IOT\...`) làm toolchain ESP32 báo `tmpXXXX.tmp: Invalid argument`, build hỏng. Clone repo vào thư mục không dấu, ví dụ `D:\IOT\Assignment_IOT_HK261`.

Board tạm là `esp32-s3-devkitc-1` vì `boards/yolo_uno.json` (#24) chưa có. Có rồi thì đổi `board = yolo_uno` trong `platformio.ini`.

## Endpoint

| URL | Làm gì | Trả về |
|---|---|---|
| `GET /` | Trang HTML, tự gọi `/api/state` mỗi 2 giây | HTML |
| `GET /api/state` | Số đo + trạng thái, key đúng CONTRACT mục 5 | `{"temperature":29.1,…,"fan_level":1,"light_on":true,"mode":"auto"}` |
| `GET /api/fan?level=0..2` | `control_set_fan(level, CmdSource::Web)` | `{"ok":true,"state":{…}}` |
| `GET /api/light?on=0\|1` | `control_set_light(on, CmdSource::Web)` | như trên |
| `GET /api/mode?value=auto\|manual` | `control_set_mode(...)` (thêm so với issue) | như trên |

Tham số sai trả HTTP 400, `{"ok":false,"error":"bad_params","state":{…}}`. `state` luôn đủ `fan_level`, `light_on`, `mode` như CONTRACT 6.3.
Chưa có số đo (`valid = false`) thì `/api/state` bỏ hẳn các key cảm biến và có `"node_a_online": false`.
