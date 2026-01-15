#!/bin/bash
# RTSP流测试脚本
# 用于测试ESP32-CAM的RTSP流是否正常工作

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

echo -e "${BLUE}================================${NC}"
echo -e "${BLUE}  ESP32-CAM RTSP流测试工具${NC}"
echo -e "${BLUE}================================${NC}"
echo ""

# 检查参数
if [ $# -eq 0 ]; then
    echo -e "${YELLOW}用法: $0 <ESP32_IP>${NC}"
    echo -e "${YELLOW}例如: $0 192.168.1.100${NC}"
    exit 1
fi

ESP32_IP=$1
RTSP_URL="rtsp://${ESP32_IP}:554/stream"

# 测试1: 网络连通性
echo -e "${GREEN}[测试1/5] 检查网络连通性...${NC}"
if ping -c 3 -W 2 "$ESP32_IP" > /dev/null 2>&1; then
    echo -e "${GREEN}✓ ESP32 ($ESP32_IP) 网络连通${NC}"
else
    echo -e "${RED}✗ 无法连接到ESP32 ($ESP32_IP)${NC}"
    exit 1
fi

# 测试2: RTSP端口检查
echo -e "${GREEN}[测试2/5] 检查RTSP端口 (554)...${NC}"
if command -v nc &> /dev/null; then
    if nc -zv -w 3 "$ESP32_IP" 554 2>&1 | grep -q "succeeded\|open"; then
        echo -e "${GREEN}✓ RTSP端口 (554) 已开放${NC}"
    else
        echo -e "${RED}✗ RTSP端口 (554) 未开放${NC}"
        echo -e "${YELLOW}  请确保ESP32已启动RTSP服务${NC}"
        exit 1
    fi
else
    echo -e "${YELLOW}⚠ 未安装nc命令，跳过端口检查${NC}"
fi

# 测试3: RTSP OPTIONS请求
echo -e "${GREEN}[测试3/5] 测试RTSP OPTIONS请求...${NC}"
if command -v curl &> /dev/null; then
    if timeout 5 curl -s --rtsp-request OPTIONS "$RTSP_URL" > /dev/null 2>&1; then
        echo -e "${GREEN}✓ RTSP服务响应正常${NC}"
    else
        echo -e "${RED}✗ RTSP服务无响应${NC}"
        exit 1
    fi
else
    echo -e "${YELLOW}⚠ 未安装curl，跳过RTSP测试${NC}"
fi

# 测试4: 尝试获取SDP描述
echo -e "${GREEN}[测试4/5] 获取流媒体描述 (SDP)...${NC}"
if command -v ffprobe &> /dev/null; then
    echo -e "${BLUE}流信息:${NC}"
    timeout 10 ffprobe -v quiet -print_format json -show_format -show_streams \
        -rtsp_transport tcp "$RTSP_URL" 2>/dev/null | \
        python3 -m json.tool 2>/dev/null || \
        echo -e "${YELLOW}  无法解析流信息${NC}"
    echo -e "${GREEN}✓ 流媒体描述获取成功${NC}"
else
    echo -e "${YELLOW}⚠ 未安装ffprobe，跳过流信息检查${NC}"
fi

# 测试5: 尝试录制一小段视频
echo -e "${GREEN}[测试5/5] 录制测试视频 (5秒)...${NC}"
if command -v ffmpeg &> /dev/null; then
    TEST_FILE="test_${ESP32_IP}_$(date +%Y%m%d_%H%M%S).mp4"
    echo -e "${BLUE}正在录制到: ${TEST_FILE}${NC}"
    
    if timeout 10 ffmpeg -y -rtsp_transport tcp -i "$RTSP_URL" \
        -t 5 -c copy "$TEST_FILE" > /dev/null 2>&1; then
        
        FILE_SIZE=$(du -h "$TEST_FILE" | cut -f1)
        echo -e "${GREEN}✓ 视频录制成功${NC}"
        echo -e "${BLUE}  文件: ${TEST_FILE}${NC}"
        echo -e "${BLUE}  大小: ${FILE_SIZE}${NC}"
        
        # 播放测试文件
        echo ""
        read -p "是否播放测试视频? (y/n) " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            if command -v vlc &> /dev/null; then
                vlc "$TEST_FILE" &
            elif command -v ffplay &> /dev/null; then
                ffplay "$TEST_FILE"
            else
                echo -e "${YELLOW}未找到播放器，请手动播放: ${TEST_FILE}${NC}"
            fi
        fi
    else
        echo -e "${RED}✗ 视频录制失败${NC}"
        exit 1
    fi
else
    echo -e "${YELLOW}⚠ 未安装ffmpeg，跳过录制测试${NC}"
fi

# 总结
echo ""
echo -e "${GREEN}================================${NC}"
echo -e "${GREEN}  测试完成！${NC}"
echo -e "${GREEN}================================${NC}"
echo ""
echo -e "${BLUE}RTSP流地址:${NC}"
echo -e "  ${YELLOW}${RTSP_URL}${NC}"
echo ""
echo -e "${BLUE}播放命令:${NC}"
echo -e "  ${YELLOW}VLC:    vlc ${RTSP_URL}${NC}"
echo -e "  ${YELLOW}ffplay: ffplay -rtsp_transport tcp ${RTSP_URL}${NC}"
echo -e "  ${YELLOW}mpv:    mpv ${RTSP_URL}${NC}"
echo ""
echo -e "${BLUE}录制命令:${NC}"
echo -e "  ${YELLOW}ffmpeg -rtsp_transport tcp -i ${RTSP_URL} -c copy output.mp4${NC}"
echo ""
echo -e "${BLUE}浏览器播放 (需要MediaMTX):${NC}"
echo -e "  ${YELLOW}http://SERVER_IP:8888/esp32cam${NC}"
echo ""
echo -e "${GREEN}================================${NC}"

# 性能信息
echo ""
echo -e "${BLUE}性能建议:${NC}"
echo -e "  • 低延迟: 使用 -rtsp_transport tcp"
echo -e "  • 高质量: 设置ESP32帧率10-15fps，JPEG质量80-90"
echo -e "  • 低带宽: 设置ESP32帧率5fps，JPEG质量60-70"
echo -e "  • 录像: 使用 -c copy 避免重新编码"
echo ""

