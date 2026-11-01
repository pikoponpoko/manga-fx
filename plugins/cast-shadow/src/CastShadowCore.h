// CastShadowCore.h — 「Cast Shadow」の画像処理本体（After Effects に依存しない）
//
// レイヤーの形（アルファ）から、光源と反対の方向へ伸びる影を作る。
//   出力の各画素から光源の方向へ Length まで形を探し、見つかった形の濃さを影の濃さにする。
//   光源から遠ざかるほど薄く（Fade）、ぼけて（Softness）いく。
//   Point＝光源の位置から放射状に伸びる / Directional＝全体が同じ向きに伸びる
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace castshadow {

struct Image {
    int w = 0, h = 0;
    std::vector<float> r, g, b, a;  // ストレートアルファ
    void resize(int W, int H) {
        w = W; h = H;
        const size_t n = size_t(W) * size_t(H);
        r.assign(n, 0.f); g.assign(n, 0.f); b.assign(n, 0.f); a.assign(n, 0.f);
    }
};

enum LightType { LIGHT_POINT = 0, LIGHT_DIRECTIONAL };
enum Output { OUTPUT_BEHIND = 0, OUTPUT_SHADOW_ONLY };

struct Params {
    int light = LIGHT_POINT;
    float lightX = 0.f, lightY = 0.f;  // 入力画像の画素座標
    float angle = 135.f;               // 度。影が伸びる向き（0＝右、90＝下）
    float length = 120.f;              // px
    float start = 0.f;                 // px 形から離して影を始める
    float fade = 1.f;                  // 0..1 先端でどれだけ薄くなるか
    float softness = 0.f;              // px 先端でのぼかし半径
    float color[3] = {0.f, 0.f, 0.f};
    float opacity = 0.6f;
    int output = OUTPUT_BEHIND;
    int threads = 0;                   // 0＝自動
};

namespace detail {

inline float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

inline void ParallelFor(int count, int threads, const std::function<void(int, int)>& fn) {
    int t = threads > 0 ? threads : int(std::thread::hardware_concurrency());
    t = std::max(1, std::min(t, std::min(count, 32)));
    if (t <= 1) { fn(0, count); return; }
    std::vector<std::thread> pool;
    const int chunk = (count + t - 1) / t;
    for (int i = 0; i < t; ++i) {
        const int b = i * chunk, e = std::min(count, b + chunk);
        if (b < e) pool.emplace_back(fn, b, e);
    }
    for (auto& th : pool) th.join();
}

// 1 次元の二乗距離変換（Felzenszwalb & Huttenlocher）
inline void Edt1D(const float* f, float* d, int n, std::vector<int>& v, std::vector<float>& z) {
    const float INF = 1e20f;
    v.resize(n); z.resize(n + 1);
    int k = 0;
    v[0] = 0; z[0] = -INF; z[1] = INF;
    for (int q = 1; q < n; ++q) {
        float s;
        for (;;) {
            const int p = v[k];
            s = ((f[q] + float(q) * q) - (f[p] + float(p) * p)) / (2.f * (q - p));
            if (s <= z[k] && k > 0) { --k; continue; }
            break;
        }
        if (s <= z[k]) { v[0] = q; z[0] = -INF; z[1] = INF; k = 0; continue; }
        ++k; v[k] = q; z[k] = s; z[k + 1] = INF;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[k + 1] < q) ++k;
        const float dq = float(q - v[k]);
        d[q] = dq * dq + f[v[k]];
    }
}

// seed[i] が真の画素までの二乗距離
inline std::vector<float> SquaredDistance(const std::vector<uint8_t>& seed, int w, int h) {
    const float INF = 1e20f;
    std::vector<float> d(size_t(w) * h);
    for (size_t i = 0; i < d.size(); ++i) d[i] = seed[i] ? 0.f : INF;
    std::vector<float> f(std::max(w, h)), o(std::max(w, h));
    std::vector<int> v; std::vector<float> z;
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y) f[y] = d[size_t(y) * w + x];
        Edt1D(f.data(), o.data(), h, v, z);
        for (int y = 0; y < h; ++y) d[size_t(y) * w + x] = o[y];
    }
    for (int y = 0; y < h; ++y) {
        float* row = &d[size_t(y) * w];
        std::copy(row, row + w, f.begin());
        Edt1D(f.data(), row, w, v, z);
    }
    return d;
}

// 1 方向の箱ぼかし（半径 r）。端の外は 0
inline void BoxPass(const std::vector<float>& in, std::vector<float>& out, int w, int h, int r, bool horiz) {
    out.assign(in.size(), 0.f);
    const int len = horiz ? w : h, lines = horiz ? h : w;
    const float inv = 1.f / float(2 * r + 1);
    for (int l = 0; l < lines; ++l) {
        auto at = [&](int k) -> float { return (k < 0 || k >= len) ? 0.f : in[horiz ? size_t(l) * w + k : size_t(k) * w + l]; };
        float s = 0.f;
        for (int k = -r; k <= r; ++k) s += at(k);
        for (int k = 0; k < len; ++k) {
            out[horiz ? size_t(l) * w + k : size_t(k) * w + l] = s * inv;
            s += at(k + r + 1) - at(k - r);
        }
    }
}

// 箱ぼかし 3 回でガウスぼかしに近づける（sigma ≒ radius / 2）
inline std::vector<float> Blur(const std::vector<float>& src, int w, int h, float radius) {
    if (radius < 0.5f) return src;
    const int r = std::max(1, int(std::lround(radius / 2.f)));
    std::vector<float> a = src, b;
    for (int i = 0; i < 3; ++i) { BoxPass(a, b, w, h, r, true); BoxPass(b, a, w, h, r, false); }
    return a;
}

inline float Sample(const std::vector<float>& m, int w, int h, float x, float y) {
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const float fx = x - x0, fy = y - y0;
    auto at = [&](int xx, int yy) -> float { return (xx < 0 || yy < 0 || xx >= w || yy >= h) ? 0.f : m[size_t(yy) * w + xx]; };
    const float a = at(x0, y0) + (at(x0 + 1, y0) - at(x0, y0)) * fx;
    const float b = at(x0, y0 + 1) + (at(x0 + 1, y0 + 1) - at(x0, y0 + 1)) * fx;
    return a + (b - a) * fy;
}

}  // namespace detail

// 影がレイヤーの外へはみ出す最大距離（px）
inline float Reach(const Params& p) { return std::max(0.f, p.start) + std::max(0.f, p.length) + 2.f * std::max(0.f, p.softness) + 2.f; }

// dst は outW×outH。dst の (x, y) は src の (x - ox, y - oy) に当たる
inline void Process(const Image& src, Image& dst, int outW, int outH, int ox, int oy, const Params& p) {
    using namespace detail;
    dst.resize(outW, outH);
    const int w = src.w, h = src.h;
    if (w <= 0 || h <= 0 || outW <= 0 || outH <= 0) return;

    // ぼかし段階ごとのアルファ（先端に近いほどぼけたものを使う）
    const int levels = p.softness >= 0.5f ? 4 : 1;
    std::vector<std::vector<float>> alpha(levels);
    alpha[0] = src.a;
    for (auto& v : alpha[0]) v = Clamp01(v);
    for (int k = 1; k < levels; ++k) alpha[k] = Blur(alpha[0], w, h, p.softness * float(k) / float(levels - 1));

    // 形までの距離。何もない所は一気に進む
    std::vector<float> empty;
    {
        std::vector<uint8_t> seed(size_t(w) * h);
        const std::vector<float>& widest = alpha[levels - 1];
        for (size_t i = 0; i < seed.size(); ++i) seed[i] = widest[i] > 1e-4f;
        empty = SquaredDistance(seed, w, h);
        for (auto& v : empty) v = std::sqrt(v);
    }

    const float L = std::max(0.f, p.length), S = std::max(0.f, p.start);
    const float step = std::max(0.5f, L / 400.f);
    const float rad = p.angle * 3.14159265f / 180.f;
    const float ddx = -std::cos(rad), ddy = -std::sin(rad);  // 光源へ向かう向き
    const float fade = Clamp01(p.fade);

    std::vector<float> shadow(size_t(outW) * outH, 0.f);
    ParallelFor(outH, p.threads, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            for (int x = 0; x < outW; ++x) {
                const float sx = float(x - ox), sy = float(y - oy);
                float dx = ddx, dy = ddy;
                float maxT = S + L;
                if (p.light == LIGHT_POINT) {
                    dx = p.lightX - sx; dy = p.lightY - sy;
                    const float len = std::sqrt(dx * dx + dy * dy);
                    if (len < 1e-3f) continue;
                    dx /= len; dy /= len;
                    maxT = std::min(maxT, len);  // 光源より向こうは探さない
                }
                float best = 0.f;
                for (float t = S; t <= maxT;) {
                    const float px = sx + dx * t, py = sy + dy * t;
                    // 画像の外は画像の端まで、何もない所は形の近くまで飛ばす
                    float skip = 0.f;
                    if (px < 0.f || py < 0.f || px > w - 1.f || py > h - 1.f) {
                        const float ex = std::max({-px, px - (w - 1.f), 0.f}), ey = std::max({-py, py - (h - 1.f), 0.f});
                        skip = std::max(ex, ey) - 1.f;
                        if ((px < 0.f && dx <= 0.f) || (px > w - 1.f && dx >= 0.f) ||
                            (py < 0.f && dy <= 0.f) || (py > h - 1.f && dy >= 0.f)) break;  // 遠ざかる一方
                    } else {
                        skip = empty[size_t(int(py)) * w + int(px)] - 1.5f;
                    }
                    if (skip > step) { t += skip; continue; }
                    const float u = L > 0.f ? (t - S) / L : 0.f;
                    const float weight = 1.f - fade * u;
                    if (weight <= best) break;  // これ以上濃くならない
                    float a;
                    if (levels == 1) {
                        a = Sample(alpha[0], w, h, px, py);
                    } else {
                        const float lv = u * float(levels - 1);
                        const int k = std::min(levels - 2, int(lv));
                        const float f = lv - float(k);
                        const float a0 = Sample(alpha[k], w, h, px, py);
                        const float a1 = Sample(alpha[k + 1], w, h, px, py);
                        a = a0 + (a1 - a0) * f;
                    }
                    best = std::max(best, a * weight);
                    t += step;
                }
                shadow[size_t(y) * outW + x] = best * Clamp01(p.opacity);
            }
        }
    });

    for (int y = 0; y < outH; ++y) {
        for (int x = 0; x < outW; ++x) {
            const size_t o = size_t(y) * outW + x;
            const float sh = shadow[o];
            const int sx = x - ox, sy = y - oy;
            const bool in = sx >= 0 && sy >= 0 && sx < w && sy < h;
            const size_t i = in ? size_t(sy) * w + sx : 0;
            const float la = in ? Clamp01(src.a[i]) : 0.f;
            if (p.output == OUTPUT_SHADOW_ONLY) {
                // 形の上には影を出さない（形で切り抜く）
                dst.r[o] = p.color[0]; dst.g[o] = p.color[1]; dst.b[o] = p.color[2];
                dst.a[o] = sh * (1.f - la);
                continue;
            }
            const float oa = la + sh * (1.f - la);
            dst.a[o] = oa;
            if (oa <= 0.f) continue;
            const float lr = in ? src.r[i] : 0.f, lg = in ? src.g[i] : 0.f, lb = in ? src.b[i] : 0.f;
            dst.r[o] = (lr * la + p.color[0] * sh * (1.f - la)) / oa;
            dst.g[o] = (lg * la + p.color[1] * sh * (1.f - la)) / oa;
            dst.b[o] = (lb * la + p.color[2] * sh * (1.f - la)) / oa;
        }
    }
}

}  // namespace castshadow
