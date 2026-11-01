// ConcentrationLines.cpp — 漫画風集中線 / 流線 エフェクト (After Effects SmartFX)
//
// 描画そのものは ConcentrationLinesCore.h にあり、ここでは AE とのやり取り
// (パラメータ定義・SmartFX の PreRender / SmartRender・8/16/32bpc の画素変換) だけを行う。

#include "ConcentrationLines.h"
#include "ConcentrationLinesCore.h"

#include <new>

namespace {

// ---------------------------------------------------------------- About / GlobalSetup
PF_Err About(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    suites.ANSICallbacksSuite1()->sprintf(out_data->return_msg, "%s v%d.%d\r%s",
                                          NAME, MAJOR_VERSION, MINOR_VERSION, DESCRIPTION);
    return PF_Err_NONE;
}

PF_Err GlobalSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
    out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
    // PiPL の AE_Effect_Global_OutFlags / _2 と必ず一致させること
    // NON_PARAM_VARY: 時間でちらつき・回転などを行うため、パラメータが同じでも
    // フレームごとに描き直してもらう
    out_data->out_flags  = PF_OutFlag_DEEP_COLOR_AWARE |                 // 0x02000000
                           PF_OutFlag_NON_PARAM_VARY;                    // 0x00000004
    out_data->out_flags2 = PF_OutFlag2_FLOAT_COLOR_AWARE |               // 0x00001000
                           PF_OutFlag2_SUPPORTS_SMART_RENDER |           // 0x00000400
                           PF_OutFlag2_SUPPORTS_THREADED_RENDERING;      // 0x08000000
    return PF_Err_NONE;
}

// ---------------------------------------------------------------- ParamsSetup
PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[], PF_LayerDef* output) {
    PF_Err err = PF_Err_NONE;
    PF_ParamDef def;

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Type", 2, 1, "Radial (Focus)|Parallel (Speed)", TYPE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POINT("Target", 50, 50, false, CENTER_DISK_ID);  // 既定はレイヤー中央

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Follow", 3, 1, "Off (use Target)|Layer Position|Layer Alpha Center", FOLLOW_MODE_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_LAYER("Follow Layer", PF_LayerDefault_NONE, FOLLOW_LAYER_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Follow Offset X", -30000, 30000, -500, 500, 0, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_NONE, 0, FOLLOW_OFFSET_X_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Follow Offset Y", -30000, 30000, -500, 500, 0, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_NONE, 0, FOLLOW_OFFSET_Y_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Inner Width", 0, 20000, 0, 2000, 300, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_NONE, 0, INNER_W_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Inner Height", 0, 20000, 0, 2000, 200, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_NONE, 0, INNER_H_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Inner Shape", 2, 1, "Ellipse|Layer Silhouette", INNER_SHAPE_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_LAYER("Shape Layer", PF_LayerDefault_NONE, SHAPE_LAYER_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Shape Margin", -500, 2000, 0, 200, 20, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_NONE, 0, SHAPE_MARGIN_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Shape Smoothness", 0, 100, 0, 100, 20, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_PERCENT, 0, SHAPE_SMOOTH_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_ANGLE("Angle", 0, ANGLE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Line Count", 1, 20000, 10, 1000, 200, COUNT_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Line Width", 0, 1000, 0, 50, 8, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_NONE, 0, WIDTH_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Width Randomness", 0, 100, 0, 100, 30, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_PERCENT, 0, WIDTH_JITTER_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Spacing Randomness", 0, 100, 0, 100, 15, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_PERCENT, 0, SPACING_JITTER_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Start Randomness", 0, 1000, 0, 200, 25, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_PERCENT, 0, START_JITTER_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Taper", 0, 100, 0, 100, 100, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_PERCENT, 0, TAPER_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Sharpness", 0, 100, 0, 100, 50, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_PERCENT, 0, SHARPNESS_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Edge Softness", 0, 200, 0, 20, 0, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_NONE, 0, SOFTNESS_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Wobble", 0, 200, 0, 10, 0, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_NONE, 0, WOBBLE_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Wobble Size", 5, 5000, 20, 500, 120, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_NONE, 0, WOBBLE_SIZE_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Stroke Unevenness", 0, 100, 0, 100, 0, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_PERCENT, 0, UNEVENNESS_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Line Length (0=inf)", 0, 20000, 0, 2000, 0, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_NONE, 0, LENGTH_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Length Randomness", 0, 100, 0, 100, 30, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_PERCENT, 0, LENGTH_JITTER_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Inner Feather", 0, 2000, 0, 200, 20, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_NONE, 0, FEATHER_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR("Color", 0, 0, 0, COLOR_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Opacity", 0, 100, 0, 100, 100, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_PERCENT, 0, OPACITY_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER("Random Seed", 0, 100000, 0, 1000, 1, SEED_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP("Composite", 3, 1, "Over Source|Behind Source|Lines Only", COMPOSITE_DISK_ID);

    // ---- アニメーション（すべて 0 なら静止。各パラメータのキーフレームも併用できる）
    AEFX_CLR_STRUCT(def);
    PF_ADD_TOPIC("Animation", ANIM_TOPIC_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Flicker Rate (/sec)", 0, 120, 0, 30, 0, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_NONE, 0, FLICKER_RATE_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Rotation Speed (deg/sec)", -3600, 3600, -90, 90, 0, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_NONE, 0, ROTATION_SPEED_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Pulse Amount", 0, 100, 0, 50, 0, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_PERCENT, 0, PULSE_AMOUNT_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Pulse Rate (/sec)", 0, 60, 0, 10, 2, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_NONE, 0, PULSE_RATE_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX("Flow Speed (px/sec)", -100000, 100000, -3000, 3000, 0, PF_Precision_TENTHS,
                         PF_ValueDisplayFlag_NONE, 0, FLOW_SPEED_DISK_ID);
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(ANIM_TOPIC_END_DISK_ID);

    out_data->num_params = CL_NUM_PARAMS;
    return err;
}

// ---------------------------------------------------------------- 画素変換
template <typename P> struct PixTraits;
template <> struct PixTraits<PF_Pixel8> {
    static double toD(A_u_char v) { return v / (double)PF_MAX_CHAN8; }
    static A_u_char fromD(double v) { return (A_u_char)(cl::clamp01(v) * PF_MAX_CHAN8 + 0.5); }
};
template <> struct PixTraits<PF_Pixel16> {
    static double toD(A_u_short v) { return v / (double)PF_MAX_CHAN16; }
    static A_u_short fromD(double v) { return (A_u_short)(cl::clamp01(v) * PF_MAX_CHAN16 + 0.5); }
};
template <> struct PixTraits<PF_PixelFloat> {
    static double toD(PF_FpShort v) { return v; }
    static PF_FpShort fromD(double v) { return (PF_FpShort)v; }  // 32bpc はオーバーレンジを保持
};

struct RenderInfo {
    const cl::Renderer* renderer;
    cl::RGBA color;
    int mode;
    const PF_EffectWorld* input;  // NULL のときは透明として扱う
    A_long outX, outY;            // 出力ワールドの (0,0) のレイヤー座標
    A_long inX, inY;              // 入力ワールドの (0,0) のレイヤー座標
};

// 入力と出力は範囲が異なることがある（テキスト/シェイプレイヤーなど）ので、
// 入力画素はレイヤー座標で引き直し、範囲外は透明とする。
template <typename P>
PF_Err PixelFunc(void* refcon, A_long x, A_long y, P* /*in*/, P* out) {
    const RenderInfo* ri = static_cast<const RenderInfo*>(refcon);
    typedef PixTraits<P> T;
    const A_long lx = x + ri->outX, ly = y + ri->outY;

    P in;
    const A_long ix = lx - ri->inX, iy = ly - ri->inY;
    if (ri->input && ix >= 0 && iy >= 0 && ix < ri->input->width && iy < ri->input->height) {
        in = reinterpret_cast<const P*>(reinterpret_cast<const char*>(ri->input->data) +
                                        iy * ri->input->rowbytes)[ix];
    } else {
        in.alpha = in.red = in.green = in.blue = 0;
    }

    const double cov = ri->renderer->coverage((double)lx, (double)ly);
    if (cov <= 0.0 && ri->mode != cl::COMPOSITE_LINES_ONLY) {
        *out = in;
        return PF_Err_NONE;
    }
    cl::RGBA src = { T::toD(in.red), T::toD(in.green), T::toD(in.blue), T::toD(in.alpha) };
    cl::RGBA o = cl::composite(src, ri->color, cov, ri->mode);
    out->red   = T::fromD(o.r);
    out->green = T::fromD(o.g);
    out->blue  = T::fromD(o.b);
    out->alpha = T::fromD(cl::clamp01(o.a));
    return PF_Err_NONE;
}

// ---------------------------------------------------------------- SmartFX
// PreRender から SmartRender へ渡すレイヤーの範囲（集中線の外端を決めるのに使う）
// 2D アフィン変換 x' = a x + c y + e, y' = b x + d y + f
struct Affine {
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    void apply(double x, double y, double* ox, double* oy) const {
        *ox = a * x + c * y + e;
        *oy = b * x + d * y + f;
    }
    // AE の行列は行ベクトル形式: [x y 0 1] * M
    static Affine fromMatrix(const A_Matrix4& m) {
        Affine t;
        t.a = m.mat[0][0]; t.b = m.mat[0][1];
        t.c = m.mat[1][0]; t.d = m.mat[1][1];
        t.e = m.mat[3][0]; t.f = m.mat[3][1];
        return t;
    }
    bool inverse(Affine* out) const {
        const double det = a * d - b * c;
        if (std::fabs(det) < 1e-12) return false;
        out->a =  d / det; out->b = -b / det;
        out->c = -c / det; out->d =  a / det;
        out->e = -(out->a * e + out->c * f);
        out->f = -(out->b * e + out->d * f);
        return true;
    }
    Affine then(const Affine& n) const {  // this のあとに n
        Affine r;
        r.a = n.a * a + n.c * b; r.b = n.b * a + n.d * b;
        r.c = n.a * c + n.c * d; r.d = n.b * c + n.d * d;
        r.e = n.a * e + n.c * f + n.e; r.f = n.b * e + n.d * f + n.f;
        return r;
    }
};

struct FrameData {
    PF_LRect layerRect;
    int      followMode = FOLLOW_OFF;
    bool     haveFollow = false;   // 追従レイヤーの変換が取れた
    Affine   targetToEffect;       // 追従レイヤーの座標 → このエフェクトのレイヤーの座標（フル解像度）
    double   anchorX = 0, anchorY = 0;  // 追従レイヤーのアンカーポイント（追従レイヤーの座標）
    bool     alphaCheckedOut = false;
    int      innerShape = INNER_SHAPE_ELLIPSE;
    bool     haveShape = false;     // 形用レイヤーの変換が取れ、画素もチェックアウトした
    Affine   shapeToEffect;         // 形用レイヤーの座標 → このエフェクトのレイヤーの座標（フル解像度）
};

// レイヤーパラメータ layerParam で選ばれたレイヤーの座標 → このエフェクトのレイヤーの座標の変換と、
// そのレイヤーのアンカーポイントを AEGP で取得する。コンポ上の位置・回転・拡大・親子付けがすべて反映される。
bool GetLayerTransform(PF_InData* in_data, int layerParam, Affine* toEffect, double* anchorX, double* anchorY) {
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    const AEGP_PluginID pid = 0;  // エフェクトからの呼び出しでは ID 不要
    AEGP_LayerH effectLayer = NULL, target = NULL;
    AEGP_CompH comp = NULL;
    AEGP_EffectRefH effectH = NULL;
    AEGP_StreamRefH layerStream = NULL, anchorStream = NULL;
    AEGP_StreamValue2 layerVal, anchorVal;
    bool haveLayerVal = false, haveAnchorVal = false, ok = false;
    A_Err e = A_Err_NONE;

    try {
        A_Time layerTime = { in_data->current_time, (A_u_long)in_data->time_scale }, compTime = { 0, 1 };
        e = suites.PFInterfaceSuite1()->AEGP_GetEffectLayer(in_data->effect_ref, &effectLayer);
        if (!e) e = suites.PFInterfaceSuite1()->AEGP_GetNewEffectForEffect(pid, in_data->effect_ref, &effectH);
        if (!e) e = suites.StreamSuite6()->AEGP_GetNewEffectStreamByIndex(pid, effectH, layerParam, &layerStream);
        if (!e) e = suites.LayerSuite9()->AEGP_ConvertLayerToCompTime(effectLayer, &layerTime, &compTime);
        if (!e) {
            e = suites.StreamSuite6()->AEGP_GetNewStreamValue(pid, layerStream, AEGP_LTimeMode_CompTime,
                                                             &compTime, FALSE, &layerVal);
            haveLayerVal = !e;
        }
        const AEGP_LayerIDVal id = haveLayerVal ? layerVal.val.layer_id : AEGP_LayerIDVal_NONE;
        if (!e && id != AEGP_LayerIDVal_NONE) {
            e = suites.LayerSuite9()->AEGP_GetLayerParentComp(effectLayer, &comp);
            if (!e) e = suites.LayerSuite9()->AEGP_GetLayerFromLayerID(comp, id, &target);

            A_Matrix4 mt, me;
            if (!e) e = suites.LayerSuite9()->AEGP_GetLayerToWorldXform(target, &compTime, &mt);
            if (!e) e = suites.LayerSuite9()->AEGP_GetLayerToWorldXform(effectLayer, &compTime, &me);
            if (!e) e = suites.StreamSuite6()->AEGP_GetNewLayerStream(pid, target, AEGP_LayerStream_ANCHORPOINT, &anchorStream);
            if (!e) {
                e = suites.StreamSuite6()->AEGP_GetNewStreamValue(pid, anchorStream, AEGP_LTimeMode_CompTime,
                                                                 &compTime, FALSE, &anchorVal);
                haveAnchorVal = !e;
            }
            Affine worldToEffect;
            if (!e && Affine::fromMatrix(me).inverse(&worldToEffect)) {
                *toEffect = Affine::fromMatrix(mt).then(worldToEffect);
                *anchorX = anchorVal.val.three_d.x;
                *anchorY = anchorVal.val.three_d.y;
                ok = true;
            }
        }
    } catch (...) {
        ok = false;  // 必要なスイートが無い古い AE など。追従なしで描く
    }

    if (haveAnchorVal) suites.StreamSuite6()->AEGP_DisposeStreamValue(&anchorVal);
    if (haveLayerVal)  suites.StreamSuite6()->AEGP_DisposeStreamValue(&layerVal);
    if (anchorStream)  suites.StreamSuite6()->AEGP_DisposeStream(anchorStream);
    if (layerStream)   suites.StreamSuite6()->AEGP_DisposeStream(layerStream);
    if (effectH)       suites.EffectSuite4()->AEGP_DisposeEffect(effectH);
    return ok;
}

void DeleteFrameData(void* data) {
    delete static_cast<FrameData*>(data);
}

PF_Err PreRender(PF_InData* in_data, PF_OutData* out_data, PF_PreRenderExtra* extra) {
    PF_Err err = PF_Err_NONE;
    PF_RenderRequest req = extra->input->output_request;
    PF_CheckoutResult in_result;
    AEFX_CLR_STRUCT(in_result);

    ERR(extra->cb->checkout_layer(in_data->effect_ref, CL_INPUT, CL_INPUT, &req,
                                  in_data->current_time, in_data->time_step, in_data->time_scale,
                                  &in_result));
    if (!err) {
        // 線はレイヤー全体に描く。入力（調整レイヤーなら下のレイヤーの合成結果）の範囲は
        // 素材の外枠に縮んでいることがあるので、レイヤー自身の大きさと合わせた範囲を使う。
        // in_data->width/height はフル解像度なのでダウンサンプルを掛ける。
        const double dsx = (double)in_data->downsample_x.num / in_data->downsample_x.den;
        const double dsy = (double)in_data->downsample_y.num / in_data->downsample_y.den;
        PF_LRect layer;
        layer.left   = 0;
        layer.top    = 0;
        layer.right  = (A_long)std::ceil(in_data->width * dsx);
        layer.bottom = (A_long)std::ceil(in_data->height * dsy);

        PF_LRect m = in_result.max_result_rect;
        m.left   = (std::min)(m.left, layer.left);
        m.top    = (std::min)(m.top, layer.top);
        m.right  = (std::max)(m.right, layer.right);
        m.bottom = (std::max)(m.bottom, layer.bottom);

        PF_LRect r = req.rect;
        r.left   = (std::max)(r.left, m.left);
        r.top    = (std::max)(r.top, m.top);
        r.right  = (std::min)(r.right, m.right);
        r.bottom = (std::min)(r.bottom, m.bottom);
        if (r.right < r.left) r.right = r.left;
        if (r.bottom < r.top) r.bottom = r.top;
        extra->output->result_rect     = r;
        extra->output->max_result_rect = m;

        // 描画範囲がタイル分割されても線の形が変わらないよう、レイヤー全体の範囲を渡す
        FrameData* fd = new (std::nothrow) FrameData;
        if (fd) {
            fd->layerRect = layer;  // 集中線の外端（最も太くなる位置）はレイヤーの縁
            extra->output->pre_render_data = fd;
            extra->output->delete_pre_render_data_func = DeleteFrameData;

            // 自動追従
            PF_ParamDef mode;
            AEFX_CLR_STRUCT(mode);
            if (!PF_CHECKOUT_PARAM(in_data, CL_FOLLOW_MODE, in_data->current_time, in_data->time_step,
                                   in_data->time_scale, &mode)) {
                fd->followMode = mode.u.pd.value - 1;
                PF_CHECKIN_PARAM(in_data, &mode);
            }
            if (fd->followMode != FOLLOW_OFF)
                fd->haveFollow = GetLayerTransform(in_data, CL_FOLLOW_LAYER, &fd->targetToEffect, &fd->anchorX, &fd->anchorY);

            // 空白をレイヤーの形に合わせる
            PF_ParamDef shape;
            AEFX_CLR_STRUCT(shape);
            if (!PF_CHECKOUT_PARAM(in_data, CL_INNER_SHAPE, in_data->current_time, in_data->time_step,
                                   in_data->time_scale, &shape)) {
                fd->innerShape = shape.u.pd.value - 1;
                PF_CHECKIN_PARAM(in_data, &shape);
            }
            if (fd->innerShape == INNER_SHAPE_LAYER) {
                double ax, ay;
                if (GetLayerTransform(in_data, CL_SHAPE_LAYER, &fd->shapeToEffect, &ax, &ay)) {
                    PF_RenderRequest sreq = req;
                    sreq.rect.left = sreq.rect.top = -30000;
                    sreq.rect.right = sreq.rect.bottom = 30000;
                    PF_CheckoutResult sres;
                    AEFX_CLR_STRUCT(sres);
                    fd->haveShape = !extra->cb->checkout_layer(in_data->effect_ref, CL_SHAPE_LAYER, CL_SHAPE_LAYER,
                                                               &sreq, in_data->current_time, in_data->time_step,
                                                               in_data->time_scale, &sres);
                }
            }
            if (fd->haveFollow && fd->followMode == FOLLOW_LAYER_ALPHA) {
                // 重心を求めるため、追従レイヤーは全体を要求する
                PF_RenderRequest treq = req;
                treq.rect.left = treq.rect.top = -30000;
                treq.rect.right = treq.rect.bottom = 30000;
                PF_CheckoutResult tres;
                AEFX_CLR_STRUCT(tres);
                fd->alphaCheckedOut = !extra->cb->checkout_layer(in_data->effect_ref, CL_FOLLOW_LAYER, CL_FOLLOW_LAYER,
                                                                 &treq, in_data->current_time, in_data->time_step,
                                                                 in_data->time_scale, &tres);
            }
        }
    }
    return err;
}

// 形用レイヤーの不透明度を、このエフェクトのレイヤーの座標（ダウンサンプル後）で読む
struct ShapeSampler {
    const PF_EffectWorld* world = NULL;
    PF_PixelFormat format = PF_PixelFormat_ARGB32;
    Affine effectToShape;   // フル解像度どうしの変換
    double sx = 1, sy = 1;  // ダウンサンプル倍率

    double operator()(double x, double y) const {
        double fx, fy;
        effectToShape.apply(x / sx, y / sy, &fx, &fy);
        const A_long ix = (A_long)std::floor(fx * sx) - world->origin_x;
        const A_long iy = (A_long)std::floor(fy * sy) - world->origin_y;
        if (ix < 0 || iy < 0 || ix >= world->width || iy >= world->height) return 0.0;
        const char* row = reinterpret_cast<const char*>(world->data) + iy * world->rowbytes;
        switch (format) {
        case PF_PixelFormat_ARGB128: return reinterpret_cast<const PF_PixelFloat*>(row)[ix].alpha;
        case PF_PixelFormat_ARGB64:  return reinterpret_cast<const PF_Pixel16*>(row)[ix].alpha / (double)PF_MAX_CHAN16;
        default:                     return reinterpret_cast<const PF_Pixel8*>(row)[ix].alpha / (double)PF_MAX_CHAN8;
        }
    }
};

// レイヤーの不透明部分の重心（ワールド座標の画素位置）。不透明な画素が無ければ false。
template <typename P>
bool AlphaCentroid(const PF_EffectWorld* w, double maxAlpha, double* cx, double* cy) {
    // 大きなレイヤーでも軽くなるよう、最大で約 512×512 点を調べる
    const A_long step = (std::max)((A_long)1, (std::max)(w->width, w->height) / 512);
    double sum = 0, sx = 0, sy = 0;
    for (A_long y = 0; y < w->height; y += step) {
        const P* row = reinterpret_cast<const P*>(reinterpret_cast<const char*>(w->data) + y * w->rowbytes);
        for (A_long x = 0; x < w->width; x += step) {
            const double a = row[x].alpha / maxAlpha;
            if (a <= 0) continue;
            sum += a; sx += a * (x + 0.5); sy += a * (y + 0.5);
        }
    }
    if (sum <= 0) return false;
    *cx = sx / sum + w->origin_x;
    *cy = sy / sum + w->origin_y;
    return true;
}

PF_Err SmartRender(PF_InData* in_data, PF_OutData* out_data, PF_SmartRenderExtra* extra) {
    PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
    AEGP_SuiteHandler suites(in_data->pica_basicP);

    PF_EffectWorld* inputP = NULL;
    PF_EffectWorld* outputP = NULL;
    ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, CL_INPUT, &inputP));
    ERR(extra->cb->checkout_output(in_data->effect_ref, &outputP));
    if (err || !outputP) {
        ERR2(extra->cb->checkin_layer_pixels(in_data->effect_ref, CL_INPUT));
        return err;
    }

    // パラメータ取得
    // グループの開始/終了は値を持たないのでチェックアウトしない
    PF_ParamDef p[CL_NUM_PARAMS];
    bool held[CL_NUM_PARAMS] = {};
    for (int i = 1; i < CL_NUM_PARAMS && !err; ++i) {
        AEFX_CLR_STRUCT(p[i]);
        // レイヤーパラメータは checkout_layer_pixels で取るので除外
        if (i == CL_ANIM_TOPIC || i == CL_ANIM_TOPIC_END || i == CL_FOLLOW_LAYER || i == CL_SHAPE_LAYER) continue;
        ERR(PF_CHECKOUT_PARAM(in_data, i, in_data->current_time, in_data->time_step,
                              in_data->time_scale, &p[i]));
        if (!err) held[i] = true;
    }

    if (!err) {
        // スライダーの px 値はダウンサンプルを自分で掛ける。ポイントは AE が掛け済み。
        const double sx = (double)in_data->downsample_x.num / in_data->downsample_x.den;
        const double sy = (double)in_data->downsample_y.num / in_data->downsample_y.den;
        const double s  = (sx + sy) * 0.5;

        cl::Settings st;
        st.type          = p[CL_TYPE].u.pd.value - 1;
        st.cx            = FIX_2_FLOAT(p[CL_CENTER].u.td.x_value);
        st.cy            = FIX_2_FLOAT(p[CL_CENTER].u.td.y_value);
        st.innerRx       = p[CL_INNER_W].u.fs_d.value * sx;
        st.innerRy       = p[CL_INNER_H].u.fs_d.value * sy;
        st.angle         = FIX_2_FLOAT(p[CL_ANGLE].u.ad.value) * cl::kPi / 180.0;
        st.count         = p[CL_COUNT].u.sd.value;
        st.width         = p[CL_WIDTH].u.fs_d.value * s;
        st.widthJitter   = p[CL_WIDTH_JITTER].u.fs_d.value / 100.0;
        st.spacingJitter = p[CL_SPACING_JITTER].u.fs_d.value / 100.0;
        st.startJitter   = p[CL_START_JITTER].u.fs_d.value / 100.0;
        st.taper         = p[CL_TAPER].u.fs_d.value / 100.0;
        st.length        = p[CL_LENGTH].u.fs_d.value * s;
        st.lengthJitter  = p[CL_LENGTH_JITTER].u.fs_d.value / 100.0;
        st.feather       = p[CL_FEATHER].u.fs_d.value * s;
        st.sharpness     = p[CL_SHARPNESS].u.fs_d.value / 100.0;
        st.softness      = p[CL_SOFTNESS].u.fs_d.value * s;
        st.wobble        = p[CL_WOBBLE].u.fs_d.value * s;
        st.wobbleSize    = p[CL_WOBBLE_SIZE].u.fs_d.value * s;
        st.unevenness    = p[CL_UNEVENNESS].u.fs_d.value / 100.0;
        st.shapeMargin   = p[CL_SHAPE_MARGIN].u.fs_d.value * s;
        st.shapeSmooth   = p[CL_SHAPE_SMOOTH].u.fs_d.value / 100.0 * 0.25;
        st.seed          = (uint32_t)p[CL_SEED].u.sd.value;
        st.scale         = s;
        if (const FrameData* fd = static_cast<const FrameData*>(extra->input->pre_render_data)) {
            st.hasFrame = true;
            st.frameX0  = fd->layerRect.left;
            st.frameY0  = fd->layerRect.top;
            st.frameX1  = fd->layerRect.right;
            st.frameY1  = fd->layerRect.bottom;

            // 自動追従: 追従レイヤー上の点をこのレイヤーの座標に移して中心にする
            if (fd->haveFollow) {
                double tx = fd->anchorX, ty = fd->anchorY;  // 追従レイヤーの座標（フル解像度）
                if (fd->followMode == FOLLOW_LAYER_ALPHA && fd->alphaCheckedOut) {
                    PF_EffectWorld* tw = NULL;
                    if (!extra->cb->checkout_layer_pixels(in_data->effect_ref, CL_FOLLOW_LAYER, &tw) && tw) {
                        PF_PixelFormat tf = PF_PixelFormat_INVALID;
                        AEFX_SuiteScoper<PF_WorldSuite2> ws(in_data, kPFWorldSuite, kPFWorldSuiteVersion2, out_data);
                        double px = 0, py = 0;
                        bool found = false;
                        if (!ws->PF_GetPixelFormat(tw, &tf)) {
                            if (tf == PF_PixelFormat_ARGB128)     found = AlphaCentroid<PF_PixelFloat>(tw, 1.0, &px, &py);
                            else if (tf == PF_PixelFormat_ARGB64) found = AlphaCentroid<PF_Pixel16>(tw, PF_MAX_CHAN16, &px, &py);
                            else                                   found = AlphaCentroid<PF_Pixel8>(tw, PF_MAX_CHAN8, &px, &py);
                        }
                        if (found) { tx = px / sx; ty = py / sy; }  // ダウンサンプル → フル解像度
                    }
                    extra->cb->checkin_layer_pixels(in_data->effect_ref, CL_FOLLOW_LAYER);
                }
                double ex, ey;
                fd->targetToEffect.apply(tx, ty, &ex, &ey);
                st.cx = (ex + p[CL_FOLLOW_OFFSET_X].u.fs_d.value) * sx;
                st.cy = (ey + p[CL_FOLLOW_OFFSET_Y].u.fs_d.value) * sy;
            }
        }

        cl::Animation anim;
        anim.flickerRate   = p[CL_FLICKER_RATE].u.fs_d.value;
        anim.rotationSpeed = p[CL_ROTATION_SPEED].u.fs_d.value;
        anim.pulseAmount   = p[CL_PULSE_AMOUNT].u.fs_d.value / 100.0;
        anim.pulseRate     = p[CL_PULSE_RATE].u.fs_d.value;
        anim.flowSpeed     = p[CL_FLOW_SPEED].u.fs_d.value;
        // レイヤー時間（秒）。レイヤーの開始位置からの時間なのでレイヤーを動かしても動きが保たれる
        const double t = in_data->time_scale ? (double)in_data->current_time / in_data->time_scale : 0.0;
        st = cl::animate(st, anim, t, s);

        // 空白をレイヤーの形に合わせる: 中心から各線の向きへ、形の一番外側を探す
        const FrameData* fdr = static_cast<const FrameData*>(extra->input->pre_render_data);
        PF_EffectWorld* shapeW = NULL;
        bool shapeHeld = false;
        ShapeSampler sampler;
        if (fdr && fdr->haveShape && st.type == cl::TYPE_RADIAL &&
            !extra->cb->checkout_layer_pixels(in_data->effect_ref, CL_SHAPE_LAYER, &shapeW)) {
            shapeHeld = true;
            if (shapeW && fdr->shapeToEffect.inverse(&sampler.effectToShape)) {
                sampler.world = shapeW;
                sampler.sx = sx;
                sampler.sy = sy;
                AEFX_SuiteScoper<PF_WorldSuite2> ws(in_data, kPFWorldSuite, kPFWorldSuiteVersion2, out_data);
                if (ws->PF_GetPixelFormat(shapeW, &sampler.format)) sampler.format = PF_PixelFormat_ARGB32;
                const double cx = st.cx, cy = st.cy;
                const double maxR = std::hypot(st.frameX1 - st.frameX0, st.frameY1 - st.frameY0) + 1.0;
                const double step = (std::max)(1.0, 2.0 * s);
                st.shapeDistance = [cx, cy, maxR, step, &sampler](double ux, double uy) {
                    return cl::outermostHit(cx, cy, ux, uy, maxR, step, sampler);
                };
            }
        }

        const PF_Pixel& c = p[CL_COLOR].u.cd.value;
        RenderInfo ri;
        cl::Renderer renderer(st);  // 形の探索はここで終わるので、この直後に返却してよい
        if (shapeHeld) extra->cb->checkin_layer_pixels(in_data->effect_ref, CL_SHAPE_LAYER);
        ri.renderer = &renderer;
        ri.color    = { c.red / 255.0, c.green / 255.0, c.blue / 255.0, p[CL_OPACITY].u.fs_d.value / 100.0 };
        ri.mode     = p[CL_COMPOSITE].u.pd.value - 1;
        ri.input    = inputP;
        ri.outX     = outputP->origin_x;
        ri.outY     = outputP->origin_y;
        ri.inX      = inputP ? inputP->origin_x : 0;
        ri.inY      = inputP ? inputP->origin_y : 0;

        // 入力画素は PixelFunc 内で読むので、iterate の src には出力自身を渡す
        PF_EffectWorld* srcP = outputP;

        PF_PixelFormat format = PF_PixelFormat_INVALID;
        AEFX_SuiteScoper<PF_WorldSuite2> wsP(in_data, kPFWorldSuite, kPFWorldSuiteVersion2, out_data);
        ERR(wsP->PF_GetPixelFormat(outputP, &format));

        if (!err) {
            switch (format) {
            case PF_PixelFormat_ARGB128:
                ERR(suites.IterateFloatSuite2()->iterate(in_data, 0, outputP->height, srcP, NULL,
                                                         &ri, PixelFunc<PF_PixelFloat>, outputP));
                break;
            case PF_PixelFormat_ARGB64:
                ERR(suites.Iterate16Suite2()->iterate(in_data, 0, outputP->height, srcP, NULL,
                                                      &ri, PixelFunc<PF_Pixel16>, outputP));
                break;
            case PF_PixelFormat_ARGB32:
            default:
                ERR(suites.Iterate8Suite2()->iterate(in_data, 0, outputP->height, srcP, NULL,
                                                     &ri, PixelFunc<PF_Pixel8>, outputP));
                break;
            }
        }
    }

    for (int i = 1; i < CL_NUM_PARAMS; ++i) {
        if (held[i]) ERR2(PF_CHECKIN_PARAM(in_data, &p[i]));
    }
    ERR2(extra->cb->checkin_layer_pixels(in_data->effect_ref, CL_INPUT));
    return err;
}

}  // namespace

// ---------------------------------------------------------------- エントリポイント
extern "C" DllExport PF_Err PluginDataEntryFunction2(PF_PluginDataPtr inPtr,
                                                     PF_PluginDataCB2 inPluginDataCallBackPtr,
                                                     SPBasicSuite* inSPBasicSuitePtr,
                                                     const char* inHostName,
                                                     const char* inHostVersion) {
    PF_Err result = PF_Err_INVALID_CALLBACK;  // PF_REGISTER_EFFECT_EXT2 は result に代入するマクロ
    PF_REGISTER_EFFECT_EXT2(inPtr, inPluginDataCallBackPtr, NAME, MATCH_NAME, CATEGORY,
                            AE_RESERVED_INFO, "EffectMain", "https://claude.ai");
    return result;
}

PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
                  PF_ParamDef* params[], PF_LayerDef* output, void* extra) {
    PF_Err err = PF_Err_NONE;
    try {
        switch (cmd) {
        case PF_Cmd_ABOUT:            err = About(in_data, out_data, params, output); break;
        case PF_Cmd_GLOBAL_SETUP:     err = GlobalSetup(in_data, out_data, params, output); break;
        case PF_Cmd_PARAMS_SETUP:     err = ParamsSetup(in_data, out_data, params, output); break;
        case PF_Cmd_SMART_PRE_RENDER: err = PreRender(in_data, out_data, (PF_PreRenderExtra*)extra); break;
        case PF_Cmd_SMART_RENDER:     err = SmartRender(in_data, out_data, (PF_SmartRenderExtra*)extra); break;
        default: break;
        }
    } catch (PF_Err& thrown) {
        err = thrown;
    } catch (...) {
        err = PF_Err_OUT_OF_MEMORY;
    }
    return err;
}
