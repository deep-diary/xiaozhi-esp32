#!/bin/bash
# MediaMTX快速部署脚本
# 用于在服务器上快速部署MediaMTX流媒体服务器
#
# 使用方法:
#   chmod +x setup_mediamtx.sh
#   ./setup_mediamtx.sh <ESP32_IP>
#
# 例如:
#   ./setup_mediamtx.sh 192.168.1.100

set -e

# 颜色输出
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}================================${NC}"
echo -e "${GREEN}  MediaMTX 快速部署脚本${NC}"
echo -e "${GREEN}  ESP32-CAM RTSP流媒体服务器${NC}"
echo -e "${GREEN}================================${NC}"
echo ""

# 检查参数
if [ $# -eq 0 ]; then
    echo -e "${YELLOW}用法: $0 <ESP32_IP>${NC}"
    echo -e "${YELLOW}例如: $0 192.168.1.100${NC}"
    exit 1
fi

ESP32_IP=$1

# 检查是否是root用户
if [ "$EUID" -ne 0 ]; then 
    echo -e "${YELLOW}警告: 建议以root权限运行以配置防火墙${NC}"
fi

# 步骤1: 下载MediaMTX
echo -e "${GREEN}[1/5] 下载MediaMTX...${NC}"
MEDIAMTX_VERSION="v1.3.0"
MEDIAMTX_ARCH="linux_amd64"
MEDIAMTX_URL="https://github.com/bluenviron/mediamtx/releases/download/${MEDIAMTX_VERSION}/mediamtx_${MEDIAMTX_VERSION}_${MEDIAMTX_ARCH}.tar.gz"

if [ -f "mediamtx" ]; then
    echo -e "${YELLOW}MediaMTX已存在，跳过下载${NC}"
else
    echo "下载地址: $MEDIAMTX_URL"
    wget -q --show-progress "$MEDIAMTX_URL" -O mediamtx.tar.gz
    tar -xzf mediamtx.tar.gz
    chmod +x mediamtx
    echo -e "${GREEN}✓ MediaMTX下载完成${NC}"
fi

# 步骤2: 生成配置文件
echo -e "${GREEN}[2/5] 生成配置文件...${NC}"
cat > mediamtx.yml << EOF
# MediaMTX配置文件
# 自动生成于: $(date)
# ESP32 IP: ${ESP32_IP}

# RTSP服务器配置
rtspAddress: :8554
rtspTCPAddress: :8322
rtspUDPRTPAddress: :8000
rtspUDPRTCPAddress: :8001

# HLS服务器配置
hlsAddress: :8888
hlsServerKey: server.key
hlsServerCert: server.crt
hlsAllowOrigin: '*'
hlsAlwaysRemux: no
hlsVariant: lowLatency
hlsSegmentCount: 7
hlsSegmentDuration: 1s
hlsPartDuration: 200ms

# WebRTC服务器配置
webrtcAddress: :8889
webrtcServerKey: server.key
webrtcServerCert: server.crt
webrtcAllowOrigin: '*'
webrtcICEServers: []

# 日志配置
logLevel: info
logDestinations: [stdout, file]
logFile: mediamtx.log

# 性能配置
readTimeout: 10s
writeTimeout: 10s
readBufferCount: 512

# 路径配置
paths:
  # ESP32摄像头主流
  esp32cam:
    source: rtsp://${ESP32_IP}:554/stream
    sourceProtocol: tcp
    sourceOnDemand: yes
    sourceOnDemandStartTimeout: 10s
    sourceOnDemandCloseAfter: 10s
    runOnReady: echo "ESP32 camera connected"
    runOnNotReady: echo "ESP32 camera disconnected"
    
  # 备用路径（可选）
  esp32cam_backup:
    source: rtsp://${ESP32_IP}:554/stream
    sourceProtocol: udp
    sourceOnDemand: yes

# 其他配置
rtspReadTimeout: 10s
rtspWriteTimeout: 10s
EOF

echo -e "${GREEN}✓ 配置文件已生成: mediamtx.yml${NC}"

# 步骤3: 测试ESP32连接
echo -e "${GREEN}[3/5] 测试ESP32连接...${NC}"
if ping -c 1 -W 2 "$ESP32_IP" > /dev/null 2>&1; then
    echo -e "${GREEN}✓ ESP32 ($ESP32_IP) 连接正常${NC}"
else
    echo -e "${RED}✗ 无法连接到ESP32 ($ESP32_IP)${NC}"
    echo -e "${YELLOW}  请检查:${NC}"
    echo -e "${YELLOW}  1. ESP32是否已连接到WiFi${NC}"
    echo -e "${YELLOW}  2. IP地址是否正确${NC}"
    echo -e "${YELLOW}  3. 防火墙设置${NC}"
    read -p "是否继续? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

# 步骤4: 配置防火墙
echo -e "${GREEN}[4/5] 配置防火墙...${NC}"
if command -v ufw &> /dev/null; then
    echo "检测到UFW防火墙，添加规则..."
    if [ "$EUID" -eq 0 ]; then
        ufw allow 8554/tcp comment "MediaMTX RTSP"
        ufw allow 8888/tcp comment "MediaMTX HLS"
        ufw allow 8889/tcp comment "MediaMTX WebRTC"
        ufw allow 8000:8001/udp comment "MediaMTX RTP/RTCP"
        echo -e "${GREEN}✓ 防火墙规则已添加${NC}"
    else
        echo -e "${YELLOW}需要root权限配置防火墙，请手动执行:${NC}"
        echo "  sudo ufw allow 8554/tcp"
        echo "  sudo ufw allow 8888/tcp"
        echo "  sudo ufw allow 8889/tcp"
        echo "  sudo ufw allow 8000:8001/udp"
    fi
elif command -v firewall-cmd &> /dev/null; then
    echo "检测到firewalld，添加规则..."
    if [ "$EUID" -eq 0 ]; then
        firewall-cmd --permanent --add-port=8554/tcp
        firewall-cmd --permanent --add-port=8888/tcp
        firewall-cmd --permanent --add-port=8889/tcp
        firewall-cmd --permanent --add-port=8000-8001/udp
        firewall-cmd --reload
        echo -e "${GREEN}✓ 防火墙规则已添加${NC}"
    else
        echo -e "${YELLOW}需要root权限配置防火墙${NC}"
    fi
else
    echo -e "${YELLOW}未检测到防火墙或需要手动配置${NC}"
fi

# 步骤5: 创建启动脚本
echo -e "${GREEN}[5/5] 创建启动脚本...${NC}"
cat > start_mediamtx.sh << 'EOF'
#!/bin/bash
# MediaMTX启动脚本

echo "启动MediaMTX流媒体服务器..."
./mediamtx mediamtx.yml
EOF

chmod +x start_mediamtx.sh

# 创建systemd服务（可选）
cat > mediamtx.service << EOF
[Unit]
Description=MediaMTX RTSP Server for ESP32-CAM
After=network.target

[Service]
Type=simple
User=$USER
WorkingDirectory=$(pwd)
ExecStart=$(pwd)/mediamtx $(pwd)/mediamtx.yml
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

echo -e "${GREEN}✓ 启动脚本已创建: start_mediamtx.sh${NC}"
echo -e "${GREEN}✓ Systemd服务文件已创建: mediamtx.service${NC}"

# 完成提示
echo ""
echo -e "${GREEN}================================${NC}"
echo -e "${GREEN}  部署完成！${NC}"
echo -e "${GREEN}================================${NC}"
echo ""
echo -e "${YELLOW}后续步骤:${NC}"
echo ""
echo -e "1. 确保ESP32已启动RTSP服务:"
echo -e "   ${YELLOW}通过MCP工具调用: rtsp_start${NC}"
echo ""
echo -e "2. 启动MediaMTX服务器:"
echo -e "   ${YELLOW}./start_mediamtx.sh${NC}"
echo ""
echo -e "3. 或者安装为系统服务:"
echo -e "   ${YELLOW}sudo cp mediamtx.service /etc/systemd/system/${NC}"
echo -e "   ${YELLOW}sudo systemctl daemon-reload${NC}"
echo -e "   ${YELLOW}sudo systemctl enable mediamtx${NC}"
echo -e "   ${YELLOW}sudo systemctl start mediamtx${NC}"
echo ""
echo -e "4. 测试观看（选择一种）:"
echo -e "   ${YELLOW}RTSP: rtsp://$(hostname -I | awk '{print $1}'):8554/esp32cam${NC}"
echo -e "   ${YELLOW}HLS:  http://$(hostname -I | awk '{print $1}'):8888/esp32cam${NC}"
echo -e "   ${YELLOW}VLC:  vlc rtsp://$(hostname -I | awk '{print $1}'):8554/esp32cam${NC}"
echo ""
echo -e "5. 查看日志:"
echo -e "   ${YELLOW}tail -f mediamtx.log${NC}"
echo ""
echo -e "${GREEN}================================${NC}"

