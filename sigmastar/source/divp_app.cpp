#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <poll.h>
#include <fcntl.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <iostream>
#include <sys/time.h>

#include "mi_sys.h"
#include "common.h"
#include "mi_divp.h"
#include "divp_app.h"
using namespace std;

void ResizebyDivp(MI_PHY pSrc, MI_PHY pdest ,MI_SYS_PixelFormat_e pixlFormat,MI_S32 s32SrcWidth,MI_S32 s32SrcHeight,MI_S32 s32destWidth,MI_S32 s32destHeight)
{
    MI_DIVP_DirectBuf_t stSrcBuf;
    MI_DIVP_DirectBuf_t stDstBuf;
    MI_SYS_WindowRect_t stSrcCrop;

    stSrcBuf.ePixelFormat = pixlFormat;
    stSrcBuf.u32Width = ALIGN_DOWN(s32SrcWidth,2);
    stSrcBuf.u32Height = ALIGN_DOWN(s32SrcHeight,2);
    stSrcBuf.phyAddr[0] = pSrc;

    stDstBuf.ePixelFormat = pixlFormat;
    stDstBuf.u32Width = s32destWidth;
    stDstBuf.u32Height = s32destHeight;
    stDstBuf.phyAddr[0] = pdest;

    stSrcCrop.u16X = 0;
    stSrcCrop.u16Y = 0;
    stSrcCrop.u16Width =  ALIGN_DOWN(s32SrcWidth,2);
    stSrcCrop.u16Height = ALIGN_DOWN(s32SrcHeight,2);

    if(pixlFormat==E_MI_SYS_PIXEL_FRAME_ARGB8888)
    {
        stSrcBuf.u32Stride[0] = ALIGN_UP(s32SrcWidth, 16)*4;
        stDstBuf.u32Stride[0] = ALIGN_UP(s32destWidth,16)*4;
    }

    if(MI_SUCCESS != MI_DIVP_StretchBuf(&stSrcBuf, &stSrcCrop, &stDstBuf))
    {
        MIXER_ERR("MI_DIVP_StretchBuf failed\n");
    }
}

MI_S32 Divp_CreatChannel(MI_DIVP_CHN DivpChn, MI_SYS_Rotate_e eRoate, MI_SYS_WindowRect_t *pstCropWin)
{
    MI_DIVP_ChnAttr_t stDivpChnAttr;

    MIXER_DBG("divp channel id:%d\n", DivpChn);
    memset(&stDivpChnAttr, 0x00, sizeof(MI_DIVP_ChnAttr_t));

    stDivpChnAttr.bHorMirror = FALSE;
    stDivpChnAttr.bVerMirror = FALSE;
    stDivpChnAttr.eDiType = E_MI_DIVP_DI_TYPE_OFF;
    stDivpChnAttr.eRotateType = eRoate;
    stDivpChnAttr.eTnrLevel = E_MI_DIVP_TNR_LEVEL_OFF;
    stDivpChnAttr.stCropRect.u16X = pstCropWin->u16X;
    stDivpChnAttr.stCropRect.u16Y = pstCropWin->u16Y;
    stDivpChnAttr.stCropRect.u16Width = pstCropWin->u16Width;
    stDivpChnAttr.stCropRect.u16Height = pstCropWin->u16Height;
    stDivpChnAttr.u32MaxWidth = pstCropWin->u16Width;
    stDivpChnAttr.u32MaxHeight = pstCropWin->u16Height;
    ExecFunc(MI_DIVP_CreateChn(DivpChn, &stDivpChnAttr), MI_SUCCESS);

    return MI_SUCCESS;
}

MI_S32 Divp_SetOutputAttr(MI_DIVP_CHN DivpChn, MI_DIVP_OutputPortAttr_t *stDivpPortAttr)
{
    if(NULL == stDivpPortAttr)
    {
        MIXER_ERR("param err\n");
        return -1;
    }
    ExecFunc(MI_DIVP_SetOutputPortAttr(DivpChn, stDivpPortAttr), MI_SUCCESS);
    return MI_SUCCESS;
}

MI_S32 Divp_GetOutputAttr(MI_DIVP_CHN DivpChn, MI_DIVP_OutputPortAttr_t *pstDivpPortAttr)
{
    Mixer_API_ISVALID_POINT(pstDivpPortAttr);
    ExecFunc(MI_DIVP_GetOutputPortAttr(DivpChn, pstDivpPortAttr), MI_SUCCESS);
    return MI_SUCCESS;
}

MI_S32 Divp_GetChnAttr(MI_DIVP_CHN DivpChn, MI_DIVP_ChnAttr_t *pstDivpChnAttr)
{
    Mixer_API_ISVALID_POINT(pstDivpChnAttr);
    ExecFunc(MI_DIVP_GetChnAttr(DivpChn, pstDivpChnAttr), MI_SUCCESS);
    return MI_SUCCESS;
}

MI_S32 Divp_SetChnAttr(MI_DIVP_CHN DivpChn, MI_DIVP_ChnAttr_t stDivpChnAttr)
{
    ExecFunc(MI_DIVP_SetChnAttr(DivpChn, &stDivpChnAttr), MI_SUCCESS);
    return MI_SUCCESS;
}

MI_S32 Divp_StartChn(MI_DIVP_CHN DivpChn)
{
    ExecFunc(MI_DIVP_StartChn(DivpChn), MI_SUCCESS);
    return MI_SUCCESS;
}

MI_S32 Divp_StopChn(MI_DIVP_CHN DivpChn)
{
    ExecFunc(MI_DIVP_StopChn(DivpChn), MI_SUCCESS);
    return MI_SUCCESS;
}

MI_S32 Divp_DestroyChn(MI_DIVP_CHN DivpChn)
{
    ExecFunc(MI_DIVP_DestroyChn(DivpChn), MI_SUCCESS);
    return MI_SUCCESS;
}

MI_S32 Divp_Sys_Bind(Divp_Sys_BindInfo_T *pstBindInfo)
{
    printf("%s:%d bind src: (%d,%d,%d,%d), dst:(%d,%d,%d,%d), SrcFps=%d DstFps=%d, BindType=%d BindParam=%d\n", __func__,__LINE__,
                       pstBindInfo->stSrcChnPort.eModId, pstBindInfo->stSrcChnPort.u32DevId,
                       pstBindInfo->stSrcChnPort.u32ChnId, pstBindInfo->stSrcChnPort.u32PortId,
                       pstBindInfo->stDstChnPort.eModId, pstBindInfo->stDstChnPort.u32DevId,
                       pstBindInfo->stDstChnPort.u32ChnId, pstBindInfo->stDstChnPort.u32PortId,
                       pstBindInfo->u32SrcFrmrate, pstBindInfo->u32DstFrmrate,
                       pstBindInfo->eBindType, pstBindInfo->u32BindParam);

    ExecFunc(MI_SYS_BindChnPort2(&pstBindInfo->stSrcChnPort, &pstBindInfo->stDstChnPort, \
                                 pstBindInfo->u32SrcFrmrate, pstBindInfo->u32DstFrmrate, \
                                 pstBindInfo->eBindType, pstBindInfo->u32BindParam),     \
                                 MI_SUCCESS);
    return MI_SUCCESS;
}

MI_S32 Divp_Sys_UnBind(Divp_Sys_BindInfo_T *pstBindInfo)
{
    printf("%s:%d unbind src(%d,%d,%d,%d), dst(%d,%d,%d,%d), SrcFps=%d, DstFps=%d\n", __func__, __LINE__,
                           pstBindInfo->stSrcChnPort.eModId, pstBindInfo->stSrcChnPort.u32DevId,
                           pstBindInfo->stSrcChnPort.u32ChnId, pstBindInfo->stSrcChnPort.u32PortId,
                           pstBindInfo->stDstChnPort.eModId, pstBindInfo->stDstChnPort.u32DevId,
                           pstBindInfo->stDstChnPort.u32ChnId, pstBindInfo->stDstChnPort.u32PortId,
                           pstBindInfo->u32SrcFrmrate, pstBindInfo->u32DstFrmrate);

    ExecFunc(MI_SYS_UnBindChnPort(&pstBindInfo->stSrcChnPort, &pstBindInfo->stDstChnPort), MI_SUCCESS);

    return MI_SUCCESS;
}

