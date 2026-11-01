// GrowFill.cpp — After Effects エフェクト「Grow Fill（塗り広がり）」
// SmartFX 対応（8 / 16 / 32bpc）。実際の画像処理は GrowFillCore.h にある。
#include "GrowFill.h"
#include "GrowFillCore.h"

#include <algorithm>
#include <cstdio>
#include <new>

namespace {

// PreRender で確定したパラメータと範囲を SmartRender に渡すためのデータ
struct RenderInfo {
    growfill::Params params;
    PF_LRect inRect;
    PF_LRect outRect;
};

void DeleteRenderInfo(void* data) { delete static_cast<RenderInfo*>(data); }

// px 単位のスライダーをダウンサンプル時に縮める
float DownsampleScale(const PF_InData* in_data) {
    const float sx = in_data->downsample_x.den ? float(in_data->downsample_x.num) / in_data->downsample_x.den : 1.f;
    const float sy = in_data->downsample_y.den ? float(in_data->downsample_y.num) / in_data->downsample_y.den : 1.f;
    return std::max(0.01f, 0.5f * (sx + sy));
}

PF_LRect Intersect(const PF_LRect& a, const PF_LRect& b) {
    PF_LRect r;
    r.left = std::max(a.left, b.left);
    r.top = std::max(a.top, b.top);
    r.right = std::max(r.left, std::min(a.right, b.right));
    r.bottom = std::max(r.top, std::min(a.bottom, b.bottom));
    return r;
}

PF_Err About(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* [], PF_LayerDef*) {
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    suites.ANSICallbacksSuite1()->sprintf(out_data->return_msg, "%s v%d.%d\r%s",
                                          NAME, MAJOR_VERSION, MINOR_VERSION, DESCRIPTION);
    return PF_Err_NONE;
}

PF_Err GlobalSetup(PF_InData*, PF_OutData* out_data, PF_ParamDef* [], PF_LayerDef*) {
    out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
    out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE;
    out_data->out_flags2 = PF_OutFlag2_SUPPORTS_SMART_RENDER |
                           PF_OutFlag2_FLOAT_COLOR_AWARE |
                           PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
    return PF_Err_NONE;
}

PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* [], PF_LayerDef*) {
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Line Source", 3, 2, "Alpha|Dark Lines (Luminance)|Bright Lines (Luminance)", DETECT_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Line Threshold", 1, 99, 1, 99, 50, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, THRESHOLD_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Close Gaps", 0, 50, 0, 10, 2, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, CLOSE_GAPS_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Fill Target", 2, 1, "All Enclosed Areas|Area at Origin", TARGET_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOX("Include Outside", "On", FALSE, 0, OUTSIDE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Min Area", 0, 100000, 0, 500, 10, PF_Precision_INTEGER, 0, 0, MIN_AREA_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POINT("Origin", 50, 50, FALSE, ORIGIN_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Progress", 0, 100, 0, 100, 100, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, PROGRESS_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Stagger", 0, 100, 0, 100, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, STAGGER_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Edge Softness", 0, 200, 0, 20, 1.5, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, SOFTNESS_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Wobble", 0, 500, 0, 50, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, WOBBLE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Wobble Size", 1, 1000, 1, 200, 24, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, WOBBLE_SIZE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_ANGLE("Evolution", 0, EVOLUTION_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Under Lines", 0, 50, 0, 10, 2, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, UNDER_LINES_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Fill Color", 255, 90, 77, COLOR_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Opacity", 0, 100, 0, 100, 100, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, OPACITY_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Output", 2, 1, "Composite|Fill Only", OUTPUT_DISK_ID);

    out_data->num_params = GF_NUM_PARAMS;
    return err;
}

PF_Err Checkout(PF_InData* in_data, int index, PF_ParamDef& p) {
    AEFX_CLR_STRUCT(p);
    return PF_CHECKOUT_PARAM(in_data, index, in_data->current_time, in_data->time_step, in_data->time_scale, &p);
}

PF_Err Checkin(PF_InData* in_data, PF_ParamDef& p) { return PF_CHECKIN_PARAM(in_data, &p); }

PF_Err ReadParams(PF_InData* in_data, growfill::Params& g, double& originX, double& originY) {
    PF_Err err = PF_Err_NONE;
    PF_ParamDef p[GF_NUM_PARAMS];
    bool out[GF_NUM_PARAMS] = {};
    for (int i = 1; i < GF_NUM_PARAMS && !err; ++i) {
        err = Checkout(in_data, i, p[i]);
        out[i] = !err;
    }
    if (!err) {
        const float s = DownsampleScale(in_data);
        auto fs = [&](int i) { return float(p[i].u.fs_d.value); };
        g.detect = int(p[GF_DETECT].u.pd.value) - 1;  // ポップアップは 1 始まり
        g.lineThreshold = fs(GF_THRESHOLD) / 100.f;
        g.closeGaps = fs(GF_CLOSE_GAPS) * s;
        g.target = int(p[GF_TARGET].u.pd.value) - 1;
        g.includeOutside = p[GF_OUTSIDE].u.bd.value != 0;
        g.minArea = fs(GF_MIN_AREA) * s * s;
        g.progress = fs(GF_PROGRESS) / 100.f;
        g.stagger = fs(GF_STAGGER) / 100.f;
        g.softness = fs(GF_SOFTNESS) * s;
        g.wobble = fs(GF_WOBBLE) * s;
        g.wobbleSize = fs(GF_WOBBLE_SIZE) * s;
        g.evolution = float(FIX_2_FLOAT(p[GF_EVOLUTION].u.ad.value)) / 90.f;
        g.underLines = fs(GF_UNDER_LINES) * s;
        g.color[0] = p[GF_COLOR].u.cd.value.red / 255.f;
        g.color[1] = p[GF_COLOR].u.cd.value.green / 255.f;
        g.color[2] = p[GF_COLOR].u.cd.value.blue / 255.f;
        g.opacity = fs(GF_OPACITY) / 100.f;
        g.output = int(p[GF_OUTPUT].u.pd.value) - 1;
        // 点の値はダウンサンプル後のレイヤー座標で届く
        originX = FIX_2_FLOAT(p[GF_ORIGIN].u.td.x_value);
        originY = FIX_2_FLOAT(p[GF_ORIGIN].u.td.y_value);
    }
    for (int i = 1; i < GF_NUM_PARAMS; ++i)
        if (out[i]) Checkin(in_data, p[i]);
    return err;
}

PF_Err PreRender(PF_InData* in_data, PF_OutData*, PF_PreRenderExtra* extra) {
    PF_Err err = PF_Err_NONE;
    RenderInfo* info = new (std::nothrow) RenderInfo();
    if (!info) return PF_Err_OUT_OF_MEMORY;
    extra->output->pre_render_data = info;
    extra->output->delete_pre_render_data_func = DeleteRenderInfo;

    double ox = 0, oy = 0;
    ERR(ReadParams(in_data, info->params, ox, oy));
    if (err) return err;

    // 囲まれた領域はレイヤー全体を見ないと決まらないので、全体を要求する
    // （After Effects がレイヤーの実際の範囲に切り詰める）
    PF_RenderRequest req = extra->input->output_request;
    req.rect.left = req.rect.top = -30000;
    req.rect.right = req.rect.bottom = 30000;
    req.preserve_rgb_of_zero_alpha = TRUE;
    req.field = PF_Field_FRAME;

    PF_CheckoutResult in_result;
    AEFX_CLR_STRUCT(in_result);
    ERR(extra->cb->checkout_layer(in_data->effect_ref, GF_INPUT, GF_INPUT, &req,
                                  in_data->current_time, in_data->time_step, in_data->time_scale, &in_result));
    if (err) return err;

    extra->output->max_result_rect = in_result.max_result_rect;
    extra->output->result_rect = Intersect(extra->input->output_request.rect, in_result.max_result_rect);
    extra->output->solid = FALSE;
    info->inRect = in_result.result_rect;
    info->outRect = extra->output->result_rect;
    info->params.originX = float(ox - in_result.result_rect.left);
    info->params.originY = float(oy - in_result.result_rect.top);
    return err;
}

// --- 画素フォーマットごとの読み書き ---
template <typename PixelT> struct PixelTraits;
template <> struct PixelTraits<PF_Pixel8> {
    static float ToF(A_u_char v) { return v / 255.f; }
    static A_u_char FromF(float v) { return A_u_char(std::min(255.f, std::max(0.f, v * 255.f + 0.5f))); }
};
template <> struct PixelTraits<PF_Pixel16> {
    static float ToF(A_u_short v) { return v / float(PF_MAX_CHAN16); }
    static A_u_short FromF(float v) { return A_u_short(std::min(float(PF_MAX_CHAN16), std::max(0.f, v * PF_MAX_CHAN16 + 0.5f))); }
};
template <> struct PixelTraits<PF_PixelFloat> {
    static float ToF(PF_FpShort v) { return v; }
    static PF_FpShort FromF(float v) { return v; }
};

template <typename PixelT>
void ReadWorld(const PF_EffectWorld* w, growfill::Image& img) {
    img.resize(w->width, w->height);
    for (A_long y = 0; y < w->height; ++y) {
        const PixelT* row = reinterpret_cast<const PixelT*>(reinterpret_cast<const char*>(w->data) + y * w->rowbytes);
        for (A_long x = 0; x < w->width; ++x) {
            const size_t i = size_t(y) * w->width + x;
            img.r[i] = PixelTraits<PixelT>::ToF(row[x].red);
            img.g[i] = PixelTraits<PixelT>::ToF(row[x].green);
            img.b[i] = PixelTraits<PixelT>::ToF(row[x].blue);
            img.a[i] = PixelTraits<PixelT>::ToF(row[x].alpha);
        }
    }
}

// img の (x - ox, y - oy) を出力 (x, y) に書く
template <typename PixelT>
void WriteWorld(const growfill::Image& img, PF_EffectWorld* w, A_long ox, A_long oy) {
    for (A_long y = 0; y < w->height; ++y) {
        PixelT* row = reinterpret_cast<PixelT*>(reinterpret_cast<char*>(w->data) + y * w->rowbytes);
        const A_long sy = y - oy;
        for (A_long x = 0; x < w->width; ++x) {
            const A_long sx = x - ox;
            if (sx < 0 || sy < 0 || sx >= img.w || sy >= img.h) {
                row[x].alpha = row[x].red = row[x].green = row[x].blue = PixelTraits<PixelT>::FromF(0.f);
                continue;
            }
            const size_t i = size_t(sy) * img.w + sx;
            row[x].alpha = PixelTraits<PixelT>::FromF(img.a[i]);
            row[x].red = PixelTraits<PixelT>::FromF(img.r[i]);
            row[x].green = PixelTraits<PixelT>::FromF(img.g[i]);
            row[x].blue = PixelTraits<PixelT>::FromF(img.b[i]);
        }
    }
}

// ox, oy = 出力バッファ内での入力バッファの原点
template <typename PixelT>
void Run(const PF_EffectWorld* in, PF_EffectWorld* out, const growfill::Params& p, A_long ox, A_long oy) {
    growfill::Image src, dst;
    ReadWorld<PixelT>(in, src);
    growfill::Process(src, dst, p);
    WriteWorld<PixelT>(dst, out, ox, oy);
}

PF_Err SmartRender(PF_InData* in_data, PF_OutData* out_data, PF_SmartRenderExtra* extra) {
    PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
    const RenderInfo* info = static_cast<const RenderInfo*>(extra->input->pre_render_data);
    if (!info) return PF_Err_BAD_CALLBACK_PARAM;

    PF_EffectWorld* inW = nullptr;
    PF_EffectWorld* outW = nullptr;
    ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, GF_INPUT, &inW));
    ERR(extra->cb->checkout_output(in_data->effect_ref, &outW));

    if (!err && inW && outW) {
        PF_WorldSuite2* ws = nullptr;
        PF_PixelFormat format = PF_PixelFormat_INVALID;
        ERR(AEFX_AcquireSuite(in_data, out_data, kPFWorldSuite, kPFWorldSuiteVersion2, "Couldn't load suite.", (void**)&ws));
        if (!err) ERR(ws->PF_GetPixelFormat(inW, &format));
        if (!err) {
            const A_long ox = info->inRect.left - info->outRect.left;
            const A_long oy = info->inRect.top - info->outRect.top;
            try {
                switch (format) {
                    case PF_PixelFormat_ARGB128: Run<PF_PixelFloat>(inW, outW, info->params, ox, oy); break;
                    case PF_PixelFormat_ARGB64:  Run<PF_Pixel16>(inW, outW, info->params, ox, oy); break;
                    case PF_PixelFormat_ARGB32:  Run<PF_Pixel8>(inW, outW, info->params, ox, oy); break;
                    default: err = PF_Err_BAD_CALLBACK_PARAM; break;
                }
            } catch (const std::bad_alloc&) {
                err = PF_Err_OUT_OF_MEMORY;
            }
        }
        ERR2(AEFX_ReleaseSuite(in_data, out_data, kPFWorldSuite, kPFWorldSuiteVersion2, "Couldn't release suite."));
    }
    if (inW) ERR2(extra->cb->checkin_layer_pixels(in_data->effect_ref, GF_INPUT));
    return err;
}

}  // namespace

extern "C" DllExport PF_Err PluginDataEntryFunction2(PF_PluginDataPtr inPtr, PF_PluginDataCB2 inPluginDataCallBackPtr,
                                                     SPBasicSuite*, const char* inHostName, const char* inHostVersion) {
    PF_Err result = PF_Err_INVALID_CALLBACK;
    (void)inHostName; (void)inHostVersion;
    PF_REGISTER_EFFECT_EXT2(inPtr, inPluginDataCallBackPtr, NAME, MATCH_NAME, CATEGORY,
                                     AE_RESERVED_INFO, "EffectMain", "https://claude.ai");
    return result;
}

PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output, void* extra) {
    PF_Err err = PF_Err_NONE;
    try {
        switch (cmd) {
            case PF_Cmd_ABOUT:            err = About(in_data, out_data, params, output); break;
            case PF_Cmd_GLOBAL_SETUP:     err = GlobalSetup(in_data, out_data, params, output); break;
            case PF_Cmd_PARAMS_SETUP:     err = ParamsSetup(in_data, out_data, params, output); break;
            case PF_Cmd_SMART_PRE_RENDER: err = PreRender(in_data, out_data, reinterpret_cast<PF_PreRenderExtra*>(extra)); break;
            case PF_Cmd_SMART_RENDER:     err = SmartRender(in_data, out_data, reinterpret_cast<PF_SmartRenderExtra*>(extra)); break;
            default: break;
        }
    } catch (PF_Err& thrown) {
        err = thrown;
    } catch (const std::bad_alloc&) {
        err = PF_Err_OUT_OF_MEMORY;
    }
    return err;
}
