// 通知を送る（LINE・Telegram・Discord・メール）。
// 揺れの計測を止めないよう、別のタスクで順番に送り、失敗したら間をあけて送り直す。
#pragma once

#include <Arduino.h>

#include "../qs/notify.h"
#include "settings.h"

namespace app {

struct SendLog {
    uint32_t epoch = 0;      // 送った時刻（UNIX 秒、分からなければ 0）
    char channel[10] = "";   // "LINE" "Telegram" "Discord" "メール"
    char title[96] = "";
    int code = 0;            // HTTP の状態コード（メールは SMTP の応答）
    bool ok = false;
};

class Notifier {
public:
    void begin();
    // 設定のうち、送り先の情報だけを写して送る
    void enqueue(const qs::NotifyMessage &m, const Settings &s);
    // 直近の送信の記録（新しい順に最大 n 件）
    int logs(SendLog *out, int n);
    int pending() const { return pending_; }

private:
    struct Job;
    static void taskMain(void *arg);
    void run(Job *job);
    bool sendHttp(const char *channel, const String &url, const std::string &json, const String &bearer, const String &title);
    bool sendMail(const Job &j);
    void addLog(const char *channel, const String &title, int code, bool ok);
    QueueHandle_t queue_ = nullptr;
    SemaphoreHandle_t logLock_ = nullptr;
    SendLog log_[20];
    int logHead_ = 0, logN_ = 0;
    volatile int pending_ = 0;
};

}  // namespace app
