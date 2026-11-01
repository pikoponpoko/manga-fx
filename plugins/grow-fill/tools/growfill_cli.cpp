// growfill_cli.cpp — AE なしでコア処理を試すコマンドラインツール。
// 入出力は簡単な raw 形式: "IPRF" + int32 w + int32 h + float32 RGBA(ストレート, 行優先)。
//
// 使い方: growfill_cli in.iprf out.iprf [key=value ...]
//   detect=0|1|2 threshold= gaps= target=0|1 outside=0|1 minarea= ox= oy= progress= stagger=
//   soft= wobble= wsize= evo= under= color=r,g,b opacity= output=0|1
#include "../src/GrowFillCore.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static bool Load(const char* path, growfill::Image& img) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    char magic[4]; int32_t wh[2];
    if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "IPRF", 4) || std::fread(wh, 4, 2, f) != 2) { std::fclose(f); return false; }
    img.resize(wh[0], wh[1]);
    std::vector<float> px(size_t(wh[0]) * wh[1] * 4);
    const bool ok = std::fread(px.data(), 4, px.size(), f) == px.size();
    std::fclose(f);
    for (size_t i = 0; i < img.r.size(); ++i) { img.r[i] = px[i*4]; img.g[i] = px[i*4+1]; img.b[i] = px[i*4+2]; img.a[i] = px[i*4+3]; }
    return ok;
}

static bool Save(const char* path, const growfill::Image& img) {
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    int32_t wh[2] = {img.w, img.h};
    std::fwrite("IPRF", 1, 4, f); std::fwrite(wh, 4, 2, f);
    std::vector<float> px(img.r.size() * 4);
    for (size_t i = 0; i < img.r.size(); ++i) { px[i*4] = img.r[i]; px[i*4+1] = img.g[i]; px[i*4+2] = img.b[i]; px[i*4+3] = img.a[i]; }
    std::fwrite(px.data(), 4, px.size(), f);
    std::fclose(f);
    return true;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: %s in.iprf out.iprf [key=value ...]\n", argv[0]); return 1; }
    growfill::Params p;
    for (int i = 3; i < argc; ++i) {
        std::string kv = argv[i];
        const auto eq = kv.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
        const float f = std::strtof(v.c_str(), nullptr);
        if (k == "detect") p.detect = int(f);
        else if (k == "threshold") p.lineThreshold = f;
        else if (k == "gaps") p.closeGaps = f;
        else if (k == "target") p.target = int(f);
        else if (k == "outside") p.includeOutside = f != 0.f;
        else if (k == "minarea") p.minArea = f;
        else if (k == "ox") p.originX = f;
        else if (k == "oy") p.originY = f;
        else if (k == "progress") p.progress = f;
        else if (k == "stagger") p.stagger = f;
        else if (k == "soft") p.softness = f;
        else if (k == "wobble") p.wobble = f;
        else if (k == "wsize") p.wobbleSize = f;
        else if (k == "evo") p.evolution = f;
        else if (k == "under") p.underLines = f;
        else if (k == "opacity") p.opacity = f;
        else if (k == "output") p.output = int(f);
        else if (k == "color") std::sscanf(v.c_str(), "%f,%f,%f", &p.color[0], &p.color[1], &p.color[2]);
    }
    growfill::Image src, dst;
    if (!Load(argv[1], src)) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    const auto t0 = std::chrono::steady_clock::now();
    growfill::Process(src, dst, p);
    const auto t1 = std::chrono::steady_clock::now();
    std::fprintf(stderr, "%dx%d: %.1f ms\n", src.w, src.h, std::chrono::duration<double, std::milli>(t1 - t0).count());
    return Save(argv[2], dst) ? 0 : 1;
}
