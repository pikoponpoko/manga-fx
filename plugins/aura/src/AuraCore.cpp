// AuraCore.cpp
// Distance field, noise and ring shading for the Aura Contour effect.

#include "AuraCore.h"

#include <cstring>
#include <limits>

namespace aura {

namespace {

const float kFar = 1e20f;

inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
inline float smooth01(float v) { v = clamp01(v); return v * v * (3 - 2 * v); }

// 1D squared Euclidean distance transform (Felzenszwalb & Huttenlocher).
// Computed in double: q*q loses precision in float for large frames.
void edt1d(const float* f, int n, float* d, int* v, double* z, int* arg = nullptr) {
	const double inf = std::numeric_limits<double>::infinity();
	int k = 0;
	v[0] = 0; z[0] = -inf; z[1] = inf;
	for (int q = 1; q < n; ++q) {
		const double fq = (double)f[q] + (double)q * q;
		double s;
		for (;;) {
			const int p = v[k];
			s = (fq - ((double)f[p] + (double)p * p)) / (2.0 * (q - p));
			if (s <= z[k]) { --k; continue; }
			break;
		}
		++k; v[k] = q; z[k] = s; z[k + 1] = inf;
	}
	k = 0;
	for (int q = 0; q < n; ++q) {
		while (z[k + 1] < q) ++k;
		const double dq = (double)(q - v[k]);
		d[q] = (float)(dq * dq + f[v[k]]);
		if (arg) arg[q] = v[k];
	}
}

// Squared distance from every pixel to the nearest pixel where feature != 0.
std::vector<float> edt2d(const std::vector<uint8_t>& feature, int w, int h) {
	std::vector<float> out((size_t)w * h);
	// Columns
	parallelFor(w, [&](int b, int e) {
		std::vector<float> f(h), d(h);
		std::vector<double> z(h + 1);
		std::vector<int> v(h);
		for (int x = b; x < e; ++x) {
			for (int y = 0; y < h; ++y) f[y] = feature[(size_t)y * w + x] ? 0.0f : kFar;
			edt1d(f.data(), h, d.data(), v.data(), z.data());
			for (int y = 0; y < h; ++y) out[(size_t)y * w + x] = d[y];
		}
	});
	// Rows
	parallelFor(h, [&](int b, int e) {
		std::vector<float> f(w), d(w);
		std::vector<double> z(w + 1);
		std::vector<int> v(w);
		for (int y = b; y < e; ++y) {
			float* row = &out[(size_t)y * w];
			std::memcpy(f.data(), row, sizeof(float) * w);
			edt1d(f.data(), w, d.data(), v.data(), z.data());
			std::memcpy(row, d.data(), sizeof(float) * w);
		}
	});
	return out;
}

// Index (y * w + x) of the nearest pixel where feature != 0, or -1 if none.
std::vector<int> nearestFeature(const std::vector<uint8_t>& feature, int w, int h) {
	std::vector<float> colDist((size_t)w * h);
	std::vector<int> colRow((size_t)w * h);
	parallelFor(w, [&](int b, int e) {
		std::vector<float> f(h), d(h);
		std::vector<double> z(h + 1);
		std::vector<int> v(h), arg(h);
		for (int x = b; x < e; ++x) {
			for (int y = 0; y < h; ++y) f[y] = feature[(size_t)y * w + x] ? 0.0f : kFar;
			edt1d(f.data(), h, d.data(), v.data(), z.data(), arg.data());
			for (int y = 0; y < h; ++y) {
				colDist[(size_t)y * w + x] = d[y];
				colRow[(size_t)y * w + x] = d[y] >= kFar * 0.5f ? -1 : arg[y];
			}
		}
	});
	std::vector<int> out((size_t)w * h, -1);
	parallelFor(h, [&](int b, int e) {
		std::vector<float> d(w);
		std::vector<double> z(w + 1);
		std::vector<int> v(w), arg(w);
		for (int y = b; y < e; ++y) {
			const float* f = &colDist[(size_t)y * w];
			edt1d(f, w, d.data(), v.data(), z.data(), arg.data());
			for (int x = 0; x < w; ++x) {
				if (d[x] >= kFar * 0.5f) continue;
				int fx = arg[x], fy = colRow[(size_t)y * w + fx];
				if (fy >= 0) out[(size_t)y * w + x] = fy * w + fx;
			}
		}
	});
	return out;
}

// One horizontal + vertical box blur pass of radius r.
void boxBlur(std::vector<float>& img, int w, int h, int r) {
	if (r <= 0) return;
	std::vector<float> tmp((size_t)w * h);
	const float inv = 1.0f / (2 * r + 1);
	parallelFor(h, [&](int b, int e) {
		for (int y = b; y < e; ++y) {
			const float* src = &img[(size_t)y * w];
			float* dst = &tmp[(size_t)y * w];
			float acc = 0;
			for (int i = -r; i <= r; ++i) acc += src[std::min(w - 1, std::max(0, i))];
			for (int x = 0; x < w; ++x) {
				dst[x] = acc * inv;
				acc += src[std::min(w - 1, x + r + 1)] - src[std::max(0, x - r)];
			}
		}
	});
	parallelFor(w, [&](int b, int e) {
		std::vector<float> col(h);
		for (int x = b; x < e; ++x) {
			for (int y = 0; y < h; ++y) col[y] = tmp[(size_t)y * w + x];
			float acc = 0;
			for (int i = -r; i <= r; ++i) acc += col[std::min(h - 1, std::max(0, i))];
			for (int y = 0; y < h; ++y) {
				img[(size_t)y * w + x] = acc * inv;
				acc += col[std::min(h - 1, y + r + 1)] - col[std::max(0, y - r)];
			}
		}
	});
}

// Approximate Gaussian blur with three box passes.
void gaussianBlur(std::vector<float>& img, int w, int h, float sigma) {
	if (sigma < 0.3f) return;
	float wIdeal = std::sqrt(12.0f * sigma * sigma / 3.0f + 1.0f);
	int r = std::max(1, (int)std::lround((wIdeal - 1.0f) * 0.5f));
	for (int i = 0; i < 3; ++i) boxBlur(img, w, h, r);
}

// Marks every pixel reachable from the canvas border through non-blocked pixels.
std::vector<uint8_t> floodExterior(const std::vector<uint8_t>& blocked, int w, int h) {
	std::vector<uint8_t> ext((size_t)w * h, 0);
	std::vector<int> stack;
	stack.reserve((size_t)(w + h) * 4);
	auto seed = [&](int x, int y) {
		size_t i = (size_t)y * w + x;
		if (!blocked[i] && !ext[i]) { ext[i] = 1; stack.push_back((int)i); }
	};
	for (int x = 0; x < w; ++x) { seed(x, 0); seed(x, h - 1); }
	for (int y = 0; y < h; ++y) { seed(0, y); seed(w - 1, y); }
	while (!stack.empty()) {
		int i = stack.back(); stack.pop_back();
		int x = i % w, y = i / w;
		if (x > 0)     seed(x - 1, y);
		if (x < w - 1) seed(x + 1, y);
		if (y > 0)     seed(x, y - 1);
		if (y < h - 1) seed(x, y + 1);
	}
	return ext;
}

// Integer hash -> gradient for the noise lattice.
inline uint32_t hash3(int x, int y, int z) {
	uint32_t h = (uint32_t)x * 0x8da6b343u ^ (uint32_t)y * 0xd8163841u ^ (uint32_t)z * 0xcb1ab31fu;
	h ^= h >> 15; h *= 0x2c1b3c6du; h ^= h >> 12; h *= 0x297a2d39u; h ^= h >> 15;
	return h;
}

inline float grad(uint32_t h, float x, float y, float z) {
	switch (h & 15) {
	case 0:  return  x + y; case 1:  return -x + y; case 2:  return  x - y; case 3:  return -x - y;
	case 4:  return  x + z; case 5:  return -x + z; case 6:  return  x - z; case 7:  return -x - z;
	case 8:  return  y + z; case 9:  return -y + z; case 10: return  y - z; case 11: return -y - z;
	case 12: return  x + y; case 13: return -x + y; case 14: return -y + z; default: return -y - z;
	}
}

inline float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

void toHsv(Color c, float& hh, float& ss, float& vv) {
	float mx = std::max(c.r, std::max(c.g, c.b)), mn = std::min(c.r, std::min(c.g, c.b));
	float d = mx - mn;
	hh = 0; ss = mx > 0 ? d / mx : 0; vv = mx;
	if (d > 1e-6f) {
		if (mx == c.r)      hh = std::fmod((c.g - c.b) / d, 6.0f);
		else if (mx == c.g) hh = (c.b - c.r) / d + 2.0f;
		else                hh = (c.r - c.g) / d + 4.0f;
		hh *= 60.0f;
	}
}

Color fromHsv(float hh, float ss, float vv) {
	hh = std::fmod(hh, 360.0f);
	if (hh < 0) hh += 360.0f;
	float C = vv * ss, X = C * (1 - std::fabs(std::fmod(hh / 60.0f, 2.0f) - 1)), m = vv - C;
	float r, g, b;
	if (hh < 60)       { r = C; g = X; b = 0; }
	else if (hh < 120) { r = X; g = C; b = 0; }
	else if (hh < 180) { r = 0; g = C; b = X; }
	else if (hh < 240) { r = 0; g = X; b = C; }
	else if (hh < 300) { r = X; g = 0; b = C; }
	else               { r = C; g = 0; b = X; }
	return { r + m, g + m, b + m };
}

Color hsvShift(Color c, float hueDegrees, bool forceColor = true) {
	float hh, ss, vv;
	toHsv(c, hh, ss, vv);
	if (forceColor) {
		if (ss < 0.05f) { ss = 1.0f; } // grey base colour: still produce a rainbow
		if (vv < 0.05f) { vv = 1.0f; }
	}
	return fromHsv(hh + hueDegrees, ss, vv);
}

} // namespace

float noise3(float x, float y, float z) {
	int xi = (int)std::floor(x), yi = (int)std::floor(y), zi = (int)std::floor(z);
	float xf = x - xi, yf = y - yi, zf = z - zi;
	float u = fade(xf), v = fade(yf), w = fade(zf);
	float n000 = grad(hash3(xi, yi, zi), xf, yf, zf);
	float n100 = grad(hash3(xi + 1, yi, zi), xf - 1, yf, zf);
	float n010 = grad(hash3(xi, yi + 1, zi), xf, yf - 1, zf);
	float n110 = grad(hash3(xi + 1, yi + 1, zi), xf - 1, yf - 1, zf);
	float n001 = grad(hash3(xi, yi, zi + 1), xf, yf, zf - 1);
	float n101 = grad(hash3(xi + 1, yi, zi + 1), xf - 1, yf, zf - 1);
	float n011 = grad(hash3(xi, yi + 1, zi + 1), xf, yf - 1, zf - 1);
	float n111 = grad(hash3(xi + 1, yi + 1, zi + 1), xf - 1, yf - 1, zf - 1);
	float nx00 = lerp(n000, n100, u), nx10 = lerp(n010, n110, u);
	float nx01 = lerp(n001, n101, u), nx11 = lerp(n011, n111, u);
	return lerp(lerp(nx00, nx10, v), lerp(nx01, nx11, v), w);
}

std::vector<float> buildDistanceField(std::vector<float>& mask, int w, int h, const Settings& s) {
	const float sc = s.scale > 0 ? s.scale : 1.0f;

	// 1. Round off the contour.
	gaussianBlur(mask, w, h, s.smoothness * sc * 0.5f);

	// 2. Binary silhouette.
	const float thr = std::max(1e-4f, s.threshold);
	std::vector<uint8_t> shape((size_t)w * h);
	for (size_t i = 0; i < shape.size(); ++i) shape[i] = mask[i] >= thr ? 1 : 0;

	// 3. Close small gaps and/or fill the inside of the character, so the
	//    rings follow the outer silhouette of line art instead of every stroke.
	const float r = s.closeGaps * sc;
	const float r2 = r * r;
	if (s.fillInterior || r > 0.5f) {
		std::vector<uint8_t> dilated = shape;
		if (r > 0.5f) {
			std::vector<float> d2 = edt2d(shape, w, h);
			for (size_t i = 0; i < d2.size(); ++i) dilated[i] = d2[i] <= r2 ? 1 : 0;
		}
		std::vector<uint8_t> outside;
		if (s.fillInterior) {
			outside = floodExterior(dilated, w, h);
		} else {
			outside.resize(dilated.size());
			for (size_t i = 0; i < dilated.size(); ++i) outside[i] = dilated[i] ? 0 : 1;
		}
		if (r > 0.5f) {
			// Erode back by the same radius (morphological closing).
			std::vector<float> d2 = edt2d(outside, w, h);
			for (size_t i = 0; i < d2.size(); ++i) shape[i] = (shape[i] || d2[i] > r2) ? 1 : 0;
		} else {
			for (size_t i = 0; i < shape.size(); ++i) shape[i] = (shape[i] || !outside[i]) ? 1 : 0;
		}
	}

	// 4. Distance from each pixel to the silhouette edge, measured to a
	//    sub-pixel edge point so the rings come out smooth instead of
	//    inheriting the pixel staircase of the mask.
	//    Edge point of a boundary pixel q: from q's centre, step outward along
	//    the mask gradient to where the (smoothed) mask crosses the threshold.
	std::vector<float> ex((size_t)w * h, 0.0f), ey((size_t)w * h, 0.0f);
	parallelFor(h, [&](int b, int e) {
		for (int y = b; y < e; ++y)
			for (int x = 0; x < w; ++x) {
				size_t i = (size_t)y * w + x;
				if (!shape[i]) continue;
				auto sh = [&](int xx, int yy) { return (xx < 0 || yy < 0 || xx >= w || yy >= h) ? 0 : (int)shape[(size_t)yy * w + xx]; };
				if (sh(x - 1, y) && sh(x + 1, y) && sh(x, y - 1) && sh(x, y + 1)) continue; // not on the edge
				auto mk = [&](int xx, int yy) {
					xx = std::min(w - 1, std::max(0, xx)); yy = std::min(h - 1, std::max(0, yy));
					return mask[(size_t)yy * w + xx];
				};
				float gx = 0.5f * (mk(x + 1, y) - mk(x - 1, y)), gy = 0.5f * (mk(x, y + 1) - mk(x, y - 1));
				float gl = std::sqrt(gx * gx + gy * gy);
				float nx, ny, step;
				if (gl > 0.02f && mask[i] >= thr) {
					nx = -gx / gl; ny = -gy / gl;
					step = std::min(1.0f, (mask[i] - thr) / gl);
				} else {
					// Pixel added by gap closing / interior fill: use the binary edge.
					float bx = (float)(sh(x - 1, y) - sh(x + 1, y)), by = (float)(sh(x, y - 1) - sh(x, y + 1));
					float bl = std::sqrt(bx * bx + by * by);
					if (bl > 0) { nx = bx / bl; ny = by / bl; } else { nx = ny = 0; }
					step = 0.5f;
				}
				ex[i] = nx * step; ey[i] = ny * step;
			}
	});
	std::vector<int> nearest = nearestFeature(shape, w, h);
	std::vector<float> dist((size_t)w * h);
	parallelFor(h, [&](int b, int e) {
		for (int y = b; y < e; ++y)
			for (int x = 0; x < w; ++x) {
				size_t i = (size_t)y * w + x;
				if (shape[i]) { dist[i] = 0.0f; continue; }
				int q = nearest[i];
				if (q < 0) { dist[i] = kFar; continue; }
				float dx = (float)x - ((float)(q % w) + ex[q]);
				float dy = (float)y - ((float)(q / w) + ey[q]);
				dist[i] = std::max(0.01f, std::sqrt(dx * dx + dy * dy));
			}
	});

	// 5. Corner rounding: blur a signed distance field so the sharp creases
	//    that form in the outer rings (e.g. between the arms and the body)
	//    become round. Rings close to the character keep the exact contour.
	const float sigma = s.cornerRounding * sc;
	if (sigma >= 0.5f) {
		// Work on a padded canvas so the blur sees the true distances beyond
		// the edges instead of clamped ones (which would bend the rings there).
		const int P = (int)std::ceil(3.0f * sigma) + 4;
		const int pw = w + 2 * P, ph = h + 2 * P;
		std::vector<uint8_t> pshape((size_t)pw * ph, 0), pinv((size_t)pw * ph, 1);
		for (int y = 0; y < h; ++y)
			for (int x = 0; x < w; ++x) {
				size_t pi = (size_t)(y + P) * pw + (x + P);
				pshape[pi] = shape[(size_t)y * w + x];
				pinv[pi] = !pshape[pi];
			}
		std::vector<float> outside = edt2d(pshape, pw, ph);
		std::vector<float> inside = edt2d(pinv, pw, ph);
		const float lim = (float)(pw + ph); // keeps empty / full canvases finite for the blur
		std::vector<float> sdf((size_t)pw * ph);
		for (int py = 0; py < ph; ++py)
			for (int px = 0; px < pw; ++px) {
				size_t i = (size_t)py * pw + px;
				int x = px - P, y = py - P;
				float v;
				if (pshape[i]) v = 0.5f - std::sqrt(inside[i]);
				else if (x >= 0 && y >= 0 && x < w && y < h) v = dist[(size_t)y * w + x]; // sub-pixel accurate
				else v = std::sqrt(outside[i]);
				sdf[i] = std::max(-lim, std::min(v, lim));
			}
		gaussianBlur(sdf, pw, ph, sigma);
		parallelFor(h, [&](int b, int e) {
			for (int y = b; y < e; ++y)
				for (int x = 0; x < w; ++x) {
					size_t i = (size_t)y * w + x;
					float d = dist[i];
					if (d <= 0 || d >= kFar * 0.5f) continue;
					float m = smooth01(d / (2.0f * sigma));
					// Only ever pull rings inward (rounds the creases); never push
					// them out of narrow gaps or away from the character.
					float bl = std::min(d, std::max(0.0f, sdf[(size_t)(y + P) * pw + (x + P)]));
					dist[i] = std::max(0.01f, d + (bl - d) * m);
				}
		});
	}
	return dist;
}

RGBA shade(float dist, float gx, float gy, float fx, float fy, const Settings& s) {
	if (dist <= 0 || dist >= kFar * 0.5f) return { 0, 0, 0, 0 };
	const float sc = s.scale > 0 ? s.scale : 1.0f;
	float d = dist / sc; // full-res pixels

	// Gradient of the field. The exact distance field always has slope 1, so
	// without corner rounding only its direction is used.
	if (s.cornerRounding <= 0) {
		float gl = std::sqrt(gx * gx + gy * gy);
		if (gl > 1e-6f) { gx /= gl; gy /= gl; } else { gx = 1; gy = 0; }
	}

	if (s.wobbleAmount > 0) {
		float size = std::max(1.0f, s.wobbleSize);
		float z = (float)(s.time * s.wobbleSpeed) + s.wobbleSeed * 13.37f;
		auto wob = [&](float x, float y) {
			return noise3(x / size, y / size, z) + 0.5f * noise3(x / size * 2.03f + 31.7f, y / size * 2.03f + 17.1f, z * 1.7f + 5.3f);
		};
		float n = wob(fx, fy);
		d += s.wobbleAmount * n;
		gx += s.wobbleAmount * (wob(fx + 1.0f, fy) - n);
		gy += s.wobbleAmount * (wob(fx, fy + 1.0f) - n);
	}
	// Slope of the (rounded, wobbled) field: distances along it are divided by
	// this so lines keep the same on-screen width everywhere.
	const float slope = std::min(5.0f, std::max(0.2f, std::sqrt(gx * gx + gy * gy)));

	float t = d - s.startOffset;
	// Filled styles keep their inner edge on the contour; wobble only moves the outside.
	const float tRaw = dist / sc - s.startOffset;
	if (s.style != STYLE_LINES && tRaw > 0) t = std::max(t, std::min(tRaw, 1e-3f));
	const float length = s.fillComposition ? 1e9f : std::max(1.0f, s.length);
	if (t <= 0 || t > length) return { 0, 0, 0, 0 };

	float spacing = std::max(0.5f, s.spacing);
	float phase = s.ringOffset + (float)(s.speed * s.time);
	// Offset by half a line so that, at rest, the first ring sits fully
	// outside the start offset; moving rings are born at the contour.
	// Fill bands start right at the contour, so the first band is never a sliver.
	float u = t - phase - (s.style == STYLE_FILL ? 0.0f : 0.5f * s.lineWidth);
	float k = std::floor(u / spacing + 0.5f);   // ring index (moves with the rings)
	float dd = std::fabs(u - k * spacing) / slope; // on-screen distance to the ring's centre line

	// Outer Width taper runs over Aura Length, or over Gradient Length when
	// the aura fills the composition.
	float norm = clamp01(t / (s.fillComposition ? std::max(1.0f, s.gradientLength) : length));
	float aa = std::max(s.softness, 1.0f / sc);
	float fade = clamp01((s.style == STYLE_LINES ? t : tRaw) / aa);  // soft at the contour
	if (!s.fillComposition) {
		if (s.fadeOut > 0) fade *= smooth01((length - t) / s.fadeOut);
		else fade *= smooth01((length - t) / aa);
	}
	if (fade <= 0) return { 0, 0, 0, 0 };

	// Colour cycle: loops exactly every cycleDuration seconds.
	const int N = std::max(1, std::min(kMaxPalette, s.paletteCount));
	float hueCycle = 0.0f, shift = 0.0f;
	if (s.colorCycle != CYCLE_OFF && s.cycleDuration > 0) {
		double ph = s.time / s.cycleDuration + s.cycleOffset;
		float cyc = (float)(ph - std::floor(ph));
		if (s.colorCycle == CYCLE_HUE || s.colorMode == COLOR_RAINBOW) hueCycle = 360.0f * cyc;
		else shift = cyc * N;   // one step per colour, back to the start after N steps
	}
	// HSB Steps: one sweep is hsbSteps rings (there and back with Ping-Pong;
	// with Hold every ring after the sweep keeps the last colour).
	const int steps = std::max(2, s.hsbSteps);
	const int period = s.hsbLoop == HSB_PINGPONG ? 2 * (steps - 1) : steps;
	if (s.colorMode == COLOR_HSB_STEPS) shift *= (float)period / N;
	float baseH, baseS, baseV;
	toHsv(s.palette[0], baseH, baseS, baseV);
	auto hsbStep = [&](int i) -> Color {
		int p;
		if (s.hsbLoop == HSB_CONTINUE) {
			float f = (float)i / (float)(steps - 1);
			return fromHsv(baseH + f * s.hsbHue, clamp01(baseS + f * s.hsbSat), clamp01(baseV + f * s.hsbBri));
		}
		if (s.hsbLoop == HSB_HOLD) p = std::min(steps - 1, std::max(0, i));
		else {
			p = ((i % period) + period) % period;
			if (s.hsbLoop == HSB_PINGPONG && p > steps - 1) p = period - p;
		}
		float f = (float)p / (float)(steps - 1);
		return fromHsv(baseH + f * s.hsbHue, clamp01(baseS + f * s.hsbSat), clamp01(baseV + f * s.hsbBri));
	};
	// Discrete colour sequences (Stripes / HSB Steps); fractional positions blend neighbours.
	const bool stepped = s.colorMode == COLOR_STRIPES || s.colorMode == COLOR_HSB_STEPS;
	auto cycled = [&](Color c) { return hueCycle != 0.0f ? hsvShift(c, hueCycle, false) : c; };
	// Palette lookup with wrap-around; fractional positions blend neighbours.
	auto paletteAt = [&](float x) -> Color {
		float fl = std::floor(x);
		int i = (int)(((long)fl % N + N) % N), j = (i + 1) % N;
		float f = x - fl;
		Color a, b;
		if (s.colorMode == COLOR_HSB_STEPS) { a = hsbStep((int)fl); b = hsbStep((int)fl + 1); }
		else { a = s.palette[i]; b = s.palette[j]; }
		return { lerp(a.r, b.r, f), lerp(a.g, b.g, f), lerp(a.b, b.b, f) };
	};
	// Colour of ring / band number `ring`.
	auto colorFor = [&](float ring) -> Color {
		switch (s.colorMode) {
		case COLOR_GRADIENT: {
			float g = clamp01(t / std::max(1.0f, s.gradientLength));
			if (N == 1) return cycled(s.palette[0]);
			float x = g * (N - 1) - shift;
			if (shift == 0.0f) { // plain gradient Color 1 -> Color N
				int i = std::min(N - 2, (int)std::floor(x));
				float f = x - i;
				const Color& a = s.palette[i]; const Color& b = s.palette[i + 1];
				return cycled({ lerp(a.r, b.r, f), lerp(a.g, b.g, f), lerp(a.b, b.b, f) });
			}
			return cycled(paletteAt(x));
		}
		case COLOR_STRIPES:
		case COLOR_HSB_STEPS:
			return cycled(paletteAt(ring - shift));
		case COLOR_RAINBOW:
			return hsvShift(s.palette[0], ring * s.hueStep + hueCycle);
		default:
			return cycled(paletteAt(-shift));
		}
	};

	RGBA out = { 0, 0, 0, 0 };

	// Filled aura: the whole area out to Aura Length. In "Fill" style the
	// colour mode applies per band (band edges sit on the ring lines), which
	// gives thick stripes with Stripes / Rainbow. With Palette Loop the bands
	// themselves travel outward, like a barber pole in rings.
	if (s.style == STYLE_FILL || s.style == STYLE_FILL_LINES) {
		Color fc = cycled(s.fillColor);
		float ub = u / spacing;
		if (s.style == STYLE_FILL && stepped) ub -= shift;
		float e = std::floor(ub + 0.5f);               // nearest band edge
		float m = smooth01((ub - e) * spacing / slope / aa + 0.5f);
		// An edge at (or inside) the contour has no band below it: don't blend
		// in that band's colour, or it shows up as a thin line around the character.
		if (t - (ub - e) * spacing <= 1e-3f) m = 1.0f;
		Color a = fc, b = fc;
		if (s.style == STYLE_FILL) {
			if (stepped) { a = cycled(paletteAt(e - 1)); b = cycled(paletteAt(e)); }
			else { a = colorFor(e - 1); b = colorFor(e); }
		}
		// Band Shading: each band runs from Shading Color at its inner edge to
		// its own colour at the outer edge; the step between bands stays sharp.
		if (s.bandShading > 0) {
			const float bs = clamp01(s.bandShading);
			const Color sc = cycled(s.shadeColor);
			auto shadeBand = [&](Color c, float f) -> Color {
				f = clamp01(f);
				float k = bs * (1.0f - f);
				return { lerp(c.r, sc.r, k), lerp(c.g, sc.g, k), lerp(c.b, sc.b, k) };
			};
			a = shadeBand(a, ub - (e - 1));
			b = shadeBand(b, ub - e);
		}
		fc = { lerp(a.r, b.r, m), lerp(a.g, b.g, m), lerp(a.b, b.b, m) };
		float fa = fade * clamp01(s.fillOpacity) * clamp01(s.opacity);
		out = { fc.r * fa, fc.g * fa, fc.b * fa, fa };
		if (s.style == STYLE_FILL) return out;
	}

	// Ring lines.
	float halfW = 0.5f * s.lineWidth * lerp(1.0f, s.outerWidth, norm);
	float cov = smooth01((halfW - dd) / aa + 0.5f) * fade;
	if (cov <= 0) return out;
	Color c = colorFor(k);
	float a = cov * clamp01(s.opacity);
	return { c.r * a + out.r * (1 - a), c.g * a + out.g * (1 - a), c.b * a + out.b * (1 - a), a + out.a * (1 - a) };
}

} // namespace aura
