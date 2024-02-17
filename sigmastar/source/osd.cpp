#include <math.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>

#include "mi_common_datatype.h"
#include "mi_sys_datatype.h"
#include "mi_vpe_datatype.h"
#include "mi_vpe.h"
#include "thread_api.h"
#include "dla_base.h"
#include "osd.h"

MI_U16  g_s32VideoStreamNum = 2;
MI_BOOL g_bOsdTaskExit = FALSE;
pthread_t g_stPthreadOsd = -1;

MI_U8 *g_u8DrawTextBuffer = NULL;
MI_RGN_HANDLE g_hOsdIpuHandle = 0;

pthread_mutex_t g_stMutexListLock;
pthread_mutex_t g_stMutexOsdDraw;
pthread_mutex_t g_stMutexOsdRun[MAX_VIDEO_NUMBER];
struct OsdTextWidgetOrder_t g_stOsdTextWidgetOrder;

pthread_mutex_t g_stMutexOsdUptState;
pthread_cond_t  g_condOsdUpadteState;

struct list_head g_WorkRectList;
struct list_head g_EmptyRectList;

MI_U32 g_u32RectCount = 0;
RectList_t g_stRectList[MAX_RECT_LIST_NUMBER];
MI_SYS_WindowRect_t g_stDlaRect[MAX_DLA_RECT_NUMBER];
MI_SYS_WindowRect_t g_stNameRect[MAX_DLA_RECT_NUMBER];
MI_SYS_WindowRect_t g_stCleanRect[MAX_DLA_RECT_NUMBER];

static MI_RGN_PaletteTable_t g_stPaletteTable = {
    { //index0 ~ index15
    {  0,   0,   0,   0}, {255, 255,  0,  0},  {255,  0,  255,   0}, {255,  0,   0, 255},
    {255, 255,  255,  0}, {255,  0, 112, 255}, {255,  0,  255, 255}, {255, 255, 255, 255},
    {255, 128,   0,   0}, {255, 128, 128, 0},  {255, 128,   0, 128}, {255,   0, 128,  0},
    {255,  0,    0, 128}, {255,  0, 128, 128}, {255, 128,  128, 128}, {255, 64,  64, 64}}
};

static Color_t g_stBlackColor     = {(MI_U8)255, (MI_U8)0,   (MI_U8)0,  (MI_U8)0};
static Color_t g_stRedColor       = {(MI_U8)255, (MI_U8)255, (MI_U8)0,   (MI_U8)0};
static Color_t g_stGreenColor     = {(MI_U8)255, (MI_U8)0,   (MI_U8)128, (MI_U8)0};
static Color_t g_stYellowColor    = {(MI_U8)255, (MI_U8)255, (MI_U8)255, (MI_U8)0};
static MI_BOOL g_bOsdColorInverse = FALSE;

MI_U32 OsdGetTextColor(char* name)
{
    MI_U32 u32Color = 3;

    if( strncmp(name, "cat", 3) == 0
      ||strncmp(name, "dog", 3) == 0
      ||strncmp(name, "bicycle", 7) == 0
      ||strncmp(name, "motorbike", 9) == 0)
    {
        u32Color = 1;
    }
    else if(strncmp(name, "person", 6) == 0)
    {
        u32Color = 2;
    }
    else if(strncmp(name, "car", 3) == 0
      ||strncmp(name, "bus", 3) == 0)
    {
        u32Color = 3;
    }
    
    return u32Color;
}

MI_S32 OsdGetDivNumber(MI_S32 value)
{
    if(0 == value % 32)       return value / 32;
    else if(0 == value % 30)  return value / 30;
    else if(0 == value % 28)  return value / 28;
    else if(0 == value % 27)  return value / 27;
    else if(0 == value % 25)  return value / 25;
    else if(0 == value % 24)  return value / 24;
    else if(0 == value % 22)  return value / 22;
    else if(0 == value % 21)  return value / 21;
    else if(0 == value % 20)  return value / 20;
    else if(0 == value % 18)  return value / 18;
    else if(0 == value % 16)  return value / 16;
    else if(0 == value % 15)  return value / 15;
    else if(0 == value % 14)  return value / 14;
    else if(0 == value % 12)  return value / 12;
    else if(0 == value % 10)  return value / 10;
    else if(0 == value % 9)   return value / 9;
    else if(0 == value % 8)   return value / 8;
    else if(0 == value % 7)   return value / 7;
    else if(0 == value % 6)   return value / 6;
    else if(0 == value % 5)   return value / 5;
    else if(0 == value % 4)   return value / 4;
    else if(0 == value % 3)   return value / 3;
    else if(0 == value % 2)   return value / 2;
    else return value;
}

MI_S32 OsdRgnChnPortConfig(MI_VENC_CHN s32VencChn, MI_RGN_ChnPort_t *pstRgnChnPort)
{
    MI_S32 s32Ret = MI_SUCCESS;
    
    if((s32VencChn < 0) || (s32VencChn >= g_s32VideoStreamNum))
    {
        MIXER_ERR("The input VenChn(%d) is out of range!\n", s32VencChn);
        s32Ret = E_MI_ERR_FAILED;
        return s32Ret;
    }

    if(pstRgnChnPort == NULL)
    {
        MIXER_ERR("The input Mixer Rgn chnPort struct pointer is NULL!\n");
        s32Ret = E_MI_ERR_NULL_PTR;
        return s32Ret;
    }

    if(s32VencChn < g_s32VideoStreamNum)
    {
        pstRgnChnPort->eModId   = E_MI_RGN_MODID_VPE;
        pstRgnChnPort->s32DevId = 0;
        pstRgnChnPort->s32ChnId = 0;
        pstRgnChnPort->s32OutputPortId = 0;
    }

    return s32Ret;
}

void OsdDrawTextToCanvas(MI_RGN_CanvasInfo_t* pstCanvasInfo,TextWidgetAttr_t* pstTextWidgetAttr, ImageData_t stImageData)
{
    MI_U32 u32Idx = 0;
    MI_U32 u32Width = 0;
    MI_U32 u32Height = 0;
    Point_t stPoint;
    MI_U8 *u8SrcBuf;
    MI_U8 *u8DstBuf = NULL;
        
    stPoint.x = ALIGN_DOWN(pstTextWidgetAttr->pPoint->x, 2);
    stPoint.y = ALIGN_DOWN(pstTextWidgetAttr->pPoint->y, 2);
    
    u8SrcBuf = stImageData.buffer;
    u32Width  = MIN(pstCanvasInfo->stSize.u32Width-stPoint.x, stImageData.width);
    u32Height = MIN(pstCanvasInfo->stSize.u32Height-stPoint.y, stImageData.height);

    switch(stImageData.pmt)
    {
        case E_MI_RGN_PIXEL_FORMAT_ARGB1555:
            u8DstBuf = (MI_U8*)((pstCanvasInfo->virtAddr) + pstCanvasInfo->u32Stride * stPoint.y + stPoint.x*2);
            break;
        case E_MI_RGN_PIXEL_FORMAT_I4:
            u8DstBuf = (MI_U8*)(pstCanvasInfo->virtAddr + pstCanvasInfo->u32Stride * stPoint.y + stPoint.x/2);
            break;
        case E_MI_RGN_PIXEL_FORMAT_I2:
            u8DstBuf = (MI_U8*)(pstCanvasInfo->virtAddr + pstCanvasInfo->u32Stride * stPoint.y + stPoint.x/4);
            break;
        default:
             MIXER_ERR("config Text widget pixel format: %d\n", stImageData.pmt);
             return;
    }

    for(u32Idx = 0; u32Idx < u32Height; u32Idx++)
    {
        memcpy(u8DstBuf, u8SrcBuf, u32Width);
        u8SrcBuf += u32Width;
        u8DstBuf += pstCanvasInfo->u32Stride;
    }
}

MI_S32 OsdUpdateTextWidget(MI_RGN_HANDLE hHandle, MI_RGN_CanvasInfo_t *pstRgnCanvasInfo, 
                                TextWidgetAttr_t* pstTextWidgetAttr, MI_SYS_WindowRect_t* pstRect)
{
    #define MAX_STRING_LEN 1024   
    MI_U8 u8Char[MAX_STRING_LEN];
    static MI_U32 u32BufSize = 0;

    MI_U32 u32StrLen = 0;
    MI_U32 u32FontSize = 0;
    MI_S32 s32Ret = E_MI_ERR_FAILED; 
    MI_BOOL bNeedUptCanvas = FALSE;
    Color_t stfColor;
    ImageData_t stImageData;
    MI_RGN_CanvasInfo_t stCanvasInfo;
    MI_RGN_CanvasInfo_t *pstCanvasInfo = pstRgnCanvasInfo; 
        
    if(((MI_S32)hHandle <= MI_RGN_HANDLE_NULL  || hHandle >= MI_RGN_MAX_HANDLE))
    {
		MIXER_ERR("OSD handle error,hHandle=%d\n", hHandle);
        return E_MI_ERR_FAILED;
    }

    if(pstTextWidgetAttr == NULL
      || pstTextWidgetAttr->string == NULL
      || strlen(pstTextWidgetAttr->string) == 0)
    {
        MIXER_ERR("pstTextWidgetAttr is null or error\n");
        return E_MI_ERR_FAILED;
    }

    pthread_mutex_lock(&g_stMutexOsdDraw);
      
    if(pstCanvasInfo == NULL)
    {
        s32Ret = MI_RGN_GetCanvasInfo(hHandle, &stCanvasInfo);
        if(s32Ret != MI_RGN_OK  )
        {
            MIXER_ERR("MI_RGN_GetCanvasInfo error s32Ret=%d\n", s32Ret);
            pthread_mutex_unlock(&g_stMutexOsdDraw);
            return s32Ret;
        }
        bNeedUptCanvas = TRUE;
        pstCanvasInfo = &stCanvasInfo;
    }
   
    memset(u8Char, 0x00, sizeof(u8Char));
    u32StrLen = strlen(pstTextWidgetAttr->string);
    u32StrLen = u32StrLen < MAX_STRING_LEN?u32StrLen:MAX_STRING_LEN;
    memcpy(u8Char, pstTextWidgetAttr->string, u32StrLen);
    
    u32StrLen = Font_Get_StrLen((MI_S8*)u8Char);
    u32FontSize = Font_Get_Size(pstTextWidgetAttr->size);
    if(u32StrLen == 0 || u32FontSize == 0)
    {
        MIXER_ERR("u32StrLen = %d, u32FontSize = %d error\n", u32StrLen, u32FontSize);
        pthread_mutex_unlock(&g_stMutexOsdDraw);
        return E_MI_ERR_FAILED;
    }

    stImageData.pmt = pstTextWidgetAttr->pmt;
    stImageData.height = u32FontSize; 
    stImageData.width = 0;

    switch(stImageData.pmt)
    {
        case E_MI_RGN_PIXEL_FORMAT_ARGB1555: 
            stImageData.width = u32FontSize*u32StrLen*2;
            break;
        case E_MI_RGN_PIXEL_FORMAT_I2:
        case E_MI_RGN_PIXEL_FORMAT_I4:
            stfColor.a = 0x00;
            stfColor.r = pstTextWidgetAttr->u32Color&0x0F;
            stfColor.g = 0x00;
            stfColor.b = 0x00;
            if(stImageData.pmt == E_MI_RGN_PIXEL_FORMAT_I2)
                stImageData.width  = u32FontSize*u32StrLen/4;
            if(stImageData.pmt == E_MI_RGN_PIXEL_FORMAT_I4)
                stImageData.width = u32FontSize*u32StrLen/2;
            break;
        default:
           MIXER_ERR("config Text widget pixel format: %d\n", stImageData.pmt);
    }

    if((MI_U32)(pstTextWidgetAttr->pPoint->x + stImageData.width) > pstCanvasInfo->stSize.u32Width)
        stImageData.width = pstCanvasInfo->stSize.u32Width - (MI_U32)pstTextWidgetAttr->pPoint->x;
        
    if((MI_U32)(pstTextWidgetAttr->pPoint->y + stImageData.height) > pstCanvasInfo->stSize.u32Height)
        stImageData.height = pstCanvasInfo->stSize.u32Height - (MI_U32)pstTextWidgetAttr->pPoint->y;

    MI_U32 u32ImgSize = stImageData.width * stImageData.height;
    if(g_u8DrawTextBuffer == NULL)
    {
        g_u8DrawTextBuffer = (MI_U8 *)malloc(u32ImgSize*2);
        u32BufSize = u32ImgSize*2;
    }
    else if(u32ImgSize > u32BufSize*2)
    {
        printf("%s:%d[%d,%d,%d]\n", __func__, __LINE__, stImageData.width, stImageData.height, u32BufSize);
        g_u8DrawTextBuffer = (MI_U8 *)realloc(g_u8DrawTextBuffer, u32ImgSize);
        u32BufSize = u32ImgSize;
    }

    if(g_u8DrawTextBuffer == NULL)
    {
        MIXER_ERR("g_u8DrawTextBuffer malloc failed\n");
        pthread_mutex_unlock(&g_stMutexOsdDraw);
        return E_MI_ERR_FAILED;
    }
    
    stImageData.buffer = g_u8DrawTextBuffer;
    memset(stImageData.buffer, 0x00, u32BufSize);

    s32Ret = Font_DrawText(&stImageData, (MI_S8*)u8Char, pstTextWidgetAttr->pPoint->x, pstTextWidgetAttr->size,
                           pstTextWidgetAttr->space, &stfColor, pstTextWidgetAttr->pbColor, pstTextWidgetAttr->bOutline);
    if(s32Ret != MI_SUCCESS)
    {
        MIXER_ERR("mid_Font_DrawText error(0x%X)\n", s32Ret);
        pthread_mutex_unlock(&g_stMutexOsdDraw);
        return s32Ret;
    }

    OsdDrawTextToCanvas(pstCanvasInfo, pstTextWidgetAttr, stImageData);

    if(pstRect)
    {
        pstRect->u16Width = stImageData.width;
        pstRect->u16Height = stImageData.height;
    }

    if(bNeedUptCanvas == TRUE)
    {
        s32Ret = MI_RGN_UpdateCanvas(hHandle);
        if(s32Ret != MI_RGN_OK)
        {
            MIXER_ERR("MI_RGN_UpdateCanvas fail\n");
            pthread_mutex_unlock(&g_stMutexOsdDraw);
            return s32Ret;
        }
    }
    pthread_mutex_unlock(&g_stMutexOsdDraw);
   
    return MI_SUCCESS;
}

MI_S32 OsdUpdateRectWidget(MI_RGN_HANDLE hHandle, MI_RGN_CanvasInfo_t *pstRgnCanvasInfo, RectWidgetAttr_t* pstRectWidgetAttr)
{
    MI_S32 s32Ret = E_MI_ERR_FAILED; 
    MI_BOOL bNeedUptCanvas = FALSE;
    DrawRgnColor_t stColor;
    DrawPoint_t stLeftTopPt;
    DrawPoint_t stRightBotPt;
    MI_SYS_WindowRect_t *stWinRect;
    MI_RGN_CanvasInfo_t stCanvasInfo;
    MI_RGN_CanvasInfo_t *pstCanvasInfo = pstRgnCanvasInfo; 
    
    if(((MI_S32)hHandle <= MI_RGN_HANDLE_NULL  || hHandle >= MI_RGN_MAX_HANDLE))
    {
		MIXER_ERR("OSD handle error,hHandle=%d\n", hHandle);
        return E_MI_ERR_FAILED;
    }

    if(pstRectWidgetAttr == NULL
      || pstRectWidgetAttr->pstRect == NULL
      || pstRectWidgetAttr->s32RectCnt == 0)
    {
        MIXER_ERR("pstRectWidgetAttr is null or error\n");
        return E_MI_ERR_FAILED;
    }

    pthread_mutex_lock(&g_stMutexOsdDraw);
      
    if(pstCanvasInfo == NULL)
    {
        s32Ret = MI_RGN_GetCanvasInfo(hHandle, &stCanvasInfo);
        if(s32Ret != MI_RGN_OK  )
        {
            MIXER_ERR("MI_RGN_GetCanvasInfo error s32Ret=%d\n", s32Ret);
            pthread_mutex_unlock(&g_stMutexOsdDraw);
            return s32Ret;
        }
        bNeedUptCanvas = TRUE;
        pstCanvasInfo = &stCanvasInfo;
    }

    stColor.ePixelFmt = pstCanvasInfo->ePixelFmt;
    
    for(int i = 0; i < pstRectWidgetAttr->s32RectCnt; i++)
    {
        stWinRect = (MI_SYS_WindowRect_t*)((pstRectWidgetAttr->pstRect) + i);
        
        if(!stWinRect->u16Width || !stWinRect->u16Height)
            continue;

        stLeftTopPt.u16X = stWinRect->u16X;
        stLeftTopPt.u16Y = stWinRect->u16Y;
        stRightBotPt.u16X = stWinRect->u16X + stWinRect->u16Width;
        stRightBotPt.u16Y = stWinRect->u16Y + stWinRect->u16Height;

        switch(pstRectWidgetAttr->pmt)
        {
            case E_MI_RGN_PIXEL_FORMAT_ARGB1555:
                stColor.u32Color = RGB2PIXEL1555(pstRectWidgetAttr->pfColor->a, pstRectWidgetAttr->pfColor->r,
                                                 pstRectWidgetAttr->pfColor->g, pstRectWidgetAttr->pfColor->b);
                break;
            case E_MI_RGN_PIXEL_FORMAT_I4:
                stColor.u32Color = pstRectWidgetAttr->u32Color;
                stLeftTopPt.u16X = ALIGN_DOWN(stLeftTopPt.u16X, 2);
                stRightBotPt.u16X = ALIGN_UP(stRightBotPt.u16X, 2);
                break;
            case E_MI_RGN_PIXEL_FORMAT_I2:
                stColor.u32Color = pstRectWidgetAttr->u32Color;
                stLeftTopPt.u16X = ALIGN_DOWN(stLeftTopPt.u16X, 4);
                stRightBotPt.u16X = ALIGN_UP(stRightBotPt.u16X, 4);
                break;
            default:
                MIXER_ERR("OSD only support %s now\n", (pstRectWidgetAttr->pmt==0)?"ARGB1555":(pstRectWidgetAttr->pmt==2)?"I2":"I4");
                break;
        }

        if(pstCanvasInfo->virtAddr)
        {
            DrawRect((void*)pstCanvasInfo->virtAddr, pstCanvasInfo->u32Stride, stLeftTopPt, stRightBotPt, pstRectWidgetAttr->u8BorderWidth, stColor);
        }
    }

    if(bNeedUptCanvas == TRUE)
    {
        s32Ret = MI_RGN_UpdateCanvas(hHandle);
        if(s32Ret != MI_RGN_OK)
        {
            MIXER_ERR("MI_RGN_UpdateCanvas fail\n");
            pthread_mutex_unlock(&g_stMutexOsdDraw);
            return s32Ret;
        }
    }
    pthread_mutex_unlock(&g_stMutexOsdDraw);

   return MI_SUCCESS;
}

MI_S32 OsdCleanRectTextWidget(MI_RGN_HANDLE hHandle, MI_RGN_CanvasInfo_t *pstRgnCanvasInfo, MI_SYS_WindowRect_t *pstWinRect, 
                                    MI_U32 u32RectCount, MI_U32 u32BorderWidth, MI_U32 index, MI_BOOL bIsText )
{
    MI_U8  u8Data = 0;
    MI_U16 u16Div = 1;
    MI_U8  *u8BaseAddr = NULL;
    MI_U16 *u16BaseAddr = NULL;
    MI_S32 s32Ret = E_MI_ERR_FAILED; 
    MI_BOOL bNeedUptCanvas = FALSE;
    MI_S32 u32RectCnt = u32RectCount;
    MI_SYS_WindowRect_t stRect;
    MI_RGN_CanvasInfo_t stCanvasInfo;
    MI_RGN_CanvasInfo_t *pstCanvasInfo = pstRgnCanvasInfo; 
    
    if(((MI_S32)hHandle <= MI_RGN_HANDLE_NULL  || hHandle >= MI_RGN_MAX_HANDLE))
    {
		MIXER_ERR("OSD handle error,hHandle=%d\n", hHandle);
        return E_MI_ERR_FAILED;
    }

    pthread_mutex_lock(&g_stMutexOsdDraw);
     
    if(pstCanvasInfo == NULL)
    {
        s32Ret = MI_RGN_GetCanvasInfo(hHandle, &stCanvasInfo);
        if(s32Ret != MI_RGN_OK  )
        {
            MIXER_ERR("MI_RGN_GetCanvasInfo error s32Ret=%d\n", s32Ret);
            pthread_mutex_unlock(&g_stMutexOsdDraw);
            return s32Ret;
        }
        bNeedUptCanvas = TRUE;
        pstCanvasInfo = &stCanvasInfo;
    }

    if(pstWinRect == NULL || u32RectCount == 0)
        u32RectCnt = 1;

    for(int i = 0; i < u32RectCnt; i++)
    {
        if(pstWinRect == NULL || u32RectCount == 0)
        {
            stRect.u16X = 0;
            stRect.u16Y = 0;
            stRect.u16Width  = pstCanvasInfo->stSize.u32Width;
            stRect.u16Height = pstCanvasInfo->stSize.u32Height;
        }
        else
        {
            stRect.u16X = pstWinRect[i].u16X;
            stRect.u16Y = pstWinRect[i].u16Y;
            stRect.u16Width  = pstWinRect[i].u16Width + u32BorderWidth*2;
            stRect.u16Height = pstWinRect[i].u16Height + u32BorderWidth*2;
        }
        
        if(stRect.u16Width == 0 || stRect.u16Height == 0)
            continue;

        if((stRect.u16X + stRect.u16Width) > pstCanvasInfo->stSize.u32Width)
            stRect.u16Width = pstCanvasInfo->stSize.u32Width - stRect.u16X;
        
        if((stRect.u16Y + stRect.u16Height) > pstCanvasInfo->stSize.u32Height)
            stRect.u16Height = pstCanvasInfo->stSize.u32Height - stRect.u16Y;

        switch(pstCanvasInfo->ePixelFmt)
        {
            case E_MI_RGN_PIXEL_FORMAT_ARGB1555:
                u16BaseAddr = (MI_U16*)(pstCanvasInfo->virtAddr + pstCanvasInfo->u32Stride * stRect.u16Y + stRect.u16X * 2);
                for(int h = 0; h < stRect.u16Height; h++)
                {
                   for(int w = 0; w < stRect.u16Width; w++)
                   {
                       *(u16BaseAddr + w) = index & 0xFFFF;
                   }
                   u16BaseAddr += pstCanvasInfo->u32Stride/2;
                }
                break;
            case E_MI_RGN_PIXEL_FORMAT_I2:
                u16Div = bIsText?1:4;
                stRect.u16X = ALIGN_DOWN(stRect.u16X, 4);
                stRect.u16Width = ALIGN_UP(stRect.u16Width, 4);
                u8Data = ((index & 0x03) << 6)|((index & 0x03) << 4)|((index & 0x03) << 2)|(index & 0x03);
                u8BaseAddr = (MI_U8*)(pstCanvasInfo->virtAddr + pstCanvasInfo->u32Stride * stRect.u16Y + stRect.u16X / 4);
                for(int h = 0; h < stRect.u16Height; h++)
                {
                    memset(u8BaseAddr, u8Data, stRect.u16Width/u16Div);  
                    u8BaseAddr += pstCanvasInfo->u32Stride;
                }
                break;
           case E_MI_RGN_PIXEL_FORMAT_I4:
                u16Div = bIsText?1:2;
                stRect.u16X = ALIGN_DOWN(stRect.u16X, 2);
                stRect.u16Width = ALIGN_UP(stRect.u16Width, 2);
                u8Data = ((index & 0x0F) << 4)|(index & 0x0F);
                u8BaseAddr = (MI_U8*)(pstCanvasInfo->virtAddr + pstCanvasInfo->u32Stride * stRect.u16Y + stRect.u16X / 2);
                for(int h = 0; h < stRect.u16Height; h++)
                {
                    memset(u8BaseAddr, u8Data, stRect.u16Width/u16Div);  
                    u8BaseAddr += pstCanvasInfo->u32Stride;
                }
                break;
           default:
               MIXER_ERR("OSD only support %s now\n", (pstCanvasInfo->ePixelFmt==0)?"ARGB1555":(pstCanvasInfo->ePixelFmt==2)?"I2":"I4");
               break;
        }
    }
    
    if(bNeedUptCanvas == TRUE)
    {
        s32Ret = MI_RGN_UpdateCanvas(hHandle);
        if(s32Ret != MI_RGN_OK)
        {
            MIXER_ERR("MI_RGN_UpdateCanvas fail\n");
            pthread_mutex_unlock(&g_stMutexOsdDraw);
            return s32Ret;
        }
    }

   pthread_mutex_unlock(&g_stMutexOsdDraw);

   return MI_SUCCESS;
}

MI_S32 OsdCreateTextWidget(MI_RGN_HANDLE hHandle, MI_RGN_Attr_t *pstRgnAttr, MI_RGN_ChnPort_t *pstRgnChnPort, MI_RGN_ChnPortParam_t *pstRgnChnPortParam)
{
   MI_S32 s32Ret = E_MI_ERR_FAILED;
   
   if(hHandle < 0)
   {
       MIXER_ERR("The input Rgn handle(%d) is out of range!\n", hHandle);
       s32Ret = E_MI_ERR_ILLEGAL_PARAM;
       return s32Ret;
   }

   if((NULL == pstRgnAttr) || (NULL == pstRgnChnPort) || (NULL == pstRgnChnPortParam))
   {
       MIXER_ERR("createOsdTextWidget() the input pointer is NULL!\n");
       s32Ret = E_MI_ERR_NULL_PTR;
       return s32Ret;
   }

   s32Ret = MI_RGN_Create(hHandle, pstRgnAttr);
   if(MI_RGN_OK != s32Ret)
   {
       MIXER_ERR("MI_RGN_Create error, %X\n", s32Ret);
       printf("%s:%d  Hdl=%d RGN_Attr:Type=%d, Width=%4d, Heitht=%4d, fmt=%d\n", __func__, __LINE__, hHandle, pstRgnAttr->eType,
              pstRgnAttr->stOsdInitParam.stSize.u32Width, pstRgnAttr->stOsdInitParam.stSize.u32Height, pstRgnAttr->stOsdInitParam.ePixelFmt);
       return s32Ret;
   }

   s32Ret = MI_RGN_AttachToChn(hHandle, pstRgnChnPort, pstRgnChnPortParam);
   if(MI_RGN_OK != s32Ret)
   {
   	   MIXER_ERR("MI_RGN_AttachToChn error, %X\n", s32Ret);
       s32Ret = MI_RGN_Destroy(hHandle);
	   if(MI_RGN_OK != s32Ret)
	   	   MIXER_ERR("MI_RGN_Destroy error, %X\n", s32Ret);

       MIXER_DBG("%s:%d  Hdl=%d RGN_Attr:Type=%d, Width=%4d, Heitht=%4d, fmt=%d\n", __func__, __LINE__, hHandle, pstRgnAttr->eType,
              pstRgnAttr->stOsdInitParam.stSize.u32Width, pstRgnAttr->stOsdInitParam.stSize.u32Height, pstRgnAttr->stOsdInitParam.ePixelFmt);
       MIXER_DBG("%s:%d  Hdl=%d RGN:ModId=%d, DevId=%d, ChnId=%d, PortId=%d, fmt=I4\n", __func__, __LINE__, hHandle, pstRgnChnPort->eModId,
                      pstRgnChnPort->s32DevId, pstRgnChnPort->s32ChnId, pstRgnChnPort->s32OutputPortId);
       MIXER_DBG("%s:%d  Hdl=%d canvas:x=%d, y=%d, InvertColorMode=%d, DivNum=%d, DivNum=%d, Threshold=%d\n", __func__, __LINE__, hHandle,
                      pstRgnChnPortParam->stPoint.u32X, pstRgnChnPortParam->stPoint.u32Y,
                      pstRgnChnPortParam->unPara.stOsdChnPort.stColorInvertAttr.eInvertColorMode,
                      pstRgnChnPortParam->unPara.stOsdChnPort.stColorInvertAttr.u16WDivNum,
                      pstRgnChnPortParam->unPara.stOsdChnPort.stColorInvertAttr.u16HDivNum,
                      pstRgnChnPortParam->unPara.stOsdChnPort.stColorInvertAttr.u16LumaThreshold);
       return s32Ret;
   }
   
   s32Ret = MI_RGN_OK;
   return s32Ret;
}

MI_S32 OsdCreateRectWidget(MI_RGN_HANDLE hHandle, MI_RGN_Attr_t *pstRgnAttr, MI_RGN_ChnPort_t *pstRgnChnPort, MI_RGN_ChnPortParam_t *pstRgnChnPortParam)
{
   MI_S32 s32Ret = E_MI_ERR_FAILED;

   if(MI_RGN_MAX_HANDLE < hHandle)
   {
       MIXER_ERR("The input Rgn handle(%d) is out of range!\n", hHandle);
       s32Ret = E_MI_ERR_ILLEGAL_PARAM;
       return s32Ret;
   }

   if((NULL == pstRgnAttr) || (NULL == pstRgnChnPort) || (NULL == pstRgnChnPortParam))
   {
       MIXER_ERR("createOsdRectWidget() the input pointer is NULL!\n");
       s32Ret = E_MI_ERR_NULL_PTR;
       return s32Ret;
   }

   s32Ret = MI_RGN_Create(hHandle, pstRgnAttr);
   if(MI_RGN_OK != s32Ret)
   {
       MIXER_ERR("MI_RGN_Create error(0x%X)\n", s32Ret);
       MIXER_ERR("%s:%d  Hdl=%d RGN_Attr:Type=%d, Width=%4d, Heitht=%4d, fmt=%d\n", __func__, __LINE__, hHandle, pstRgnAttr->eType,
              pstRgnAttr->stOsdInitParam.stSize.u32Width, pstRgnAttr->stOsdInitParam.stSize.u32Height, pstRgnAttr->stOsdInitParam.ePixelFmt);
       return s32Ret;
   }

   s32Ret = MI_RGN_AttachToChn(hHandle, pstRgnChnPort, pstRgnChnPortParam);
   if(MI_RGN_OK != s32Ret)
   {
       MIXER_ERR("MI_RGN_AttachToChn error(0x%X)\n MI_RGN_Destroy ret=(0x%X)\n", s32Ret, MI_RGN_Destroy(hHandle));
       MIXER_ERR("%s:%d  Hdl=%d RGN_Attr:Type=%d, Width=%4d, Heitht=%4d, fmt=%d\n", __func__, __LINE__, hHandle, pstRgnAttr->eType,
              pstRgnAttr->stOsdInitParam.stSize.u32Width, pstRgnAttr->stOsdInitParam.stSize.u32Height, pstRgnAttr->stOsdInitParam.ePixelFmt);
       MIXER_ERR("%s:%d  Hdl=%d RGN:ModId=%d, DevId=%d, ChnId=%d, PortId=%d\n", __func__, __LINE__, hHandle, pstRgnChnPort->eModId,
                      pstRgnChnPort->s32DevId, pstRgnChnPort->s32ChnId, pstRgnChnPort->s32OutputPortId);
       MIXER_ERR("%s:%d  Hdl=%d canvas:x=%d, y=%d, InvertColorMode=%d, DivNum=%d, DivNum=%d, Threshold=%d\n", __func__, __LINE__, hHandle,
                      pstRgnChnPortParam->stPoint.u32X, pstRgnChnPortParam->stPoint.u32Y,
                      pstRgnChnPortParam->unPara.stOsdChnPort.stColorInvertAttr.eInvertColorMode,
                      pstRgnChnPortParam->unPara.stOsdChnPort.stColorInvertAttr.u16WDivNum,
                      pstRgnChnPortParam->unPara.stOsdChnPort.stColorInvertAttr.u16HDivNum,
                      pstRgnChnPortParam->unPara.stOsdChnPort.stColorInvertAttr.u16LumaThreshold);
       return s32Ret;
   }
   s32Ret = MI_RGN_OK;
   return s32Ret;
}

MI_S32 OsdCreateWidget(MI_RGN_HANDLE *pu32RgnHandle, ST_RGN_WIDGET_ATTR *pstRgnWidgetAttr)
{
    MI_S32 s32Ret = MI_SUCCESS;
    MI_S8  errinfo[64] = {0};
    MI_RGN_Attr_t stRgnAttr;
    MI_RGN_ChnPortParam_t stRgnChnPortParam;
    MI_RGN_HANDLE u32OsdRgnHandleTmp = MI_RGN_HANDLE_NULL;

    CHECK_PARAM_IS_X(pu32RgnHandle,NULL,E_MI_ERR_NULL_PTR,"The input Rgn Param pointer is NULL!");
    CHECK_PARAM_IS_X(pstRgnWidgetAttr,NULL,E_MI_ERR_NULL_PTR,"The input Rgn Param pointer is NULL!");
    CHECK_PARAM_IS_X(pstRgnWidgetAttr->pstMutexOsdRun,NULL,E_MI_ERR_NULL_PTR,"The input Mixer Rgn Param pointer is NULL!");

    sprintf((char*)errinfo,"The input VencChn(%d) is out of range!\n",pstRgnWidgetAttr->s32VencChn);
    CHECK_PARAM_OPT_X(pstRgnWidgetAttr->s32VencChn,<,0,E_MI_ERR_ILLEGAL_PARAM,errinfo);
    CHECK_PARAM_OPT_X(pstRgnWidgetAttr->s32VencChn,>,g_s32VideoStreamNum,E_MI_ERR_ILLEGAL_PARAM,errinfo);
    memset(errinfo,0,sizeof(errinfo));
    sprintf((char*)errinfo,"The input OSD handle index(%d) is out of range!\n",pstRgnWidgetAttr->s32Idx);
    CHECK_PARAM_OPT_X(pstRgnWidgetAttr->s32Idx,<,0,E_MI_ERR_ILLEGAL_PARAM,errinfo);
    CHECK_PARAM_OPT_X(pstRgnWidgetAttr->s32Idx,>,MAX_RGN_NUMBER_PER_CHN,E_MI_ERR_ILLEGAL_PARAM,errinfo);

    pthread_mutex_lock(pstRgnWidgetAttr->pstMutexOsdRun);

    if(E_OSD_WIDGET_TYPE_RECT == pstRgnWidgetAttr->eOsdWidgetType)
    {
        u32OsdRgnHandleTmp = g_hOsdIpuHandle;
        if(0 > (MI_S32)u32OsdRgnHandleTmp  || MI_RGN_MAX_HANDLE < u32OsdRgnHandleTmp)
        {
            MIXER_ERR("Get OSD handle error(0x%X), VencChn=%d, index=%d, OSD type:%d\n", u32OsdRgnHandleTmp,
                       pstRgnWidgetAttr->s32VencChn, pstRgnWidgetAttr->s32Idx, pstRgnWidgetAttr->eOsdWidgetType);
            *pu32RgnHandle = MI_RGN_HANDLE_NULL;
            pthread_mutex_unlock(pstRgnWidgetAttr->pstMutexOsdRun);
            s32Ret = E_MI_ERR_ILLEGAL_PARAM;
            return s32Ret;
        }

        memset(&stRgnAttr, 0x00, sizeof(MI_RGN_Attr_t));
        stRgnAttr.eType = E_MI_RGN_TYPE_OSD;
        stRgnAttr.stOsdInitParam.ePixelFmt = pstRgnWidgetAttr->eRgnPixelFormat;
        stRgnAttr.stOsdInitParam.stSize.u32Width  = pstRgnWidgetAttr->stRect.u16Width;
        stRgnAttr.stOsdInitParam.stSize.u32Height = pstRgnWidgetAttr->stRect.u16Height;

        memset(&stRgnChnPortParam, 0x00, sizeof(MI_RGN_ChnPortParam_t));
        stRgnChnPortParam.bShow = pstRgnWidgetAttr->bShow;
        stRgnChnPortParam.stPoint.u32X = pstRgnWidgetAttr->stRect.u16X;
        stRgnChnPortParam.stPoint.u32Y = pstRgnWidgetAttr->stRect.u16Y;

        if(((pstRgnWidgetAttr->stRect.u16Width <= 1920) && (pstRgnWidgetAttr->stRect.u16Height <= 1080)) ||
           ((pstRgnWidgetAttr->stRect.u16Width <= 1080) && (pstRgnWidgetAttr->stRect.u16Height <= 1920)))
        {
            stRgnChnPortParam.unPara.stOsdChnPort.stColorInvertAttr.eInvertColorMode = E_MI_RGN_ABOVE_LUMA_THRESHOLD;
            stRgnChnPortParam.unPara.stOsdChnPort.stColorInvertAttr.u16WDivNum = OsdGetDivNumber(stRgnAttr.stOsdInitParam.stSize.u32Width);
            stRgnChnPortParam.unPara.stOsdChnPort.stColorInvertAttr.u16HDivNum = OsdGetDivNumber(stRgnAttr.stOsdInitParam.stSize.u32Height);

            if((1920 == stRgnAttr.stOsdInitParam.stSize.u32Width) && (1080 == stRgnAttr.stOsdInitParam.stSize.u32Height))
            {
                stRgnChnPortParam.unPara.stOsdChnPort.stColorInvertAttr.u16HDivNum /= 2;
            }
            else if((1080 == stRgnAttr.stOsdInitParam.stSize.u32Width) && (1920 == stRgnAttr.stOsdInitParam.stSize.u32Height))
            {
                stRgnChnPortParam.unPara.stOsdChnPort.stColorInvertAttr.u16WDivNum /= 2;
            }

            stRgnChnPortParam.unPara.stOsdChnPort.stColorInvertAttr.u16LumaThreshold = pstRgnWidgetAttr->u16LumaThreshold;
            stRgnChnPortParam.unPara.stOsdChnPort.stColorInvertAttr.bEnableColorInv = pstRgnWidgetAttr->bOsdColorInverse;
        }
           
	    stRgnChnPortParam.unPara.stOsdChnPort.u32Layer = (MI_U32)u32OsdRgnHandleTmp;

        s32Ret = OsdCreateRectWidget(u32OsdRgnHandleTmp, &stRgnAttr, &pstRgnWidgetAttr->stRgnChnPort, &stRgnChnPortParam);
        if(MI_RGN_OK != s32Ret)
        {
            *pu32RgnHandle = MI_RGN_HANDLE_NULL;
            MIXER_ERR("createOsdRectWidget error(0x%X), hdl=%d\n", s32Ret, u32OsdRgnHandleTmp);
            pthread_mutex_unlock(pstRgnWidgetAttr->pstMutexOsdRun);
            return s32Ret;
        }

        printf("%s:%d Create RectHdl=%d canvas:x=%d, y=%d, w=%4d, h=%4d, DevID=%d, ChnID=%d, PortID=%d, fmt=%d\n", __func__, __LINE__,
                          u32OsdRgnHandleTmp, stRgnChnPortParam.stPoint.u32X, stRgnChnPortParam.stPoint.u32Y,
                          stRgnAttr.stOsdInitParam.stSize.u32Width, stRgnAttr.stOsdInitParam.stSize.u32Height,
                          pstRgnWidgetAttr->stRgnChnPort.s32DevId, pstRgnWidgetAttr->stRgnChnPort.s32ChnId,
                          pstRgnWidgetAttr->stRgnChnPort.s32OutputPortId, pstRgnWidgetAttr->eRgnPixelFormat);

        printf("%s:%d Osd_W=%4d, Osd_H=%4d, WDivNum=%d, HDivNum=%d, InvertColorMode=%d, Threshold=%d, Layer=%d\n\n", __func__, __LINE__,
                          pstRgnWidgetAttr->stRect.u16Width, pstRgnWidgetAttr->stRect.u16Height,
                          stRgnChnPortParam.unPara.stOsdChnPort.stColorInvertAttr.u16WDivNum,
                          stRgnChnPortParam.unPara.stOsdChnPort.stColorInvertAttr.u16HDivNum,
                          stRgnChnPortParam.unPara.stOsdChnPort.stColorInvertAttr.eInvertColorMode,
                          stRgnChnPortParam.unPara.stOsdChnPort.stColorInvertAttr.u16LumaThreshold,
                          stRgnChnPortParam.unPara.stOsdChnPort.u32Layer);
        
        s32Ret = OsdCleanRectTextWidget(u32OsdRgnHandleTmp, NULL, NULL, 0, 0, RGN_PALETTEL_TABLE_ALPHA_INDEX, FALSE);
        if(MI_RGN_OK != s32Ret)
        {
            *pu32RgnHandle = u32OsdRgnHandleTmp;
            MIXER_ERR("cleanOsdTextWidget error(0x%X), hdl=%d\n", s32Ret, u32OsdRgnHandleTmp);
            pthread_mutex_unlock(pstRgnWidgetAttr->pstMutexOsdRun);
            return s32Ret;
        }
    }
    else if(E_OSD_WIDGET_TYPE_TEXT == pstRgnWidgetAttr->eOsdWidgetType)
    {
        u32OsdRgnHandleTmp = g_hOsdIpuHandle;
        
        if(0 > (MI_S32)u32OsdRgnHandleTmp || MI_RGN_MAX_HANDLE < u32OsdRgnHandleTmp)
        {
            MIXER_ERR("Get OSD handle error(0x%X), VencChn=%d, index=%d, OSD type:%d\n", u32OsdRgnHandleTmp,
                       pstRgnWidgetAttr->s32VencChn, pstRgnWidgetAttr->s32Idx, pstRgnWidgetAttr->eOsdWidgetType);
            *pu32RgnHandle = MI_RGN_HANDLE_NULL;
            pthread_mutex_unlock(pstRgnWidgetAttr->pstMutexOsdRun);
            s32Ret = E_MI_ERR_ILLEGAL_PARAM;
            return s32Ret;
        }

        memset(&stRgnAttr, 0x00, sizeof(MI_RGN_Attr_t));
        stRgnAttr.eType = E_MI_RGN_TYPE_OSD;
        stRgnAttr.stOsdInitParam.ePixelFmt = pstRgnWidgetAttr->eRgnPixelFormat;
        stRgnAttr.stOsdInitParam.stSize.u32Width  = pstRgnWidgetAttr->stRect.u16Width;
        stRgnAttr.stOsdInitParam.stSize.u32Height = pstRgnWidgetAttr->stRect.u16Height;

        memset(&stRgnChnPortParam, 0x00, sizeof(MI_RGN_ChnPortParam_t));
        stRgnChnPortParam.bShow = pstRgnWidgetAttr->bShow;
        stRgnChnPortParam.stPoint.u32X = pstRgnWidgetAttr->stRect.u16X;
        stRgnChnPortParam.stPoint.u32Y = pstRgnWidgetAttr->stRect.u16Y;
        stRgnChnPortParam.unPara.stOsdChnPort.stColorInvertAttr.eInvertColorMode = E_MI_RGN_ABOVE_LUMA_THRESHOLD;
        stRgnChnPortParam.unPara.stOsdChnPort.stColorInvertAttr.u16LumaThreshold = pstRgnWidgetAttr->u16LumaThreshold;
        stRgnChnPortParam.unPara.stOsdChnPort.stColorInvertAttr.u16WDivNum = OsdGetDivNumber(stRgnAttr.stOsdInitParam.stSize.u32Width);
        stRgnChnPortParam.unPara.stOsdChnPort.stColorInvertAttr.u16HDivNum = OsdGetDivNumber(stRgnAttr.stOsdInitParam.stSize.u32Height);
        stRgnChnPortParam.unPara.stOsdChnPort.u32Layer = (MI_U32)u32OsdRgnHandleTmp;
        stRgnChnPortParam.unPara.stOsdChnPort.stColorInvertAttr. = pstRgnWidgetAttr->bOsdColorInverse;
        stRgnChnPortParam.unPara.stOsdChnPort.stOsdAlphaAttr.eAlphaMode = E_MI_RGN_PIXEL_ALPHA;
        stRgnChnPortParam.unPara.stOsdChnPort.stOsdAlphaAttr.stAlphaPara.stArgb1555Alpha.u8BgAlpha = 0x0;
        stRgnChnPortParam.unPara.stOsdChnPort.stOsdAlphaAttr.stAlphaPara.stArgb1555Alpha.u8FgAlpha = 0xff;

        MIXER_DBG("u32OsdRgnHandleTmp;%d\n", u32OsdRgnHandleTmp);

        s32Ret = OsdCreateTextWidget(u32OsdRgnHandleTmp, &stRgnAttr, &pstRgnWidgetAttr->stRgnChnPort, &stRgnChnPortParam);
        if(MI_RGN_OK != s32Ret)
        {
            *pu32RgnHandle = MI_RGN_HANDLE_NULL;
            MIXER_ERR("createOsdTextWidget error(0x%X), hdl=%d\n", s32Ret, u32OsdRgnHandleTmp);
            pthread_mutex_unlock(pstRgnWidgetAttr->pstMutexOsdRun);
            return s32Ret;
        }

        printf("%s:%d Create TextHdl=%d canvas:x=%d, y=%d, w=%4d, h=%4d, DevID=%d, ChnID=%d, PortID=%d, fmt=%d\n", __func__, __LINE__,
                          u32OsdRgnHandleTmp, stRgnChnPortParam.stPoint.u32X, stRgnChnPortParam.stPoint.u32Y,
                          stRgnAttr.stOsdInitParam.stSize.u32Width, stRgnAttr.stOsdInitParam.stSize.u32Height,
                          pstRgnWidgetAttr->stRgnChnPort.s32DevId, pstRgnWidgetAttr->stRgnChnPort.s32ChnId,
                          pstRgnWidgetAttr->stRgnChnPort.s32OutputPortId, pstRgnWidgetAttr->eRgnPixelFormat);

        printf("%s:%d Osd_W=%4d, Osd_H=%4d, WDivNum=%d, HDivNum=%d, InvertColorMode=%d, Threshold=%d, Layer=%d\n\n", __func__, __LINE__,
                          pstRgnWidgetAttr->stRect.u16Width, pstRgnWidgetAttr->stRect.u16Height,
                          stRgnChnPortParam.unPara.stOsdChnPort.stColorInvertAttr.u16WDivNum,
                          stRgnChnPortParam.unPara.stOsdChnPort.stColorInvertAttr.u16HDivNum,
                          stRgnChnPortParam.unPara.stOsdChnPort.stColorInvertAttr.eInvertColorMode,
                          stRgnChnPortParam.unPara.stOsdChnPort.stColorInvertAttr.u16LumaThreshold,
                          stRgnChnPortParam.unPara.stOsdChnPort.u32Layer);
        
        s32Ret = OsdCleanRectTextWidget(u32OsdRgnHandleTmp, NULL, NULL, 0, 0, RGN_PALETTEL_TABLE_ALPHA_INDEX, TRUE);
        if(MI_RGN_OK != s32Ret)
        {
            *pu32RgnHandle = u32OsdRgnHandleTmp;
            MIXER_ERR("cleanOsdTextWidget error(%X), hdl=%d\n", s32Ret, u32OsdRgnHandleTmp);
            pthread_mutex_unlock(pstRgnWidgetAttr->pstMutexOsdRun);
            return s32Ret;
        }
    }
    else if(E_OSD_WIDGET_TYPE_COVER == pstRgnWidgetAttr->eOsdWidgetType)
    {
    }
    else
    {
        *pu32RgnHandle = MI_RGN_HANDLE_NULL;
        MIXER_ERR("Set wrong OSD type(%d) and return!\n", pstRgnWidgetAttr->eOsdWidgetType);
        pthread_mutex_unlock(pstRgnWidgetAttr->pstMutexOsdRun);
        s32Ret = E_MI_ERR_ILLEGAL_PARAM;
        return s32Ret;
    }
    *pu32RgnHandle = u32OsdRgnHandleTmp;
    pthread_mutex_unlock(pstRgnWidgetAttr->pstMutexOsdRun);

    return s32Ret;
}

MI_S32 OsdDestroyWidget(MI_VENC_CHN s32VencChn, MI_RGN_HANDLE u32OsdHandle)
{
    MI_S32 s32Ret = MI_SUCCESS;
    MI_RGN_ChnPort_t stRgnChnPort;
    MI_RGN_HANDLE *pu32OsdRgnHandleTmp = &u32OsdHandle;
    EN_OSD_WIDGET_TYPE eOsdWidgetType = E_OSD_WIDGET_TYPE_MAX;
    
    if((s32VencChn < 0) || (s32VencChn > g_s32VideoStreamNum))
    {
        MIXER_ERR("The input VenChn(%d) is out of range!\n", s32VencChn);
        s32Ret = E_MI_ERR_ILLEGAL_PARAM;
        return s32Ret;
    }

    if((MI_RGN_HANDLE)MI_RGN_HANDLE_NULL == u32OsdHandle)
    {
        MIXER_ERR("The input OSD handle(%d) is out of range!\n", u32OsdHandle);
        s32Ret = E_MI_ERR_ILLEGAL_PARAM;
        return s32Ret;
    }

    pthread_mutex_lock(&g_stMutexOsdRun[s32VencChn]);
    
    if(NULL == pu32OsdRgnHandleTmp)
    {
        pthread_mutex_unlock(&g_stMutexOsdRun[s32VencChn]);
        MIXER_ERR("Can not find the OSD handle(%d) on s32VencChn(%d)\n", u32OsdHandle, s32VencChn);
        s32Ret = E_MI_ERR_NULL_PTR;
        return s32Ret;
    }
    
    OsdRgnChnPortConfig(s32VencChn, &stRgnChnPort);
    
    if((E_OSD_WIDGET_TYPE_RECT == eOsdWidgetType) || (E_OSD_WIDGET_TYPE_TEXT == eOsdWidgetType))
    {
        OsdCleanRectTextWidget(u32OsdHandle, NULL, NULL, 0, 0, RGN_PALETTEL_TABLE_ALPHA_INDEX, TRUE);
    }
    
    s32Ret = MI_RGN_DetachFromChn(u32OsdHandle, &stRgnChnPort);
    if(MI_RGN_OK != s32Ret)
    {
        MIXER_ERR("MI_RGN_DetachFromChn error(0x%X), hdl=%d\n", s32Ret, u32OsdHandle);
        pthread_mutex_unlock(&g_stMutexOsdRun[s32VencChn]);
        return s32Ret;
    }
    
    s32Ret = MI_RGN_Destroy(u32OsdHandle);
    if(MI_RGN_OK != s32Ret)
    {
        MIXER_ERR("MI_RGN_Destroy error(0x%X), hdl=%d\n", s32Ret, u32OsdHandle);
        pthread_mutex_unlock(&g_stMutexOsdRun[s32VencChn]);
        return s32Ret;
    }
    
    *pu32OsdRgnHandleTmp = MI_RGN_HANDLE_NULL;
    pthread_mutex_unlock(&g_stMutexOsdRun[s32VencChn]);
    
    return s32Ret;
}

int OsdAddDlaRectData(MI_S32 s32VencChn, MI_S32 recCnt, ST_DlaRectInfo_T* pRecInfo, MI_BOOL bShow, MI_BOOL bShowBorder)
{
    MI_U32 size = 0x0;
    RectList_t *stRectList;

    if(recCnt <= 0x0 || g_bOsdTaskExit)
    {
        //MIXER_ERR("param err, recCnt should not be zero.\n");
        return E_MI_ERR_FAILED;
    }

    if(list_empty(&g_EmptyRectList))
    {
        MIXER_ERR("g_EmptyRectList is empty\n");
        return MI_SUCCESS;
    }

    stRectList = list_entry(g_EmptyRectList.next, RectList_t, rectlist);
    if(NULL == stRectList)
    {
        MIXER_ERR("no find stRectList.\n");
        return E_MI_ERR_FAILED;
    }

    size = recCnt * sizeof(ST_DlaRectInfo_T);

    stRectList->pChar= (MI_U8 *)malloc(size);
    if(NULL == stRectList->pChar)
    {
        MIXER_ERR("can not malloc size:%d.\n", size);
        return E_MI_ERR_FAILED;
    }

    stRectList->tCount = recCnt;
    memcpy((MI_S8 *)stRectList->pChar, (MI_S8 *)pRecInfo, recCnt * sizeof(ST_DlaRectInfo_T));

    pthread_mutex_lock(&g_stMutexListLock);
    list_del(&stRectList->rectlist);
    list_add_tail(&stRectList->rectlist, &g_WorkRectList);
    pthread_mutex_unlock(&g_stMutexListLock);
    
    return MI_SUCCESS;
}

int OsdUpdateDlaRect()
{
    #define RECT_BORDER_WIDTH   4
    static MI_BOOL bRectNeedClean = FALSE;
    
    MI_BOOL bWorkListEmpty = TRUE;
    MI_RGN_HANDLE hHandle = g_hOsdIpuHandle;
    
    MI_U32 u32Color = 0;
    Point_t stPoint;
    RectList_t* stRectList = NULL;
    ST_DlaRectInfo_T stIpuRectInfo;    
    RectWidgetAttr_t stRectWidgetAttr;
    TextWidgetAttr_t stTextWidgetAttr;
        
    if(((MI_S32)hHandle <= MI_RGN_HANDLE_NULL  || hHandle >= MI_RGN_MAX_HANDLE))
    {
		MIXER_ERR("OSD handle error,hHandle=%d\n", hHandle);
        return E_MI_ERR_FAILED;
    }

    bWorkListEmpty = list_empty(&g_WorkRectList);
    
    if(bRectNeedClean == FALSE && bWorkListEmpty == TRUE)
    {
        MIXER_DBG("g_WorkRectList is empty\n");
        return E_MI_ERR_FAILED;
    }

    if(bRectNeedClean == TRUE)
    {
        for(MI_U32 i = 0; i < g_u32RectCount; i++)
        {
            g_stCleanRect[i].u16X = MIN(g_stNameRect[i].u16X, g_stDlaRect[i].u16X);
            g_stCleanRect[i].u16Y = MIN(g_stNameRect[i].u16Y, g_stDlaRect[i].u16Y);
            g_stCleanRect[i].u16Width = MAX(g_stNameRect[i].u16Width, g_stDlaRect[i].u16Width);
            g_stCleanRect[i].u16Height = MAX(g_stNameRect[i].u16Height, g_stDlaRect[i].u16Height);
        }
        OsdCleanRectTextWidget(hHandle, NULL, g_stCleanRect, g_u32RectCount, RECT_BORDER_WIDTH, RGN_PALETTEL_TABLE_ALPHA_INDEX, TRUE);
        //OsdCleanRectTextWidget(hHandle, NULL, g_stNameRect, g_u32RectCount, RECT_BORDER_WIDTH, RGN_PALETTEL_TABLE_ALPHA_INDEX, TRUE);
        //OsdCleanRectTextWidget(hHandle, NULL, g_stDlaRect, g_u32RectCount, RECT_BORDER_WIDTH, RGN_PALETTEL_TABLE_ALPHA_INDEX, FALSE);
        bRectNeedClean = FALSE;
    }

    if(bWorkListEmpty == TRUE)
    {
        return MI_SUCCESS;  
    }
    
    pthread_mutex_lock(&g_stMutexListLock);
    
    stRectList = list_entry(g_WorkRectList.next, RectList_t, rectlist);
    list_del(&stRectList->rectlist);
    
    if( stRectList->pChar == NULL 
      ||stRectList->tCount <= 0 
      ||stRectList->tCount > MAX_DLA_RECT_NUMBER)
    {
        if(stRectList->pChar)
		{
        	free(stRectList->pChar);
        	stRectList->pChar = NULL;
		}
        
        stRectList->tCount = 0x0;
        list_add_tail(&stRectList->rectlist, &g_EmptyRectList);
        
        pthread_mutex_unlock(&g_stMutexListLock);       
        return E_MI_ERR_FAILED;
    }
      
    memset(&g_stDlaRect[0], 0x00, sizeof(MI_SYS_WindowRect_t) * MAX_DLA_RECT_NUMBER);

    for(int i = 0; i < stRectList->tCount; i++)
    {
        memset(&stIpuRectInfo,0x00,sizeof(ST_DlaRectInfo_T));
        memcpy(&stIpuRectInfo,stRectList->pChar + i*sizeof(ST_DlaRectInfo_T), sizeof(ST_DlaRectInfo_T));

        g_stDlaRect[i].u16X = stIpuRectInfo.rect.u32X;
        g_stDlaRect[i].u16Y = stIpuRectInfo.rect.u32Y;
        g_stDlaRect[i].u16Width = stIpuRectInfo.rect.u16PicW;
        g_stDlaRect[i].u16Height = stIpuRectInfo.rect.u16PicH;
        
	    stPoint.x = stIpuRectInfo.rect.u32X;
	    stPoint.y = stIpuRectInfo.rect.u32Y;
        g_stNameRect[i].u16X = stPoint.x;
        g_stNameRect[i].u16Y = stPoint.y;

        u32Color = OsdGetTextColor(stIpuRectInfo.szObjName);

        memset(&stTextWidgetAttr, 0x00, sizeof(TextWidgetAttr_t));
        stTextWidgetAttr.string  = stIpuRectInfo.szObjName;
        stTextWidgetAttr.pPoint  = &stPoint;
        stTextWidgetAttr.size    = g_stOsdTextWidgetOrder.size[0];
        stTextWidgetAttr.pmt     = g_stOsdTextWidgetOrder.pmt;
        stTextWidgetAttr.pfColor = &g_stGreenColor;
        stTextWidgetAttr.pbColor = &g_stBlackColor;
        stTextWidgetAttr.u32Color = u32Color;
        stTextWidgetAttr.space = 0;
        stTextWidgetAttr.bOutline = FALSE;
        OsdUpdateTextWidget(hHandle, NULL, &stTextWidgetAttr, &g_stNameRect[i]);

        memset(&stRectWidgetAttr, 0x00, sizeof(RectWidgetAttr_t));
        stRectWidgetAttr.pstRect = &g_stDlaRect[i];
		stRectWidgetAttr.s32RectCnt = 1;
		stRectWidgetAttr.u8BorderWidth = RECT_BORDER_WIDTH;
		stRectWidgetAttr.pmt = g_stOsdTextWidgetOrder.pmt;
		stRectWidgetAttr.pfColor = &g_stRedColor;
		stRectWidgetAttr.pbColor = &g_stBlackColor;
		stRectWidgetAttr.bFill = FALSE;
		stRectWidgetAttr.u32Color = u32Color;
		OsdUpdateRectWidget(hHandle, NULL, &stRectWidgetAttr);
    }

    bRectNeedClean = TRUE; 
    g_u32RectCount = stRectList->tCount;
    
    if(stRectList->pChar)
	{
    	free(stRectList->pChar);
    	stRectList->pChar = NULL;
	}
    
    stRectList->tCount = 0x0;
    list_add_tail(&stRectList->rectlist, &g_EmptyRectList);

    pthread_mutex_unlock(&g_stMutexListLock);
    
    return MI_SUCCESS; 
}

void* OsdTask(void * argu)
{
    MI_S32 s32Ret = -1;
   	struct timeval now;
	struct timespec outtime;
    
    while(FALSE == g_bOsdTaskExit)
    {
       gettimeofday(&now, NULL);
	   outtime.tv_sec = now.tv_sec + 2;
	   outtime.tv_nsec = now.tv_usec * 1000;
	   pthread_mutex_lock(&g_stMutexOsdUptState);
	   pthread_cond_timedwait(&g_condOsdUpadteState, &g_stMutexOsdUptState, &outtime);
	   s32Ret = OsdUpdateDlaRect();
	   pthread_mutex_unlock(&g_stMutexOsdUptState);
	   if(s32Ret != MI_SUCCESS)
	   {
	        usleep(1000);
	   }
    }
    return 0;
}

void OsdThreadInit(void)
{   
    g_bOsdTaskExit = FALSE;
    Create_thread(&g_stPthreadOsd, -1, "OsdTask", OsdTask, (void*)NULL);
}

void OsdThreadUnInit(void)
{     
    g_bOsdTaskExit = TRUE;
	pthread_cancel(g_stPthreadOsd);
    pthread_join(g_stPthreadOsd, NULL);
}

int OsdModuleInit()
{
    MI_S32 s32Chn = 0;
    MI_S32 s32Port = 0;
    MI_S32 s32Ret = MI_SUCCESS;
    MI_VPE_PortMode_t stVpePortMode;
    ST_RGN_WIDGET_ATTR stRgnWidgetAttr;

    STCHECKRESULT(MI_VPE_GetPortMode(s32Chn, s32Port, &stVpePortMode));

    memset(&stRgnWidgetAttr, 0x00, sizeof(ST_RGN_WIDGET_ATTR));
    stRgnWidgetAttr.s32VencChn = s32Chn;
    stRgnWidgetAttr.s32Idx = 0;
    stRgnWidgetAttr.eOsdWidgetType = E_OSD_WIDGET_TYPE_RECT;
    stRgnWidgetAttr.bShow = TRUE;
    stRgnWidgetAttr.bOsdColorInverse = g_bOsdColorInverse;
    stRgnWidgetAttr.eRgnPixelFormat = g_stOsdTextWidgetOrder.pmt;
    stRgnWidgetAttr.u32Color = 0xff801080; //black
    stRgnWidgetAttr.u16LumaThreshold = OSD_COLOR_INVERSE_THD;
    stRgnWidgetAttr.pstMutexOsdRun = &g_stMutexOsdRun[s32Chn];

    stRgnWidgetAttr.stRect.u16X = 0;
    stRgnWidgetAttr.stRect.u16Y = 0;
    stRgnWidgetAttr.stRect.u16Width  = stVpePortMode.u16Width;
    stRgnWidgetAttr.stRect.u16Height = stVpePortMode.u16Height;
    
    OsdRgnChnPortConfig(s32Chn, &stRgnWidgetAttr.stRgnChnPort);
    
    s32Ret = OsdCreateWidget(&g_hOsdIpuHandle, &stRgnWidgetAttr);
    if(s32Ret != MI_SUCCESS)
    {
        MIXER_ERR("createOsdWidget() error(0x%X)\n", s32Ret);
    }
    
    return s32Ret;
}

int OsdModuleUnInit()
{
    MI_S32 s32Ret = MI_SUCCESS;
    
    if((MI_S32)g_hOsdIpuHandle !=  MI_RGN_HANDLE_NULL)
    {
        s32Ret = OsdDestroyWidget(0 , g_hOsdIpuHandle);
        if(s32Ret != MI_SUCCESS)
        {
            MIXER_ERR("destroyOsdWidget() error(0x%X)\n", s32Ret);
        }
    }

    if(g_u8DrawTextBuffer != NULL)
    {
        free(g_u8DrawTextBuffer);
    }
    
    return s32Ret;
}

void OsdMutexInit()
{
    MI_U16 u16Chn = 0;
        
	for(u16Chn = 0; u16Chn < MAX_VIDEO_NUMBER; u16Chn++)
	{
	    g_stOsdTextWidgetOrder.size[u16Chn] = OSD_TEXT_MEDIUM_FONT_SIZE;
	    pthread_mutex_init(&g_stMutexOsdRun[u16Chn],NULL);
	}
    
    g_stOsdTextWidgetOrder.pmt  = E_MI_RGN_PIXEL_FORMAT_I4;
    pthread_mutex_init(&g_stMutexListLock, NULL);
    pthread_mutex_init(&g_stMutexOsdDraw, NULL);
    pthread_mutex_init(&g_stMutexOsdUptState, NULL);
    pthread_cond_init( &g_condOsdUpadteState, NULL ) ;
}

void OsdMutexUnInit()
{
    MI_U16 u16Chn = 0;
    
    for(u16Chn = 0; u16Chn < MAX_VIDEO_NUMBER; u16Chn++)
	{
		g_stOsdTextWidgetOrder.size[u16Chn] = OSD_TEXT_MEDIUM_FONT_SIZE;
		pthread_mutex_destroy(&g_stMutexOsdRun[u16Chn]);
	}

    pthread_mutex_destroy(&g_stMutexListLock);
    pthread_mutex_destroy(&g_stMutexOsdDraw);
    pthread_mutex_destroy(&g_stMutexOsdUptState);
    pthread_cond_destroy(&g_condOsdUpadteState);
}

void OsdListInit(void)
{
    MI_U16 u16Idx = 0;

    INIT_LIST_HEAD(&g_EmptyRectList);
    INIT_LIST_HEAD(&g_WorkRectList);
    
    for (u16Idx = 0; u16Idx < MAX_RECT_LIST_NUMBER; u16Idx++)
    {
        g_stRectList[u16Idx].tCount = 0x0;
        g_stRectList[u16Idx].pChar = NULL;
        INIT_LIST_HEAD(&g_stRectList[u16Idx].rectlist);
        list_add_tail(&g_stRectList[u16Idx].rectlist, &g_EmptyRectList);
    }
}

void OsdListUnInit(void)
{
    MI_U16 u16Idx = 0;
 
    for (u16Idx = 0; u16Idx < MAX_RECT_LIST_NUMBER; u16Idx++)
    {
        if(g_stRectList[u16Idx].pChar)
        {
            free(g_stRectList[u16Idx].pChar);
        }
    }
}

void OsdFontInit(void)
{
    OsdSetFontPath((char*)DEF_FONT_PATH);
}

void OsdRgnInit()
{
	MI_S32 s32Ret = E_MI_ERR_FAILED;
    
    s32Ret = MI_RGN_Init(&g_stPaletteTable);
	if(MI_RGN_OK != s32Ret)
    {
        MIXER_ERR("MI_RGN_Init error(%X)\n", s32Ret);
    }
}

void OsdRgnUnInit()
{
	MI_S32 s32Ret = E_MI_ERR_FAILED;
    
    s32Ret = MI_RGN_DeInit();
	if(MI_RGN_OK != s32Ret)
    {
        MIXER_ERR("MI_RGN_Init error(%X)\n", s32Ret);
    }
}

void OsdInitAndStart(void)
{
    OsdFontInit();
    OsdRgnInit();
    OsdListInit();
	OsdMutexInit();
    OsdModuleInit();
    OsdThreadInit();
}

void OsdUnInitAndStop(void)
{
    OsdThreadUnInit();
    OsdModuleUnInit();
    OsdMutexUnInit();
    OsdListUnInit();
    OsdRgnUnInit();    
}

