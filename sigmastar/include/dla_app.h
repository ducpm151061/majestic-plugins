#ifndef _DLA_APP_H_
#define _DLA_APP_H_

#include "dla_base.h"

#ifdef __cplusplus
extern "C" {
#endif

void IPURunCommand(MI_U32 u32CmdCode);
void IPUInitAndStart(IPU_DlaInfo_S stDlaInfo);
void IPUUnInitAndStop(void);

#ifdef __cplusplus
}

#endif
#endif
