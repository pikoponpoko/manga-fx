// FlowLoop.cpp — After Effects エフェクト「Flow Loop（流れるループ）」
// SmartFX 対応（8 / 16 / 32bpc）。実際の画像処理は FlowLoopCore.h にある。
#include "FlowLoop.h"
#include "FlowLoopCore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <new>
#include <vector>

namespace {

// PreRender で確定したパラメータと範囲を SmartRender に渡すためのデータ
struct RenderInfo {
    flowloop::Params params;
    PF_LRect inRect;
    PF_LRect outRect;
    std::vector<flowloop::Polyline> guides, areas;  // 入力バッファの座標
    bool haveMap = false, haveArea = false;
    PF_LRect mapRect, areaRect;
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
    // NON_PARAM_VARY: 時間で流れ、マスクの形でも変わるので、パラメータが同じでも描き直す
    out_data->out_flags = PF_OutFlag_DEEP_COLOR_AWARE | PF_OutFlag_NON_PARAM_VARY;  // PiPL と一致させること
    out_data->out_flags2 = PF_OutFlag2_SUPPORTS_SMART_RENDER |
                           PF_OutFlag2_FLOAT_COLOR_AWARE |
                           PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
    return PF_Err_NONE;
}

PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* [], PF_LayerDef*) {
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Flow Source", 6, 1, "Masks|Direction|Radial Out|Radial In|Swirl|Flow Map Layer", SOURCE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Loop Duration", 0.1, 600, 0.5, 10, 2, PF_Precision_HUNDREDTHS, 0, 0, LOOP_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Speed", -2000, 2000, 0, 200, 40, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, SPEED_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Phase Offset", 0, 100, 0, 100, 0, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, PHASE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_ANGLE("Direction", 90, DIRECTION_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POINT("Center", 50, 50, FALSE, CENTER_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Guide Reach", 1, 2000, 5, 300, 60, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, INFLUENCE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_LAYER("Flow Map Layer", PF_LayerDefault_NONE, MAP_LAYER_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Map Smoothing", 0, 200, 0, 50, 8, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, MAP_SMOOTH_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_LAYER("Area Layer", PF_LayerDefault_NONE, AREA_LAYER_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Area Feather", 0, 200, 0, 50, 10, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, AREA_FEATHER_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Phase Noise", 0, 100, 0, 100, 50, PF_Precision_TENTHS, PF_ValueDisplayFlag_PERCENT, 0, PHASE_NOISE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Noise Scale", 1, 2000, 10, 500, 120, PF_Precision_TENTHS, PF_ValueDisplayFlag_PIXEL, 0, NOISE_SCALE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_CHECKBOXX("Wrap Edges", FALSE, 0, WRAP_DISK_ID);

    out_data->num_params = FL_NUM_PARAMS;
    return err;
}

PF_Err Checkout(PF_InData* in_data, int index, PF_ParamDef& p) {
    AEFX_CLR_STRUCT(p);
    return PF_CHECKOUT_PARAM(in_data, index, in_data->current_time, in_data->time_step, in_data->time_scale, &p);
}

PF_Err Checkin(PF_InData* in_data, PF_ParamDef& p) { return PF_CHECKIN_PARAM(in_data, &p); }

PF_Err ReadParams(PF_InData* in_data, flowloop::Params& c, double& cx, double& cy) {
    PF_Err err = PF_Err_NONE;
    PF_ParamDef p[FL_NUM_PARAMS];
    bool out[FL_NUM_PARAMS] = {};
    for (int i = 1; i < FL_NUM_PARAMS && !err; ++i) {
        if (i == FL_MAP_LAYER || i == FL_AREA_LAYER) continue;  // レイヤーは PreRender で checkout_layer する
        err = Checkout(in_data, i, p[i]);
        out[i] = !err;
    }
    if (!err) {
        const float s = DownsampleScale(in_data);
        auto fs = [&](int i) { return float(p[i].u.fs_d.value); };
        c.source = int(p[FL_SOURCE].u.pd.value) - 1;  // ポップアップは 1 始まり
        const double t = in_data->time_scale ? double(in_data->current_time) / in_data->time_scale : 0.0;
        const double loop = std::max(0.01, double(p[FL_LOOP].u.fs_d.value));
        const double ph = t / loop + p[FL_PHASE].u.fs_d.value / 100.0;
        c.phase = float(ph - std::floor(ph));
        c.speed = fs(FL_SPEED) * s;
        // AE の角度は 0°＝上・時計回り。コアは 0°＝右・90°＝下
        c.angle = float(FIX_2_FLOAT(p[FL_DIRECTION].u.ad.value)) - 90.f;
        c.influence = fs(FL_INFLUENCE) * s;
        c.mapSmooth = fs(FL_MAP_SMOOTH) * s;
        c.areaFeather = fs(FL_AREA_FEATHER) * s;
        c.phaseNoise = fs(FL_PHASE_NOISE) / 100.f;
        c.noiseScale = fs(FL_NOISE_SCALE) * s;
        c.wrapEdges = p[FL_WRAP].u.bd.value != 0;
        // 点の値はダウンサンプル後のレイヤー座標で届く
        cx = FIX_2_FLOAT(p[FL_CENTER].u.td.x_value);
        cy = FIX_2_FLOAT(p[FL_CENTER].u.td.y_value);
    }
    for (int i = 1; i < FL_NUM_PARAMS; ++i)
        if (out[i]) Checkin(in_data, p[i]);
    return err;
}

// このレイヤーの、合成モードが「なし」のマスクを読む。開いたマスク＝ガイド、閉じたマスク＝流れる範囲。
// 座標はフル解像度のレイヤー座標 → scale を掛けて (ox, oy) を引く
void ReadMasks(PF_InData* in_data, float scale, float ox, float oy,
               std::vector<flowloop::Polyline>& guides, std::vector<flowloop::Polyline>& areas) {
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    const AEGP_PluginID pid = 0;  // エフェクトからの呼び出しでは ID 不要
    AEGP_LayerH layer = nullptr;
    A_long num = 0;
    if (suites.PFInterfaceSuite1()->AEGP_GetEffectLayer(in_data->effect_ref, &layer) || !layer) return;
    if (suites.MaskSuite6()->AEGP_GetLayerNumMasks(layer, &num)) return;
    const A_Time t = {in_data->current_time, A_u_long(in_data->time_scale)};
    for (A_long m = 0; m < num; ++m) {
        AEGP_MaskRefH mask = nullptr;
        if (suites.MaskSuite6()->AEGP_GetLayerMaskByIndex(layer, m, &mask) || !mask) continue;
        PF_MaskMode mode = PF_MaskMode_NONE;
        AEGP_StreamRefH stream = nullptr;
        AEGP_StreamValue2 val;
        bool haveVal = false;
        if (!suites.MaskSuite6()->AEGP_GetMaskMode(mask, &mode) && mode == PF_MaskMode_NONE &&
            !suites.StreamSuite6()->AEGP_GetNewMaskStream(pid, mask, AEGP_MaskStream_OUTLINE, &stream) && stream) {
            haveVal = !suites.StreamSuite6()->AEGP_GetNewStreamValue(pid, stream, AEGP_LTimeMode_LayerTime, &t, FALSE, &val);
        }
        if (haveVal) {
            AEGP_MaskOutlineValH outline = val.val.mask;
            A_Boolean open = FALSE;
            A_long segs = 0;
            suites.MaskOutlineSuite3()->AEGP_IsMaskOutlineOpen(outline, &open);
            suites.MaskOutlineSuite3()->AEGP_GetMaskOutlineNumSegments(outline, &segs);
            flowloop::Polyline pl;
            AEGP_MaskVertex a, b;
            for (A_long j = 0; j < segs; ++j) {
                if (suites.MaskOutlineSuite3()->AEGP_GetMaskOutlineVertexInfo(outline, j, &a) ||
                    suites.MaskOutlineSuite3()->AEGP_GetMaskOutlineVertexInfo(outline, j + 1, &b)) break;
                // ベジェを 16 分割して折れ線にする
                const double p0x = a.x, p0y = a.y, p1x = a.x + a.tan_out_x, p1y = a.y + a.tan_out_y;
                const double p2x = b.x + b.tan_in_x, p2y = b.y + b.tan_in_y, p3x = b.x, p3y = b.y;
                for (int k = (j == 0 ? 0 : 1); k <= 16; ++k) {
                    const double u = k / 16.0, v = 1.0 - u;
                    const double x = v * v * v * p0x + 3 * v * v * u * p1x + 3 * v * u * u * p2x + u * u * u * p3x;
                    const double y = v * v * v * p0y + 3 * v * v * u * p1y + 3 * v * u * u * p2y + u * u * u * p3y;
                    pl.push_back({float(x * scale - ox), float(y * scale - oy)});
                }
            }
            if (pl.size() >= 2) (open ? guides : areas).push_back(pl);
            suites.StreamSuite6()->AEGP_DisposeStreamValue(&val);
        }
        if (stream) suites.StreamSuite6()->AEGP_DisposeStream(stream);
        suites.MaskSuite6()->AEGP_DisposeMask(mask);
    }
}

PF_Err PreRender(PF_InData* in_data, PF_OutData*, PF_PreRenderExtra* extra) {
    PF_Err err = PF_Err_NONE;
    RenderInfo* info = new (std::nothrow) RenderInfo();
    if (!info) return PF_Err_OUT_OF_MEMORY;
    extra->output->pre_render_data = info;
    extra->output->delete_pre_render_data_func = DeleteRenderInfo;

    double cx = 0, cy = 0;
    ERR(ReadParams(in_data, info->params, cx, cy));
    if (err) return err;

    // 画像は最大で半ループ分ずれた所から取ってくるので、その分広く入力を要求する。出力は広げない
    const A_long reach = A_long(std::ceil(flowloop::Reach(info->params)));
    PF_RenderRequest req = extra->input->output_request;
    req.rect = Expand(req.rect, reach);
    req.preserve_rgb_of_zero_alpha = TRUE;
    req.field = PF_Field_FRAME;

    PF_CheckoutResult in_result;
    AEFX_CLR_STRUCT(in_result);
    ERR(extra->cb->checkout_layer(in_data->effect_ref, FL_INPUT, FL_INPUT, &req,
                                  in_data->current_time, in_data->time_step, in_data->time_scale, &in_result));
    if (err) return err;

    extra->output->max_result_rect = in_result.max_result_rect;
    extra->output->result_rect = Intersect(extra->input->output_request.rect, in_result.max_result_rect);
    extra->output->solid = FALSE;
    info->inRect = in_result.result_rect;
    info->outRect = extra->output->result_rect;
    const float ox = float(in_result.result_rect.left), oy = float(in_result.result_rect.top);
    info->params.centerX = float(cx) - ox;
    info->params.centerY = float(cy) - oy;

    ReadMasks(in_data, DownsampleScale(in_data), ox, oy, info->guides, info->areas);

    // 流れの地図・範囲のレイヤー（未指定ならチェックアウトしても中身なし）
    PF_CheckoutResult r;
    if (info->params.source == flowloop::SRC_FLOW_MAP) {
        AEFX_CLR_STRUCT(r);
        if (!extra->cb->checkout_layer(in_data->effect_ref, FL_MAP_LAYER, FL_MAP_LAYER, &req,
                                       in_data->current_time, in_data->time_step, in_data->time_scale, &r)) {
            info->haveMap = true;
            info->mapRect = r.result_rect;
        }
    }
    AEFX_CLR_STRUCT(r);
    if (!extra->cb->checkout_layer(in_data->effect_ref, FL_AREA_LAYER, FL_AREA_LAYER, &req,
                                   in_data->current_time, in_data->time_step, in_data->time_scale, &r)) {
        info->haveArea = true;
        info->areaRect = r.result_rect;
    }
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
void ReadWorld(const PF_EffectWorld* w, flowloop::Image& img) {
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

// 別レイヤーの画素を、入力バッファ（大きさ w×h、原点 inRect）にそろえて読む
template <typename PixelT>
void ReadAligned(const PF_EffectWorld* w, const PF_LRect& rect, const PF_LRect& inRect, int W, int H, flowloop::Image& img) {
    img.resize(W, H);
    const A_long dx = inRect.left - rect.left, dy = inRect.top - rect.top;
    for (int y = 0; y < H; ++y) {
        const A_long sy = y + dy;
        if (sy < 0 || sy >= w->height) continue;
        const PixelT* row = reinterpret_cast<const PixelT*>(reinterpret_cast<const char*>(w->data) + sy * w->rowbytes);
        for (int x = 0; x < W; ++x) {
            const A_long sx = x + dx;
            if (sx < 0 || sx >= w->width) continue;
            const size_t i = size_t(y) * W + x;
            img.r[i] = PixelTraits<PixelT>::ToF(row[sx].red);
            img.g[i] = PixelTraits<PixelT>::ToF(row[sx].green);
            img.b[i] = PixelTraits<PixelT>::ToF(row[sx].blue);
            img.a[i] = PixelTraits<PixelT>::ToF(row[sx].alpha);
        }
    }
}

// img の (x - ox, y - oy) を出力 (x, y) に書く
template <typename PixelT>
void WriteWorld(const flowloop::Image& img, PF_EffectWorld* w, A_long ox, A_long oy) {
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
void Run(const PF_EffectWorld* in, PF_EffectWorld* out, const RenderInfo& info, A_long ox, A_long oy,
         const PF_EffectWorld* mapW, const PF_EffectWorld* areaW) {
    flowloop::Image src, dst, map, area;
    ReadWorld<PixelT>(in, src);
    if (mapW) ReadAligned<PixelT>(mapW, info.mapRect, info.inRect, src.w, src.h, map);
    if (areaW) ReadAligned<PixelT>(areaW, info.areaRect, info.inRect, src.w, src.h, area);
    flowloop::Process(src, dst, out->width, out->height, ox, oy, info.params, info.guides,
                      mapW ? &map : nullptr, info.areas, areaW ? &area : nullptr);
    WriteWorld<PixelT>(dst, out, 0, 0);
}

PF_Err SmartRender(PF_InData* in_data, PF_OutData* out_data, PF_SmartRenderExtra* extra) {
    PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
    const RenderInfo* info = static_cast<const RenderInfo*>(extra->input->pre_render_data);
    if (!info) return PF_Err_BAD_CALLBACK_PARAM;

    PF_EffectWorld* inW = nullptr;
    PF_EffectWorld* outW = nullptr;
    ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, FL_INPUT, &inW));
    ERR(extra->cb->checkout_output(in_data->effect_ref, &outW));
    PF_EffectWorld* mapW = nullptr;
    PF_EffectWorld* areaW = nullptr;
    if (!err && info->haveMap) extra->cb->checkout_layer_pixels(in_data->effect_ref, FL_MAP_LAYER, &mapW);
    if (!err && info->haveArea) extra->cb->checkout_layer_pixels(in_data->effect_ref, FL_AREA_LAYER, &areaW);

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
                    case PF_PixelFormat_ARGB128: Run<PF_PixelFloat>(inW, outW, *info, ox, oy, mapW, areaW); break;
                    case PF_PixelFormat_ARGB64:  Run<PF_Pixel16>(inW, outW, *info, ox, oy, mapW, areaW); break;
                    case PF_PixelFormat_ARGB32:  Run<PF_Pixel8>(inW, outW, *info, ox, oy, mapW, areaW); break;
                    default: err = PF_Err_BAD_CALLBACK_PARAM; break;
                }
            } catch (const std::bad_alloc&) {
                err = PF_Err_OUT_OF_MEMORY;
            }
        }
        ERR2(AEFX_ReleaseSuite(in_data, out_data, kPFWorldSuite, kPFWorldSuiteVersion2, "Couldn't release suite."));
    }
    if (inW) ERR2(extra->cb->checkin_layer_pixels(in_data->effect_ref, FL_INPUT));
    if (mapW) ERR2(extra->cb->checkin_layer_pixels(in_data->effect_ref, FL_MAP_LAYER));
    if (areaW) ERR2(extra->cb->checkin_layer_pixels(in_data->effect_ref, FL_AREA_LAYER));
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
