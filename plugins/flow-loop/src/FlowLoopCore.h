// FlowLoopCore.h — 「Flow Loop」の画像処理本体（After Effects に依存しない）
//
// 止め絵を、流れの向きに沿って動き続けるループ映像にする（水・煙・髪・雲など）。
//   各画素の流れ v（1 ループで進む px）を決め、画像を p - v·t から取ってくる。
//   t を 0→1 で進めると画像が流れるが、そのままだと伸び切ってしまうので、
//   半周ずらした 2 枚を三角形の重みで重ねる（片方が伸び切る瞬間には重み 0）。
//   1 ループの最後と最初が同じ絵になるので、つなぎ目なく繰り返せる。
// 流れの決め方:
//   Guides     = ガイドの線（AE ではレイヤーのマスク）の描いた向きに沿って流れる。線から離れるほど弱い
//   Direction  = 全体が同じ向き
//   Radial Out / Radial In / Swirl = 中心から外へ / 中心へ / 中心の周りを回る
//   Flow Map   = 別の画像の R=横 G=縦（0.5＝止まる）、アルファ＝流れる強さ
// 流れる範囲は、閉じた線（AE では閉じたマスク）や別レイヤーのアルファで限れる（Area Feather でぼかす）。
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

namespace flowloop {

struct Image {
    int w = 0, h = 0;
    std::vector<float> r, g, b, a;  // ストレートアルファ
    void resize(int W, int H) {
        w = W; h = H;
        const size_t n = size_t(W) * size_t(H);
        r.assign(n, 0.f); g.assign(n, 0.f); b.assign(n, 0.f); a.assign(n, 0.f);
    }
};

struct Point { float x, y; };
using Polyline = std::vector<Point>;  // 描いた向き = 流れる向き

enum Source { SRC_GUIDES = 0, SRC_DIRECTION, SRC_RADIAL_OUT, SRC_RADIAL_IN, SRC_SWIRL, SRC_FLOW_MAP };

struct Params {
    int source = SRC_GUIDES;
    float phase = 0.f;          // 0..1 ループの中の位置（時間 / Loop Duration）
    float speed = 40.f;         // px 1 ループで進む距離
    float angle = 0.f;          // 度 Direction の向き（0＝右、90＝下）
    float centerX = 0.f, centerY = 0.f;  // 入力画像の画素座標
    float influence = 80.f;     // px ガイドの届く距離
    float mapSmooth = 8.f;      // px Flow Map をならす半径
    float phaseNoise = 0.5f;    // 0..1 場所ごとにタイミングをずらして脈打ちを隠す
    float noiseScale = 120.f;   // px ずらし方の模様の大きさ
    float areaFeather = 10.f;   // px 流れる範囲の境目をなだらかにする幅
    bool wrapEdges = false;     // 画面の端を反対側とつなぐ（タイル状の素材向け）
    int threads = 0;            // 0＝自動
};

// 入力をどれだけ広く読むか（px）
inline float Reach(const Params& p) { return std::fabs(p.speed) * 0.5f + 2.f; }

namespace detail {

inline float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
inline float Fract(float v) { return v - std::floor(v); }

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

// なめらかな値ノイズ（0..1）
inline float Hash(int x, int y) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return float((h ^ (h >> 16)) & 0xffffff) / float(0xffffff);
}
inline float ValueNoise(float x, float y) {
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    float fx = x - ix, fy = y - iy;
    fx = fx * fx * (3.f - 2.f * fx); fy = fy * fy * (3.f - 2.f * fy);
    const float a = Hash(ix, iy), b = Hash(ix + 1, iy), c = Hash(ix, iy + 1), d = Hash(ix + 1, iy + 1);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
}

// 1 方向の箱ぼかし（半径 r）。端は端の値を伸ばす
inline void BoxPass(std::vector<float>& v, int w, int h, int r, bool horiz) {
    if (r < 1) return;
    std::vector<float> out(v.size());
    const int len = horiz ? w : h, lines = horiz ? h : w;
    for (int l = 0; l < lines; ++l) {
        auto at = [&](int k) { k = std::min(std::max(k, 0), len - 1); return v[horiz ? size_t(l) * w + k : size_t(k) * w + l]; };
        float s = 0.f;
        for (int k = -r; k <= r; ++k) s += at(k);
        for (int k = 0; k < len; ++k) {
            out[horiz ? size_t(l) * w + k : size_t(k) * w + l] = s / float(2 * r + 1);
            s += at(k + r + 1) - at(k - r);
        }
    }
    v.swap(out);
}

}  // namespace detail

// 画素 (x, y)（入力画像の座標）の流れ [px / ループ] を並べた場を作る。cell ごとに計算して補間する
struct FlowField {
    int w = 0, h = 0, cell = 1;
    std::vector<float> vx, vy;
    std::vector<float> area;  // 流れる範囲（空なら全体）
    float Area(float x, float y) const {
        if (area.empty()) return 1.f;
        const int gx = std::min(std::max(int(x / cell + 0.5f), 0), w - 1), gy = std::min(std::max(int(y / cell + 0.5f), 0), h - 1);
        return area[size_t(gy) * w + gx];
    }
    void Sample(float x, float y, float& ox, float& oy) const {
        const float gx = std::min(std::max(x / cell, 0.f), float(w - 1)), gy = std::min(std::max(y / cell, 0.f), float(h - 1));
        const int x0 = int(gx), y0 = int(gy), x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
        const float fx = gx - x0, fy = gy - y0;
        auto lerp2 = [&](const std::vector<float>& f) {
            const float a = f[size_t(y0) * w + x0], b = f[size_t(y0) * w + x1], c = f[size_t(y1) * w + x0], d = f[size_t(y1) * w + x1];
            return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
        };
        ox = lerp2(vx); oy = lerp2(vy);
    }
};

// 流れる範囲（0..1）を格子に描く。範囲の指定がなければ空を返す
inline std::vector<float> AreaGrid(const FlowField& f, const Params& p, const std::vector<Polyline>& areas, const Image* areaMap) {
    using namespace detail;
    if (areas.empty() && !(areaMap && areaMap->w > 0)) return {};
    std::vector<float> m(size_t(f.w) * f.h, 0.f);
    // 閉じた線: 偶奇規則で内側を 1（格子点ごとに横方向の交点を数える）
    for (const Polyline& pl : areas) {
        if (pl.size() < 3) continue;
        for (int gy = 0; gy < f.h; ++gy) {
            const float py = gy * float(f.cell);
            std::vector<float> xs;
            for (size_t i = 0; i < pl.size(); ++i) {
                const Point& a = pl[i];
                const Point& b = pl[(i + 1) % pl.size()];
                if ((a.y <= py) != (b.y <= py)) xs.push_back(a.x + (py - a.y) / (b.y - a.y) * (b.x - a.x));
            }
            std::sort(xs.begin(), xs.end());
            for (size_t k = 0; k + 1 < xs.size(); k += 2) {
                const int x0 = std::max(0, int(std::ceil(xs[k] / f.cell))), x1 = std::min(f.w - 1, int(std::floor(xs[k + 1] / f.cell)));
                for (int gx = x0; gx <= x1; ++gx) m[size_t(gy) * f.w + gx] = 1.f;
            }
        }
    }
    if (areaMap && areaMap->w > 0)
        for (int gy = 0; gy < f.h; ++gy)
            for (int gx = 0; gx < f.w; ++gx) {
                const int sx = std::min(gx * f.cell, areaMap->w - 1), sy = std::min(gy * f.cell, areaMap->h - 1);
                float& v = m[size_t(gy) * f.w + gx];
                v = std::max(v, Clamp01(areaMap->a[size_t(sy) * areaMap->w + sx]));
            }
    const int r = int(std::lround(std::max(0.f, p.areaFeather) / (2.f * f.cell)));
    for (int i = 0; i < 3; ++i) { BoxPass(m, f.w, f.h, r, true); BoxPass(m, f.w, f.h, r, false); }
    return m;
}

inline FlowField BuildField(int W, int H, const Params& p, const std::vector<Polyline>& guides, const Image* flowMap,
                            const std::vector<Polyline>& areas = {}, const Image* areaMap = nullptr) {
    using namespace detail;
    FlowField f;
    f.cell = p.source == SRC_GUIDES ? 4 : 1;
    if (p.source == SRC_DIRECTION || p.source == SRC_RADIAL_OUT || p.source == SRC_RADIAL_IN || p.source == SRC_SWIRL) f.cell = 2;
    f.w = W / f.cell + 2; f.h = H / f.cell + 2;
    const size_t n = size_t(f.w) * f.h;
    f.vx.assign(n, 0.f); f.vy.assign(n, 0.f);
    const float sp = p.speed;

    if (p.source == SRC_DIRECTION) {
        const float a = p.angle * 3.14159265f / 180.f;
        std::fill(f.vx.begin(), f.vx.end(), std::cos(a) * sp);
        std::fill(f.vy.begin(), f.vy.end(), std::sin(a) * sp);
    } else if (p.source == SRC_RADIAL_OUT || p.source == SRC_RADIAL_IN || p.source == SRC_SWIRL) {
        for (int y = 0; y < f.h; ++y)
            for (int x = 0; x < f.w; ++x) {
                float dx = x * f.cell - p.centerX, dy = y * f.cell - p.centerY;
                const float d = std::sqrt(dx * dx + dy * dy);
                // 中心のすぐ近くは弱める（一点に吸い込まれて破綻しないように）
                const float k = Clamp01(d / std::max(1.f, std::fabs(sp))) / std::max(d, 1e-3f);
                dx *= k; dy *= k;
                const size_t i = size_t(y) * f.w + x;
                if (p.source == SRC_RADIAL_OUT) { f.vx[i] = dx * sp; f.vy[i] = dy * sp; }
                else if (p.source == SRC_RADIAL_IN) { f.vx[i] = -dx * sp; f.vy[i] = -dy * sp; }
                else { f.vx[i] = -dy * sp; f.vy[i] = dx * sp; }  // 時計回り（y は下向き）
            }
    } else if (p.source == SRC_FLOW_MAP && flowMap && flowMap->w > 0) {
        for (int y = 0; y < f.h; ++y)
            for (int x = 0; x < f.w; ++x) {
                const int sx = std::min(x, flowMap->w - 1), sy = std::min(y, flowMap->h - 1);
                const size_t s = size_t(sy) * flowMap->w + sx, i = size_t(y) * f.w + x;
                const float a = Clamp01(flowMap->a[s]);
                f.vx[i] = (flowMap->r[s] - 0.5f) * 2.f * sp * a;
                f.vy[i] = (flowMap->g[s] - 0.5f) * 2.f * sp * a;
            }
        const int r = int(std::lround(std::max(0.f, p.mapSmooth) / 2.f));
        for (int i = 0; i < 3; ++i) {
            BoxPass(f.vx, f.w, f.h, r, true); BoxPass(f.vx, f.w, f.h, r, false);
            BoxPass(f.vy, f.w, f.h, r, true); BoxPass(f.vy, f.w, f.h, r, false);
        }
    } else if (p.source == SRC_GUIDES) {
        // 線分ごとに、近い格子点へ向きを足す（ガウスの重み）。強さは最も近い線の重み
        struct Seg { float ax, ay, bx, by, tx, ty, len; };
        std::vector<Seg> segs;
        for (const Polyline& pl : guides)
            for (size_t i = 0; i + 1 < pl.size(); ++i) {
                const float dx = pl[i + 1].x - pl[i].x, dy = pl[i + 1].y - pl[i].y;
                const float len = std::sqrt(dx * dx + dy * dy);
                if (len < 1e-3f) continue;
                segs.push_back({pl[i].x, pl[i].y, pl[i + 1].x, pl[i + 1].y, dx / len, dy / len, len});
            }
        const float sig = std::max(1.f, p.influence);
        const float cut = sig * 3.f;
        std::vector<float> sw(n, 0.f), wmax(n, 0.f);
        // 格子の行ごとに並列化
        ParallelFor(f.h, p.threads, [&](int y0, int y1) {
            for (const Seg& s : segs) {
                const int gx0 = std::max(0, int(std::floor((std::min(s.ax, s.bx) - cut) / f.cell)));
                const int gx1 = std::min(f.w - 1, int(std::ceil((std::max(s.ax, s.bx) + cut) / f.cell)));
                const int gy0 = std::max(y0, int(std::floor((std::min(s.ay, s.by) - cut) / f.cell)));
                const int gy1 = std::min(y1 - 1, int(std::ceil((std::max(s.ay, s.by) + cut) / f.cell)));
                for (int gy = gy0; gy <= gy1; ++gy)
                    for (int gx = gx0; gx <= gx1; ++gx) {
                        const float px = float(gx * f.cell), py = float(gy * f.cell);
                        float t = (px - s.ax) * s.tx + (py - s.ay) * s.ty;
                        t = std::min(std::max(t, 0.f), s.len);
                        const float qx = s.ax + s.tx * t - px, qy = s.ay + s.ty * t - py;
                        const float d2 = qx * qx + qy * qy;
                        if (d2 > cut * cut) continue;
                        const float wgt = std::exp(-0.5f * d2 / (sig * sig));
                        const size_t i = size_t(gy) * f.w + gx;
                        // 線の重なりで二重に数えないよう、重みは線分の長さで割らずそのまま足す
                        f.vx[i] += s.tx * wgt; f.vy[i] += s.ty * wgt;
                        sw[i] += wgt;
                        wmax[i] = std::max(wmax[i], wgt);
                    }
            }
        });
        for (size_t i = 0; i < n; ++i) {
            if (sw[i] <= 1e-6f) { f.vx[i] = f.vy[i] = 0.f; continue; }
            float dx = f.vx[i] / sw[i], dy = f.vy[i] / sw[i];
            const float l = std::sqrt(dx * dx + dy * dy);
            // 逆向きの線がぶつかる所は打ち消して止まる。向きはそろえ、強さは近さで決める
            if (l > 1e-4f) { dx /= l; dy /= l; }
            const float k = wmax[i] * std::min(1.f, l * 1.5f) * sp;
            f.vx[i] = dx * k; f.vy[i] = dy * k;
        }
    }
    f.area = AreaGrid(f, p, areas, areaMap);
    for (size_t i = 0; i < f.area.size(); ++i) { f.vx[i] *= f.area[i]; f.vy[i] *= f.area[i]; }
    return f;
}

// src を出力 (W, H) の (ox, oy) に置いて処理する。dst は W×H。
// guides / areas / flowMap / areaMap / center は src の座標
inline void Process(const Image& src, Image& dst, int W, int H, int ox, int oy, const Params& p,
                    const std::vector<Polyline>& guides, const Image* flowMap,
                    const std::vector<Polyline>& areas = {}, const Image* areaMap = nullptr) {
    using namespace detail;
    dst.resize(W, H);
    if (src.w <= 0 || src.h <= 0) return;
    const FlowField field = BuildField(src.w, src.h, p, guides, flowMap, areas, areaMap);

    // プリマルチの双一次補間。端は端の値を伸ばす（Wrap Edges なら反対側から）
    auto sample = [&](float x, float y, float out[4]) {
        x -= 0.5f; y -= 0.5f;
        int x0 = int(std::floor(x)), y0 = int(std::floor(y));
        const float fx = x - x0, fy = y - y0;
        out[0] = out[1] = out[2] = out[3] = 0.f;
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i) {
                int sx = x0 + i, sy = y0 + j;
                if (p.wrapEdges) { sx = ((sx % src.w) + src.w) % src.w; sy = ((sy % src.h) + src.h) % src.h; }
                else { sx = std::min(std::max(sx, 0), src.w - 1); sy = std::min(std::max(sy, 0), src.h - 1); }
                const float wgt = (i ? fx : 1.f - fx) * (j ? fy : 1.f - fy);
                const size_t s = size_t(sy) * src.w + sx;
                const float a = src.a[s] * wgt;
                out[0] += src.r[s] * a; out[1] += src.g[s] * a; out[2] += src.b[s] * a; out[3] += a;
            }
    };

    const float ns = std::max(1.f, p.noiseScale), pn = Clamp01(p.phaseNoise);
    ParallelFor(H, p.threads, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y)
            for (int x = 0; x < W; ++x) {
                const float sx = float(x - ox) + 0.5f, sy = float(y - oy) + 0.5f;
                float vx, vy;
                field.Sample(sx, sy, vx, vy);
                const float off = pn * ValueNoise(sx / ns, sy / ns);
                const float t0 = Fract(p.phase + off), t1 = Fract(t0 + 0.5f);
                const float w0 = 1.f - std::fabs(2.f * t0 - 1.f);
                // 伸びを ±半ループ分に収めるため t − 0.5 だけずらす
                // 範囲の外から取ってきた色は使わず、その場の色にする（止まっている物が流れ込まないように）
                float c0[4], c1[4], c[4];
                const float qx0 = sx - vx * (t0 - 0.5f), qy0 = sy - vy * (t0 - 0.5f);
                const float qx1 = sx - vx * (t1 - 0.5f), qy1 = sy - vy * (t1 - 0.5f);
                sample(qx0, qy0, c0);
                sample(qx1, qy1, c1);
                if (!field.area.empty()) {
                    sample(sx, sy, c);
                    const float k0 = field.Area(qx0, qy0), k1 = field.Area(qx1, qy1);
                    for (int ch = 0; ch < 4; ++ch) {
                        c0[ch] = c[ch] + (c0[ch] - c[ch]) * k0;
                        c1[ch] = c[ch] + (c1[ch] - c[ch]) * k1;
                    }
                }
                const size_t i = size_t(y) * W + x;
                const float a = c0[3] * w0 + c1[3] * (1.f - w0);
                dst.a[i] = Clamp01(a);
                if (a > 1e-6f) {
                    dst.r[i] = (c0[0] * w0 + c1[0] * (1.f - w0)) / a;
                    dst.g[i] = (c0[1] * w0 + c1[1] * (1.f - w0)) / a;
                    dst.b[i] = (c0[2] * w0 + c1[2] * (1.f - w0)) / a;
                }
            }
    });
}

}  // namespace flowloop
