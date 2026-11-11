// Webserver Node B (issue #33). Chỉ gọi hàm trong control.h, không đụng biến của module khác.
// Chạy trong một task FreeRTOS riêng để W2 ghép thẳng vào Node B.

#include <Arduino.h>
#include <WebServer.h>
#include "control.h"
#include "web.h"

static WebServer server(80);

static const char INDEX_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="vi"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Room 1 - Node B</title>
<style>
body{font-family:sans-serif;margin:0;padding:16px;background:#f2f4f7;color:#222}
h1{font-size:20px;margin:0 0 12px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:10px}
.card{background:#fff;border-radius:10px;padding:12px;box-shadow:0 1px 3px #0002}
.lbl{font-size:13px;color:#666}.val{font-size:26px;font-weight:600;margin-top:4px}
.row{display:flex;gap:8px;flex-wrap:wrap;margin-top:12px}
button{flex:1;min-width:80px;padding:12px;font-size:16px;border:0;border-radius:8px;background:#dfe5ee}
button.on{background:#1e88e5;color:#fff}
#msg{margin-top:10px;font-size:13px;color:#666;min-height:1em}
</style></head><body>
<h1>Phòng 1 – Node B</h1>
<div class="grid">
 <div class="card"><div class="lbl">Nhiệt độ</div><div class="val" id="temperature">--</div></div>
 <div class="card"><div class="lbl">Độ ẩm</div><div class="val" id="humidity">--</div></div>
 <div class="card"><div class="lbl">Ánh sáng (ADC)</div><div class="val" id="light">--</div></div>
 <div class="card"><div class="lbl">Có người</div><div class="val" id="presence">--</div></div>
 <div class="card"><div class="lbl">Quạt</div><div class="val" id="fan_level">--</div></div>
 <div class="card"><div class="lbl">Đèn</div><div class="val" id="light_on">--</div></div>
 <div class="card"><div class="lbl">Chế độ</div><div class="val" id="mode">--</div></div>
</div>
<div class="row">
 <button id="f0" onclick="cmd('/api/fan?level=0')">Quạt 0</button>
 <button id="f1" onclick="cmd('/api/fan?level=1')">Quạt 1</button>
 <button id="f2" onclick="cmd('/api/fan?level=2')">Quạt 2</button>
</div>
<div class="row">
 <button id="lt" onclick="cmd('/api/light?on='+(S.light_on?0:1))">Bật/tắt đèn</button>
 <button id="md" onclick="cmd('/api/mode?value='+(S.mode=='auto'?'manual':'auto'))">Auto/Manual</button>
</div>
<div id="msg"></div>
<script>
var S={};
function show(s){S=s;
 var t=function(id,v){document.getElementById(id).textContent=v;};
 t('temperature',s.temperature!==undefined?s.temperature.toFixed(1)+' °C':'--');
 t('humidity',s.humidity!==undefined?s.humidity.toFixed(1)+' %':'--');
 t('light',s.light!==undefined?s.light:'--');
 t('presence',s.presence===undefined?'--':(s.presence?'Có':'Không'));
 t('fan_level',s.fan_level);t('light_on',s.light_on?'Bật':'Tắt');t('mode',s.mode);
 for(var i=0;i<3;i++)document.getElementById('f'+i).className=(s.fan_level==i?'on':'');
 document.getElementById('lt').className=s.light_on?'on':'';
 document.getElementById('md').className=s.mode=='auto'?'on':'';
}
function load(){fetch('/api/state').then(function(r){return r.json();}).then(show)
 .catch(function(){document.getElementById('msg').textContent='Mất kết nối tới Node B';});}
function cmd(u){fetch(u).then(function(r){return r.json();}).then(function(j){
 document.getElementById('msg').textContent=j.ok?'OK':'Lỗi: '+j.error;load();});}
load();setInterval(load,2000);
</script></body></html>)HTML";

// {"fan_level":1,"light_on":true,"mode":"auto"}: dùng chung cho /api/state và trả lời lệnh.
static int write_state(char* buf, size_t n, const ControlState& st) {
  return snprintf(buf, n, "\"fan_level\":%u,\"light_on\":%s,\"mode\":\"%s\"",
                  st.fan_level, st.light_on ? "true" : "false", mode_to_str(st.mode));
}

static void send_result(bool ok, const char* error) {
  char st[96];
  write_state(st, sizeof(st), control_get_state());
  char body[192];
  if (ok) snprintf(body, sizeof(body), "{\"ok\":true,\"state\":{%s}}", st);
  else snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\",\"state\":{%s}}", error, st);
  server.send(ok ? 200 : 400, "application/json", body);
}

static void handle_root() { server.send_P(200, "text/html; charset=utf-8", INDEX_HTML); }

static void handle_state() {
  SensorSnapshot s = control_get_sensors();
  char st[96];
  write_state(st, sizeof(st), control_get_state());
  char body[320];
  if (s.valid) {
    // Key và số lẻ theo CONTRACT mục 2 và 5. Chưa có số đo thì bỏ hẳn key cảm biến.
    char score[24] = "";
    if (s.anomaly_score >= 0) snprintf(score, sizeof(score), ",\"anomaly_score\":%.3f", s.anomaly_score);
    snprintf(body, sizeof(body),
             "{\"temperature\":%.1f,\"humidity\":%.1f,\"light\":%u,\"presence\":%s,"
             "\"anomaly\":%s%s,\"node_a_online\":true,%s}",
             s.temperature, s.humidity, s.light, s.presence ? "true" : "false",
             s.anomaly ? "true" : "false", score, st);
  } else {
    snprintf(body, sizeof(body), "{\"node_a_online\":false,%s}", st);
  }
  server.send(200, "application/json", body);
}

static void handle_fan() {
  String v = server.arg("level");
  if (v.length() != 1 || v[0] < '0' || v[0] > '2') return send_result(false, "bad_params");
  send_result(control_set_fan(v[0] - '0', CmdSource::Web), "bad_params");
}

static void handle_light() {
  String v = server.arg("on");
  if (v != "0" && v != "1") return send_result(false, "bad_params");
  send_result(control_set_light(v == "1", CmdSource::Web), "bad_params");
}

static void handle_mode() {
  String v = server.arg("value");
  if (v == "auto") return send_result(control_set_mode(Mode::Auto), "bad_params");
  if (v == "manual") return send_result(control_set_mode(Mode::Manual), "bad_params");
  send_result(false, "bad_params");
}

static void web_task(void*) {
  server.on("/", HTTP_GET, handle_root);
  server.on("/api/state", HTTP_GET, handle_state);
  server.on("/api/fan", HTTP_GET, handle_fan);
  server.on("/api/light", HTTP_GET, handle_light);
  server.on("/api/mode", HTTP_GET, handle_mode);
  server.onNotFound([] { server.send(404, "application/json", "{\"ok\":false,\"error\":\"not_found\"}"); });
  server.begin();
  for (;;) {
    server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void web_start() {
  xTaskCreate(web_task, "web", 6144, nullptr, 1, nullptr);
}
