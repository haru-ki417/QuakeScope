// 開発用のコマンド: 合成した揺れを CSV で出す／CSV の揺れの計測震度を求める
//   qs_cli synth <near|far|door|steps|truck|quiet> <計測震度> <seed> <秒>   > wave.csv
//   qs_cli intensity < wave.csv
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "intensity.h"
#include "synth.h"

using namespace qs;

int main(int argc, char **argv) {
    if (argc >= 2 && std::strcmp(argv[1], "synth") == 0 && argc >= 6) {
        SynthParams p;
        const std::string k = argv[2];
        p.kind = k == "near" ? SynthKind::NearQuake : k == "far" ? SynthKind::FarQuake : k == "door" ? SynthKind::DoorSlam
               : k == "steps" ? SynthKind::Footsteps : k == "truck" ? SynthKind::Truck : SynthKind::Quiet;
        p.targetIntensity = std::atof(argv[3]);
        p.seed = static_cast<uint32_t>(std::atoi(argv[4]));
        p.seconds = std::atof(argv[5]);
        const SynthSignal s = synthesize(p);
        std::printf("x,y,z\n");
        for (size_t i = 0; i < s.x.size(); ++i) std::printf("%.5f,%.5f,%.5f\n", s.x[i], s.y[i], s.z[i]);
        return 0;
    }
    if (argc >= 2 && std::strcmp(argv[1], "intensity") == 0) {
        std::vector<float> x, y, z;
        std::string line;
        std::getline(std::cin, line);
        while (std::getline(std::cin, line)) {
            float a, b, c;
            if (std::sscanf(line.c_str(), "%f,%f,%f", &a, &b, &c) == 3) { x.push_back(a); y.push_back(b); z.push_back(c); }
        }
        const ExactResult r = computeJmaIntensity(x.data(), y.data(), z.data(), x.size(), 100);
        std::printf("%.6f %.6f %.6f %s %.3f\n", r.raw, r.intensity, r.a03, shindoLabel(r.shindo),
                    realtimeIntensityOf(x.data(), y.data(), z.data(), x.size()));
        return 0;
    }
    std::fprintf(stderr, "usage: qs_cli synth <kind> <I> <seed> <sec> | qs_cli intensity < wave.csv\n");
    return 2;
}
