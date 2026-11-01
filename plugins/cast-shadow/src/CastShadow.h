// CastShadow.h — After Effects エフェクト「Cast Shadow（落ち影）」
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

#define NAME        "Cast Shadow"
#define MATCH_NAME  "KZ CastShadow"
#define CATEGORY    "Manga FX"
#define DESCRIPTION "Casts a long shadow from the layer's shape,\naway from a point or directional light."

#define MAJOR_VERSION 1
#define MINOR_VERSION 0
#define BUG_VERSION   0
#define STAGE_VERSION PF_Stage_DEVELOP
#define BUILD_VERSION 1
// PiPL の AE_Effect_Version と一致させること（PF_VERSION(1,0,0,DEVELOP,1) = 524289）

// パラメータの並び（0 は入力レイヤー）
enum {
    CS_INPUT = 0,
    CS_LIGHT_TYPE,      // 光源の種類
    CS_LIGHT_POS,       // 光源の位置（Point）
    CS_DIRECTION,       // 影の向き（Directional）
    CS_LENGTH,          // 影の長さ (px)
    CS_START,           // 形から離す距離 (px)
    CS_FADE,            // 先端で薄くなる量 (%)
    CS_SOFTNESS,        // 先端でのぼかし (px)
    CS_COLOR,           // 影の色
    CS_OPACITY,         // 不透明度 (%)
    CS_OUTPUT,          // 出力
    CS_NUM_PARAMS
};

// ディスクID（保存データとの対応。並びを変えても値は変えないこと）
enum {
    LIGHT_TYPE_DISK_ID = 1,
    LIGHT_POS_DISK_ID,
    DIRECTION_DISK_ID,
    LENGTH_DISK_ID,
    START_DISK_ID,
    FADE_DISK_ID,
    SOFTNESS_DISK_ID,
    COLOR_DISK_ID,
    OPACITY_DISK_ID,
    OUTPUT_DISK_ID,
};

extern "C" {
DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
                            PF_ParamDef* params[], PF_LayerDef* output, void* extra);
}
