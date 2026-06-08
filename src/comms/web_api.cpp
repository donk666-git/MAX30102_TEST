#include "web_api.h"

#include "../config.h"
#include "../drivers/max30102.h"
#include "../drivers/max30205.h"
#include "time_api.h"

#include <WebServer.h>
#include <WiFi.h>

static WebServer s_server(80);
static bool s_running = false;
static bool s_routes_registered = false;
static unsigned long s_last_client_ms = 0;

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>WatchTest</title><style>
:root{color-scheme:dark}body{margin:0;background:#090d12;color:#edf2f7;font-family:Arial,sans-serif}
main{max-width:940px;margin:auto;padding:16px}header{display:flex;justify-content:space-between;gap:12px;align-items:flex-end;border-bottom:1px solid #25303a;padding-bottom:12px}
h1{font-size:24px;margin:0}.muted,.label{color:#91a1ae}.grid{display:grid;grid-template-columns:repeat(4,1fr);gap:10px;margin:14px 0}
.card,.panel{background:#131a22;border:1px solid #25303a;border-radius:8px;padding:12px}.value{font-size:32px;font-weight:700}.unit{font-size:14px;color:#91a1ae}
canvas{width:100%;height:230px;display:block}.row{display:flex;justify-content:space-between;gap:10px;line-height:1.7;font-size:14px}
@media(max-width:720px){.grid{grid-template-columns:repeat(2,1fr)}header{display:block}.value{font-size:28px}}
</style></head><body><main><header><div><h1>WatchTest Telemetry</h1><div id="status" class="muted">starting</div></div><div><b id="time">--:--</b><div id="date" class="muted">--/--</div></div></header>
<section class="grid"><div class="card"><div class="label">Heart Rate</div><span id="hr" class="value">--</span><span class="unit"> BPM</span></div>
<div class="card"><div class="label">SpO2</div><span id="spo2" class="value">--</span><span class="unit"> %</span></div>
<div class="card"><div class="label">Temperature</div><span id="temp" class="value">--</span><span class="unit"> C</span></div>
<div class="card"><div class="label">Body Est.</div><span id="body" class="value">--</span><span class="unit"> C</span></div></section>
<section class="panel"><canvas id="wave"></canvas><div class="row"><span>IR <b id="ir">--</b></span><span>RED <b id="red">--</b></span><span>Rate <b id="rate">--</b>/s</span><span>Finger <b id="finger">--</b></span></div></section>
<section class="panel"><div class="row"><span>AP</span><b id="ap">--</b></div><div class="row"><span>Clients</span><b id="clients">0</b></div><div class="row"><span>Temp contact</span><b id="contact">--</b></div><div class="row"><span>SpO2 ratio / amp</span><b id="ratio">--</b></div></section>
</main><script>
const $=id=>document.getElementById(id),pts=[];function f(v,n=1){return Number.isFinite(v)?(+v).toFixed(n):"--"}
function draw(){const c=$("wave"),r=c.getBoundingClientRect(),d=devicePixelRatio||1;c.width=Math.max(1,r.width*d);c.height=Math.max(1,r.height*d);const x=c.getContext("2d");x.scale(d,d);x.clearRect(0,0,r.width,r.height);x.strokeStyle="#25303a";for(let i=0;i<5;i++){let y=16+i*(r.height-32)/4;x.beginPath();x.moveTo(0,y);x.lineTo(r.width,y);x.stroke()}if(pts.length<2)return;let mn=Math.min(...pts),mx=Math.max(...pts),sp=Math.max(1,mx-mn);x.strokeStyle="#ff6079";x.lineWidth=2;x.beginPath();pts.forEach((v,i)=>{let px=i*r.width/79,py=r.height-16-(v-mn)/sp*(r.height-32);i?x.lineTo(px,py):x.moveTo(px,py)});x.stroke()}
function render(j){$("hr").textContent=j.bpm>0?f(j.bpm,0):"--";$("spo2").textContent=j.spo2>0?f(j.spo2,1):"--";$("temp").textContent=j.tempValid?f(j.temp,1):"--";$("body").textContent=j.bodyValid?f(j.body,1):"--";$("ir").textContent=j.ir;$("red").textContent=j.red;$("rate").textContent=j.rate;$("finger").textContent=j.finger?"ON":"OFF";$("time").textContent=j.time;$("date").textContent=j.date;$("ap").textContent=j.apIp;$("clients").textContent=j.clients;$("contact").textContent=(j.tempContact?"ON ":"OFF ")+j.tempContactSeconds+"s range "+f(j.tempRange,2);$("ratio").textContent=f(j.ratio,3)+" / "+f(j.redAmp,0)+"/"+f(j.irAmp,0);$("status").textContent=j.wifi+" · "+(j.timeValid?"time synced":"time waiting");pts.push(Number.isFinite(j.wave)?j.wave:0);while(pts.length>80)pts.shift();draw()}
async function syncTime(){try{await fetch("/api/time",{method:"POST",body:new URLSearchParams({epoch:Math.floor(Date.now()/1000)})})}catch(e){}}
async function poll(){try{let r=await fetch("/api/snapshot",{cache:"no-store"});render(await r.json())}catch(e){$("status").textContent="offline"}}
addEventListener("resize",draw);syncTime();poll();setInterval(poll,1000);
</script></body></html>
)HTML";

static void mark_client_seen()
{
    s_last_client_ms = millis();
}

static void send_no_cache(int code, const char *type, const String &body)
{
    s_server.sendHeader("Cache-Control", "no-store");
    s_server.send(code, type, body);
}

static String snapshot_json()
{
    const Max30102Reading &bio = max30102_reading();
    const Max30205Reading &temp = max30205_reading();
    char time_buf[8];
    char date_buf[8];
    time_api_get_strings(time_buf, sizeof(time_buf), date_buf, sizeof(date_buf));

    char json[960];
    snprintf(json,
             sizeof(json),
             "{\"bpm\":%.1f,\"spo2\":%.1f,\"temp\":%.2f,\"tempValid\":%s,"
             "\"body\":%.2f,\"bodyValid\":%s,\"tempContact\":%s,"
             "\"tempContactSeconds\":%lu,\"tempRange\":%.2f,\"tempBase\":%.2f,"
             "\"ir\":%lu,\"red\":%lu,\"wave\":%.1f,\"rate\":%u,\"finger\":%s,"
             "\"ratio\":%.3f,\"redAmp\":%.1f,\"irAmp\":%.1f,\"samples\":%lu,"
             "\"accepted\":%lu,\"rejected\":%lu,\"settling\":%s,\"saturated\":%s,"
             "\"wifi\":\"%s\",\"clients\":%u,\"apIp\":\"%s\",\"time\":\"%s\","
             "\"date\":\"%s\",\"timeValid\":%s,\"epoch\":%ld}",
             bio.bpm,
             bio.spo2,
             temp.temperature_c,
             temp.valid ? "true" : "false",
             temp.body_temperature_c,
             temp.body_valid ? "true" : "false",
             temp.contact_detected ? "true" : "false",
             temp.contact_elapsed_ms / 1000UL,
             temp.stability_range_c,
             temp.baseline_temperature_c,
             static_cast<unsigned long>(bio.ir),
             static_cast<unsigned long>(bio.red),
             bio.ir_cardiogram,
             bio.samples_last_second,
             bio.finger_present ? "true" : "false",
             bio.spo2_ratio,
             bio.spo2_red_amp,
             bio.spo2_ir_amp,
             static_cast<unsigned long>(bio.sample_count),
             static_cast<unsigned long>(bio.accepted_pulses),
             static_cast<unsigned long>(bio.rejected_pulses),
             bio.settling ? "true" : "false",
             bio.saturated ? "true" : "false",
             web_api_status_text(),
             web_api_client_count(),
             web_api_ap_ip().c_str(),
             time_buf,
             date_buf,
             time_api_is_valid() ? "true" : "false",
             time_api_epoch());
    return String(json);
}

static void handle_root()
{
    mark_client_seen();
    s_server.send_P(200, "text/html", INDEX_HTML);
}

static void handle_snapshot()
{
    mark_client_seen();
    send_no_cache(200, "application/json", snapshot_json());
}

static void handle_time()
{
    mark_client_seen();
    if (!s_server.hasArg("epoch")) {
        send_no_cache(400, "application/json", "{\"ok\":false}");
        return;
    }
    bool ok = time_api_set_epoch(s_server.arg("epoch").toInt());
    send_no_cache(ok ? 200 : 400, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
}

static void register_routes()
{
    if (s_routes_registered) {
        return;
    }
    s_server.on("/", HTTP_GET, handle_root);
    s_server.on("/api/snapshot", HTTP_GET, handle_snapshot);
    s_server.on("/api/time", HTTP_POST, handle_time);
    s_server.onNotFound([]() {
        mark_client_seen();
        send_no_cache(404, "text/plain", "not found");
    });
    s_routes_registered = true;
}

void web_api_init()
{
    if (s_running) {
        return;
    }

    register_routes();
    WiFi.mode(WIFI_AP);
    WiFi.setSleep(false);
    if (WEB_AP_PASSWORD[0] != '\0') {
        WiFi.softAP(WEB_AP_SSID, WEB_AP_PASSWORD);
    } else {
        WiFi.softAP(WEB_AP_SSID);
    }
    delay(100);
    s_server.begin();
    s_running = true;
}

void web_api_update()
{
    if (!s_running) {
        return;
    }
    s_server.handleClient();
}

void web_api_stop()
{
    if (!s_running) {
        return;
    }

    s_server.stop();
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_OFF);
    s_last_client_ms = 0;
    s_running = false;
}

bool web_api_is_running()
{
    return s_running;
}

uint8_t web_api_client_count()
{
    return s_running ? WiFi.softAPgetStationNum() : 0;
}

const char *web_api_status_text()
{
    if (!s_running) {
        return "AP off";
    }
    return web_api_client_count() > 0 ? "phone connected" : "AP waiting";
}

String web_api_ap_ip()
{
    return s_running ? WiFi.softAPIP().toString() : String("--");
}
