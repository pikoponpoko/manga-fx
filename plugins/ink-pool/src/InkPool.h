// InkPool.h — After Effects エフェクト「Ink Pool（墨溜まり）」
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

#define NAME        "Ink Pool"
#define MATCH_NAME  "KZ InkPool"
#define CATEGORY    "Manga FX"
#define DESCRIPTION "Ink pooling (墨溜まり).\nMelts line art where strokes cross or come close."

#define MAJOR_VERSION 1
#define MINOR_VERSION 0
#define BUG_VERSION   0
#define STAGE_VERSION PF_Stage_DEVELOP
#define BUILD_VERSION 1
// PiPL の AE_Effect_Version と一致させること（PF_VERSION(1,0,0,DEVELOP,1) = 524289）

// パラメータの並び（0 は入力レイヤー）
enum {
    IP_INPUT = 0,
    IP_DETECT,          // 線の検出方法
    IP_THRESHOLD,       // 線のしきい値 (%)
    IP_SIZE,            // 溶ける範囲 (px)
    IP_STRENGTH,        // 溶け具合 (%)
    IP_SPREAD,          // 線から広がる上限 (%)
    IP_JUNCTION,        // 分かれ目の溜まり (%)
    IP_KEEP_GAPS,       // すき間の維持 (%)
    IP_SMOOTH,          // なめらかさ (px)
    IP_END_TAPER,       // 線の端の細め (%)
    IP_COLOR_MODE,      // 溶けた部分の色
    IP_COLOR,           // 指定色
    IP_OUTPUT,          // 出力
    IP_NUM_PARAMS
};

// ディスクID（保存データとの対応。並びを変えても値は変えないこと）
enum {
    DETECT_DISK_ID = 1,
    THRESHOLD_DISK_ID,
    SIZE_DISK_ID,
    STRENGTH_DISK_ID,
    SPREAD_DISK_ID,
    SMOOTH_DISK_ID,
    END_TAPER_DISK_ID,
    COLOR_MODE_DISK_ID,
    COLOR_DISK_ID,
    OUTPUT_DISK_ID,
    KEEP_GAPS_DISK_ID,
    JUNCTION_DISK_ID,
};

extern "C" {
DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
                            PF_ParamDef* params[], PF_LayerDef* output, void* extra);
}
