/*
* mid_utils.h- Sigmastar
*
* Copyright (C) 2018 Sigmastar Technology Corp.
*
* Author: XXXX <XXXX@sigmastar.com.cn>
*
* This software is licensed under the terms of the GNU General Public
* License version 2, as published by the Free Software Foundation, and
* may be copied, distributed, and modified under those terms.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
*/

#ifndef _OSD_H_
#define _OSD_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include "mi_rgn.h"
#include "List.h"
#include "mi_sys_datatype.h"
#include "st_common.h"
#include "common.h"

#define DEF_FONT_PATH                            "/customer"
#define DEF_AUDIOPLAY_FILE                       "/customer/xiaopingguo.wav"

#define MAX_DLA_RECT_NUMBER                      10
#define MAX_RECT_LIST_NUMBER                     10

#define MAX_RGN_NUMBER_PER_CHN                   16

#define HZ_8P_BIN_SIZE                           65424
#define HZ_12P_BIN_SIZE                          196272
#define HZ_16P_BIN_SIZE                          261696
typedef unsigned short RGBA4444;
#define MAX_VIDEO_NUMBER  6

#define OSD_TEXT_SMALL_FONT_SIZE                 FONT_SIZE_16
#define OSD_TEXT_MEDIUM_FONT_SIZE                FONT_SIZE_32
#define OSD_TEXT_LARGE_FONT_SIZE                 FONT_SIZE_72

#define OSD_COLOR_INVERSE_THD                    96
#define RGN_PALETTEL_TABLE_ALPHA_INDEX           0x00

#define CHECK_PARAM_IS_X(PARAM,X,RET,errinfo) \
do\
{\
    if((PARAM) == (X))\
    { \
       MIXER_ERR("The input Mixer Rgn Param pointer is NULL!\n");\
       return RET;\
    }\
}while(0);
    
#define CHECK_PARAM_OPT_X(PARAM,OPT,X,RET,errinfo) \
do\
{\
 if((PARAM) OPT (X))\
  { \
       MIXER_ERR("%s\n",errinfo);\
       return RET;\
  }\
}while(0);

#ifndef RGB2PIXEL1555
#define RGB2PIXEL1555(a,r,g,b)    (((a & 0x80) << 8) | ((r & 0xF8) << 7) | ((g & 0xF8) << 2) | ((b & 0xF8) >> 3))
#endif

#ifndef MALLOC
#define MALLOC(s) malloc(s)
#endif

#ifndef FREEIF
#define FREEIF(m) if(m!=0) {free(m);m=NULL;}
#endif

#ifndef MI_SYS_Malloc
#define MI_SYS_Malloc(size) malloc(size)
#endif

#ifndef MI_SYS_Realloc
#define MI_SYS_Realloc(ptr, size) realloc(ptr, size)
#endif

#ifndef MI_SYS_Free
#define MI_SYS_Free(pData) { if(pData != NULL) free(pData); pData = NULL; }
#endif

typedef enum _PixelFormat_e
{
    COLOR_FormatUnused,
    COLOR_FormatYCbYCr,                 //yuv422
    COLOR_FormatSstarSensor16bitRaw,    //
    COLOR_FormatSstarSensor16bitYC,
    COLOR_FormatSstarSensor16bitSTS,    //do not support
    COLOR_FormatYUV420SemiPlanar,
    COLOR_FormatYUV420Planar,           //yuv420
    COLOR_Format16bitBGR565,
    COLOR_Format16bitARGB4444,
    COLOR_Format16bitARGB1555,
    COLOR_Format24bitRGB888,
    COLOR_Format32bitABGR8888,
    COLOR_FormatL8,
    COLOR_FormatMONO,
    COLOR_FormatGRAY2,
    COLOR_FormatMax,
} PixelFormat_e;

typedef enum _OsdFontSize_e
{
    FONT_SIZE_8,
    FONT_SIZE_12,
    FONT_SIZE_16,
    FONT_SIZE_24,
    FONT_SIZE_32,
    FONT_SIZE_36,
    FONT_SIZE_40,
    FONT_SIZE_48,
    FONT_SIZE_56,
    FONT_SIZE_60,
    FONT_SIZE_64,
    FONT_SIZE_72,
    FONT_SIZE_80,
    FONT_SIZE_84,
    FONT_SIZE_96
} OsdFontSize_e;

typedef enum
{
    E_OSD_WIDGET_TYPE_RECT = 0,
    E_OSD_WIDGET_TYPE_TEXT,
    E_OSD_WIDGET_TYPE_COVER,
    E_OSD_WIDGET_TYPE_BITMAP,
    E_OSD_WIDGET_TYPE_MAX
} EN_OSD_WIDGET_TYPE;

typedef struct
{
    ST_Rect_T rect;
    char szObjName[64];
}ST_DlaRectInfo_T;

typedef struct ImageData_s
{
    MI_RGN_PixelFormat_e pmt;
    MI_U16 width;
    MI_U16 height;
    MI_U8* buffer;
} ImageData_t;

typedef struct _Color_t
{
	MI_U8  a;
	MI_U8  r;
	MI_U8  g;
	MI_U8  b;
} Color_t;

typedef struct _Point_t
{
	MI_S32 x;
	MI_S32 y;
} Point_t;

typedef struct _YUVColor_t
{
	MI_U8  y;
	MI_U8  u;
	MI_U8  v;
	MI_U8  transparent;
} YUVColor_t;

struct OsdTextWidgetOrder_t
{
    Point_t stPointTime;
    Point_t stPointFps;
    Point_t stPointBitRate;
    Point_t stPointResolution;
    Point_t stPointGop;
    Point_t stPointTemp;
    Point_t stPointTotalGain;
    Point_t stPointIspExpo;
    Point_t stPointIspWb;
    Point_t stPointIspExpoInfo;
    Point_t stPointUser;
    Point_t stPointUser1;
    OsdFontSize_e size[MAX_VIDEO_NUMBER+1];
    MI_RGN_PixelFormat_e pmt;
};
  
typedef struct
{
    MI_VENC_CHN           s32VencChn;
    MI_S32                s32Idx;
    MI_U32                u32Color;
    MI_U16                u16LumaThreshold;
    MI_BOOL               bShow;
    MI_BOOL               bOsdColorInverse;
    MI_RGN_PixelFormat_e  eRgnPixelFormat;
    EN_OSD_WIDGET_TYPE    eOsdWidgetType;
    MI_SYS_WindowRect_t   stRect;
    MI_RGN_ChnPort_t      stRgnChnPort;
    pthread_mutex_t       *pstMutexOsdRun;
} ST_RGN_WIDGET_ATTR;

typedef struct _TextWidgetAttr_s
{
    const char * string;
    Point_t* pPoint;
    OsdFontSize_e size;
    MI_RGN_PixelFormat_e pmt;
    Color_t* pfColor;
    Color_t* pbColor;
    MI_U8 u32Color;
    MI_U32 space;
    MI_BOOL bHard;
    MI_BOOL bRle;
    MI_BOOL bOutline;
} TextWidgetAttr_t;

typedef struct _RectWidgetAttr_s
{
    MI_SYS_WindowRect_t *pstRect;
    MI_S32 s32RectCnt;
    MI_U32 u32Color;
    MI_U8  u8BorderWidth;
    MI_RGN_PixelFormat_e pmt;
    Color_t* pfColor;
    Color_t* pbColor;
    MI_BOOL bFill;
    MI_BOOL bHard;
    MI_BOOL bOutline;
} RectWidgetAttr_t;

typedef struct MI_Font_s
{
    MI_U32 nFontSize;
    MI_U8* pData;
} MI_Font_t;

typedef enum
{
    HZ_DOT_8,
    HZ_DOT_12,
    HZ_DOT_16,
    HZ_DOT_NUM
} MI_FontDot_e;

typedef struct
{
    MI_U32 u16X;
    MI_U32 u16Y;
}DrawPoint_t;

typedef struct
{
    MI_U32 u16Width;
    MI_U32 u16Height;
}DrawSize_t;

typedef struct
{
    MI_RGN_PixelFormat_e ePixelFmt;
    MI_U32 u32Color;
}DrawRgnColor_t;

typedef struct _rect{
    struct list_head rectlist;
    MI_S32  tCount;
    MI_U8 *pChar;
}RectList_t;

MI_U32 Font_Get_StrLen(const MI_S8 *phzstr);
MI_U32 Font_Get_Size(OsdFontSize_e eFontSize);
MI_U32 Font_GetCharQW(const MI_S8 *str, MI_U32 *p_qu, MI_U32 *p_wei);
MI_U32 Font_Strcmp(MI_S8* pStr1, MI_S8* pStr2);

MI_Font_t* Font_GetHandle(OsdFontSize_e eFontSize, MI_U32* pMultiple);

MI_S32 Font_DrawText(ImageData_t* pImage, const MI_S8 *pStr, MI_U32 idx_start, OsdFontSize_e eFontSize,
                              MI_U32 nSpace, Color_t* pfColor, Color_t* pbColor , MI_BOOL bOutline);
MI_S32 Font_DrawYUV420(MI_Font_t* pFont , MI_U32 qu , MI_U32 wei, MI_U32 nMultiple, ImageData_t* pYuv,
                           MI_U32 nIndex, YUVColor_t* pfColor, YUVColor_t* pbColor , MI_BOOL bOutline);
MI_S32 Font_DrawRGB4444(MI_Font_t* pFont , MI_U32 qu , MI_U32 wei, MI_U32 nMultiple, ImageData_t* pBmp,
                           MI_U32 nIndex, Color_t* pfColor, Color_t* pbColor, MI_BOOL bOutline);
MI_S32 Font_DrawRGB1555(MI_Font_t* pFont , MI_U32 qu , MI_U32 wei, MI_U32 nMultiple, ImageData_t* pBmp,
                           MI_U32 nIndex, Color_t* pfColor, Color_t* pbColor, MI_BOOL bOutline);

void DrawPoint(void *pBaseAddr, MI_U32 u32Stride, DrawPoint_t stPt, DrawRgnColor_t stColor);
void DrawLine(void *pBaseAddr, MI_U32 u32Stride, DrawPoint_t stStartPt, DrawPoint_t stEndPt, MI_U8 u8BorderWidth, DrawRgnColor_t stColor);
void DrawRect(void *pBaseAddr, MI_U32 u32Stride, DrawPoint_t stLeftTopPt, DrawPoint_t stRightBottomPt, MI_U8 u8BorderWidth, DrawRgnColor_t stColor);

void FillRect(void *pBaseAddr, MI_U32 u32Stride, DrawPoint_t stLeftTopPt, DrawPoint_t stRightBottomPt, DrawRgnColor_t stColor);
void DrawCircular(void *pBaseAddr, MI_U32 u32Stride, DrawPoint_t stCenterPt, MI_U32 u32Radius, MI_U8 u8BorderWidth, DrawRgnColor_t stColor);
void FillCircular(void *pBaseAddr, MI_U32 u32Stride, DrawPoint_t stCenterPt, MI_U32 u32Radius, DrawRgnColor_t stColor);

void OsdSetFontPath(char* OsdFontPath);
int  OsdAddDlaRectData(MI_S32 s32VencChn, MI_S32 recCnt, ST_DlaRectInfo_T* pRecInfo, MI_BOOL bShow, MI_BOOL bShowBorder);
void OsdInitAndStart(void);
void OsdUnInitAndStop(void);
#ifdef __cplusplus
}
#endif

#endif //_MID_UTILS_H_

