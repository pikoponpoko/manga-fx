// FlowLoop.h — After Effects エフェクト「Flow Loop（流れるループ）」
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

#define NAME        "Flow Loop"
#define MATCH_NAME  "KZ FlowLoop"
#define CATEGORY    "Manga FX"
#define DESCRIPTION "Makes a still image flow along drawn guides\nas a seamless loop."

#define MAJOR_VERSION 1
#define MINOR_VERSION 0
#define BUG_VERSION   0
#define STAGE_VERSION PF_Stage_DEVELOP
#define BUILD_VERSION 1
// PiPL の AE_Effect_Version と一致させること（PF_VERSION(1,0,0,DEVELOP,1) = 524289）

// パラメータの並び（0 は入力レイヤー）
enum {
    FL_INPUT = 0,
    FL_SOURCE,          // 流れの決め方
    FL_LOOP,            // 1 ループの秒数
    FL_SPEED,           // 1 ループで進む距離 (px)
    FL_PHASE,           // ループの開始位置 (%)
    FL_DIRECTION,       // Direction の向き
    FL_CENTER,          // Radial / Swirl の中心
    FL_INFLUENCE,       // マスクのガイドの届く距離 (px)
    FL_MAP_LAYER,       // Flow Map のレイヤー
    FL_MAP_SMOOTH,      // Flow Map をならす (px)
    FL_AREA_LAYER,      // 流れる範囲のレイヤー
    FL_AREA_FEATHER,    // 範囲の境目 (px)
    FL_PHASE_NOISE,     // タイミングのばらつき (%)
    FL_NOISE_SCALE,     // ばらつきの模様の大きさ (px)
    FL_WRAP,            // 端を反対側とつなぐ
    FL_NUM_PARAMS
};

// ディスクID（保存データとの対応。並びを変えても値は変えないこと）
enum {
    SOURCE_DISK_ID = 1,
    LOOP_DISK_ID,
    SPEED_DISK_ID,
    PHASE_DISK_ID,
    DIRECTION_DISK_ID,
    CENTER_DISK_ID,
    INFLUENCE_DISK_ID,
    MAP_LAYER_DISK_ID,
    MAP_SMOOTH_DISK_ID,
    AREA_LAYER_DISK_ID,
    AREA_FEATHER_DISK_ID,
    PHASE_NOISE_DISK_ID,
    NOISE_SCALE_DISK_ID,
    WRAP_DISK_ID,
};

extern "C" {
DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
                            PF_ParamDef* params[], PF_LayerDef* output, void* extra);
}
