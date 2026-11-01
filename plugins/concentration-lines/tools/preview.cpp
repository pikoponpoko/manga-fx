// preview.cpp — AE なしで描画コアを確認するためのツール
// 使い方: preview in.rgba out.rgba width height key=value ...
//   in.rgba / out.rgba は 8bit RGBA の生データ（ストレートアルファ）
//   key: type cx cy rx ry angle(度) count width wj sj stj taper length lj feather seed mode
//        sharp(0..1) soft(px) color=RRGGBB opacity(0..1) scale
//        アニメーション: t(秒) flicker rot(度/秒) pulse(割合) pulserate flow(px/秒)
//        がたつき: wobble(px) wobblesize(px) uneven(0..1)
//        shape=1: 入力画像の不透明部分の形に空白を合わせる  margin(px) smooth(0..0.25)
#include "../src/ConcentrationLinesCore.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 5) { std::fprintf(stderr, "usage: preview in out w h [k=v...]\n"); return 1; }
    const int w = std::atoi(argv[3]), h = std::atoi(argv[4]);
    std::vector<unsigned char> img((size_t)w * h * 4);
    FILE* f = std::fopen(argv[1], "rb");
    if (!f || std::fread(img.data(), 1, img.size(), f) != img.size()) { std::fprintf(stderr, "read error\n"); return 1; }
    std::fclose(f);

    cl::Settings s;
    s.cx = w * 0.5; s.cy = h * 0.5;
    s.hasFrame = true; s.frameX1 = w; s.frameY1 = h;
    int mode = cl::COMPOSITE_OVER;
    cl::RGBA color{0, 0, 0, 1};
    cl::Animation anim;
    double t = 0.0;
    bool useShape = false;
    for (int i = 5; i < argc; ++i) {
        std::string kv = argv[i];
        size_t eq = kv.find('=');
        if (eq == std::string::npos) continue;
        std::string k = kv.substr(0, eq);
        const char* v = kv.c_str() + eq + 1;
        double d = std::atof(v);
        if (k == "type") s.type = (int)d;
        else if (k == "cx") s.cx = d;
        else if (k == "cy") s.cy = d;
        else if (k == "rx") s.innerRx = d;
        else if (k == "ry") s.innerRy = d;
        else if (k == "angle") s.angle = d * cl::kPi / 180.0;
        else if (k == "count") s.count = (int)d;
        else if (k == "width") s.width = d;
        else if (k == "wj") s.widthJitter = d;
        else if (k == "sj") s.spacingJitter = d;
        else if (k == "stj") s.startJitter = d;
        else if (k == "taper") s.taper = d;  // 0..1
        else if (k == "length") s.length = d;
        else if (k == "lj") s.lengthJitter = d;
        else if (k == "feather") s.feather = d;
        else if (k == "seed") s.seed = (uint32_t)d;
        else if (k == "scale") s.scale = d;
        else if (k == "sharp") s.sharpness = d;
        else if (k == "soft") s.softness = d;
        else if (k == "t") t = d;
        else if (k == "wobble") s.wobble = d;
        else if (k == "wobblesize") s.wobbleSize = d;
        else if (k == "uneven") s.unevenness = d;
        else if (k == "shape") useShape = d != 0.0;
        else if (k == "margin") s.shapeMargin = d;
        else if (k == "smooth") s.shapeSmooth = d;
        else if (k == "flicker") anim.flickerRate = d;
        else if (k == "rot") anim.rotationSpeed = d;
        else if (k == "pulse") anim.pulseAmount = d;
        else if (k == "pulserate") anim.pulseRate = d;
        else if (k == "flow") anim.flowSpeed = d;
        else if (k == "mode") mode = (int)d;
        else if (k == "opacity") color.a = d;
        else if (k == "color") {
            unsigned c = (unsigned)std::strtoul(v, nullptr, 16);
            color.r = ((c >> 16) & 255) / 255.0; color.g = ((c >> 8) & 255) / 255.0; color.b = (c & 255) / 255.0;
        }
    }

    if (useShape) {
        // プラグインと同じく、中心から各方向へ形の一番外側を探す
        const std::vector<unsigned char>* src = &img;
        const double maxR = std::sqrt((double)w * w + (double)h * h);
        const double cx = s.cx, cy = s.cy;
        s.shapeDistance = [=](double ux, double uy) {
            return cl::outermostHit(cx, cy, ux, uy, maxR, 2.0, [&](double x, double y) {
                const int ix = (int)x, iy = (int)y;
                if (x < 0 || y < 0 || ix >= w || iy >= h) return 0.0;
                return (*src)[((size_t)iy * w + ix) * 4 + 3] / 255.0;
            });
        };
    }
    cl::Renderer r(cl::animate(s, anim, t));
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned char* p = &img[((size_t)y * w + x) * 4];
            cl::RGBA src{p[0] / 255.0, p[1] / 255.0, p[2] / 255.0, p[3] / 255.0};
            cl::RGBA o = cl::composite(src, color, r.coverage(x, y), mode);
            p[0] = (unsigned char)(cl::clamp01(o.r) * 255 + 0.5);
            p[1] = (unsigned char)(cl::clamp01(o.g) * 255 + 0.5);
            p[2] = (unsigned char)(cl::clamp01(o.b) * 255 + 0.5);
            p[3] = (unsigned char)(cl::clamp01(o.a) * 255 + 0.5);
        }
    f = std::fopen(argv[2], "wb");
    std::fwrite(img.data(), 1, img.size(), f);
    std::fclose(f);
    return 0;
}
