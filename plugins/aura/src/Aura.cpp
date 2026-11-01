// Aura.cpp
// After Effects SmartFX entry point for "Aura Contour".
// Supports 8 / 16 / 32 bpc and Multi-Frame Rendering. The image work itself
// lives in AuraCore.{h,cpp}.

#include "Aura.h"
#include "AuraCore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <new>

namespace {

// ---------------------------------------------------------------------------
// Pixel access. After Effects hands effects straight (un-premultiplied) pixels.

struct Traits8 {
	using Pixel = PF_Pixel8;
	static aura::RGBA read(const Pixel& p) {
		const float k = 1.0f / PF_MAX_CHAN8;
		return { p.red * k, p.green * k, p.blue * k, p.alpha * k };
	}
	static A_u_char q(float v) {
		v = v < 0 ? 0 : (v > 1 ? 1 : v);
		return (A_u_char)(v * PF_MAX_CHAN8 + 0.5f);
	}
	static void write(Pixel& p, const aura::RGBA& c) {
		p.alpha = q(c.a); p.red = q(c.r); p.green = q(c.g); p.blue = q(c.b);
	}
};

struct Traits16 {
	using Pixel = PF_Pixel16;
	static aura::RGBA read(const Pixel& p) {
		const float k = 1.0f / PF_MAX_CHAN16;
		return { p.red * k, p.green * k, p.blue * k, p.alpha * k };
	}
	static A_u_short q(float v) {
		v = v < 0 ? 0 : (v > 1 ? 1 : v);
		return (A_u_short)(v * PF_MAX_CHAN16 + 0.5f);
	}
	static void write(Pixel& p, const aura::RGBA& c) {
		p.alpha = q(c.a); p.red = q(c.r); p.green = q(c.g); p.blue = q(c.b);
	}
};

struct Traits32 {
	using Pixel = PF_PixelFloat;
	static aura::RGBA read(const Pixel& p) { return { p.red, p.green, p.blue, p.alpha }; }
	static void write(Pixel& p, const aura::RGBA& c) {
		p.alpha = std::min(1.0f, std::max(0.0f, c.a)); p.red = c.r; p.green = c.g; p.blue = c.b;
	}
};

// Data handed from PreRender to SmartRender.
struct PreRenderData {
	PF_LRect inRect;
	PF_LRect outRect;
	bool haveShape = false;      // the Shape Layer was checked out
	PF_LRect shapeRect;
	aura::Affine shapeToLayer;   // its buffer coordinates -> ours
};

void DeletePreRenderData(void* p) { delete static_cast<PreRenderData*>(p); }

PF_LRect Expand(PF_LRect r, A_long by) {
	r.left -= by; r.top -= by; r.right += by; r.bottom += by;
	return r;
}

PF_LRect Intersect(const PF_LRect& a, const PF_LRect& b) {
	PF_LRect r;
	r.left = std::max(a.left, b.left);   r.top = std::max(a.top, b.top);
	r.right = std::min(a.right, b.right); r.bottom = std::min(a.bottom, b.bottom);
	if (r.right <= r.left || r.bottom <= r.top) { r.left = r.top = r.right = r.bottom = 0; }
	return r;
}

// ---------------------------------------------------------------------------
// Parameters

// Reads every parameter at the current time (SmartFX must check them out).
PF_Err ReadSettings(PF_InData* in_data, aura::Settings& s, bool& growBounds) {
	PF_Err err = PF_Err_NONE;
	PF_ParamDef p[AURA_NUM_PARAMS];
	bool out[AURA_NUM_PARAMS] = {};
	auto isTopic = [](int i) {
		return i == AURA_SOURCE_TOPIC || i == AURA_SOURCE_TOPIC_END || i == AURA_RINGS_TOPIC ||
		       i == AURA_RINGS_TOPIC_END || i == AURA_ANIM_TOPIC || i == AURA_ANIM_TOPIC_END ||
		       i == AURA_COLOR_TOPIC || i == AURA_COLOR_TOPIC_END;
	};
	for (int i = 1; i < AURA_NUM_PARAMS && !err; ++i) {
		AEFX_CLR_STRUCT(p[i]);
		if (isTopic(i) || i == AURA_SHAPE_LAYER) continue;   // the layer is checked out as pixels
		err = in_data->inter.checkout_param(in_data->effect_ref, i, in_data->current_time,
		                                    in_data->time_step, in_data->time_scale, &p[i]);
		out[i] = !err;
	}
	if (!err) {
		auto fs = [&](int i) { return (float)p[i].u.fs_d.value; };
		auto col = [&](int i) {
			const PF_Pixel& c = p[i].u.cd.value;
			return aura::Color{ c.red / 255.0f, c.green / 255.0f, c.blue / 255.0f };
		};
		s.sourceMode     = (int)p[AURA_SOURCE_MODE].u.pd.value - 1;
		s.threshold      = fs(AURA_THRESHOLD) / 100.0f;
		s.fillInterior   = p[AURA_FILL_INTERIOR].u.bd.value != 0;
		s.closeGaps      = fs(AURA_CLOSE_GAPS);
		s.smoothness     = fs(AURA_SMOOTHNESS);
		s.ringShape      = (int)p[AURA_RING_SHAPE].u.pd.value - 1;
		s.style          = (int)p[AURA_STYLE].u.pd.value - 1;
		s.startOffset    = fs(AURA_START_OFFSET);
		s.spacing        = fs(AURA_SPACING);
		s.lineWidth      = fs(AURA_LINE_WIDTH);
		s.softness       = fs(AURA_SOFTNESS);
		s.outerWidth     = fs(AURA_OUTER_WIDTH) / 100.0f;
		s.length         = fs(AURA_LENGTH);
		s.fadeOut        = fs(AURA_FADE_OUT);
		s.fillComposition = p[AURA_FILL_COMPOSITION].u.bd.value != 0;
		s.cornerRounding = fs(AURA_CORNER_ROUNDING);
		s.ringOffset     = fs(AURA_RING_OFFSET);
		s.speed          = fs(AURA_SPEED);
		s.wobbleAmount   = fs(AURA_WOBBLE_AMOUNT);
		s.wobbleSize     = fs(AURA_WOBBLE_SIZE);
		s.wobbleSpeed    = fs(AURA_WOBBLE_SPEED);
		s.wobbleSeed     = fs(AURA_WOBBLE_SEED);
		s.colorMode      = (int)p[AURA_COLOR_MODE].u.pd.value - 1;
		s.paletteCount   = (int)p[AURA_NUM_COLORS].u.pd.value;   // popup "1|2|3|4|5"
		s.palette[0]     = col(AURA_COLOR1);
		s.palette[1]     = col(AURA_COLOR2);
		s.palette[2]     = col(AURA_COLOR3);
		s.palette[3]     = col(AURA_COLOR4);
		s.palette[4]     = col(AURA_COLOR5);
		s.gradientLength = fs(AURA_GRADIENT_LENGTH);
		s.hueStep        = fs(AURA_HUE_STEP);
		s.hsbSteps       = (int)std::lround(fs(AURA_HSB_STEPS));
		s.hsbHue         = fs(AURA_HSB_HUE);
		s.hsbSat         = fs(AURA_HSB_SAT) / 100.0f;
		s.hsbBri         = fs(AURA_HSB_BRI) / 100.0f;
		s.hsbLoop        = (int)p[AURA_HSB_LOOP].u.pd.value - 1;
		s.bandShading    = fs(AURA_BAND_SHADING) / 100.0f;
		s.shadeColor     = col(AURA_SHADE_COLOR);
		s.fillColor      = col(AURA_FILL_COLOR);
		s.fillOpacity    = fs(AURA_FILL_OPACITY) / 100.0f;
		s.colorCycle     = (int)p[AURA_COLOR_CYCLE].u.pd.value - 1;
		s.cycleDuration  = fs(AURA_CYCLE_DURATION);
		s.cycleOffset    = fs(AURA_CYCLE_OFFSET) / 100.0f;
		s.opacity        = fs(AURA_OPACITY) / 100.0f;
		s.composite      = (int)p[AURA_COMPOSITE].u.pd.value - 1;
		growBounds       = p[AURA_GROW_BOUNDS].u.bd.value != 0;

		s.scale = in_data->downsample_x.den
			? (float)in_data->downsample_x.num / (float)in_data->downsample_x.den : 1.0f;
		s.time = in_data->time_scale ? (double)in_data->current_time / (double)in_data->time_scale : 0.0;
		// Point values arrive already scaled to the (downsampled) buffer.
		const float sc = s.scale > 0 ? s.scale : 1.0f;
		s.centerX = (float)FIX_2_FLOAT(p[AURA_CIRCLE_CENTER].u.td.x_value) / sc;
		s.centerY = (float)FIX_2_FLOAT(p[AURA_CIRCLE_CENTER].u.td.y_value) / sc;
	}
	for (int i = 1; i < AURA_NUM_PARAMS; ++i) {
		if (out[i]) in_data->inter.checkin_param(in_data->effect_ref, &p[i]);
	}
	return err;
}

// Shape Layer -> this layer, in full-resolution layer pixels, via AEGP (takes
// position, scale, rotation and parenting into account). False when no layer is
// chosen or the transform is not available.
bool GetShapeTransform(PF_InData* in_data, aura::Affine& shapeToLayer) {
	AEGP_SuiteHandler suites(in_data->pica_basicP);
	const AEGP_PluginID pid = 0;   // not needed for calls made from an effect
	AEGP_LayerH effectLayer = nullptr, target = nullptr;
	AEGP_CompH comp = nullptr;
	AEGP_EffectRefH effectH = nullptr;
	AEGP_StreamRefH layerStream = nullptr;
	AEGP_StreamValue2 layerVal;
	bool haveLayerVal = false, ok = false;
	A_Err e = A_Err_NONE;
	try {
		A_Time layerTime = { in_data->current_time, (A_u_long)in_data->time_scale }, compTime = { 0, 1 };
		e = suites.PFInterfaceSuite1()->AEGP_GetEffectLayer(in_data->effect_ref, &effectLayer);
		if (!e) e = suites.PFInterfaceSuite1()->AEGP_GetNewEffectForEffect(pid, in_data->effect_ref, &effectH);
		if (!e) e = suites.StreamSuite6()->AEGP_GetNewEffectStreamByIndex(pid, effectH, AURA_SHAPE_LAYER, &layerStream);
		if (!e) e = suites.LayerSuite9()->AEGP_ConvertLayerToCompTime(effectLayer, &layerTime, &compTime);
		if (!e) {
			e = suites.StreamSuite6()->AEGP_GetNewStreamValue(pid, layerStream, AEGP_LTimeMode_CompTime, &compTime, FALSE, &layerVal);
			haveLayerVal = !e;
		}
		const AEGP_LayerIDVal id = haveLayerVal ? layerVal.val.layer_id : AEGP_LayerIDVal_NONE;
		if (!e && id != AEGP_LayerIDVal_NONE) {
			e = suites.LayerSuite9()->AEGP_GetLayerParentComp(effectLayer, &comp);
			if (!e) e = suites.LayerSuite9()->AEGP_GetLayerFromLayerID(comp, id, &target);
			A_Matrix4 mt, me;
			if (!e) e = suites.LayerSuite9()->AEGP_GetLayerToWorldXform(target, &compTime, &mt);
			if (!e) e = suites.LayerSuite9()->AEGP_GetLayerToWorldXform(effectLayer, &compTime, &me);
			if (!e) {
				// AE matrices are row-vector form: [x y 0 1] * M.
				auto fromMatrix = [](const A_Matrix4& m) {
					aura::Affine t;
					t.a = m.mat[0][0]; t.b = m.mat[0][1];
					t.c = m.mat[1][0]; t.d = m.mat[1][1];
					t.e = m.mat[3][0]; t.f = m.mat[3][1];
					return t;
				};
				aura::Affine toWorld = fromMatrix(mt), worldToLayer;
				if (fromMatrix(me).inverse(worldToLayer)) {
					const aura::Affine& n = worldToLayer;
					const aura::Affine& t = toWorld;
					shapeToLayer.a = n.a * t.a + n.c * t.b; shapeToLayer.b = n.b * t.a + n.d * t.b;
					shapeToLayer.c = n.a * t.c + n.c * t.d; shapeToLayer.d = n.b * t.c + n.d * t.d;
					shapeToLayer.e = n.a * t.e + n.c * t.f + n.e; shapeToLayer.f = n.b * t.e + n.d * t.f + n.f;
					ok = true;
				}
			}
		}
	} catch (...) {
		ok = false;   // suite missing (old host): fall back to this layer's own silhouette
	}
	if (haveLayerVal) suites.StreamSuite6()->AEGP_DisposeStreamValue(&layerVal);
	if (layerStream)  suites.StreamSuite6()->AEGP_DisposeStream(layerStream);
	if (effectH)      suites.EffectSuite4()->AEGP_DisposeEffect(effectH);
	return ok;
}

// Color presets: Color Mode, colours and Band Shading in one pick.
struct ColorPreset {
	int mode;          // aura::ColorMode
	int count;
	A_u_long colors[aura::kMaxPalette];
	float shading;     // percent
	A_u_long shade;
};
const ColorPreset kPresets[] = {
	// Retro Rings: red rings, each fading to orange on its inner side
	{ aura::COLOR_STRIPES, 1, { 0xE01818, 0xE01818, 0xE01818, 0xE01818, 0xE01818 }, 100, 0xFFB020 },
	// Barber Pole
	{ aura::COLOR_STRIPES, 3, { 0x1F4FD0, 0xE02030, 0xFFFFFF, 0xE02030, 0xFFFFFF }, 0, 0xFFB020 },
	// Rainbow
	{ aura::COLOR_STRIPES, 5, { 0xFF3050, 0xFFA020, 0xFFF060, 0x40E080, 0x40A0FF }, 0, 0xFFB020 },
	// Fire
	{ aura::COLOR_STRIPES, 4, { 0xFFF0A0, 0xFFB020, 0xFF5010, 0xB01010, 0xB01010 }, 0, 0xFFB020 },
	// Ocean
	{ aura::COLOR_STRIPES, 4, { 0xE0FAFF, 0x60D0FF, 0x2080E0, 0x103080, 0x103080 }, 0, 0xFFB020 },
	// Candy
	{ aura::COLOR_STRIPES, 4, { 0xFF80C0, 0xFFFFFF, 0x80E0FF, 0xFFF080, 0xFFFFFF }, 0, 0xFFB020 },
	// Neon
	{ aura::COLOR_STRIPES, 3, { 0xFF20E0, 0x20FFF0, 0xF0FF20, 0xFF20E0, 0x20FFF0 }, 0, 0xFFB020 },
	// Mono
	{ aura::COLOR_STRIPES, 2, { 0xFFFFFF, 0x101010, 0xFFFFFF, 0x101010, 0xFFFFFF }, 0, 0xFFB020 },
};
const char kPresetNames[] = "Custom|Retro Rings|Barber Pole|Rainbow|Fire|Ocean|Candy|Neon|Mono";
const int kNumPresetChoices = 1 + (int)(sizeof(kPresets) / sizeof(kPresets[0]));

// Picking a preset copies its colours into the normal parameters, so they can
// still be tweaked (and saved as an Animation Preset) afterwards.
PF_Err UserChangedParam(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
                        const PF_UserChangedParamExtra* extra) {
	if (!extra || extra->param_index != AURA_COLOR_PRESET) return PF_Err_NONE;
	const int choice = (int)params[AURA_COLOR_PRESET]->u.pd.value;
	if (choice < 2 || choice > kNumPresetChoices) return PF_Err_NONE;
	const ColorPreset& pr = kPresets[choice - 2];
	auto setColor = [&](int index, A_u_long rgb) {
		PF_Pixel& c = params[index]->u.cd.value;
		c.alpha = PF_MAX_CHAN8;
		c.red = (A_u_char)((rgb >> 16) & 255); c.green = (A_u_char)((rgb >> 8) & 255); c.blue = (A_u_char)(rgb & 255);
		params[index]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
	};
	const int colorParams[aura::kMaxPalette] = { AURA_COLOR1, AURA_COLOR2, AURA_COLOR3, AURA_COLOR4, AURA_COLOR5 };
	for (int i = 0; i < aura::kMaxPalette; ++i) setColor(colorParams[i], pr.colors[i]);
	setColor(AURA_SHADE_COLOR, pr.shade);
	params[AURA_COLOR_MODE]->u.pd.value = pr.mode + 1;
	params[AURA_COLOR_MODE]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
	params[AURA_NUM_COLORS]->u.pd.value = pr.count;
	params[AURA_NUM_COLORS]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
	params[AURA_BAND_SHADING]->u.fs_d.value = pr.shading;
	params[AURA_BAND_SHADING]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
	return PF_Err_NONE;
}

PF_Err About(PF_InData* in_data, PF_OutData* out_data) {
	std::snprintf(out_data->return_msg, sizeof(out_data->return_msg), "%s v%d.%d\r%s",
	              NAME, MAJOR_VERSION, MINOR_VERSION, DESCRIPTION);
	return PF_Err_NONE;
}

PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data) {
	out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
	// Keep in sync with AE_Effect_Global_OutFlags(_2) in AuraPiPL.r.
	out_data->out_flags  = PF_OutFlag_DEEP_COLOR_AWARE |
	                       PF_OutFlag_I_EXPAND_BUFFER |
	                       PF_OutFlag_NON_PARAM_VARY;   // animates with time (Speed / Wobble)
	out_data->out_flags2 = PF_OutFlag2_SUPPORTS_SMART_RENDER |
	                       PF_OutFlag2_FLOAT_COLOR_AWARE |
	                       PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
	return PF_Err_NONE;
}

PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data) {
	PF_Err err = PF_Err_NONE;
	PF_ParamDef def;

	// --- Source -------------------------------------------------------------
	AEFX_CLR_STRUCT(def);
	PF_ADD_TOPIC("Source", ID_SOURCE_TOPIC);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP("Source Channel", 3, 1, "Alpha|Luminance (Dark Lines)|Luminance (Bright)", ID_SOURCE_MODE);

	// None = this layer. Pick the character layer to put the aura on a background layer.
	AEFX_CLR_STRUCT(def);
	PF_ADD_LAYER("Shape Layer", PF_LayerDefault_NONE, ID_SHAPE_LAYER);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Threshold", 0, 100, 0, 100, 50, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_THRESHOLD);

	AEFX_CLR_STRUCT(def);
	PF_ADD_CHECKBOXX("Fill Interior", TRUE, 0, ID_FILL_INTERIOR);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Close Gaps", 0, 200, 0, 30, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, ID_CLOSE_GAPS);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Contour Smoothness", 0, 500, 0, 50, 2, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, ID_SMOOTHNESS);

	AEFX_CLR_STRUCT(def);
	PF_END_TOPIC(ID_SOURCE_TOPIC_END);

	// --- Rings --------------------------------------------------------------
	AEFX_CLR_STRUCT(def);
	PF_ADD_TOPIC("Rings", ID_RINGS_TOPIC);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP("Ring Shape", 2, 1, "Contour|Circles", ID_RING_SHAPE);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POINT("Circle Center", 50, 50, false, ID_CIRCLE_CENTER);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP("Style", 3, 1, "Lines|Fill|Fill + Lines", ID_STYLE);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Start Offset", -100, 2000, 0, 200, 2, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, ID_START_OFFSET);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Spacing", 1, 2000, 2, 200, 16, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, ID_SPACING);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Line Width", 0, 1000, 0, 50, 4, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, ID_LINE_WIDTH);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Softness", 0, 500, 0, 30, 1, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, ID_SOFTNESS);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Outer Width", 0, 1000, 0, 300, 100, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT, 0, ID_OUTER_WIDTH);

	AEFX_CLR_STRUCT(def);
	PF_ADD_CHECKBOXX("Fill Composition", TRUE, 0, ID_FILL_COMPOSITION);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Aura Length", 1, 20000, 10, 3000, 800, PF_Precision_INTEGER, PF_ValueDisplayFlag_PIXEL, 0, ID_LENGTH);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Fade Out", 0, 20000, 0, 3000, 400, PF_Precision_INTEGER, PF_ValueDisplayFlag_PIXEL, 0, ID_FADE_OUT);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Corner Rounding", 0, 1000, 0, 100, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, ID_CORNER_ROUNDING);

	AEFX_CLR_STRUCT(def);
	PF_END_TOPIC(ID_RINGS_TOPIC_END);

	// --- Animation ----------------------------------------------------------
	AEFX_CLR_STRUCT(def);
	PF_ADD_TOPIC("Animation", ID_ANIM_TOPIC);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Ring Offset", -100000, 100000, -200, 200, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, ID_RING_OFFSET);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Speed (px/sec)", -5000, 5000, -200, 200, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_NONE, 0, ID_SPEED);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Wobble Amount", 0, 1000, 0, 50, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, ID_WOBBLE_AMOUNT);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Wobble Size", 1, 5000, 5, 500, 80, PF_Precision_INTEGER, PF_ValueDisplayFlag_PIXEL, 0, ID_WOBBLE_SIZE);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Wobble Speed", 0, 100, 0, 10, 1, PF_Precision_HUNDREDTHS, PF_ValueDisplayFlag_NONE, 0, ID_WOBBLE_SPEED);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Wobble Seed", 0, 10000, 0, 100, 0, PF_Precision_INTEGER, PF_ValueDisplayFlag_NONE, 0, ID_WOBBLE_SEED);

	AEFX_CLR_STRUCT(def);
	PF_END_TOPIC(ID_ANIM_TOPIC_END);

	// --- Color --------------------------------------------------------------
	AEFX_CLR_STRUCT(def);
	PF_ADD_TOPIC("Color", ID_COLOR_TOPIC);

	AEFX_CLR_STRUCT(def);
	def.flags = PF_ParamFlag_SUPERVISE;
	PF_ADD_POPUP("Color Preset", kNumPresetChoices, 1, kPresetNames, ID_COLOR_PRESET);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP("Color Mode", 5, 1, "Solid|Gradient|Stripes|Rainbow|HSB Steps", ID_COLOR_MODE);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP("Number of Colors", 5, 2, "1|2|3|4|5", ID_NUM_COLORS);

	AEFX_CLR_STRUCT(def);
	PF_ADD_COLOR("Color 1", 90, 215, 255, ID_COLOR1);

	AEFX_CLR_STRUCT(def);
	PF_ADD_COLOR("Color 2", 190, 70, 255, ID_COLOR2);

	AEFX_CLR_STRUCT(def);
	PF_ADD_COLOR("Color 3", 255, 255, 255, ID_COLOR3);

	AEFX_CLR_STRUCT(def);
	PF_ADD_COLOR("Color 4", 230, 25, 40, ID_COLOR4);

	AEFX_CLR_STRUCT(def);
	PF_ADD_COLOR("Color 5", 255, 215, 50, ID_COLOR5);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Gradient Length", 1, 20000, 10, 3000, 600, PF_Precision_INTEGER, PF_ValueDisplayFlag_PIXEL, 0, ID_GRADIENT_LENGTH);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Hue Step (deg/ring)", -360, 360, -90, 90, 30, PF_Precision_TENTHS, PF_ValueDisplayFlag_NONE, 0, ID_HUE_STEP);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("HSB Steps (rings)", 2, 64, 2, 16, 6, PF_Precision_INTEGER, PF_ValueDisplayFlag_NONE, 0, ID_HSB_STEPS);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("HSB Hue Shift", -360, 360, -180, 180, 60, PF_Precision_TENTHS, PF_ValueDisplayFlag_NONE, 0, ID_HSB_HUE);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("HSB Saturation Shift", -100, 100, -100, 100, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_HSB_SAT);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("HSB Brightness Shift", -100, 100, -100, 100, -40, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_HSB_BRI);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP("HSB After Last Step", 4, 4, "Hold|Ping-Pong|Repeat|Continue", ID_HSB_LOOP);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Band Shading", 0, 100, 0, 100, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_BAND_SHADING);

	AEFX_CLR_STRUCT(def);
	PF_ADD_COLOR("Shading Color", 255, 176, 32, ID_SHADE_COLOR);

	AEFX_CLR_STRUCT(def);
	PF_ADD_COLOR("Fill Color (Fill + Lines)", 40, 110, 255, ID_FILL_COLOR);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Fill Opacity", 0, 100, 0, 100, 100, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_FILL_OPACITY);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP("Color Cycle", 3, 1, "Off|Hue Rotate|Palette Loop", ID_COLOR_CYCLE);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Cycle Duration (sec)", 0.01, 600, 0.1, 10, 2, PF_Precision_HUNDREDTHS, PF_ValueDisplayFlag_NONE, 0, ID_CYCLE_DURATION);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Cycle Offset", 0, 100, 0, 100, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_CYCLE_OFFSET);

	AEFX_CLR_STRUCT(def);
	PF_END_TOPIC(ID_COLOR_TOPIC_END);

	// --- Output -------------------------------------------------------------
	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Opacity", 0, 100, 0, 100, 100, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_OPACITY);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP("Composite", 4, 1, "Behind Original|Aura Only|Over Original|Add", ID_COMPOSITE);

	AEFX_CLR_STRUCT(def);
	PF_ADD_CHECKBOXX("Grow Layer Bounds", TRUE, 0, ID_GROW_BOUNDS);

	out_data->num_params = AURA_NUM_PARAMS;
	return err;
}

// ---------------------------------------------------------------------------
// SmartFX

PF_Err PreRender(PF_InData* in_data, PF_OutData* out_data, PF_PreRenderExtra* extra) {
	PF_Err err = PF_Err_NONE;
	aura::Settings s;
	bool growBounds = true;
	ERR(ReadSettings(in_data, s, growBounds));
	if (err) return err;

	const A_long reach = (A_long)std::ceil(aura::reach(s));

	// Every input pixel within `reach` can affect a requested pixel. With
	// Fill Interior the whole silhouette matters, so ask for all of it
	// (After Effects clips the request to what the layer really has).
	PF_RenderRequest req = extra->input->output_request;
	req.rect = Expand(req.rect, s.fillInterior ? 32768 : reach);
	req.preserve_rgb_of_zero_alpha = FALSE;

	PF_CheckoutResult in_result;
	AEFX_CLR_STRUCT(in_result);
	ERR(extra->cb->checkout_layer(in_data->effect_ref, AURA_INPUT, AURA_INPUT, &req,
	                              in_data->current_time, in_data->time_step, in_data->time_scale, &in_result));
	if (err) return err;

	PF_LRect maxOut = growBounds ? Expand(in_result.max_result_rect, reach) : in_result.max_result_rect;
	extra->output->max_result_rect = maxOut;
	extra->output->result_rect = Intersect(extra->input->output_request.rect, maxOut);
	extra->output->solid = FALSE;

	PreRenderData* data = new (std::nothrow) PreRenderData;
	if (!data) return PF_Err_OUT_OF_MEMORY;
	data->inRect = in_result.result_rect;
	data->outRect = extra->output->result_rect;

	// Shape Layer: the whole layer is needed (its silhouette shapes every ring).
	aura::Affine full;
	if (s.ringShape == aura::RING_CONTOUR && GetShapeTransform(in_data, full)) {
		PF_RenderRequest sreq = extra->input->output_request;
		sreq.rect.left = sreq.rect.top = -30000;
		sreq.rect.right = sreq.rect.bottom = 30000;
		sreq.preserve_rgb_of_zero_alpha = FALSE;
		PF_CheckoutResult sres;
		AEFX_CLR_STRUCT(sres);
		if (!extra->cb->checkout_layer(in_data->effect_ref, AURA_SHAPE_LAYER, AURA_SHAPE_LAYER, &sreq,
		                               in_data->current_time, in_data->time_step, in_data->time_scale, &sres)) {
			// Full-res transform -> buffer pixels (buffer = full * downsample).
			const double sx = in_data->downsample_x.den ? (double)in_data->downsample_x.num / in_data->downsample_x.den : 1.0;
			const double sy = in_data->downsample_y.den ? (double)in_data->downsample_y.num / in_data->downsample_y.den : 1.0;
			aura::Affine& m = data->shapeToLayer;
			m = full;
			m.c = full.c * sx / sy; m.b = full.b * sy / sx;
			m.e = full.e * sx;      m.f = full.f * sy;
			data->shapeRect = sres.result_rect;
			data->haveShape = true;
		}
	}
	extra->output->pre_render_data = data;
	extra->output->delete_pre_render_data_func = DeletePreRenderData;
	return err;
}

PF_Err SmartRender(PF_InData* in_data, PF_OutData* out_data, PF_SmartRenderExtra* extra) {
	PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
	const PreRenderData* data = static_cast<const PreRenderData*>(extra->input->pre_render_data);
	if (!data) return PF_Err_BAD_CALLBACK_PARAM;

	PF_EffectWorld* inW = nullptr;
	PF_EffectWorld* outW = nullptr;
	ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, AURA_INPUT, &inW));
	ERR(extra->cb->checkout_output(in_data->effect_ref, &outW));

	aura::Settings s;
	bool growBounds = true;
	ERR(ReadSettings(in_data, s, growBounds));

	PF_EffectWorld* shapeW = nullptr;
	bool shapeOut = false;
	aura::ShapeSource shape;
	if (!err && data->haveShape) {
		shapeOut = !extra->cb->checkout_layer_pixels(in_data->effect_ref, AURA_SHAPE_LAYER, &shapeW);
		if (shapeOut && shapeW && shapeW->data) {
			shape.data = shapeW->data;
			shape.rowbytes = (long)shapeW->rowbytes;
			shape.rect = { (int)data->shapeRect.left, (int)data->shapeRect.top,
			               (int)data->shapeRect.left + (int)shapeW->width, (int)data->shapeRect.top + (int)shapeW->height };
			shape.toLayer = data->shapeToLayer;
			// The aura usually goes on an opaque background here; drawing it
			// behind that layer would hide it, so draw it over instead.
			if (s.composite == aura::COMP_BEHIND) s.composite = aura::COMP_OVER;
		}
	}
	const aura::ShapeSource* shapeP = shape.data ? &shape : nullptr;

	if (!err && outW) {
		PF_WorldSuite2* ws = nullptr;
		err = in_data->pica_basicP->AcquireSuite(kPFWorldSuite, kPFWorldSuiteVersion2, (const void**)&ws);
		PF_PixelFormat format = PF_PixelFormat_INVALID;
		if (!err && ws) err = ws->PF_GetPixelFormat(outW, &format);

		if (!err) {
			aura::Rect inRect = { 0, 0, 0, 0 };
			const void* inData = nullptr;
			long inRowbytes = 0;
			if (inW && inW->data) {
				inRect = { (int)data->inRect.left, (int)data->inRect.top,
				           (int)data->inRect.left + (int)inW->width, (int)data->inRect.top + (int)inW->height };
				inData = inW->data;
				inRowbytes = (long)inW->rowbytes;
			}
			aura::Rect outRect = { (int)data->outRect.left, (int)data->outRect.top,
			                       (int)data->outRect.left + (int)outW->width, (int)data->outRect.top + (int)outW->height };
			try {
				switch (format) {
				case PF_PixelFormat_ARGB32:
					aura::render<Traits8>(inData, inRowbytes, inRect, outW->data, (long)outW->rowbytes, outRect, s, shapeP);
					break;
				case PF_PixelFormat_ARGB64:
					aura::render<Traits16>(inData, inRowbytes, inRect, outW->data, (long)outW->rowbytes, outRect, s, shapeP);
					break;
				case PF_PixelFormat_ARGB128:
					aura::render<Traits32>(inData, inRowbytes, inRect, outW->data, (long)outW->rowbytes, outRect, s, shapeP);
					break;
				default:
					err = PF_Err_BAD_CALLBACK_PARAM;
					break;
				}
			} catch (const std::bad_alloc&) {
				err = PF_Err_OUT_OF_MEMORY;
			} catch (...) {
				err = PF_Err_INTERNAL_STRUCT_DAMAGED;
			}
		}
		if (ws) in_data->pica_basicP->ReleaseSuite(kPFWorldSuite, kPFWorldSuiteVersion2);
	}

	if (inW) ERR2(extra->cb->checkin_layer_pixels(in_data->effect_ref, AURA_INPUT));
	if (shapeOut) ERR2(extra->cb->checkin_layer_pixels(in_data->effect_ref, AURA_SHAPE_LAYER));
	return err;
}

} // namespace

// ---------------------------------------------------------------------------
// Entry points

extern "C" DllExport PF_Err PluginDataEntryFunction2(
	PF_PluginDataPtr inPtr,
	PF_PluginDataCB2 inPluginDataCallBackPtr,
	SPBasicSuite* inSPBasicSuitePtr,
	const char* inHostName,
	const char* inHostVersion)
{
	PF_Err result = PF_Err_INVALID_CALLBACK;
	result = PF_REGISTER_EFFECT_EXT2(
		inPtr,
		inPluginDataCallBackPtr,
		NAME,          // Name
		MATCH_NAME,    // Match Name
		CATEGORY,      // Category
		AE_RESERVED_INFO,
		"EffectMain",  // Entry point
		"");           // Support URL
	return result;
}

PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
                  PF_ParamDef* params[], PF_LayerDef* output, void* extra)
{
	PF_Err err = PF_Err_NONE;
	try {
		switch (cmd) {
		case PF_Cmd_ABOUT:            err = About(in_data, out_data); break;
		case PF_Cmd_GLOBAL_SETUP:     err = GlobalSetup(in_data, out_data); break;
		case PF_Cmd_PARAMS_SETUP:     err = ParamsSetup(in_data, out_data); break;
		case PF_Cmd_SMART_PRE_RENDER: err = PreRender(in_data, out_data, static_cast<PF_PreRenderExtra*>(extra)); break;
		case PF_Cmd_SMART_RENDER:     err = SmartRender(in_data, out_data, static_cast<PF_SmartRenderExtra*>(extra)); break;
		case PF_Cmd_USER_CHANGED_PARAM:
			err = UserChangedParam(in_data, out_data, params, static_cast<const PF_UserChangedParamExtra*>(extra));
			break;
		default: break;
		}
	} catch (PF_Err& thrown) {
		err = thrown;
	}
	return err;
}
