// kaleido_test.cpp
// Command-line harness for KaleidoCore, used to check the look without After Effects.
//
//   kaleido_test in.rgba width height out.rgba pattern.rgba|- pw ph [key=value ...]
//
// in.rgba / out.rgba / pattern.rgba are raw 8-bit RGBA files ("-" = built-in motif).

#include "../src/KaleidoCore.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

// Same memory layout as After Effects' PF_Pixel8.
struct Pixel8 { unsigned char alpha, red, green, blue; };

struct Traits8 {
	using Pixel = Pixel8;
	static kaleido::RGBA read(const Pixel& p) {
		return { p.red / 255.0f, p.green / 255.0f, p.blue / 255.0f, p.alpha / 255.0f };
	}
	static unsigned char q(float v) { v = v < 0 ? 0 : (v > 1 ? 1 : v); return (unsigned char)(v * 255.0f + 0.5f); }
	static void write(Pixel& p, const kaleido::RGBA& c) { p.alpha = q(c.a); p.red = q(c.r); p.green = q(c.g); p.blue = q(c.b); }
};

static bool setParam(kaleido::Settings& s, const std::string& k, const std::string& v) {
	float f = (float)std::atof(v.c_str());
	auto col = [&](kaleido::Color& c) { unsigned x = (unsigned)std::strtoul(v.c_str(), nullptr, 16); c = { ((x >> 16) & 255) / 255.0f, ((x >> 8) & 255) / 255.0f, (x & 255) / 255.0f }; };
	if (k == "mask") s.maskMode = (int)f;
	else if (k == "key") col(s.keyColor);
	else if (k == "tol") s.keyTolerance = f;
	else if (k == "ksoft") s.keySoftness = f;
	else if (k == "choke") s.choke = f;
	else if (k == "feather") s.feather = f;
	else if (k == "mineye") s.minEyeSize = f;
	else if (k == "center") s.centerMode = (int)f;
	else if (k == "cx") s.centerX = f;
	else if (k == "cy") s.centerY = f;
	else if (k == "fit") s.shapeFit = f;
	else if (k == "iris") s.irisSize = f;
	else if (k == "pupil") s.pupilSize = f;
	else if (k == "shape") s.shape = (int)f;
	else if (k == "copies") s.copies = (int)f;
	else if (k == "spacing") s.ringSpacing = f;
	else if (k == "size") s.copySize = f;
	else if (k == "crot") s.copyRotation = f;
	else if (k == "mirror") s.mirror = f != 0;
	else if (k == "stagger") s.stagger = f;
	else if (k == "rot") s.rotation = f;
	else if (k == "rspeed") s.rotationSpeed = f;
	else if (k == "sway") s.swayAmount = f;
	else if (k == "sspeed") s.swaySpeed = f;
	else if (k == "slag") s.swayLag = f;
	else if (k == "twist") s.twist = f;
	else if (k == "flow") s.flowSpeed = f;
	else if (k == "wave") s.waveAmount = f;
	else if (k == "wcount") s.waveCount = f;
	else if (k == "wspeed") s.waveSpeed = f;
	else if (k == "fade") s.depthFade = f;
	else if (k == "fadec") col(s.fadeColor);
	else if (k == "blur") s.blurAmount = f;
	else if (k == "focus") s.focusDepth = f;
	else if (k == "frange") s.focusRange = f;
	else if (k == "overlay") s.overlayMode = (int)f;
	else if (k == "oc1") col(s.overlayOuter);
	else if (k == "oc2") col(s.overlayInner);
	else if (k == "oamt") s.overlayAmount = f;
	else if (k == "blend") s.blendMode = (int)f;
	else if (k == "opacity") s.opacity = f;
	else if (k == "comp") s.composite = (int)f;
	else if (k == "scale") s.scale = f;
	else if (k == "time") s.time = f;
	else return false;
	return true;
}

static bool readRaw(const char* path, int w, int h, std::vector<Pixel8>& px) {
	std::vector<unsigned char> raw((size_t)w * h * 4);
	FILE* fi = std::fopen(path, "rb");
	if (!fi) return false;
	const bool ok = std::fread(raw.data(), 1, raw.size(), fi) == raw.size();
	std::fclose(fi);
	if (!ok) return false;
	px.resize((size_t)w * h);
	for (size_t i = 0; i < px.size(); ++i) px[i] = { raw[i * 4 + 3], raw[i * 4], raw[i * 4 + 1], raw[i * 4 + 2] };
	return true;
}

int main(int argc, char** argv) {
	if (argc < 8) {
		std::fprintf(stderr, "usage: %s in.rgba w h out.rgba pattern.rgba|- pw ph [key=value ...]\n", argv[0]);
		return 1;
	}
	const int w = std::atoi(argv[2]), h = std::atoi(argv[3]);
	int pw = std::atoi(argv[6]), ph = std::atoi(argv[7]);
	kaleido::Settings s;
	for (int i = 8; i < argc; ++i) {
		std::string a = argv[i];
		size_t eq = a.find('=');
		if (eq == std::string::npos || !setParam(s, a.substr(0, eq), a.substr(eq + 1))) {
			std::fprintf(stderr, "bad arg %s\n", argv[i]); return 1;
		}
	}

	std::vector<Pixel8> in, pat;
	if (!readRaw(argv[1], w, h, in)) { std::fprintf(stderr, "read failed\n"); return 1; }
	const bool hasPat = std::strcmp(argv[5], "-") != 0;
	if (hasPat && !readRaw(argv[5], pw, ph, pat)) { std::fprintf(stderr, "pattern read failed\n"); return 1; }

	kaleido::Rect rect = { 0, 0, w, h };
	std::vector<Pixel8> out((size_t)w * h);
	kaleido::render<Traits8>(in.data(), (long)w * sizeof(Pixel8), rect,
	                         hasPat ? pat.data() : nullptr, (long)pw * sizeof(Pixel8), hasPat ? pw : 0, hasPat ? ph : 0,
	                         out.data(), (long)w * sizeof(Pixel8), rect, s);

	std::vector<unsigned char> o((size_t)w * h * 4);
	for (size_t i = 0; i < out.size(); ++i) { o[i * 4] = out[i].red; o[i * 4 + 1] = out[i].green; o[i * 4 + 2] = out[i].blue; o[i * 4 + 3] = out[i].alpha; }
	FILE* fo = std::fopen(argv[4], "wb");
	if (!fo) return 1;
	std::fwrite(o.data(), 1, o.size(), fo);
	std::fclose(fo);
	return 0;
}
