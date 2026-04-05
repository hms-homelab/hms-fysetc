#include "web_server.h"
#include "config.h"
#include "sd_manager.h"
#include "wifi_manager.h"
#include "traffic_monitor.h"
#include "file_server.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static const char *TAG = "web";

// ---------------------------------------------------------------------------
// Ring buffer for captured log lines
// ---------------------------------------------------------------------------
#define LOG_RING_SIZE     (8 * 1024)
#define LOG_LINE_BUF      256

static char s_ring[LOG_RING_SIZE];
static size_t s_ring_head = 0;
static size_t s_ring_len  = 0;
static SemaphoreHandle_t s_ring_lock;

static vprintf_like_t s_orig_vprintf;
static httpd_handle_t s_server;

static int log_vprintf_hook(const char *fmt, va_list args)
{
    char buf[LOG_LINE_BUF];
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    if (n < 0) n = 0;
    if (n >= (int)sizeof(buf)) n = sizeof(buf) - 1;

    if (s_ring_lock && xSemaphoreTake(s_ring_lock, pdMS_TO_TICKS(5)) == pdTRUE) {
        for (int i = 0; i < n; i++) {
            s_ring[s_ring_head] = buf[i];
            s_ring_head = (s_ring_head + 1) % LOG_RING_SIZE;
            if (s_ring_len < LOG_RING_SIZE) {
                s_ring_len++;
            }
        }
        xSemaphoreGive(s_ring_lock);
    }

    return vprintf(fmt, args);
}

// ---------------------------------------------------------------------------
// HTML page
// ---------------------------------------------------------------------------
static const char INDEX_HTML[] =
"<!DOCTYPE html>"
"<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>FYSETC SD WiFi</title>"
"<style>"
"*{margin:0;padding:0;box-sizing:border-box}"
"body{background:#1a1a2e;color:#e0e0e0;font-family:'Courier New',monospace;font-size:13px}"
".hdr{background:#16213e;padding:12px 16px;display:flex;align-items:center;gap:16px;border-bottom:1px solid #0f3460}"
".hdr h1{font-size:16px;color:#e94560}"
".hdr .dot{width:10px;height:10px;border-radius:50%;display:inline-block}"
".dot.on{background:#0f0}.dot.off{background:#f00}"
".stats{display:flex;gap:20px;flex-wrap:wrap}"
".stats span{color:#a0a0a0}.stats b{color:#e0e0e0}"
".panel{background:#0d1117;padding:8px 12px;height:calc(100vh - 110px);overflow-y:auto;white-space:pre-wrap;word-break:break-all;line-height:1.5}"
".E{color:#f44}.W{color:#fa0}.I{color:#4f4}.D{color:#888}"
".tabs{display:flex;gap:0;background:#16213e;border-bottom:1px solid #0f3460}"
".tab{padding:8px 20px;cursor:pointer;color:#a0a0a0;border-bottom:2px solid transparent}"
".tab.active{color:#e94560;border-bottom:2px solid #e94560}"
".tab:hover{color:#e0e0e0}"
".hidden{display:none}"
".btn{background:#0f3460;color:#e0e0e0;border:1px solid #e94560;padding:6px 16px;cursor:pointer;font-family:inherit;font-size:13px;margin:8px}"
".btn:hover{background:#e94560;color:#fff}"
"</style></head><body>"
"<div class='hdr'>"
"<h1>hms-fysetc</h1>"
"<div class='stats'>"
"<span>WiFi: <span class='dot' id='wd'></span></span>"
"<span>Uptime: <b id='up'>--</b></span>"
"</div></div>"
"<div class='tabs'>"
"<div class='tab active' onclick='showTab(\"log\")'>Logs</div>"
"<div class='tab' onclick='showTab(\"pcnt\")'>PCNT</div>"
"</div>"
"<div id='log' class='panel'></div>"
"<div id='pcnt' class='panel hidden'>"
"<button class='btn' onclick='fetchPcnt()'>Refresh</button>"
"<button class='btn' onclick='startPcntPoll()'>Auto (2s)</button>"
"<button class='btn' onclick='stopPcntPoll()'>Stop</button>"
"<canvas id='chart' style='width:100%;height:calc(100vh - 180px);background:#0d1117'></canvas>"
"<div id='pcnt-info' style='color:#888;padding:4px 8px'></div>"
"</div>"
"<script>"
"function showTab(t){"
"document.querySelectorAll('.tab').forEach(e=>e.classList.remove('active'));"
"document.querySelectorAll('.panel').forEach(e=>e.classList.add('hidden'));"
"document.getElementById(t).classList.remove('hidden');"
"event.target.classList.add('active');}"
"let pcntTimer=null;"
"function drawChart(samples,ms){"
"var c=document.getElementById('chart'),ctx=c.getContext('2d');"
"c.width=c.offsetWidth;c.height=c.offsetHeight;"
"var w=c.width,h=c.height;"
"ctx.fillStyle='#0d1117';ctx.fillRect(0,0,w,h);"
"if(!samples.length)return;"
"var mx=Math.max.apply(null,samples);"
"if(mx<1)mx=1;"
"var barW=w/samples.length,i,v,bh;"
"ctx.strokeStyle='#1a2233';ctx.lineWidth=1;"
"for(i=0;i<5;i++){var y=h-(i/4)*h;ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(w,y);ctx.stroke();}"
"ctx.fillStyle='#555';ctx.font='11px monospace';"
"for(i=0;i<=4;i++){ctx.fillText(Math.round(mx*i/4),4,h-(i/4)*h-4);}"
"for(i=0;i<samples.length;i++){"
"v=samples[i];bh=(v/mx)*h;"
"ctx.fillStyle=v>0?'#e94560':'#1a2233';"
"ctx.fillRect(i*barW,h-bh,Math.max(barW-0.5,1),bh);}"
"var total=0,active=0;"
"for(i=0;i<samples.length;i++){total+=samples[i];if(samples[i]>0)active++;}"
"document.getElementById('pcnt-info').textContent="
"'Pulses: '+total+' | Active: '+active+'/'+samples.length+"
"' ('+Math.round(active/samples.length*100)+'%) | Peak: '+mx;}"
"async function fetchPcnt(){"
"try{let r=await fetch('/api/pcnt');let d=await r.json();"
"drawChart(d.samples,d.sample_ms);}catch(e){}}"
"function startPcntPoll(){stopPcntPoll();fetchPcnt();pcntTimer=setInterval(fetchPcnt,2000);}"
"function stopPcntPoll(){if(pcntTimer){clearInterval(pcntTimer);pcntTimer=null;}}"
"let cursor=0,el=document.getElementById('log');"
"function colorize(t){"
"return t.replace(/^(.*?)(E \\(.*)/gm,'$1<span class=E>$2</span>')"
".replace(/^(.*?)(W \\(.*)/gm,'$1<span class=W>$2</span>')"
".replace(/^(.*?)(I \\(.*)/gm,'$1<span class=I>$2</span>')"
".replace(/^(.*?)(D \\(.*)/gm,'$1<span class=D>$2</span>');}"
"async function poll(){"
"try{"
"let r=await fetch('/api/logs?cursor='+cursor);"
"let j=await r.json();"
"if(j.text.length>0){el.innerHTML+=colorize(j.text);cursor=j.cursor;"
"el.scrollTop=el.scrollHeight;}"
"let s=await fetch('/api/status');let d=await s.json();"
"document.getElementById('wd').className='dot '+(d.wifi?'on':'off');"
"document.getElementById('up').textContent=d.uptime;"
"}catch(e){}"
"setTimeout(poll,1000);}"
"poll();"
"</script></body></html>";

// ---------------------------------------------------------------------------
// HTTP handlers
// ---------------------------------------------------------------------------
static esp_err_t handle_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, sizeof(INDEX_HTML) - 1);
}

static esp_err_t handle_api_logs(httpd_req_t *req)
{
    char qbuf[32] = {0};
    size_t cursor = 0;
    if (httpd_req_get_url_query_str(req, qbuf, sizeof(qbuf)) == ESP_OK) {
        char val[16];
        if (httpd_query_key_value(qbuf, "cursor", val, sizeof(val)) == ESP_OK) {
            cursor = (size_t)atoi(val);
        }
    }

    httpd_resp_set_type(req, "application/json");

    if (xSemaphoreTake(s_ring_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        httpd_resp_sendstr(req, "{\"text\":\"\",\"cursor\":0}");
        return ESP_OK;
    }

    size_t new_cursor = s_ring_len;

    if (cursor >= s_ring_len) {
        xSemaphoreGive(s_ring_lock);
        char resp[64];
        snprintf(resp, sizeof(resp), "{\"text\":\"\",\"cursor\":%u}", (unsigned)new_cursor);
        return httpd_resp_sendstr(req, resp);
    }

    size_t send_len = s_ring_len - cursor;
    if (send_len > LOG_RING_SIZE) send_len = LOG_RING_SIZE;

    size_t send_start;
    if (s_ring_len <= LOG_RING_SIZE) {
        send_start = cursor;
    } else {
        send_start = (s_ring_head + LOG_RING_SIZE - s_ring_len + cursor) % LOG_RING_SIZE;
    }

    httpd_resp_sendstr_chunk(req, "{\"text\":\"");

    size_t pos = send_start;
    size_t remaining = send_len;
    char esc_buf[512];
    while (remaining > 0) {
        size_t esc_i = 0;
        while (remaining > 0 && esc_i < sizeof(esc_buf) - 8) {
            char c = s_ring[pos % LOG_RING_SIZE];
            pos = (pos + 1) % LOG_RING_SIZE;
            remaining--;
            if (c == '"') { esc_buf[esc_i++] = '\\'; esc_buf[esc_i++] = '"'; }
            else if (c == '\\') { esc_buf[esc_i++] = '\\'; esc_buf[esc_i++] = '\\'; }
            else if (c == '\n') { esc_buf[esc_i++] = '\\'; esc_buf[esc_i++] = 'n'; }
            else if (c == '\r') { /* skip */ }
            else if (c == '\t') { esc_buf[esc_i++] = ' '; esc_buf[esc_i++] = ' '; }
            else if ((unsigned char)c >= 0x20) { esc_buf[esc_i++] = c; }
        }
        esc_buf[esc_i] = '\0';
        httpd_resp_sendstr_chunk(req, esc_buf);
    }

    xSemaphoreGive(s_ring_lock);

    char footer[64];
    snprintf(footer, sizeof(footer), "\",\"cursor\":%u}", (unsigned)new_cursor);
    httpd_resp_sendstr_chunk(req, footer);
    httpd_resp_sendstr_chunk(req, NULL);

    return ESP_OK;
}

static esp_err_t handle_api_pcnt(httpd_req_t *req)
{
    int count = 0, idx = 0;
    const uint16_t *hist = traffic_monitor_get_history(&count, &idx);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\"sample_ms\":100,\"samples\":[");

    char buf[16];
    for (int i = 0; i < count; i++) {
        int pos = (idx + i) % count;
        if (i > 0) httpd_resp_sendstr_chunk(req, ",");
        snprintf(buf, sizeof(buf), "%u", hist[pos]);
        httpd_resp_sendstr_chunk(req, buf);
    }

    httpd_resp_sendstr_chunk(req, "]}");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
void web_server_start(void)
{
    s_ring_lock = xSemaphoreCreateMutex();

    s_orig_vprintf = esp_log_set_vprintf(log_vprintf_hook);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.stack_size = 6144;

    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start web server");
        return;
    }

    httpd_uri_t uri_index = {
        .uri = "/", .method = HTTP_GET, .handler = handle_index
    };
    httpd_uri_t uri_logs = {
        .uri = "/api/logs", .method = HTTP_GET, .handler = handle_api_logs
    };
    httpd_uri_t uri_pcnt = {
        .uri = "/api/pcnt", .method = HTTP_GET, .handler = handle_api_pcnt
    };
    httpd_register_uri_handler(s_server, &uri_index);
    httpd_register_uri_handler(s_server, &uri_logs);
    httpd_register_uri_handler(s_server, &uri_pcnt);

    file_server_register_endpoints(s_server);

    ESP_LOGI(TAG, "Web server started on port 80");
}

void web_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
    }
    if (s_orig_vprintf) {
        esp_log_set_vprintf(s_orig_vprintf);
        s_orig_vprintf = NULL;
    }
}
