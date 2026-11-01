// GooMerge.cpp — After Effects エフェクト「Goo Merge（くっつき合体）」
// SmartFX 対応（8 / 16 / 32bpc）。実際の画像処理は GooMergeCore.h にある。
#include "GooMerge.h"
#include "GooMergeCore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <new>

namespace {

// PreRender で確定したパラメータと範囲を SmartRender に渡すためのデータ
struct RenderInfo {
    goomerge::Params params;
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

PF_LRect Expand(PF_LRect r, A_long by) {
    r.left -= by; r.top -= by; r.right += by; r.bottom += by;
    return r;
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
    out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_I_EXPAND_BUFFER;  // PiPL と一致させること
    out_data->out_flags2 = PF_OutFlag2_SUPPORTS_SMART_RENDER |
                           PF_OutFlag2_FLOAT_COLOR_AWARE |
                           PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
    return PF_Err_NONE;
}

PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* [], PF_LayerDef*) {
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Merge Radius", 0, 500, 0, 100, 16, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, RADIUS_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Round Corners", 0, 200, 0, 50, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, ROUND_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Style", 2, 1, "Solid|Bubble", STYLE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Color", 2, 1, "Layer Colors|Fill Color", COLOR_MODE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Fill Color", 255, 255, 255, FILL_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Bubble", BUBBLE_START_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Bubble Opacity", 0, 100, 0, 100, 25, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, BUBBLE_OPACITY_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Rim Width", 0, 200, 0, 40, 6, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, RIM_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Highlight", 0, 100, 0, 100, 80, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, HIGHLIGHT_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_ANGLE("Light Direction", -45, LIGHT_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(BUBBLE_END_DISK_ID);

    out_data->num_params = GM_NUM_PARAMS;
    return err;
}

PF_Err Checkout(PF_InData* in_data, int index, PF_ParamDef& p) {
    AEFX_CLR_STRUCT(p);
    return PF_CHECKOUT_PARAM(in_data, index, in_data->current_time, in_data->time_step, in_data->time_scale, &p);
}

PF_Err Checkin(PF_InData* in_data, PF_ParamDef& p) { return PF_CHECKIN_PARAM(in_data, &p); }

PF_Err ReadParams(PF_InData* in_data, goomerge::Params& c) {
    PF_Err err = PF_Err_NONE;
    PF_ParamDef p[GM_NUM_PARAMS];
    bool out[GM_NUM_PARAMS] = {};
    for (int i = 1; i < GM_NUM_PARAMS && !err; ++i) {
        if (i == GM_BUBBLE_START || i == GM_BUBBLE_END) continue;
        err = Checkout(in_data, i, p[i]);
        out[i] = !err;
    }
    if (!err) {
        const float s = DownsampleScale(in_data);
        auto fs = [&](int i) { return float(p[i].u.fs_d.value); };
        c.radius = fs(GM_RADIUS) * s;
        c.roundCorners = fs(GM_ROUND) * s;
        c.style = int(p[GM_STYLE].u.pd.value) - 1;  // ポップアップは 1 始まり
        c.colorMode = int(p[GM_COLOR_MODE].u.pd.value) - 1;
        c.fill[0] = p[GM_FILL].u.cd.value.red / 255.f;
        c.fill[1] = p[GM_FILL].u.cd.value.green / 255.f;
        c.fill[2] = p[GM_FILL].u.cd.value.blue / 255.f;
        c.bubbleOpacity = fs(GM_BUBBLE_OPACITY) / 100.f;
        c.rimWidth = fs(GM_RIM) * s;
        c.highlight = fs(GM_HIGHLIGHT) / 100.f;
        // AE の角度は 0°＝上・時計回り。コアは 0°＝右・90°＝下
        c.lightAngle = float(FIX_2_FLOAT(p[GM_LIGHT].u.ad.value)) - 90.f;
    }
    for (int i = 1; i < GM_NUM_PARAMS; ++i)
        if (out[i]) Checkin(in_data, p[i]);
    return err;
}

PF_Err PreRender(PF_InData* in_data, PF_OutData*, PF_PreRenderExtra* extra) {
    PF_Err err = PF_Err_NONE;
    RenderInfo* info = new (std::nothrow) RenderInfo();
    if (!info) return PF_Err_OUT_OF_MEMORY;
    extra->output->pre_render_data = info;
    extra->output->delete_pre_render_data_func = DeleteRenderInfo;

    ERR(ReadParams(in_data, info->params));
    if (err) return err;

    // 2×半径までの形がつながるので入力はその分広く要求する。出力はアンチエイリアスの分だけ広げる
    const A_long reach = A_long(std::ceil(goomerge::Reach(info->params)));
    const A_long need = A_long(std::ceil(2.f * (info->params.radius + info->params.roundCorners))) + reach;
    PF_RenderRequest req = extra->input->output_request;
    req.rect = Expand(req.rect, need);
    req.preserve_rgb_of_zero_alpha = FALSE;
    req.field = PF_Field_FRAME;

    PF_CheckoutResult in_result;
    AEFX_CLR_STRUCT(in_result);
    ERR(extra->cb->checkout_layer(in_data->effect_ref, GM_INPUT, GM_INPUT, &req,
                                  in_data->current_time, in_data->time_step, in_data->time_scale, &in_result));
    if (err) return err;

    const PF_LRect maxOut = Expand(in_result.max_result_rect, reach);
    extra->output->max_result_rect = maxOut;
    extra->output->result_rect = Intersect(extra->input->output_request.rect, maxOut);
    extra->output->solid = FALSE;
    info->inRect = in_result.result_rect;
    info->outRect = extra->output->result_rect;
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
void ReadWorld(const PF_EffectWorld* w, goomerge::Image& img) {
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
void WriteWorld(const goomerge::Image& img, PF_EffectWorld* w, A_long ox, A_long oy) {
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
void Run(const PF_EffectWorld* in, PF_EffectWorld* out, const goomerge::Params& p, A_long ox, A_long oy) {
    goomerge::Image src, dst;
    ReadWorld<PixelT>(in, src);
    goomerge::Process(src, dst, out->width, out->height, ox, oy, p);
    WriteWorld<PixelT>(dst, out, 0, 0);
}

PF_Err SmartRender(PF_InData* in_data, PF_OutData* out_data, PF_SmartRenderExtra* extra) {
    PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
    const RenderInfo* info = static_cast<const RenderInfo*>(extra->input->pre_render_data);
    if (!info) return PF_Err_BAD_CALLBACK_PARAM;

    PF_EffectWorld* inW = nullptr;
    PF_EffectWorld* outW = nullptr;
    ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, GM_INPUT, &inW));
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
    if (inW) ERR2(extra->cb->checkin_layer_pixels(in_data->effect_ref, GM_INPUT));
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
