// goomerge_cli.cpp — AE なしでコア処理を試すコマンドラインツール。
// 入出力は簡単な raw 形式: "IPRF" + int32 w + int32 h + float32 RGBA(ストレート, 行優先)。
//
// 使い方: goomerge_cli in.iprf out.iprf [key=value ...]
//   radius= round= style=0|1 color=0|1 fill=r,g,b
//   bopacity= rim= highlight= light=
//   出力は四辺を Reach の分だけ広げる
#include "../src/GooMergeCore.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static bool Load(const char* path, goomerge::Image& img) {
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

static bool Save(const char* path, const goomerge::Image& img) {
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
    goomerge::Params p;
    for (int i = 3; i < argc; ++i) {
        std::string kv = argv[i];
        const auto eq = kv.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
        const float f = std::strtof(v.c_str(), nullptr);
        if (k == "radius") p.radius = f;
        else if (k == "round") p.roundCorners = f;
        else if (k == "style") p.style = int(f);
        else if (k == "color") p.colorMode = int(f);
        else if (k == "bopacity") p.bubbleOpacity = f;
        else if (k == "rim") p.rimWidth = f;
        else if (k == "highlight") p.highlight = f;
        else if (k == "light") p.lightAngle = f;
        else if (k == "fill") std::sscanf(v.c_str(), "%f,%f,%f", &p.fill[0], &p.fill[1], &p.fill[2]);
    }
    goomerge::Image src, dst;
    if (!Load(argv[1], src)) { std::fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    const auto t0 = std::chrono::steady_clock::now();
    const int m = int(std::ceil(goomerge::Reach(p)));
    goomerge::Process(src, dst, src.w + 2 * m, src.h + 2 * m, m, m, p);
    const auto t1 = std::chrono::steady_clock::now();
    std::fprintf(stderr, "%dx%d: %.1f ms\n", src.w, src.h, std::chrono::duration<double, std::milli>(t1 - t0).count());
    return Save(argv[2], dst) ? 0 : 1;
}
