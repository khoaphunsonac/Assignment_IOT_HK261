// Phiên bản firmware Node B, dạng "MAJOR.MINOR.PATCH" (CONTRACT mục 5, 8, 9).
// Gửi trong telemetry (fw_version) và room1/status. OTA (W3) so số này với "version"
// trong room1/ota: bằng nhau thì bỏ qua. Mỗi lần build bản mới để OTA thì TĂNG số này.
#pragma once
#define FW_VERSION "1.0.0"
