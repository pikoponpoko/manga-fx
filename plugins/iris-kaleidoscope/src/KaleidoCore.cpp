// KaleidoCore.cpp
// Eye mask, eye detection, motif texture and kaleidoscope shading.

#include "KaleidoCore.h"

#include <cstring>

namespace kaleido {

namespace {

const double kPi = 3.14159265358979323846;

inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline float smooth01(float v) { v = clamp01(v); return v * v * (3 - 2 * v); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }
inline Color lerp(const Color& a, const Color& b, float t) {
	return { lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t) };
}

// ---------------------------------------------------------------------------
// Mask filters

// Separable min (erode) or max (dilate) filter with a square window.
void minMaxFilter(std::vector<float>& m, int w, int h, int r, bool erode) {
	if (r <= 0) return;
	std::vector<float> tmp(m.size());
	auto pick = [erode](float a, float b) { return erode ? std::min(a, b) : std::max(a, b); };
	parallelFor(h, [&](int b, int e) {
		for (int y = b; y < e; ++y) {
			const float* row = &m[(size_t)y * w];
			for (int x = 0; x < w; ++x) {
				float v = row[x];
				for (int k = std::max(0, x - r); k <= std::min(w - 1, x + r); ++k) v = pick(v, row[k]);
				// Outside the canvas counts as empty, so eroding eats the border.
				if (erode && (x - r < 0 || x + r >= w)) v = 0;
				tmp[(size_t)y * w + x] = v;
			}
		}
	});
	parallelFor(w, [&](int b, int e) {
		for (int x = b; x < e; ++x) {
			for (int y = 0; y < h; ++y) {
				float v = tmp[(size_t)y * w + x];
				for (int k = std::max(0, y - r); k <= std::min(h - 1, y + r); ++k) v = pick(v, tmp[(size_t)k * w + x]);
				if (erode && (y - r < 0 || y + r >= h)) v = 0;
				m[(size_t)y * w + x] = v;
			}
		}
	});
}

// One box blur pass along rows (stride 1) or columns (stride w).
void boxPass(const std::vector<float>& in, std::vector<float>& out, int w, int h, int r, bool rows) {
	const int n = rows ? w : h, lines = rows ? h : w;
	const size_t step = rows ? 1 : (size_t)w;
	const float inv = 1.0f / (2 * r + 1);
	parallelFor(lines, [&](int b, int e) {
		for (int l = b; l < e; ++l) {
			const size_t base = rows ? (size_t)l * w : (size_t)l;
			double acc = 0;
			for (int k = -r; k <= r; ++k) if (k >= 0 && k < n) acc += in[base + k * step];
			for (int i = 0; i < n; ++i) {
				out[base + i * step] = (float)(acc * inv);
				const int add = i + r + 1, sub = i - r;
				if (add < n) acc += in[base + add * step];
				if (sub >= 0) acc -= in[base + sub * step];
			}
		}
	});
}

// Approximate Gaussian blur (three box passes) with standard deviation sigma.
void gaussBlur(std::vector<float>& m, int w, int h, float sigma) {
	if (sigma < 0.3f) return;
	const int r = std::max(1, (int)std::lround((std::sqrt(4.0 * sigma * sigma + 1.0) - 1.0) * 0.5));
	std::vector<float> tmp(m.size());
	for (int i = 0; i < 3; ++i) { boxPass(m, tmp, w, h, r, true); boxPass(tmp, m, w, h, r, false); }
}

// ---------------------------------------------------------------------------
// Eyes

struct Eye {
	double cx = 0, cy = 0;       // centre, buffer pixels in canvas coordinates
	double cosA = 1, sinA = 0;   // orientation of the oval
	double a = 1, b = 1;         // semi-axes of the ring oval, buffer pixels
};

// Finds the eyes in the mask: every solid blob becomes one eye whose centre
// and oval come from its moments. `label` receives the eye index of every
// pixel the effect touches (-1 = none).
std::vector<Eye> findEyes(const std::vector<float>& m, int w, int h, const Rect& canvas,
                          const Settings& s, std::vector<int>& label)
{
	label.assign((size_t)w * h, -1);
	std::vector<int> comp((size_t)w * h, -1);
	std::vector<int> queue;
	queue.reserve(1024);

	struct Stats { double n = 0, sx = 0, sy = 0, sxx = 0, syy = 0, sxy = 0; };
	std::vector<Stats> stats;
	for (int y0 = 0; y0 < h; ++y0) {
		for (int x0 = 0; x0 < w; ++x0) {
			const size_t i0 = (size_t)y0 * w + x0;
			if (m[i0] < 0.5f || comp[i0] >= 0) continue;
			const int id = (int)stats.size();
			stats.emplace_back();
			Stats& st = stats.back();
			queue.clear();
			queue.push_back((int)i0);
			comp[i0] = id;
			for (size_t q = 0; q < queue.size(); ++q) {
				const int i = queue[q], x = i % w, y = i / w;
				st.n += 1; st.sx += x; st.sy += y;
				st.sxx += (double)x * x; st.syy += (double)y * y; st.sxy += (double)x * y;
				const int nb[4] = { x > 0 ? i - 1 : -1, x < w - 1 ? i + 1 : -1, y > 0 ? i - w : -1, y < h - 1 ? i + w : -1 };
				for (int j : nb) {
					if (j >= 0 && comp[j] < 0 && m[j] >= 0.5f) { comp[j] = id; queue.push_back(j); }
				}
			}
		}
	}

	const double minR = 0.5 * s.minEyeSize * s.scale;
	const double minArea = std::max(4.0, kPi * minR * minR);
	std::vector<int> remap(stats.size(), -1);
	std::vector<Eye> eyes;
	for (size_t k = 0; k < stats.size(); ++k) {
		const Stats& st = stats[k];
		if (st.n < minArea) continue;
		const double mx = st.sx / st.n, my = st.sy / st.n;
		const double vxx = st.sxx / st.n - mx * mx + 1.0 / 12;
		const double vyy = st.syy / st.n - my * my + 1.0 / 12;
		const double vxy = st.sxy / st.n - mx * my;
		const double tr = 0.5 * (vxx + vyy), dd = std::sqrt(0.25 * (vxx - vyy) * (vxx - vyy) + vxy * vxy);
		const double l1 = tr + dd, l2 = std::max(1e-3, tr - dd);
		const double ang = 0.5 * std::atan2(2 * vxy, vxx - vyy);
		// A filled ellipse with semi-axis a has variance a^2/4 along that axis.
		// The visible eye is a cut in the skin over the eyeball: the ring oval
		// follows the eye's shape but is scaled up by Iris Size, so the pattern
		// carries on under the lids instead of starting at their edge.
		double a = 2 * std::sqrt(l1), b = 2 * std::sqrt(l2);
		b = a * std::pow(b / a, (double)clamp01(s.shapeFit));
		const double iris = std::max(0.05f, s.irisSize);
		a *= iris; b *= iris;

		Eye e;
		e.cx = mx + 0.5; e.cy = my + 0.5;
		if (s.centerMode == CENTER_MANUAL) {
			e.cx = s.centerX * s.scale - canvas.left;
			e.cy = s.centerY * s.scale - canvas.top;
		}
		e.cosA = std::cos(ang); e.sinA = std::sin(ang);
		e.a = std::max(1.0, a); e.b = std::max(1.0, b);
		remap[k] = (int)eyes.size();
		eyes.push_back(e);
	}

	// Seed the labels with the solid part, then grow them into the soft edge
	// (and paint gaps inside the eye) so partial pixels belong to their eye.
	queue.clear();
	for (size_t i = 0; i < comp.size(); ++i) {
		if (comp[i] >= 0 && remap[comp[i]] >= 0) { label[i] = remap[comp[i]]; queue.push_back((int)i); }
	}
	for (size_t q = 0; q < queue.size(); ++q) {
		const int i = queue[q], x = i % w, y = i / w;
		for (int dy = -1; dy <= 1; ++dy) {
			for (int dx = -1; dx <= 1; ++dx) {
				const int nx = x + dx, ny = y + dy;
				if ((dx | dy) == 0 || nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
				const int j = ny * w + nx;
				if (label[j] < 0 && m[j] > 0.002f) { label[j] = label[i]; queue.push_back(j); }
			}
		}
	}
	return eyes;
}

// ---------------------------------------------------------------------------
// Motif texture with a mip pyramid (premultiplied).

struct Level { int w = 0, h = 0; std::vector<RGBA> px; };

struct Texture {
	std::vector<Level> levels;
	float w = 0, h = 0;     // motif size (without padding), texels
	float pad = 0;          // transparent border around the motif, texels

	bool empty() const { return levels.empty(); }

	RGBA fetch(const Level& L, int x, int y) const {
		if (x < 0 || y < 0 || x >= L.w || y >= L.h) return { 0, 0, 0, 0 };
		return L.px[(size_t)y * L.w + x];
	}
	RGBA bilinear(int li, float x, float y) const {
		const Level& L = levels[li];
		const float k = 1.0f / (float)(1 << li);
		x = x * k - 0.5f; y = y * k - 0.5f;
		const int x0 = (int)std::floor(x), y0 = (int)std::floor(y);
		const float fx = x - x0, fy = y - y0;
		const RGBA a = fetch(L, x0, y0), b = fetch(L, x0 + 1, y0), c = fetch(L, x0, y0 + 1), d = fetch(L, x0 + 1, y0 + 1);
		auto mix = [&](float p, float q, float r, float t) { return lerp(lerp(p, q, fx), lerp(r, t, fx), fy); };
		return { mix(a.r, b.r, c.r, d.r), mix(a.g, b.g, c.g, d.g), mix(a.b, b.b, c.b, d.b), mix(a.a, b.a, c.a, d.a) };
	}
	// (x, y) in motif texels (0..w, 0..h); lod 0 = full detail.
	RGBA sample(float x, float y, float lod) const {
		x += pad; y += pad;
		const float maxLod = (float)(levels.size() - 1);
		lod = std::min(std::max(lod, 0.0f), maxLod);
		const int l0 = (int)lod;
		const float t = lod - l0;
		RGBA p = bilinear(l0, x, y);
		if (t > 1e-3f && l0 + 1 <= (int)maxLod) {
			RGBA q = bilinear(l0 + 1, x, y);
			p = { lerp(p.r, q.r, t), lerp(p.g, q.g, t), lerp(p.b, q.b, t), lerp(p.a, q.a, t) };
		}
		return p;
	}
};

// Halves a level with a [1 3 3 1] / 8 filter, which keeps the pyramid smooth
// (close to a Gaussian), so big blurs do not look blocky.
Level downsample(const Level& in) {
	Level out;
	out.w = std::max(1, (in.w + 1) / 2);
	out.h = std::max(1, (in.h + 1) / 2);
	const float wt[4] = { 0.125f, 0.375f, 0.375f, 0.125f };
	std::vector<RGBA> tmp((size_t)out.w * in.h);
	for (int y = 0; y < in.h; ++y) {
		for (int x = 0; x < out.w; ++x) {
			RGBA acc = { 0, 0, 0, 0 };
			for (int k = 0; k < 4; ++k) {
				const int sx = 2 * x + k - 1;
				if (sx < 0 || sx >= in.w) continue;
				const RGBA& p = in.px[(size_t)y * in.w + sx];
				acc.r += p.r * wt[k]; acc.g += p.g * wt[k]; acc.b += p.b * wt[k]; acc.a += p.a * wt[k];
			}
			tmp[(size_t)y * out.w + x] = acc;
		}
	}
	out.px.assign((size_t)out.w * out.h, RGBA{ 0, 0, 0, 0 });
	for (int y = 0; y < out.h; ++y) {
		for (int x = 0; x < out.w; ++x) {
			RGBA acc = { 0, 0, 0, 0 };
			for (int k = 0; k < 4; ++k) {
				const int sy = 2 * y + k - 1;
				if (sy < 0 || sy >= in.h) continue;
				const RGBA& p = tmp[(size_t)sy * out.w + x];
				acc.r += p.r * wt[k]; acc.g += p.g * wt[k]; acc.b += p.b * wt[k]; acc.a += p.a * wt[k];
			}
			out.px[(size_t)y * out.w + x] = acc;
		}
	}
	return out;
}

// Built-in motif: < 1 inside. u: -1..1 left to right, v: -1 (outer end) .. 1 (pupil end).
float shapeLevel(int shape, float u, float v) {
	switch (shape) {
	case SHAPE_DIAMOND:
		return std::fabs(u) / 0.6f + std::fabs(v) / 0.96f;
	case SHAPE_DOT:
		return std::sqrt(u * u + v * v) / 0.8f;
	case SHAPE_STAR: {
		const float r = std::sqrt(u * u + v * v), phi = std::atan2(u, -v);
		return r / (0.42f + 0.54f * std::pow(std::fabs(std::cos(2.5f * phi)), 4.0f));
	}
	case SHAPE_PETAL:
	default: {
		// Teardrop: round at the outer end, pointed toward the pupil.
		const float vv = v / 0.96f;
		if (std::fabs(vv) >= 1) return 2;
		const float hw = 0.9f * std::sqrt(1 - vv * vv) * (1 - vv) * 0.5f;
		return hw > 1e-4f ? std::fabs(u) / hw : 2;
	}
	}
}

// Draws the built-in motif with 4x4 supersampling: a light body with a darker
// ink rim, so tints and blend modes read well. The top points away from the pupil.
std::vector<RGBA> builtinShape(int shape, int n) {
	std::vector<RGBA> img((size_t)n * n, RGBA{ 0, 0, 0, 0 });
	const int ss = 4;
	const float rim = 0.07f;   // rim thickness in shape units
	const float dirs[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 0.7f, 0.7f }, { -0.7f, 0.7f }, { 0.7f, -0.7f }, { -0.7f, -0.7f } };
	for (int y = 0; y < n; ++y) {
		for (int x = 0; x < n; ++x) {
			float cover = 0, shade = 0;
			for (int sy = 0; sy < ss; ++sy) {
				for (int sx = 0; sx < ss; ++sx) {
					const float u = ((x + (sx + 0.5f) / ss) / n) * 2 - 1;
					const float v = ((y + (sy + 0.5f) / ss) / n) * 2 - 1;
					if (shapeLevel(shape, u, v) >= 1) continue;
					bool edge = false;
					for (const auto& d : dirs) {
						if (shapeLevel(shape, u + d[0] * rim, v + d[1] * rim) >= 1) { edge = true; break; }
					}
					cover += 1;
					// Soft highlight toward the outer end of the body.
					shade += edge ? 0.35f : 0.8f + 0.2f * (1 - (v + 1) * 0.5f);
				}
			}
			if (cover > 0) {
				const float c = shade / cover;
				img[(size_t)y * n + x] = { c, c, c, cover / (ss * ss) };
			}
		}
	}
	return img;
}

// Crops the motif to its visible part, adds a transparent border (room for
// blur) and builds the pyramid.
Texture buildTexture(const std::vector<RGBA>& straight, int w, int h) {
	Texture t;
	int x0 = w, y0 = h, x1 = -1, y1 = -1;
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			if (straight[(size_t)y * w + x].a > 0.004f) {
				x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
			}
		}
	}
	if (x1 < x0) return t;
	const int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
	const int pad = std::max(2, (int)(0.25f * std::max(cw, ch)));
	Level L;
	L.w = cw + 2 * pad; L.h = ch + 2 * pad;
	L.px.assign((size_t)L.w * L.h, RGBA{ 0, 0, 0, 0 });
	for (int y = 0; y < ch; ++y) {
		for (int x = 0; x < cw; ++x) {
			RGBA p = straight[(size_t)(y + y0) * w + x + x0];
			const float a = clamp01(p.a);
			L.px[(size_t)(y + pad) * L.w + x + pad] = { p.r * a, p.g * a, p.b * a, a };
		}
	}
	t.w = (float)cw; t.h = (float)ch; t.pad = (float)pad;
	t.levels.push_back(std::move(L));
	while (t.levels.back().w > 2 || t.levels.back().h > 2) {
		Level next = downsample(t.levels.back());
		t.levels.push_back(std::move(next));
		if (t.levels.size() > 16) break;
	}
	return t;
}

// Separable blend functions (W3C), straight colours.
inline float blendChannel(float s, float p, int mode) {
	switch (mode) {
	case BLEND_MULTIPLY: return s * p;
	case BLEND_SCREEN:   return s + p - s * p;
	case BLEND_ADD:      return s + p;
	case BLEND_OVERLAY:  return s <= 0.5f ? 2 * s * p : 1 - 2 * (1 - s) * (1 - p);
	case BLEND_NORMAL:
	default:             return p;
	}
}

} // namespace

std::vector<RGBA> process(const std::vector<RGBA>& src, const Rect& canvas,
                          const std::vector<RGBA>& pattern, int pw, int ph,
                          const Rect& outRect, const Settings& s)
{
	const int cw = canvas.width(), ch = canvas.height();
	const int ow = outRect.width(), oh = outRect.height();
	std::vector<RGBA> out((size_t)ow * oh, RGBA{ 0, 0, 0, 0 });

	auto srcAt = [&](int lx, int ly) -> RGBA {
		const int x = lx - canvas.left, y = ly - canvas.top;
		if (x < 0 || y < 0 || x >= cw || y >= ch) return { 0, 0, 0, 0 };
		return src[(size_t)y * cw + x];
	};
	auto passThrough = [&]() {
		if (s.composite == COMP_PATTERN_ONLY) return;
		for (int y = 0; y < oh; ++y)
			for (int x = 0; x < ow; ++x) out[(size_t)y * ow + x] = srcAt(x + outRect.left, y + outRect.top);
	};

	// --- Eye mask -------------------------------------------------------------
	std::vector<float> m((size_t)cw * ch, 0.0f);
	const float soft = std::max(1e-3f, s.keySoftness);
	parallelFor(ch, [&](int b, int e) {
		for (int y = b; y < e; ++y) {
			for (int x = 0; x < cw; ++x) {
				const RGBA& p = src[(size_t)y * cw + x];
				float v = clamp01(p.a);
				if (s.maskMode == MASK_KEY) {
					const float dr = p.r - s.keyColor.r, dg = p.g - s.keyColor.g, db = p.b - s.keyColor.b;
					const float dist = std::sqrt((dr * dr + dg * dg + db * db) / 3.0f);
					v *= 1.0f - smooth01((dist - s.keyTolerance) / soft);
				}
				m[(size_t)y * cw + x] = v;
			}
		}
	});
	const int chokePx = (int)std::lround(std::min(200.0f, std::fabs(s.choke) * s.scale));
	minMaxFilter(m, cw, ch, chokePx, s.choke > 0);
	gaussBlur(m, cw, ch, s.feather * s.scale);

	std::vector<int> label;
	std::vector<Eye> eyes = findEyes(m, cw, ch, canvas, s, label);
	if (eyes.empty()) { passThrough(); return out; }

	// --- Motif ----------------------------------------------------------------
	Texture tex = (pw > 0 && ph > 0 && pattern.size() >= (size_t)pw * ph)
		? buildTexture(pattern, pw, ph)
		: buildTexture(builtinShape(s.shape, 160), 160, 160);
	if (tex.empty()) { passThrough(); return out; }

	// --- Layout in log-polar space -------------------------------------------
	// Around the pupil, (ln r, angle) is a conformal map: copies laid out on a
	// grid in it keep their proportions while they shrink toward the centre.
	// Each copy is then drawn as a rigid sprite at its grid position, so its
	// outline stays smooth and neighbours may overlap instead of being cut
	// off at the cell borders.
	const int copies = std::max(1, s.copies);
	const double A = 2 * kPi / copies;                      // cell width (log units)
	const double copySize = std::min(3.0, std::max(0.05, (double)s.copySize));
	const double motifW = A * copySize;
	const double motifH = motifW * tex.h / tex.w;
	const double H = std::max(1e-3, (motifH + A * std::max(0.0, 1 - copySize)) * std::max(0.05f, s.ringSpacing)); // ring height
	const double T = tex.w / motifW;                        // texels per log unit
	const double pupil = std::min(0.95, std::max(0.0, (double)s.pupilSize));
	const double lnIn = -std::log(std::max(0.02, pupil));   // depth at the pupil
	const double t = s.time;
	const double deg = kPi / 180.0;
	const double rot0 = (s.rotation + s.rotationSpeed * t) * deg;
	const double swayPhase = 2 * kPi * s.swaySpeed * t;
	const double wavePhase = 2 * kPi * s.waveSpeed * t;
	const double cr = std::cos(-s.copyRotation * deg), sr = std::sin(-s.copyRotation * deg);
	const float overlayAmt = clamp01(s.overlayAmount);
	const float blurDen = std::max(0.05f, 1 - clamp01(s.focusRange));
	const float texPadX = tex.pad, texPadY = tex.pad;

	// How many neighbouring cells a copy can reach into (it is drawn at the
	// tangential scale, so on a flat oval it can grow along the short axis).
	std::vector<int> reachQ(eyes.size()), reachS(eyes.size());
	for (size_t k = 0; k < eyes.size(); ++k) {
		const double aniso = std::max(eyes[k].a, eyes[k].b) / std::min(eyes[k].a, eyes[k].b);
		const double ext = 0.5 * std::sqrt(motifW * motifW + motifH * motifH) * aniso + 0.25 * A;
		reachQ[k] = std::min(8, (int)std::ceil(ext / A));
		reachS[k] = std::min(8, (int)std::ceil(ext / H));
	}
	auto rotAt = [&](double sc) {
		return rot0 + s.swayAmount * deg * std::sin(swayPhase - s.swayLag * deg * sc) + s.twist * deg * sc;
	};
	auto waveAt = [&](double theta, double depth) {
		if (s.waveAmount == 0) return 1.0;
		return std::max(0.1, 1 + s.waveAmount * std::sin(s.waveCount * theta + wavePhase + 0.9 * depth / H));
	};

	parallelFor(oh, [&](int b, int e) {
		for (int oy = b; oy < e; ++oy) {
			const int ly = oy + outRect.top;
			for (int ox = 0; ox < ow; ++ox) {
				const int lx = ox + outRect.left;
				const RGBA S = srcAt(lx, ly);
				RGBA& O = out[(size_t)oy * ow + ox];
				O = s.composite == COMP_PATTERN_ONLY ? RGBA{ 0, 0, 0, 0 } : S;

				const int x = lx - canvas.left, y = ly - canvas.top;
				if (x < 0 || y < 0 || x >= cw || y >= ch) continue;
				const size_t ci = (size_t)y * cw + x;
				const int k = label[ci];
				const float mv = m[ci];
				if (k < 0 || mv <= 0) continue;
				const Eye& E = eyes[k];

				// Position in the eye's oval, normalised so its edge is r = 1.
				const double dx = x + 0.5 - E.cx, dy = y + 0.5 - E.cy;
				const double ux = (dx * E.cosA + dy * E.sinA) / E.a;
				const double uy = (-dx * E.sinA + dy * E.cosA) / E.b;
				double r = std::max(1e-6, std::sqrt(ux * ux + uy * uy));
				const double theta = std::atan2(uy, ux);
				r /= waveAt(theta, -std::log(r));
				const double depth = -std::log(r);
				const float td = (float)std::min(1.0, std::max(0.0, depth / lnIn)); // 0 edge .. 1 pupil

				float pupilAlpha = 1;
				if (pupil > 0) pupilAlpha = smooth01((float)((r - pupil * 0.85) / (pupil * 0.15)));
				if (pupilAlpha <= 0) continue;

				// Draw the copies around this pixel, inner (deeper) rings first.
				const double sPix = depth / H + s.flowSpeed * t;
				const long long i0 = (long long)std::floor(sPix);
				RGBA P = { 0, 0, 0, 0 };
				for (long long i = i0 + reachS[k]; i >= i0 - reachS[k]; --i) {
					const double sc = (double)i + 0.5;
					const double depthC = (sc - s.flowSpeed * t) * H;
					if (depthC > 40) continue;
					const double rC0 = std::exp(-depthC);
					if (pupil > 0 && rC0 < pupil * 0.5) continue;
					const double rotC = rotAt(sc);
					const long long j0 = (long long)std::floor((theta - rotC) / A + s.stagger * (double)i);
					for (long long j = j0 - reachQ[k]; j <= j0 + reachQ[k]; ++j) {
						const double thC = rotC + ((double)j + 0.5 - s.stagger * (double)i) * A;
						const double cth = std::cos(thC), sth = std::sin(thC);
						const double rC = rC0 * waveAt(thC, depthC);
						// Copy centre and its outward direction in the eye's frame (px).
						const double ex = rC * cth * E.a, ey = rC * sth * E.b;
						const double len = std::sqrt(ex * ex + ey * ey);
						if (len < 1e-6) continue;
						const double upX = ex / len, upY = ey / len;
						// Pixels per log unit along the ring at the copy centre.
						const double pxPerLog = rC * std::sqrt(E.a * E.a * sth * sth + E.b * E.b * cth * cth);
						if (pxPerLog < 1e-4) continue;
						// Pixel relative to the copy, in log units (+y away from the pupil).
						const double qx = (ux * E.a - ex), qy = (uy * E.b - ey);
						double lx2 = (-upY * qx + upX * qy) / pxPerLog;
						const double ly2 = (upX * qx + upY * qy) / pxPerLog;
						if (s.mirror && (j & 1)) lx2 = -lx2;
						const double rx = lx2 * cr - ly2 * sr, ry = lx2 * sr + ly2 * cr;
						const float tx = (float)(tex.w * 0.5 + rx * T);
						const float ty = (float)(tex.h * 0.5 - ry * T);
						if (tx < -texPadX || ty < -texPadY || tx > tex.w + texPadX || ty > tex.h + texPadY) continue;

						// Texture footprint of one screen pixel, plus depth-of-field blur.
						const double texPerPx = T / pxPerLog;
						const float tdC = (float)std::min(1.0, std::max(0.0, depthC / lnIn));
						const float defocus = std::min(1.0f, std::max(0.0f, std::fabs(tdC - s.focusDepth) - s.focusRange) / blurDen);
						const double sigmaTex = s.blurAmount * s.scale * defocus * texPerPx;
						const double foot = std::sqrt(texPerPx * texPerPx + 4 * sigmaTex * sigmaTex);
						const RGBA c = tex.sample(tx, ty, (float)std::log2(std::max(1.0, foot)));
						if (c.a <= 1e-5f) continue;
						const float keep = 1 - c.a;
						P = { c.r + P.r * keep, c.g + P.g * keep, c.b + P.b * keep, c.a + P.a * keep };
					}
				}
				if (P.a <= 1e-5f) continue;

				Color c = { P.r / P.a, P.g / P.a, P.b / P.a };
				if (s.overlayMode != OVERLAY_OFF && overlayAmt > 0) {
					const Color oc = lerp(s.overlayOuter, s.overlayInner, td);
					Color rc = oc;
					switch (s.overlayMode) {
					case OVERLAY_TINT: {
						const float lum = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
						rc = { oc.r * lum, oc.g * lum, oc.b * lum };
						break;
					}
					case OVERLAY_MULTIPLY: rc = { c.r * oc.r, c.g * oc.g, c.b * oc.b }; break;
					case OVERLAY_SCREEN:   rc = { c.r + oc.r - c.r * oc.r, c.g + oc.g - c.g * oc.g, c.b + oc.b - c.b * oc.b }; break;
					default: break;
					}
					c = lerp(c, rc, overlayAmt);
				}
				c = lerp(c, s.fadeColor, clamp01(s.depthFade) * td);

				const float pa = clamp01(P.a * clamp01(s.opacity) * clamp01(mv) * pupilAlpha);
				if (s.composite == COMP_PATTERN_ONLY) {
					O = { c.r, c.g, c.b, pa };
					continue;
				}
				const float sa = clamp01(S.a);
				const float oa = pa + sa - pa * sa;
				if (oa <= 1e-6f) continue;
				auto ch1 = [&](float sc, float pc) {
					const float bl = blendChannel(sc, pc, s.blendMode);
					return (pc * pa * (1 - sa) + sc * sa * (1 - pa) + bl * pa * sa) / oa;
				};
				O = { ch1(S.r, c.r), ch1(S.g, c.g), ch1(S.b, c.b), oa };
			}
		}
	});
	return out;
}

} // namespace kaleido
