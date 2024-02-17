#ifndef _DIVP_APP_H_
#define _DIVP_APP_H_
#include "mi_divp_datatype.h"
#ifdef __cplusplus
extern "C" {
#endif

#define Mixer_API_ISVALID_POINT(X)  \
    {   \
        if( X == NULL)  \
        {   \
            printf("mixer input point param is null!\n");  \
            return MI_SUCCESS;   \
        }   \
	}

typedef struct Divp_Sys_BindInfo_s
{
    MI_SYS_ChnPort_t stSrcChnPort;
    MI_SYS_ChnPort_t stDstChnPort;
    MI_U32 u32SrcFrmrate;
    MI_U32 u32DstFrmrate;
    MI_SYS_BindType_e eBindType;
    MI_U32 u32BindParam;
} Divp_Sys_BindInfo_T;

MI_S32 Divp_CreatChannel(int, MI_SYS_Rotate_e, MI_SYS_WindowRect_s*);
MI_S32 Divp_SetOutputAttr(int, MI_DIVP_OutputPortAttr_s*);
MI_S32 Divp_StartChn(int chn);
MI_S32 Divp_StopChn(MI_DIVP_CHN DivpChn);
MI_S32 Divp_DestroyChn(MI_DIVP_CHN DivpChn);
MI_S32 Divp_Sys_Bind(Divp_Sys_BindInfo_T *pstBindInfo);
MI_S32 Divp_Sys_UnBind(Divp_Sys_BindInfo_T *pstBindInfo);
#ifdef __cplusplus
}
#endif

#endif