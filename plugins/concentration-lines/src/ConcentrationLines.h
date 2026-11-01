// ConcentrationLines.h — 漫画風集中線 / 流線 エフェクト (After Effects SmartFX)
#pragma once

#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectSuites.h"
#include "AE_Macros.h"
#include "AEGP_SuiteHandler.h"
#include "AEFX_SuiteHelper.h"
#include "Param_Utils.h"
#include "String_Utils.h"

#define NAME          "Concentration Lines"
#define MATCH_NAME    "KZ ConcentrationLines"
#define CATEGORY      "Manga FX"
#define DESCRIPTION   "Manga style focus lines / speed lines.\rWorks on any footage (8/16/32 bpc)."

#define MAJOR_VERSION 1
#define MINOR_VERSION 0
#define BUG_VERSION   0
#define STAGE_VERSION PF_Stage_DEVELOP
#define BUILD_VERSION 1

// パラメータの並び（0 は入力レイヤー）
enum {
    CL_INPUT = 0,
    CL_TYPE,            // 種類: 集中線 / 流線
    CL_CENTER,          // ターゲット（中心）。コンポ画面上でドラッグして選べる
    CL_FOLLOW_MODE,     // 自動追従: オフ / レイヤーの位置 / レイヤーの不透明部分の中心
    CL_FOLLOW_LAYER,    // 追従するレイヤー
    CL_FOLLOW_OFFSET_X, // 追従位置からのずらし (px)
    CL_FOLLOW_OFFSET_Y,
    CL_INNER_W,         // 空白の幅 (半径)
    CL_INNER_H,         // 空白の高さ (半径)
    CL_INNER_SHAPE,     // 空白の形: 楕円 / レイヤーの形
    CL_SHAPE_LAYER,     // 空白の形に使うレイヤー（キャラクターなど）
    CL_SHAPE_MARGIN,    // 形からの余白 (px)
    CL_SHAPE_SMOOTH,    // 形の凹凸をならす量
    CL_ANGLE,           // 角度
    CL_COUNT,           // 本数 / 密度
    CL_WIDTH,           // 線の太さ
    CL_WIDTH_JITTER,    // 太さのばらつき
    CL_SPACING_JITTER,  // 間隔のばらつき
    CL_START_JITTER,    // 始点のばらつき
    CL_TAPER,           // 入り抜きの長さ
    CL_SHARPNESS,       // 先端の鋭さ
    CL_SOFTNESS,        // 輪郭のぼかし
    CL_WOBBLE,          // 手描き風のがたつき (px)
    CL_WOBBLE_SIZE,     // がたつきの波の長さ (px)
    CL_UNEVENNESS,      // 太さのむら
    CL_LENGTH,          // 線の長さ
    CL_LENGTH_JITTER,   // 長さのばらつき
    CL_FEATHER,         // 空白の境界のぼかし (流線)
    CL_COLOR,           // 線の色
    CL_OPACITY,         // 不透明度
    CL_SEED,            // ランダムシード
    CL_COMPOSITE,       // 合成方法
    CL_ANIM_TOPIC,      // ─ アニメーション（グループ開始）
    CL_FLICKER_RATE,    // ちらつき (回/秒)
    CL_ROTATION_SPEED,  // 回転速度 (度/秒)
    CL_PULSE_AMOUNT,    // 脈動の大きさ
    CL_PULSE_RATE,      // 脈動の速さ (回/秒)
    CL_FLOW_SPEED,      // 流れる速さ (px/秒)
    CL_ANIM_TOPIC_END,  // ─ グループ終了
    CL_NUM_PARAMS
};

// UI 上で ID を固定しておく（並べ替えてもプロジェクト互換が崩れないように）
enum {
    TYPE_DISK_ID = 1,
    CENTER_DISK_ID,
    INNER_W_DISK_ID,
    INNER_H_DISK_ID,
    ANGLE_DISK_ID,
    COUNT_DISK_ID,
    WIDTH_DISK_ID,
    WIDTH_JITTER_DISK_ID,
    SPACING_JITTER_DISK_ID,
    START_JITTER_DISK_ID,
    TAPER_DISK_ID,
    LENGTH_DISK_ID,
    LENGTH_JITTER_DISK_ID,
    FEATHER_DISK_ID,
    COLOR_DISK_ID,
    OPACITY_DISK_ID,
    SEED_DISK_ID,
    COMPOSITE_DISK_ID,
    SHARPNESS_DISK_ID,
    SOFTNESS_DISK_ID,
    ANIM_TOPIC_DISK_ID,
    FLICKER_RATE_DISK_ID,
    ROTATION_SPEED_DISK_ID,
    PULSE_AMOUNT_DISK_ID,
    PULSE_RATE_DISK_ID,
    FLOW_SPEED_DISK_ID,
    ANIM_TOPIC_END_DISK_ID,
    FOLLOW_MODE_DISK_ID,
    FOLLOW_LAYER_DISK_ID,
    FOLLOW_OFFSET_X_DISK_ID,
    FOLLOW_OFFSET_Y_DISK_ID,
    INNER_SHAPE_DISK_ID,
    SHAPE_LAYER_DISK_ID,
    SHAPE_MARGIN_DISK_ID,
    SHAPE_SMOOTH_DISK_ID,
    WOBBLE_DISK_ID,
    WOBBLE_SIZE_DISK_ID,
    UNEVENNESS_DISK_ID
};

enum {
    INNER_SHAPE_ELLIPSE = 0,
    INNER_SHAPE_LAYER       // 指定レイヤーの不透明部分の形
};

enum {
    FOLLOW_OFF = 0,
    FOLLOW_LAYER_POSITION,  // レイヤーのアンカーポイント（ヌルやトラッキング結果に）
    FOLLOW_LAYER_ALPHA      // レイヤーの不透明部分の重心（切り抜いたキャラクターに）
};

extern "C" {
DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
                            PF_ParamDef* params[], PF_LayerDef* output, void* extra);
}
