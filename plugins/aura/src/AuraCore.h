// AuraCore.h
// Host-independent core of the "Aura Contour" effect.
// It turns a layer's silhouette into an endless series of contour-shaped
// borders (rings) that radiate outward from the character.
//
// Nothing in this file depends on the After Effects SDK, so the same code
// is used by the plugin (Aura.cpp) and by the command-line test (test/).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

namespace aura {

enum SourceMode { SOURCE_ALPHA = 0, SOURCE_DARK = 1, SOURCE_BRIGHT = 2 };
// COLOR_STRIPES repeats the palette ring by ring (barber-pole style).
// COLOR_HSB_STEPS derives one solid colour per ring from Color 1 by stepping
// hue / saturation / brightness, so neighbouring rings keep hard edges.
enum ColorMode { COLOR_SOLID = 0, COLOR_GRADIENT = 1, COLOR_STRIPES = 2, COLOR_RAINBOW = 3, COLOR_HSB_STEPS = 4 };
// RING_CIRCLES ignores the silhouette and draws concentric circles around a centre point.
// After the last HSB step: keep the last colour, sweep back, start over, or
// keep generating colours with the same per-step change (hue goes on turning,
// saturation / brightness stop at their limits).
enum HsbLoop { HSB_HOLD = 0, HSB_PINGPONG = 1, HSB_REPEAT = 2, HSB_CONTINUE = 3 };
enum RingShape { RING_CONTOUR = 0, RING_CIRCLES = 1 };
enum Style { STYLE_LINES = 0, STYLE_FILL = 1, STYLE_FILL_LINES = 2 };
// CYCLE_PALETTE walks through the palette colours once per cycle.
enum ColorCycle { CYCLE_OFF = 0, CYCLE_HUE = 1, CYCLE_PALETTE = 2 };
const int kMaxPalette = 5;
enum CompositeMode { COMP_BEHIND = 0, COMP_AURA_ONLY = 1, COMP_OVER = 2, COMP_ADD = 3 };

struct Color { float r, g, b; };

// All distances are in full-resolution pixels; Settings::scale converts
// them to the (possibly downsampled) buffer resolution.
struct Settings {
	// Source
	int   sourceMode   = SOURCE_ALPHA;
	float threshold    = 0.5f;   // 0..1
	bool  fillInterior = true;   // treat enclosed areas as part of the silhouette
	float closeGaps    = 0.0f;   // px, bridges small gaps in line art (0 = rings enter every gap)
	float smoothness   = 2.0f;   // px, rounds the contour before the rings are built

	// Rings
	int   ringShape    = RING_CONTOUR;
	float centerX      = 0.0f;   // full-res layer pixels (Circles)
	float centerY      = 0.0f;
	int   style        = STYLE_LINES;
	float startOffset  = 2.0f;   // px gap between the character and the first ring
	float spacing      = 16.0f;  // px between ring centres
	float lineWidth    = 4.0f;   // px
	float softness     = 1.0f;   // px edge softness (glow when large)
	float outerWidth   = 1.0f;   // width multiplier at the far end (1 = constant)
	bool  fillComposition = true; // rings continue to the edge of the frame (ignores length / fadeOut)
	float length       = 800.0f; // px reach of the aura
	float fadeOut      = 400.0f; // px over which the outer end fades
	float cornerRounding = 0.0f; // px, rounds the sharp corners of the outer rings

	// Animation
	float ringOffset   = 0.0f;   // px, moves rings outward when increased
	float speed        = 0.0f;   // px per second, added to ringOffset over time
	float wobbleAmount = 0.0f;   // px
	float wobbleSize   = 80.0f;  // px
	float wobbleSpeed  = 1.0f;   // cycles per second (roughly)
	float wobbleSeed   = 0.0f;

	// Color / output
	int   colorMode    = COLOR_SOLID;
	int   paletteCount = 2;      // 1..kMaxPalette colours in use
	Color palette[kMaxPalette] = { { 0.35f, 0.85f, 1.0f }, { 0.75f, 0.25f, 1.0f },
	                               { 1.0f, 1.0f, 1.0f }, { 0.9f, 0.1f, 0.15f }, { 1.0f, 0.85f, 0.2f } };
	float gradientLength = 600.0f; // px
	float hueStep      = 30.0f;  // degrees per ring (Rainbow)
	int   colorCycle   = CYCLE_OFF;
	float cycleDuration = 2.0f;  // seconds per loop
	float cycleOffset  = 0.0f;   // 0..1 shift within the loop
	int   hsbSteps     = 6;      // rings per sweep (HSB Steps)
	float hsbHue       = 60.0f;  // degrees over one sweep
	float hsbSat       = 0.0f;   // -1..1 over one sweep
	float hsbBri       = -0.4f;  // -1..1 over one sweep
	int   hsbLoop      = HSB_CONTINUE; // what follows the last step
	float bandShading  = 0.0f;   // 0..1, gradient inside each filled band
	Color shadeColor   = { 1.0f, 0.75f, 0.15f }; // colour at the inner edge of a band
	Color fillColor    = { 0.2f, 0.45f, 1.0f }; // fill under the lines (Fill + Lines)
	float fillOpacity  = 1.0f;
	float opacity      = 1.0f;
	int   composite    = COMP_BEHIND;

	// Context
	float scale        = 1.0f;   // buffer pixels per full-res pixel (downsample)
	double time        = 0.0;    // seconds
};

// Integer rectangle in layer coordinates, right/bottom exclusive.
struct Rect {
	int left = 0, top = 0, right = 0, bottom = 0;
	int width()  const { return right - left; }
	int height() const { return bottom - top; }
	bool empty() const { return right <= left || bottom <= top; }
};

inline Rect unite(const Rect& a, const Rect& b) {
	if (a.empty()) return b;
	if (b.empty()) return a;
	return { std::min(a.left, b.left), std::min(a.top, b.top),
	         std::max(a.right, b.right), std::max(a.bottom, b.bottom) };
}

// How far (in buffer pixels) the aura can reach outside the silhouette.
inline float reach(const Settings& s) {
	// Fill Composition: grow far enough to cover an 8K frame from any layer
	// position; AE only ever asks for the part that is visible in the comp.
	if (s.fillComposition) return 8192.0f * s.scale;
	return (s.startOffset + s.length + s.wobbleAmount + s.lineWidth * std::max(1.0f, s.outerWidth) + s.softness) * s.scale + 2.0f;
}

// ---------------------------------------------------------------------------
// Small threading helper: runs fn(begin, end) over [0, n) in parallel chunks.
template <class F>
void parallelFor(int n, F&& fn) {
	unsigned hw = std::thread::hardware_concurrency();
	int threads = (int)std::min(16u, std::max(1u, hw));
	if (n < 64 || threads <= 1) { fn(0, n); return; }
	threads = std::min(threads, n / 16 + 1);
	std::vector<std::thread> pool;
	int chunk = (n + threads - 1) / threads;
	for (int t = 0; t < threads; ++t) {
		int b = t * chunk, e = std::min(n, b + chunk);
		if (b >= e) break;
		pool.emplace_back([&fn, b, e] { fn(b, e); });
	}
	for (auto& th : pool) th.join();
}

// ---------------------------------------------------------------------------
// Distance field. `mask` holds the silhouette strength (0..1) for every pixel
// of a canvas of size w*h. Returns, for every pixel, the distance in buffer
// pixels to the nearest silhouette pixel (0 inside the silhouette).
std::vector<float> buildDistanceField(std::vector<float>& mask, int w, int h, const Settings& s);

// Smooth 3D gradient noise in roughly [-1, 1].
float noise3(float x, float y, float z);

// Aura colour for one pixel, premultiplied RGBA.
struct RGBA { float r, g, b, a; };
// (gx, gy) is the gradient of the distance field at the pixel; it is used to
// keep the line width constant where the field is stretched or squeezed.
RGBA shade(float dist, float gx, float gy, float fullResX, float fullResY, const Settings& s);

// Final mix of the source pixel (straight alpha) and the aura (premultiplied).
// Returns straight alpha.
inline RGBA compositePixel(RGBA src, RGBA aura, int mode) {
	RGBA sp = { src.r * src.a, src.g * src.a, src.b * src.a, src.a };
	RGBA o;
	switch (mode) {
	case COMP_AURA_ONLY:
		o = aura;
		break;
	case COMP_OVER:
		o = { aura.r + sp.r * (1 - aura.a), aura.g + sp.g * (1 - aura.a),
		      aura.b + sp.b * (1 - aura.a), aura.a + sp.a * (1 - aura.a) };
		break;
	case COMP_ADD:
		o = { sp.r + aura.r, sp.g + aura.g, sp.b + aura.b, sp.a + aura.a - sp.a * aura.a };
		break;
	case COMP_BEHIND:
	default:
		o = { sp.r + aura.r * (1 - sp.a), sp.g + aura.g * (1 - sp.a),
		      sp.b + aura.b * (1 - sp.a), sp.a + aura.a * (1 - sp.a) };
		break;
	}
	if (o.a > 1e-6f) { float inv = 1.0f / o.a; o.r *= inv; o.g *= inv; o.b *= inv; }
	else { o = { 0, 0, 0, 0 }; }
	return o;
}

// 2D affine map x' = a x + c y + e, y' = b x + d y + f.
struct Affine {
	double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
	void apply(double x, double y, double& ox, double& oy) const { ox = a * x + c * y + e; oy = b * x + d * y + f; }
	bool inverse(Affine& o) const {
		const double det = a * d - b * c;
		if (std::fabs(det) < 1e-12) return false;
		o.a = d / det; o.b = -b / det; o.c = -c / det; o.d = a / det;
		o.e = -(o.a * e + o.c * f); o.f = -(o.b * e + o.d * f);
		return true;
	}
};

// Another layer whose silhouette shapes the rings (e.g. the aura is applied to
// a background layer but should follow the character above it).
struct ShapeSource {
	const void* data = nullptr;
	long rowbytes = 0;
	Rect rect;              // its pixels, in its own layer buffer coordinates
	Affine toLayer;         // its buffer coordinates -> this layer's buffer coordinates
};

// ---------------------------------------------------------------------------
// Generic renderer. PixelTraits must provide:
//   using Pixel = ...;
//   static RGBA read(const Pixel&);      // straight alpha, 0..1 (may exceed 1 in float)
//   static void write(Pixel&, const RGBA&);
//
// `in`  : input pixels covering inRect  (rowbytes in bytes), may be null.
// `out` : output pixels covering outRect.
template <class PixelTraits>
void render(const void* inData, long inRowbytes, const Rect& inRect,
            void* outData, long outRowbytes, const Rect& outRect,
            const Settings& s, const ShapeSource* shape = nullptr)
{
	using Pixel = typename PixelTraits::Pixel;
	if (shape && !shape->data) shape = nullptr;
	Affine fromLayer;
	if (shape && !shape->toLayer.inverse(fromLayer)) shape = nullptr;

	auto inPixel = [&](int lx, int ly) -> RGBA {
		if (!inData || lx < inRect.left || ly < inRect.top || lx >= inRect.right || ly >= inRect.bottom)
			return { 0, 0, 0, 0 };
		const char* row = (const char*)inData + (long)(ly - inRect.top) * inRowbytes;
		return PixelTraits::read(((const Pixel*)row)[lx - inRect.left]);
	};

	// Canvas = input area plus output area, so distances are measured to the
	// whole silhouette even when the output is only a tile of it.
	// With a shape layer the canvas covers that layer's footprint instead.
	Rect canvas = unite(inRect, outRect);
	if (shape) {
		const Rect& r = shape->rect;
		double xs[4] = { (double)r.left, (double)r.right, (double)r.left, (double)r.right };
		double ys[4] = { (double)r.top, (double)r.top, (double)r.bottom, (double)r.bottom };
		double x0 = 1e30, y0 = 1e30, x1 = -1e30, y1 = -1e30;
		for (int i = 0; i < 4; ++i) {
			double X, Y;
			shape->toLayer.apply(xs[i], ys[i], X, Y);
			x0 = std::min(x0, X); y0 = std::min(y0, Y); x1 = std::max(x1, X); y1 = std::max(y1, Y);
		}
		// Clamp so a layer far off screen cannot blow up the canvas.
		const double lim = 40000.0;
		Rect sr = { (int)std::floor(std::max(-lim, x0)) - 1, (int)std::floor(std::max(-lim, y0)) - 1,
		            (int)std::ceil(std::min(lim, x1)) + 1, (int)std::ceil(std::min(lim, y1)) + 1 };
		canvas = sr.empty() ? outRect : unite(outRect, sr);
	}
	if (s.ringShape == RING_CIRCLES) canvas = outRect; // no silhouette needed
	const int cw = canvas.width(), ch = canvas.height();
	if (cw <= 0 || ch <= 0) return;

	// Bilinear sample of the shape layer at this layer's buffer position.
	auto shapePixel = [&](double lx, double ly) -> RGBA {
		double sx, sy;
		fromLayer.apply(lx + 0.5, ly + 0.5, sx, sy);
		sx -= 0.5; sy -= 0.5;
		const Rect& r = shape->rect;
		int x0 = (int)std::floor(sx), y0 = (int)std::floor(sy);
		float fx = (float)(sx - x0), fy = (float)(sy - y0);
		auto px = [&](int x, int y) -> RGBA {
			if (x < r.left || y < r.top || x >= r.right || y >= r.bottom) return { 0, 0, 0, 0 };
			const char* row = (const char*)shape->data + (long)(y - r.top) * shape->rowbytes;
			return PixelTraits::read(((const Pixel*)row)[x - r.left]);
		};
		RGBA a = px(x0, y0), b = px(x0 + 1, y0), c = px(x0, y0 + 1), d = px(x0 + 1, y0 + 1);
		auto mix = [&](float p, float q, float t, float u) {
			return (p * (1 - fx) + q * fx) * (1 - fy) + (t * (1 - fx) + u * fx) * fy;
		};
		return { mix(a.r, b.r, c.r, d.r), mix(a.g, b.g, c.g, d.g), mix(a.b, b.b, c.b, d.b), mix(a.a, b.a, c.a, d.a) };
	};

	std::vector<float> mask((size_t)cw * ch, 0.0f);
	if (s.ringShape != RING_CIRCLES) parallelFor(ch, [&](int b, int e) {
		for (int y = b; y < e; ++y) {
			int ly = y + canvas.top;
			for (int x = 0; x < cw; ++x) {
				RGBA p = shape ? shapePixel(x + canvas.left, ly) : inPixel(x + canvas.left, ly);
				float a = std::min(1.0f, std::max(0.0f, p.a));
				float m = a;
				if (s.sourceMode != SOURCE_ALPHA) {
					float luma = 0.2126f * p.r + 0.7152f * p.g + 0.0722f * p.b;
					luma = std::min(1.0f, std::max(0.0f, luma));
					m = (s.sourceMode == SOURCE_DARK ? 1.0f - luma : luma) * a;
				}
				mask[(size_t)y * cw + x] = m;
			}
		}
	});

	std::vector<float> dist;
	if (s.ringShape == RING_CIRCLES) {
		// Distance to the centre point; its slope is 1 everywhere, like the contour field.
		dist.resize((size_t)cw * ch);
		const float cx = s.centerX * s.scale, cy = s.centerY * s.scale;
		parallelFor(ch, [&](int b, int e) {
			for (int y = b; y < e; ++y)
				for (int x = 0; x < cw; ++x) {
					float dx = (float)(x + canvas.left) + 0.5f - cx, dy = (float)(y + canvas.top) + 0.5f - cy;
					dist[(size_t)y * cw + x] = std::max(0.01f, std::sqrt(dx * dx + dy * dy));
				}
		});
	} else {
		dist = buildDistanceField(mask, cw, ch, s);
	}

	const float invScale = s.scale > 0 ? 1.0f / s.scale : 1.0f;
	const int ow = outRect.width(), oh = outRect.height();
	parallelFor(oh, [&](int b, int e) {
		for (int oy = b; oy < e; ++oy) {
			int ly = oy + outRect.top;
			Pixel* row = (Pixel*)((char*)outData + (long)oy * outRowbytes);
			const float* drow = &dist[(size_t)(ly - canvas.top) * cw];
			for (int ox = 0; ox < ow; ++ox) {
				int lx = ox + outRect.left;
				const int cx = lx - canvas.left, cy = ly - canvas.top;
				float d = drow[cx];
				// Central differences of the distance field (unitless).
				float gx = 1.0f, gy = 0.0f;
				if (d > 0 && cx > 0 && cx < cw - 1 && cy > 0 && cy < ch - 1) {
					float l = drow[cx - 1], r = drow[cx + 1], u = drow[cx - cw], dn = drow[cx + cw];
					if (l > 0 && r > 0 && u > 0 && dn > 0 && l < 1e19f && r < 1e19f && u < 1e19f && dn < 1e19f) {
						gx = 0.5f * (r - l); gy = 0.5f * (dn - u);
					}
				}
				RGBA a = shade(d, gx, gy, lx * invScale, ly * invScale, s);
				RGBA src = inPixel(lx, ly);
				PixelTraits::write(row[ox], compositePixel(src, a, s.composite));
			}
		}
	});
}

} // namespace aura
