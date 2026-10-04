#include "notify.h"

#include <cmath>
#include <cstdio>

namespace qs {

std::string formatTime(const WallTime &t, bool withDate) {
    if (!t.valid()) return "";
    char buf[32];
    if (withDate) std::snprintf(buf, sizeof buf, "%d/%d/%d %02d:%02d:%02d", t.year, t.month, t.day, t.hour, t.minute, t.second);
    else std::snprintf(buf, sizeof buf, "%02d:%02d:%02d", t.hour, t.minute, t.second);
    return buf;
}

namespace {

std::string fmt1(double v) {
    char b[24];
    std::snprintf(b, sizeof b, "%.1f", v);
    return b;
}
std::string fmt0(double v) {
    char b[24];
    std::snprintf(b, sizeof b, "%.0f", v);
    return b;
}

const char *kNote = "※本機が測った揺れからの推定値です。気象庁の発表する震度ではありません。";

}  // namespace

bool makeMessage(const EngineEvent &ev, const MessageContext &ctx, NotifyMessage &out) {
    const std::string when = formatTime(ctx.when, true);
    const std::string shindo = std::string("震度") + shindoLabel(ev.shindo) + "相当";
    out = NotifyMessage();
    out.level = ev.level;
    if (ev.type == EventType::LevelUp) {
        out.kind = NotifyKind::LevelUp;
        if (ev.level == Level::Warning) {
            out.title = "【強い揺れ】" + ctx.place + "で" + shindo + "の揺れ";
            out.body = out.title + "を検知しました。\n身の安全を確保してください。\n";
        } else {
            out.title = "【揺れを検知】" + ctx.place + "で" + shindo + "の揺れ";
            out.body = out.title + "を検知しました。\n";
        }
        if (!when.empty()) out.body += "時刻: " + when + "\n";
        out.body += kNote;
        return true;
    }
    if (ev.type == EventType::ShakeEnd) {
        out.kind = NotifyKind::End;
        out.title = "【揺れがおさまりました】" + ctx.place + " 最大" + shindo;
        out.body = out.title + "\n";
        const std::string start = formatTime(ctx.start, true);
        if (!start.empty()) out.body += "揺れ始め: " + start + "\n";
        out.body += "揺れていた時間: 約" + fmt0(ev.duration) + "秒\n";
        out.body += "最大加速度: " + fmt0(ev.pga) + " gal\n";
        out.body += "計測震度（推定）: " + fmt1(ev.intensity) + "\n";
        out.body += kNote;
        return true;
    }
    if (ev.type == EventType::SensorFault) {
        out.kind = NotifyKind::Fault;
        out.title = "【点検してください】" + ctx.place + "の QuakeScope";
        out.body = out.title + "\n";
        if (ev.value == 2) out.body += "本体が傾いているか、センサーの向きがおかしいようです。水平な場所に置き直してください。\n";
        else if (ev.value == 3) out.body += "センサーから値が届いていません。配線を確かめてください。\n";
        else out.body += "センサーの値が止まっています。配線と電源を確かめてください。\n";
        if (!when.empty()) out.body += "時刻: " + when + "\n";
        return true;
    }
    return false;
}

NotifyMessage makeTestMessage(const MessageContext &ctx) {
    NotifyMessage m;
    m.kind = NotifyKind::Test;
    m.title = "【テスト】QuakeScope（" + ctx.place + "）";
    m.body = m.title + "からのテスト通知です。\n揺れを検知すると、このように届きます。";
    const std::string when = formatTime(ctx.when, true);
    if (!when.empty()) m.body += "\n時刻: " + when;
    return m;
}

std::string jsonEscape(const std::string &s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof b, "\\u%04x", c);
                    o += b;
                } else {
                    o += static_cast<char>(c);
                }
        }
    }
    return o;
}

std::string linePushJson(const std::string &userId, const NotifyMessage &m) {
    return "{\"to\":\"" + jsonEscape(userId) + "\",\"messages\":[{\"type\":\"text\",\"text\":\"" + jsonEscape(m.body) + "\"}]}";
}

std::string telegramJson(const std::string &chatId, const NotifyMessage &m) {
    return "{\"chat_id\":\"" + jsonEscape(chatId) + "\",\"text\":\"" + jsonEscape(m.body) + "\"}";
}

std::string discordJson(const NotifyMessage &m) {
    return "{\"content\":\"" + jsonEscape(m.body) + "\"}";
}

std::string base64(const std::string &s) {
    static const char *t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    size_t i = 0;
    for (; i + 2 < s.size(); i += 3) {
        const unsigned v = (unsigned char)s[i] << 16 | (unsigned char)s[i + 1] << 8 | (unsigned char)s[i + 2];
        o += t[v >> 18 & 63]; o += t[v >> 12 & 63]; o += t[v >> 6 & 63]; o += t[v & 63];
    }
    if (i + 1 == s.size()) {
        const unsigned v = (unsigned char)s[i] << 16;
        o += t[v >> 18 & 63]; o += t[v >> 12 & 63]; o += "==";
    } else if (i + 2 == s.size()) {
        const unsigned v = (unsigned char)s[i] << 16 | (unsigned char)s[i + 1] << 8;
        o += t[v >> 18 & 63]; o += t[v >> 12 & 63]; o += t[v >> 6 & 63]; o += '=';
    }
    return o;
}

std::string mimeHeaderWord(const std::string &s) { return "=?UTF-8?B?" + base64(s) + "?="; }

// ------------------------------------------------------------ 送るかどうか

void NotifyPolicy::record(double now) {
    sent_[sentHead_] = now;
    sentHead_ = (sentHead_ + 1) % 32;
    if (sentN_ < 32) ++sentN_;
}

int NotifyPolicy::sentInLastHour(double now) const {
    int c = 0;
    for (int i = 0; i < sentN_; ++i) if (now - sent_[i] < 3600.0) ++c;
    return c;
}

bool NotifyPolicy::shouldSend(const EngineEvent &ev, double now) {
    bool want = false;
    if (ev.type == EventType::ShakeStart) {
        sentLevel_ = Level::Quiet;
        eventNotified_ = false;
    } else if (ev.type == EventType::LevelUp) {
        // 同じ揺れで、同じレベル以下はもう送らない
        want = static_cast<int>(ev.level) >= static_cast<int>(cfg_.minLevel) &&
               static_cast<int>(ev.level) > static_cast<int>(sentLevel_);
    } else if (ev.type == EventType::ShakeEnd) {
        // 揺れの途中で通知したときだけ、まとめを送る
        want = cfg_.sendEnd && eventNotified_;
        eventNotified_ = false;
        sentLevel_ = Level::Quiet;
    } else if (ev.type == EventType::SensorFault) {
        want = true;
    }
    if (!want) return false;
    // 強い揺れの警報は、上限に関係なく送る
    const bool urgent = ev.type == EventType::LevelUp && ev.level == Level::Warning;
    if (!urgent && sentInLastHour(now) >= cfg_.maxPerHour) return false;
    record(now);
    if (ev.type == EventType::LevelUp) {
        sentLevel_ = ev.level;
        eventNotified_ = true;
    }
    return true;
}

}  // namespace qs
