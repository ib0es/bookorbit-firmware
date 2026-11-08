#include "bookorbit_protocol.h"

#include <ArduinoJson.h>
#include "../services/time_utils.h"


// ArduinoJson may be built without std::string support: go through a buffer.
static std::string toStdString(const JsonDocument& doc) {
    std::string out(measureJson(doc), '\0');
    serializeJson(doc, &out[0], out.size() + 1);
    return out;
}

namespace BookOrbitProtocol {

namespace {
float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

std::string str(JsonVariantConst v) {
    const char* s = v.as<const char*>();
    return s ? std::string(s) : std::string();
}
}  // namespace

bool parseLoginToken(const std::string& json, std::string& token) {
    token.clear();
    JsonDocument doc;
    if (deserializeJson(doc, json.data(), json.size())) return false;
    for (JsonVariantConst v : {doc["accessToken"].as<JsonVariantConst>(), doc["token"].as<JsonVariantConst>(),
                               doc["access_token"].as<JsonVariantConst>(), doc["data"]["token"].as<JsonVariantConst>(),
                               doc["data"]["accessToken"].as<JsonVariantConst>()}) {
        if (v.is<const char*>() && str(v).size() > 0) { token = str(v); return true; }
    }
    return false;
}

bool parseBookProgress(const std::string& json, Progress& out) {
    out = Progress();
    JsonDocument doc;
    if (deserializeJson(doc, json.data(), json.size())) return false;
    JsonArrayConst arr = doc.is<JsonArrayConst>() ? doc.as<JsonArrayConst>() : doc["items"].as<JsonArrayConst>();

    bool found = false;
    for (JsonObjectConst obj : arr) {
        if (obj["percentage"].isNull()) continue;   // not a reading position
        Progress p;
        p.exists = true;
        p.percent = clamp01(obj["percentage"].as<float>() / 100.0f);   // server: 0..100
        if (!obj["pageNumber"].isNull()) p.page = obj["pageNumber"].as<int>();
        else if (!obj["text"]["page"].isNull()) p.page = obj["text"]["page"].as<int>();
        if (obj["cfi"].is<const char*>()) p.cfi = str(obj["cfi"]);
        else if (obj["text"]["cfi"].is<const char*>()) p.cfi = str(obj["text"]["cfi"]);
        p.updatedAt = TimeUtils::parseIso8601(obj["updatedAt"].as<const char*>());

        // The most recent record wins; without timestamps the first one is kept.
        if (!found || p.updatedAt > out.updatedAt) {
            out = p;
            found = true;
        }
    }
    return true;
}

std::string buildProgressWrite(int fileId, float percent, int page, const std::string& cfi) {
    float pct100 = percent > 1.0f ? percent : percent * 100.0f;
    if (pct100 < 0.0f) pct100 = 0.0f;
    if (pct100 > 100.0f) pct100 = 100.0f;
    JsonDocument doc;
    JsonObject item = doc["writes"].to<JsonArray>().add<JsonObject>();
    item["fileId"] = fileId;
    item["percentage"] = pct100;
    if (!cfi.empty()) item["text"]["cfi"] = cfi.c_str();
    else if (page > 0) item["text"]["page"] = page;
    return toStdString(doc);
}

uint32_t parseProgressWriteUpdatedAt(const std::string& json) {
    JsonDocument doc;
    if (deserializeJson(doc, json.data(), json.size())) return 0;
    const char* u = doc["results"][0]["progress"]["updatedAt"].as<const char*>();
    if (!u) u = doc["progress"]["updatedAt"].as<const char*>();
    if (!u) u = doc["updatedAt"].as<const char*>();
    return TimeUtils::parseIso8601(u);
}

}  // namespace BookOrbitProtocol
