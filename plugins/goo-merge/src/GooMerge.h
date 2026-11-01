// GooMerge.h — After Effects エフェクト「Goo Merge（くっつき合体）」
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

#define NAME        "Goo Merge"
#define MATCH_NAME  "KZ GooMerge"
#define CATEGORY    "Manga FX"
#define DESCRIPTION "Joins nearby shapes with gooey bridges\nand rounds their corners."

#define MAJOR_VERSION 1
#define MINOR_VERSION 0
#define BUG_VERSION   0
#define STAGE_VERSION PF_Stage_DEVELOP
#define BUILD_VERSION 1
// PiPL の AE_Effect_Version と一致させること（PF_VERSION(1,0,0,DEVELOP,1) = 524289）

// パラメータの並び（0 は入力レイヤー）
enum {
    GM_INPUT = 0,
    GM_RADIUS,          // くっつく半径 (px)
    GM_ROUND,           // 出っ張った角の丸み (px)
    GM_STYLE,           // Solid / Bubble
    GM_COLOR_MODE,      // レイヤーの色 / 塗りの色
    GM_FILL,            // 塗りの色
    GM_BUBBLE_START,    // グループ開始
    GM_BUBBLE_OPACITY,  // 泡の中の不透明度 (%)
    GM_RIM,             // 縁の太さ (px)
    GM_HIGHLIGHT,       // 光の映り込み (%)
    GM_LIGHT,           // 光の向き
    GM_BUBBLE_END,      // グループ終了
    GM_NUM_PARAMS
};

// ディスクID（保存データとの対応。並びを変えても値は変えないこと）
enum {
    RADIUS_DISK_ID = 1,
    ROUND_DISK_ID,
    STYLE_DISK_ID,
    COLOR_MODE_DISK_ID,
    FILL_DISK_ID,
    BUBBLE_START_DISK_ID,
    BUBBLE_OPACITY_DISK_ID,
    RIM_DISK_ID,
    HIGHLIGHT_DISK_ID,
    LIGHT_DISK_ID,
    BUBBLE_END_DISK_ID,
};

extern "C" {
DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
                            PF_ParamDef* params[], PF_LayerDef* output, void* extra);
}
