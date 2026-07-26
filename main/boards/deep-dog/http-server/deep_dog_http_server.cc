#include "config.h"
#include "http-server/deep_dog_http_server.h"

#if DEEP_DOG_DOG_ENABLE
#include "dog/dog_control.h"
#include "motor/deep_motor.h"
#endif
#include "esp_video.h"

#include "camera.h"
#include "face_ai_bridge.h"
#include "face_ai_config.h"
#include "immich_client.h"
#include "vision/vision_config.h"
#include "vision/vision_frame_hub.h"
#include "vision/vision_types.h"

#include <wifi_manager.h>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <string>
#include <strings.h>
#include <freertos/FreeRTOS.h>
#include <freertos/idf_additions.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#if DEEP_DOG_HTTP_SERVER_ENABLE

#define TAG "dog_http"

/** 并发拉 /stream 的路数（每路在独立任务里跑，不阻塞 httpd） */
#define MJPEG_STREAM_QUEUE_DEPTH 2
#define MJPEG_STREAM_WORKER_COUNT 2
#define MJPEG_STREAM_TASK_STACK 10240
#define MJPEG_STREAM_TASK_PRIO 3

#define PART_BOUNDARY "deepdogmjpegboundary"
static const char* kStreamContentType = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* kStreamBoundary = "\r\n--" PART_BOUNDARY "\r\n";
static const char* kStreamPartHdr = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

namespace {

enum class DogWebCmd : uint8_t {
    Init = 1,
    Forward = 2,
    Back = 3,
    Stand = 4,
    LieDown = 5,
    Dance = 6,
    StopWalk = 7,
    Disable = 8,
};

static const char* ModeToStr(DeepDogCaptureMode m) {
    switch (m) {
        case DeepDogCaptureMode::Off:
            return "off";
        case DeepDogCaptureMode::PeriodicSample:
            return "periodic";
        case DeepDogCaptureMode::Streaming:
            return "stream";
        case DeepDogCaptureMode::RtspPush:
            return "rtsp_push";
        default:
            return "unknown";
    }
}

static const char* DogWebCmdStr(DogWebCmd c) {
    switch (c) {
        case DogWebCmd::Init:
            return "init";
        case DogWebCmd::Forward:
            return "forward";
        case DogWebCmd::Back:
            return "back";
        case DogWebCmd::Stand:
            return "stand";
        case DogWebCmd::LieDown:
            return "liedown";
        case DogWebCmd::Dance:
            return "dance";
        case DogWebCmd::StopWalk:
            return "stop_walk";
        case DogWebCmd::Disable:
            return "disable";
        default:
            return "?";
    }
}

static bool StrToMode(const char* s, DeepDogCaptureMode* out) {
    if (!s || !out) {
        return false;
    }
    if (strcmp(s, "off") == 0) {
        *out = DeepDogCaptureMode::Off;
        return true;
    }
    if (strcmp(s, "periodic") == 0) {
        *out = DeepDogCaptureMode::PeriodicSample;
        return true;
    }
    if (strcmp(s, "stream") == 0 || strcmp(s, "streaming") == 0 || strcmp(s, "mjpeg") == 0) {
        *out = DeepDogCaptureMode::Streaming;
        return true;
    }
    if (strcmp(s, "rtsp_push") == 0 || strcmp(s, "rtsp") == 0 || strcmp(s, "push") == 0) {
        *out = DeepDogCaptureMode::RtspPush;
        return true;
    }
    return false;
}

static esp_err_t SendCorsJson(httpd_req_t* req, const char* json) {
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    // 页面端高频轮询状态接口，显式禁止缓存，避免浏览器/中间层返回旧 JSON。
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    httpd_resp_set_hdr(req, "Pragma", "no-cache");
    httpd_resp_set_hdr(req, "Expires", "0");
    return httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

/** esp_http_server 要求读完 POST body，否则套接字上残留数据会导致后续请求解析失败（表现为按钮偶发全部失效）。 */
static esp_err_t DrainPostBody(httpd_req_t* req) {
    size_t remaining = req->content_len;
    if (remaining == 0) {
        return ESP_OK;
    }
    char buf[128];
    while (remaining > 0) {
        const size_t chunk = remaining > sizeof(buf) ? sizeof(buf) : remaining;
        const int ret = httpd_req_recv(req, buf, chunk);
        if (ret < 0) {
            return ESP_FAIL;
        }
        if (ret == 0) {
            break;
        }
        remaining -= (size_t)ret;
    }
    return ESP_OK;
}

static esp_err_t RootHandler(httpd_req_t* req) {
    static const char kPage[] =
        "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width'>"
        "<title>DeepDog</title><style>"
        "body{font-family:system-ui;margin:12px;background:#111;color:#eee;}"
        "button{margin:4px;padding:10px 14px;border-radius:8px;border:0;background:#2a6df4;color:#fff;}"
        "button.secondary{background:#444;} #m{margin-top:12px;max-width:100%;border-radius:8px;background:#000;}"
        ".row{display:flex;flex-wrap:wrap;gap:6px;align-items:center;} #st{font-size:14px;color:#9cf;margin:8px 0;}"
        "#dogMeta{font-size:12px;color:#9cf;margin:8px 0 4px 0;}"
        "#dogTbl{border-collapse:collapse;width:100%;max-width:640px;font-size:12px;color:#ddd;}"
        "#dogTbl th,#dogTbl td{border:1px solid #333;padding:4px 6px;text-align:right;}"
        "#dogTbl th:first-child,#dogTbl td:first-child{text-align:center;}"
        "#dogTbl tr.fault td{background:#3a1f1f;color:#ffd2d2;}"
        "#dogTbl td.tauWarn{background:#3a3215;color:#ffe28a;}"
        "#dogTbl td.tauDanger{background:#4a1b1b;color:#ffb3b3;}"
        "#dogTbl tr.changed td{box-shadow:inset 0 0 0 9999px rgba(64,128,255,0.14);}"
        "</style></head><body>"
        "<h2>DeepDog 遥控</h2>"
        "<p style='color:#888;font-size:13px;margin:0 0 10px 0'>"
        "人脸检测默认同开。视频：局域网 MJPEG，或推 MediaMTX（内外网用 HLS 看）。关推流不影响人脸。"
        "</p>"
        "<div class='row'><span>视频发布:</span>"
        "<button class='secondary' onclick=\"setMode('off')\">关闭推流</button>"
        "<button onclick=\"setMode('stream')\">局域网 MJPEG</button>"
        "<button onclick=\"setMode('rtsp_push')\">推 MediaMTX</button></div>"
        "<p id='st'></p>"
        "<p id='playLinks' style='font-size:12px;color:#9cf;margin:4px 0;word-break:break-all'></p>"
        "<div class='row' style='margin-top:8px'><label style='color:#ccc;font-size:14px'>"
        "<input type='checkbox' id='faceEn' checked onchange='toggleFace(this.checked)'/> 人脸检测/识别（静默可开，不必开视频）</label></div>"
        "<p id='faceMeta' style='font-size:12px;color:#9cf;margin:4px 0'></p>"
        "<div style='position:relative;display:none;max-width:100%;width:640px;aspect-ratio:4/3' id='vidWrap'>"
        "<img id='m' width='640' height='480' alt='mjpeg' style='display:block;width:100%;height:100%;object-fit:contain'/>"
        "<canvas id='fc' width='640' height='480' style='position:absolute;left:0;top:0;width:100%;height:100%;pointer-events:none'></canvas>"
        "</div>"
        "<div class='row'>"
        "<button id='btnInit' onclick=\"cmd('init')\">初始化</button>"
        "<button id='btnForward' onclick=\"cmd('forward')\">前进</button>"
        "<button id='btnBack' onclick=\"cmd('back')\">后退</button>"
        "<button id='btnStand' onclick=\"cmd('stand')\">站立</button>"
        "<button id='btnLie' onclick=\"cmd('liedown')\">卧倒</button>"
        "<button id='btnDance' onclick=\"cmd('dance')\">跳舞</button>"
        "<button id='btnStop' class='secondary' onclick=\"cmd('stop_walk')\">停走</button>"
        "<button id='btnDisable' class='secondary' onclick=\"cmd('disable')\">失能</button>"
        "</div>"
        "<div id='dogMeta'></div>"
        "<table id='dogTbl'>"
        "<thead><tr><th>ID</th><th>角度(°)</th><th>扭矩(N·m)</th><th>|τ|max(N·m)</th><th>碰撞</th></tr></thead>"
        "<tbody id='dogTblBody'></tbody></table>"
        "<script>"
        "const st=document.getElementById('st');const wrap=document.getElementById('vidWrap');"
        "const playLinks=document.getElementById('playLinks');"
        "const img=document.getElementById('m');const cv=document.getElementById('fc');const faceMeta=document.getElementById('faceMeta');"
        "const dogMeta=document.getElementById('dogMeta');const dogTblBody=document.getElementById('dogTblBody');"
        "let facePoll=null;let lastStatus=null;let cmdInFlight=false;let lastCmdTs=0;"
        "let statusInFlight=false;let dogInFlight=false;let faceInFlight=false;"
        "let lastDegById={};"
        "function syncVidSize(w,h){if(!w||!h)return;if(cv.width!==w||cv.height!==h){cv.width=w;cv.height=h;}"
        "wrap.style.width=w+'px';wrap.style.aspectRatio=w+'/'+h;img.width=w;img.height=h;}"
        "function drawFaces(j){const w=(j&&j.w)||cv.width||640;const h=(j&&j.h)||cv.height||480;"
        "syncVidSize(w,h);const c=cv.getContext('2d');c.clearRect(0,0,w,h);"
        "if(!j||!j.faces)return;(j.faces||[]).forEach(f=>{"
        "const hasId=!!f.local_id;const real=f.display_name&&f.display_name.length&&f.display_name.charAt(0)!=='#';"
        "c.strokeStyle=real?'#6cf':(hasId?'#0f0':'#8a8');c.lineWidth=2;"
        "c.strokeRect(f.x0*w,f.y0*h,(f.x1-f.x0)*w,(f.y1-f.y0)*h);"
        "let label='';"
        "if(real){label=f.display_name+(f.local_id?(' #'+f.local_id):'');}"
        "else if(f.display_name&&f.display_name.length){label=f.display_name;}"
        "else if(f.local_id){label='#'+f.local_id;}"
        "if(label){c.fillStyle=c.strokeStyle;c.font='bold 13px sans-serif';"
        "c.fillText(label,f.x0*w+2,Math.max(14,f.y0*h-4));}"
        "});}"
        "function pollFace(){if(faceInFlight)return;faceInFlight=true;fetch('/api/face?ts='+Date.now(),{cache:'no-store'}).then(r=>r.json()).then(j=>{"
        "if(!j.enabled){faceMeta.textContent='人脸检测未编译';drawFaces(null);return;}"
        "const names=(j.faces||[]).filter(f=>f.local_id||(f.display_name&&f.display_name.length))"
        ".map(f=>{const real=f.display_name&&f.display_name.charAt(0)!=='#';"
        "return real?(f.display_name+'(#'+f.local_id+')'):(f.display_name||('#'+f.local_id));}).join(', ');"
        "const primary=j.display_name?(j.display_name+(j.local_id?('/'+j.recognize_source):'')):'';"
        "faceMeta.textContent='人脸:'+(j.feature_on?'开':'关')+' has_face:'+j.has_face+' n:'+j.n"
        "+' 帧:'+j.w+'x'+j.h+(names?(' | '+names):'')+(primary&&!names.includes(j.display_name)?(' | 主:'+primary):'');"
        "if(j.feature_on)drawFaces(j);else drawFaces(null);}).catch(()=>{}).finally(()=>{faceInFlight=false;});}"
        "function toggleFace(on){fetch('/api/face_enable?enabled='+(on?'1':'0'),{method:'POST'})"
        ".then(()=>pollFace()).catch(()=>{});if(on&&!facePoll){facePoll=setInterval(pollFace,500);pollFace();}"
        "else if(!on&&facePoll){clearInterval(facePoll);facePoll=null;drawFaces(null);faceMeta.textContent='';}}"
        "function applyDogInitState(j){"
        "const inited=!!j.dog_initialized;"
        "const bi=document.getElementById('btnInit');"
        "const bs=['btnForward','btnBack','btnStand','btnLie','btnDance','btnStop','btnDisable'].map(id=>document.getElementById(id));"
        "if(bi)bi.disabled=inited;"
        "bs.forEach(b=>{if(b)b.disabled=!inited;});"
        "}"
        "function refresh(){if(statusInFlight)return Promise.resolve();statusInFlight=true;return fetch('/api/status?ts='+Date.now(),{cache:'no-store'}).then(r=>r.json()).then(j=>{"
        "lastStatus=j;"
        "st.textContent='发布:'+j.mode+' 拉流:'+j.stream_clients+' JPEG:'+(j.has_jpeg?'有':'无')"
        "+(j.push_status?(' 推流:'+j.push_status):'')"
        "+(j.face_ai_compiled?' 人脸模块:有':' 人脸模块:无')"
        "+' 狗初始化:'+((j.dog_initialized)?'已完成':'未完成');"
        "if(j.mode==='rtsp_push'&&(j.play_url||j.lan_play_url)){"
        "playLinks.innerHTML='外网 HLS: <a href=\"'+(j.play_url||'')+'\" target=_blank rel=noopener>'+(j.play_url||'')+'</a><br/>'"
        "+'内网 HLS: <a href=\"'+(j.lan_play_url||'')+'\" target=_blank rel=noopener>'+(j.lan_play_url||'')+'</a><br/>'"
        "+'设备推流: '+(j.push_url||'');}"
        "else if(j.mode==='stream'){playLinks.textContent='本页下方 MJPEG 预览（仅局域网）';}"
        "else{playLinks.textContent='';}"
        "applyDogInitState(j);"
        "if(j.mode==='stream'){wrap.style.display='block';if(!img.src||img.src.indexOf('/stream')<0)img.src='/stream';}"
        "else{wrap.style.display='none';img.removeAttribute('src');}"
        "if(document.getElementById('faceEn').checked){if(!facePoll){facePoll=setInterval(pollFace,500);pollFace();}}"
        "}).catch(()=>{st.textContent='状态获取失败';}).finally(()=>{statusInFlight=false;});}"
        "function setMode(m){fetch('/api/vision_publish?mode='+encodeURIComponent(m),{method:'POST'})"
        ".then(r=>{if(!r.ok)throw new Error('HTTP '+r.status);return refresh();})"
        ".catch(e=>{st.textContent='切换发布失败: '+e.message;});}"
        "function fmtNum(v,d){return (v==null||Number.isNaN(v))?'?':Number(v).toFixed(d);}"
        "function updateDogStatus(){if(dogInFlight)return Promise.resolve();dogInFlight=true;const t0=Date.now();return fetch('/api/dog_status?ts='+t0,{cache:'no-store'}).then(r=>r.json()).then(j=>{"
        "if(!j.motors){dogMeta.textContent='电机状态: 无';dogTblBody.innerHTML='';return;}"
        "const now=new Date();const hh=String(now.getHours()).padStart(2,'0');"
        "const mm=String(now.getMinutes()).padStart(2,'0');const ss=String(now.getSeconds()).padStart(2,'0');"
        "dogMeta.textContent='更新: '+hh+':'+mm+':'+ss+'  延迟: '+(Date.now()-t0)+' ms  limit='+(j.torque_limit_nm!=null?fmtNum(j.torque_limit_nm,2):'-')+' N·m  fault='+(j.has_fault?'是':'否');"
        "const limit=(j.torque_limit_nm!=null)?Number(j.torque_limit_nm):null;"
        "const nextDegById={};"
        "const rows=(j.motors||[]).slice().sort((a,b)=>(a.id||0)-(b.id||0)).map(m=>{"
        "const id=Number(m.id||0);const degNum=Number(m.deg);const tauAbs=Math.abs(Number(m.torque_nm||0));"
        "nextDegById[id]=degNum;"
        "const prevDeg=lastDegById[id];"
        "const changed=(Number.isFinite(prevDeg)&&Number.isFinite(degNum)&&Math.abs(degNum-prevDeg)>=0.3);"
        "let tauCls='';"
        "if(limit!=null&&limit>0){if(tauAbs>=limit)tauCls='tauDanger';else if(tauAbs>=limit*0.7)tauCls='tauWarn';}"
        "else{if(tauAbs>=2.0)tauCls='tauDanger';else if(tauAbs>=1.0)tauCls='tauWarn';}"
        "const col=(m.collision?'是':'否');"
        "const rowCls=(m.collision?'fault':(changed?'changed':''));"
        "return '<tr'+(rowCls?' class=\"'+rowCls+'\"':'')+'><td>'+id+'</td><td>'+fmtNum(m.deg,1)+'</td><td class=\"'+tauCls+'\">'+fmtNum(m.torque_nm,3)+'</td><td>'+fmtNum(m.max_abs_torque_nm,3)+'</td><td>'+col+'</td></tr>';"
        "});"
        "lastDegById=nextDegById;"
        "dogTblBody.innerHTML=rows.join('');"
        "}).catch(()=>{dogMeta.textContent='电机状态获取失败';}).finally(()=>{dogInFlight=false;});}"
        "function setCmdButtonsDisabled(dis){['btnInit','btnForward','btnBack','btnStand','btnLie','btnDance','btnStop','btnDisable']"
        ".forEach(id=>{const b=document.getElementById(id);if(b)b.disabled=!!dis;});}"
        "function cmd(c){const now=Date.now();"
        "if(cmdInFlight||now-lastCmdTs<280){st.textContent='操作过快，请稍候...';return;}"
        "lastCmdTs=now;cmdInFlight=true;setCmdButtonsDisabled(true);"
        "fetch('/api/cmd?cmd='+encodeURIComponent(c),{method:'POST'})"
        ".then(r=>r.json().catch(()=>({})).then(j=>{"
        "if(!r.ok||j.ok===false){st.textContent='指令失败: '+(j.error||('HTTP '+r.status));}"
        "else{st.textContent='指令已发送: '+c;}"
        "return Promise.all([refresh(),updateDogStatus()]);"
        "})).catch(e=>{st.textContent='指令失败: '+e.message;})"
        ".finally(()=>{setTimeout(()=>{cmdInFlight=false;refresh();},220);});}"
        "function scheduleRefresh(){refresh().finally(()=>setTimeout(scheduleRefresh,1000));}"
        "function scheduleDogStatus(){updateDogStatus().finally(()=>setTimeout(scheduleDogStatus,700));}"
        "if(document.getElementById('faceEn').checked){facePoll=setInterval(pollFace,500);pollFace();}"
        "refresh();updateDogStatus();setTimeout(scheduleRefresh,1000);setTimeout(scheduleDogStatus,700);"
        "</script></body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, kPage, HTTPD_RESP_USE_STRLEN);
}

/**
 * MJPEG 长循环：须在独立任务中运行（由 httpd_req_async 投递），否则会一直占用 httpd 线程，
 * /api/cmd、/api/capture_mode 等无法被 select 处理。
 */
static esp_err_t RunMjpegStream(httpd_req_t* req) {
    auto* srv = static_cast<DeepDogHttpServer*>(req->user_ctx);
    if (!srv || !srv->camera()) {
        return ESP_FAIL;
    }
    srv->IncStreamClient();

    esp_err_t res = httpd_resp_set_type(req, kStreamContentType);
    if (res != ESP_OK) {
        srv->DecStreamClient();
        return res;
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");

    char part_hdr[80];
    while (true) {
        std::vector<uint8_t> jpeg;
        if (!srv->CopyLatestJpeg(&jpeg) || jpeg.empty()) {
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }

        if (httpd_resp_send_chunk(req, kStreamBoundary, strlen(kStreamBoundary)) != ESP_OK) {
            res = ESP_FAIL;
            break;
        }
        int hlen = snprintf(part_hdr, sizeof(part_hdr), kStreamPartHdr, (unsigned)jpeg.size());
        if (httpd_resp_send_chunk(req, part_hdr, (size_t)hlen) != ESP_OK) {
            res = ESP_FAIL;
            break;
        }
        if (httpd_resp_send_chunk(req, reinterpret_cast<const char*>(jpeg.data()), jpeg.size()) != ESP_OK) {
            res = ESP_FAIL;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(60));
    }

    httpd_resp_send_chunk(req, nullptr, 0);
    srv->DecStreamClient();
    return res;
}

static esp_err_t StreamHandler(httpd_req_t* req) {
    auto* srv = static_cast<DeepDogHttpServer*>(req->user_ctx);
    if (!srv) {
        return ESP_FAIL;
    }
    if (srv->GetCaptureMode() != DeepDogCaptureMode::Streaming) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        httpd_resp_set_type(req, "text/plain; charset=utf-8");
        httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
        return httpd_resp_send(req, "MJPEG disabled (publish mode is not stream)", HTTPD_RESP_USE_STRLEN);
    }
    httpd_req_t* async_req = nullptr;
    if (httpd_req_async_handler_begin(req, &async_req) != ESP_OK) {
        ESP_LOGW(TAG, "/stream async_handler_begin 失败");
        return ESP_FAIL;
    }
    if (!srv->EnqueueMjpegStreamJob(async_req)) {
        ESP_LOGW(TAG, "/stream 队列满，拒绝并发拉流");
        httpd_resp_set_status(async_req, "503 Service Unavailable");
        httpd_resp_set_type(async_req, "text/plain; charset=utf-8");
        httpd_resp_set_hdr(async_req, "Access-Control-Allow-Origin", "*");
        httpd_resp_send(async_req, "MJPEG busy (max concurrent streams)", HTTPD_RESP_USE_STRLEN);
        httpd_req_async_handler_complete(async_req);
    }
    return ESP_OK;
}

static esp_err_t ApiStatusHandler(httpd_req_t* req) {
    auto* srv = static_cast<DeepDogHttpServer*>(req->user_ctx);
    if (!srv) {
        return ESP_FAIL;
    }
    bool dog_initialized = false;
#if DEEP_DOG_DOG_ENABLE
    const DogControl* dog = srv->dog();
    if (dog) {
        dog_initialized = (dog->getPoseState() != DogPoseState::Uninitialized);
    }
#endif
    const char* push_status = "idle";
    char push_url[128] = "";
    if (srv->vision_hub()) {
        push_status = VisionPushStatusStr(srv->vision_hub()->GetPushStatus());
        const std::string u = srv->vision_hub()->RtspUrl();
        snprintf(push_url, sizeof(push_url), "%s", u.c_str());
    }
    char buf[768];
    snprintf(buf, sizeof(buf),
             "{\"mode\":\"%s\",\"publish\":\"%s\",\"stream_clients\":%d,\"has_jpeg\":%s,\"port\":%u,"
             "\"push_status\":\"%s\",\"push_url\":\"%s\","
             "\"play_url\":\"%s\",\"lan_play_url\":\"%s\","
             "\"face_ai_compiled\":%s,\"dog_initialized\":%s}",
             ModeToStr(srv->GetCaptureMode()), ModeToStr(srv->GetCaptureMode()), srv->StreamClientCount(),
             srv->HasJpegFrame() ? "true" : "false", (unsigned)srv->Port(), push_status, push_url,
             DEEP_DOG_VISION_PUBLIC_PLAY_URL, DEEP_DOG_VISION_LAN_PLAY_URL,
#if DEEP_DOG_FACE_AI_ENABLE
             "true",
#else
             "false",
#endif
             dog_initialized ? "true" : "false");
    return SendCorsJson(req, buf);
}

static esp_err_t ApiModeHandler(httpd_req_t* req) {
    auto* srv = static_cast<DeepDogHttpServer*>(req->user_ctx);
    if (!srv) {
        return ESP_FAIL;
    }
    if (req->method != HTTP_POST) {
        httpd_resp_set_status(req, "405 Method Not Allowed");
        return httpd_resp_send(req, nullptr, 0);
    }
    if (DrainPostBody(req) != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, nullptr, 0);
    }
    char query[96];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, R"({"error":"missing query"})", HTTPD_RESP_USE_STRLEN);
    }
    char val[24];
    if (httpd_query_key_value(query, "mode", val, sizeof(val)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, R"({"error":"need mode=off|periodic|stream|rtsp_push"})", HTTPD_RESP_USE_STRLEN);
    }
    DeepDogCaptureMode m = DeepDogCaptureMode::Off;
    if (!StrToMode(val, &m)) {
        ESP_LOGW(TAG, "网页 采集模式无效: %s", val);
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, R"({"error":"bad mode"})", HTTPD_RESP_USE_STRLEN);
    }
    srv->SetCaptureMode(m);
    char buf[80];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"mode\":\"%s\"}", ModeToStr(m));
    return SendCorsJson(req, buf);
}

static esp_err_t ApiVisionPublishHandler(httpd_req_t* req) {
    // 与 /api/capture_mode 同源；C03 MQTT 也将映射到同一状态机
    return ApiModeHandler(req);
}

static esp_err_t ApiCmdHandler(httpd_req_t* req) {
    auto* srv = static_cast<DeepDogHttpServer*>(req->user_ctx);
    if (!srv) {
        return ESP_FAIL;
    }
#if !DEEP_DOG_DOG_ENABLE
    (void)srv;
    httpd_resp_set_status(req, "503 Service Unavailable");
    return SendCorsJson(req, R"({"ok":false,"error":"dog disabled"})");
#else
    if (req->method != HTTP_POST) {
        httpd_resp_set_status(req, "405 Method Not Allowed");
        return httpd_resp_send(req, nullptr, 0);
    }
    if (DrainPostBody(req) != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, nullptr, 0);
    }
    char query[128];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, R"({"error":"missing query"})", HTTPD_RESP_USE_STRLEN);
    }
    char val[32];
    if (httpd_query_key_value(query, "cmd", val, sizeof(val)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, R"({"error":"need cmd"})", HTTPD_RESP_USE_STRLEN);
    }

    DogWebCmd cmd = DogWebCmd::Init;
    if (strcmp(val, "init") == 0) {
        cmd = DogWebCmd::Init;
    } else if (strcmp(val, "forward") == 0) {
        cmd = DogWebCmd::Forward;
    } else if (strcmp(val, "back") == 0) {
        cmd = DogWebCmd::Back;
    } else if (strcmp(val, "stand") == 0) {
        cmd = DogWebCmd::Stand;
    } else if (strcmp(val, "liedown") == 0 || strcmp(val, "lie_down") == 0) {
        cmd = DogWebCmd::LieDown;
    } else if (strcmp(val, "dance") == 0) {
        cmd = DogWebCmd::Dance;
    } else if (strcmp(val, "stop_walk") == 0) {
        cmd = DogWebCmd::StopWalk;
    } else if (strcmp(val, "disable") == 0) {
        cmd = DogWebCmd::Disable;
    } else {
        ESP_LOGW(TAG, "网页 狗指令未知: %s", val);
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, R"({"error":"unknown cmd"})", HTTPD_RESP_USE_STRLEN);
    }

    uint8_t c = static_cast<uint8_t>(cmd);
    if (!srv->TryEnqueueDogCmd(c)) {
        ESP_LOGW(TAG, "网页 狗指令未入队(队列满): %s", val);
        httpd_resp_set_status(req, "503 Busy");
        return SendCorsJson(req, R"({"ok":false,"error":"queue full"})");
    }
    ESP_LOGI(TAG, "网页 狗指令已入队: %s", val);
    return SendCorsJson(req, R"({"ok":true})");
#endif  // DEEP_DOG_DOG_ENABLE
}

static esp_err_t ApiDogStatusHandler(httpd_req_t* req) {
    auto* srv = static_cast<DeepDogHttpServer*>(req->user_ctx);
    if (!srv) {
        return ESP_FAIL;
    }
#if !DEEP_DOG_DOG_ENABLE
    (void)srv;
    return SendCorsJson(req, R"({"motors":[],"torque_limit_nm":null,"has_fault":false})");
#else
    DogControl* dog = srv->dog();
    if (!dog) {
        return SendCorsJson(req, R"({"motors":[],"torque_limit_nm":null,"has_fault":false})");
    }
    DeepMotor* motor = dog->getDeepMotor();
    if (!motor) {
        return SendCorsJson(req, R"({"motors":[],"torque_limit_nm":null,"has_fault":false})");
    }

    constexpr size_t kCap = 4096;
    char* buf = static_cast<char*>(heap_caps_malloc(kCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buf) {
        buf = static_cast<char*>(heap_caps_malloc(kCap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (!buf) {
        return SendCorsJson(req, R"({"error":"no_mem"})");
    }

    int8_t ids[MAX_MOTOR_COUNT];
    uint8_t count = motor->getRegisteredMotorIds(ids, MAX_MOTOR_COUNT);
    size_t off = 0;
    bool has_fault = false;

    off += snprintf(buf + off, kCap - off, "{\"motors\":[");
    for (uint8_t i = 0; i < count && off < kCap - 128; ++i) {
        const uint8_t id = (uint8_t)ids[i];
        motor_status_t st{};
        if (!motor->getMotorStatus(id, &st) || !st.has_feedback) {
            continue;
        }
        const float deg = st.current_angle * 180.0f / (float)M_PI;
        if (st.error_status || st.collision) {
            has_fault = true;
        }
        off += snprintf(buf + off, kCap - off,
                        "%s{\"id\":%u,\"rad\":%.6f,\"deg\":%.2f,\"torque_nm\":%.3f,"
                        "\"max_abs_torque_nm\":%.3f,\"collision\":%s}",
                        (off > 11 ? "," : ""), (unsigned)id,
                        (double)st.current_angle, (double)deg,
                        (double)st.current_torque,
                        (double)st.max_abs_torque,
                        st.collision ? "true" : "false");
    }
    if (off >= kCap - 64) {
        heap_caps_free(buf);
        return SendCorsJson(req, R"({"error":"dog_status_too_large"})");
    }
    const double torque_limit = (DEEP_DOG_TORQUE_LIMIT_NM > 0.0f) ? (double)DEEP_DOG_TORQUE_LIMIT_NM : 0.0;
    const bool has_limit = (DEEP_DOG_TORQUE_LIMIT_NM > 0.0f);
    off += snprintf(buf + off, kCap - off,
                    "],\"torque_limit_nm\":%s,\"has_fault\":%s}",
                    has_limit ? "" : "null",
                    has_fault ? "true" : "false");
    if (has_limit) {
        // 回填数值到 "torque_limit_nm": 之后
        // 简化处理：重新格式化整个对象避免回填复杂性
        // 这里只在 has_limit=true 路径用更简单的重写
        heap_caps_free(buf);
        char small[256];
        snprintf(small, sizeof(small),
                 "{\"motors\":[],\"torque_limit_nm\":%.3f,\"has_fault\":%s}",
                 torque_limit, has_fault ? "true" : "false");
        return SendCorsJson(req, small);
    }
    buf[off] = '\0';
    esp_err_t err = SendCorsJson(req, buf);
    heap_caps_free(buf);
    return err;
#endif  // DEEP_DOG_DOG_ENABLE
}

static esp_err_t ApiFaceHandler(httpd_req_t* req) {
    (void)req;
    /** 勿在 httpd 任务栈上分配 ~2KB：默认 CONFIG_ESP_HTTPD_STACK_SIZE 仅 4KB 级，易栈溢出重启。 */
    constexpr size_t kFaceJsonCap = 2048;
    char* buf = static_cast<char*>(heap_caps_malloc(kFaceJsonCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (buf == nullptr) {
        buf = static_cast<char*>(heap_caps_malloc(kFaceJsonCap, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    if (buf == nullptr) {
        return SendCorsJson(req, R"({"error":"no_mem"})");
    }
    const size_t n = DeepDogFaceAiFormatJson(buf, kFaceJsonCap);
    esp_err_t err;
    if (n == 0 || n >= kFaceJsonCap) {
        err = SendCorsJson(req, R"({"error":"face json overflow"})");
    } else {
        buf[n] = '\0';
        err = SendCorsJson(req, buf);
    }
    heap_caps_free(buf);
    return err;
}

static esp_err_t ApiFaceEnableHandler(httpd_req_t* req) {
    if (req->method != HTTP_POST) {
        httpd_resp_set_status(req, "405 Method Not Allowed");
        return httpd_resp_send(req, nullptr, 0);
    }
    if (DrainPostBody(req) != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, nullptr, 0);
    }
#if !DEEP_DOG_FACE_AI_ENABLE
    return SendCorsJson(req, R"({"ok":false,"reason":"face_ai_disabled"})");
#else
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, R"({"error":"need enabled=0|1"})", HTTPD_RESP_USE_STRLEN);
    }
    char val[16];
    if (httpd_query_key_value(query, "enabled", val, sizeof(val)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, R"({"error":"need enabled=0|1"})", HTTPD_RESP_USE_STRLEN);
    }
    const bool on = (strcmp(val, "1") == 0 || strcasecmp(val, "true") == 0);
    DeepDogFaceAiSetEnabled(on);
    ESP_LOGI(TAG, "网页 人脸检测 -> %s", on ? "on" : "off");
    return SendCorsJson(req, R"({"ok":true})");
#endif
}

static void UrlDecodeInPlace(char* s) {
    if (!s) {
        return;
    }
    char* w = s;
    for (char* r = s; *r; ++r) {
        if (*r == '%' && r[1] && r[2]) {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            const int hi = hex(r[1]);
            const int lo = hex(r[2]);
            if (hi >= 0 && lo >= 0) {
                *w++ = (char)((hi << 4) | lo);
                r += 2;
                continue;
            }
        } else if (*r == '+') {
            *w++ = ' ';
            continue;
        }
        *w++ = *r;
    }
    *w = '\0';
}

static esp_err_t ApiImmichConfigHandler(httpd_req_t* req) {
    if (req->method != HTTP_POST) {
        httpd_resp_set_status(req, "405 Method Not Allowed");
        return httpd_resp_send(req, nullptr, 0);
    }
    if (DrainPostBody(req) != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, nullptr, 0);
    }
#if !DEEP_DOG_FACE_IMMICH_ENABLE
    return SendCorsJson(req, R"({"ok":false,"reason":"immich_disabled"})");
#else
    char query[288];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, R"({"error":"need api_key=... and/or delete_asset=0|1"})", HTTPD_RESP_USE_STRLEN);
    }
    char key[96] = {};
    char url[96] = {};
    char del[8] = {};
    (void)httpd_query_key_value(query, "api_key", key, sizeof(key));
    (void)httpd_query_key_value(query, "api_url", url, sizeof(url));
    (void)httpd_query_key_value(query, "delete_asset", del, sizeof(del));
    UrlDecodeInPlace(key);
    UrlDecodeInPlace(url);
    int delete_asset = -1;
    if (del[0] == '0') {
        delete_asset = 0;
    } else if (del[0] == '1') {
        delete_asset = 1;
    } else if (del[0] != '\0') {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, R"({"error":"delete_asset must be 0 or 1"})", HTTPD_RESP_USE_STRLEN);
    }
    if (key[0] == '\0' && delete_asset < 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, R"({"error":"need api_key and/or delete_asset"})", HTTPD_RESP_USE_STRLEN);
    }
    const bool ok = DeepDogImmichSetConfig(url[0] ? url : nullptr, key[0] ? key : nullptr, delete_asset);
    return SendCorsJson(req, ok ? R"({"ok":true})" : R"({"ok":false})");
#endif
}

static esp_err_t ApiImmichStatusHandler(httpd_req_t* req) {
#if !DEEP_DOG_FACE_IMMICH_ENABLE
    return SendCorsJson(req, R"({"configured":false,"enabled":false})");
#else
    char buf[320];
    const size_t n = DeepDogImmichFormatStatusJson(buf, sizeof(buf));
    if (n == 0) {
        return SendCorsJson(req, R"({"error":"status"})");
    }
    return SendCorsJson(req, buf);
#endif
}

static esp_err_t ApiFaceRefreshNameHandler(httpd_req_t* req) {
    if (req->method != HTTP_POST) {
        httpd_resp_set_status(req, "405 Method Not Allowed");
        return httpd_resp_send(req, nullptr, 0);
    }
    if (DrainPostBody(req) != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, nullptr, 0);
    }
#if !DEEP_DOG_FACE_IMMICH_ENABLE
    return SendCorsJson(req, R"({"ok":false,"reason":"immich_disabled"})");
#else
    int local_id = 0;
    char query[64];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[16];
        if (httpd_query_key_value(query, "local_id", val, sizeof(val)) == ESP_OK) {
            local_id = atoi(val);
        }
    }
    if (local_id <= 0) {
        local_id = DeepDogFaceAiPrimaryLocalId();
    }
    DeepDogImmichRequestRefresh(local_id > 0 ? local_id : 0);
    char buf[80];
    snprintf(buf, sizeof(buf), "{\"ok\":true,\"local_id\":%d}", local_id);
    return SendCorsJson(req, buf);
#endif
}

}  // namespace

DeepDogHttpServer::DeepDogHttpServer(EspVideo* camera, DogControl* dog, uint16_t port)
    : camera_(camera), dog_(dog), port_(port) {}

DeepDogHttpServer::~DeepDogHttpServer() {
    Stop();
}

void DeepDogHttpServer::IpGotHandler(void* arg, esp_event_base_t base, int32_t event_id, void* event_data) {
    (void)base;
    (void)event_data;
    if (event_id != IP_EVENT_STA_GOT_IP) {
        return;
    }
    static_cast<DeepDogHttpServer*>(arg)->LogHttpAccessUrls();
}

void DeepDogHttpServer::LogHttpAccessUrls() {
    if (!server_) {
        return;
    }
    std::string ip = WifiManager::GetInstance().GetIpAddress();
    if (ip.empty() || ip == "0.0.0.0") {
        ESP_LOGW(TAG, "HTTP 服务已启动，WiFi IP 尚未就绪；联网后本日志会再次出现完整地址");
        return;
    }
    ESP_LOGI(TAG, "HTTP 控制页 http://%s:%u/  MJPEG http://%s:%u/stream  (推流/人脸见 VisionHub；HLS 内外网见 /api/status)",
             ip.c_str(), (unsigned)port_, ip.c_str(), (unsigned)port_);
}

void DeepDogHttpServer::SetCaptureMode(DeepDogCaptureMode m) {
    capture_mode_.store(static_cast<uint8_t>(m), std::memory_order_release);
#if DEEP_DOG_VISION_HUB_ENABLE
    if (vision_hub_) {
        VisionPublishMode pm = VisionPublishMode::Off;
        switch (m) {
            case DeepDogCaptureMode::Streaming:
                pm = VisionPublishMode::HttpMjpeg;
                break;
            case DeepDogCaptureMode::RtspPush:
                pm = VisionPublishMode::RtspPush;
                break;
            case DeepDogCaptureMode::PeriodicSample:
            case DeepDogCaptureMode::Off:
            default:
                pm = VisionPublishMode::Off;
                break;
        }
        vision_hub_->SetPublishMode(pm);
    }
#endif
    ESP_LOGI(TAG, "capture/publish mode -> %s", ModeToStr(m));
}

DeepDogCaptureMode DeepDogHttpServer::GetCaptureMode() const {
#if DEEP_DOG_VISION_HUB_ENABLE
    if (vision_hub_) {
        switch (vision_hub_->GetPublishMode()) {
            case VisionPublishMode::HttpMjpeg:
                return DeepDogCaptureMode::Streaming;
            case VisionPublishMode::RtspPush:
                return DeepDogCaptureMode::RtspPush;
            case VisionPublishMode::Off:
            default:
                return DeepDogCaptureMode::Off;
        }
    }
#endif
    return static_cast<DeepDogCaptureMode>(capture_mode_.load(std::memory_order_acquire));
}

bool DeepDogHttpServer::HasJpegFrame() const {
#if DEEP_DOG_VISION_HUB_ENABLE
    if (vision_hub_) {
        return vision_hub_->HasJpegFrame();
    }
#endif
    std::lock_guard<std::mutex> lock(jpeg_mutex_);
    return !jpeg_latest_.empty();
}

void DeepDogHttpServer::PublishJpeg(std::vector<uint8_t>&& jpeg) {
#if DEEP_DOG_VISION_HUB_ENABLE
    if (vision_hub_) {
        vision_hub_->PublishJpeg(std::move(jpeg));
        return;
    }
#endif
    std::lock_guard<std::mutex> lock(jpeg_mutex_);
    jpeg_latest_ = std::move(jpeg);
}

bool DeepDogHttpServer::CopyLatestJpeg(std::vector<uint8_t>* out) const {
#if DEEP_DOG_VISION_HUB_ENABLE
    if (vision_hub_) {
        return vision_hub_->CopyLatestJpeg(out);
    }
#endif
    std::lock_guard<std::mutex> lock(jpeg_mutex_);
    if (jpeg_latest_.empty()) {
        return false;
    }
    *out = jpeg_latest_;
    return true;
}

bool DeepDogHttpServer::TryEnqueueDogCmd(uint8_t cmd) {
    if (!dog_cmd_queue_) {
        return false;
    }
    // HTTP 回调里不阻塞等待队列，优先快速返回给前端。
    if (xQueueSend(dog_cmd_queue_, &cmd, 0) == pdTRUE) {
        return true;
    }

    // 队列满时丢弃最旧命令，尽量保留“最新点击意图”，避免网页看起来卡顿数秒。
    uint8_t dropped = 0;
    if (xQueueReceive(dog_cmd_queue_, &dropped, 0) == pdTRUE) {
        if (xQueueSend(dog_cmd_queue_, &cmd, 0) == pdTRUE) {
            ESP_LOGW(TAG, "dog_web_cmd 队列满：已丢弃最旧命令 %s，保留最新 %s",
                     DogWebCmdStr(static_cast<DogWebCmd>(dropped)),
                     DogWebCmdStr(static_cast<DogWebCmd>(cmd)));
            return true;
        }
    }
    return false;
}

bool DeepDogHttpServer::EnqueueMjpegStreamJob(httpd_req_t* async_req) {
    if (!mjpeg_stream_queue_ || async_req == nullptr) {
        return false;
    }
    return xQueueSend(mjpeg_stream_queue_, &async_req, 0) == pdTRUE;
}

void DeepDogHttpServer::MjpegStreamWorkerEntry(void* arg) {
    static_cast<DeepDogHttpServer*>(arg)->MjpegStreamWorkerLoop();
}

void DeepDogHttpServer::MjpegStreamWorkerLoop() {
    for (;;) {
        httpd_req_t* req = nullptr;
        if (xQueueReceive(mjpeg_stream_queue_, &req, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        (void)RunMjpegStream(req);
        if (httpd_req_async_handler_complete(req) != ESP_OK) {
            ESP_LOGW(TAG, "MJPEG async_handler_complete 失败");
        }
    }
}

void DeepDogHttpServer::DogCmdTaskEntry(void* arg) {
    static_cast<DeepDogHttpServer*>(arg)->DogCmdTaskLoop();
}

void DeepDogHttpServer::DogCmdTaskLoop() {
#if !DEEP_DOG_DOG_ENABLE
    for (;;) {
        uint8_t raw = 0;
        if (xQueueReceive(dog_cmd_queue_, &raw, portMAX_DELAY) == pdTRUE) {
            (void)raw;
        }
    }
#else
    uint8_t raw = 0;
    for (;;) {
        if (xQueueReceive(dog_cmd_queue_, &raw, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!dog_) {
            continue;
        }

        // 突发点击时（例如连续点 stand），合并队列中的待执行命令，只执行最后一条。
        // 这样可避免把多个耗时动作串行跑完，显著降低按钮“迟钝感”。
        uint8_t latest_raw = raw;
        uint8_t pending_raw = 0;
        uint32_t merged = 0;
        while (xQueueReceive(dog_cmd_queue_, &pending_raw, 0) == pdTRUE) {
            latest_raw = pending_raw;
            ++merged;
        }
        if (merged > 0) {
            ESP_LOGI(TAG, "dog_web_cmd 合并突发命令 %lu 条，执行最新: %s",
                     (unsigned long)merged,
                     DogWebCmdStr(static_cast<DogWebCmd>(latest_raw)));
        }

        const auto cmd = static_cast<DogWebCmd>(latest_raw);
        ESP_LOGI(TAG, "dog_web_cmd 执行: %s", DogWebCmdStr(cmd));
        switch (cmd) {
            case DogWebCmd::Init:
                dog_->init();
                break;
            case DogWebCmd::Forward:
                (void)dog_->startContinuousForward(dog_->getContinuousSpeed(), dog_->getContinuousStepPeriodMs());
                break;
            case DogWebCmd::Back:
                (void)dog_->startContinuousBackward(dog_->getContinuousSpeed(), dog_->getContinuousStepPeriodMs());
                break;
            case DogWebCmd::Stand:
                dog_->stand();
                break;
            case DogWebCmd::LieDown:
                dog_->lieDown();
                break;
            case DogWebCmd::Dance:
                dog_->dance();
                break;
            case DogWebCmd::StopWalk:
                dog_->stopContinuousLocomotion();
                break;
            case DogWebCmd::Disable:
                (void)dog_->disable();
                break;
            default:
                break;
        }
    }
#endif  // DEEP_DOG_DOG_ENABLE
}

bool DeepDogHttpServer::Start() {
    if (server_ != nullptr) {
        return true;
    }
    if (!camera_) {
        ESP_LOGE(TAG, "no camera");
        return false;
    }

    worker_stop_.store(false, std::memory_order_release);
    dog_cmd_queue_ = xQueueCreate(16, sizeof(uint8_t));
    if (!dog_cmd_queue_) {
        ESP_LOGE(TAG, "dog cmd queue failed");
        return false;
    }

    if (xTaskCreateWithCaps(DogCmdTaskEntry, "dog_web_cmd", 4096, this, 5, &dog_cmd_task_,
                            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        vQueueDelete(dog_cmd_queue_);
        dog_cmd_queue_ = nullptr;
        ESP_LOGE(TAG, "dog_web_cmd task failed");
        return false;
    }

    mjpeg_stream_queue_ = xQueueCreate(MJPEG_STREAM_QUEUE_DEPTH, sizeof(httpd_req_t*));
    if (!mjpeg_stream_queue_) {
        ESP_LOGE(TAG, "mjpeg stream queue failed");
        return false;
    }
    if (xTaskCreateWithCaps(MjpegStreamWorkerEntry, "dog_mjpeg", MJPEG_STREAM_TASK_STACK, this, MJPEG_STREAM_TASK_PRIO,
                            &mjpeg_stream_tasks_[0], MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        vQueueDelete(mjpeg_stream_queue_);
        mjpeg_stream_queue_ = nullptr;
        ESP_LOGE(TAG, "dog_mjpeg task failed");
        return false;
    }
    if (xTaskCreateWithCaps(MjpegStreamWorkerEntry, "dog_mjpeg2", MJPEG_STREAM_TASK_STACK, this, MJPEG_STREAM_TASK_PRIO,
                            &mjpeg_stream_tasks_[1], MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        ESP_LOGW(TAG, "dog_mjpeg2 task failed (single stream only)");
        mjpeg_stream_tasks_[1] = nullptr;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port_;
    config.ctrl_port = (uint16_t)(port_ + 1);
    /* 须满足 max_open_sockets + 3 <= CONFIG_LWIP_MAX_SOCKETS（常见为 10 → 最多 7） */
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
    /* 死掉的 /stream 客户端若无发送超时会永久占住 dog_mjpeg worker */
    config.send_wait_timeout = 2;
    config.recv_wait_timeout = 2;
    /* S05 + vision_publish */
    config.max_uri_handlers = 18;
#if DEEP_DOG_FACE_AI_ENABLE
    if (config.stack_size < 8192) {
        config.stack_size = 8192;
    }
#endif

    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        for (TaskHandle_t& t : mjpeg_stream_tasks_) {
            if (t) {
                vTaskDeleteWithCaps(t);
                t = nullptr;
            }
        }
        vQueueDelete(mjpeg_stream_queue_);
        mjpeg_stream_queue_ = nullptr;
        return false;
    }

    httpd_uri_t uri_root = {.uri = "/", .method = HTTP_GET, .handler = RootHandler, .user_ctx = this};
    httpd_uri_t uri_stream = {.uri = "/stream", .method = HTTP_GET, .handler = StreamHandler, .user_ctx = this};
    httpd_uri_t uri_status = {.uri = "/api/status", .method = HTTP_GET, .handler = ApiStatusHandler, .user_ctx = this};
    httpd_uri_t uri_mode = {.uri = "/api/capture_mode", .method = HTTP_POST, .handler = ApiModeHandler, .user_ctx = this};
    httpd_uri_t uri_vision =
        {.uri = "/api/vision_publish", .method = HTTP_POST, .handler = ApiVisionPublishHandler, .user_ctx = this};
    httpd_uri_t uri_cmd = {.uri = "/api/cmd", .method = HTTP_POST, .handler = ApiCmdHandler, .user_ctx = this};
    httpd_uri_t uri_dog_status =
        {.uri = "/api/dog_status", .method = HTTP_GET, .handler = ApiDogStatusHandler, .user_ctx = this};
    httpd_uri_t uri_face = {.uri = "/api/face", .method = HTTP_GET, .handler = ApiFaceHandler, .user_ctx = this};
    httpd_uri_t uri_face_en =
        {.uri = "/api/face_enable", .method = HTTP_POST, .handler = ApiFaceEnableHandler, .user_ctx = this};
    httpd_uri_t uri_immich_cfg =
        {.uri = "/api/immich_config", .method = HTTP_POST, .handler = ApiImmichConfigHandler, .user_ctx = this};
    httpd_uri_t uri_immich_st =
        {.uri = "/api/immich_status", .method = HTTP_GET, .handler = ApiImmichStatusHandler, .user_ctx = this};
    httpd_uri_t uri_face_refresh =
        {.uri = "/api/face_refresh_name", .method = HTTP_POST, .handler = ApiFaceRefreshNameHandler, .user_ctx = this};

    if (httpd_register_uri_handler(server_, &uri_root) != ESP_OK || httpd_register_uri_handler(server_, &uri_stream) != ESP_OK ||
        httpd_register_uri_handler(server_, &uri_status) != ESP_OK || httpd_register_uri_handler(server_, &uri_mode) != ESP_OK ||
        httpd_register_uri_handler(server_, &uri_vision) != ESP_OK || httpd_register_uri_handler(server_, &uri_cmd) != ESP_OK ||
        httpd_register_uri_handler(server_, &uri_dog_status) != ESP_OK || httpd_register_uri_handler(server_, &uri_face) != ESP_OK ||
        httpd_register_uri_handler(server_, &uri_face_en) != ESP_OK ||
        httpd_register_uri_handler(server_, &uri_immich_cfg) != ESP_OK ||
        httpd_register_uri_handler(server_, &uri_immich_st) != ESP_OK ||
        httpd_register_uri_handler(server_, &uri_face_refresh) != ESP_OK) {
        httpd_stop(server_);
        server_ = nullptr;
        ESP_LOGE(TAG, "register uri failed");
        return false;
    }

    LogHttpAccessUrls();
    if (esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &DeepDogHttpServer::IpGotHandler, this) ==
        ESP_OK) {
        ip_event_registered_ = true;
    } else {
        ESP_LOGW(TAG, "未注册 IP 事件回调，若启动时无 IP 请手动查路由器或状态页");
    }
    return true;
}

void DeepDogHttpServer::Stop() {
    worker_stop_.store(true, std::memory_order_release);
    if (ip_event_registered_) {
        esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &DeepDogHttpServer::IpGotHandler);
        ip_event_registered_ = false;
    }
    if (server_) {
        httpd_stop(server_);
        server_ = nullptr;
    }
    vTaskDelay(pdMS_TO_TICKS(150));
}

#else  // !DEEP_DOG_HTTP_SERVER_ENABLE

DeepDogHttpServer::DeepDogHttpServer(EspVideo* camera, DogControl* dog, uint16_t port)
    : camera_(camera), dog_(dog), port_(port) {}

DeepDogHttpServer::~DeepDogHttpServer() = default;

void DeepDogHttpServer::SetCaptureMode(DeepDogCaptureMode) {}

DeepDogCaptureMode DeepDogHttpServer::GetCaptureMode() const {
    return DeepDogCaptureMode::Off;
}

bool DeepDogHttpServer::HasJpegFrame() const {
    return false;
}

void DeepDogHttpServer::PublishJpeg(std::vector<uint8_t>&&) {}

bool DeepDogHttpServer::CopyLatestJpeg(std::vector<uint8_t>*) const {
    return false;
}

bool DeepDogHttpServer::TryEnqueueDogCmd(uint8_t) {
    return false;
}

bool DeepDogHttpServer::EnqueueMjpegStreamJob(httpd_req_t*) {
    return false;
}

void DeepDogHttpServer::MjpegStreamWorkerEntry(void*) {}

void DeepDogHttpServer::MjpegStreamWorkerLoop() {}

bool DeepDogHttpServer::Start() {
    return false;
}

void DeepDogHttpServer::Stop() {}

void DeepDogHttpServer::DogCmdTaskEntry(void* arg) {
    static_cast<DeepDogHttpServer*>(arg)->DogCmdTaskLoop();
}

void DeepDogHttpServer::DogCmdTaskLoop() {}

#endif  // DEEP_DOG_HTTP_SERVER_ENABLE
