// 通知の文面と、送り先ごとの送る中身（JSON など）。送るかどうかの判断（同じ揺れで何度も送らない・送りすぎない）。
// 実際の送信（HTTPS・メール）は本体のファームウェアが行う。
#pragma once

#include <cstdint>
#include <string>

#include "engine.h"

namespace qs {

struct WallTime {
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    bool valid() const { return year >= 2020; }
};

enum class NotifyKind : uint8_t { LevelUp, End, Test, Fault };

struct NotifyMessage {
    NotifyKind kind = NotifyKind::Test;
    Level level = Level::Quiet;
    std::string title;  // 件名（メール）・1 行目
    std::string body;   // 本文（複数行）
};

struct MessageContext {
    std::string place = "自宅";   // 本体を置いた場所の名前
    WallTime when;               // 出来事の時刻（時刻が分からなければ year = 0）
    WallTime start;              // 揺れ始めの時刻
};

// 出来事から通知の文面を作る（通知しない出来事なら false）
bool makeMessage(const EngineEvent &ev, const MessageContext &ctx, NotifyMessage &out);
NotifyMessage makeTestMessage(const MessageContext &ctx);

// 送り先ごとの中身
std::string jsonEscape(const std::string &s);
std::string linePushJson(const std::string &userId, const NotifyMessage &m);
std::string telegramJson(const std::string &chatId, const NotifyMessage &m);
std::string discordJson(const NotifyMessage &m);
std::string base64(const std::string &s);
// メールのヘッダーの件名（RFC 2047、UTF-8 を base64 で）
std::string mimeHeaderWord(const std::string &s);
std::string formatTime(const WallTime &t, bool withDate);

// 送るかどうか
struct NotifyPolicyConfig {
    Level minLevel = Level::Caution;  // このレベル以上になったら送る
    bool sendEnd = true;              // 揺れが収まったときに、まとめを送る
    int maxPerHour = 12;              // 1 時間に送る上限（誤作動のときに送り続けないため）
};

class NotifyPolicy {
public:
    void setConfig(const NotifyPolicyConfig &c) { cfg_ = c; }
    const NotifyPolicyConfig &config() const { return cfg_; }
    // now: 秒（単調増加）。送るなら true
    bool shouldSend(const EngineEvent &ev, double now);
    int sentInLastHour(double now) const;

private:
    void record(double now);
    NotifyPolicyConfig cfg_;
    Level sentLevel_ = Level::Quiet;   // この揺れで、すでに送ったレベル
    bool eventNotified_ = false;
    double sent_[32] = {};
    int sentN_ = 0, sentHead_ = 0;
};

}  // namespace qs
