// InkPoolCore.h
// 線画の交差部に墨溜まり（インクの溜まり・とろけ）を作るコア処理。
// After Effects SDK に依存しない純粋な C++17 実装で、AE プラグイン本体と
// コマンドラインのテストツールの両方から使う。
//
// 処理の流れ
//   1. 入力から「線の濃さ」マスク m を作る（アルファ / 輝度から選択）
//   1b. 線とつながっていない小さな形（目・鼻など）や太い部分（文字・塗り）は溶かさずに残す
//   2. m をガウスぼかしした線密度 G を作る。1本線の上の典型的な密度を基準にする。
//   2b. 線が Y / T 字に分かれる所は、そのまわりだけ大きめにクロージングして角に溜まりを作る
//   3. G が基準より濃い所（線が交わる・近づく・内側に曲がる所）だけ線を太らせる。
//      メタボールのように線どうしが丸く溶け合うが、1本だけの線や離れた線の間の
//      空間は塗りつぶさない（線から広がる距離にも上限を設ける）。
//   4. 輪郭をぼかし→しきい値で丸め（なめらかさ）、すき間を残し、線の端を細める。
//      線で囲まれた小さな穴は埋めずに元のまま戻す。
//   5. 一番近い線の色で塗り（色は混ぜない）、元画像に合成する。
//
// 画素はストレートアルファ (RGB と A が独立) の float として扱う。
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <thread>
#include <vector>

namespace inkpool {

enum DetectMode {
    kDetectAlpha = 0,       // アルファチャンネルを線とみなす（透明背景の線画）
    kDetectDarkLines = 1,   // 暗い所を線とみなす（白い紙に黒い線など）
    kDetectBrightLines = 2  // 明るい所を線とみなす（黒地に白い線など）
};

enum ColorMode {
    kColorFromLines = 0,  // 近くの線の色を使う（カラー線画向け）
    kColorCustom = 1      // 指定色で塗る
};

enum OutputMode {
    kOutputComposite = 0,  // 元の線画に溜まりを合成
    kOutputPoolOnly = 1    // 増えた溜まり部分だけを出力
};

struct Params {
    int detect = kDetectAlpha;
    float lineThreshold = 0.5f;  // 2値化しきい値 (0..1)
    float meltSize = 6.0f;       // 溶ける範囲 (px): 線密度を測るぼかしのσ
    float strength = 0.5f;       // 溶け具合 (0..1)。上げるほど広く溶け合う
    float spread = 0.6f;         // 線から広がってよい距離（溶ける範囲に対する比率）
    float keepGaps = 0.5f;       // すき間の維持 (0..1)。線のあいだのすき間を、元の幅のこの割合までは残す
    float melt = 3.0f;           // なめらかさ (px): 角を丸める半径
    float endTaper = 0.5f;       // 線の端の細め (0..1)。端を線幅のこの割合まで細めて丸くする
    float junction = 0.5f;       // 分かれ目の溜まり (0..1)。線が Y / T 字に分かれる所にも溜まりを作る
    int colorMode = kColorFromLines;
    float color[3] = {0.f, 0.f, 0.f};  // 指定色 (0..1)
    int output = kOutputComposite;
    int threads = 0;  // 0 = 自動
};

// ストレートアルファの RGBA float 画像（各チャンネルは別配列）
struct Image {
    int w = 0, h = 0;
    std::vector<float> r, g, b, a;
    void resize(int W, int H) {
        w = W; h = H;
        const size_t n = size_t(W) * size_t(H);
        r.assign(n, 0.f); g.assign(n, 0.f); b.assign(n, 0.f); a.assign(n, 0.f);
    }
};

namespace detail {

inline void ParallelFor(int count, int threads, const std::function<void(int, int)>& fn) {
    if (threads <= 0) {
        threads = int(std::thread::hardware_concurrency());
        if (threads <= 0) threads = 4;
    }
    threads = std::max(1, std::min(threads, count));
    if (threads == 1) { fn(0, count); return; }
    std::vector<std::thread> pool;
    pool.reserve(threads);
    const int chunk = (count + threads - 1) / threads;
    for (int t = 0; t < threads; ++t) {
        const int b = t * chunk, e = std::min(count, b + chunk);
        if (b >= e) break;
        pool.emplace_back(fn, b, e);
    }
    for (auto& th : pool) th.join();
}

inline float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

inline float SmoothStep(float e0, float e1, float x) {
    if (e1 <= e0) return x >= e0 ? 1.f : 0.f;
    const float t = Clamp01((x - e0) / (e1 - e0));
    return t * t * (3.f - 2.f * t);
}

// --- 1D 二乗ユークリッド距離変換 (Felzenszwalb & Huttenlocher) ---
// 精度のため内部は double。f は二乗距離（特徴点は 0、それ以外は大きな値）。
inline void Edt1D(const double* f, double* d, int n, int* v, double* z) {
    int k = 0;
    v[0] = 0;
    z[0] = -std::numeric_limits<double>::infinity();
    z[1] = std::numeric_limits<double>::infinity();
    for (int q = 1; q < n; ++q) {
        double s = ((f[q] + double(q) * q) - (f[v[k]] + double(v[k]) * v[k])) / (2.0 * (q - v[k]));
        while (s <= z[k]) {
            --k;
            s = ((f[q] + double(q) * q) - (f[v[k]] + double(v[k]) * v[k])) / (2.0 * (q - v[k]));
        }
        ++k;
        v[k] = q;
        z[k] = s;
        z[k + 1] = std::numeric_limits<double>::infinity();
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[k + 1] < q) ++k;
        const double dq = double(q - v[k]);
        d[q] = dq * dq + f[v[k]];
    }
}

// feature[i] が true の画素までの距離（ユークリッド, px）を返す。
// feature が1つも無ければ巨大な値になる。
inline std::vector<float> DistanceTo(const std::vector<uint8_t>& feature, int w, int h, int threads) {
    const double INF = 1e20;
    std::vector<double> g(size_t(w) * h);
    for (size_t i = 0; i < g.size(); ++i) g[i] = feature[i] ? 0.0 : INF;

    // 列方向
    ParallelFor(w, threads, [&](int x0, int x1) {
        std::vector<double> f(h), d(h), z(h + 1);
        std::vector<int> v(h);
        for (int x = x0; x < x1; ++x) {
            for (int y = 0; y < h; ++y) f[y] = g[size_t(y) * w + x];
            Edt1D(f.data(), d.data(), h, v.data(), z.data());
            for (int y = 0; y < h; ++y) g[size_t(y) * w + x] = d[y];
        }
    });
    // 行方向
    std::vector<float> out(g.size());
    ParallelFor(h, threads, [&](int y0, int y1) {
        std::vector<double> f(w), d(w), z(w + 1);
        std::vector<int> v(w);
        for (int y = y0; y < y1; ++y) {
            const double* row = &g[size_t(y) * w];
            std::copy(row, row + w, f.begin());
            Edt1D(f.data(), d.data(), w, v.data(), z.data());
            float* o = &out[size_t(y) * w];
            for (int x = 0; x < w; ++x) o[x] = float(std::sqrt(std::min(d[x], 1e18)));
        }
    });
    return out;
}

// --- ボックスぼかし3回によるガウス近似（画像外は 0 とみなす） ---
inline void BoxBlurH(const float* src, float* dst, int w, int h, int rad, int threads) {
    ParallelFor(h, threads, [&](int y0, int y1) {
        const float inv = 1.f / float(2 * rad + 1);
        for (int y = y0; y < y1; ++y) {
            const float* s = src + size_t(y) * w;
            float* d = dst + size_t(y) * w;
            double acc = 0.0;
            for (int x = -rad; x <= rad; ++x) if (x >= 0 && x < w) acc += s[x];
            for (int x = 0; x < w; ++x) {
                d[x] = float(acc) * inv;
                const int add = x + rad + 1, sub = x - rad;
                if (add < w) acc += s[add];
                if (sub >= 0) acc -= s[sub];
            }
        }
    });
}

inline void BoxBlurV(const float* src, float* dst, int w, int h, int rad, int threads) {
    ParallelFor(w, threads, [&](int x0, int x1) {
        const float inv = 1.f / float(2 * rad + 1);
        for (int x = x0; x < x1; ++x) {
            double acc = 0.0;
            for (int y = -rad; y <= rad; ++y) if (y >= 0 && y < h) acc += src[size_t(y) * w + x];
            for (int y = 0; y < h; ++y) {
                dst[size_t(y) * w + x] = float(acc) * inv;
                const int add = y + rad + 1, sub = y - rad;
                if (add < h) acc += src[size_t(add) * w + x];
                if (sub >= 0) acc -= src[size_t(sub) * w + x];
            }
        }
    });
}

inline void GaussianBlur(std::vector<float>& img, int w, int h, float sigma, int threads) {
    if (sigma < 0.3f || w <= 0 || h <= 0) return;
    // σ に対応する3段ボックスの半径（等幅近似）
    const float wIdeal = std::sqrt(12.f * sigma * sigma / 3.f + 1.f);
    const int rad = std::max(1, int(std::lround((wIdeal - 1.f) * 0.5f)));
    std::vector<float> tmp(img.size());
    for (int i = 0; i < 3; ++i) {
        BoxBlurH(img.data(), tmp.data(), w, h, rad, threads);
        BoxBlurV(tmp.data(), img.data(), w, h, rad, threads);
    }
}

// 正方形の窓での最大値フィルタ（行→列の分離処理）
inline void MaxFilter(std::vector<float>& img, int w, int h, int rad, int threads) {
    if (rad <= 0) return;
    std::vector<float> tmp(img.size());
    ParallelFor(h, threads, [&](int y0, int y1) {
        for (int y = y0; y < y1; ++y) {
            const float* s = &img[size_t(y) * w];
            float* d = &tmp[size_t(y) * w];
            for (int x = 0; x < w; ++x) {
                float mx = s[x];
                for (int k = std::max(0, x - rad), e = std::min(w - 1, x + rad); k <= e; ++k) mx = std::max(mx, s[k]);
                d[x] = mx;
            }
        }
    });
    ParallelFor(w, threads, [&](int x0, int x1) {
        std::vector<float> col(h);
        for (int x = x0; x < x1; ++x) {
            for (int y = 0; y < h; ++y) col[y] = tmp[size_t(y) * w + x];
            for (int y = 0; y < h; ++y) {
                float mx = col[y];
                for (int k = std::max(0, y - rad), e = std::min(h - 1, y + rad); k <= e; ++k) mx = std::max(mx, col[k]);
                img[size_t(y) * w + x] = mx;
            }
        }
    });
}

// --- 線の端の細め ---
// Zhang-Suen 法で 2 値画像を細線化する（1 = 線）
inline void Thin(std::vector<uint8_t>& img, int w, int h) {
    std::vector<size_t> del;
    bool changed = true;
    auto at = [&](int x, int y) -> int {
        return (x < 0 || y < 0 || x >= w || y >= h) ? 0 : img[size_t(y) * w + x];
    };
    while (changed) {
        changed = false;
        for (int pass = 0; pass < 2; ++pass) {
            del.clear();
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    if (!img[size_t(y) * w + x]) continue;
                    const int p2 = at(x, y - 1), p3 = at(x + 1, y - 1), p4 = at(x + 1, y), p5 = at(x + 1, y + 1);
                    const int p6 = at(x, y + 1), p7 = at(x - 1, y + 1), p8 = at(x - 1, y), p9 = at(x - 1, y - 1);
                    const int B = p2 + p3 + p4 + p5 + p6 + p7 + p8 + p9;
                    if (B < 2 || B > 6) continue;
                    const int A = (!p2 && p3) + (!p3 && p4) + (!p4 && p5) + (!p5 && p6) +
                                  (!p6 && p7) + (!p7 && p8) + (!p8 && p9) + (!p9 && p2);
                    if (A != 1) continue;
                    if (pass == 0 ? ((p2 && p4 && p6) || (p4 && p6 && p8)) : ((p2 && p4 && p8) || (p2 && p6 && p8))) continue;
                    del.push_back(size_t(y) * w + x);
                }
            for (size_t i : del) img[i] = 0;
            changed |= !del.empty();
        }
    }
}

// pool（線の濃さ 0..1）の線の端を細めて丸める
// maxHalf より太い線（文字や塗りの形など）の端は細めない
// skip が 1 の画素にある端点（そのまま残す小さな形など）は細めない
inline void TaperEnds(std::vector<float>& pool, int w, int h, float amount, float maxHalf,
                      const std::vector<uint8_t>& skip, int threads) {
    const size_t n = size_t(w) * h;
    std::vector<uint8_t> ink(n), bg(n);
    for (size_t i = 0; i < n; ++i) { ink[i] = pool[i] >= 0.5f; bg[i] = !ink[i]; }
    const std::vector<float> dIn = DistanceTo(bg, w, h, threads);  // 輪郭までの距離
    std::vector<uint8_t> sk = ink;
    Thin(sk, w, h);
    auto skAt = [&](int x, int y) -> bool {
        return x >= 0 && y >= 0 && x < w && y < h && sk[size_t(y) * w + x];
    };
    auto neighbors = [&](int x, int y, int* nx, int* ny) {
        int c = 0;
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
                if ((dx || dy) && skAt(x + dx, y + dy)) { nx[c] = x + dx; ny[c] = y + dy; ++c; }
        return c;
    };
    // (x,y) から前の画素 (px,py) 以外へ進める隣を数える。階段状の斜め線では
    // 互いに隣り合う2画素が並ぶので、それは1本の道としてまとめる。
    auto forward = [&](int px, int py, int x, int y, int* fx, int* fy) {
        int ax[8], ay[8];
        const int c = neighbors(x, y, ax, ay);
        int cnt = 0;
        for (int k = 0; k < c; ++k) {
            if (ax[k] == px && ay[k] == py) continue;
            if (std::abs(ax[k] - px) <= 1 && std::abs(ay[k] - py) <= 1) continue;
            fx[cnt] = ax[k]; fy[cnt] = ay[k]; ++cnt;
        }
        if (cnt == 2 && std::abs(fx[0] - fx[1]) <= 1 && std::abs(fy[0] - fy[1]) <= 1) {
            // 前の画素から遠い方へ進む
            const int d0 = (fx[0] - px) * (fx[0] - px) + (fy[0] - py) * (fy[0] - py);
            const int d1 = (fx[1] - px) * (fx[1] - px) + (fy[1] - py) * (fy[1] - py);
            if (d1 > d0) { fx[0] = fx[1]; fy[0] = fy[1]; }
            cnt = 1;
        }
        return cnt;
    };

    // 細線を (fx,fy) から (x,y) へ進んでたどり、分かれ道・端点・limit 歩までの長さを返す。
    // 途中で分かれ道に着いたら「長い枝」とみなして limit を返す。
    int nx[8], ny[8];
    auto branchLen = [&](int px, int py, int x, int y, int limit) {
        int fx[8], fy[8];
        for (int steps = 1; steps < limit; ++steps) {
            const int c = forward(px, py, x, y, fx, fy);
            if (c == 0) return steps;      // 端点で終わる短い枝
            if (c >= 2) return limit;      // 本当の分かれ道
            px = x; py = y; x = fx[0]; y = fy[0];
        }
        return limit;
    };

    // 端点を集める。四角い線端は細線化で先が二股のヒゲになり、図形の角にもヒゲが出る。
    // ヒゲの付け根から「長い枝」が1本だけ出ていれば線の端、2本以上なら角なので除外する。
    struct EndPt { int x, y; float half; };
    std::vector<EndPt> ends;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (!sk[size_t(y) * w + x] || neighbors(x, y, nx, ny) != 1) continue;
            // 端点のまわりの線幅（輪郭までの距離の最大値）
            float half = 1.f;
            for (int yy = std::max(0, y - 6); yy <= std::min(h - 1, y + 6); ++yy)
                for (int xx = std::max(0, x - 6); xx <= std::min(w - 1, x + 6); ++xx)
                    if (sk[size_t(yy) * w + xx]) half = std::max(half, dIn[size_t(yy) * w + xx]);
            const int longLimit = int(std::ceil(3.f * half)) + 3;
            // 端点から最初の分かれ道までたどる
            int px = x, py = y, cx = nx[0], cy = ny[0];
            bool hitEnd = false, hitJunction = false;
            for (int steps = 1; steps < longLimit; ++steps) {
                int fx[8], fy[8];
                const int c = forward(px, py, cx, cy, fx, fy);
                if (c == 0) { hitEnd = true; break; }
                if (c >= 2) {
                    // 分かれ道: 先に伸びる枝のうち長いものを数える
                    int longCount = 0;
                    for (int k = 0; k < c; ++k)
                        if (branchLen(cx, cy, fx[k], fy[k], longLimit) >= longLimit) ++longCount;
                    if (longCount != 1) hitEnd = true;  // 角（2本以上）か小さな塊（0本）
                    hitJunction = true;
                    break;
                }
                px = cx; py = cy; cx = fx[0]; cy = fy[0];
            }
            (void)hitJunction;
            if (hitEnd) continue;  // 点・短い塊・図形の角
            if (half > maxHalf) continue;  // 太すぎる線はそのまま
            if (skip[size_t(y) * w + x]) continue;
            ends.push_back({x, y, half});
        }

    // 削るのは端点と同じひとつながりのインクの中だけ（近くの別の線や形は削らない）
    std::vector<int> comp(n, -1);
    {
        std::vector<size_t> stack;
        int next = 0;
        for (size_t s0 = 0; s0 < n; ++s0) {
            if (!ink[s0] || comp[s0] >= 0) continue;
            comp[s0] = next;
            stack.push_back(s0);
            while (!stack.empty()) {
                const size_t i = stack.back();
                stack.pop_back();
                const int x = int(i % size_t(w)), y = int(i / size_t(w));
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = x + dx, yy = y + dy;
                        if (xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                        const size_t j = size_t(yy) * w + xx;
                        if (ink[j] && comp[j] < 0) { comp[j] = next; stack.push_back(j); }
                    }
            }
            ++next;
        }
    }

    // 端点ごとに、削る量の場 E を作る
    std::vector<float> E(n, 0.f);
    std::vector<float> halves;
    for (const EndPt& ep : ends) {
        const float half = ep.half;
        halves.push_back(half);
        const float L = 4.f * half;
        const float maxE = amount * half;
        const int R = int(std::ceil(L + half)) + 1;
        const int c0 = comp[size_t(ep.y) * w + ep.x];
        for (int yy = std::max(0, ep.y - R); yy <= std::min(h - 1, ep.y + R); ++yy)
            for (int xx = std::max(0, ep.x - R); xx <= std::min(w - 1, ep.x + R); ++xx) {
                // 先端側（端点から線幅の半分以内）は一律に最大量、そこから L かけて 0 へ
                const float d = std::max(0.f, std::sqrt(float((xx - ep.x) * (xx - ep.x) + (yy - ep.y) * (yy - ep.y))) - half);
                if (d >= L) continue;
                if (comp[size_t(yy) * w + xx] != c0) continue;
                const float t = 1.f - d / L;
                float& e = E[size_t(yy) * w + xx];
                e = std::max(e, maxE * t * t * (3.f - 2.f * t));
            }
    }
    if (halves.empty()) return;
    auto hm = halves.begin() + halves.size() / 2;
    std::nth_element(halves.begin(), hm, halves.end());
    const float halfAll = *hm;

    // 削る: 輪郭から E より内側だけ残し、少しぼかして丸める
    std::vector<float> cut(n);
    for (size_t i = 0; i < n; ++i)
        cut[i] = E[i] > 0.f ? std::min(pool[i], Clamp01(dIn[i] - E[i] + 0.5f)) : pool[i];
    // 線幅の半分くらいのぼかしで切り直すと、四角い先端が丸くなる（輪郭は1px幅でアンチエイリアス）
    std::vector<float> s = cut;
    GaussianBlur(s, w, h, std::max(1.f, 0.6f * halfAll), threads);
    for (size_t i = 0; i < n; ++i) {
        if (E[i] <= 0.f) continue;
        const int x = int(i % size_t(w)), y = int(i / size_t(w));
        const float gx = 0.5f * (s[i + (x + 1 < w ? 1 : 0)] - s[i - (x > 0 ? 1 : 0)]);
        const float gy = 0.5f * (s[i + (y + 1 < h ? size_t(w) : 0)] - s[i - (y > 0 ? size_t(w) : 0)]);
        const float grad = std::max(std::sqrt(gx * gx + gy * gy), 0.02f);
        const float v = Clamp01((s[i] - 0.5f) / grad + 0.5f);
        // 削る量がごく小さい所（細めの範囲の外れ）は元の形へなめらかに戻す
        const float k = Clamp01(E[i] / 0.5f);
        pool[i] = std::min(pool[i], v * k + pool[i] * (1.f - k));
    }
}

// --- 線の分かれ目（Y 字・T 字・交差）を探す ---
// line を細線化し、長さ prune 未満のヒゲを刈ってから、3 方向以上に枝分かれする画素を返す。
// 近い分かれ目（minDist 以内）は 1 つにまとめる。
struct Junction { int x, y; };
inline std::vector<Junction> FindJunctions(const std::vector<uint8_t>& line, int w, int h,
                                           int prune, float minDist) {
    std::vector<uint8_t> sk = line;
    Thin(sk, w, h);
    auto at = [&](int x, int y) -> int {
        return (x >= 0 && y >= 0 && x < w && y < h && sk[size_t(y) * w + x]) ? 1 : 0;
    };
    static const int rx[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    static const int ry[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    auto count = [&](int x, int y) {
        int c = 0;
        for (int k = 0; k < 8; ++k) c += at(x + rx[k], y + ry[k]);
        return c;
    };
    // 周囲8画素を一周して 0→1 に変わる回数 = そこから出ている枝の数
    auto crossings = [&](int x, int y) {
        int c = 0;
        for (int k = 0; k < 8; ++k) c += !at(x + rx[k], y + ry[k]) && at(x + rx[(k + 1) & 7], y + ry[(k + 1) & 7]);
        return c;
    };
    // ヒゲ刈り: 端点を prune 回削る（短い枝は消え、長い線は端が少し縮むだけ）
    for (int it = 0; it < prune; ++it) {
        std::vector<size_t> del;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (sk[size_t(y) * w + x] && (count(x, y) <= 1 || (crossings(x, y) == 1 && count(x, y) == 2)))
                    del.push_back(size_t(y) * w + x);
        if (del.empty()) break;
        for (size_t i : del) sk[i] = 0;
    }
    std::vector<Junction> js;
    const float md2 = minDist * minDist;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (!sk[size_t(y) * w + x] || crossings(x, y) < 3) continue;
            bool merged = false;
            for (const Junction& j : js)
                if (float((j.x - x) * (j.x - x) + (j.y - y) * (j.y - y)) < md2) { merged = true; break; }
            if (!merged) js.push_back({x, y});
        }
    return js;
}

inline float Luma(float r, float g, float b) { return 0.2126f * r + 0.7152f * g + 0.0722f * b; }

}  // namespace detail

// src から dst を作る。src と dst は同じサイズ。
// 戻り値: 交差点が見つかったかどうか（見つからなければ dst は src のコピー）
inline bool Process(const Image& src, Image& dst, const Params& p) {
    using namespace detail;
    const int w = src.w, h = src.h;
    const size_t n = size_t(w) * size_t(h);
    dst = src;
    if (w <= 0 || h <= 0) return false;
    const int T = p.threads;

    // 1. 線の濃さマスク
    std::vector<float> m(n);
    for (size_t i = 0; i < n; ++i) {
        const float a = Clamp01(src.a[i]);
        switch (p.detect) {
            case kDetectDarkLines:   m[i] = Clamp01(1.f - Luma(src.r[i], src.g[i], src.b[i])) * a; break;
            case kDetectBrightLines: m[i] = Clamp01(Luma(src.r[i], src.g[i], src.b[i])) * a; break;
            default:                 m[i] = a; break;
        }
    }
    std::vector<uint8_t> line(n);
    size_t lineCount = 0;
    for (size_t i = 0; i < n; ++i) { line[i] = m[i] >= p.lineThreshold; lineCount += line[i]; }
    if (lineCount == 0 || p.meltSize <= 0.f) return false;
    const float r = p.meltSize;

    // 1b. 小さな独立した形（目・鼻・点など）は溶かさずそのまま残す。
    //     線とつながっていない小さな塊を探し、密度の計算から外す（まわりも少し含めて dot）。
    std::vector<uint8_t> dot(n, 0);
    {
        const int maxDim = int(std::max(12.f, 8.f * r));
        std::vector<uint8_t> seen(n, 0);
        std::vector<size_t> stack, comp;
        for (size_t s0 = 0; s0 < n; ++s0) {
            if (!line[s0] || seen[s0]) continue;
            comp.clear();
            stack.push_back(s0);
            seen[s0] = 1;
            int x0 = w, y0 = h, x1 = -1, y1 = -1;
            bool big = false;
            while (!stack.empty()) {
                const size_t i = stack.back();
                stack.pop_back();
                if (!big) comp.push_back(i);
                const int x = int(i % size_t(w)), y = int(i / size_t(w));
                x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
                if (x1 - x0 >= maxDim || y1 - y0 >= maxDim) big = true;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = x + dx, yy = y + dy;
                        if ((!dx && !dy) || xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                        const size_t j = size_t(yy) * w + xx;
                        if (line[j] && !seen[j]) { seen[j] = 1; stack.push_back(j); }
                    }
            }
            if (big) continue;
            for (size_t i : comp) {
                const int x = int(i % size_t(w)), y = int(i / size_t(w));
                for (int yy = std::max(0, y - 2); yy <= std::min(h - 1, y + 2); ++yy)
                    for (int xx = std::max(0, x - 2); xx <= std::min(w - 1, x + 2); ++xx) dot[size_t(yy) * w + xx] = 1;
            }
        }
    }
    // 1c. 太い線や塗りつぶした面（文字、色を塗った面など）も溶かさない。
    //     半径 thickHalf の円が収まる部分（オープニング）とそのまわりを dot に加える。
    {
        const float thickHalf = std::max(4.f, 0.75f * r);
        std::vector<uint8_t> bg(n), core(n);
        for (size_t i = 0; i < n; ++i) bg[i] = !line[i];
        const std::vector<float> dIn = DistanceTo(bg, w, h, T);
        bool anyCore = false;
        // 黒い（濃さがほぼ 1 の）太線や文字は普通の線として溶かす。色の面だけを塗りとみなす
        for (size_t i = 0; i < n; ++i) { core[i] = dIn[i] > thickHalf && m[i] < 0.9f; anyCore = anyCore || core[i]; }
        if (anyCore) {
            // 太い部分から近い画素を、近い順にたどって太い部分の色を受け継ぐ。
            // 色が近い画素だけを「塗り」とみなす（色の面を縁取る黒い線は溶かす対象に残す）。
            const float reach = thickHalf + 2.f;
            const std::vector<float> dc = DistanceTo(core, w, h, T);
            std::vector<int> owner(n, -1);
            std::vector<size_t> q;
            for (size_t i = 0; i < n; ++i) if (core[i]) { owner[i] = int(i); q.push_back(i); }
            for (size_t qi = 0; qi < q.size(); ++qi) {
                const size_t i = q[qi];
                const int x = int(i % size_t(w)), y = int(i / size_t(w));
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int xx = x + dx, yy = y + dy;
                        if ((!dx && !dy) || xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                        const size_t j = size_t(yy) * w + xx;
                        if (owner[j] >= 0 || dc[j] > reach) continue;
                        owner[j] = owner[i];
                        q.push_back(j);
                    }
            }
            for (size_t i = 0; i < n; ++i) {
                if (owner[i] < 0) continue;
                const size_t o = size_t(owner[i]);
                const float d = std::fabs(src.r[i] - src.r[o]) + std::fabs(src.g[i] - src.g[o]) + std::fabs(src.b[i] - src.b[o]);
                if (line[i] && d < 0.35f) dot[i] = 1;
            }
        }
    }
    std::vector<uint8_t> lineG(n);  // 溶かす対象の線（小さな形・太い部分を除く）
    size_t lineGCount = 0;
    for (size_t i = 0; i < n; ++i) { lineG[i] = line[i] && !dot[i]; lineGCount += lineG[i]; }
    if (lineGCount == 0) return false;

    // 2. 線密度: 線をぼかすと、線が交わる所・近づく所・内側に曲がる所ほど値が大きくなる
    std::vector<float> G = m;
    for (size_t i = 0; i < n; ++i) if (dot[i]) G[i] = 0.f;
    GaussianBlur(G, w, h, std::max(0.5f, r), T);
    // 基準は、塗りや小さな形から離れた線の上で測る（そばの塗りを外した分だけ密度が下がるため）
    std::vector<float> onLine;
    onLine.reserve(lineGCount);
    {
        const std::vector<float> dDot = DistanceTo(dot, w, h, T);
        for (size_t i = 0; i < n; ++i) if (lineG[i] && dDot[i] > 2.f * r) onLine.push_back(G[i]);
        if (onLine.empty())
            for (size_t i = 0; i < n; ++i) if (lineG[i]) onLine.push_back(G[i]);
    }
    auto mid = onLine.begin() + onLine.size() / 2;
    std::nth_element(onLine.begin(), mid, onLine.end());
    const float ref = std::max(1e-6f, *mid);  // 1本線の上での典型的な密度

    // 2b. 分かれ目を探す: Y 字や T 字に分かれる所は、角度が開いていると密度があまり
    //     上がらず溶けにくい（足先の付け根など）。あとで分かれ目のまわりだけ大きめに
    //     クロージングして、へこんだ角（線と線のあいだ）にだけ溜まりを作る。
    //     線の外側（平らな側）には膨らませない。
    std::vector<Junction> js;
    const float Rj = r * (0.5f + 1.5f * Clamp01(p.junction));
    if (p.junction > 0.f) {
        // 線幅の目安（線の画素から線の外までの距離の中央値 ≈ 線幅/4）
        std::vector<uint8_t> bg(n);
        for (size_t i = 0; i < n; ++i) bg[i] = !lineG[i];
        const std::vector<float> dIn = DistanceTo(bg, w, h, T);
        std::vector<float> ds;
        ds.reserve(lineGCount);
        for (size_t i = 0; i < n; ++i) if (lineG[i]) ds.push_back(dIn[i]);
        auto dm = ds.begin() + ds.size() / 2;
        std::nth_element(ds.begin(), dm, ds.end());
        const float halfW = std::max(1.f, 2.f * *dm);
        js = FindJunctions(lineG, w, h, int(std::ceil(3.f * halfW)) + 2, std::max(2.f, 0.5f * r));
    }

    // 3. 溶け: 密度が「1本線の上」より濃い所だけ線を太らせる（メタボール）。
    //    1本だけの線はそのままで、交点や近い線どうしが丸く溶け合う。
    //    溶け具合 0 → 1.25倍, 0.5 → 1.025倍, 1 → 0.8倍 の密度を輪郭にする。
    const float level = ref * (1.25f - 0.45f * Clamp01(p.strength));
    // 塗りつぶしにならないよう、線から離れすぎた所には広げない
    std::vector<float> distToLine = DistanceTo(line, w, h, T);
    const std::vector<float> distToLineG = DistanceTo(lineG, w, h, T);
    const float maxSpread = p.spread * r;
    std::vector<float> pool(n);
    for (size_t i = 0; i < n; ++i) {
        // 輪郭を1px幅でアンチエイリアス: (G - level) を勾配の大きさで割ると輪郭までの距離(px)になる
        const int x = int(i % size_t(w)), y = int(i / size_t(w));
        const float gx = 0.5f * (G[i + (x + 1 < w ? 1 : 0)] - G[i - (x > 0 ? 1 : 0)]);
        const float gy = 0.5f * (G[i + (y + 1 < h ? size_t(w) : 0)] - G[i - (y > 0 ? size_t(w) : 0)]);
        const float grad = std::max(std::sqrt(gx * gx + gy * gy), ref * 0.02f);
        const float v = Clamp01((G[i] - level) / grad + 0.5f) * Clamp01(maxSpread - distToLineG[i] + 1.f);
        pool[i] = std::max(m[i], v);
    }

    // 分かれ目の溜まり: 線を半径 Rj で太らせてから同じだけ細める（クロージング）と、
    // 線どうしのへこんだ角だけが丸く埋まる。分かれ目から離れるほど弱める。
    if (!js.empty()) {
        std::vector<float> jw(n, 0.f);
        const float reach = 2.5f * Rj;
        const int R = int(std::ceil(reach));
        for (const Junction& j : js)
            for (int y = std::max(0, j.y - R); y <= std::min(h - 1, j.y + R); ++y)
                for (int x = std::max(0, j.x - R); x <= std::min(w - 1, j.x + R); ++x) {
                    const float d = std::sqrt(float((x - j.x) * (x - j.x) + (y - j.y) * (y - j.y)));
                    float& v = jw[size_t(y) * w + x];
                    v = std::max(v, Clamp01((reach - d) / Rj));
                }
        std::vector<uint8_t> outside(n);
        for (size_t i = 0; i < n; ++i) outside[i] = distToLineG[i] > Rj;
        const std::vector<float> dC = DistanceTo(outside, w, h, T);
        for (size_t i = 0; i < n; ++i)
            if (jw[i] > 0.f) pool[i] = std::max(pool[i], Clamp01(dC[i] - Rj + 0.5f) * jw[i]);
    }

    bool any = false;
    for (size_t i = 0; i < n && !any; ++i) any = pool[i] > m[i] + 1e-3f;
    if (!any) return false;

    // 3b. 角の丸め
    //   (1) 線と溶けた部分を合わせた形を半径 melt の円でクロージングし、へこんだ角
    //       （溶けた部分と線の付け根など）を丸いフィレットにする。
    //       ただし閉じた隙間がまるごと埋まる場合は埋めない（塗りつぶしはしない）。
    //   (2) 全体を線幅より小さいσでぼかして 0.5 で切り直し、とがった先や
    //       四角い線端も丸くする。まっすぐな線の太さはほとんど変わらない。
    std::vector<float> rounded;
    if (p.melt > 0.f) {
        const float rho = p.melt;
        std::vector<uint8_t> ink(n), mask(n);
        for (size_t i = 0; i < n; ++i) ink[i] = pool[i] >= 0.5f;
        const std::vector<float> dOut = DistanceTo(ink, w, h, T);
        for (size_t i = 0; i < n; ++i) mask[i] = dOut[i] > rho;
        const std::vector<float> dIn = DistanceTo(mask, w, h, T);
        std::vector<float> closedV(n);
        for (size_t i = 0; i < n; ++i) closedV[i] = Clamp01(dIn[i] - rho + 0.5f);

        // まるごと埋まってしまう背景の島（小さな穴）を探して、そこは元に戻す
        std::vector<uint8_t> seen(n, 0);
        std::vector<int> stack;
        std::vector<size_t> comp;
        for (size_t start = 0; start < n; ++start) {
            if (seen[start] || ink[start]) continue;
            comp.clear();
            bool survives = false;
            stack.push_back(int(start));
            seen[start] = 1;
            while (!stack.empty()) {
                const int idx = stack.back(); stack.pop_back();
                comp.push_back(size_t(idx));
                if (closedV[size_t(idx)] < 0.5f) survives = true;
                const int x = idx % w, y = idx / w;
                const int nb[4][2] = {{x - 1, y}, {x + 1, y}, {x, y - 1}, {x, y + 1}};
                for (const auto& q : nb) {
                    if (q[0] < 0 || q[1] < 0 || q[0] >= w || q[1] >= h) { survives = true; continue; }
                    const size_t qi = size_t(q[1]) * w + q[0];
                    if (seen[qi] || ink[qi]) continue;
                    seen[qi] = 1;
                    stack.push_back(int(qi));
                }
            }
            if (!survives) for (size_t qi : comp) closedV[qi] = 0.f;
        }
        for (size_t i = 0; i < n; ++i) pool[i] = std::max(pool[i], closedV[i]);

        // (2) 線幅の目安: 線の画素から線の外までの距離の中央値 ≈ 線幅/4
        std::vector<uint8_t> notLine(n);
        for (size_t i = 0; i < n; ++i) notLine[i] = !line[i];
        const std::vector<float> dLineIn = DistanceTo(notLine, w, h, T);
        std::vector<float> inner;
        inner.reserve(lineCount);
        for (size_t i = 0; i < n; ++i) if (line[i]) inner.push_back(dLineIn[i]);
        auto im = inner.begin() + inner.size() / 2;
        std::nth_element(inner.begin(), im, inner.end());
        const float lineWidth = 4.f * *im;
        const float sigma = std::max(1.2f, std::min(0.5f * rho, 0.45f * lineWidth));
        std::vector<float> s2 = pool;
        GaussianBlur(s2, w, h, sigma, T);
        rounded.resize(n);
        for (size_t i = 0; i < n; ++i) {
            const int x = int(i % size_t(w)), y = int(i / size_t(w));
            const float gx = 0.5f * (s2[i + (x + 1 < w ? 1 : 0)] - s2[i - (x > 0 ? 1 : 0)]);
            const float gy = 0.5f * (s2[i + (y + 1 < h ? size_t(w) : 0)] - s2[i - (y > 0 ? size_t(w) : 0)]);
            const float grad = std::max(std::sqrt(gx * gx + gy * gy), 0.02f);
            rounded[i] = Clamp01((s2[i] - 0.5f) / grad + 0.5f);
        }
        // ぼかしで丸ごと消えた細い線や点は元のまま残す
        for (size_t i = 0; i < n; ++i) ink[i] = rounded[i] >= 0.5f;
        const std::vector<float> dR = DistanceTo(ink, w, h, T);
        for (size_t i = 0; i < n; ++i) if (dR[i] > sigma + 1.5f) rounded[i] = std::max(rounded[i], m[i]);
        // 丸めるのは溶けた部分のまわりだけ。離れた所の元の線は細らせない
        for (size_t i = 0; i < n; ++i) ink[i] = pool[i] > m[i] + 0.05f;
        const std::vector<float> dAdd = DistanceTo(ink, w, h, T);
        for (size_t i = 0; i < n; ++i) if (dAdd[i] > 2.f * sigma + 2.f) rounded[i] = std::max(rounded[i], m[i]);
        pool = rounded;
    }

    // 3c. すき間の維持: 線のあいだの細い溝や小さな穴を潰さない。
    //     各画素の近くで「線から一番遠い距離」（＝そこのすき間の半分の幅）を測り、
    //     線からの距離がその (1 - keepGaps) 倍を超える所は塗らない。
    //     交点の角のようにすき間が外へ開いている所は近くに広い場所があるので、
    //     この制限はほとんど掛からず、角はこれまでどおり丸く溶ける。
    if (p.keepGaps > 0.f) {
        std::vector<float> halfGap = distToLine;
        const int rad = std::max(2, int(std::ceil(1.5f * maxSpread + p.melt)));
        MaxFilter(halfGap, w, h, rad, T);
        const float keep = Clamp01(p.keepGaps);
        for (size_t i = 0; i < n; ++i) {
            if (line[i]) continue;
            // 少なくとも 2px ほどのすき間は残す
            const float allowed = std::min(halfGap[i] * (1.f - keep), halfGap[i] - 1.25f);
            const float lim = Clamp01(allowed - distToLine[i] + 0.5f);
            pool[i] = std::max(m[i], std::min(pool[i], lim));
        }
        // 残したすき間の先端がとがらないよう、軽くぼかして切り直す（線は細らせない）
        std::vector<float> s3 = pool;
        GaussianBlur(s3, w, h, 1.0f, T);
        for (size_t i = 0; i < n; ++i) {
            const int x = int(i % size_t(w)), y = int(i / size_t(w));
            const float gx = 0.5f * (s3[i + (x + 1 < w ? 1 : 0)] - s3[i - (x > 0 ? 1 : 0)]);
            const float gy = 0.5f * (s3[i + (y + 1 < h ? size_t(w) : 0)] - s3[i - (y > 0 ? size_t(w) : 0)]);
            const float grad = std::max(std::sqrt(gx * gx + gy * gy), 0.02f);
            const float v = Clamp01((s3[i] - 0.5f) / grad + 0.5f);
            pool[i] = std::max(m[i], std::min(v, std::max(pool[i], 0.f) + (line[i] ? 1.f : 0.25f)));
        }
    }

    // 4. 線の端を細めて丸める（口の両端など）
    //    仕上がった形を細線化して端点を探し、端点の近くほど強く輪郭を削る。
    //    削る量は端点で「線幅の半分 × endTaper」、端点から線幅の約2倍で 0 に戻る。
    //    点や短い塊（目など）は削らない。
    if (p.endTaper > 0.f) {
        TaperEnds(pool, w, h, Clamp01(p.endTaper), std::numeric_limits<float>::max(), dot, T);
    }

    // 小さな独立した形・太い部分はもとのまま。つなぎ目に段差が出ないよう、
    // そのまわりでも元の線より細くはしない。
    {
        const std::vector<float> dDot = DistanceTo(dot, w, h, T);
        for (size_t i = 0; i < n; ++i) {
            if (dot[i]) pool[i] = m[i];
            else if (dDot[i] <= 4.f) pool[i] = std::max(pool[i], m[i]);
        }
    }

    // 4b. 小さな穴を残す: 線で囲まれた小さなすき間（鼻の穴、文字の小さな囲みなど）は
    //     溶けや丸めで埋まらないよう、元の画像のまま戻す。
    if (p.keepGaps > 0.f) {
        const size_t maxArea = size_t(3.14159f * (2.f * r) * (2.f * r) * std::min(1.f, 2.f * p.keepGaps)) + 16;
        std::vector<int> label(n, -1);
        std::vector<size_t> stack, comp;
        for (size_t s0 = 0; s0 < n; ++s0) {
            if (line[s0] || label[s0] >= 0) continue;
            comp.clear();
            stack.push_back(s0);
            label[s0] = 1;
            bool border = false;
            while (!stack.empty()) {
                const size_t i = stack.back();
                stack.pop_back();
                comp.push_back(i);
                const int x = int(i % size_t(w)), y = int(i / size_t(w));
                if (x == 0 || y == 0 || x == w - 1 || y == h - 1) border = true;
                const int nx4[4] = {x - 1, x + 1, x, x};
                const int ny4[4] = {y, y, y - 1, y + 1};
                for (int k = 0; k < 4; ++k) {
                    if (nx4[k] < 0 || ny4[k] < 0 || nx4[k] >= w || ny4[k] >= h) continue;
                    const size_t j = size_t(ny4[k]) * w + nx4[k];
                    if (line[j] || label[j] >= 0) continue;
                    label[j] = 1;
                    stack.push_back(j);
                }
            }
            if (!border && comp.size() <= maxArea)
                for (size_t i : comp) pool[i] = std::min(pool[i], m[i]);
        }
    }

    // 5. 溶けた部分の色
    std::vector<float> cr, cg, cb;
    if (p.colorMode == kColorFromLines) {
        // 線の芯（近くで一番濃い画素）を探す。アンチエイリアスの画素は紙の色と混ざっているので使わない。
        std::vector<float> mloc(n);
        ParallelFor(h, T, [&](int y0, int y1) {
            for (int y = y0; y < y1; ++y)
                for (int x = 0; x < w; ++x) {
                    float mx = 0.f;
                    for (int dy = -2; dy <= 2; ++dy) {
                        const int yy = y + dy;
                        if (yy < 0 || yy >= h) continue;
                        for (int dx = -2; dx <= 2; ++dx) {
                            const int xx = x + dx;
                            if (xx >= 0 && xx < w) mx = std::max(mx, m[size_t(yy) * w + xx]);
                        }
                    }
                    mloc[size_t(y) * w + x] = mx;
                }
        });
        // 一番近い「線の芯」の色をそのまま使う（色を混ぜない＝グラデーションを作らない）
        cr.resize(n); cg.resize(n); cb.resize(n);
        std::vector<int> owner(n, -1);
        std::vector<size_t> q;
        q.reserve(n);
        for (size_t i = 0; i < n; ++i)
            if (line[i] && m[i] >= 0.9f * mloc[i]) { owner[i] = int(i); q.push_back(i); }
        if (q.empty())
            for (size_t i = 0; i < n; ++i) if (line[i]) { owner[i] = int(i); q.push_back(i); }
        for (size_t qi = 0; qi < q.size(); ++qi) {
            const size_t i = q[qi];
            const int x = int(i % size_t(w)), y = int(i / size_t(w));
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    const int xx = x + dx, yy = y + dy;
                    if ((!dx && !dy) || xx < 0 || yy < 0 || xx >= w || yy >= h) continue;
                    const size_t j = size_t(yy) * w + xx;
                    if (owner[j] < 0) { owner[j] = owner[i]; q.push_back(j); }
                }
        }
        for (size_t i = 0; i < n; ++i) {
            const size_t o = owner[i] >= 0 ? size_t(owner[i]) : i;
            cr[i] = src.r[o]; cg[i] = src.g[o]; cb[i] = src.b[o];
        }
    }

    // 6. 合成
    // 線の端を細めた所では線を消す必要があるので、紙の色（線でない所の平均色）を求めておく
    float paper[3] = {1.f, 1.f, 1.f};
    {
        double pr = 0, pg = 0, pb = 0, pc = 0;
        for (size_t i = 0; i < n; ++i)
            if (m[i] < 0.05f && src.a[i] > 0.5f) { pr += src.r[i]; pg += src.g[i]; pb += src.b[i]; pc += 1; }
        if (pc > 0) { paper[0] = float(pr / pc); paper[1] = float(pg / pc); paper[2] = float(pb / pc); }
        else if (p.detect == kDetectBrightLines) { paper[0] = paper[1] = paper[2] = 0.f; }
    }
    // 紙の不透明度は場所ごとに求める（透明な背景の文字レイヤーなどでは、消した所は透明に戻す）
    std::vector<float> paperA;
    if (p.detect != kDetectAlpha) {
        bool erase = false;
        for (size_t i = 0; i < n && !erase; ++i) erase = pool[i] < m[i] - 1e-4f;
        if (erase) {
            std::vector<float> wp(n);
            paperA.resize(n);
            for (size_t i = 0; i < n; ++i) { wp[i] = m[i] < 0.05f ? 1.f : 0.f; paperA[i] = wp[i] * Clamp01(src.a[i]); }
            const float ps = std::max(2.f, r);
            GaussianBlur(wp, w, h, ps, T);
            GaussianBlur(paperA, w, h, ps, T);
            for (size_t i = 0; i < n; ++i) paperA[i] = wp[i] > 1e-4f ? Clamp01(paperA[i] / wp[i]) : 1.f;
        }
    }
    for (size_t i = 0; i < n; ++i) {
        const float target = pool[i];
        const float mi = m[i];
        if (target < mi - 1e-4f) {
            // 線を細めた所: 濃さを target まで下げる
            const float Q = Clamp01((mi - target) / std::max(1e-6f, mi));  // 消す割合
            if (p.output == kOutputPoolOnly) { dst.r[i] = dst.g[i] = dst.b[i] = dst.a[i] = 0.f; continue; }
            if (p.detect == kDetectAlpha) {
                dst.a[i] = Clamp01(src.a[i]) * (1.f - Q);
            } else {
                // 紙（色 paper, 不透明度 pa）へ近づける。乗算済みの色で混ぜる
                const float sa = Clamp01(src.a[i]), pa = paperA[i];
                const float oa = sa + (pa - sa) * Q;
                const float inv = oa > 1e-6f ? 1.f / oa : 0.f;
                dst.r[i] = (src.r[i] * sa + (paper[0] * pa - src.r[i] * sa) * Q) * inv;
                dst.g[i] = (src.g[i] * sa + (paper[1] * pa - src.g[i] * sa) * Q) * inv;
                dst.b[i] = (src.b[i] * sa + (paper[2] * pa - src.b[i] * sa) * Q) * inv;
                dst.a[i] = oa;
            }
            continue;
        }
        if (target <= mi + 1e-4f) {
            if (p.output == kOutputPoolOnly) { dst.r[i] = dst.g[i] = dst.b[i] = dst.a[i] = 0.f; }
            continue;
        }
        const float pr = p.colorMode == kColorFromLines ? cr[i] : p.color[0];
        const float pg = p.colorMode == kColorFromLines ? cg[i] : p.color[1];
        const float pb = p.colorMode == kColorFromLines ? cb[i] : p.color[2];
        // インクを重ねたとき濃さが target になる被覆率 P: mi + P(1-mi) = target
        const float P = Clamp01((target - mi) / std::max(1e-6f, 1.f - mi));

        if (p.output == kOutputPoolOnly) {
            dst.r[i] = pr; dst.g[i] = pg; dst.b[i] = pb; dst.a[i] = Clamp01(target - mi);
            continue;
        }
        const float sa = Clamp01(src.a[i]);
        if (p.detect == kDetectAlpha) {
            // 透明背景の線画: 溜まりを over 合成
            const float oa = P + sa * (1.f - P);
            const float inv = oa > 1e-6f ? 1.f / oa : 0.f;
            dst.r[i] = (pr * P + src.r[i] * sa * (1.f - P)) * inv;
            dst.g[i] = (pg * P + src.g[i] * sa * (1.f - P)) * inv;
            dst.b[i] = (pb * P + src.b[i] * sa * (1.f - P)) * inv;
            dst.a[i] = oa;
        } else {
            // 紙の上の線画: 色を塗り重ね、透明部分にはアルファも足す
            dst.r[i] = src.r[i] + (pr - src.r[i]) * P;
            dst.g[i] = src.g[i] + (pg - src.g[i]) * P;
            dst.b[i] = src.b[i] + (pb - src.b[i]) * P;
            dst.a[i] = sa + (1.f - sa) * P;
        }
    }
    return true;
}

}  // namespace inkpool
