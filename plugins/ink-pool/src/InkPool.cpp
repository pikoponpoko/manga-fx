// InkPool.cpp — After Effects エフェクト「Ink Pool（墨溜まり）」
// SmartFX 対応（8 / 16 / 32bpc）。実際の画像処理は InkPoolCore.h にある。
#include "InkPool.h"
#include "InkPoolCore.h"

#include <algorithm>
#include <cstdio>
#include <new>

namespace {

// PreRender で確定したパラメータを SmartRender に渡すためのデータ
struct RenderInfo {
    inkpool::Params params;
    PF_LRect inRect = {0, 0, 0, 0};   // 入力として受け取る範囲（レイヤー座標）
    PF_LRect outRect = {0, 0, 0, 0};  // 出力する範囲（レイヤー座標）
};

// 32bpc ではスライダーを px 単位で保持するので、ダウンサンプル時に縮める
float DownsampleScale(const PF_InData* in_data) {
    const float sx = in_data->downsample_x.den ? float(in_data->downsample_x.num) / in_data->downsample_x.den : 1.f;
    const float sy = in_data->downsample_y.den ? float(in_data->downsample_y.num) / in_data->downsample_y.den : 1.f;
    return std::max(0.01f, 0.5f * (sx + sy));
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
    PF_ADD_FLOAT_SLIDERX("Melt Size", 0, 500, 0, 40, 6, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, SIZE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Melt Strength", 0, 100, 0, 100, 50, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, STRENGTH_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Max Spread", 0, 300, 0, 200, 60, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, SPREAD_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Junction Pool", 0, 100, 0, 100, 50, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, JUNCTION_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Keep Gaps", 0, 100, 0, 100, 50, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, KEEP_GAPS_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Smoothness", 0, 100, 0, 20, 3, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, SMOOTH_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("End Taper", 0, 100, 0, 100, 50, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, END_TAPER_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Melt Color", 2, 1, "From Lines|Custom", COLOR_MODE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Custom Color", 0, 0, 0, COLOR_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Output", 2, 1, "Composite|Melted Parts Only", OUTPUT_DISK_ID);

    out_data->num_params = IP_NUM_PARAMS;
    return err;
}

PF_Err CheckoutFloat(PF_InData* in_data, int index, double& value) {
    PF_ParamDef p;
    AEFX_CLR_STRUCT(p);
    PF_Err err = PF_CHECKOUT_PARAM(in_data, index, in_data->current_time, in_data->time_step, in_data->time_scale, &p);
    if (!err) value = p.u.fs_d.value;
    return err;
}

PF_Err CheckoutInt(PF_InData* in_data, int index, bool popup, int& value) {
    PF_ParamDef p;
    AEFX_CLR_STRUCT(p);
    PF_Err err = PF_CHECKOUT_PARAM(in_data, index, in_data->current_time, in_data->time_step, in_data->time_scale, &p);
    if (!err) value = popup ? int(p.u.pd.value) : int(p.u.sd.value);
    return err;
}

PF_Err PreRender(PF_InData* in_data, PF_OutData*, PF_PreRenderExtra* extra) {
    PF_Err err = PF_Err_NONE;
    AEGP_SuiteHandler suites(in_data->pica_basicP);

    PF_Handle infoH = suites.HandleSuite1()->host_new_handle(sizeof(RenderInfo));
    if (!infoH) return PF_Err_OUT_OF_MEMORY;
    RenderInfo* info = reinterpret_cast<RenderInfo*>(suites.HandleSuite1()->host_lock_handle(infoH));
    if (!info) return PF_Err_OUT_OF_MEMORY;
    new (info) RenderInfo();
    extra->output->pre_render_data = infoH;

    double threshold = 50, size = 6, strength = 50, spread = 60, keepGaps = 50, smooth = 3, endTaper = 50, junction = 50;
    int detect = 2, colorMode = 1, outputMode = 1;
    PF_ParamDef colorP;
    AEFX_CLR_STRUCT(colorP);

    ERR(CheckoutInt(in_data, IP_DETECT, true, detect));
    ERR(CheckoutFloat(in_data, IP_THRESHOLD, threshold));
    ERR(CheckoutFloat(in_data, IP_SIZE, size));
    ERR(CheckoutFloat(in_data, IP_STRENGTH, strength));
    ERR(CheckoutFloat(in_data, IP_SPREAD, spread));
    ERR(CheckoutFloat(in_data, IP_JUNCTION, junction));
    ERR(CheckoutFloat(in_data, IP_KEEP_GAPS, keepGaps));
    ERR(CheckoutFloat(in_data, IP_SMOOTH, smooth));
    ERR(CheckoutFloat(in_data, IP_END_TAPER, endTaper));
    ERR(CheckoutInt(in_data, IP_COLOR_MODE, true, colorMode));
    ERR(PF_CHECKOUT_PARAM(in_data, IP_COLOR, in_data->current_time, in_data->time_step, in_data->time_scale, &colorP));
    ERR(CheckoutInt(in_data, IP_OUTPUT, true, outputMode));

    if (!err) {
        const float s = DownsampleScale(in_data);
        inkpool::Params& p = info->params;
        p.detect = detect - 1;  // ポップアップは 1 始まり
        p.lineThreshold = float(threshold / 100.0);
        p.meltSize = float(size) * s;
        p.strength = float(strength / 100.0);
        p.spread = float(spread / 100.0);
        p.junction = float(junction / 100.0);
        p.keepGaps = float(keepGaps / 100.0);
        p.melt = float(smooth) * s;
        p.endTaper = float(endTaper / 100.0);
        p.colorMode = colorMode - 1;
        p.color[0] = colorP.u.cd.value.red / 255.f;
        p.color[1] = colorP.u.cd.value.green / 255.f;
        p.color[2] = colorP.u.cd.value.blue / 255.f;
        p.output = outputMode - 1;

        // 溶け具合や線の端の判定は周囲の線に左右されるので、入力は広めに要求する
        const A_long margin = A_long(3.f * p.meltSize + 3.f * p.melt) + 16;
        PF_RenderRequest req = extra->input->output_request;
        req.rect.left -= margin;
        req.rect.top -= margin;
        req.rect.right += margin;
        req.rect.bottom += margin;
        req.preserve_rgb_of_zero_alpha = TRUE;
        req.field = PF_Field_FRAME;

        PF_CheckoutResult in_result;
        ERR(extra->cb->checkout_layer(in_data->effect_ref, IP_INPUT, IP_INPUT, &req,
                                      in_data->current_time, in_data->time_step, in_data->time_scale, &in_result));
        if (!err) {
            // 入力は余白付きで受け取るが、出力は AE から求められた範囲を超えてはいけない
            const PF_LRect& want = extra->input->output_request.rect;
            PF_LRect out = in_result.result_rect;
            out.left = std::max(out.left, want.left);
            out.top = std::max(out.top, want.top);
            out.right = std::min(out.right, want.right);
            out.bottom = std::min(out.bottom, want.bottom);
            if (out.right < out.left) out.right = out.left;
            if (out.bottom < out.top) out.bottom = out.top;
            extra->output->result_rect = out;
            extra->output->max_result_rect = in_result.max_result_rect;
            info->inRect = in_result.result_rect;
            info->outRect = out;
        }
    }

    suites.HandleSuite1()->host_unlock_handle(infoH);
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
void ReadWorld(const PF_EffectWorld* w, inkpool::Image& img) {
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
void WriteWorld(const inkpool::Image& img, PF_EffectWorld* w, A_long ox, A_long oy) {
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

template <typename PixelT>
void Run(const PF_EffectWorld* in, PF_EffectWorld* out, const inkpool::Params& p, A_long ox, A_long oy) {
    inkpool::Image src, dst;
    ReadWorld<PixelT>(in, src);
    inkpool::Process(src, dst, p);
    WriteWorld<PixelT>(dst, out, ox, oy);
}

PF_Err SmartRender(PF_InData* in_data, PF_OutData* out_data, PF_SmartRenderExtra* extra) {
    PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
    AEGP_SuiteHandler suites(in_data->pica_basicP);

    PF_Handle infoH = reinterpret_cast<PF_Handle>(extra->input->pre_render_data);
    RenderInfo* info = infoH ? reinterpret_cast<RenderInfo*>(suites.HandleSuite1()->host_lock_handle(infoH)) : nullptr;
    if (!info) return PF_Err_BAD_CALLBACK_PARAM;

    PF_EffectWorld* inW = nullptr;
    PF_EffectWorld* outW = nullptr;
    ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, IP_INPUT, &inW));
    ERR(extra->cb->checkout_output(in_data->effect_ref, &outW));

    if (!err && inW && outW) {
        PF_WorldSuite2* ws = nullptr;
        PF_PixelFormat format = PF_PixelFormat_INVALID;
        ERR(AEFX_AcquireSuite(in_data, out_data, kPFWorldSuite, kPFWorldSuiteVersion2, "Couldn't load suite.", (void**)&ws));
        if (!err) ERR(ws->PF_GetPixelFormat(inW, &format));
        if (!err) {
            // 出力バッファ内での入力バッファの原点（PreRender で決めた範囲の差）
            const A_long ox = info->inRect.left - info->outRect.left, oy = info->inRect.top - info->outRect.top;
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
    ERR2(extra->cb->checkin_layer_pixels(in_data->effect_ref, IP_INPUT));
    suites.HandleSuite1()->host_unlock_handle(infoH);
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
