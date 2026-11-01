// Kaleido.cpp
// After Effects SmartFX entry point for "Iris Kaleidoscope".
// Supports 8 / 16 / 32 bpc and Multi-Frame Rendering. The image work itself
// lives in KaleidoCore.{h,cpp}.

#include "Kaleido.h"
#include "KaleidoCore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <new>

namespace {

// ---------------------------------------------------------------------------
// Pixel access. After Effects hands effects straight (un-premultiplied) pixels.

struct Traits8 {
	using Pixel = PF_Pixel8;
	static kaleido::RGBA read(const Pixel& p) {
		const float k = 1.0f / PF_MAX_CHAN8;
		return { p.red * k, p.green * k, p.blue * k, p.alpha * k };
	}
	static A_u_char q(float v) {
		v = v < 0 ? 0 : (v > 1 ? 1 : v);
		return (A_u_char)(v * PF_MAX_CHAN8 + 0.5f);
	}
	static void write(Pixel& p, const kaleido::RGBA& c) {
		p.alpha = q(c.a); p.red = q(c.r); p.green = q(c.g); p.blue = q(c.b);
	}
};

struct Traits16 {
	using Pixel = PF_Pixel16;
	static kaleido::RGBA read(const Pixel& p) {
		const float k = 1.0f / PF_MAX_CHAN16;
		return { p.red * k, p.green * k, p.blue * k, p.alpha * k };
	}
	static A_u_short q(float v) {
		v = v < 0 ? 0 : (v > 1 ? 1 : v);
		return (A_u_short)(v * PF_MAX_CHAN16 + 0.5f);
	}
	static void write(Pixel& p, const kaleido::RGBA& c) {
		p.alpha = q(c.a); p.red = q(c.r); p.green = q(c.g); p.blue = q(c.b);
	}
};

struct Traits32 {
	using Pixel = PF_PixelFloat;
	static kaleido::RGBA read(const Pixel& p) { return { p.red, p.green, p.blue, p.alpha }; }
	static void write(Pixel& p, const kaleido::RGBA& c) {
		p.alpha = std::min(1.0f, std::max(0.0f, c.a)); p.red = c.r; p.green = c.g; p.blue = c.b;
	}
};

// Data handed from PreRender to SmartRender.
struct PreRenderData {
	PF_LRect inRect;
	PF_LRect outRect;
	PF_LRect patRect;
	bool hasPattern;
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

bool IsEmpty(const PF_LRect& r) { return r.right <= r.left || r.bottom <= r.top; }

// ---------------------------------------------------------------------------
// Parameters

bool IsTopicOrLayer(int i) {
	return i == KAL_MASK_TOPIC || i == KAL_MASK_TOPIC_END || i == KAL_CENTER_TOPIC ||
	       i == KAL_CENTER_TOPIC_END || i == KAL_PATTERN_TOPIC || i == KAL_PATTERN_TOPIC_END ||
	       i == KAL_MOTION_TOPIC || i == KAL_MOTION_TOPIC_END || i == KAL_DEPTH_TOPIC ||
	       i == KAL_DEPTH_TOPIC_END || i == KAL_OVERLAY_TOPIC || i == KAL_OVERLAY_TOPIC_END ||
	       i == KAL_PATTERN_LAYER;   // the layer is checked out with checkout_layer
}

// Reads every parameter at the current time (SmartFX must check them out).
PF_Err ReadSettings(PF_InData* in_data, kaleido::Settings& s) {
	PF_Err err = PF_Err_NONE;
	PF_ParamDef p[KAL_NUM_PARAMS];
	bool out[KAL_NUM_PARAMS] = {};
	for (int i = 1; i < KAL_NUM_PARAMS && !err; ++i) {
		AEFX_CLR_STRUCT(p[i]);
		if (IsTopicOrLayer(i)) continue;
		err = in_data->inter.checkout_param(in_data->effect_ref, i, in_data->current_time,
		                                    in_data->time_step, in_data->time_scale, &p[i]);
		out[i] = !err;
	}
	if (!err) {
		auto fs = [&](int i) { return (float)p[i].u.fs_d.value; };
		auto pct = [&](int i) { return (float)p[i].u.fs_d.value / 100.0f; };
		auto col = [&](int i) {
			const PF_Pixel& c = p[i].u.cd.value;
			return kaleido::Color{ c.red / 255.0f, c.green / 255.0f, c.blue / 255.0f };
		};
		auto popup = [&](int i) { return (int)p[i].u.pd.value - 1; };
		auto angle = [&](int i) { return (float)FIX_2_FLOAT(p[i].u.ad.value); };

		s.maskMode      = popup(KAL_MASK_MODE);
		s.keyColor      = col(KAL_KEY_COLOR);
		s.keyTolerance  = pct(KAL_KEY_TOLERANCE);
		s.keySoftness   = pct(KAL_KEY_SOFTNESS);
		s.choke         = fs(KAL_CHOKE);
		s.feather       = fs(KAL_FEATHER);
		s.minEyeSize    = fs(KAL_MIN_EYE_SIZE);
		s.centerMode    = popup(KAL_CENTER_MODE);
		// Point values are full-resolution layer coordinates.
		s.centerX       = (float)FIX_2_FLOAT(p[KAL_CENTER_POINT].u.td.x_value);
		s.centerY       = (float)FIX_2_FLOAT(p[KAL_CENTER_POINT].u.td.y_value);
		s.shapeFit      = pct(KAL_SHAPE_FIT);
		s.irisSize      = pct(KAL_IRIS_SIZE);
		s.pupilSize     = pct(KAL_PUPIL_SIZE);
		s.shape         = popup(KAL_SHAPE);
		s.copies        = (int)p[KAL_COPIES].u.sd.value;
		s.ringSpacing   = pct(KAL_RING_SPACING);
		s.copySize      = pct(KAL_COPY_SIZE);
		s.copyRotation  = angle(KAL_COPY_ROTATION);
		s.mirror        = p[KAL_MIRROR].u.bd.value != 0;
		s.stagger       = pct(KAL_STAGGER);
		s.rotation      = angle(KAL_ROTATION);
		s.rotationSpeed = fs(KAL_ROTATION_SPEED);
		s.swayAmount    = fs(KAL_SWAY_AMOUNT);
		s.swaySpeed     = fs(KAL_SWAY_SPEED);
		s.swayLag       = fs(KAL_SWAY_LAG);
		s.twist         = fs(KAL_TWIST);
		s.flowSpeed     = fs(KAL_FLOW_SPEED);
		s.waveAmount    = pct(KAL_WAVE_AMOUNT);
		s.waveCount     = fs(KAL_WAVE_COUNT);
		s.waveSpeed     = fs(KAL_WAVE_SPEED);
		s.depthFade     = pct(KAL_DEPTH_FADE);
		s.fadeColor     = col(KAL_FADE_COLOR);
		s.blurAmount    = fs(KAL_BLUR_AMOUNT);
		s.focusDepth    = pct(KAL_FOCUS_DEPTH);
		s.focusRange    = pct(KAL_FOCUS_RANGE);
		s.overlayMode   = popup(KAL_OVERLAY_MODE);
		s.overlayOuter  = col(KAL_OVERLAY_OUTER);
		s.overlayInner  = col(KAL_OVERLAY_INNER);
		s.overlayAmount = pct(KAL_OVERLAY_AMOUNT);
		s.blendMode     = popup(KAL_BLEND_MODE);
		s.opacity       = pct(KAL_OPACITY);
		s.composite     = popup(KAL_COMPOSITE);

		s.scale = in_data->downsample_x.den
			? (float)in_data->downsample_x.num / (float)in_data->downsample_x.den : 1.0f;
		s.time = in_data->time_scale ? (double)in_data->current_time / (double)in_data->time_scale : 0.0;
	}
	for (int i = 1; i < KAL_NUM_PARAMS; ++i) {
		if (out[i]) in_data->inter.checkin_param(in_data->effect_ref, &p[i]);
	}
	return err;
}

PF_Err About(PF_InData* in_data, PF_OutData* out_data) {
	std::snprintf(out_data->return_msg, sizeof(out_data->return_msg), "%s v%d.%d\r%s",
	              NAME, MAJOR_VERSION, MINOR_VERSION, DESCRIPTION);
	return PF_Err_NONE;
}

PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data) {
	out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
	// Keep in sync with AE_Effect_Global_OutFlags(_2) in KaleidoPiPL.r.
	out_data->out_flags  = PF_OutFlag_DEEP_COLOR_AWARE |
	                       PF_OutFlag_NON_PARAM_VARY;   // animates with time (rotation / sway / waves)
	out_data->out_flags2 = PF_OutFlag2_SUPPORTS_SMART_RENDER |
	                       PF_OutFlag2_FLOAT_COLOR_AWARE |
	                       PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
	return PF_Err_NONE;
}

PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data) {
	PF_Err err = PF_Err_NONE;
	PF_ParamDef def;

	// --- Eye Mask -----------------------------------------------------------
	AEFX_CLR_STRUCT(def);
	PF_ADD_TOPIC("Eye Mask", ID_MASK_TOPIC);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP("Mask Source", 2, 1, "Layer Alpha|Key Color", ID_MASK_MODE);

	AEFX_CLR_STRUCT(def);
	PF_ADD_COLOR("Key Color", 64, 115, 217, ID_KEY_COLOR);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Key Tolerance", 0, 100, 0, 60, 20, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_KEY_TOLERANCE);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Key Softness", 0, 100, 0, 60, 15, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_KEY_SOFTNESS);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Mask Choke", -200, 200, -20, 20, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, ID_CHOKE);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Mask Feather", 0, 200, 0, 20, 1, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, ID_FEATHER);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Min Eye Size", 0, 2000, 0, 200, 12, PF_Precision_INTEGER, PF_ValueDisplayFlag_PIXEL, 0, ID_MIN_EYE_SIZE);

	AEFX_CLR_STRUCT(def);
	PF_END_TOPIC(ID_MASK_TOPIC_END);

	// --- Center -------------------------------------------------------------
	AEFX_CLR_STRUCT(def);
	PF_ADD_TOPIC("Center", ID_CENTER_TOPIC);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP("Center Mode", 2, 1, "Auto (Each Eye)|Manual Point", ID_CENTER_MODE);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POINT("Manual Center", 50, 50, FALSE, ID_CENTER_POINT);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Fit Eye Shape", 0, 100, 0, 100, 100, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_SHAPE_FIT);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Iris Size", 10, 500, 50, 200, 125, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT, 0, ID_IRIS_SIZE);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Pupil Size", 0, 95, 0, 60, 12, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_PUPIL_SIZE);

	AEFX_CLR_STRUCT(def);
	PF_END_TOPIC(ID_CENTER_TOPIC_END);

	// --- Pattern ------------------------------------------------------------
	AEFX_CLR_STRUCT(def);
	PF_ADD_TOPIC("Pattern", ID_PATTERN_TOPIC);

	AEFX_CLR_STRUCT(def);
	PF_ADD_LAYER("Pattern Layer", PF_LayerDefault_NONE, ID_PATTERN_LAYER);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP("Built-in Shape", 4, 1, "Petal|Diamond|Dot|Star", ID_SHAPE);

	AEFX_CLR_STRUCT(def);
	PF_ADD_SLIDER("Copies per Ring", 1, 360, 3, 120, 36, ID_COPIES);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Ring Spacing", 5, 500, 30, 300, 100, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT, 0, ID_RING_SPACING);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Copy Size", 5, 300, 20, 200, 90, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT, 0, ID_COPY_SIZE);

	AEFX_CLR_STRUCT(def);
	PF_ADD_ANGLE("Copy Rotation", 0, ID_COPY_ROTATION);

	AEFX_CLR_STRUCT(def);
	PF_ADD_CHECKBOXX("Mirror Copies", TRUE, 0, ID_MIRROR);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Ring Stagger", 0, 100, 0, 100, 50, PF_Precision_INTEGER, PF_ValueDisplayFlag_PERCENT, 0, ID_STAGGER);

	AEFX_CLR_STRUCT(def);
	PF_END_TOPIC(ID_PATTERN_TOPIC_END);

	// --- Motion -------------------------------------------------------------
	AEFX_CLR_STRUCT(def);
	PF_ADD_TOPIC("Motion", ID_MOTION_TOPIC);

	AEFX_CLR_STRUCT(def);
	PF_ADD_ANGLE("Rotation", 0, ID_ROTATION);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Rotation Speed (deg/sec)", -3600, 3600, -180, 180, 20, PF_Precision_TENTHS, PF_ValueDisplayFlag_NONE, 0, ID_ROTATION_SPEED);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Sway Amount (deg)", 0, 360, 0, 45, 8, PF_Precision_TENTHS, PF_ValueDisplayFlag_NONE, 0, ID_SWAY_AMOUNT);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Sway Speed (Hz)", 0, 30, 0, 3, 0.5, PF_Precision_HUNDREDTHS, PF_ValueDisplayFlag_NONE, 0, ID_SWAY_SPEED);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Sway Lag (deg/ring)", -360, 360, -180, 180, 40, PF_Precision_TENTHS, PF_ValueDisplayFlag_NONE, 0, ID_SWAY_LAG);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Twist (deg/ring)", -360, 360, -90, 90, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_NONE, 0, ID_TWIST);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Flow Speed (rings/sec)", -20, 20, -2, 2, 0, PF_Precision_HUNDREDTHS, PF_ValueDisplayFlag_NONE, 0, ID_FLOW_SPEED);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Wave Amount", 0, 50, 0, 15, 3, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_WAVE_AMOUNT);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Wave Count", 0, 64, 0, 24, 7, PF_Precision_INTEGER, PF_ValueDisplayFlag_NONE, 0, ID_WAVE_COUNT);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Wave Speed (Hz)", -30, 30, -3, 3, 0.3, PF_Precision_HUNDREDTHS, PF_ValueDisplayFlag_NONE, 0, ID_WAVE_SPEED);

	AEFX_CLR_STRUCT(def);
	PF_END_TOPIC(ID_MOTION_TOPIC_END);

	// --- Depth --------------------------------------------------------------
	AEFX_CLR_STRUCT(def);
	PF_ADD_TOPIC("Depth", ID_DEPTH_TOPIC);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Depth Fade", 0, 100, 0, 100, 50, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_DEPTH_FADE);

	AEFX_CLR_STRUCT(def);
	PF_ADD_COLOR("Fade Color", 0, 0, 0, ID_FADE_COLOR);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Blur Amount", 0, 500, 0, 30, 2.5, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, ID_BLUR_AMOUNT);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Focus Depth", 0, 100, 0, 100, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_FOCUS_DEPTH);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Focus Range", 0, 100, 0, 100, 15, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_FOCUS_RANGE);

	AEFX_CLR_STRUCT(def);
	PF_END_TOPIC(ID_DEPTH_TOPIC_END);

	// --- Color Overlay ------------------------------------------------------
	AEFX_CLR_STRUCT(def);
	PF_ADD_TOPIC("Color Overlay", ID_OVERLAY_TOPIC);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP("Overlay Mode", 5, 1, "Off|Tint|Multiply|Screen|Normal", ID_OVERLAY_MODE);

	AEFX_CLR_STRUCT(def);
	PF_ADD_COLOR("Outer Color", 77, 217, 255, ID_OVERLAY_OUTER);

	AEFX_CLR_STRUCT(def);
	PF_ADD_COLOR("Inner Color", 166, 64, 255, ID_OVERLAY_INNER);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Overlay Amount", 0, 100, 0, 100, 100, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_OVERLAY_AMOUNT);

	AEFX_CLR_STRUCT(def);
	PF_END_TOPIC(ID_OVERLAY_TOPIC_END);

	// --- Output -------------------------------------------------------------
	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP("Blend Mode", 5, 1, "Normal|Multiply|Screen|Add|Overlay", ID_BLEND_MODE);

	AEFX_CLR_STRUCT(def);
	PF_ADD_FLOAT_SLIDERX("Opacity", 0, 100, 0, 100, 100, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, ID_OPACITY);

	AEFX_CLR_STRUCT(def);
	PF_ADD_POPUP("Composite", 2, 1, "Over Original|Pattern Only", ID_COMPOSITE);

	out_data->num_params = KAL_NUM_PARAMS;
	return err;
}

// ---------------------------------------------------------------------------
// SmartFX

PF_Err PreRender(PF_InData* in_data, PF_OutData* out_data, PF_PreRenderExtra* extra) {
	PF_Err err = PF_Err_NONE;

	// The whole eye is needed to find its centre and oval, so ask for all of
	// the layer (After Effects clips the request to what the layer really has).
	PF_RenderRequest req = extra->input->output_request;
	req.rect = Expand(req.rect, 32768);
	req.preserve_rgb_of_zero_alpha = FALSE;

	PF_CheckoutResult in_result;
	AEFX_CLR_STRUCT(in_result);
	ERR(extra->cb->checkout_layer(in_data->effect_ref, KAL_INPUT, KAL_INPUT, &req,
	                              in_data->current_time, in_data->time_step, in_data->time_scale, &in_result));
	if (err) return err;

	// The motif layer, whole (it is trimmed to its visible part later).
	PF_RenderRequest preq = extra->input->output_request;
	preq.rect.left = preq.rect.top = -32768;
	preq.rect.right = preq.rect.bottom = 32768;
	preq.preserve_rgb_of_zero_alpha = FALSE;
	PF_CheckoutResult pat_result;
	AEFX_CLR_STRUCT(pat_result);
	ERR(extra->cb->checkout_layer(in_data->effect_ref, KAL_PATTERN_LAYER, KAL_PATTERN_LAYER, &preq,
	                              in_data->current_time, in_data->time_step, in_data->time_scale, &pat_result));
	if (err) return err;

	extra->output->max_result_rect = in_result.max_result_rect;
	extra->output->result_rect = Intersect(extra->input->output_request.rect, in_result.max_result_rect);
	extra->output->solid = FALSE;

	PreRenderData* data = new (std::nothrow) PreRenderData;
	if (!data) return PF_Err_OUT_OF_MEMORY;
	data->inRect = in_result.result_rect;
	data->outRect = extra->output->result_rect;
	data->patRect = pat_result.result_rect;
	data->hasPattern = !IsEmpty(pat_result.result_rect);
	extra->output->pre_render_data = data;
	extra->output->delete_pre_render_data_func = DeletePreRenderData;
	return err;
}

PF_Err SmartRender(PF_InData* in_data, PF_OutData* out_data, PF_SmartRenderExtra* extra) {
	PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
	const PreRenderData* data = static_cast<const PreRenderData*>(extra->input->pre_render_data);
	if (!data) return PF_Err_BAD_CALLBACK_PARAM;

	PF_EffectWorld* inW = nullptr;
	PF_EffectWorld* patW = nullptr;
	PF_EffectWorld* outW = nullptr;
	ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, KAL_INPUT, &inW));
	if (!err && data->hasPattern) {
		ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, KAL_PATTERN_LAYER, &patW));
	}
	ERR(extra->cb->checkout_output(in_data->effect_ref, &outW));

	kaleido::Settings s;
	ERR(ReadSettings(in_data, s));

	if (!err && outW) {
		PF_WorldSuite2* ws = nullptr;
		err = in_data->pica_basicP->AcquireSuite(kPFWorldSuite, kPFWorldSuiteVersion2, (const void**)&ws);
		PF_PixelFormat format = PF_PixelFormat_INVALID;
		if (!err && ws) err = ws->PF_GetPixelFormat(outW, &format);

		if (!err) {
			kaleido::Rect inRect = { 0, 0, 0, 0 };
			const void* inData = nullptr;
			long inRowbytes = 0;
			if (inW && inW->data) {
				inRect = { (int)data->inRect.left, (int)data->inRect.top,
				           (int)data->inRect.left + (int)inW->width, (int)data->inRect.top + (int)inW->height };
				inData = inW->data;
				inRowbytes = (long)inW->rowbytes;
			}
			const void* patData = nullptr;
			long patRowbytes = 0;
			int pw = 0, ph = 0;
			if (patW && patW->data) {
				patData = patW->data;
				patRowbytes = (long)patW->rowbytes;
				pw = (int)patW->width;
				ph = (int)patW->height;
			}
			kaleido::Rect outRect = { (int)data->outRect.left, (int)data->outRect.top,
			                          (int)data->outRect.left + (int)outW->width, (int)data->outRect.top + (int)outW->height };
			try {
				switch (format) {
				case PF_PixelFormat_ARGB32:
					kaleido::render<Traits8>(inData, inRowbytes, inRect, patData, patRowbytes, pw, ph,
					                         outW->data, (long)outW->rowbytes, outRect, s);
					break;
				case PF_PixelFormat_ARGB64:
					kaleido::render<Traits16>(inData, inRowbytes, inRect, patData, patRowbytes, pw, ph,
					                          outW->data, (long)outW->rowbytes, outRect, s);
					break;
				case PF_PixelFormat_ARGB128:
					kaleido::render<Traits32>(inData, inRowbytes, inRect, patData, patRowbytes, pw, ph,
					                          outW->data, (long)outW->rowbytes, outRect, s);
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

	if (patW) ERR2(extra->cb->checkin_layer_pixels(in_data->effect_ref, KAL_PATTERN_LAYER));
	if (inW) ERR2(extra->cb->checkin_layer_pixels(in_data->effect_ref, KAL_INPUT));
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
		default: break;
		}
	} catch (PF_Err& thrown) {
		err = thrown;
	}
	return err;
}
