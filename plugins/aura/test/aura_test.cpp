// aura_test.cpp
// Command-line harness for AuraCore, used to check the look without After Effects.
//
//   aura_test in.rgba width height out.rgba [pad] [key=value ...]
//
// in.rgba / out.rgba are raw 8-bit RGBA files. The output is (width+2*pad) x
// (height+2*pad), mimicking After Effects growing the layer bounds.
//
// Shape layer: shape=file.rgba sw=W sh=H [stx=X sty=Y ssc=SCALE srot=DEG] takes
// the silhouette from another raw RGBA image placed with that transform.

#include "../src/AuraCore.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Same memory layout as After Effects' PF_Pixel8.
struct Pixel8 { unsigned char alpha, red, green, blue; };

struct Traits8 {
	using Pixel = Pixel8;
	static aura::RGBA read(const Pixel& p) {
		return { p.red / 255.0f, p.green / 255.0f, p.blue / 255.0f, p.alpha / 255.0f };
	}
	static unsigned char q(float v) { v = v < 0 ? 0 : (v > 1 ? 1 : v); return (unsigned char)(v * 255.0f + 0.5f); }
	static void write(Pixel& p, const aura::RGBA& c) { p.alpha = q(c.a); p.red = q(c.r); p.green = q(c.g); p.blue = q(c.b); }
};

struct ShapeArgs { std::string file; int w = 0, h = 0; double tx = 0, ty = 0, sc = 1, rot = 0; };

static bool setParam(aura::Settings& s, ShapeArgs& sh, const std::string& k, const std::string& v) {
	float f = (float)std::atof(v.c_str());
	auto col = [&](aura::Color& c) { unsigned x = (unsigned)std::strtoul(v.c_str(), nullptr, 16); c = { ((x >> 16) & 255) / 255.0f, ((x >> 8) & 255) / 255.0f, (x & 255) / 255.0f }; };
	if (k == "source") s.sourceMode = (int)f;
	else if (k == "threshold") s.threshold = f;
	else if (k == "fill") s.fillInterior = f != 0;
	else if (k == "gaps") s.closeGaps = f;
	else if (k == "smooth") s.smoothness = f;
	else if (k == "style") s.style = (int)f;
	else if (k == "round") s.cornerRounding = f;
	else if (k == "cycle") s.colorCycle = (int)f;
	else if (k == "cdur") s.cycleDuration = f;
	else if (k == "coff") s.cycleOffset = f;
	else if (k == "fillc") col(s.fillColor);
	else if (k == "fillop") s.fillOpacity = f;
	else if (k == "start") s.startOffset = f;
	else if (k == "spacing") s.spacing = f;
	else if (k == "width") s.lineWidth = f;
	else if (k == "soft") s.softness = f;
	else if (k == "outer") s.outerWidth = f;
	else if (k == "fillcomp") s.fillComposition = f != 0;
	else if (k == "length") s.length = f;
	else if (k == "fade") s.fadeOut = f;
	else if (k == "offset") s.ringOffset = f;
	else if (k == "speed") s.speed = f;
	else if (k == "wobble") s.wobbleAmount = f;
	else if (k == "wsize") s.wobbleSize = f;
	else if (k == "wspeed") s.wobbleSpeed = f;
	else if (k == "seed") s.wobbleSeed = f;
	else if (k == "color") s.colorMode = (int)f;
	else if (k.size() == 2 && k[0] == 'c' && k[1] >= '1' && k[1] <= '5') col(s.palette[k[1] - '1']);
	else if (k == "ncol") s.paletteCount = (int)f;
	else if (k == "glen") s.gradientLength = f;
	else if (k == "hue") s.hueStep = f;
	else if (k == "opacity") s.opacity = f;
	else if (k == "comp") s.composite = (int)f;
	else if (k == "scale") s.scale = f;
	else if (k == "time") s.time = f;
	else if (k == "ring") s.ringShape = (int)f;
	else if (k == "cx") s.centerX = f;
	else if (k == "cy") s.centerY = f;
	else if (k == "steps") s.hsbSteps = (int)f;
	else if (k == "hshift") s.hsbHue = f;
	else if (k == "sshift") s.hsbSat = f;
	else if (k == "bshift") s.hsbBri = f;
	else if (k == "hsbloop") s.hsbLoop = (int)f;
	else if (k == "shading") s.bandShading = f;
	else if (k == "shadec") col(s.shadeColor);
	else if (k == "shape") sh.file = v;
	else if (k == "sw") sh.w = (int)f;
	else if (k == "sh") sh.h = (int)f;
	else if (k == "stx") sh.tx = f;
	else if (k == "sty") sh.ty = f;
	else if (k == "ssc") sh.sc = f;
	else if (k == "srot") sh.rot = f;
	else return false;
	return true;
}

int main(int argc, char** argv) {
	if (argc < 5) { std::fprintf(stderr, "usage: %s in.rgba w h out.rgba [pad] [key=value...]\n", argv[0]); return 1; }
	int w = std::atoi(argv[2]), h = std::atoi(argv[3]);
	int pad = argc > 5 ? std::atoi(argv[5]) : 0;
	aura::Settings s;
	ShapeArgs sha;
	for (int i = 6; i < argc; ++i) {
		std::string a = argv[i];
		size_t eq = a.find('=');
		if (eq == std::string::npos || !setParam(s, sha, a.substr(0, eq), a.substr(eq + 1))) {
			std::fprintf(stderr, "bad arg %s\n", argv[i]); return 1;
		}
	}

	std::vector<unsigned char> raw((size_t)w * h * 4);
	FILE* fi = std::fopen(argv[1], "rb");
	if (!fi || std::fread(raw.data(), 1, raw.size(), fi) != raw.size()) { std::fprintf(stderr, "read failed\n"); return 1; }
	std::fclose(fi);
	auto toPixels = [](const std::vector<unsigned char>& r) {
		std::vector<Pixel8> p(r.size() / 4);
		for (size_t i = 0; i < p.size(); ++i) p[i] = { r[i * 4 + 3], r[i * 4], r[i * 4 + 1], r[i * 4 + 2] };
		return p;
	};
	std::vector<Pixel8> in = toPixels(raw);

	aura::ShapeSource shape;
	std::vector<Pixel8> shapePx;
	if (!sha.file.empty()) {
		std::vector<unsigned char> sr((size_t)sha.w * sha.h * 4);
		FILE* fs = std::fopen(sha.file.c_str(), "rb");
		if (!fs || std::fread(sr.data(), 1, sr.size(), fs) != sr.size()) { std::fprintf(stderr, "shape read failed\n"); return 1; }
		std::fclose(fs);
		shapePx = toPixels(sr);
		shape.data = shapePx.data();
		shape.rowbytes = (long)sha.w * sizeof(Pixel8);
		shape.rect = { 0, 0, sha.w, sha.h };
		// Scale and rotate about the shape's centre, then translate.
		const double r = sha.rot * 3.14159265358979 / 180.0, c = std::cos(r) * sha.sc, sn = std::sin(r) * sha.sc;
		const double hx = sha.w * 0.5, hy = sha.h * 0.5;
		shape.toLayer = { c, sn, -sn, c, hx + sha.tx - (c * hx - sn * hy), hy + sha.ty - (sn * hx + c * hy) };
	}

	aura::Rect inRect = { 0, 0, w, h };
	aura::Rect outRect = { -pad, -pad, w + pad, h + pad };
	int ow = outRect.width(), oh = outRect.height();
	std::vector<Pixel8> out((size_t)ow * oh);
	aura::render<Traits8>(in.data(), (long)w * sizeof(Pixel8), inRect, out.data(), (long)ow * sizeof(Pixel8), outRect, s, sha.file.empty() ? nullptr : &shape);

	std::vector<unsigned char> o((size_t)ow * oh * 4);
	for (size_t i = 0; i < out.size(); ++i) { o[i * 4] = out[i].red; o[i * 4 + 1] = out[i].green; o[i * 4 + 2] = out[i].blue; o[i * 4 + 3] = out[i].alpha; }
	FILE* fo = std::fopen(argv[4], "wb");
	std::fwrite(o.data(), 1, o.size(), fo);
	std::fclose(fo);
	std::printf("%d %d\n", ow, oh);
	return 0;
}
