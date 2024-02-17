#ifndef _SIGMA_MODEL_H
#define _SIGMA_MODEL_H

#include "mi_ipu.h"

#ifdef  __cplusplus
extern "C"{
#endif

MI_S32 sigmaLoadModel(MI_IPU_CHN *ptChnId, MI_IPUChnAttr_t *pstIPUChnAttr, const char* filename);

#ifdef __cplusplus
}
#endif

#endif