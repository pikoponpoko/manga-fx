// GrowFillCore.h — 「Grow Fill」の画像処理本体（After Effects に依存しない）
//
// 線画で囲まれた領域を見つけ、起点から領域の形に沿って塗りが広がっていく
// アニメーションを作る。
//   1. 線の濃さマスクを作り、しきい値以上を「壁」にする（Close Gaps で壁を太らせて小さな切れ目を閉じる）
//   2. 壁でない画素を 4 連結で領域に分ける
//   3. 各領域で起点に一番近い画素から、領域の中だけを通る距離（測地距離）を測る
//   4. Progress に合わせて距離の前線を進め、前線より内側を塗る（Wobble で前線を揺らす）
//   5. 塗りを線の下まで広げ、線画の下に合成する
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <queue>
#include <vector>

namespace growfill {

struct Image {
    int w = 0, h = 0;
    std::vector<float> r, g, b, a;  // ストレートアルファ
    void resize(int W, int H) {
        w = W; h = H;
        const size_t n = size_t(W) * size_t(H);
        r.assign(n, 0.f); g.assign(n, 0.f); b.assign(n, 0.f); a.assign(n, 0.f);
    }
};

enum Detect { DETECT_ALPHA = 0, DETECT_DARK, DETECT_BRIGHT };
enum Target { TARGET_ALL = 0, TARGET_AT_ORIGIN };
enum Output { OUTPUT_COMPOSITE = 0, OUTPUT_FILL_ONLY };

struct Params {
    int detect = DETECT_DARK;
    float lineThreshold = 0.5f;   // 0..1
    float closeGaps = 2.f;        // px
    int target = TARGET_ALL;
    bool includeOutside = false;  // 画像の端に触れる領域（外側）も塗る
    float minArea = 10.f;         // px²。これより小さい領域は塗らない
    float originX = 0.f, originY = 0.f;  // 入力画像の画素座標
    float progress = 1.f;         // 0..1
    float stagger = 0.f;          // 0..1 起点から遠い領域ほど遅れて始まる
    float softness = 1.5f;        // px 前線のぼかし幅
    float wobble = 0.f;           // px 前線の揺れ
    float wobbleSize = 24.f;      // px 揺れの模様の大きさ
    float evolution = 0.f;        // 揺れの模様の変化
    float underLines = 2.f;       // px 線の下に塗りを広げる量
    float color[3] = {1.f, 0.35f, 0.3f};
    float opacity = 1.f;
    int output = OUTPUT_COMPOSITE;
};

namespace detail {

inline float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

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

inline uint32_t Hash(int x, int y, int z) {
    uint32_t hsh = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u + uint32_t(z) * 2147483647u;
    hsh = (hsh ^ (hsh >> 13)) * 1274126177u;
    return hsh ^ (hsh >> 16);
}
inline float Lattice(int x, int y, int z) { return (Hash(x, y, z) & 0xffffff) / float(0xffffff); }
inline float Smooth(float t) { return t * t * (3.f - 2.f * t); }

// 0..1 の値ノイズ（z 方向にも補間して Evolution で滑らかに変化させる）
inline float ValueNoise(float x, float y, float z) {
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y)), z0 = int(std::floor(z));
    const float fx = Smooth(x - x0), fy = Smooth(y - y0), fz = Smooth(z - z0);
    float c[2];
    for (int k = 0; k < 2; ++k) {
        const float a = Lattice(x0, y0, z0 + k) + (Lattice(x0 + 1, y0, z0 + k) - Lattice(x0, y0, z0 + k)) * fx;
        const float b = Lattice(x0, y0 + 1, z0 + k) + (Lattice(x0 + 1, y0 + 1, z0 + k) - Lattice(x0, y0 + 1, z0 + k)) * fx;
        c[k] = a + (b - a) * fy;
    }
    return c[0] + (c[1] - c[0]) * fz;
}

}  // namespace detail

inline void Process(const Image& src, Image& dst, const Params& p) {
    using namespace detail;
    const int w = src.w, h = src.h;
    const size_t n = size_t(w) * size_t(h);
    dst = src;
    if (w <= 0 || h <= 0) return;

    // 1. 線の濃さと壁
    std::vector<float> line(n);
    std::vector<uint8_t> wall(n);
    for (size_t i = 0; i < n; ++i) {
        const float a = Clamp01(src.a[i]);
        const float lum = 0.299f * src.r[i] + 0.587f * src.g[i] + 0.114f * src.b[i];
        float m;
        switch (p.detect) {
            case DETECT_ALPHA: m = a; break;
            case DETECT_BRIGHT: m = Clamp01(lum) * a; break;
            default: m = (1.f - Clamp01(lum)) * a; break;
        }
        line[i] = m;
        wall[i] = m >= p.lineThreshold;
    }
    if (p.closeGaps > 0.f) {
        const std::vector<float> d2 = SquaredDistance(wall, w, h);
        const float r2 = p.closeGaps * p.closeGaps;
        for (size_t i = 0; i < n; ++i) wall[i] = d2[i] <= r2;
    }

    // 2. 領域分け（4 連結）
    std::vector<int> label(n, -1);
    struct Region { int area = 0; bool border = false; int seed = -1; float seedDist2 = 1e30f; };
    std::vector<Region> regions;
    {
        std::vector<int> stack;
        for (size_t s = 0; s < n; ++s) {
            if (wall[s] || label[s] >= 0) continue;
            const int id = int(regions.size());
            regions.emplace_back();
            Region& rg = regions.back();
            label[s] = id;
            stack.push_back(int(s));
            while (!stack.empty()) {
                const int i = stack.back(); stack.pop_back();
                const int x = i % w, y = i / w;
                ++rg.area;
                if (x == 0 || y == 0 || x == w - 1 || y == h - 1) rg.border = true;
                const float dx = x - p.originX, dy = y - p.originY, dd = dx * dx + dy * dy;
                if (dd < rg.seedDist2) { rg.seedDist2 = dd; rg.seed = i; }
                const int nb[4] = {x > 0 ? i - 1 : -1, x < w - 1 ? i + 1 : -1, y > 0 ? i - w : -1, y < h - 1 ? i + w : -1};
                for (int j : nb) {
                    if (j < 0 || wall[j] || label[j] >= 0) continue;
                    label[j] = id;
                    stack.push_back(j);
                }
            }
        }
    }

    // 塗る領域を選ぶ
    std::vector<uint8_t> active(regions.size(), 0);
    for (size_t k = 0; k < regions.size(); ++k)
        active[k] = regions[k].area >= p.minArea && (p.includeOutside || !regions[k].border);
    if (p.target == TARGET_AT_ORIGIN) {
        const int ox = int(std::floor(p.originX + 0.5f)), oy = int(std::floor(p.originY + 0.5f));
        int pick = -1;
        if (ox >= 0 && oy >= 0 && ox < w && oy < h) pick = label[size_t(oy) * w + ox];
        if (pick < 0 || !active[pick]) {
            pick = -1;
            for (size_t k = 0; k < regions.size(); ++k)
                if (active[k] && (pick < 0 || regions[k].seedDist2 < regions[pick].seedDist2)) pick = int(k);
        }
        std::fill(active.begin(), active.end(), 0);
        if (pick >= 0) active[pick] = 1;
    }

    // 3. 領域の中を通る距離（8 近傍 + 桂馬で前線を丸くする）
    const float INF = 1e30f;
    std::vector<float> dist(n, INF);
    {
        using Item = std::pair<float, int>;
        std::priority_queue<Item, std::vector<Item>, std::greater<Item>> pq;
        for (size_t k = 0; k < regions.size(); ++k)
            if (active[k]) { dist[regions[k].seed] = 0.f; pq.push({0.f, regions[k].seed}); }
        struct Step { int dx, dy; float cost; };
        static const Step steps[16] = {
            {1, 0, 1.f}, {-1, 0, 1.f}, {0, 1, 1.f}, {0, -1, 1.f},
            {1, 1, 1.41421356f}, {-1, 1, 1.41421356f}, {1, -1, 1.41421356f}, {-1, -1, 1.41421356f},
            {2, 1, 2.23606798f}, {-2, 1, 2.23606798f}, {2, -1, 2.23606798f}, {-2, -1, 2.23606798f},
            {1, 2, 2.23606798f}, {-1, 2, 2.23606798f}, {1, -2, 2.23606798f}, {-1, -2, 2.23606798f}};
        while (!pq.empty()) {
            const Item it = pq.top(); pq.pop();
            const int i = it.second;
            if (it.first > dist[i]) continue;
            const int x = i % w, y = i / w, lb = label[i];
            for (const Step& s : steps) {
                const int nx = x + s.dx, ny = y + s.dy;
                if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                const int j = ny * w + nx;
                if (label[j] != lb) continue;
                if (std::abs(s.dx) + std::abs(s.dy) == 3) {
                    // 桂馬は途中の 2 画素も同じ領域のときだけ（細い壁を飛び越えない）
                    const int ax = x + s.dx / 2, ay = y + s.dy / 2;
                    const int bx = x + (s.dx - s.dx / 2), by = y + (s.dy - s.dy / 2);
                    if (label[ay * w + ax] != lb || label[by * w + bx] != lb) continue;
                }
                const float nd = it.first + s.cost;
                if (nd < dist[j]) { dist[j] = nd; pq.push({nd, j}); }
            }
        }
    }

    // 4. 前線を進める
    std::vector<float> maxD(regions.size(), 0.f);
    for (size_t i = 0; i < n; ++i)
        if (label[i] >= 0 && active[label[i]] && dist[i] < INF) maxD[label[i]] = std::max(maxD[label[i]], dist[i]);
    float maxSeed = 0.f;
    for (size_t k = 0; k < regions.size(); ++k)
        if (active[k]) maxSeed = std::max(maxSeed, std::sqrt(regions[k].seedDist2));
    const float stagger = Clamp01(p.stagger);
    const float progress = Clamp01(p.progress);
    const float soft = std::max(0.75f, p.softness);
    const float wob = std::max(0.f, p.wobble);
    std::vector<float> front(regions.size(), -1.f);
    for (size_t k = 0; k < regions.size(); ++k) {
        if (!active[k]) continue;
        const float start = maxSeed > 0.f ? stagger * std::sqrt(regions[k].seedDist2) / maxSeed : 0.f;
        const float t = stagger >= 1.f ? (progress >= start ? 1.f : 0.f)
                                       : Clamp01((progress - start) / (1.f - stagger));
        front[k] = t * (maxD[k] + wob + soft);
    }

    std::vector<float> cover(n, 0.f);
    const float invSize = 1.f / std::max(1.f, p.wobbleSize);
    for (size_t i = 0; i < n; ++i) {
        const int lb = label[i];
        if (lb < 0 || !active[lb] || dist[i] >= INF || front[lb] <= 0.f) continue;
        float d = dist[i];
        if (wob > 0.f) {
            const float x = float(i % w) * invSize, y = float(i / w) * invSize;
            const float nz = 0.65f * ValueNoise(x, y, p.evolution) + 0.35f * ValueNoise(x * 2.3f + 17.f, y * 2.3f + 5.f, p.evolution * 1.7f + 3.f);
            d += wob * nz;
        }
        cover[i] = Clamp01((front[lb] - d) / soft);
    }

    // 5. 線の下（と閉じた切れ目）へ塗りを広げる
    const int grow = int(std::ceil(std::max(0.f, p.closeGaps) + std::max(0.f, p.underLines)));
    if (grow > 0) {
        // 塗らない領域へははみ出さない（壁の画素だけを広げる）
        std::vector<int> wallTodo;
        for (size_t i = 0; i < n; ++i) if (wall[i]) wallTodo.push_back(int(i));
        std::vector<float> next;
        for (int it = 0; it < grow; ++it) {
            next = cover;
            for (int i : wallTodo) {
                const int x = i % w, y = i / w;
                float m = cover[i];
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int nx = x + dx, ny = y + dy;
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                        m = std::max(m, cover[size_t(ny) * w + nx]);
                    }
                next[i] = m;
            }
            cover.swap(next);
        }
    }

    // 6. 合成
    const float op = Clamp01(p.opacity);
    for (size_t i = 0; i < n; ++i) {
        const float f = cover[i] * op;
        if (p.output == OUTPUT_FILL_ONLY) {
            dst.r[i] = p.color[0]; dst.g[i] = p.color[1]; dst.b[i] = p.color[2]; dst.a[i] = f;
            continue;
        }
        if (f <= 0.f) continue;
        if (p.detect == DETECT_ALPHA) {
            // 線画の後ろに置く
            const float sa = Clamp01(src.a[i]);
            const float oa = sa + f * (1.f - sa);
            if (oa <= 0.f) continue;
            dst.r[i] = (src.r[i] * sa + p.color[0] * f * (1.f - sa)) / oa;
            dst.g[i] = (src.g[i] * sa + p.color[1] * f * (1.f - sa)) / oa;
            dst.b[i] = (src.b[i] * sa + p.color[2] * f * (1.f - sa)) / oa;
            dst.a[i] = oa;
        } else {
            // 紙ごとの線画: 線の濃さの分だけ元の色を残し、紙の部分を塗りの色にする
            const float m = line[i];
            const float cr = p.color[0] + (src.r[i] - p.color[0]) * m;
            const float cg = p.color[1] + (src.g[i] - p.color[1]) * m;
            const float cb = p.color[2] + (src.b[i] - p.color[2]) * m;
            dst.r[i] = src.r[i] + (cr - src.r[i]) * f;
            dst.g[i] = src.g[i] + (cg - src.g[i]) * f;
            dst.b[i] = src.b[i] + (cb - src.b[i]) * f;
            dst.a[i] = std::max(src.a[i], f);
        }
    }
}

}  // namespace growfill
