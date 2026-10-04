#include "storage.h"

#include <LittleFS.h>
#include <Preferences.h>

#include <algorithm>
#include <vector>

namespace app {

bool Storage::begin() {
    ok_ = LittleFS.begin(true);   // 初めてなら初期化する
    if (ok_ && !LittleFS.exists("/ev")) LittleFS.mkdir("/ev");
    return ok_;
}

String Storage::binPath(uint32_t id) const { return "/ev/" + String(id) + ".bin"; }

namespace {

std::vector<uint32_t> ids() {
    std::vector<uint32_t> v;
    File dir = LittleFS.open("/ev");
    if (!dir) return v;
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        String n = f.name();
        if (n.endsWith(".json")) v.push_back(static_cast<uint32_t>(n.toInt()));
    }
    std::sort(v.begin(), v.end());
    return v;
}

}  // namespace

uint32_t Storage::save(const EventSummary &s, const Recorder &rec, int samples) {
    if (!ok_) return 0;
    Preferences p;
    p.begin("quakescope", false);
    const uint32_t id = p.getUInt("evid", 0) + 1;
    p.putUInt("evid", id);
    p.end();
    prune(kKeepSummaries - 1, kKeepWaves - 1);
    // 波形: 2 サンプルずつ平均して 50 Hz で保存する（保存の領域を節約。25 Hz までの揺れを残す）
    int saved = 0;
    File b = LittleFS.open(binPath(id), FILE_WRITE);
    if (b) {
        int16_t chunk[64 * 3];
        int k = 0;
        for (int i = 0; i + 1 < samples; i += 2) {
            const int16_t *a = rec.at(i), *c = rec.at(i + 1);
            for (int ax = 0; ax < 3; ++ax) chunk[k * 3 + ax] = static_cast<int16_t>((a[ax] + c[ax]) / 2);
            ++saved;
            if (++k == 64) { b.write(reinterpret_cast<uint8_t *>(chunk), sizeof chunk); k = 0; }
        }
        if (k) b.write(reinterpret_cast<uint8_t *>(chunk), k * 6);
        b.close();
    }
    // まとめ
    char json[320];
    snprintf(json, sizeof json,
             "{\"id\":%lu,\"start\":%lu,\"duration\":%.1f,\"intensity\":%.1f,\"shindo\":\"%s\",\"pga\":%.1f,"
             "\"level\":%u,\"notified\":%s,\"samples\":%d,\"fs\":50,\"unit\":0.1}",
             static_cast<unsigned long>(id), static_cast<unsigned long>(s.epochStart), s.duration, s.intensity, s.shindo, s.pga,
             s.level, s.notified ? "true" : "false", saved);
    File j = LittleFS.open("/ev/" + String(id) + ".json", FILE_WRITE);
    if (!j) return 0;
    j.print(json);
    j.close();
    return id;
}

void Storage::prune(int keepSummaries, int keepWaves) {
    std::vector<uint32_t> v = ids();
    for (size_t i = 0; i < v.size(); ++i) {
        const int fromNewest = static_cast<int>(v.size() - 1 - i);
        if (fromNewest >= keepSummaries) LittleFS.remove("/ev/" + String(v[i]) + ".json");
        if (fromNewest >= keepWaves && LittleFS.exists(binPath(v[i]))) LittleFS.remove(binPath(v[i]));
    }
    // 空きが少なければ、古い波形から消す
    for (size_t i = 0; i < v.size() && LittleFS.totalBytes() - LittleFS.usedBytes() < 48 * 1024; ++i)
        if (LittleFS.exists(binPath(v[i]))) LittleFS.remove(binPath(v[i]));
}

String Storage::listJson(int maxN) {
    String out = "[";
    if (ok_) {
        std::vector<uint32_t> v = ids();
        int n = 0;
        for (auto it = v.rbegin(); it != v.rend() && n < maxN; ++it, ++n) {
            File f = LittleFS.open("/ev/" + String(*it) + ".json");
            if (!f) continue;
            if (n) out += ",";
            out += f.readString();
            f.close();
        }
    }
    return out + "]";
}

void Storage::clear() { prune(0, 0); }
size_t Storage::usedBytes() const { return ok_ ? LittleFS.usedBytes() : 0; }
size_t Storage::totalBytes() const { return ok_ ? LittleFS.totalBytes() : 0; }

}  // namespace app
