// GrowFill.h — After Effects エフェクト「Grow Fill（塗り広がり）」
#pragma once

#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "Smart_Utils.h"
#include "AEGP_SuiteHandler.h"
#include "AEFX_SuiteHelper.h"

#ifdef AE_OS_WIN
#include <Windows.h>
#endif

#define NAME        "Grow Fill"
#define MATCH_NAME  "KZ GrowFill"
#define CATEGORY    "Manga FX"
#define DESCRIPTION "Fills areas enclosed by line art,\ngrowing from an origin point."

#define MAJOR_VERSION 1
#define MINOR_VERSION 0
#define BUG_VERSION   0
#define STAGE_VERSION PF_Stage_DEVELOP
#define BUILD_VERSION 1
// PiPL の AE_Effect_Version と一致させること（PF_VERSION(1,0,0,DEVELOP,1) = 524289）

// パラメータの並び（0 は入力レイヤー）
enum {
    GF_INPUT = 0,
    GF_DETECT,          // 線の検出方法
    GF_THRESHOLD,       // 線のしきい値 (%)
    GF_CLOSE_GAPS,      // 線の切れ目を閉じる (px)
    GF_TARGET,          // 塗る領域
    GF_OUTSIDE,         // 外側も塗る
    GF_MIN_AREA,        // 最小の面積 (px²)
    GF_ORIGIN,          // 起点
    GF_PROGRESS,        // 進み具合 (%)
    GF_STAGGER,         // 領域ごとの時間差 (%)
    GF_SOFTNESS,        // 前線のぼかし (px)
    GF_WOBBLE,          // 前線の揺れ (px)
    GF_WOBBLE_SIZE,     // 揺れの大きさ (px)
    GF_EVOLUTION,       // 揺れの変化
    GF_UNDER_LINES,     // 線の下に広げる (px)
    GF_COLOR,           // 塗りの色
    GF_OPACITY,         // 不透明度 (%)
    GF_OUTPUT,          // 出力
    GF_NUM_PARAMS
};

// ディスクID（保存データとの対応。並びを変えても値は変えないこと）
enum {
    DETECT_DISK_ID = 1,
    THRESHOLD_DISK_ID,
    CLOSE_GAPS_DISK_ID,
    TARGET_DISK_ID,
    OUTSIDE_DISK_ID,
    MIN_AREA_DISK_ID,
    ORIGIN_DISK_ID,
    PROGRESS_DISK_ID,
    STAGGER_DISK_ID,
    SOFTNESS_DISK_ID,
    WOBBLE_DISK_ID,
    WOBBLE_SIZE_DISK_ID,
    EVOLUTION_DISK_ID,
    UNDER_LINES_DISK_ID,
    COLOR_DISK_ID,
    OPACITY_DISK_ID,
    OUTPUT_DISK_ID,
};

extern "C" {
DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
                            PF_ParamDef* params[], PF_LayerDef* output, void* extra);
}
