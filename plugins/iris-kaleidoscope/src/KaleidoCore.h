// KaleidoCore.h
// Host-independent core of the "Iris Kaleidoscope" effect.
// It finds the eyes (iris fill) in a layer and fills each one with copies of
// a motif arranged radially around the pupil, shrinking toward the centre
// like a tunnel, so the eye looks like a turning kaleidoscope.
//
// Nothing in this file depends on the After Effects SDK, so the same code
// is used by the plugin (Kaleido.cpp) and by the command-line test (test/).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

namespace kaleido {

enum MaskMode { MASK_ALPHA = 0, MASK_KEY = 1 };
enum CenterMode { CENTER_AUTO = 0, CENTER_MANUAL = 1 };
// Built-in motif, used while no Pattern Layer is set.
enum Shape { SHAPE_PETAL = 0, SHAPE_DIAMOND = 1, SHAPE_DOT = 2, SHAPE_STAR = 3 };
enum OverlayMode { OVERLAY_OFF = 0, OVERLAY_TINT = 1, OVERLAY_MULTIPLY = 2, OVERLAY_SCREEN = 3, OVERLAY_NORMAL = 4 };
enum BlendMode { BLEND_NORMAL = 0, BLEND_MULTIPLY = 1, BLEND_SCREEN = 2, BLEND_ADD = 3, BLEND_OVERLAY = 4 };
enum CompositeMode { COMP_OVER = 0, COMP_PATTERN_ONLY = 1 };

struct Color { float r, g, b; };
struct RGBA { float r, g, b, a; };

// Distances are in full-resolution pixels; Settings::scale converts them to
// the (possibly downsampled) buffer resolution. Fractions are 0..1.
struct Settings {
	// Eye mask
	int   maskMode     = MASK_ALPHA;
	Color keyColor     = { 0.25f, 0.45f, 0.85f };
	float keyTolerance = 0.20f;   // colour distance that still counts as the eye
	float keySoftness  = 0.15f;   // falloff beyond the tolerance (for painted / analog fills)
	float choke        = 0.0f;    // px, > 0 shrinks the mask, < 0 grows it
	float feather      = 1.0f;    // px blur of the mask edge
	float minEyeSize   = 12.0f;   // px, smaller blobs (paint specks) are ignored

	// Centre / shape
	int   centerMode   = CENTER_AUTO;
	float centerX = 0, centerY = 0; // Manual centre in full-res layer pixels
	float shapeFit     = 1.0f;    // 1 = rings follow the eye's oval, 0 = circular rings
	float irisSize     = 1.25f;   // ring oval relative to the visible eye; > 1 continues under the lids
	float pupilSize    = 0.12f;   // radius of the untouched centre, relative to the eye

	// Pattern
	int   shape        = SHAPE_PETAL;
	int   copies       = 36;      // copies per ring
	float ringSpacing  = 1.0f;    // 1 = copies keep their proportions
	float copySize     = 0.9f;    // motif width relative to its cell (> 1 overlaps the neighbours)
	float copyRotation = 0.0f;    // degrees
	bool  mirror       = true;    // flip every other copy (kaleidoscope)
	float stagger      = 0.5f;    // shift of every other ring, in copies

	// Motion
	float rotation      = 0.0f;   // degrees
	float rotationSpeed = 20.0f;  // degrees per second
	float swayAmount    = 8.0f;   // degrees
	float swaySpeed     = 0.5f;   // swings per second
	float swayLag       = 40.0f;  // degrees of sway phase per ring (inner rings follow later)
	float twist         = 0.0f;   // degrees per ring (spiral)
	float flowSpeed     = 0.0f;   // rings per second moving toward the pupil
	float waveAmount    = 0.03f;  // ring waviness, relative to the radius
	float waveCount     = 7.0f;   // waves around a ring
	float waveSpeed     = 0.3f;   // cycles per second

	// Depth
	float depthFade    = 0.5f;    // blend toward fadeColor at the pupil
	Color fadeColor    = { 0, 0, 0 };
	float blurAmount   = 2.5f;    // px of blur at full defocus
	float focusDepth   = 0.0f;    // 0 = eye edge in focus, 1 = pupil in focus
	float focusRange   = 0.15f;   // depth range that stays sharp

	// Colour overlay
	int   overlayMode   = OVERLAY_OFF;
	Color overlayOuter  = { 0.30f, 0.85f, 1.00f };
	Color overlayInner  = { 0.65f, 0.25f, 1.00f };
	float overlayAmount = 1.0f;

	// Output
	int   blendMode    = BLEND_NORMAL;
	float opacity      = 1.0f;
	int   composite    = COMP_OVER;

	// Context
	float scale        = 1.0f;    // buffer pixels per full-res pixel (downsample)
	double time        = 0.0;     // seconds
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

// Renders the effect for outRect.
//   src     : straight-alpha pixels of `canvas` (row-major, canvas.width() wide)
//   pattern : straight-alpha motif pixels (pw x ph); empty = built-in shape
// Returns straight-alpha pixels for outRect.
std::vector<RGBA> process(const std::vector<RGBA>& src, const Rect& canvas,
                          const std::vector<RGBA>& pattern, int pw, int ph,
                          const Rect& outRect, const Settings& s);

// ---------------------------------------------------------------------------
// Generic renderer. PixelTraits must provide:
//   using Pixel = ...;
//   static RGBA read(const Pixel&);      // straight alpha, 0..1 (may exceed 1 in float)
//   static void write(Pixel&, const RGBA&);
//
// `in`      : layer pixels covering inRect (rowbytes in bytes), may be null.
// `pattern` : motif layer pixels (pw x ph), may be null.
// `out`     : output pixels covering outRect.
template <class PixelTraits>
void render(const void* inData, long inRowbytes, const Rect& inRect,
            const void* patData, long patRowbytes, int pw, int ph,
            void* outData, long outRowbytes, const Rect& outRect,
            const Settings& s)
{
	using Pixel = typename PixelTraits::Pixel;

	// The whole eye is needed to find its centre, so work on input + output.
	Rect canvas = unite(inRect, outRect);
	const int cw = canvas.width(), ch = canvas.height();
	if (cw <= 0 || ch <= 0 || outRect.empty()) return;

	std::vector<RGBA> src((size_t)cw * ch, RGBA{ 0, 0, 0, 0 });
	if (inData) {
		parallelFor(inRect.height(), [&](int b, int e) {
			for (int y = b; y < e; ++y) {
				const Pixel* row = (const Pixel*)((const char*)inData + (long)y * inRowbytes);
				RGBA* dst = &src[(size_t)(y + inRect.top - canvas.top) * cw + (inRect.left - canvas.left)];
				for (int x = 0; x < inRect.width(); ++x) dst[x] = PixelTraits::read(row[x]);
			}
		});
	}

	std::vector<RGBA> pat;
	if (patData && pw > 0 && ph > 0) {
		pat.resize((size_t)pw * ph);
		for (int y = 0; y < ph; ++y) {
			const Pixel* row = (const Pixel*)((const char*)patData + (long)y * patRowbytes);
			for (int x = 0; x < pw; ++x) pat[(size_t)y * pw + x] = PixelTraits::read(row[x]);
		}
	} else {
		pw = ph = 0;
	}

	std::vector<RGBA> out = process(src, canvas, pat, pw, ph, outRect, s);

	const int ow = outRect.width(), oh = outRect.height();
	parallelFor(oh, [&](int b, int e) {
		for (int y = b; y < e; ++y) {
			Pixel* row = (Pixel*)((char*)outData + (long)y * outRowbytes);
			const RGBA* o = &out[(size_t)y * ow];
			for (int x = 0; x < ow; ++x) PixelTraits::write(row[x], o[x]);
		}
	});
}

} // namespace kaleido
