// GooMergeCore.h — 「Goo Merge」の画像処理本体（After Effects に依存しない）
//
// レイヤーの形（アルファ）を、近いもの同士が水あめのようにくっつく形に作り直す。
//   1. 円で膨らませてから同じだけ縮める（クロージング）。Merge Distance より狭いすき間は
//      くびれた橋でつながり、へこんだ角は丸く埋まる。元の形が削られることはない。
//   2. Round Corners を上げると、出っ張った角も丸くする（細い部分は消えることがある）。
//   3. 色は元の色を残し、橋の部分だけ周りの色を混ぜて埋める。
//   4. Bubble モードでは、形の内側からの距離でドーム状の面を作り、縁と光の映り込みで泡に見せる。
// 複数のレイヤーをくっつけたいときは、調整レイヤーに適用するかプリコンポーズする。
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace goomerge {

struct Image {
    int w = 0, h = 0;
    std::vector<float> r, g, b, a;  // ストレートアルファ
    void resize(int W, int H) {
        w = W; h = H;
        const size_t n = size_t(W) * size_t(H);
        r.assign(n, 0.f); g.assign(n, 0.f); b.assign(n, 0.f); a.assign(n, 0.f);
    }
};

enum Style { STYLE_SOLID = 0, STYLE_BUBBLE };
enum ColorMode { COLOR_LAYER = 0, COLOR_FILL };

struct Params {
    float radius = 20.f;        // px この 2 倍までのすき間がつながる
    float roundCorners = 0.f;   // px 出っ張った角を丸める半径
    int style = STYLE_SOLID;
    int colorMode = COLOR_LAYER;
    float fill[3] = {1.f, 1.f, 1.f};
    // Bubble
    float bubbleOpacity = 0.25f;  // 0..1 中の不透明度
    float rimWidth = 6.f;         // px 縁の太さ
    float highlight = 0.8f;       // 0..1 光の映り込み
    float lightAngle = -45.f;     // 度（0＝右、-90＝上）
    int threads = 0;              // 0＝自動
};

inline float Reach(const Params&) { return 2.f; }

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

// 1 方向の箱ぼかし。半径は小数可（両端を小数の重みで足す）ので、Radius を動かしても段がつかない
inline void BoxPass(const std::vector<float>& in, std::vector<float>& out, int w, int h, float r, bool horiz, int threads) {
    out.assign(in.size(), 0.f);
    const int len = horiz ? w : h, lines = horiz ? h : w;
    const int ri = int(r);
    const float fr = r - float(ri);
    const float inv = 1.f / (2.f * r + 1.f);
    ParallelFor(lines, threads, [&](int l0, int l1) {
        std::vector<double> P(len + 1);
        std::vector<float> x(len);
        for (int l = l0; l < l1; ++l) {
            for (int k = 0; k < len; ++k) x[k] = in[horiz ? size_t(l) * w + k : size_t(k) * w + l];
            P[0] = 0;
            for (int k = 0; k < len; ++k) P[k + 1] = P[k] + x[k];
            auto sum = [&](int a, int b) -> double {  // x[a..b]（範囲外は 0）
                a = std::max(a, 0); b = std::min(b, len - 1);
                return a > b ? 0.0 : P[b + 1] - P[a];
            };
            auto at = [&](int k) -> float { return (k < 0 || k >= len) ? 0.f : x[k]; };
            for (int k = 0; k < len; ++k) {
                const double s = sum(k - ri, k + ri) + fr * (at(k - ri - 1) + at(k + ri + 1));
                out[horiz ? size_t(l) * w + k : size_t(k) * w + l] = float(s * inv);
            }
        }
    });
}

// 箱ぼかし 3 回でガウスぼかし（sigma）に近づける
inline std::vector<float> Blur(const std::vector<float>& src, int w, int h, float sigma, int threads) {
    if (sigma < 0.3f) return src;
    const float r = 0.5f * (std::sqrt(4.f * sigma * sigma + 1.f) - 1.f);
    std::vector<float> a = src, b;
    for (int i = 0; i < 3; ++i) {
        BoxPass(a, b, w, h, r, true, threads);
        BoxPass(b, a, w, h, r, false, threads);
    }
    return a;
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

// seed[i] が真の画素までの距離
inline std::vector<float> Distance(const std::vector<uint8_t>& seed, int w, int h) {
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
    for (auto& x : d) x = std::sqrt(x);
    return d;
}

}  // namespace detail

// src を出力 (W, H) の (ox, oy) に置いて処理する。dst は W×H
inline void Process(const Image& src, Image& dst, int W, int H, int ox, int oy, const Params& p) {
    using namespace detail;
    dst.resize(W, H);
    const size_t n = size_t(W) * H;
    if (n == 0) return;

    // 出力座標に並べ直した元画像（プリマルチ）
    std::vector<float> A(n, 0.f), R(n, 0.f), G(n, 0.f), B(n, 0.f);
    for (int y = 0; y < src.h; ++y) {
        const int ty = y + oy;
        if (ty < 0 || ty >= H) continue;
        for (int x = 0; x < src.w; ++x) {
            const int tx = x + ox;
            if (tx < 0 || tx >= W) continue;
            const size_t s = size_t(y) * src.w + x, t = size_t(ty) * W + tx;
            const float a = Clamp01(src.a[s]);
            A[t] = a; R[t] = src.r[s] * a; G[t] = src.g[s] * a; B[t] = src.b[s] * a;
        }
    }

    // 1. クロージング（距離変換で円の膨張・収縮）。距離の端数をそのまま縁のアンチエイリアスに使う
    const float rad = std::max(0.f, p.radius), rc = std::max(0.f, p.roundCorners);
    const float sigma = rad;  // 色を混ぜる範囲
    std::vector<uint8_t> shape(n), seed(n);
    for (size_t i = 0; i < n; ++i) shape[i] = A[i] >= 0.5f;
    std::vector<float> cov(n);
    {
        std::vector<float> dOut = Distance(shape, W, H);
        for (size_t i = 0; i < n; ++i) seed[i] = dOut[i] > rad;  // 膨らませた形の外
        std::vector<float> dIn = Distance(seed, W, H);           // 膨らませた形の外までの距離
        for (size_t i = 0; i < n; ++i) cov[i] = Clamp01(dIn[i] - rad + 0.5f);
        if (rc > 0.f) {
            // オープニング: さらに rc 縮めてから rc 膨らませる
            for (size_t i = 0; i < n; ++i) seed[i] = dIn[i] - rad >= rc;
            std::vector<float> dE = Distance(seed, W, H);
            for (size_t i = 0; i < n; ++i) cov[i] = Clamp01(rc - dE[i] + 0.5f);
        }
    }
    for (size_t i = 0; i < n; ++i) {
        // 元の縁のアンチエイリアスは元のまま（角を丸めるときは削れた所を除く）
        if (rc <= 0.f || cov[i] > 0.5f) cov[i] = std::max(cov[i], A[i]);
        if (cov[i] < 1e-3f) cov[i] = 0.f;
    }

    // 2. 色: 元の色を残し、足りないところは周りの色を混ぜる（近いほど強く）
    std::vector<float> cr, cg, cb;
    if (p.colorMode == COLOR_LAYER) {
        std::vector<float> s1 = Blur(R, W, H, std::max(1.f, sigma * 0.5f), p.threads);
        std::vector<float> s2 = Blur(G, W, H, std::max(1.f, sigma * 0.5f), p.threads);
        std::vector<float> s3 = Blur(B, W, H, std::max(1.f, sigma * 0.5f), p.threads);
        std::vector<float> sa = Blur(A, W, H, std::max(1.f, sigma * 0.5f), p.threads);
        std::vector<float> l1 = Blur(R, W, H, sigma * 1.5f, p.threads);
        std::vector<float> l2 = Blur(G, W, H, sigma * 1.5f, p.threads);
        std::vector<float> l3 = Blur(B, W, H, sigma * 1.5f, p.threads);
        std::vector<float> la = Blur(A, W, H, sigma * 1.5f, p.threads);
        cr.resize(n); cg.resize(n); cb.resize(n);
        for (size_t i = 0; i < n; ++i) {
            float r, g, b;
            if (sa[i] > 1e-3f) { r = s1[i] / sa[i]; g = s2[i] / sa[i]; b = s3[i] / sa[i]; }
            else if (la[i] > 1e-6f) { r = l1[i] / la[i]; g = l2[i] / la[i]; b = l3[i] / la[i]; }
            else { r = g = b = 0.f; }
            // 近くの色の重み（小さいぼかしが届かない所は大きいぼかしへ寄せる）
            const float k = Clamp01(sa[i] * 8.f);
            if (sa[i] > 1e-3f && la[i] > 1e-6f) {
                r = r * k + (l1[i] / la[i]) * (1.f - k);
                g = g * k + (l2[i] / la[i]) * (1.f - k);
                b = b * k + (l3[i] / la[i]) * (1.f - k);
            }
            const float a = A[i];
            const float or_ = a > 1e-4f ? R[i] / a : 0.f, og = a > 1e-4f ? G[i] / a : 0.f, ob = a > 1e-4f ? B[i] / a : 0.f;
            cr[i] = or_ * a + r * (1.f - a);
            cg[i] = og * a + g * (1.f - a);
            cb[i] = ob * a + b * (1.f - a);
        }
    }
    auto baseColor = [&](size_t i, float& r, float& g, float& b) {
        if (p.colorMode == COLOR_FILL) { r = p.fill[0]; g = p.fill[1]; b = p.fill[2]; }
        else { r = cr[i]; g = cg[i]; b = cb[i]; }
    };

    if (p.style == STYLE_SOLID) {
        for (size_t i = 0; i < n; ++i) {
            float r, g, b;
            baseColor(i, r, g, b);
            dst.r[i] = r; dst.g[i] = g; dst.b[i] = b; dst.a[i] = cov[i];
        }
        return;
    }

    // 3. Bubble: 内側からの距離でドームを作る
    std::vector<uint8_t> outside(n);
    for (size_t i = 0; i < n; ++i) outside[i] = cov[i] < 0.5f;
    const std::vector<float> D = Distance(outside, W, H);
    const float rim = std::max(0.5f, p.rimWidth);
    // ドームの高さの基準は、Radius と縁の太さのうち大きい方
    const float dome = std::max(rim * 2.f, std::max(4.f, p.radius));
    std::vector<float> hf(n);
    for (size_t i = 0; i < n; ++i) { const float d = std::min(D[i], dome) / dome; hf[i] = std::sqrt(std::max(0.f, 1.f - (1.f - d) * (1.f - d))); }
    hf = Blur(hf, W, H, dome * 0.25f, p.threads);  // 角の折れ目で光が筋にならないように
    auto height = [&](int x, int y) -> float {
        x = std::min(std::max(x, 0), W - 1); y = std::min(std::max(y, 0), H - 1);
        return hf[size_t(y) * W + x];
    };
    const float la = p.lightAngle * 3.14159265f / 180.f;
    float L[3] = {std::cos(la) * 0.6f, std::sin(la) * 0.6f, 0.8f};
    float H3[3] = {L[0], L[1], L[2] + 1.f};
    const float hn = std::sqrt(H3[0] * H3[0] + H3[1] * H3[1] + H3[2] * H3[2]);
    for (float& v : H3) v /= hn;
    const float bo = Clamp01(p.bubbleOpacity), hl = Clamp01(p.highlight);
    ParallelFor(H, p.threads, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < W; ++x) {
                const size_t i = size_t(y) * W + x;
                if (cov[i] <= 0.f) continue;
                float r, g, b;
                baseColor(i, r, g, b);
                // 縁: 外周から rim まで濃く、内側へなめらかに薄く
                const float d = D[i];
                const float rimK = 1.f - Clamp01((d - rim) / std::max(1.f, rim * 0.5f) + 0.5f);
                // 光の映り込み: ドームの法線と半ベクトルで鋭いハイライト
                const float nx = -(height(x + 1, y) - height(x - 1, y)) * dome * 0.5f;
                const float ny = -(height(x, y + 1) - height(x, y - 1)) * dome * 0.5f;
                const float nl = std::sqrt(nx * nx + ny * ny + 1.f);
                const float nh = std::max(0.f, (nx * H3[0] + ny * H3[1] + H3[2]) / nl);
                const float spec = hl * Clamp01((std::pow(nh, 60.f) - 0.25f) * 1.6f);
                const float body = bo + (1.f - bo) * rimK;
                const float a = Clamp01(body + spec * (1.f - body));
                const float wgt = a > 1e-4f ? spec / a : 0.f;
                dst.r[i] = r * (1.f - wgt) + wgt;
                dst.g[i] = g * (1.f - wgt) + wgt;
                dst.b[i] = b * (1.f - wgt) + wgt;
                dst.a[i] = a * cov[i];
            }
    });
}

}  // namespace goomerge
