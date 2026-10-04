#include "notifier.h"

#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <time.h>

namespace app {

struct Notifier::Job {
    qs::NotifyMessage msg;
    bool lineOn, tgOn, dcOn, mailOn;
    String lineToken, lineUser, tgToken, tgChat, dcWebhook, smtpHost, smtpUser, smtpPass, mailTo;
    uint16_t smtpPort;
};

void Notifier::begin() {
    queue_ = xQueueCreate(8, sizeof(Job *));
    logLock_ = xSemaphoreCreateMutex();
    // TLS（HTTPS）には大きなスタックがいる
    xTaskCreatePinnedToCore(taskMain, "notify", 12288, this, 2, nullptr, 0);
}

void Notifier::enqueue(const qs::NotifyMessage &m, const Settings &s) {
    if (!queue_) return;
    Job *j = new Job();
    j->msg = m;
    j->lineOn = s.lineOn && s.lineToken.length() && s.lineUser.length();
    j->tgOn = s.tgOn && s.tgToken.length() && s.tgChat.length();
    j->dcOn = s.dcOn && s.dcWebhook.startsWith("https://");
    j->mailOn = s.mailOn && s.smtpHost.length() && s.mailTo.length();
    j->lineToken = s.lineToken; j->lineUser = s.lineUser;
    j->tgToken = s.tgToken; j->tgChat = s.tgChat;
    j->dcWebhook = s.dcWebhook;
    j->smtpHost = s.smtpHost; j->smtpPort = s.smtpPort; j->smtpUser = s.smtpUser; j->smtpPass = s.smtpPass; j->mailTo = s.mailTo;
    if (!j->lineOn && !j->tgOn && !j->dcOn && !j->mailOn) { delete j; return; }
    if (xQueueSend(queue_, &j, 0) != pdTRUE) { delete j; return; }  // 詰まっていたら捨てる（計測を止めない）
    pending_ = pending_ + 1;
}

void Notifier::taskMain(void *arg) {
    Notifier *self = static_cast<Notifier *>(arg);
    for (;;) {
        Job *j = nullptr;
        if (xQueueReceive(self->queue_, &j, portMAX_DELAY) == pdTRUE && j) {
            self->run(j);
            delete j;
            self->pending_ = self->pending_ - 1;
        }
    }
}

void Notifier::run(Job *j) {
    // Wi-Fi がつながるまで、少し待つ（停電からの復帰直後など）
    for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; ++i) vTaskDelay(pdMS_TO_TICKS(1000));
    const String title = j->msg.title.c_str();
    // 送り先ごとに、最大 3 回（5 秒・15 秒あけて）
    for (int ch = 0; ch < 4; ++ch) {
        bool on = (ch == 0 && j->lineOn) || (ch == 1 && j->tgOn) || (ch == 2 && j->dcOn) || (ch == 3 && j->mailOn);
        if (!on) continue;
        for (int attempt = 0; attempt < 3; ++attempt) {
            bool ok = false;
            if (ch == 0) ok = sendHttp("LINE", "https://api.line.me/v2/bot/message/push",
                                       qs::linePushJson(j->lineUser.c_str(), j->msg), j->lineToken, title);
            else if (ch == 1) ok = sendHttp("Telegram", "https://api.telegram.org/bot" + j->tgToken + "/sendMessage",
                                            qs::telegramJson(j->tgChat.c_str(), j->msg), "", title);
            else if (ch == 2) ok = sendHttp("Discord", j->dcWebhook, qs::discordJson(j->msg), "", title);
            else ok = sendMail(*j);
            if (ok) break;
            // 設定の誤り（4xx）は送り直しても同じなので、やめる
            SendLog last;
            logs(&last, 1);
            if (last.code >= 400 && last.code < 500 && last.code != 429) break;
            vTaskDelay(pdMS_TO_TICKS(attempt == 0 ? 5000 : 15000));
        }
    }
}

bool Notifier::sendHttp(const char *channel, const String &url, const std::string &json, const String &bearer, const String &title) {
    NetworkClientSecure client;
    client.useBuiltinCACertBundle();   // 証明書を確かめる（Mozilla の主要な認証局）
    HTTPClient http;
    http.setTimeout(15000);
    http.setConnectTimeout(10000);
    if (!http.begin(client, url)) { addLog(channel, title, -1, false); return false; }
    http.addHeader("Content-Type", "application/json; charset=utf-8");
    if (bearer.length()) http.addHeader("Authorization", "Bearer " + bearer);
    const int code = http.POST(reinterpret_cast<uint8_t *>(const_cast<char *>(json.data())), json.size());
    http.end();
    const bool ok = code >= 200 && code < 300;
    addLog(channel, title, code, ok);
    return ok;
}

namespace {

// SMTP の応答を 1 つ読む（複数行なら最後まで）。状態コードを返す
int smtpReply(NetworkClientSecure &c) {
    String line;
    const uint32_t until = millis() + 15000;
    while (millis() < until) {
        if (!c.connected() && !c.available()) return -1;
        if (!c.available()) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
        line = c.readStringUntil('\n');
        if (line.length() >= 4 && line[3] == ' ') return line.substring(0, 3).toInt();
        if (line.length() == 3) return line.toInt();
    }
    return -2;
}

int smtpCmd(NetworkClientSecure &c, const String &cmd) {
    c.print(cmd);
    c.print("\r\n");
    return smtpReply(c);
}

String b64(const String &s) { return qs::base64(s.c_str()).c_str(); }

}  // namespace

bool Notifier::sendMail(const Job &j) {
    NetworkClientSecure c;
    c.useBuiltinCACertBundle();
    c.setTimeout(15);
    const bool starttls = j.smtpPort == 587;
    if (starttls) c.setPlainStart();
    int code = -1;
    auto fail = [&](int cd) { c.stop(); addLog("メール", j.msg.title.c_str(), cd, false); return false; };
    if (!c.connect(j.smtpHost.c_str(), j.smtpPort)) return fail(-1);
    if ((code = smtpReply(c)) != 220) return fail(code);
    if ((code = smtpCmd(c, "EHLO quakescope")) != 250) return fail(code);
    if (starttls) {
        if ((code = smtpCmd(c, "STARTTLS")) != 220) return fail(code);
        if (c.startTLS() != 1) return fail(-3);
        if ((code = smtpCmd(c, "EHLO quakescope")) != 250) return fail(code);
    }
    if ((code = smtpCmd(c, "AUTH LOGIN")) != 334) return fail(code);
    if ((code = smtpCmd(c, b64(j.smtpUser))) != 334) return fail(code);
    if ((code = smtpCmd(c, b64(j.smtpPass))) != 235) return fail(code);
    if ((code = smtpCmd(c, "MAIL FROM:<" + j.smtpUser + ">")) != 250) return fail(code);
    // 宛先はカンマ区切りで複数
    String to = j.mailTo;
    int start = 0;
    while (start < static_cast<int>(to.length())) {
        int comma = to.indexOf(',', start);
        if (comma < 0) comma = to.length();
        String addr = to.substring(start, comma);
        addr.trim();
        if (addr.length()) {
            code = smtpCmd(c, "RCPT TO:<" + addr + ">");
            if (code != 250 && code != 251) return fail(code);
        }
        start = comma + 1;
    }
    if ((code = smtpCmd(c, "DATA")) != 354) return fail(code);
    c.print("From: QuakeScope <" + j.smtpUser + ">\r\n");
    c.print("To: " + j.mailTo + "\r\n");
    c.print(String("Subject: ") + qs::mimeHeaderWord(j.msg.title).c_str() + "\r\n");
    c.print("MIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: base64\r\n\r\n");
    const std::string body = qs::base64(j.msg.body);
    for (size_t i = 0; i < body.size(); i += 76) {
        c.print(body.substr(i, 76).c_str());
        c.print("\r\n");
    }
    if ((code = smtpCmd(c, ".")) != 250) return fail(code);
    smtpCmd(c, "QUIT");
    c.stop();
    addLog("メール", j.msg.title.c_str(), 250, true);
    return true;
}

void Notifier::addLog(const char *channel, const String &title, int code, bool ok) {
    xSemaphoreTake(logLock_, portMAX_DELAY);
    SendLog &l = log_[logHead_];
    time_t now = time(nullptr);
    l.epoch = now > 1700000000 ? static_cast<uint32_t>(now) : 0;
    strlcpy(l.channel, channel, sizeof l.channel);
    strlcpy(l.title, title.c_str(), sizeof l.title);
    l.code = code;
    l.ok = ok;
    logHead_ = (logHead_ + 1) % 20;
    if (logN_ < 20) ++logN_;
    xSemaphoreGive(logLock_);
}

int Notifier::logs(SendLog *out, int n) {
    if (!logLock_) return 0;
    xSemaphoreTake(logLock_, portMAX_DELAY);
    const int k = n < logN_ ? n : logN_;
    for (int i = 0; i < k; ++i) out[i] = log_[(logHead_ - 1 - i + 20) % 20];
    xSemaphoreGive(logLock_);
    return k;
}

}  // namespace app
