// GooMergePiPL.r — エフェクトの登録情報 (PiPL)
// 値は GooMerge.h / GlobalSetup と一致させること。
#include "AEConfig.h"
#include "AE_EffectVers.h"

#ifndef AE_OS_WIN
	#include "AE_General.r"
#endif

resource 'PiPL' (16000) {
	{
		Kind {
			AEEffect
		},
		Name {
			"Goo Merge"
		},
		Category {
			"Manga FX"
		},
#ifdef AE_OS_WIN
	#ifdef AE_PROC_INTELx64
		CodeWin64X86 {"EffectMain"},
	#endif
#else
	#ifdef AE_OS_MAC
		CodeMacIntel64 {"EffectMain"},
		CodeMacARM64 {"EffectMain"},
	#endif
#endif
		AE_PiPL_Version {
			2,
			0
		},
		AE_Effect_Spec_Version {
			PF_PLUG_IN_VERSION,
			PF_PLUG_IN_SUBVERS
		},
		AE_Effect_Version {
			524289	/* 1.0 = PF_VERSION(1,0,0,PF_Stage_DEVELOP,1) */
		},
		AE_Effect_Info_Flags {
			0
		},
		AE_Effect_Global_OutFlags {
			0x2000200	/* DEEP_COLOR_AWARE | I_EXPAND_BUFFER */
		},
		AE_Effect_Global_OutFlags_2 {
			0x8001400	/* SUPPORTS_SMART_RENDER | FLOAT_COLOR_AWARE | SUPPORTS_THREADED_RENDERING */
		},
		AE_Effect_Match_Name {
			"KZ GooMerge"
		},
		AE_Reserved_Info {
			8
		},
		AE_Effect_Support_URL {
			"https://claude.ai"
		}
	}
};
