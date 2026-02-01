#include "face_explain.hpp"

#include <cJSON.h>
#include <esp_log.h>

static const char* TAG = "FaceExplain";

namespace deep_thumble {

bool ParseExplainFaceRecognitionResponse(const std::string& json, std::vector<std::string>* out_names) {
    if (out_names == nullptr) {
        return false;
    }
    out_names->clear();

    cJSON* root = cJSON_Parse(json.c_str());
    if (root == nullptr) {
        ESP_LOGD(TAG, "ParseExplainFaceRecognitionResponse: cJSON_Parse failed");
        return false;
    }

    cJSON* face_rec = cJSON_GetObjectItem(root, "face_recognition");
    if (face_rec == nullptr || !cJSON_IsObject(face_rec)) {
        cJSON_Delete(root);
        return true;
    }

    cJSON* people = cJSON_GetObjectItem(face_rec, "people");
    if (people == nullptr || !cJSON_IsArray(people)) {
        cJSON_Delete(root);
        return true;
    }

    int n = cJSON_GetArraySize(people);
    for (int i = 0; i < n; i++) {
        cJSON* person = cJSON_GetArrayItem(people, i);
        if (person == nullptr || !cJSON_IsObject(person)) {
            continue;
        }
        cJSON* name_item = cJSON_GetObjectItem(person, "name");
        if (name_item != nullptr && cJSON_IsString(name_item) && name_item->valuestring != nullptr) {
            out_names->emplace_back(name_item->valuestring);
        }
    }

    cJSON_Delete(root);
    return true;
}

}  // namespace deep_thumble
