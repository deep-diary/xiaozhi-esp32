#pragma once

#include <string>
#include <vector>

// Explain 相关逻辑：请求服务器做人脸识别
// 协议：question = "人脸识别" 表示请求为人脸识别；响应 JSON 含 face_recognition.people[].name

namespace deep_thumble {

constexpr const char* FACE_EXPLAIN_QUESTION = "人脸识别";

bool ParseExplainFaceRecognitionResponse(const std::string& json, std::vector<std::string>* out_names);

}  // namespace deep_thumble
