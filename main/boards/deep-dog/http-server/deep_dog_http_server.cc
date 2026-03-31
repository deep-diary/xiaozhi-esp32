#include "http-server/deep_dog_http_server.h"

#include "dog/dog_control.h"
#include "motor/deep_motor.h"
#include "esp_video.h"

#include "camera.h"
#include "face_ai_bridge.h"
#include "image_to_jpeg.h"

#include <wifi_manager.h>

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <strings.h>

#if DEEP_DOG_HTTP_SERVER_ENABLE

#include <linux/videodev2.h>

#define TAG "dog_http"

/** 并发拉 /stream 的路数（每路在独立任务里跑，不阻塞 httpd） */
#define MJPEG_STREAM_QUEUE_DEPTH 2
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
};

static v4l2_pix_fmt_t V4lFromCameraFrame(const CameraFrame& cf) {
    switch (cf.format) {
        case 1:
            return V4L2_PIX_FMT_RGB565;
        case 2:
            return V4L2_PIX_FMT_RGB24;
        case 3:
            return V4L2_PIX_FMT_YUYV;
        default:
            return static_cast<v4l2_pix_fmt_t>(cf.format);
    }
}

/** 将带行 stride 的 RGB565 压成紧密 w*h*2，供 JPEG 编码与 width/height 一致 */
static bool PackedRgb565FromFrame(const CameraFrame& cf, std::vector<uint8_t>* packed) {
    const uint32_t w = cf.width;
    const uint32_t h = cf.height;
    if (w == 0 || h == 0) {
        return false;
    }
    const size_t row_b = (size_t)w * 2u;
    if (cf.len >= row_b * (size_t)h) {
        if (cf.len % (size_t)h == 0u) {
            const size_t src_stride = cf.len / (size_t)h;
            if (src_stride < row_b) {
                return false;
            }
            if (src_stride == row_b) {
                packed->assign(cf.data, cf.data + row_b * (size_t)h);
                return true;
            }
            packed->resize(row_b * (size_t)h);
            for (uint32_t row = 0; row < h; row++) {
                memcpy(packed->data() + row * row_b, cf.data + row * src_stride, row_b);
            }
            return true;
        }
        packed->assign(cf.data, cf.data + row_b * (size_t)h);
        return true;
    }
    return false;
}

static const char* ModeToStr(DeepDogCaptureMode m) {
    switch (m) {
        case DeepDogCaptureMode::Off:
            return "off";
        case DeepDogCaptureMode::PeriodicSample:
            return "periodic";
        case DeepDogCaptureMode::Streaming:
            return "stream";
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
    if (strcmp(s, "stream") == 0 || strcmp(s, "streaming") == 0) {
        *out = DeepDogCaptureMode::Streaming;
        return true;
    }
    return false;
}

static esp_err_t SendCorsJson(httpd_req_t* req, const char* json) {
    httpd_resp_set_type(req, "application/json; charset=utf-8");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
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
        "</style></head><body>"
        "<h2>DeepDog 遥控</h2>"
        "<p style='color:#888;font-size:13px;margin:0 0 10px 0'>"
        "页面内预览与 <code>/stream</code> 需先点「视频流」；「关闭」不采图故无 MJPEG。「定时」仅占位不推流。</p>"
        "<div class='row'><span>采集模式:</span>"
        "<button class='secondary' onclick=\"setMode('off')\">关闭</button>"
        "<button class='secondary' onclick=\"setMode('periodic')\">定时(1Hz)</button>"
        "<button onclick=\"setMode('stream')\">视频流</button></div>"
        "<p id='st'></p>"
        "<div class='row' style='margin-top:8px'><label style='color:#ccc;font-size:14px'>"
        "<input type='checkbox' id='faceEn' onchange='toggleFace(this.checked)'/> 网页人脸框（需视频流，轮询 /api/face）</label></div>"
        "<p id='faceMeta' style='font-size:12px;color:#9cf;margin:4px 0'></p>"
        "<div style='position:relative;width:240px;height:240px;display:none' id='vidWrap'>"
        "<img id='m' width='240' height='240' alt='mjpeg' style='display:block;width:240px;height:240px'/>"
        "<canvas id='fc' width='240' height='240' style='position:absolute;left:0;top:0;pointer-events:none'></canvas>"
        "</div>"
        "<div class='row'>"
        "<button id='btnInit' onclick=\"cmd('init')\">初始化</button>"
        "<button id='btnForward' onclick=\"cmd('forward')\">前进</button>"
        "<button id='btnBack' onclick=\"cmd('back')\">后退</button>"
        "<button id='btnStand' onclick=\"cmd('stand')\">站立</button>"
        "<button id='btnLie' onclick=\"cmd('liedown')\">卧倒</button>"
        "<button id='btnDance' onclick=\"cmd('dance')\">跳舞</button>"
        "<button id='btnStop' class='secondary' onclick=\"cmd('stop_walk')\">停走</button>"
        "</div>"
        "<div id='dogStat' style='margin-top:8px;font-size:12px;color:#ccc'></div>"
        "<script>"
        "const st=document.getElementById('st');const wrap=document.getElementById('vidWrap');"
        "const img=document.getElementById('m');const cv=document.getElementById('fc');const faceMeta=document.getElementById('faceMeta');"
        "const dogStat=document.getElementById('dogStat');"
        "let facePoll=null;let lastStatus=null;"
        "function drawFaces(j){const c=cv.getContext('2d');c.clearRect(0,0,240,240);"
        "if(!j||!j.faces)return;(j.faces||[]).forEach(f=>{c.strokeStyle='#0f0';c.lineWidth=2;"
        "c.strokeRect(f.x0*240,f.y0*240,(f.x1-f.x0)*240,(f.y1-f.y0)*240);});}"
        "function pollFace(){fetch('/api/face').then(r=>r.json()).then(j=>{"
        "if(!j.enabled){faceMeta.textContent='人脸检测未编译';drawFaces(null);return;}"
        "faceMeta.textContent='人脸:'+(j.feature_on?'开':'关')+' has_face:'+j.has_face+' n:'+j.n+' 帧:'+j.w+'x'+j.h;"
        "if(j.feature_on)drawFaces(j);else drawFaces(null);}).catch(()=>{});}"
        "function toggleFace(on){fetch('/api/face_enable?enabled='+(on?'1':'0'),{method:'POST'})"
        ".then(()=>pollFace()).catch(()=>{});if(on&&!facePoll){facePoll=setInterval(pollFace,200);pollFace();}"
        "else if(!on&&facePoll){clearInterval(facePoll);facePoll=null;drawFaces(null);faceMeta.textContent='';}}"
        "function applyDogInitState(j){"
        "const inited=!!j.dog_initialized;"
        "const bi=document.getElementById('btnInit');"
        "const bs=['btnForward','btnBack','btnStand','btnLie','btnDance','btnStop'].map(id=>document.getElementById(id));"
        "if(bi)bi.disabled=inited;"
        "bs.forEach(b=>{if(b)b.disabled=!inited;});"
        "}"
        "function refresh(){return fetch('/api/status').then(r=>r.json()).then(j=>{"
        "lastStatus=j;"
        "st.textContent='模式:'+j.mode+' 拉流:'+j.stream_clients+' JPEG:'+(j.has_jpeg?'有':'无')"
        "+(j.face_ai_compiled?' 人脸模块:有':' 人脸模块:无')"
        "+' 初始化:'+((j.dog_initialized)?'已完成':'未完成');"
        "applyDogInitState(j);"
        "if(j.mode==='stream'){wrap.style.display='block';if(!img.src||img.src.indexOf('/stream')<0)img.src='/stream';}"
        "else{wrap.style.display='none';img.removeAttribute('src');if(facePoll){clearInterval(facePoll);facePoll=null;}"
        "document.getElementById('faceEn').checked=false;drawFaces(null);faceMeta.textContent='';}}).catch(()=>{st.textContent='状态获取失败';});}"
        "function setMode(m){fetch('/api/capture_mode?mode='+encodeURIComponent(m),{method:'POST'})"
        ".then(r=>{if(!r.ok)throw new Error('HTTP '+r.status);return refresh();})"
        ".catch(e=>{st.textContent='切换模式失败: '+e.message;});}"
        "function updateDogStatus(){fetch('/api/dog_status').then(r=>r.json()).then(j=>{"
        "if(!j.motors){dogStat.textContent='';return;}"
        "let txt='电机状态: ';"
        "txt+='limit='+(j.torque_limit_nm!=null?j.torque_limit_nm:'-')+' N·m; ';"
        "txt+='fault='+(j.has_fault?'是':'否')+'; ';"
        "txt+= '关节: '+j.motors.map(m=>m.id+':'+(m.deg!=null?m.deg.toFixed(1):'?')+'°/τ='+(m.torque_nm!=null?m.torque_nm.toFixed(2):'?')).join(' , ');"
        "dogStat.textContent=txt;}).catch(()=>{});}"
        "function cmd(c){fetch('/api/cmd?cmd='+encodeURIComponent(c),{method:'POST'})"
        ".then(r=>r.json().catch(()=>({})).then(j=>{"
        "if(!r.ok||j.ok===false){st.textContent='指令失败: '+(j.error||('HTTP '+r.status));}"
        "else{st.textContent='指令已发送: '+c;}"
        "})).catch(e=>{st.textContent='指令失败: '+e.message;});}"
        "refresh();setInterval(refresh,2000);setInterval(updateDogStatus,2000);"
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
    if (!srv || !srv->camera()) {
        return ESP_FAIL;
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
    const DogControl* dog = srv->dog();
    bool dog_initialized = false;
    if (dog) {
        dog_initialized = (dog->getPoseState() != DogPoseState::Uninitialized);
    }
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"mode\":\"%s\",\"stream_clients\":%d,\"has_jpeg\":%s,\"port\":%u,"
             "\"face_ai_compiled\":%s,\"dog_initialized\":%s}",
             ModeToStr(srv->GetCaptureMode()), srv->StreamClientCount(), srv->HasJpegFrame() ? "true" : "false",
             (unsigned)srv->Port(),
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
        return httpd_resp_send(req, R"({"error":"need mode=off|periodic|stream"})", HTTPD_RESP_USE_STRLEN);
    }
    DeepDogCaptureMode m = DeepDogCaptureMode::Off;
    if (!StrToMode(val, &m)) {
        ESP_LOGW(TAG, "网页 采集模式无效: %s", val);
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, R"({"error":"bad mode"})", HTTPD_RESP_USE_STRLEN);
    }
    srv->SetCaptureMode(m);
    ESP_LOGI(TAG, "网页 采集模式 -> %s", ModeToStr(m));
    return SendCorsJson(req, R"({"ok":true})");
}

static esp_err_t ApiCmdHandler(httpd_req_t* req) {
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
}

static esp_err_t ApiDogStatusHandler(httpd_req_t* req) {
    auto* srv = static_cast<DeepDogHttpServer*>(req->user_ctx);
    if (!srv) {
        return ESP_FAIL;
    }
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
    ESP_LOGI(TAG, "HTTP 控制页 http://%s:%u/  MJPEG http://%s:%u/stream  (默认采集 off)", ip.c_str(),
             (unsigned)port_, ip.c_str(), (unsigned)port_);
}

void DeepDogHttpServer::SetCaptureMode(DeepDogCaptureMode m) {
    capture_mode_.store(static_cast<uint8_t>(m), std::memory_order_release);
}

bool DeepDogHttpServer::HasJpegFrame() const {
    std::lock_guard<std::mutex> lock(jpeg_mutex_);
    return !jpeg_latest_.empty();
}

void DeepDogHttpServer::PublishJpeg(std::vector<uint8_t>&& jpeg) {
    std::lock_guard<std::mutex> lock(jpeg_mutex_);
    jpeg_latest_ = std::move(jpeg);
}

bool DeepDogHttpServer::CopyLatestJpeg(std::vector<uint8_t>* out) const {
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
    return xQueueSend(dog_cmd_queue_, &cmd, pdMS_TO_TICKS(100)) == pdTRUE;
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

bool DeepDogHttpServer::EncodePackedJpegFromCamera(std::vector<uint8_t>* out, bool submit_face_for_ai) {
    if (!camera_ || !out) {
        return false;
    }
    if (!camera_->CaptureOnly()) {
        return false;
    }
    CameraFrame cf{};
    if (!camera_->GetLastFrame(&cf)) {
        return false;
    }

    v4l2_pix_fmt_t vf = V4lFromCameraFrame(cf);
    std::vector<uint8_t> packed;
    uint8_t* src = cf.data;
    size_t src_len = cf.len;
    if (vf == V4L2_PIX_FMT_RGB565) {
        if (!PackedRgb565FromFrame(cf, &packed)) {
            return false;
        }
        src = packed.data();
        src_len = packed.size();
#if DEEP_DOG_FACE_AI_ENABLE
        if (submit_face_for_ai && !packed.empty()) {
            DeepDogFaceAiSubmitFrameIfDue(packed.data(), packed.size(), static_cast<uint16_t>(cf.width),
                                         static_cast<uint16_t>(cf.height));
        }
#endif
    }

    uint8_t* jpeg_ptr = nullptr;
    size_t jpeg_len = 0;
    if (!image_to_jpeg(src, src_len, cf.width, cf.height, vf, (uint8_t)jpeg_quality_, &jpeg_ptr, &jpeg_len)) {
        return false;
    }
    out->assign(jpeg_ptr, jpeg_ptr + jpeg_len);
    free(jpeg_ptr);
    return true;
}

bool DeepDogHttpServer::EncodeCurrentFrameToJpeg(std::vector<uint8_t>* out) {
    return EncodePackedJpegFromCamera(out, false);
}

void DeepDogHttpServer::CameraWorkerEntry(void* arg) {
    static_cast<DeepDogHttpServer*>(arg)->CameraWorkerLoop();
}

void DeepDogHttpServer::CameraWorkerLoop() {
    const TickType_t periodic = pdMS_TO_TICKS(1000);
    while (!WorkerStopRequested()) {
        const auto mode = GetCaptureMode();
        switch (mode) {
            case DeepDogCaptureMode::Off:
                vTaskDelay(pdMS_TO_TICKS(200));
                break;
            case DeepDogCaptureMode::PeriodicSample:
                if (camera_ && camera_->CaptureOnly()) {
                    ESP_LOGD(TAG, "periodic capture tick (人脸/检测可挂接此处)");
                }
                vTaskDelay(periodic);
                break;
            case DeepDogCaptureMode::Streaming: {
                std::vector<uint8_t> jpeg;
                if (EncodePackedJpegFromCamera(&jpeg, true) && !jpeg.empty()) {
                    PublishJpeg(std::move(jpeg));
                }
                {
                    int fps = stream_target_fps_ > 0 ? stream_target_fps_ : 8;
                    vTaskDelay(pdMS_TO_TICKS(1000 / fps));
                }
                break;
            }
            default:
                vTaskDelay(pdMS_TO_TICKS(200));
                break;
        }
    }
    camera_worker_ = nullptr;
    vTaskDelete(nullptr);
}

void DeepDogHttpServer::DogCmdTaskEntry(void* arg) {
    static_cast<DeepDogHttpServer*>(arg)->DogCmdTaskLoop();
}

void DeepDogHttpServer::DogCmdTaskLoop() {
    uint8_t raw = 0;
    for (;;) {
        if (xQueueReceive(dog_cmd_queue_, &raw, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!dog_) {
            continue;
        }
        const auto cmd = static_cast<DogWebCmd>(raw);
        ESP_LOGI(TAG, "dog_web_cmd 执行: %s", DogWebCmdStr(cmd));
        switch (cmd) {
            case DogWebCmd::Init:
                dog_->init();
                break;
            case DogWebCmd::Forward:
                dog_->goForward();
                break;
            case DogWebCmd::Back:
                dog_->goBack();
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
            default:
                break;
        }
    }
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

    if (xTaskCreate(DogCmdTaskEntry, "dog_web_cmd", 4096, this, 5, &dog_cmd_task_) != pdPASS) {
        vQueueDelete(dog_cmd_queue_);
        dog_cmd_queue_ = nullptr;
        ESP_LOGE(TAG, "dog_web_cmd task failed");
        return false;
    }

    if (xTaskCreate(CameraWorkerEntry, "dog_cam_http", 10240, this, 4, &camera_worker_) != pdPASS) {
        // task created dog_cmd - leave it; rare failure
        ESP_LOGE(TAG, "camera worker task failed");
        return false;
    }

    mjpeg_stream_queue_ = xQueueCreate(MJPEG_STREAM_QUEUE_DEPTH, sizeof(httpd_req_t*));
    if (!mjpeg_stream_queue_) {
        ESP_LOGE(TAG, "mjpeg stream queue failed");
        return false;
    }
    if (xTaskCreate(MjpegStreamWorkerEntry, "dog_mjpeg", MJPEG_STREAM_TASK_STACK, this, MJPEG_STREAM_TASK_PRIO,
                    &mjpeg_stream_task_) != pdPASS) {
        vQueueDelete(mjpeg_stream_queue_);
        mjpeg_stream_queue_ = nullptr;
        ESP_LOGE(TAG, "dog_mjpeg task failed");
        return false;
    }

#if DEEP_DOG_FACE_AI_ENABLE
    if (!DeepDogFaceAiRuntimeStart()) {
        ESP_LOGW(TAG, "人脸检测 runtime 未启动（可继续用网页/MJPEG，仅无检测）");
    }
#endif

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port_;
    config.ctrl_port = (uint16_t)(port_ + 1);
    /* 须满足 max_open_sockets + 3 <= CONFIG_LWIP_MAX_SOCKETS（常见为 10 → 最多 7） */
    config.max_open_sockets = 7;
    config.lru_purge_enable = true;
#if DEEP_DOG_FACE_AI_ENABLE
    /* 人脸相关 API + 解析/日志链略深，略高于默认 4096，避免边缘场景栈溢出 */
    if (config.stack_size < 8192) {
        config.stack_size = 8192;
    }
#endif

    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        if (mjpeg_stream_task_) {
            vTaskDelete(mjpeg_stream_task_);
            mjpeg_stream_task_ = nullptr;
        }
        vQueueDelete(mjpeg_stream_queue_);
        mjpeg_stream_queue_ = nullptr;
#if DEEP_DOG_FACE_AI_ENABLE
        DeepDogFaceAiRuntimeStop();
#endif
        return false;
    }

    httpd_uri_t uri_root = {.uri = "/", .method = HTTP_GET, .handler = RootHandler, .user_ctx = this};
    httpd_uri_t uri_stream = {.uri = "/stream", .method = HTTP_GET, .handler = StreamHandler, .user_ctx = this};
    httpd_uri_t uri_status = {.uri = "/api/status", .method = HTTP_GET, .handler = ApiStatusHandler, .user_ctx = this};
    httpd_uri_t uri_mode = {.uri = "/api/capture_mode", .method = HTTP_POST, .handler = ApiModeHandler, .user_ctx = this};
    httpd_uri_t uri_cmd = {.uri = "/api/cmd", .method = HTTP_POST, .handler = ApiCmdHandler, .user_ctx = this};
    httpd_uri_t uri_dog_status =
        {.uri = "/api/dog_status", .method = HTTP_GET, .handler = ApiDogStatusHandler, .user_ctx = this};
    httpd_uri_t uri_face = {.uri = "/api/face", .method = HTTP_GET, .handler = ApiFaceHandler, .user_ctx = this};
    httpd_uri_t uri_face_en =
        {.uri = "/api/face_enable", .method = HTTP_POST, .handler = ApiFaceEnableHandler, .user_ctx = this};

    if (httpd_register_uri_handler(server_, &uri_root) != ESP_OK || httpd_register_uri_handler(server_, &uri_stream) != ESP_OK ||
        httpd_register_uri_handler(server_, &uri_status) != ESP_OK || httpd_register_uri_handler(server_, &uri_mode) != ESP_OK ||
        httpd_register_uri_handler(server_, &uri_cmd) != ESP_OK || httpd_register_uri_handler(server_, &uri_dog_status) != ESP_OK ||
        httpd_register_uri_handler(server_, &uri_face) != ESP_OK || httpd_register_uri_handler(server_, &uri_face_en) != ESP_OK) {
        httpd_stop(server_);
        server_ = nullptr;
#if DEEP_DOG_FACE_AI_ENABLE
        DeepDogFaceAiRuntimeStop();
#endif
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
#if DEEP_DOG_FACE_AI_ENABLE
    DeepDogFaceAiRuntimeStop();
#endif
    if (ip_event_registered_) {
        esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &DeepDogHttpServer::IpGotHandler);
        ip_event_registered_ = false;
    }
    if (server_) {
        httpd_stop(server_);
        server_ = nullptr;
    }
    vTaskDelay(pdMS_TO_TICKS(150));
    // dog_cmd_queue_ / tasks：板级通常不反复 Stop；析构时简单丢弃
}

#else  // !DEEP_DOG_HTTP_SERVER_ENABLE

DeepDogHttpServer::DeepDogHttpServer(EspVideo* camera, DogControl* dog, uint16_t port)
    : camera_(camera), dog_(dog), port_(port) {}

DeepDogHttpServer::~DeepDogHttpServer() = default;

void DeepDogHttpServer::SetCaptureMode(DeepDogCaptureMode) {}

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

void DeepDogHttpServer::CameraWorkerEntry(void* arg) {
    static_cast<DeepDogHttpServer*>(arg)->CameraWorkerLoop();
}

void DeepDogHttpServer::CameraWorkerLoop() {}

void DeepDogHttpServer::DogCmdTaskEntry(void* arg) {
    static_cast<DeepDogHttpServer*>(arg)->DogCmdTaskLoop();
}

void DeepDogHttpServer::DogCmdTaskLoop() {}

bool DeepDogHttpServer::EncodeCurrentFrameToJpeg(std::vector<uint8_t>*) {
    return false;
}

bool DeepDogHttpServer::EncodePackedJpegFromCamera(std::vector<uint8_t>*, bool) {
    return false;
}

#endif  // DEEP_DOG_HTTP_SERVER_ENABLE
