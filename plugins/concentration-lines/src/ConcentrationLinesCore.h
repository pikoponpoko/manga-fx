// ConcentrationLinesCore.h
// 漫画風の集中線 / 流線を描くための描画コア。
// After Effects SDK に依存しない純粋な C++ なので、プラグイン本体とプレビュー用ツールの両方から使う。
//
// 使い方:
//   cl::Settings s; ... 値を設定 ...
//   cl::Renderer r(s);              // 線のテーブルを作る（1フレームに1回）
//   float a = r.coverage(x, y);     // ピクセル中心 (x+0.5, y+0.5) での線の被覆率 0..1
//   cl::composite(...)              // ソース画素と合成
//
// coverage() は const で内部状態を書き換えないので、複数スレッドから同時に呼んでよい。

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

namespace cl {

enum Type {
    TYPE_RADIAL   = 0,  // 集中線
    TYPE_PARALLEL = 1   // 流線
};

enum CompositeMode {
    COMPOSITE_OVER       = 0,  // 素材の上に線を描く
    COMPOSITE_BEHIND     = 1,  // 素材の後ろに線を描く（透明部分にだけ線が見える）
    COMPOSITE_LINES_ONLY = 2   // 線だけを出力（背景は透明）
};

struct Settings {
    int    type          = TYPE_RADIAL;
    double cx            = 0.0;    // 中心 (px)
    double cy            = 0.0;
    double innerRx       = 300.0;  // 空白楕円の半径 (px)
    double innerRy       = 200.0;
    double angle         = 0.0;    // 楕円の回転 / 流線の向き (ラジアン)
    int    count         = 200;    // 集中線: 本数 / 流線: 1000px あたりの本数
    double width         = 8.0;    // 線の最大太さ (px)
    double widthJitter   = 0.3;    // 太さのばらつき 0..1
    double spacingJitter = 0.15;    // 間隔のばらつき 0..1
    double startJitter   = 0.25;    // 集中線の始点のばらつき (内側楕円に対する割合, 0..2)
    double taper         = 1.0;    // 入り抜きの割合 0..1。集中線: 外側の端で最大幅、内側へ向かって
                                   // 線の長さ×taper の区間で細くなる (1 = 外端から先端まで一様に細く)。
                                   // 流線: 線分の両端それぞれ 長さ/2×taper の区間で細くなる
    double length        = 0.0;    // 線の長さ (px, 0 = 無限)
    double lengthJitter  = 0.3;    // 長さのばらつき 0..1
    double feather       = 20.0;   // 流線: 空白楕円の境界のぼかし (px)
    double sharpness     = 0.5;    // 先端の鋭さ 0..1（0.5 = まっすぐなくさび形, 1 = 針のように細い）
    double softness      = 0.0;    // 線の輪郭のぼかし (px, 0 = くっきり)
    // 手描き風のがたつき
    double wobble        = 0.0;    // 線が横に揺れる量 (px)
    double wobbleSize    = 120.0;  // 揺れの波の長さ (px)
    double unevenness    = 0.0;    // 線の途中で太さがむらになる量 0..1
    // 中央の空白をレイヤーの形に合わせる（集中線のみ）。
    // shapeDistance(ux, uy) は中心から向き (ux, uy) に進んだとき、形の一番外側までの距離 (px)。
    // 設定されていれば楕円の代わりに使う。形に届かない向きは負の値を返す。
    std::function<double(double, double)> shapeDistance;
    double shapeMargin   = 20.0;   // 形からの余白 (px)
    double shapeSmooth   = 0.05;   // 形の凹凸をならす幅（一周に対する割合, 0..0.25）
    double lineRotation  = 0.0;    // 集中線だけを回す角度 (ラジアン, 楕円は回さない)
    double flowOffset    = 0.0;    // 流線を流れ方向にずらす量 (px)
    uint32_t seed        = 1;
    // 描画する範囲（レイヤーの矩形, px）。集中線は線がこの外枠に届く位置を「外端」とし、
    // そこで最も太くする（筆圧は外側が一番強く、内側へ弱くなる）。
    bool   hasFrame      = false;
    double frameX0 = 0.0, frameY0 = 0.0, frameX1 = 0.0, frameY1 = 0.0;
    double scale         = 1.0;    // ダウンサンプル倍率。流線の間隔 (1000px あたり) にだけ使う。
                                   // 他の px 単位の値は呼び出し側でスケール済みのものを渡す。
};

constexpr double kPi = 3.14159265358979323846;

// 時間で変化させる設定。すべて 0 なら静止画。
struct Animation {
    double flickerRate   = 0.0;    // 1 秒あたりに線の配置を切り替える回数
    double rotationSpeed = 0.0;    // 集中線の回転速度 (度/秒)
    double pulseAmount   = 0.0;    // 空白楕円の大きさが脈打つ量 (割合, 0.2 = ±20%)
    double pulseRate     = 0.0;    // 脈打つ速さ (回/秒)
    double flowSpeed     = 0.0;    // 流線が流れる速さ (px/秒, ダウンサンプル前)
};

// 時刻 t (秒) での設定を作る。scale はダウンサンプル倍率（px 単位の速さに掛ける）。
inline Settings animate(Settings s, const Animation& a, double t, double scale = 1.0) {
    if (a.flickerRate > 0.0) s.seed += (uint32_t)(int64_t)std::floor(t * a.flickerRate + 1e-6);
    s.lineRotation += a.rotationSpeed * t * kPi / 180.0;
    if (a.pulseAmount != 0.0 && a.pulseRate > 0.0) {
        const double k = 1.0 + a.pulseAmount * std::sin(2.0 * kPi * a.pulseRate * t);
        s.innerRx *= (k > 0.0 ? k : 0.0);
        s.innerRy *= (k > 0.0 ? k : 0.0);
    }
    s.flowOffset += a.flowSpeed * t * scale;
    return s;
}

// ---------------------------------------------------------------- 乱数
inline uint64_t splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// (seed, a, b, salt) から決定的に [0,1) の乱数を作る
inline double hash01(uint32_t seed, int64_t a, int64_t b, uint32_t salt) {
    uint64_t h = splitmix64((uint64_t)seed * 0x100000001B3ull ^ salt);
    h = splitmix64(h ^ (uint64_t)a);
    h = splitmix64(h ^ ((uint64_t)b * 0x9E3779B97F4A7C15ull));
    return (double)(h >> 11) * (1.0 / 9007199254740992.0);
}

inline double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

// 半幅 hw の線から距離 d の位置の被覆率。aa はフィルタ幅 (px)。
// aa = 1 で 1px の箱フィルタ相当（くっきり）、大きいほど輪郭がぼける。
inline double lineCoverage(double hw, double d, double aa = 1.0) {
    d = std::fabs(d);
    if (hw <= 0.0) return 0.0;
    if (hw < aa * 0.5) return (2.0 * hw / aa) * clamp01(1.0 - d / aa);  // 細い線は濃度で表す
    return clamp01((hw - d) / aa + 0.5);
}

// 中心 (cx, cy) から向き (ux, uy) へ maxR まで進み、alpha(x, y) が 0.5 を超える最も遠い距離を返す。
// 一度も超えなければ -1。粗い刻みで探してから 1px 単位で詰める。
template <typename AlphaFn>
double outermostHit(double cx, double cy, double ux, double uy, double maxR, double step, AlphaFn alpha) {
    double hit = -1.0;
    for (double r = 0.0; r <= maxR; r += step)
        if (alpha(cx + ux * r, cy + uy * r) > 0.5) hit = r;
    if (hit < 0.0) return -1.0;
    for (double r = hit + step; r > hit; r -= 1.0)  // 刻みの間を詰める
        if (alpha(cx + ux * r, cy + uy * r) > 0.5) return r;
    return hit;
}

class Renderer {
public:
    explicit Renderer(const Settings& s) : s_(s) {
        s_.count = std::max(1, s_.count);
        s_.width = std::max(0.0, s_.width);
        s_.taper = clamp01(s_.taper);
        aa_ = 1.0 + std::max(0.0, s_.softness);
        // 鋭さ 0..1 → 先端の太り方の指数 0.25..4（0.5 で直線的なくさび形）
        taperPow_ = std::pow(4.0, (clamp01(s_.sharpness) - 0.5) * 2.0);
        cosA_ = std::cos(s_.angle);
        sinA_ = std::sin(s_.angle);
        if (s_.type == TYPE_RADIAL) buildRadial();
    }

    // ピクセル (x, y) の中心における線の被覆率 0..1
    double coverage(double px, double py) const {
        const double x = px + 0.5 - s_.cx;
        const double y = py + 0.5 - s_.cy;
        return s_.type == TYPE_RADIAL ? radial(x, y) : parallel(x, y);
    }

private:
    struct RLine {
        double theta;   // 楕円空間での角度（ソート用）
        double ux, uy;  // ピクセル空間での向き（単位ベクトル）
        double start;   // 中心からの始点距離 (px)
        double end;     // 中心からの終点距離 (px)
        double hw;      // 最大半幅 (px, 外端での半幅)
        double taperLen;// 外端から内側へ細くなる区間の長さ (px)
        double outer;   // 外端（線が最も太くなる位置）までの中心からの距離 (px)
        int    id;      // がたつきの乱数に使う番号
    };

    Settings s_;
    double cosA_ = 1.0, sinA_ = 0.0;
    double aa_ = 1.0;
    double taperPow_ = 1.0;
    std::vector<RLine> lines_;
    double minStart_ = 0.0;
    double maxHw_ = 0.0;

    // ---------------------------------------------------------------- 集中線
    // 並びを均一にするため、
    //  - 線の向きは画面上の角度で等間隔に置く（楕円は始点の位置だけに使う）
    //  - 間隔のずれは隣の線と入れ替わらない範囲 (±半間隔 × ばらつき) に抑える
    //  - 太さ・始点・長さのばらつきは低食い違い列で配るので、似た線が固まらない
    void buildRadial() {
        const int n = std::min(s_.count, 20000);
        const double rx = std::max(s_.innerRx, 0.0);
        const double ry = std::max(s_.innerRy, 0.0);
        const double step = 2.0 * kPi / n;
        const double sj = clamp01(s_.spacingJitter);

        lines_.resize(n);
        std::vector<double> edges(n, 0.0);
        minStart_ = 1e30;
        maxHw_ = 0.0;
        for (int i = 0; i < n; ++i) {
            RLine& L = lines_[i];
            double th = (i + 0.5) * step + (hash01(s_.seed, i, 0, 1) - 0.5) * step * sj * 0.9 + s_.angle + s_.lineRotation;
            th = std::fmod(th, 2.0 * kPi);
            if (th < 0) th += 2.0 * kPi;
            L.theta = th;
            L.ux = std::cos(th);
            L.uy = std::sin(th);

            L.id = i;
            edges[i] = ellipseEdge(th, rx, ry);
        }
        if (s_.shapeDistance) applyShape(edges);
        for (int i = 0; i < n; ++i) {
            RLine& L = lines_[i];
            const double edge = edges[i];
            L.start = edge * (1.0 + s_.startJitter * spread(i, 2, kSeqA));
            if (s_.length > 0.0) {
                L.end = L.start + s_.length * (1.0 - s_.lengthJitter * spread(i, 3, kSeqC));
            } else {
                L.end = 1e30;
            }
            L.hw = 0.5 * s_.width * (1.0 - s_.widthJitter * spread(i, 4, kSeqB));
            // 外端 = 線の終点か、レイヤーの外枠に届く位置の近いほう
            const double outer = std::min(L.end, frameExit(L.ux, L.uy, L.start + 1000.0 * s_.scale));
            L.taperLen = std::max(outer - L.start, 1.0) * s_.taper;
            L.outer = std::max(outer, 1.0);
            minStart_ = std::min(minStart_, L.start);
        }
        // 線の間隔は中心から遠いほど広がる。外端での太さを中心からの距離に比例させて、
        // 画面の隅（遠い外端）でも辺の近く（近い外端）でも線と隙間の比率を揃える。
        // Line Width は外端までの平均距離での太さになる。
        double meanOuter = 0.0;
        for (const RLine& L : lines_) meanOuter += L.outer;
        meanOuter /= n;
        maxHw_ = 0.0;
        for (RLine& L : lines_) {
            L.hw *= L.outer / meanOuter;
            maxHw_ = std::max(maxHw_, L.hw);
        }
        std::sort(lines_.begin(), lines_.end(), [](const RLine& a, const RLine& b) { return a.theta < b.theta; });
    }

    // 低食い違い列 (i × 無理数 の小数部) に少しだけ乱数を混ぜた 0..1 の値。
    // 純粋な乱数より値が偏らず、隣り合う線どうしが似すぎない。
    static constexpr double kSeqA = 0.6180339887498949;  // 黄金比
    static constexpr double kSeqB = 0.4142135623730951;  // √2 - 1
    static constexpr double kSeqC = 0.7320508075688772;  // √3 - 1
    double spread(int64_t i, uint32_t salt, double seq) const {
        const double v = (double)i * seq + hash01(s_.seed, 0, 0, salt) + (hash01(s_.seed, i, 0, salt) - 0.5) * 0.3;
        return v - std::floor(v);
    }

    // この向きで中心から内側楕円の縁までの距離（楕円は Angle で回転）
    double ellipseEdge(double th, double rx, double ry) const {
        const double lc = std::cos(th - s_.angle), ls = std::sin(th - s_.angle);
        if (rx <= 0.0 || ry <= 0.0) return 0.0;
        return 1.0 / std::sqrt((lc / rx) * (lc / rx) + (ls / ry) * (ls / ry));
    }

    // 空白をレイヤーの形に合わせる。線は角度順に並んでいるので、隣り合う線どうしで
    // 「最大値 → 平均」とならして、形の細かい凹凸で線が内側へ食い込まないようにする。
    void applyShape(std::vector<double>& edges) const {
        const int n = (int)lines_.size();
        std::vector<double> d(n);
        int hits = 0;
        for (int i = 0; i < n; ++i) {
            d[i] = s_.shapeDistance(lines_[i].ux, lines_[i].uy);
            if (d[i] >= 0.0) ++hits;
        }
        if (hits == 0) return;  // 形が無い（空のレイヤー）ときは楕円のまま
        for (double& v : d) if (v < 0.0) v = 0.0;  // 形に当たらない向きは中心から描く

        const int k = (int)std::lround(std::min(std::max(s_.shapeSmooth, 0.0), 0.25) * n * 0.5);  // 片側の本数
        std::vector<double> mx(n), avg(n);
        for (int i = 0; i < n; ++i) {
            double m = 0.0;
            for (int j = -k; j <= k; ++j) m = std::max(m, d[((i + j) % n + n) % n]);
            mx[i] = m;
        }
        for (int i = 0; i < n; ++i) {
            double sum = 0.0;
            for (int j = -k; j <= k; ++j) sum += mx[((i + j) % n + n) % n];
            avg[i] = sum / (2 * k + 1);
        }
        for (int i = 0; i < n; ++i) edges[i] = std::max(avg[i], d[i]) + s_.shapeMargin;
    }

    // なめらかな 1 次元ノイズ（-1..1）。手描き風の揺れに使う
    double noise1(double x, int64_t id, uint32_t salt) const {
        const double fl = std::floor(x);
        const double f = x - fl;
        const int64_t i = (int64_t)fl;
        const double a = hash01(s_.seed, id, i, salt) * 2.0 - 1.0;
        const double b = hash01(s_.seed, id, i + 1, salt) * 2.0 - 1.0;
        const double u = f * f * (3.0 - 2.0 * f);
        return a + (b - a) * u;
    }
    // 2 オクターブ重ねた揺れ（-1..1 程度）
    double wobbleAt(double along, int64_t id, uint32_t salt) const {
        const double x = along / std::max(s_.wobbleSize, 1.0);
        return (noise1(x, id, salt) * 0.7 + noise1(x * 2.3 + 17.0, id, salt + 1) * 0.3);
    }

    // 中心から向き (ux, uy) に進んで外枠を出る距離。外枠が無いときは fallback。
    double frameExit(double ux, double uy, double fallback) const {
        if (!s_.hasFrame) return fallback;
        // 中心 (s_.cx, s_.cy) から見た外枠（ピクセル中心は +0.5 なので外枠は 0..W のまま）
        double tFar = 1e30;
        const double lo[2] = { s_.frameX0 - s_.cx, s_.frameY0 - s_.cy };
        const double hi[2] = { s_.frameX1 - s_.cx, s_.frameY1 - s_.cy };
        const double dir[2] = { ux, uy };
        for (int k = 0; k < 2; ++k) {
            if (std::fabs(dir[k]) < 1e-12) continue;
            const double t1 = lo[k] / dir[k], t2 = hi[k] / dir[k];
            tFar = std::min(tFar, std::max(t1, t2));
        }
        return tFar < 1e29 ? std::max(tFar, 0.0) : fallback;
    }

    // 先端からの距離 dist での太さの倍率 0..1
    double taperProfile(double dist, double taperLen) const {
        if (taperLen <= 0.0) return 1.0;
        const double f = clamp01(dist / taperLen);
        return taperPow_ == 1.0 ? f : std::pow(f, taperPow_);
    }

    double radialLine(const RLine& L, double x, double y, double* perp) const {
        const double t = x * L.ux + y * L.uy;       // 線方向の距離
        double d = -x * L.uy + y * L.ux;            // 線からの垂直距離
        *perp = (t > 0.0) ? std::fabs(d) - s_.wobble : 1e30;
        if (t <= L.start - 0.5 || t >= L.end + 0.5) return 0.0;
        double hw = L.hw * taperProfile(t - L.start, L.taperLen);
        // 線の間隔は中心からの距離に比例して開くので、太さを距離の 2 乗で増やして
        // 「線の占める割合」が外端へ向かってずっと増え続ける（外端が一番濃い）ようにする。
        // Taper を下げて太いまま残る区間でも、中ほどが外端より濃くならない。
        const double rr = std::min(t / L.outer, 1.0);
        hw *= rr * rr;
        if (s_.wobble > 0.0) d -= s_.wobble * wobbleAt(t, L.id, 21);
        if (s_.unevenness > 0.0) hw *= std::max(0.0, 1.0 + clamp01(s_.unevenness) * wobbleAt(t * 1.7, L.id, 23));
        double c = lineCoverage(hw, d, aa_);
        if (L.taperLen <= 0.0) c *= clamp01(t - L.start + 0.5);
        if (L.end < 1e29) c *= clamp01(L.end - t + 0.5);
        return c;
    }

    double radial(double x, double y) const {
        const int n = (int)lines_.size();
        if (n == 0) return 0.0;
        const double r2 = x * x + y * y;
        if (r2 < (minStart_ - 1.0) * (minStart_ - 1.0) && minStart_ > 1.0) return 0.0;

        double phi = std::atan2(y, x);
        if (phi < 0) phi += 2.0 * kPi;

        // phi 以上の最初の線
        int idx = (int)(std::lower_bound(lines_.begin(), lines_.end(), phi,
                        [](const RLine& L, double v) { return L.theta < v; }) - lines_.begin());

        const double limit = maxHw_ * (1.0 + clamp01(s_.unevenness)) + aa_;
        const int maxSteps = std::min(n, 512);
        double keep = 1.0;  // 1 - 被覆率 の積（重なりを自然に合成）
        // 角度が近い線から順に、両側へ「十分離れた線が2本続く」まで調べる
        int far = 0, cw = 0;
        for (; cw < maxSteps && far < 2; ++cw) {
            const RLine& L = lines_[(idx + cw) % n];
            double perp;
            keep *= 1.0 - radialLine(L, x, y, &perp);
            far = (perp > limit) ? far + 1 : 0;
        }
        far = 0;
        for (int k = 1; k < maxSteps && k <= n - cw && far < 2; ++k) {
            const RLine& L = lines_[((idx - k) % n + n) % n];
            double perp;
            keep *= 1.0 - radialLine(L, x, y, &perp);
            far = (perp > limit) ? far + 1 : 0;
        }
        return 1.0 - keep;
    }

    // ---------------------------------------------------------------- 流線
    // 線は「レーン」(流れに垂直な方向の等間隔の帯) ごとに1本。長さがある場合は
    // レーンを流れ方向に区切った「セル」ごとに1本の線分を置く。
    // すべてハッシュで決まるので、レイヤーサイズや描画範囲に依存せず無限に続く。
    double parallel(double x, double y) const {
        // u: 流れの方向, v: 垂直方向
        const double u0 = x * cosA_ + y * sinA_;
        const double v = -x * sinA_ + y * cosA_;
        const double u = u0 - s_.flowOffset;  // 線だけを流す（空白楕円は動かさない）

        // 空白楕円のマスク（楕円の回転も角度に従う）
        double mask = 1.0;
        if (s_.innerRx > 0.5 && s_.innerRy > 0.5) {
            const double q = std::sqrt((u0 / s_.innerRx) * (u0 / s_.innerRx) + (v / s_.innerRy) * (v / s_.innerRy));
            const double dist = (q - 1.0) * std::min(s_.innerRx, s_.innerRy);  // おおよその境界からの距離
            mask = (s_.feather > 0.0) ? clamp01(dist / s_.feather + 0.5) : clamp01(dist + 0.5);
            if (mask <= 0.0) return 0.0;
        }

        const double spacing = 1000.0 / s_.count * s_.scale;
        const int64_t lane0 = (int64_t)std::floor(v / spacing);
        const int reach = (int)std::ceil((s_.width * (1.0 + clamp01(s_.unevenness)) * 0.5 + aa_ + s_.wobble) / spacing) + 1;
        double keep = 1.0;
        for (int64_t lane = lane0 - reach; lane <= lane0 + reach; ++lane) {
            const double lv = (lane + 0.5 + (hash01(s_.seed, lane, 0, 11) - 0.5) * clamp01(s_.spacingJitter) * 0.9) * spacing;
            const double hwMax = 0.5 * s_.width * (1.0 - s_.widthJitter * spread(lane, 12, kSeqB));
            double d = v - lv;
            if (s_.wobble > 0.0) d -= s_.wobble * wobbleAt(u, lane, 31);
            double hwLane = hwMax;
            if (s_.unevenness > 0.0) hwLane *= std::max(0.0, 1.0 + clamp01(s_.unevenness) * wobbleAt(u * 1.7, lane, 33));
            if (std::fabs(d) > hwLane + aa_) continue;

            if (s_.length <= 0.0) {
                keep *= 1.0 - lineCoverage(hwLane, d, aa_);
                continue;
            }
            // 線分: セル長 = 長さ × 2（平均で半分が線、半分が隙間）
            const double cell = s_.length * 2.0;
            const double offset = hash01(s_.seed, lane, 0, 13) * cell;
            const int64_t c0 = (int64_t)std::floor((u - offset) / cell);
            for (int64_t c = c0 - 1; c <= c0 + 1; ++c) {
                const double len = s_.length * (1.0 - s_.lengthJitter * hash01(s_.seed, lane, c, 14));
                const double a = offset + c * cell + (cell - len) * hash01(s_.seed, lane, c, 15);
                const double b = a + len;
                if (u <= a - 0.5 || u >= b + 0.5) continue;
                const double tp = len * 0.5 * s_.taper;
                const double hw = hwLane * taperProfile(std::min(u - a, b - u), tp);
                double cov = lineCoverage(hw, d, aa_);
                if (tp <= 0.0) cov *= clamp01(std::min(u - a, b - u) + 0.5);
                keep *= 1.0 - cov;
            }
        }
        return (1.0 - keep) * mask;
    }

};

// ---------------------------------------------------------------- 合成
// ストレートアルファ (0..1) 同士の合成。After Effects のピクセルはストレートアルファ。
struct RGBA { double r, g, b, a; };

inline RGBA composite(const RGBA& src, const RGBA& lineColor, double coverage, int mode) {
    const double la = clamp01(coverage * lineColor.a);
    RGBA out;
    switch (mode) {
    case COMPOSITE_LINES_ONLY:
        out = { lineColor.r, lineColor.g, lineColor.b, la };
        return out;
    case COMPOSITE_BEHIND: {
        // 素材 over 線
        const double a = src.a + la * (1.0 - src.a);
        if (a <= 0.0) return { 0, 0, 0, 0 };
        out.r = (src.r * src.a + lineColor.r * la * (1.0 - src.a)) / a;
        out.g = (src.g * src.a + lineColor.g * la * (1.0 - src.a)) / a;
        out.b = (src.b * src.a + lineColor.b * la * (1.0 - src.a)) / a;
        out.a = a;
        return out;
    }
    case COMPOSITE_OVER:
    default: {
        const double a = la + src.a * (1.0 - la);
        if (a <= 0.0) return { 0, 0, 0, 0 };
        out.r = (lineColor.r * la + src.r * src.a * (1.0 - la)) / a;
        out.g = (lineColor.g * la + src.g * src.a * (1.0 - la)) / a;
        out.b = (lineColor.b * la + src.b * src.a * (1.0 - la)) / a;
        out.a = a;
        return out;
    }
    }
}

}  // namespace cl
