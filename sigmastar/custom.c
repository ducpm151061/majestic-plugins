#include <mi_common.h>
#include <mi_isp_iq.h>
#include <mi_sys.h>
#include <plugin.h>

static void set_brightness(const char *value) {
	MI_ISP_IQ_BRIGHTNESS_TYPE_t brightness;
	if (MI_ISP_IQ_GetBrightness(0, &brightness)) {
		RETURN("MI_ISP_IQ_GetBrightness failed");
	}

	if (!strlen(value)) {
		RETURN("Get brightness: %d", brightness.stManual.stParaAPI.u32Lev);
	}

	int index = atoi(value);
	brightness.bEnable = SS_TRUE;
	brightness.enOpType = SS_OP_TYP_MANUAL;
	brightness.stManual.stParaAPI.u32Lev = index;

	if (MI_ISP_IQ_SetBrightness(0, &brightness)) {
		RETURN("MI_ISP_IQ_SetBrightness failed");
	}

	RETURN("Set brightness: %d", index);
}

static void set_contrast(const char *value) {
	MI_ISP_IQ_CONTRAST_TYPE_t contrast;
	if (MI_ISP_IQ_GetContrast(0, &contrast)) {
		RETURN("MI_ISP_IQ_GetContrast failed");
	}

	if (!strlen(value)) {
		RETURN("Get contrast: %d", contrast.stManual.stParaAPI.u32Lev);
	}

	int index = atoi(value);
	contrast.bEnable = SS_TRUE;
	contrast.enOpType = SS_OP_TYP_MANUAL;
	contrast.stManual.stParaAPI.u32Lev = index;

	if (MI_ISP_IQ_SetContrast(0, &contrast)) {
		RETURN("MI_ISP_IQ_SetContrast failed");
	}

	RETURN("Set contrast: %d", index);
}

static void get_version() {
	MI_SYS_Version_t version;
	if (MI_SYS_GetVersion(&version)) {
		RETURN("MI_SYS_GetVersion failed");
	}

	RETURN("%s", version.u8Version);
}

static table custom[] = {
	{ "brightness", &set_brightness },
	{ "contrast", &set_contrast },
	{ "version", &get_version },
	{ "motion", &call_motion },
	{ "setup", &call_setup },
	{ "help", &get_usage },
};

config common = {
	.list = custom,
	.size = sizeof(custom) / sizeof(table),
};
