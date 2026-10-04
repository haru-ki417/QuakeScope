// このファイルは core/src から写したものです。編集は core/src で行い、tools/sync_core.py を実行してください。
// 本体の小さな画面（OLED）に出す内容。本体とブラウザー版で同じものを表示するため、ここで作る。
// OLED の文字は英数字だけなので、表示は ASCII（震度 5 弱は "5-"、5 強は "5+"）。
#pragma once

#include <cstdint>

#include "engine.h"

namespace qs {

struct ScreenModel {
    char clock[16] = "";      // "23:41:05"（時刻が分からなければ "--:--:--"）
    char status[12] = "";     // "NORMAL" "SHAKE" "CAUTION" "WARNING" "SETUP" "CALIB" "FAULT"
    char shindo[4] = "";      // "0"〜"7"、"5-" "5+" "6-" "6+"（揺れていなければ空）
    char nowText[12] = "";    // "NOW 0.3"
    char maxText[12] = "";    // "MAX 2.6"
    bool alert = false;       // 白黒反転で強調する
    bool wifi = false;
    bool muted = false;
    uint8_t graph[kHistoryLen] = {};  // 0〜255（対数目盛）
};

// 揺れの大きさ（gal）を、グラフの高さ（0〜255、0.1〜300 gal を対数で）に
uint8_t graphLevel(float gal);
const char *shindoAscii(Shindo s);

struct ScreenInput {
    const Engine *engine = nullptr;
    int hour = -1, minute = -1, second = -1;
    bool wifi = false;
    bool setupMode = false;
    bool muted = false;
};

void buildScreen(const ScreenInput &in, ScreenModel &out);

}  // namespace qs
