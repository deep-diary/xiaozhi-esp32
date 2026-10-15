# V-C05 · RTSP 传输层：TCP Interleaved → UDP 评估

| 项 | 内容 |
|----|------|
| 路线图 ID | **V-C05**（评估中，未排期） |
| 优先级 | 待评估后确定 |
| 依赖 | [C02](./C02-device-push-stream.md) 已实现 |
| 代码落点 | `main/boards/deep-dog/vision/rtsp_h264_pusher.{h,cc}`、`rtsp_jpeg_pusher.{h,cc}`、`vision_config.h` |
| 状态 | **方案评估文档**（未改代码） |

---

## 1. 背景

当前 RTSP 推流使用 **RTP over TCP Interleaved** 模式（见 [C02 §7](./C02-device-push-stream.md)），即 RTSP 控制与 RTP 数据共用一条 TCP 连接，数据帧以 `$ <channel> <len> <RTP>` 格式封装。

随着 S06 §9 规划分辨率从 240×240 提升到 320×240（甚至更高），需要重新评估传输层效率，确认 TCP 是否仍是最佳选择。

---

## 2. 当前方案（TCP Interleaved）

### 2.1 架构

```
RTSP 控制 + RTP 数据  ⇄  单一 TCP Socket (:8554)  ⇄  MediaMTX
```

### 2.2 数据格式

```
+----+---------+----------+-------------------+
| $  | channel | length   | RTP packet        |
| 1B | 1B      | 2B (BE)  | 12B header + payload |
+----+---------+----------+-------------------+
```

每包额外开销：**4 字节**（$ + channel + length）+ TCP 头（20B）+ IP 头（20B）= 约 44 字节/包

### 2.3 握手流程（现状）

```
OPTIONS → ANNOUNCE → SETUP(transport: RTP/AVP/TCP;interleaved=0-1) → RECORD
```

### 2.4 优点

1. **实现简单**：仅需一个 socket，控制与数据同通道
2. **NAT 友好**：单连接出站，防火墙/路由器无需额外配置
3. **可靠传输**：TCP 保证不丢包、不乱序
4. **调试方便**：Wireshark 直接跟踪一条流

### 2.5 缺点

1. **TCP 头部开销**：每个 RTP 包多 20B TCP + 20B IP 头
2. **队头阻塞（HoL Blocking）**：某包丢失后，后续所有包等待重传，增加延迟
3. **拥塞控制开销**：慢启动、拥塞避免等算法在低带宽、低丢包的内网环境下反而限制吞吐
4. **RTSP 控制与数据互相影响**：数据量大时可能阻塞 RTSP 信令响应（如 keep-alive）

---

## 3. 备选方案（RTP over UDP）

### 3.1 架构

```
RTSP 控制  ⇄  TCP Socket (:8554)          ⇄  MediaMTX RTSP
RTP 数据   ⇄  UDP Socket (客户端随机端口)  ⇄  MediaMTX RTP (:服务器端口)
RTCP 控制  ⇄  UDP Socket (RTP端口+1)      ⇄  MediaMTX RTCP
```

### 3.2 数据格式

直接发送 RTP 包，无 `$` 前缀与 interleaved 头：

```
+---------+-------------------+
| RTP hdr | RTP payload       |
| 12B     | H.264 NAL / JPEG  |
+---------+-------------------+
```

每包额外开销：**UDP 头（8B）+ IP 头（20B）** = 28 字节/包（比 TCP 少约 16B/包）

### 3.3 握手流程（变更后）

```
OPTIONS → ANNOUNCE → SETUP(transport: RTP/AVP/UDP;client_port=port0-port1) → RECORD
```

客户端在 SETUP 请求的 `Transport` 头中声明本地 RTP/RTCP 端口对，服务器在 200 响应中返回服务器侧端口对。

### 3.4 MediaMTX 兼容性

MediaMTX 原生支持 RTP over UDP，且为默认拉流传输方式。对于 ANNOUNCE（推流）路径同样支持 UDP。

验证命令：
```bash
# 用 ffmpeg 以 UDP 推 RTSP 到 MediaMTX
ffmpeg -re -i test.mp4 -c:v copy -f rtsp -rtsp_transport udp rtsp://192.168.31.25:8554/deep-dog/dev
```

---

## 4. 改动量评估

### 4.1 文件改动清单

| 文件 | 改动类型 | 预估行数 | 说明 |
|------|----------|----------|------|
| `vision/rtsp_h264_pusher.h` | 修改 | +15 / -2 | 新增 UDP socket 成员、端口配置、RTCP 相关成员 |
| `vision/rtsp_h264_pusher.cc` | 修改 | +180 / -30 | UDP 收发、SETUP 解析服务器端口、UDP 发送逻辑 |
| `vision/rtsp_jpeg_pusher.h` | 修改 | +15 / -2 | 同上（与 H.264 对称） |
| `vision/rtsp_jpeg_pusher.cc` | 修改 | +160 / -30 | 同上（与 H.264 对称；略少因无 FU-A） |
| `vision/vision_config.h` | 修改 | +10 | 新增 `DEEP_DOG_VISION_RTSP_TRANSPORT_UDP` 宏、UDP 端口范围宏 |
| `vision/vision_frame_hub.cc` | 修改 | ±0 | 上层接口不变，无需改动 |
| `docs/requirements/vision/client/C02-device-push-stream.md` | 修改 | +10 | 补充 UDP 传输说明 |

**合计：约 +380 行新增 / -64 行修改，涉及 6 个文件**

### 4.2 核心改动点详述

#### 改动 1：UDP Socket 管理（两个 pusher 各一份）

**新增成员变量：**
```cpp
int rtp_sock_ = -1;       // UDP RTP socket
int rtcp_sock_ = -1;      // UDP RTCP socket
uint16_t server_rtp_port_ = 0;   // MediaMTX RTP 端口（从 SETUP 响应解析）
uint16_t server_rtcp_port_ = 0;  // MediaMTX RTCP 端口
```

**新增逻辑：**
- `Connect()` 中创建 UDP socket 并绑定本地端口（从配置的范围中选，如 50000-51000）
- `Disconnect()` 中关闭 UDP socket
- `ParseSetupResponse()`：从 SETUP 响应的 `Transport` 头提取 `server_port=RTP-RTCP`

#### 改动 2：SETUP 请求修改

**当前（TCP）：**
```
Transport: RTP/AVP/TCP;unicast;interleaved=0-1;mode=RECORD
```

**改为（UDP）：**
```
Transport: RTP/AVP/UDP;unicast;client_port=50000-50001;mode=RECORD
```

需要生成一对连续可用的本地 UDP 端口。

#### 改动 3：数据发送路径

**当前（TCP Interleaved）：**
```cpp
bool RtspH264Pusher::SendInterleavedRtp(const uint8_t* rtp, size_t rtp_len) {
    uint8_t hdr[4];
    hdr[0] = 0x24;  // '$'
    hdr[1] = 0;     // channel 0
    WriteBe16(hdr + 2, rtp_len);
    SendAll(hdr, 4);
    return SendAll(rtp, rtp_len);
}
```

**改为（UDP）：**
```cpp
bool RtspH264Pusher::SendUdpRtp(const uint8_t* rtp, size_t rtp_len) {
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server_rtp_port_);
    // ... 填充 server_ip_ ...
    ssize_t n = sendto(rtp_sock_, rtp, rtp_len, 0,
                       (struct sockaddr*)&addr, sizeof(addr));
    return n == (ssize_t)rtp_len;
}
```

移除 4 字节 interleaved 头，直接用 `sendto` 发到服务器 RTP 端口。

#### 改动 4：连接状态与超时

**TCP 模式**：通过 `sock_ >= 0` 和 `send()` 返回值判断连接状态。

**UDP 模式**：UDP 是无连接的，需要额外机制：
- **最简方案**：保留 RTSP TCP 连接做控制，用 RECORD 成功 + 持续发送判定为 Streaming；发送失败（如 ICMP port unreachable 导致的 error）视为断连
- **推荐方案**：增加 RTCP RR 接收超时（如 10s 未收到 RR 则认为链路异常），触发重连

本评估推荐**最简方案**（第一版不实现完整 RTCP），理由：
- 内网场景丢包率极低
- 现有指数退避重连机制已足够容错
- 后续可按需追加 RTCP 质量监控

#### 改动 5：配置宏

新增编译期/运行时切换开关：

```c
/** RTSP 传输层：1=UDP（默认），0=TCP Interleaved（回退） */
#ifndef DEEP_DOG_VISION_RTSP_TRANSPORT_UDP
#define DEEP_DOG_VISION_RTSP_TRANSPORT_UDP 1
#endif

/** UDP RTP 本地端口范围（从中选一对连续端口） */
#ifndef DEEP_DOG_VISION_RTP_PORT_MIN
#define DEEP_DOG_VISION_RTP_PORT_MIN 50000
#endif
#ifndef DEEP_DOG_VISION_RTP_PORT_MAX
#define DEEP_DOG_VISION_RTP_PORT_MAX 51000
#endif
```

### 4.3 风险点与应对

| 风险 | 概率 | 影响 | 应对措施 |
|------|------|------|----------|
| UDP 丢包导致花屏/卡顿 | 低（内网） | 中 | 先在内网实测丢包率；保留 TCP 回退开关 |
| NAT/防火墙阻挡 UDP | 中（跨网段时） | 高 | 默认仅内网推流（见 [infra](../infra.md)），与当前拓扑一致；公网场景继续 TCP 或另寻方案 |
| 端口冲突 | 低 | 低 | 端口范围 1000 个，动态选取空闲端口 |
| RTP 乱序 | 极低（内网） | 低 | H.264 解码对少量乱序容忍度高；必要时加抖动缓冲 |
| 开发调试难度上升 | 中 | 低 | 保留 TCP 模式作调试回退 |

---

## 5. 效率与性能对比

### 5.1 带宽开销估算（以 240×240 @ 5fps H.264 为例）

| 项 | TCP Interleaved | UDP | 差异 |
|----|-----------------|-----|------|
| 典型帧大小（H.264） | ~8 KB | ~8 KB | 相同 |
| 每帧 RTP 包数（MTU=1200） | ~7 包 | ~7 包 | 相同 |
| 每包协议头开销 | 44B（IP20+TCP20+$4） | 28B（IP20+UDP8） | UDP 少 16B/包 |
| 每秒总开销（5fps × 7包） | 1540 B/s | 980 B/s | UDP 省 ~36% |
| 总码率（含视频+开销） | ~41.5 KB/s | ~40.9 KB/s | 差 ~1.5% |

**结论：当前低码率下，传输层协议的带宽差异可以忽略（< 2%）。**

### 5.2 延迟对比

| 指标 | TCP Interleaved | UDP | 说明 |
|------|-----------------|-----|------|
| 单向传输延迟 | 1~5ms（内网） | < 1ms（内网） | UDP 无拥塞控制排队 |
| 丢包时延迟 | 数十 ms ~ 数 s（重传） | 1 帧时间（直接丢弃） | TCP 重传导致卡顿，UDP 直接丢帧 |
| 端到端延迟（采集→播放） | 300~800ms | 200~500ms | 含编码、缓冲、解码 |

**结论：UDP 延迟更低，但在当前 5fps 低帧率场景下体感差异不明显。**

### 5.3 CPU 开销

- **TCP**：内核处理 TCP 拥塞控制、重传、序号管理，有一定 CPU 占用
- **UDP**：内核处理简单，CPU 占用更低

但在 ESP32-S3 上，CPU 瓶颈主要是 **H.264 软编** 和 **图像缩放**，网络协议栈的 CPU 占比很小（估计 < 5%）。

---

## 6. 综合评估结论

### 6.1 是否值得做？

**当前阶段（240×240 @ 3~5fps，内网推流）：不值得。**

理由：
1. **带宽差异极小**：低码率场景下，TCP vs UDP 的带宽差异 < 2%，完全可以忽略
2. **CPU 瓶颈在编码**：网络传输不是系统瓶颈，H.264 软编才是
3. **实现收益不明显**：省的带宽和延迟对当前场景几乎不可感知
4. **增加维护复杂度**：两套传输模式 + UDP 调试排障成本更高
5. **内网可靠性足够**：局域网 TCP 延迟和丢包率都很低，UDP 优势不突出

### 6.2 什么时候值得做？

出现以下情况时，应重新评估 UDP 方案：

| 触发条件 | 说明 |
|----------|------|
| 推流分辨率 ≥ 640×480 | 码率上升后，TCP 开销占比增大 |
| 帧率 ≥ 15fps | 高帧率对延迟更敏感 |
| 跨公网推流 | 公网延迟抖动大，TCP 队头阻塞更严重 |
| 多设备同时推流 | 总带宽紧张时每比特都重要 |
| 实时互动场景（如 WebRTC 回看） | 对端到端延迟有严格要求 |

### 6.3 替代优化方向（性价比更高）

在考虑换传输协议之前，优先做以下优化：

| 方向 | 预期收益 | 改动量 |
|------|----------|--------|
| 提升 H.264 编码效率（调参/换编码器） | 码率降 20~50% | 中 |
| 硬件 JPEG 编码替代软编（若摄像头支持） | CPU 降 30%+ | 中 |
| 优化采集→编码 pipeline（零拷贝） | CPU 降 10~20% | 中 |
| 升级到硬件 H.264 编码（需换芯片） | CPU 降 80%+ | 大（硬件变更） |

---

## 7. 若要实施的实施计划（可选，未排期）

### Phase 1：UDP 基础版（约 2 人天）
- [ ] `rtsp_h264_pusher` 支持 UDP 传输（RTP 发送，无 RTCP）
- [ ] `rtsp_jpeg_pusher` 同步支持 UDP
- [ ] 新增 `DEEP_DOG_VISION_RTSP_TRANSPORT_UDP` 宏，默认 TCP（保守回退）
- [ ] 现有测试脚本 `deep_dog_rtsp_pull_verify.py` 验证通过
- [ ] 更新 C02 文档

### Phase 2：RTCP 质量监控（约 1 人天，可选）
- [ ] RTCP RR 接收与解析
- [ ] 丢包率、抖动统计
- [ ] 超时检测与自动重连触发
- [ ] 上报到 MQTT `stream/status`

### Phase 3：性能对比与切换策略（约 0.5 人天）
- [ ] 实测 TCP vs UDP 的延迟、带宽、CPU 差异
- [ ] 确定默认模式（根据实测结果）
- [ ] 动态切换机制（运行时可切）

---

## 8. 验收标准（若实施）

- [ ] UDP 模式下，`deep_dog_rtsp_pull_verify.py --python-only` 拉流 100% 成功
- [ ] 连续推流 10 分钟不中断，无花屏
- [ ] TCP 模式仍可正常工作（编译回退 `DEEP_DOG_VISION_RTSP_TRANSPORT_UDP=0`）
- [ ] 与 MJPEG、face_ai 的互斥/复用关系不变
- [ ] MQTT `stream/status` 行为不变
- [ ] 文档与代码注释同步更新

---

## 9. 相关文件

- 当前实现：[`vision/rtsp_h264_pusher.cc`](../../../../vision/rtsp_h264_pusher.cc)、[`vision/rtsp_jpeg_pusher.cc`](../../../../vision/rtsp_jpeg_pusher.cc)
- 配置：[`vision/vision_config.h`](../../../../vision/vision_config.h)
- 需求背景：[C02-device-push-stream.md](./C02-device-push-stream.md)
- 基础设施：[vision/infra.md](../infra.md)
