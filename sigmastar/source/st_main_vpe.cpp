/* Copyright (c) 2018-2019 Sigmastar Technology Corp.
 All rights reserved.

  Unless otherwise stipulated in writing, any and all information contained
 herein regardless in any format shall remain the sole proprietary of
 Sigmastar Technology Corp. and be kept in strict confidence
 (��Sigmastar Confidential Information��) by the recipient.
 Any unauthorized act including without limitation unauthorized disclosure,
 copying, use, reproduction, sale, distribution, modification, disassembling,
 reverse engineering and compiling of the contents of Sigmastar Confidential
 Information is unlawful and strictly prohibited. Sigmastar hereby reserves the
 rights to any and all damages, losses, costs and expenses resulting therefrom.
*/
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <string>

#include "st_common.h"
#include "st_vif.h"
#include "st_vpe.h"
#include "st_venc.h"
#include "dictionary.h"
#include "iniparser.h"

#include "BasicUsageEnvironment.hh"
#include "liveMedia.hh"
#include "Live555RTSPServer.hh"

#include "mi_sensor.h"
#include "mi_sensor_datatype.h"
#include "mi_isp.h"
#include "mi_iqserver.h"
#include "mi_eptz.h"
#include "mi_ldc.h"
#include "mi_eptz.h"
#include "mi_divp_datatype.h"
#include "mi_divp.h"
#include "mi_sys_datatype.h"
#include "divp_app.h"
#include "dla_base.h"
#include "dla_app.h"
#include "osd.h"

using namespace std;

int s32LoadIQBin = 1;

#define RTSP_LISTEN_PORT       554
static Live555RTSPServer *g_pRTSPServer = NULL;

#define ST_MAX_PORT_NUM        (5)
#define ST_MAX_SCL_NUM         (3)
#define ST_MAX_SENSOR_NUM      (3)
#define ST_MAX_LDC_VIEWNUM     (4)
#define ST_MAX_STREAM_NUM      (ST_MAX_PORT_NUM * ST_MAX_SENSOR_NUM)

#define PATH_PREFIX            "/mnt"
#define NONHDR_PATH            "/customer/nohdr.bin"
#define HDR_PATH               "/customer/hdr.bin"

typedef struct ST_Sensor_Attr_s
{
    MI_U32  u32BindVifDev;
    MI_BOOL bUsed;
    MI_U8   u8ResIndex;
}ST_Sensor_Attr_t;

typedef struct ST_Vif_Attr_s
{
    MI_U32     u32BindVpeChan;
    MI_SYS_BindType_e  eBindType;
    MI_VIF_WorkMode_e  eWorkMode;
}ST_Vif_Attr_t;

typedef struct ST_VpePortAttr_s
{
    MI_BOOL bUsed;
    MI_U32  u32BindVencChan;
    MI_BOOL bMirror;
    MI_BOOL bFlip;
	MI_BOOL bBindDivp;
    MI_SYS_PixelFormat_e ePixelFormat;
    MI_SYS_WindowSize_t  stOrigPortSize;
    MI_SYS_WindowRect_t  stOrigPortCrop;

    MI_SYS_WindowRect_t  stPortCrop;
    MI_SYS_WindowSize_t  stPortSize;
    MI_S32 s32DumpBuffNum;
    char   FilePath[256];
    pthread_mutex_t Portmutex;
    pthread_t pGetDatathread;

    MI_U16 u16Depth;
    MI_U16 u16UserDepth;
}ST_VpePortAttr_t;

typedef struct ST_VpeChannelAttr_s
{
    ST_VpePortAttr_t        stVpePortAttr[ST_MAX_PORT_NUM];
    MI_VPE_HDRType_e        eHdrType;
    MI_VPE_3DNR_Level_e     e3DNRLevel;
    MI_SYS_Rotate_e         eVpeRotate;
    MI_BOOL                 bChnMirror;
    MI_BOOL                 bChnFlip;
    MI_SYS_WindowRect_t     stOrgVpeChnCrop;
    MI_BOOL                 bEnLdc;
    MI_U32                  u32ChnPortMode;
    MI_VPE_RunningMode_e    eRunningMode;

    MI_SYS_WindowRect_t     stVpeChnCrop;

    char LdcCfgbin_Path[128];
    char IqCfgbin_Path[128];
    mi_eptz_config_param    tconfig_para;
    MI_U32 u32ViewNum;
    LDC_BIN_HANDLE          ldcBinBuffer[ST_MAX_LDC_VIEWNUM];
    MI_U32                  u32LdcBinSize[ST_MAX_LDC_VIEWNUM];
    MI_S32  s32Rot[ST_MAX_LDC_VIEWNUM];
}ST_VpeChannelAttr_t;

typedef struct ST_VencAttr_s
{
    MI_U32     u32BindVpeChn;
    MI_U32     u32BindVpePort;
    MI_SYS_BindType_e  eBindType;
    MI_U32  u32BindParam;

    MI_VENC_CHN vencChn;
    MI_VENC_ModType_e eType;
    MI_U32    u32Width;
    MI_U32    u32Height;
    char szStreamName[128];
    MI_BOOL bUsed;
    MI_BOOL bStart;
}ST_VencAttr_t;

MI_BOOL g_bPreviewByVenc = FALSE;
MI_BOOL g_bExit = FALSE;

ST_Sensor_Attr_t    gstSensorAttr[ST_MAX_SENSOR_NUM];
ST_Vif_Attr_t       gstVifAttr[ST_MAX_SENSOR_NUM];
ST_VpeChannelAttr_t gstVpeChnattr[ST_MAX_SENSOR_NUM];
ST_VencAttr_t       gstVencattr[ST_MAX_STREAM_NUM];
MI_DIVP_OutputPortAttr_t gstDivpPortAttr;
IPU_DlaInfo_S  gstDlaInfo;

void ST_Flush(void)
{
    char c;
    while((c = getchar()) != '\n' && c != EOF);
}

void *ST_OpenStream(char const *szStreamName, void *arg)
{
    MI_U32 i = 0;
    MI_S32 s32Ret = MI_SUCCESS;

    for(i = 0; i < ST_MAX_STREAM_NUM; i ++)
    {
        if(!strncmp(szStreamName, gstVencattr[i].szStreamName,
                    strlen(szStreamName)))
        {
            break;
        }
    }

    if(i >= ST_MAX_STREAM_NUM)
    {
        ST_ERR("not found this stream, \"%s\"", szStreamName);
        return NULL;
    }

    ST_VencAttr_t *pstVencAttr = &gstVencattr[i];

    s32Ret = MI_VENC_RequestIdr(pstVencAttr->vencChn, TRUE);

    ST_DBG("open stream \"%s\" success, chn:%d\n", szStreamName, pstVencAttr->vencChn);

    if(MI_SUCCESS != s32Ret)
    {
        ST_WARN("request IDR fail, error:%x\n", s32Ret);
    }

    return pstVencAttr;
}

MI_U32 u32GetCnt=0, u32ReleaseCnt=0;
int ST_VideoReadStream(void *handle, unsigned char *ucpBuf, int BufLen, struct timeval *p_Timestamp, void *arg)
{
    MI_SYS_BufInfo_t stBufInfo;
    MI_S32 s32Ret = MI_SUCCESS;
    MI_S32 len = 0;
    MI_U32 u32DevId = 0;
    MI_VENC_Stream_t stStream;
    MI_VENC_Pack_t stPack;
    MI_VENC_ChnStat_t stStat;
    MI_VENC_CHN vencChn ;

    if(handle == NULL)
    {
        return -1; 
    }

    ST_VencAttr_t *pstStreamInfo = (ST_VencAttr_t *)handle;

    vencChn = pstStreamInfo->vencChn;

    if(pstStreamInfo->bStart == FALSE)
        return 0;

    s32Ret = MI_VENC_GetChnDevid(vencChn, &u32DevId);

    if(MI_SUCCESS != s32Ret)
    {
        ST_INFO("MI_VENC_GetChnDevid %d error, %X\n", vencChn, s32Ret);
    }

    memset(&stBufInfo, 0x0, sizeof(MI_SYS_BufInfo_t));
    memset(&stStream, 0, sizeof(stStream));
    memset(&stPack, 0, sizeof(stPack));
    stStream.pstPack = &stPack;
    stStream.u32PackCount = 1;
    s32Ret = MI_VENC_Query(vencChn, &stStat);

    if(s32Ret != MI_SUCCESS || stStat.u32CurPacks == 0)
    {
        return 0;
    }

    s32Ret = MI_VENC_GetStream(vencChn, &stStream, 40);

    if(MI_SUCCESS == s32Ret)
    {
        u32GetCnt++;
        len = stStream.pstPack[0].u32Len;
        memcpy(ucpBuf, stStream.pstPack[0].pu8Addr, MIN(len, BufLen));

        s32Ret = MI_VENC_ReleaseStream(vencChn, &stStream);
        if(s32Ret != MI_SUCCESS)
        {
            ST_WARN("RELEASE venc buffer fail\n");
        }
        u32ReleaseCnt ++;
        return len;
    }

    return 0;
}

int ST_CloseStream(void *handle, void *arg)
{
    if(handle == NULL)
    {
        return -1;
    }

    ST_VencAttr_t *pstStreamInfo = (ST_VencAttr_t *)handle;

    ST_DBG("close \"%s\" success\n", pstStreamInfo->szStreamName);
    return 0;
}

MI_S32 ST_RtspServerStart(void)
{
    unsigned int rtspServerPortNum = RTSP_LISTEN_PORT;
    int iRet = 0;
    char *urlPrefix = NULL;
    int arraySize = ARRAY_SIZE(gstVencattr);
    ST_VencAttr_t *pstStreamAttr = gstVencattr;
    int i = 0;
    ServerMediaSession *mediaSession = NULL;
    ServerMediaSubsession *subSession = NULL;
    Live555RTSPServer *pRTSPServer = NULL;

    pRTSPServer = new Live555RTSPServer();

    if(pRTSPServer == NULL)
    {
        ST_ERR("malloc error\n");
        return -1;
    }

    iRet = pRTSPServer->SetRTSPServerPort(rtspServerPortNum);

    while(iRet < 0)
    {
        rtspServerPortNum++;

        if(rtspServerPortNum > 65535)
        {
            ST_INFO("Failed to create RTSP server: %s\n", pRTSPServer->getResultMsg());
            delete pRTSPServer;
            pRTSPServer = NULL;
            return -2;
        }

        iRet = pRTSPServer->SetRTSPServerPort(rtspServerPortNum);
    }

    urlPrefix = pRTSPServer->rtspURLPrefix();

    for(i = 0; i < arraySize; i ++)
    {
        if(pstStreamAttr[i].bUsed != TRUE)
            continue;

        printf("=================URL===================\n");
        printf("%s%s\n", urlPrefix, pstStreamAttr[i].szStreamName);
        printf("=================URL===================\n");

        pRTSPServer->createServerMediaSession(mediaSession,
                                              pstStreamAttr[i].szStreamName,
                                              NULL, NULL);

        if(pstStreamAttr[i].eType == E_MI_VENC_MODTYPE_H264E)
        {
            subSession = WW_H264VideoFileServerMediaSubsession::createNew(
                             *(pRTSPServer->GetUsageEnvironmentObj()),
                             pstStreamAttr[i].szStreamName,
                             ST_OpenStream,
                             ST_VideoReadStream,
                             ST_CloseStream, 30);
        }
        else if(pstStreamAttr[i].eType == E_MI_VENC_MODTYPE_H265E)
        {
            subSession = WW_H265VideoFileServerMediaSubsession::createNew(
                             *(pRTSPServer->GetUsageEnvironmentObj()),
                             pstStreamAttr[i].szStreamName,
                             ST_OpenStream,
                             ST_VideoReadStream,
                             ST_CloseStream, 30);
        }
        else if(pstStreamAttr[i].eType == E_MI_VENC_MODTYPE_JPEGE)
        {
            subSession = WW_JPEGVideoFileServerMediaSubsession::createNew(
                             *(pRTSPServer->GetUsageEnvironmentObj()),
                             pstStreamAttr[i].szStreamName,
                             ST_OpenStream,
                             ST_VideoReadStream,
                             ST_CloseStream, 30);
        }

        pRTSPServer->addSubsession(mediaSession, subSession);
        pRTSPServer->addServerMediaSession(mediaSession);
    }

    pRTSPServer->Start();

    g_pRTSPServer = pRTSPServer;

    return 0;
}

MI_S32 ST_RtspServerStop(void)
{
    if(g_pRTSPServer)
    {
        g_pRTSPServer->Join();
        delete g_pRTSPServer;
        g_pRTSPServer = NULL;
    }

    return 0;
}

MI_S32 ST_WriteOneFrame(FILE *fp, int offset, char *pDataFrame, int line_offset, int line_size, int lineNumber)
{
    int size = 0;
    int i = 0;
    char *pData = NULL;
    int yuvSize = line_size;
    MI_S32 s32Ret = -1;

    for(i = 0; i < lineNumber; i++)
    {
        pData = pDataFrame + line_offset * i;
        yuvSize = line_size;

        do
        {
            if(yuvSize < 256)
            {
                size = yuvSize;
            }
            else
            {
                size = 256;
            }

            size = fwrite(pData, 1, size, fp);

            if(size == 0)
            {
                break;
            }
            else if(size < 0)
            {
                break;
            }

            pData += size;
            yuvSize -= size;
        }
        while(yuvSize > 0);
        s32Ret = MI_SUCCESS;
    }

    return s32Ret;
}

MI_S32 ST_GetVpeOutputData(MI_U32 u32SensorNum)
{
    MI_S32  s32Portid = 0;
    MI_S32  s32Channelid = 0;
    MI_S32  s32DumpBuffNum =0;
    char sFilePath[128];
    time_t stTime = 0;
    
    ST_VpePortAttr_t *pstVpePortAttr = NULL;
    MI_VPE_PortMode_t stVpePortMode;
    memset(&stVpePortMode, 0x0, sizeof(MI_VPE_PortMode_t));
    memset(&stTime, 0, sizeof(stTime));

    if(u32SensorNum > 1)
    {
        printf("select channel id:");
        scanf("%d", &s32Channelid);
        ST_Flush();

        if(s32Channelid >= ST_MAX_SENSOR_NUM)
        {
            printf("chnid %d > max %d \n", s32Channelid, ST_MAX_SENSOR_NUM);
            return 0;
        }
    }
    else
    {
        s32Channelid = 0;
    }

    printf("select port id:");
    scanf("%d", &s32Portid);
    ST_Flush();

    printf("Dump Buffer Num:");
    scanf("%d", &s32DumpBuffNum);
    ST_Flush();

    printf("write file path:\n");
    scanf("%s", sFilePath);
    ST_Flush();

    if(s32Portid >= ST_MAX_PORT_NUM)
    {
        printf("port %d, not valid 0~3 \n", s32Portid);
        return 0;
    }

    pstVpePortAttr = &gstVpeChnattr[s32Channelid].stVpePortAttr[s32Portid];

    if(pstVpePortAttr->bUsed != TRUE)
    {
        printf("port %d, not valid \n", s32Portid);
        return 0;
    }

    pthread_mutex_lock(&pstVpePortAttr->Portmutex);

    STCHECKRESULT(MI_VPE_GetPortMode(0, s32Portid, &stVpePortMode));
    sprintf(pstVpePortAttr->FilePath, "%s/vpeport%d_%dx%d_pixel%d_%ld.raw", sFilePath, s32Portid, stVpePortMode.u16Width, stVpePortMode.u16Height, stVpePortMode.ePixelFormat, time(&stTime));
    pstVpePortAttr->s32DumpBuffNum = s32DumpBuffNum;

    pthread_mutex_unlock(&pstVpePortAttr->Portmutex);

    return 0;

}

MI_S32 ST_VpeDisablePort(MI_U32 u32SensorNum)
{
    MI_S32  s32Portid = 0;
    ST_VpePortAttr_t *pstVpePortAttr = NULL;

    MI_S32  s32Channelid = 0;
    if(u32SensorNum > 1)
    {
        printf("select channel id:");
        scanf("%d", &s32Channelid);
        ST_Flush();

        if(s32Channelid >= ST_MAX_SENSOR_NUM)
        {
            printf("chnid %d > max %d \n", s32Channelid, ST_MAX_SENSOR_NUM);
            return 0;
        }
    }
    else
    {
        s32Channelid = 0;
    }

    printf("select port id:");
    scanf("%d", &s32Portid);
    ST_Flush();

    if(s32Portid >= ST_MAX_PORT_NUM)
    {
        printf("port %d, not valid 0~3 \n", s32Portid);
        return 0;
    }

    pstVpePortAttr = &gstVpeChnattr[s32Channelid].stVpePortAttr[s32Portid];
    pstVpePortAttr->bUsed = FALSE;

    STCHECKRESULT(MI_VPE_DisablePort(s32Channelid, s32Portid));

    return 0;
}

MI_S32 ST_GetVencOut()
{
    MI_S32 s32Ret = MI_SUCCESS;
    MI_VENC_Stream_t stStream;
    MI_VENC_Pack_t stPack;
    MI_U32 u32BypassCnt = 0;
    MI_S32 s32DumpBuffNum = 0;
    MI_S32  VencChn = 0;
    MI_VENC_Pack_t *pstPack = NULL;
    MI_U32  i=0;
    FILE *fp = NULL;
    char FilePath[256];
    char sFilePath[128];
    time_t stTime = 0;
    MI_VENC_ChnStat_t stStat;
    MI_VENC_ChnAttr_t stVencAttr;
    memset(&stVencAttr, 0x0, sizeof(MI_VENC_ChnAttr_t));

    memset(&stStat, 0x0, sizeof(MI_VENC_ChnStat_t));
    memset(&stStream, 0, sizeof(MI_VENC_Stream_t));
    memset(&stPack, 0, sizeof(MI_VENC_Pack_t));
    stStream.pstPack = &stPack;
    stStream.u32PackCount = 1;

    printf("select venc chn id:");
    scanf("%d", &VencChn);
    ST_Flush();

    printf("Dump Buffer Num:");
    scanf("%d", &s32DumpBuffNum);
    ST_Flush();

    printf("write file path:\n");
    scanf("%s", sFilePath);
    ST_Flush();

    s32Ret = MI_VENC_GetChnAttr(VencChn, &stVencAttr);

    if(s32Ret != MI_SUCCESS)
    {
        printf("channel %d, not valid \n", VencChn);
        return 0;
    }

    if(stVencAttr.stVeAttr.eType == E_MI_VENC_MODTYPE_JPEGE)
        sprintf(FilePath, "%s/venc_%ld.jpg", sFilePath, time(&stTime));
    else
        sprintf(FilePath, "%s/venc_%ld.es", sFilePath, time(&stTime));

    fp = fopen(FilePath,"wb");

    if(fp == NULL)
    {
        printf("open file %s fail \n",FilePath);
        return 0;
    }

    while(s32DumpBuffNum > 0)
    {
        s32Ret = MI_VENC_Query(VencChn, &stStat);
        if(s32Ret != MI_SUCCESS || stStat.u32CurPacks == 0)
        {
            usleep(1 * 1000);
            continue;
        }

        s32Ret = MI_VENC_GetStream(VencChn, &stStream, 100);
        if(MI_SUCCESS == s32Ret)
        {
            if (0 == u32BypassCnt)
            {
                printf("##########Start to write file %s, id %d !!!#####################\n",FilePath, s32DumpBuffNum);

                for(i = 0; i < stStream.u32PackCount; i ++)
                {
                    pstPack = &stStream.pstPack[i];
                    fwrite(pstPack->pu8Addr + pstPack->u32Offset, pstPack->u32Len - pstPack->u32Offset, 1, fp);
                }
                printf("##########End to write file id %d !!!#####################\n", s32DumpBuffNum);

                s32DumpBuffNum --;
            }
            else
            {
                u32BypassCnt--;
                printf("Bypasss frame %d !\n", u32BypassCnt);
            }
            s32Ret = MI_VENC_ReleaseStream(VencChn, &stStream);
            if(MI_SUCCESS != s32Ret)
            {
                ST_DBG("MI_VENC_ReleaseStream fail, ret:0x%x\n", s32Ret);
            }
        }

        usleep(200 * 1000);
    }
    fclose(fp);

    return 0;
}

MI_S32 ST_ReadLdcTableBin(const char *pConfigPath, LDC_BIN_HANDLE *tldc_bin, MI_U32 *pu32BinSize)
{
    struct stat statbuff;
    MI_U8 *pBufData = NULL;
    MI_S32 s32Fd = 0;
    MI_U32 u32Size = 0;

    if (pConfigPath == NULL)
    {
        ST_ERR("File path null!\n");
        return MI_ERR_LDC_ILLEGAL_PARAM;
    }
    printf("Read file %s\n", pConfigPath);
    memset(&statbuff, 0, sizeof(struct stat));
    if(stat(pConfigPath, &statbuff) < 0)
    {
        ST_ERR("Bb table file not exit!\n");
        return MI_ERR_LDC_ILLEGAL_PARAM;
    }
    else
    {
        if (statbuff.st_size == 0)
        {
            ST_ERR("File size is zero!\n");
            return MI_ERR_LDC_ILLEGAL_PARAM;
        }
        u32Size = statbuff.st_size;
    }
    s32Fd = open(pConfigPath, O_RDONLY);
    if (s32Fd < 0)
    {
        ST_ERR("Open file[%d] error!\n", s32Fd);
        return MI_ERR_LDC_ILLEGAL_PARAM;
    }
    pBufData = (MI_U8 *)malloc(u32Size);
    if (!pBufData)
    {
        ST_ERR("Malloc error!\n");
        close(s32Fd);

        return MI_ERR_LDC_ILLEGAL_PARAM;
    }

    memset(pBufData, 0, u32Size);
    read(s32Fd, pBufData, u32Size);
    close(s32Fd);

    *tldc_bin = pBufData;
    *pu32BinSize = u32Size;

    printf("%d: read buffer %p \n",__LINE__, pBufData);
    printf("%d: &bin address %p, *binbuffer %p \n",__LINE__, tldc_bin, *tldc_bin);

    //free(pBufData);

    return MI_SUCCESS;
}

MI_S32 ST_GetLdcCfgViewNum(mi_LDC_MODE eLdcMode)
{
    MI_U32 u32ViewNum = 0;

    switch(eLdcMode)
    {
        case LDC_MODE_1R:
        case LDC_MODE_1P_CM:
        case LDC_MODE_1P_WM:
        case LDC_MODE_1O:
        case LDC_MODE_1R_WM:
            u32ViewNum = 1;
            break;
        case LDC_MODE_2P_CM:
        case LDC_MODE_2P_DM:
            u32ViewNum = 2;
            break;
        case LDC_MODE_4R_CM:
        case LDC_MODE_4R_WM:
            u32ViewNum = 4;
            break;
        default:
            printf("########### ldc mode %d err \n", eLdcMode);
            break;
    }

    printf("view num %d \n", u32ViewNum);
    return u32ViewNum;
}

MI_S32 ST_Parse_LibarayCfgFilePath(char *pLdcLibCfgPath, mi_eptz_config_param *ptconfig_para)
{
    mi_eptz_err err_state = MI_EPTZ_ERR_NONE;

    printf("cfg file path %s\n", pLdcLibCfgPath);
    //check cfg file, in out path with bin position

    err_state = mi_eptz_config_parse(pLdcLibCfgPath, ptconfig_para);
    if (err_state != MI_EPTZ_ERR_NONE)
    {
        printf("confile file read error: %d\n", err_state);
        return err_state;
    }

    printf("ldc mode %d \n", ptconfig_para->ldc_mode);
    return 0;
}
    
MI_S32 ST_Libaray_CreatBin(MI_S32 s32ViewId, mi_eptz_config_param *ptconfig_para, LDC_BIN_HANDLE *ptldc_bin, MI_U32 *pu32LdcBinSize, MI_S32 s32Rot)
{
    unsigned char* pWorkingBuffer;
    int working_buf_len = 0;
    mi_eptz_err err_state = MI_EPTZ_ERR_NONE;
    EPTZ_DEV_HANDLE eptz_handle = NULL;

    mi_eptz_para teptz_para;
    memset(&teptz_para, 0x0, sizeof(mi_eptz_para));

    printf("view %d rot %d\n", s32ViewId, s32Rot);

    working_buf_len = mi_eptz_get_buffer_info(ptconfig_para);
    pWorkingBuffer = (unsigned char*)malloc(working_buf_len);
    if (pWorkingBuffer == NULL)
    {
        printf("buffer allocate error\n");
        return MI_EPTZ_ERR_MEM_ALLOCATE_FAIL;
    }
    
   // printf("%s:%d working_buf_len %d \n", __FUNCTION__, __LINE__, working_buf_len);

    //EPTZ init
    teptz_para.ptconfig_para = ptconfig_para; //ldc configure
    
  //  printf("%s:%d ptconfig_para %p, pWorkingBuffer %p, working_buf_len %d\n", __FUNCTION__, __LINE__, teptz_para.ptconfig_para,
    //    pWorkingBuffer, working_buf_len);
    
    eptz_handle =  mi_eptz_runtime_init(pWorkingBuffer, working_buf_len, &teptz_para);
    if (eptz_handle == NULL)
    {
        printf("EPTZ init error\n");
        return MI_EPTZ_ERR_NOT_INIT;
    }

    teptz_para.pan = 0;
    teptz_para.tilt = -60;
    if(ptconfig_para->ldc_mode == 1)
        teptz_para.tilt = 60;
    teptz_para.rotate = s32Rot;
    teptz_para.zoom = 150.00;
    teptz_para.out_rot = 0;
    teptz_para.view_index = s32ViewId;

    //Gen bin files from 0 to 360 degree
    switch (ptconfig_para->ldc_mode)
    {
        case LDC_MODE_4R_CM:  //LDC_MODE_4R_CM/Desk, if in desk mount mode, tilt is nagetive.
            teptz_para.view_index = s32ViewId;
            teptz_para.pan = 0;
            teptz_para.tilt = -50; //In CM mode, tilt is positive, but in desk mode, tilt is negative.
            teptz_para.rotate = s32Rot;
            teptz_para.zoom = 150;
            teptz_para.out_rot = 0;
            err_state = (mi_eptz_err)mi_eptz_runtime_map_gen(eptz_handle,(mi_eptz_para*)&teptz_para, ptldc_bin, (int *)pu32LdcBinSize);
            if (err_state != MI_EPTZ_ERR_NONE)
            {
                printf("[EPTZ ERR] =  %d !! \n", err_state);
            }
            break;
        case LDC_MODE_4R_WM:  //LDC_MODE_4R_WM
            teptz_para.view_index = s32ViewId;
            teptz_para.pan = 0;
            teptz_para.tilt = 50; //In CM mode, tilt is positive, but in desk mode, tilt is negative.
            teptz_para.rotate = s32Rot;
            teptz_para.zoom = 150;
            teptz_para.out_rot = 0;
            err_state = (mi_eptz_err)mi_eptz_runtime_map_gen(eptz_handle,(mi_eptz_para*)&teptz_para, ptldc_bin, (int *)pu32LdcBinSize);
            if (err_state != MI_EPTZ_ERR_NONE)
            {
                printf("[EPTZ ERR] =  %d !! \n", err_state);
            }
            break;
        case LDC_MODE_1R:  //LDC_MODE_1R CM/Desk,  if in desk mount mode, tilt is negative.
            teptz_para.view_index = s32ViewId;
            teptz_para.pan = 0;
            teptz_para.tilt = 0; //In CM mode, tilt is positive, but in desk mode, tilt is negative.
            teptz_para.rotate = s32Rot;
            teptz_para.zoom = 150;
            teptz_para.out_rot = 0;
            err_state = (mi_eptz_err)mi_eptz_runtime_map_gen(eptz_handle,(mi_eptz_para*)&teptz_para, ptldc_bin, (int *)pu32LdcBinSize);
            if (err_state != MI_EPTZ_ERR_NONE)
            {
                printf("[EPTZ ERR] =  %d !! \n", err_state);
            }
            break;
        case LDC_MODE_1R_WM:  //LDC_MODE_1R WM
            teptz_para.view_index = s32ViewId;
            teptz_para.pan = 0;
            teptz_para.tilt = 50; //In CM mode, tilt is positive, but in desk mode, tilt is negative.
            teptz_para.rotate = s32Rot;
            teptz_para.zoom = 150;
            teptz_para.out_rot = 0;

            err_state = (mi_eptz_err)mi_eptz_runtime_map_gen(eptz_handle,(mi_eptz_para*)&teptz_para, ptldc_bin, (int *)pu32LdcBinSize);
            if (err_state != MI_EPTZ_ERR_NONE)
            {
                printf("[EPTZ ERR] =  %d !! \n", err_state);
            }
            break;

        case LDC_MODE_2P_CM:  //LDC_MODE_2P_CM
        case LDC_MODE_2P_DM:  //LDC_MODE_2P_DM
        case LDC_MODE_1P_CM:  //LDC_MODE_1P_CM
            //Set the input parameters for donut mode
            if(s32Rot > 180)
            {
                //Degree 180 ~ 360
                teptz_para.view_index = s32ViewId;
                teptz_para.r_inside = 550;
                teptz_para.r_outside = 10;
                teptz_para.theta_start = s32Rot;
                teptz_para.theta_end = s32Rot+360;
            }
            else
            {
                //Degree 180 ~ 0
                teptz_para.view_index = s32ViewId;
                teptz_para.r_inside = 10;
                teptz_para.r_outside = 550;
                teptz_para.theta_start = s32Rot;
                teptz_para.theta_end = s32Rot+360;
            }
            err_state = (mi_eptz_err)mi_donut_runtime_map_gen(eptz_handle, (mi_eptz_para*)&teptz_para, ptldc_bin, (int *)pu32LdcBinSize);
            if (err_state != MI_EPTZ_ERR_NONE)
            {
                printf("[EPTZ ERR] =  %d !! \n", err_state);
            }
            break;
        case LDC_MODE_1P_WM:  //LDC_MODE_1P wall mount.
            teptz_para.view_index = s32ViewId;
            teptz_para.pan = 0;
            teptz_para.tilt = 0;
            teptz_para.zoom_h = 100;
            teptz_para.zoom_v = 100;
            err_state = (mi_eptz_err)mi_erp_runtime_map_gen(eptz_handle,(mi_eptz_para*)&teptz_para, ptldc_bin, (int *)pu32LdcBinSize);
            if (err_state != MI_EPTZ_ERR_NONE)
            {
                printf("[EPTZ ERR] =  %d !! \n", err_state);
            }
            break;
        case LDC_MODE_1O:    //bypass mode
            teptz_para.view_index = 0; //view index
            printf("begin mi_bypass_runtime_map_gen \n");
            err_state = (mi_eptz_err)mi_bypass_runtime_map_gen(eptz_handle, (mi_eptz_para*)&teptz_para, ptldc_bin, (int *)pu32LdcBinSize);
            if (err_state != MI_EPTZ_ERR_NONE)
            {
                printf("[MODE %d ERR] =  %d !! \n", LDC_MODE_1O, err_state);
                return err_state;
            }
            
            printf("end mi_bypass_runtime_map_gen\n");
            break;
        default :
             printf("********************err ldc mode %d \n", ptconfig_para->ldc_mode);
             return 0;
    }
#if 0
    FILE *fp = NULL;
    fp = fopen("/mnt/ldc.bin","wb");
    fwrite(ptldc_bin, *pu32LdcBinSize, 1, fp);
    fclose(fp);
#endif
    //Free bin buffer
    /*
    err_state = mi_eptz_buffer_free((LDC_BIN_HANDLE)tldc_bin);
    if (err_state != MI_EPTZ_ERR_NONE)
    {
        printf("[MI EPTZ ERR] =  %d !! \n", err_state);
    }*/
    //release working buffer
    if(pWorkingBuffer)
    {
    	free(pWorkingBuffer);
		pWorkingBuffer = NULL;
    }
    return 0;
}
    
MI_S32 ST_GetLdcBinBuffer(char *pCfgFilePath, mi_eptz_config_param *ptconfig_para, MI_U32 *pu32ViewNum, LDC_BIN_HANDLE *ptldc_bin, MI_U32 *pu32LdcBinSize, MI_S32 *ps32Rot)
{
    MI_U8 i =0;
    MI_U32 u32ViewNum = 0;
    MI_S32 s32Cfg_Param = 0;
    MI_U16 u16PathSize = strlen(pCfgFilePath);
    std::string path = pCfgFilePath;
    std::string sub;
    MI_U8 tmp = path.rfind(".");
    sub = path.substr(tmp, u16PathSize);
    const char *p = sub.data();

    if(!strcmp(p, ".bin"))
    {
        s32Cfg_Param = 1;
    }
    else if(!strcmp(p, ".cfg"))
    {
        s32Cfg_Param = 0;
    }
    else
    {
        printf("path name %s err, need use .bin or .cfg \n", pCfgFilePath);
    }

    if(s32Cfg_Param == 0)
    {
        ST_Parse_LibarayCfgFilePath(pCfgFilePath, ptconfig_para);
        u32ViewNum = ST_GetLdcCfgViewNum(ptconfig_para->ldc_mode);
        for(i=0; i<u32ViewNum; i++)
        {
            ST_Libaray_CreatBin(i, ptconfig_para, &ptldc_bin[i], &pu32LdcBinSize[i], ps32Rot[i]);
        }
        *pu32ViewNum = u32ViewNum;
    }
    else
    {
        ST_ReadLdcTableBin(pCfgFilePath, ptldc_bin, pu32LdcBinSize);
        *pu32ViewNum = 1;
    }

    return 0;
}


MI_S32 ST_SetLdcOnOff(MI_U32 u32SensorNum)
{
    MI_S32 s32LdcOnoff = 0;
    MI_VPE_ChannelPara_t stVpeChnParam;
    memset(&stVpeChnParam, 0x0, sizeof(MI_VPE_ChannelPara_t));

    MI_S32  s32Channelid = 0;
    if(u32SensorNum > 1)
    {
        printf("select channel id:");
        scanf("%d", &s32Channelid);
        ST_Flush();

        if(s32Channelid >= ST_MAX_SENSOR_NUM)
        {
            printf("chnid %d > max %d \n", s32Channelid, ST_MAX_SENSOR_NUM);
            return 0;
        }
    }
    else
    {
        s32Channelid = 0;
    }

    ST_VpeChannelAttr_t *pstVpeChnattr = &gstVpeChnattr[s32Channelid];

    printf("Set Ldc ON(1), OFF(0): \n");
    scanf("%d", &s32LdcOnoff);
    ST_Flush();

    if(s32LdcOnoff == TRUE && pstVpeChnattr->bEnLdc == FALSE)
    {
        printf("ldc onoff %d, before enldc %d need set bin path \n", s32LdcOnoff, pstVpeChnattr->bEnLdc);
        printf("set Ldc libaray cfg path:  ");
        scanf("%s", pstVpeChnattr->LdcCfgbin_Path);
        ST_Flush();

        ST_ReadLdcTableBin(pstVpeChnattr->LdcCfgbin_Path, &pstVpeChnattr->ldcBinBuffer[0], &pstVpeChnattr->u32LdcBinSize[0]);
    }

    /************************************************
    Step1: Stop Vpe (Wait driver all buffer done)
    *************************************************/
    STCHECKRESULT(MI_VPE_StopChannel(s32Channelid));

    /************************************************
    Step2: Set Ldc on/off Param
    *************************************************/
    STCHECKRESULT(MI_VPE_GetChannelParam(s32Channelid, &stVpeChnParam));
    printf("get channel param  benldc %d, bmirror %d, bflip %d, e3dnrlevel %d, hdrtype %d \n", 
        stVpeChnParam.bEnLdc, stVpeChnParam.bMirror,stVpeChnParam.bFlip,stVpeChnParam.e3DNRLevel,stVpeChnParam.eHDRType);

    stVpeChnParam.bEnLdc = s32LdcOnoff;

    STCHECKRESULT(MI_VPE_SetChannelParam(s32Channelid, &stVpeChnParam));

    /************************************************
    Step3: if Ldc First On Set Ldc Bin
    *************************************************/
    if(s32LdcOnoff == TRUE && pstVpeChnattr->bEnLdc == FALSE)
    {
        STCHECKRESULT(MI_VPE_LDCBegViewConfig(s32Channelid));

        STCHECKRESULT(MI_VPE_LDCSetViewConfig(s32Channelid, pstVpeChnattr->ldcBinBuffer[0], pstVpeChnattr->u32LdcBinSize[0]));

        STCHECKRESULT(MI_VPE_LDCEndViewConfig(s32Channelid));

        //free(pstVpeChnattr->ldcBinBuffer);
        if(mi_eptz_buffer_free(pstVpeChnattr->ldcBinBuffer[0]) != MI_EPTZ_ERR_NONE)
        {
            printf("[MI EPTZ ERR]   %d !! \n", __LINE__);
        }

        pstVpeChnattr->bEnLdc = TRUE;
    }

    /************************************************
    Step4: Start Vpe
    *************************************************/
    STCHECKRESULT(MI_VPE_StartChannel(s32Channelid));

    return 0;
}

void * ST_GetVpeOutputDataThread(void * args)
{
    MI_SYS_BufInfo_t stBufInfo;
    MI_SYS_BUF_HANDLE hHandle;
    MI_U8  u8Params = *((MI_U8 *)(args));
    MI_U8  u8Chnid = u8Params / ST_MAX_PORT_NUM;
    MI_U8  u8Portid = u8Params % ST_MAX_PORT_NUM;
    FILE *fp = NULL;
    ST_VpePortAttr_t *pstVpePortAttr = &gstVpeChnattr[u8Chnid].stVpePortAttr[u8Portid];
    MI_BOOL bFileOpen = FALSE;

    MI_SYS_ChnPort_t stChnPort;

    memset(&stChnPort, 0x0, sizeof(MI_SYS_ChnPort_t));
    stChnPort.eModId = E_MI_MODULE_ID_VPE;
    stChnPort.u32DevId = 0;
    stChnPort.u32ChnId = u8Chnid;
    stChnPort.u32PortId = u8Portid;

    if(pstVpePortAttr->bUsed == TRUE)
    {
        MI_SYS_SetChnOutputPortDepth(&stChnPort, 1, 4);
    }

    while (!g_bExit)
    {
        pthread_mutex_lock(&pstVpePortAttr->Portmutex);

        if(pstVpePortAttr->s32DumpBuffNum > 0 && bFileOpen == FALSE)
        {
            fp = fopen(pstVpePortAttr->FilePath ,"wb");
            if(fp == NULL)
            {
                printf("file %s open fail\n", pstVpePortAttr->FilePath);
                pstVpePortAttr->s32DumpBuffNum = 0;\
                pthread_mutex_unlock(&pstVpePortAttr->Portmutex);
                continue;
            }
            else
            {
                bFileOpen = TRUE;
            }
        }

        if(pstVpePortAttr->bUsed == TRUE)
        {
            if (MI_SUCCESS == MI_SYS_ChnOutputPortGetBuf(&stChnPort, &stBufInfo, &hHandle))
            {
                //printf("get out success \n");
                if(pstVpePortAttr->s32DumpBuffNum > 0)
                {
                    pstVpePortAttr->s32DumpBuffNum--;
                    printf(
"=======begin writ port %d file id %d, file path %s, bufsize %d, stride %d, height %d\n", u8Portid, pstVpePortAttr->s32DumpBuffNum, pstVpePortAttr->FilePath, 
                        stBufInfo.stFrameData.u32BufSize,stBufInfo.stFrameData.u32Stride[0], stBufInfo.stFrameData.u16Height);

                    fwrite(stBufInfo.stFrameData.pVirAddr[0], stBufInfo.stFrameData.u32BufSize, 1, fp);
                    printf(
"=======end   writ port %d file id %d, file path %s \n", u8Portid, pstVpePortAttr->s32DumpBuffNum, pstVpePortAttr->FilePath);
                }

                //printf("begin release out \n");
                MI_SYS_ChnOutputPortPutBuf(hHandle);
                //printf("end release out \n");
            }
        }

        if(bFileOpen == TRUE && pstVpePortAttr->s32DumpBuffNum == 0)
        {
            fclose(fp);
            bFileOpen = FALSE;
        }

        pthread_mutex_unlock(&pstVpePortAttr->Portmutex);
        usleep(10*1000);
    }

    return NULL;
}

void *ST_IQthread(void * args)
{
    MI_VIF_ChnPortAttr_t stVifAttr;
    MI_VPE_ChannelPara_t stVpeParam;
    MI_VPE_HDRType_e  eLastHdrType = E_MI_VPE_HDR_TYPE_MAX;
    MI_ISP_IQ_PARAM_INIT_INFO_TYPE_t status;
    MI_U8  u8ispreadycnt = 0;

    memset(&stVifAttr, 0x0, sizeof(MI_VIF_ChnPortAttr_t));
    memset(&stVpeParam, 0x0, sizeof(MI_VPE_ChannelPara_t));

    MI_IQSERVER_Open(1920, 1080, 0);

    while(1)
    {
        if(u8ispreadycnt > 100)
        {
            printf("%s:%d, isp ready time out \n", __FUNCTION__, __LINE__);
            u8ispreadycnt = 0;
        }

        MI_ISP_IQ_GetParaInitStatus(0, &status);
        if(status.stParaAPI.bFlag != 1)
        {
            usleep(10*1000);
            u8ispreadycnt++;
            continue;
        }

        u8ispreadycnt = 0;

        MI_VPE_GetChannelParam(0, &stVpeParam);
        if(eLastHdrType != stVpeParam.eHDRType)
        {
            printf("hdr type change before %d, current %d, load api bin\n", eLastHdrType, stVpeParam.eHDRType);
            MI_ISP_API_CmdLoadBinFile(0, (char *)((stVpeParam.eHDRType>0) ? HDR_PATH : NONHDR_PATH),  1234);
        }
        eLastHdrType = stVpeParam.eHDRType;

        usleep(10*1000);
    }

    return  NULL;
}

void *ST_SendVpeBufthread(void * args)
{
    MI_SYS_ChnPort_t stVpeChnInput;
    MI_SYS_BUF_HANDLE hHandle = 0;
    MI_SYS_BufConf_t stBufConf;
    MI_SYS_BufInfo_t stBufInfo;
    struct timeval stTv;
    MI_U16 u16Width = 1920, u16Height = 1080;
    FILE *fp = NULL;

    memset(&stVpeChnInput, 0x0, sizeof(MI_SYS_ChnPort_t));
    memset(&stBufConf, 0x0, sizeof(MI_SYS_BufConf_t));
    memset(&stBufInfo, 0x0, sizeof(MI_SYS_BufInfo_t));

    stVpeChnInput.eModId = E_MI_MODULE_ID_VPE;
    stVpeChnInput.u32DevId = 0;
    stVpeChnInput.u32ChnId = 0;
    stVpeChnInput.u32PortId = 0;

    fp = fopen("/mnt/vpeport0_1920x1080_pixel0_737.raw","rb");
    if(fp == NULL)
    {
        printf("file %s open fail\n", "/mnt/vpeport0_1920x1080_pixel0_737.raw");
        return 0;
    }

    while(1)
    {
        stBufConf.eBufType = E_MI_SYS_BUFDATA_FRAME;
        gettimeofday(&stTv, NULL);
        stBufConf.u64TargetPts = stTv.tv_sec*1000000 + stTv.tv_usec;
        stBufConf.stFrameCfg.eFormat = E_MI_SYS_PIXEL_FRAME_YUV422_YUYV;
        stBufConf.stFrameCfg.eFrameScanMode = E_MI_SYS_FRAME_SCAN_MODE_PROGRESSIVE;
        stBufConf.stFrameCfg.u16Width = u16Width;
        stBufConf.stFrameCfg.u16Height = u16Height;

        if(MI_SUCCESS  == MI_SYS_ChnInputPortGetBuf(&stVpeChnInput,&stBufConf,&stBufInfo,&hHandle,0))
        {
            if(fread(stBufInfo.stFrameData.pVirAddr[0], u16Width*u16Height*2, 1, fp) <= 0)
            {
                fseek(fp, 0, SEEK_SET);
            }

            MI_SYS_ChnInputPortPutBuf(hHandle,&stBufInfo, FALSE);
        }
    }
}

MI_S32 ST_BaseModuleInit(MI_SNR_PAD_ID_e eSnrPad)
{
    MI_SNR_PAD_ID_e eSnrPadId = eSnrPad;
    MI_SNR_PADInfo_t  stPad0Info;
    MI_SNR_PlaneInfo_t stSnrPlane0Info;
    MI_U32 u32CapWidth = 0, u32CapHeight = 0;
    memset(&stPad0Info, 0x0, sizeof(MI_SNR_PADInfo_t));
    memset(&stSnrPlane0Info, 0x0, sizeof(MI_SNR_PlaneInfo_t));

    MI_SYS_PixelFormat_e ePixFormat;
    MI_VIF_DEV vifDev = gstSensorAttr[eSnrPad].u32BindVifDev;
    MI_VIF_CHN vifChn = vifDev*4;
    MI_VIF_PORT vifPort = 0;
    ST_Vif_Attr_t *pstVifDevAttr = &gstVifAttr[vifDev];

    MI_VPE_CHANNEL vpechn = gstVifAttr[vifDev].u32BindVpeChan;
    ST_VpeChannelAttr_t *pstVpeChnattr = &gstVpeChnattr[vpechn];

    MI_U8 i=0;


    /************************************************
    Step1:  init SYS
    *************************************************/
    STCHECKRESULT(ST_Sys_Init());

    /************************************************
    Step2:  init Sensor
    *************************************************/
    if(E_MI_VPE_HDR_TYPE_OFF== pstVpeChnattr->eHdrType 
        || E_MI_VPE_HDR_TYPE_EMBEDDED == pstVpeChnattr->eHdrType
        || E_MI_VPE_HDR_TYPE_LI== pstVpeChnattr->eHdrType)
    {
        STCHECKRESULT(MI_SNR_SetPlaneMode(eSnrPad, FALSE));
    }
    else
    {
        STCHECKRESULT(MI_SNR_SetPlaneMode(eSnrPad, TRUE));
    }

    MI_U32 u32ResCount =0;
    MI_U8 u8ResIndex =0;
    MI_SNR_Res_t stRes;
    MI_U8 u8ChocieRes =gstSensorAttr[eSnrPad].u8ResIndex;
    memset(&stRes, 0x0, sizeof(MI_SNR_Res_t));

    STCHECKRESULT(MI_SNR_QueryResCount(eSnrPadId, &u32ResCount));
    for(u8ResIndex=0; u8ResIndex < u32ResCount; u8ResIndex++)
    {
        STCHECKRESULT(MI_SNR_GetRes(eSnrPadId, u8ResIndex, &stRes));
        printf("index %d, Crop(%d,%d,%d,%d), outputsize(%d,%d), maxfps %d, minfps %d, ResDesc %s\n",
        u8ResIndex,
        stRes.stCropRect.u16X, stRes.stCropRect.u16Y, stRes.stCropRect.u16Width,stRes.stCropRect.u16Height,
        stRes.stOutputSize.u16Width, stRes.stOutputSize.u16Height,
        stRes.u32MaxFps,stRes.u32MinFps,
        stRes.strResDesc);
    }
    if(u8ChocieRes >= u32ResCount && u8ChocieRes != 0xff)
    {
        printf("res set err  %d > =cnt %d\n", u8ChocieRes, u32ResCount);
        return TRUE;
    }
    else if(u8ChocieRes == 0xff)
    {
        //MI_S32 s32Input =0;
        printf("choice which resolution use, cnt %d\n", u32ResCount);
        do
        {
          //  scanf("%d", &s32Input);
            u8ChocieRes = 0;//(MI_U8)s32Input;
          //  ST_Flush();
            STCHECKRESULT(MI_SNR_QueryResCount(eSnrPadId, &u32ResCount));
            if(u8ChocieRes >= u32ResCount)
            {
                printf("choice err res %d > =cnt %d\n", u8ChocieRes, u32ResCount);
            }
        }while(u8ChocieRes >= u32ResCount);
        printf("You select %d res\n", u8ChocieRes);
    }
    printf("Rest %d\n", u8ChocieRes);

    STCHECKRESULT(MI_SNR_SetRes(eSnrPadId,u8ChocieRes));
    STCHECKRESULT(MI_SNR_Enable(eSnrPadId));

    STCHECKRESULT(MI_SNR_GetPadInfo(eSnrPadId, &stPad0Info));
    STCHECKRESULT(MI_SNR_GetPlaneInfo(eSnrPadId, 0, &stSnrPlane0Info));

    u32CapWidth = stSnrPlane0Info.stCapRect.u16Width;
    u32CapHeight = stSnrPlane0Info.stCapRect.u16Height;
    ePixFormat = (MI_SYS_PixelFormat_e)RGB_BAYER_PIXEL(stSnrPlane0Info.ePixPrecision, stSnrPlane0Info.eBayerId);

    printf("sensor bayerid %d, bit mode %d \n", stSnrPlane0Info.eBayerId, stSnrPlane0Info.ePixPrecision);
    if(stSnrPlane0Info.eBayerId >= E_MI_SYS_PIXEL_BAYERID_MAX)
    {
        ePixFormat = E_MI_SYS_PIXEL_FRAME_YUV422_YUYV;
        pstVpeChnattr->eRunningMode = E_MI_VPE_RUN_DVR_MODE;
    }
    printf("u32CapWidth=%d,u32CapHeight=%d, ePixFormat=%d\n", u32CapWidth, u32CapHeight, ePixFormat);
    /************************************************
    Step3:  init VIF
    *************************************************/
    /*MI_VIF_Dev2SnrPadMuxCfg_t stVifDev[4];
    stVifDev[0].eSensorPadID = E_MI_VIF_SNRPAD_ID_0;
    stVifDev[0].u32PlaneID = 0xff;
    stVifDev[1].eSensorPadID = E_MI_VIF_SNRPAD_ID_2;
    stVifDev[1].u32PlaneID = 0xff;
    stVifDev[2].eSensorPadID = E_MI_VIF_SNRPAD_ID_1;
    stVifDev[2].u32PlaneID = 0xff;
    stVifDev[3].eSensorPadID = E_MI_VIF_SNRPAD_ID_3;
    stVifDev[3].u32PlaneID = 0xff;
    MI_S32 MI_VIF_SetDev2SnrPadMux(stVifDev, 4)*/

    MI_VIF_DevAttr_t stDevAttr;
    memset(&stDevAttr, 0x0, sizeof(MI_VIF_DevAttr_t));

    stDevAttr.eIntfMode = stPad0Info.eIntfMode;
    stDevAttr.eWorkMode = pstVifDevAttr->eWorkMode;
    stDevAttr.eHDRType = (MI_VIF_HDRType_e)pstVpeChnattr->eHdrType;
    if(stDevAttr.eIntfMode == E_MI_VIF_MODE_BT656)
        stDevAttr.eClkEdge = stPad0Info.unIntfAttr.stBt656Attr.eClkEdge;
    else
        stDevAttr.eClkEdge = E_MI_VIF_CLK_EDGE_DOUBLE;

    if(stDevAttr.eIntfMode == E_MI_VIF_MODE_MIPI)
        stDevAttr.eDataSeq =stPad0Info.unIntfAttr.stMipiAttr.eDataYUVOrder;
    else
        stDevAttr.eDataSeq = E_MI_VIF_INPUT_DATA_YUYV;

    if(stDevAttr.eIntfMode == E_MI_VIF_MODE_BT656)
        memcpy(&stDevAttr.stSyncAttr, &stPad0Info.unIntfAttr.stBt656Attr.stSyncAttr, sizeof(MI_VIF_SyncAttr_t));

    stDevAttr.eBitOrder = E_MI_VIF_BITORDER_NORMAL;

    STCHECKRESULT(MI_VIF_SetDevAttr(vifDev, &stDevAttr));
    STCHECKRESULT(MI_VIF_EnableDev(vifDev));

    MI_VIF_ChnPortAttr_t stVifPortInfo;
    memset(&stVifPortInfo, 0, sizeof(MI_VIF_ChnPortAttr_t));
    stVifPortInfo.stCapRect.u16X = stSnrPlane0Info.stCapRect.u16X;
    stVifPortInfo.stCapRect.u16Y = stSnrPlane0Info.stCapRect.u16Y;
    stVifPortInfo.stCapRect.u16Width =  stSnrPlane0Info.stCapRect.u16Width;
    stVifPortInfo.stCapRect.u16Height = stSnrPlane0Info.stCapRect.u16Height;
    stVifPortInfo.stDestSize.u16Width = u32CapWidth;
    stVifPortInfo.stDestSize.u16Height = u32CapHeight;
    stVifPortInfo.ePixFormat = ePixFormat;
    //stVifPortInfo.u32FrameModeLineCount for lowlantancy mode

    if(stDevAttr.eIntfMode == E_MI_VIF_MODE_BT656)
    {
        stVifPortInfo.eFrameRate = E_MI_VIF_FRAMERATE_FULL;
        stVifPortInfo.eCapSel = E_MI_SYS_FIELDTYPE_BOTH;
        stVifPortInfo.eScanMode = E_MI_SYS_FRAME_SCAN_MODE_PROGRESSIVE;
    }
    STCHECKRESULT(MI_VIF_SetChnPortAttr(vifChn, vifPort, &stVifPortInfo));
    STCHECKRESULT(MI_VIF_EnableChnPort(vifChn, vifPort));

    /************************************************
    Step4:  init VPE (create one VPE)
    *************************************************/
    MI_VPE_ChannelAttr_t stChannelVpeAttr;
    MI_VPE_ChannelPara_t stChannelVpeParam;

    memset(&stChannelVpeAttr, 0, sizeof(MI_VPE_ChannelAttr_t));
    memset(&stChannelVpeParam, 0x00, sizeof(MI_VPE_ChannelPara_t));

    stChannelVpeAttr.u16MaxW = u32CapWidth;
    stChannelVpeAttr.u16MaxH = u32CapHeight;
    stChannelVpeAttr.ePixFmt = ePixFormat;
    stChannelVpeAttr.eRunningMode = pstVpeChnattr->eRunningMode;
    stChannelVpeAttr.eSensorBindId = (MI_VPE_SensorChannel_e)(eSnrPadId+1);
    stChannelVpeAttr.bEnLdc = pstVpeChnattr->bEnLdc;
    stChannelVpeAttr.u32ChnPortMode = pstVpeChnattr->u32ChnPortMode;
    stChannelVpeAttr.eHDRType = pstVpeChnattr->eHdrType;
    STCHECKRESULT(MI_VPE_CreateChannel(vpechn, &stChannelVpeAttr));

    stChannelVpeParam.eHDRType = pstVpeChnattr->eHdrType;
    stChannelVpeParam.e3DNRLevel = pstVpeChnattr->e3DNRLevel;
    stChannelVpeParam.bMirror = pstVpeChnattr->bChnMirror;
    stChannelVpeParam.bFlip = pstVpeChnattr->bChnFlip;
    stChannelVpeParam.bEnLdc = pstVpeChnattr->bEnLdc;
    STCHECKRESULT(MI_VPE_SetChannelParam(vpechn, &stChannelVpeParam));

#if 0
    if(pstVpeChnattr->bEnLdc == TRUE)
    {
        pstVpeChnattr->s32Rot[0] = 0;
        pstVpeChnattr->s32Rot[1] = 90;
        pstVpeChnattr->s32Rot[2] = 180;
        pstVpeChnattr->s32Rot[3] = 270;//parame for test

        ST_GetLdcBinBuffer(pstVpeChnattr->LdcCfgbin_Path, &pstVpeChnattr->tconfig_para, &pstVpeChnattr->u32ViewNum, 
            pstVpeChnattr->ldcBinBuffer, pstVpeChnattr->u32LdcBinSize, pstVpeChnattr->s32Rot);
    }

    if(pstVpeChnattr->ldcBinBuffer[0] != NULL)
    {
        MI_VPE_LDCBegViewConfig(vpechn);

        for(i=0; i<pstVpeChnattr->u32ViewNum; i++)
        {
            MI_VPE_LDCSetViewConfig(vpechn, pstVpeChnattr->ldcBinBuffer[i], pstVpeChnattr->u32LdcBinSize[i]);

            //free(pstVpeChnattr->ldcBinBuffer);
            if(mi_eptz_buffer_free(pstVpeChnattr->ldcBinBuffer[i]) != MI_EPTZ_ERR_NONE)
            {
                printf("[MI EPTZ ERR]   %d !! \n", __LINE__);
            }
        }
        STCHECKRESULT(MI_VPE_LDCEndViewConfig(vpechn));
    }
    else
        printf("##############benldc %d, ldc bin buffer %p \n",pstVpeChnattr->bEnLdc, pstVpeChnattr->ldcBinBuffer);
#endif

    STCHECKRESULT(MI_VPE_SetChannelRotation(vpechn, pstVpeChnattr->eVpeRotate));

    if(pstVpeChnattr->eVpeRotate == E_MI_SYS_ROTATE_90
       || pstVpeChnattr->eVpeRotate == E_MI_SYS_ROTATE_270)
     {
        if(pstVpeChnattr->stOrgVpeChnCrop.u16Height !=0)
            pstVpeChnattr->stVpeChnCrop.u16X = u32CapHeight - pstVpeChnattr->stOrgVpeChnCrop.u16Y-pstVpeChnattr->stOrgVpeChnCrop.u16Height;
        else
            pstVpeChnattr->stVpeChnCrop.u16X = 0;

        pstVpeChnattr->stVpeChnCrop.u16Y = pstVpeChnattr->stOrgVpeChnCrop.u16X;
        pstVpeChnattr->stVpeChnCrop.u16Width = pstVpeChnattr->stOrgVpeChnCrop.u16Height;
        pstVpeChnattr->stVpeChnCrop.u16Height = pstVpeChnattr->stOrgVpeChnCrop.u16Width;
    }
    else
    {
        pstVpeChnattr->stVpeChnCrop.u16X = pstVpeChnattr->stOrgVpeChnCrop.u16X;
        pstVpeChnattr->stVpeChnCrop.u16Y = pstVpeChnattr->stOrgVpeChnCrop.u16Y;
        pstVpeChnattr->stVpeChnCrop.u16Width = pstVpeChnattr->stOrgVpeChnCrop.u16Width;
        pstVpeChnattr->stVpeChnCrop.u16Height = pstVpeChnattr->stOrgVpeChnCrop.u16Height;
    }

    STCHECKRESULT(MI_VPE_SetChannelCrop(vpechn, &pstVpeChnattr->stVpeChnCrop));

    STCHECKRESULT(MI_VPE_StartChannel (vpechn));

    for(i=0; i<ST_MAX_PORT_NUM; i++)
    {
        MI_VPE_PortMode_t stVpeMode;
        MI_U32  u32SclSourceHeight = 0;
        memset(&stVpeMode, 0, sizeof(stVpeMode));

        ST_VpePortAttr_t *pstVpePortAttr = &pstVpeChnattr->stVpePortAttr[i];
        printf("port%d: pstVpePortAttr->bUsed=%d\n",i,pstVpePortAttr->bUsed);
        if(pstVpePortAttr->bUsed == TRUE)
        {
            if(i==3)//port3 not scaling
            {
                pstVpePortAttr->stOrigPortSize.u16Width = u32CapWidth;
                pstVpePortAttr->stOrigPortSize.u16Height = u32CapHeight;
            }
            else if(i==4)//port4 IR port width/height is source 1/2
            {
                pstVpePortAttr->stOrigPortSize.u16Width = u32CapWidth/2;
                pstVpePortAttr->stOrigPortSize.u16Height = u32CapHeight/2;
            }

            if(pstVpeChnattr->u32ChnPortMode ==0 
                || pstVpeChnattr->stVpeChnCrop.u16Width ==0
                || pstVpeChnattr->stVpeChnCrop.u16Height ==0)
            {
                u32SclSourceHeight = u32CapHeight;
            }
            else
            {
                u32SclSourceHeight =  pstVpeChnattr->stOrgVpeChnCrop.u16Height;
            }

            if(pstVpeChnattr->eVpeRotate == E_MI_SYS_ROTATE_90
               || pstVpeChnattr->eVpeRotate == E_MI_SYS_ROTATE_270)
            {
                if(pstVpePortAttr->stOrigPortCrop.u16Height !=0)
                    pstVpePortAttr->stPortCrop.u16X = u32SclSourceHeight - pstVpePortAttr->stOrigPortCrop.u16Y - pstVpePortAttr->stOrigPortCrop.u16Height;
                else
                    pstVpePortAttr->stPortCrop.u16X = 0;

                pstVpePortAttr->stPortCrop.u16Y = pstVpePortAttr->stOrigPortCrop.u16X;
                pstVpePortAttr->stPortCrop.u16Width = pstVpePortAttr->stOrigPortCrop.u16Height;
                pstVpePortAttr->stPortCrop.u16Height = pstVpePortAttr->stOrigPortCrop.u16Width;

                pstVpePortAttr->stPortSize.u16Width = pstVpePortAttr->stOrigPortSize.u16Height;
                pstVpePortAttr->stPortSize.u16Height = pstVpePortAttr->stOrigPortSize.u16Width;
            }
            else
            {
                pstVpePortAttr->stPortCrop.u16X = pstVpePortAttr->stOrigPortCrop.u16X;
                pstVpePortAttr->stPortCrop.u16Y = pstVpePortAttr->stOrigPortCrop.u16Y;
                pstVpePortAttr->stPortCrop.u16Width = pstVpePortAttr->stOrigPortCrop.u16Width;
                pstVpePortAttr->stPortCrop.u16Height = pstVpePortAttr->stOrigPortCrop.u16Height;

                pstVpePortAttr->stPortSize.u16Width = pstVpePortAttr->stOrigPortSize.u16Width;
                pstVpePortAttr->stPortSize.u16Height = pstVpePortAttr->stOrigPortSize.u16Height;
            }
            if(i < ST_MAX_SCL_NUM)
            {
                STCHECKRESULT(MI_VPE_SetPortCrop(vpechn, i, &pstVpeChnattr->stVpePortAttr[i].stPortCrop));
            }
            stVpeMode.u16Width = pstVpeChnattr->stVpePortAttr[i].stPortSize.u16Width;
            stVpeMode.u16Height = pstVpeChnattr->stVpePortAttr[i].stPortSize.u16Height;
            stVpeMode.ePixelFormat = pstVpeChnattr->stVpePortAttr[i].ePixelFormat;
            stVpeMode.eCompressMode = E_MI_SYS_COMPRESS_MODE_NONE;
            stVpeMode.bMirror =pstVpeChnattr->stVpePortAttr[i].bMirror;
            stVpeMode.bFlip = pstVpeChnattr->stVpePortAttr[i].bFlip;

            STCHECKRESULT(MI_VPE_SetPortMode(vpechn, i, &stVpeMode));

            STCHECKRESULT(MI_VPE_EnablePort(vpechn, i));

            MI_SYS_ChnPort_t stChnPort;
            stChnPort.eModId = E_MI_MODULE_ID_VPE;
            stChnPort.u32DevId = 0;
            stChnPort.u32ChnId = vpechn;
            stChnPort.u32PortId = i;

            MI_SYS_SetChnOutputPortDepth(&stChnPort , pstVpePortAttr->u16UserDepth, pstVpePortAttr->u16Depth);
        }
    }

    /************************************************
    Step5:  bind VIF->VPE
    *************************************************/
    ST_Sys_BindInfo_T stBindInfo;
    memset(&stBindInfo, 0x0, sizeof(ST_Sys_BindInfo_T));
    stBindInfo.stSrcChnPort.eModId = E_MI_MODULE_ID_VIF;
    stBindInfo.stSrcChnPort.u32DevId = vifDev;
    stBindInfo.stSrcChnPort.u32ChnId = vifChn;
    stBindInfo.stSrcChnPort.u32PortId = 0;

    stBindInfo.stDstChnPort.eModId = E_MI_MODULE_ID_VPE;
    stBindInfo.stDstChnPort.u32DevId = 0;
    stBindInfo.stDstChnPort.u32ChnId = vpechn;
    stBindInfo.stDstChnPort.u32PortId = 0;

    stBindInfo.u32SrcFrmrate = 30;
    stBindInfo.u32DstFrmrate = 30;
    stBindInfo.eBindType = pstVifDevAttr->eBindType;
    STCHECKRESULT(ST_Sys_Bind(&stBindInfo));

    return MI_SUCCESS;
}

MI_S32 ST_BaseModuleUnInit(MI_SNR_PAD_ID_e eSnrPad)
{
    MI_VIF_DEV vifDev = gstSensorAttr[eSnrPad].u32BindVifDev;
    MI_VIF_CHN vifChn = vifDev*4;
    MI_VPE_CHANNEL vpechn = gstVifAttr[vifDev].u32BindVpeChan;

    ST_VpeChannelAttr_t *pstVpeChnattr = &gstVpeChnattr[vpechn];
    MI_U32 i = 0;
    ST_Sys_BindInfo_T stBindInfo;

    /************************************************
    Step1:  unbind VIF->VPE
    *************************************************/
    memset(&stBindInfo, 0x0, sizeof(ST_Sys_BindInfo_T));
    stBindInfo.stSrcChnPort.eModId = E_MI_MODULE_ID_VIF;
    stBindInfo.stSrcChnPort.u32DevId = vifDev;
    stBindInfo.stSrcChnPort.u32ChnId = vifChn;
    stBindInfo.stSrcChnPort.u32PortId = 0;

    stBindInfo.stDstChnPort.eModId = E_MI_MODULE_ID_VPE;
    stBindInfo.stDstChnPort.u32DevId = 0;
    stBindInfo.stDstChnPort.u32ChnId = vpechn;
    stBindInfo.stDstChnPort.u32PortId = 0;

    stBindInfo.u32SrcFrmrate = 30;
    stBindInfo.u32DstFrmrate = 30;
    STCHECKRESULT(ST_Sys_UnBind(&stBindInfo));

    /************************************************
    Step2:  destory VPE
    *************************************************/
    for(i = 0; i < ST_MAX_PORT_NUM; i ++)
    {
        if(pstVpeChnattr->stVpePortAttr[i].bUsed == TRUE)
        {
            STCHECKRESULT(MI_VPE_DisablePort(vpechn, i));
        }
    }

    STCHECKRESULT(MI_VPE_StopChannel(vpechn));
    STCHECKRESULT(MI_VPE_DestroyChannel(vpechn));

    /************************************************
    Step3:  destory VIF
    *************************************************/
    STCHECKRESULT(MI_VIF_DisableChnPort(vifChn, 0));
    STCHECKRESULT(MI_VIF_DisableDev(vifDev));

    /************************************************
    Step4:  destory Sensor
    *************************************************/
    STCHECKRESULT(MI_SNR_Disable(eSnrPad));

    /************************************************
    Step5:  destory SYS
    *************************************************/
    STCHECKRESULT(MI_SYS_Exit());

    return MI_SUCCESS;
}

MI_S32 ST_VencStart(MI_U32 u32MaxVencWidth, MI_U32 u32MaxVencHeight, MI_U32 u32VpeChn)
{
    MI_U32 i = 0;
    MI_U32 u32DevId = -1;
    MI_U32 u32VenBitRate = 0;
    ST_Sys_BindInfo_T stBindInfo;   
    MI_VENC_ChnAttr_t stChnAttr;
	MI_BOOL bBindDivp = FALSE;
    
    for(i = 0; i < ST_MAX_PORT_NUM; i ++)
    {
        MI_U32 u32VencChn = gstVpeChnattr[u32VpeChn].stVpePortAttr[i].u32BindVencChan;
        ST_VencAttr_t *pstStreamAttr = &gstVencattr[u32VencChn];
        bBindDivp = gstVpeChnattr[u32VpeChn].stVpePortAttr[i].bBindDivp;
        
        if(pstStreamAttr->bUsed != TRUE && bBindDivp != TRUE)
            continue;

        memset(&stChnAttr, 0, sizeof(MI_VENC_ChnAttr_t));

        u32VenBitRate = ((pstStreamAttr->u32Width * pstStreamAttr->u32Height + 500000)/1000000)*1024*1024;
        if(u32VenBitRate == 0)
            u32VenBitRate = 2*1024*1024;

        pstStreamAttr->u32Width = gstVpeChnattr[u32VpeChn].stVpePortAttr[i].stPortSize.u16Width;
        pstStreamAttr->u32Height = gstVpeChnattr[u32VpeChn].stVpePortAttr[i].stPortSize.u16Height;
	
        MIXER_DBG("bBindDivp=%d chn %d, pichwidth %d, height %d, MaxWidth %d, MaxHeight %d bitrate %d \n",bBindDivp, u32VencChn, 
            pstStreamAttr->u32Width, pstStreamAttr->u32Height, u32MaxVencWidth, u32MaxVencHeight, u32VenBitRate);
        
		if(pstStreamAttr->eType == E_MI_VENC_MODTYPE_H264E)
        {
            stChnAttr.stVeAttr.stAttrH264e.u32PicWidth = pstStreamAttr->u32Width;
            stChnAttr.stVeAttr.stAttrH264e.u32PicHeight = pstStreamAttr->u32Height;
            stChnAttr.stVeAttr.stAttrH264e.u32MaxPicWidth = u32MaxVencWidth;
            stChnAttr.stVeAttr.stAttrH264e.u32MaxPicHeight = u32MaxVencHeight;
            stChnAttr.stVeAttr.stAttrH264e.u32BFrameNum = 2;
            stChnAttr.stVeAttr.stAttrH264e.bByFrame = TRUE;

            stChnAttr.stRcAttr.eRcMode = E_MI_VENC_RC_MODE_H264CBR;
            stChnAttr.stRcAttr.stAttrH264Cbr.u32BitRate = u32VenBitRate;
            stChnAttr.stRcAttr.stAttrH264Cbr.u32FluctuateLevel = 0;
            stChnAttr.stRcAttr.stAttrH264Cbr.u32Gop = 30;
            stChnAttr.stRcAttr.stAttrH264Cbr.u32SrcFrmRateNum = 30;
            stChnAttr.stRcAttr.stAttrH264Cbr.u32SrcFrmRateDen = 1;
            stChnAttr.stRcAttr.stAttrH264Cbr.u32StatTime = 0;
        }
        else if(pstStreamAttr->eType == E_MI_VENC_MODTYPE_H265E)
        {
            stChnAttr.stVeAttr.stAttrH265e.u32PicWidth = pstStreamAttr->u32Width;
            stChnAttr.stVeAttr.stAttrH265e.u32PicHeight = pstStreamAttr->u32Height;
            stChnAttr.stVeAttr.stAttrH265e.u32MaxPicWidth = u32MaxVencWidth;
            stChnAttr.stVeAttr.stAttrH265e.u32MaxPicHeight = u32MaxVencHeight;
            stChnAttr.stVeAttr.stAttrH265e.bByFrame = TRUE;

            stChnAttr.stRcAttr.eRcMode = E_MI_VENC_RC_MODE_H265CBR;
            stChnAttr.stRcAttr.stAttrH265Cbr.u32BitRate = u32VenBitRate;
            stChnAttr.stRcAttr.stAttrH265Cbr.u32SrcFrmRateNum = 30;
            stChnAttr.stRcAttr.stAttrH265Cbr.u32SrcFrmRateDen = 1;
            stChnAttr.stRcAttr.stAttrH265Cbr.u32Gop = 30;
            stChnAttr.stRcAttr.stAttrH265Cbr.u32FluctuateLevel = 0;
            stChnAttr.stRcAttr.stAttrH265Cbr.u32StatTime = 0;

        }
        else if(pstStreamAttr->eType == E_MI_VENC_MODTYPE_JPEGE)
        {
            stChnAttr.stVeAttr.eType = E_MI_VENC_MODTYPE_JPEGE;
            stChnAttr.stVeAttr.stAttrJpeg.u32PicWidth = pstStreamAttr->u32Width;
            stChnAttr.stVeAttr.stAttrJpeg.u32PicHeight = pstStreamAttr->u32Height;
            stChnAttr.stVeAttr.stAttrJpeg.u32MaxPicWidth = u32MaxVencWidth;
            stChnAttr.stVeAttr.stAttrJpeg.u32MaxPicHeight = u32MaxVencHeight;

            stChnAttr.stRcAttr.eRcMode = E_MI_VENC_RC_MODE_MJPEGFIXQP;
            stChnAttr.stRcAttr.stAttrMjpegCbr.u32BitRate = 30;
            stChnAttr.stRcAttr.stAttrMjpegCbr.u32SrcFrmRateNum = 30;
            stChnAttr.stRcAttr.stAttrMjpegCbr.u32SrcFrmRateDen = 1;
        }

        stChnAttr.stVeAttr.eType = pstStreamAttr->eType;
        u32VencChn = pstStreamAttr->vencChn;

        STCHECKRESULT(ST_Venc_CreateChannel(u32VencChn, &stChnAttr));

        MI_VENC_InputSourceConfig_t stVencSourceCfg;
        if(pstStreamAttr->eBindType == E_MI_SYS_BIND_TYPE_HW_RING)
        {
            pstStreamAttr->u32BindParam = ALIGN_UP(pstStreamAttr->u32Height, 32);
            stVencSourceCfg.eInputSrcBufferMode = E_MI_VENC_INPUT_MODE_RING_ONE_FRM;
        }
        else
            stVencSourceCfg.eInputSrcBufferMode = E_MI_VENC_INPUT_MODE_NORMAL_FRMBASE;

        MI_VENC_SetInputSourceConfig(u32VencChn, &stVencSourceCfg);

        STCHECKRESULT(ST_Venc_StartChannel(u32VencChn));

        ExecFunc(MI_VENC_GetChnDevid(u32VencChn, &u32DevId), MI_SUCCESS);

        memset(&stBindInfo, 0x0, sizeof(ST_Sys_BindInfo_T));
        stBindInfo.stSrcChnPort.eModId = E_MI_MODULE_ID_VPE;
        stBindInfo.stSrcChnPort.u32DevId = 0;
        stBindInfo.stSrcChnPort.u32ChnId = pstStreamAttr->u32BindVpeChn;
        stBindInfo.stSrcChnPort.u32PortId = pstStreamAttr->u32BindVpePort;

        stBindInfo.stDstChnPort.eModId = E_MI_MODULE_ID_VENC;
        stBindInfo.stDstChnPort.u32DevId = u32DevId;
        stBindInfo.stDstChnPort.u32ChnId = u32VencChn;
        stBindInfo.stDstChnPort.u32PortId = 0;

        stBindInfo.u32SrcFrmrate = 30;
        stBindInfo.u32DstFrmrate = 30;
        stBindInfo.eBindType = pstStreamAttr->eBindType;
        stBindInfo.u32BindParam = pstStreamAttr->u32BindParam;
        STCHECKRESULT(ST_Sys_Bind(&stBindInfo));

        pstStreamAttr->bStart = TRUE;
    }

    return MI_SUCCESS;
}

MI_S32 ST_VencStop(MI_U32 u32VpeChn)
{
    MI_U32  i = 0;
    MI_U32  u32DevId  = -1;
    MI_VENC_CHN VencChn = 0;
    ST_Sys_BindInfo_T stBindInfo;
    
    for(i = 0; i < ST_MAX_PORT_NUM; i ++)
    {
        MI_U32 u32VencChn = gstVpeChnattr[u32VpeChn].stVpePortAttr[i].u32BindVencChan;
        ST_VencAttr_t *pstStreamAttr = &gstVencattr[u32VencChn];

        if((pstStreamAttr->bUsed != TRUE) 
            || (0 == pstStreamAttr->eType 
            || pstStreamAttr->eType > 4))
            continue;
        
        VencChn = pstStreamAttr->vencChn;
        ExecFunc(MI_VENC_GetChnDevid(VencChn, &u32DevId), MI_SUCCESS);

        memset(&stBindInfo, 0x0, sizeof(ST_Sys_BindInfo_T));
        stBindInfo.stSrcChnPort.eModId = E_MI_MODULE_ID_VPE;
        stBindInfo.stSrcChnPort.u32DevId = 0;
        stBindInfo.stSrcChnPort.u32ChnId = pstStreamAttr->u32BindVpeChn;
        stBindInfo.stSrcChnPort.u32PortId = pstStreamAttr->u32BindVpePort;

        stBindInfo.stDstChnPort.eModId = E_MI_MODULE_ID_VENC;
        stBindInfo.stDstChnPort.u32DevId = u32DevId;
        stBindInfo.stDstChnPort.u32ChnId = VencChn;
        
        stBindInfo.stDstChnPort.u32PortId = 0;
        stBindInfo.u32SrcFrmrate = 30;
        stBindInfo.u32DstFrmrate = 30;
        STCHECKRESULT(ST_Sys_UnBind(&stBindInfo));

        STCHECKRESULT(ST_Venc_StopChannel(VencChn));
        STCHECKRESULT(ST_Venc_DestoryChannel(VencChn));

        pstStreamAttr->bStart = FALSE;
    }

    return MI_SUCCESS;
}

MI_S32 ST_GetSring(dictionary *pstDict, const char* pInString, char* pOutString)
{
    char* string;
    
    string = iniparser_getstring(pstDict, pInString, (char *)"ERR");
    if(!strcmp((const char *)string, (const char*)"NULL"))
    {
        printf("Get %s is NULL \n", pInString);
        return 1;
    }
    else if(!strcmp((const char *)string, (const char*)"ERR"))
    {
        printf("Get %s is ERR \n", pInString);
        return 2;
    }

    memcpy(pOutString, string, strlen(string));
    pOutString[strlen(string)] = 0;
    
    return MI_SUCCESS;
}

MI_S32 ST_ParserIni(char *pIniPath)

{
    MI_SNR_PAD_ID_e eSnrPad = E_MI_SNR_PAD_ID_0;
    ST_Sensor_Attr_t *pstSensorAttr = NULL;
    ST_Vif_Attr_t *pstVifAttr = NULL;
    ST_VpeChannelAttr_t *pstVpeChnAttr = NULL;
    ST_VpePortAttr_t   *pstVpePortAttr = NULL;
    char string[64];
    MI_U8 i = 0;
    time_t stTime = 0;

    printf("inipath %s \n", pIniPath);

    dictionary *pstDict = iniparser_load(pIniPath);

    if(pstDict == NULL)
        return -1;

    printf("pstDict %p \n", pstDict);
    g_bPreviewByVenc = iniparser_getint(pstDict, ":UseVenc", 0);
    eSnrPad = (MI_SNR_PAD_ID_e)iniparser_getint(pstDict, ":SensorPad", 0);
    pstSensorAttr = &gstSensorAttr[eSnrPad];
    pstSensorAttr->bUsed = TRUE;
    pstSensorAttr->u32BindVifDev = iniparser_getint(pstDict, ":VifDev", 0);
    pstSensorAttr->u8ResIndex = iniparser_getint(pstDict, ":ResIndex", -1);
    pstVifAttr = &gstVifAttr[pstSensorAttr->u32BindVifDev];
    pstVifAttr->u32BindVpeChan = pstSensorAttr->u32BindVifDev;

    pstVpeChnAttr = &gstVpeChnattr[pstVifAttr->u32BindVpeChan];
    ST_GetSring(pstDict, ":RunMode", string);

    if(!strcmp(string, (char *)"RealTime"))
    {
        pstVpeChnAttr->eRunningMode = E_MI_VPE_RUN_REALTIME_MODE;
        pstVifAttr->eWorkMode = E_MI_VIF_WORK_MODE_RGB_REALTIME;
        pstVifAttr->eBindType = E_MI_SYS_BIND_TYPE_REALTIME;
    }
    else if(!strcmp(string, "FrameMode"))
    {
        pstVpeChnAttr->eRunningMode = E_MI_VPE_RUN_CAM_MODE;
        pstVifAttr->eWorkMode = E_MI_VIF_WORK_MODE_RGB_FRAMEMODE;
        pstVifAttr->eBindType = E_MI_SYS_BIND_TYPE_FRAME_BASE;
    }
    else if(!strcmp(string, "DVRMode"))
    {
        pstVpeChnAttr->eRunningMode = E_MI_VPE_RUN_DVR_MODE;
        pstVifAttr->eWorkMode = E_MI_VIF_WORK_MODE_1MULTIPLEX;
        pstVifAttr->eBindType = E_MI_SYS_BIND_TYPE_FRAME_BASE;
    }
    else
    {
        printf("runmode parse %s err, please use RealTime/FrameMode/DVRMode. default use RealTime \n", string);
        pstVpeChnAttr->eRunningMode = E_MI_VPE_RUN_REALTIME_MODE;
        pstVifAttr->eWorkMode = E_MI_VIF_WORK_MODE_RGB_REALTIME;
        pstVifAttr->eBindType = E_MI_SYS_BIND_TYPE_REALTIME;
    }

    pstVpeChnAttr->eHdrType = (MI_VPE_HDRType_e)iniparser_getint(pstDict, ":HDR", 0);
    pstVpeChnAttr->e3DNRLevel = (MI_VPE_3DNR_Level_e)iniparser_getint(pstDict, ":NRLevel", 0);
    pstVpeChnAttr->eVpeRotate = (MI_SYS_Rotate_e)iniparser_getint(pstDict, ":Rotation", 0);
    pstVpeChnAttr->bChnMirror = (MI_SYS_Rotate_e)iniparser_getint(pstDict, ":ChnMirror", 0);
    pstVpeChnAttr->bChnFlip = (MI_SYS_Rotate_e)iniparser_getint(pstDict, ":ChnFlip", 0);
    pstVpeChnAttr->stOrgVpeChnCrop.u16X = iniparser_getint(pstDict, ":ChnCropX", 0);
    pstVpeChnAttr->stOrgVpeChnCrop.u16Y = iniparser_getint(pstDict, ":ChnCropY", 0);
    pstVpeChnAttr->stOrgVpeChnCrop.u16Width = iniparser_getint(pstDict, ":ChnCropW", 0);
    pstVpeChnAttr->stOrgVpeChnCrop.u16Height = iniparser_getint(pstDict, ":ChnCropH", 0);
    pstVpeChnAttr->u32ChnPortMode = iniparser_getint(pstDict, ":ChnPortMode", 0);
//	printf("===pstVpeChnAttr->eVpeRotate=%d   pstVpeChnAttr->u32ChnPortMode=%d\n",pstVpeChnAttr->eVpeRotate, pstVpeChnAttr->u32ChnPortMode);
    pstVpeChnAttr->bEnLdc = FALSE;
    if(ST_GetSring(pstDict, ":LdcBinPath", string) == MI_SUCCESS)
    {
        pstVpeChnAttr->bEnLdc = TRUE;
        memcpy(pstVpeChnAttr->LdcCfgbin_Path, string, strlen(string));
    }

    if(ST_GetSring(pstDict, ":IqBinPath", string) == MI_SUCCESS)
    {
        memcpy(pstVpeChnAttr->IqCfgbin_Path,string,strlen(string));
        printf("IqBinPath:%s \n",pstVpeChnAttr->IqCfgbin_Path);
    }

    for(i=0; i<ST_MAX_PORT_NUM; i++)
    {
        char PortString[128] = {0};
        pstVpePortAttr = &pstVpeChnAttr->stVpePortAttr[i];

        MI_U16 u16VencChn = pstVifAttr->u32BindVpeChan*ST_MAX_PORT_NUM+i;
        ST_VencAttr_t *pstVencAttr = &gstVencattr[u16VencChn];
        pstVpePortAttr->u32BindVencChan = u16VencChn;

        sprintf(PortString, ":port%dUse", i);
        pstVpePortAttr->bUsed = iniparser_getint(pstDict, PortString, 0);
		printf("PortString:%s  ST_ParserIni pstVpePortAttr->bUsed=%d\n",PortString,pstVpePortAttr->bUsed);
        if(pstVpePortAttr->bUsed == TRUE)
        {
            sprintf(PortString, ":port%dPortCropX", i);
            pstVpePortAttr->stOrigPortCrop.u16X = iniparser_getint(pstDict, PortString, 0);
            sprintf(PortString, ":port%dPortCropY", i);
            pstVpePortAttr->stOrigPortCrop.u16Y = iniparser_getint(pstDict, PortString, 0);
            sprintf(PortString, ":port%dPortCropW", i);
            pstVpePortAttr->stOrigPortCrop.u16Width = iniparser_getint(pstDict, PortString, 0);
            sprintf(PortString, ":port%dPortCropH", i);
            pstVpePortAttr->stOrigPortCrop.u16Height = iniparser_getint(pstDict, PortString, 0);

            sprintf(PortString, ":port%dmirror", i);
            pstVpePortAttr->bMirror = iniparser_getint(pstDict, PortString, 0);
            sprintf(PortString, ":port%dflip", i);
            pstVpePortAttr->bFlip = iniparser_getint(pstDict, PortString, 0);

            sprintf(PortString, ":port%dW", i);
            pstVpePortAttr->stOrigPortSize.u16Width = iniparser_getint(pstDict, PortString, 0);
            sprintf(PortString, ":port%dH", i);
            pstVpePortAttr->stOrigPortSize.u16Height = iniparser_getint(pstDict, PortString, 0);

			sprintf(PortString, ":port%d_bBindDivp", i);
			pstVpePortAttr->bBindDivp  = (MI_BOOL)iniparser_getint(pstDict, PortString, 0);
			
            sprintf(PortString, ":port%dPixel", i);
            pstVpePortAttr->ePixelFormat = (MI_SYS_PixelFormat_e)iniparser_getint(pstDict, PortString, 0);

            if(g_bPreviewByVenc == TRUE)
            {
                if(!pstVpePortAttr->bBindDivp)
                    pstVencAttr->bUsed = TRUE;

                sprintf(PortString, ":port%dBindtype", i);
                pstVencAttr->eBindType = (MI_SYS_BindType_e)iniparser_getint(pstDict, PortString, 0);
                sprintf(PortString, ":port%dEncodeType", i);
                pstVencAttr->eType = (MI_VENC_ModType_e)iniparser_getint(pstDict, PortString, 0);
                sprintf(pstVencAttr->szStreamName, "video%d", u16VencChn);
                pstVencAttr->u32BindParam = 0;
                pstVencAttr->u32BindVpeChn = pstVifAttr->u32BindVpeChan;
                pstVencAttr->u32BindVpePort = i;
                pstVencAttr->vencChn = u16VencChn;

                if(pstVencAttr->eBindType == E_MI_SYS_BIND_TYPE_REALTIME
                    && pstVencAttr->eType == E_MI_VENC_MODTYPE_JPEGE)
                    pstVpePortAttr->ePixelFormat = E_MI_SYS_PIXEL_FRAME_YUV422_YUYV;
                else
                    pstVpePortAttr->ePixelFormat = E_MI_SYS_PIXEL_FRAME_YUV_SEMIPLANAR_420;
            }

            sprintf(PortString, ":port%duserdepth", i);
            pstVpePortAttr->u16UserDepth = (MI_U16)iniparser_getint(pstDict, PortString, 0);

            sprintf(PortString, ":port%ddepth", i);
            pstVpePortAttr->u16Depth = (MI_U16)iniparser_getint(pstDict, PortString, 0);

            sprintf(PortString, ":port%dDumpBuffNum", i);
            pstVpePortAttr->s32DumpBuffNum = (MI_S32)iniparser_getint(pstDict, PortString, 0);

            sprintf(PortString, ":port%dOutPutPath", i);
            if(ST_GetSring(pstDict, PortString, string) == MI_SUCCESS)
            {
                sprintf(pstVpePortAttr->FilePath, "%s/vpeport%d_%dx%d_pixel%d_%ld.raw", string, i, pstVpePortAttr->stOrigPortSize.u16Width, pstVpePortAttr->stOrigPortSize.u16Height, pstVpePortAttr->ePixelFormat, time(&stTime));
                printf("OutPutFile_Path:%s \n",pstVpePortAttr->FilePath);
            }
            printf("i %d, string:%s \n",i,string);

            if(pstVpePortAttr->u16Depth == 0)
                pstVpePortAttr->u16Depth = 4;
            if(g_bPreviewByVenc == FALSE)
                pstVpePortAttr->u16UserDepth = 1;
        }
    }
    
    printf("UseVenc %d, SensorPad %d, VifDev %d,  string %s\n", g_bPreviewByVenc, eSnrPad, pstSensorAttr->u32BindVifDev, string);

    gstDivpPortAttr.u32Width = iniparser_getint(pstDict, ":divpWidth", 320);
    gstDivpPortAttr.u32Height = iniparser_getint(pstDict, ":divpHeigth", 240);
    gstDivpPortAttr.ePixelFormat = (MI_SYS_PixelFormat_e)iniparser_getint(pstDict, ":divpPixel", 1);
    gstDivpPortAttr.eCompMode = (MI_SYS_CompressMode_e)iniparser_getint(pstDict, ":divpCompMode", 0);

    gstDlaInfo.bDlaUse = iniparser_getint(pstDict, ":dlaUse", 0);
    gstDlaInfo.stIpuInitInfo.enModelType = (IPU_Model_Type_E)iniparser_getint(pstDict, ":dlaType", 0);
    memcpy(&gstDlaInfo.stDivpPortAttr, &gstDivpPortAttr, sizeof(MI_DIVP_OutputPortAttr_t));
    
    if(gstDlaInfo.bDlaUse == TRUE)
    {
        MI_U32  u32Ret = 0;
        char* defFirmware = (char*)"/config/dla/ipu_firmware.bin";
        
        if(ST_GetSring(pstDict, ":dlaFirmware", string) == MI_SUCCESS)
        {
            memcpy(gstDlaInfo.stIpuInitInfo.szIpuFirmware, string, strlen(string));
        }     
        else
        {
            memcpy(gstDlaInfo.stIpuInitInfo.szIpuFirmware, defFirmware, strlen(defFirmware));
        } 

        if((u32Ret+=ST_GetSring(pstDict, ":dlaModel0", string)) == MI_SUCCESS)
        {
            memcpy(gstDlaInfo.stIpuInitInfo.szModelFile, string, strlen(string));
        } 

        if((u32Ret+=ST_GetSring(pstDict, ":dlaLabel", string)) == MI_SUCCESS)
        {
            if(gstDlaInfo.stIpuInitInfo.enModelType != Model_Type_FaceReg)
            {
                memcpy(gstDlaInfo.stIpuInitInfo.u.ExtendInfo1.szLabelFile, string, strlen(string));
            }
            else
            {
                memcpy(gstDlaInfo.stIpuInitInfo.u.ExtendInfo2.szNameListFile, string, strlen(string));

                if((u32Ret+=ST_GetSring(pstDict, ":dlaModel1", string)) == MI_SUCCESS)
                {
                    memcpy(gstDlaInfo.stIpuInitInfo.u.ExtendInfo2.szModelFile1, string, strlen(string));
                }
            
                if((u32Ret+=ST_GetSring(pstDict, ":dlaFeat", string)) == MI_SUCCESS)
                {
                    memcpy(gstDlaInfo.stIpuInitInfo.u.ExtendInfo2.szFaceDBFile, string, strlen(string));
                }
            }
        }

        gstDlaInfo.bDlaUse = u32Ret?FALSE:TRUE;
    }
    
    iniparser_freedict(pstDict);

    return 0;
}

MI_BOOL ST_DoGetVifRawData(MI_S32 HDRtype)
{
    MI_SNR_PADInfo_t  stPad0Info;
    MI_SNR_PlaneInfo_t stSnrPlane0Info;
    MI_U32 u32CapWidth = 0, u32CapHeight = 0;
    MI_VIF_FrameRate_e eFrameRate = E_MI_VIF_FRAMERATE_FULL;
    MI_SYS_PixelFormat_e ePixFormat;
    MI_VIF_DEV vifDev = 1;
    MI_VIF_CHN vifChn = 4;

    MI_SNR_GetPadInfo(E_MI_SNR_PAD_ID_0, &stPad0Info);
    MI_SNR_GetPlaneInfo(E_MI_SNR_PAD_ID_0, 0, &stSnrPlane0Info);
    u32CapWidth = stSnrPlane0Info.stCapRect.u16Width;
    u32CapHeight = stSnrPlane0Info.stCapRect.u16Height;
    eFrameRate = E_MI_VIF_FRAMERATE_FULL;
    ePixFormat = (MI_SYS_PixelFormat_e)RGB_BAYER_PIXEL(stSnrPlane0Info.ePixPrecision, stSnrPlane0Info.eBayerId);

    /************************************************
    Step2:  init VIF(for IPC, only one dev)
    *************************************************/
    MI_VIF_Dev2SnrPadMuxCfg_t stVifDevMap[4];
    memset(stVifDevMap, 0xff, sizeof(MI_VIF_Dev2SnrPadMuxCfg_t)*4);

    if(HDRtype > 0)
    {
       /* stVifDevMap[0].eSensorPadID = E_MI_VIF_SNRPAD_ID_0;
        stVifDevMap[0].u32PlaneID = 1;
        stVifDevMap[1].eSensorPadID = E_MI_VIF_SNRPAD_ID_0;
        stVifDevMap[1].u32PlaneID = 0;
        stVifDevMap[2].eSensorPadID = E_MI_VIF_SNRPAD_ID_0;
        stVifDevMap[2].u32PlaneID = 1;*/
        printf("HDR ON not support");
        return 0;
    }
    else
    {
        stVifDevMap[0].eSensorPadID = E_MI_VIF_SNRPAD_ID_0;
        stVifDevMap[0].u32PlaneID = 0XFF;
        stVifDevMap[1].eSensorPadID = E_MI_VIF_SNRPAD_ID_0;
        stVifDevMap[1].u32PlaneID = 0XFF;
    }
    printf("devmap %p\n", stVifDevMap);
    MI_VIF_SetDev2SnrPadMux(stVifDevMap, 4);

    MI_VIF_DevAttr_t stDevAttr;
    memset(&stDevAttr, 0x0, sizeof(MI_VIF_DevAttr_t));

    stDevAttr.eIntfMode = stPad0Info.eIntfMode;
    stDevAttr.eWorkMode = E_MI_VIF_WORK_MODE_RGB_FRAMEMODE;
    stDevAttr.eHDRType = E_MI_VIF_HDR_TYPE_OFF;
    if(stDevAttr.eIntfMode == E_MI_VIF_MODE_BT656)
        stDevAttr.eClkEdge = stPad0Info.unIntfAttr.stBt656Attr.eClkEdge;
    else
        stDevAttr.eClkEdge = E_MI_VIF_CLK_EDGE_DOUBLE;

    if(stDevAttr.eIntfMode == E_MI_VIF_MODE_MIPI)
        stDevAttr.eDataSeq =stPad0Info.unIntfAttr.stMipiAttr.eDataYUVOrder;
    else
        stDevAttr.eDataSeq = E_MI_VIF_INPUT_DATA_YUYV;

    if(stDevAttr.eIntfMode == E_MI_VIF_MODE_BT656)
        memcpy(&stDevAttr.stSyncAttr, &stPad0Info.unIntfAttr.stBt656Attr.stSyncAttr, sizeof(MI_VIF_SyncAttr_t));

    stDevAttr.eBitOrder = E_MI_VIF_BITORDER_NORMAL;

    ExecFunc(MI_VIF_SetDevAttr(vifDev, &stDevAttr), MI_SUCCESS);
    ExecFunc(MI_VIF_EnableDev(vifDev), MI_SUCCESS);

    ST_VIF_PortInfo_T stVifPortInfoInfo;
    memset(&stVifPortInfoInfo, 0, sizeof(ST_VIF_PortInfo_T));
    stVifPortInfoInfo.u32RectX = 0;
    stVifPortInfoInfo.u32RectY = 0;
    stVifPortInfoInfo.u32RectWidth = u32CapWidth;
    stVifPortInfoInfo.u32RectHeight = u32CapHeight;
    stVifPortInfoInfo.u32DestWidth = u32CapWidth;
    stVifPortInfoInfo.u32DestHeight = u32CapHeight;
    stVifPortInfoInfo.eFrameRate = eFrameRate;
    stVifPortInfoInfo.ePixFormat = ePixFormat;//E_MI_SYS_PIXEL_FRAME_RGB_BAYER_12BPP_GR;
    STCHECKRESULT(ST_Vif_CreatePort(vifChn, 0, &stVifPortInfoInfo));
    STCHECKRESULT(ST_Vif_StartPort(0, vifChn, 0));
    {
        MI_SYS_ChnPort_t stChnPort;
        MI_SYS_BufInfo_t stBufInfo;
        MI_SYS_BUF_HANDLE hHandle;
        MI_S32 s32WriteCnt = 0;
        FILE *fp = NULL;
        char aName[128];
        struct timeval CurTime;
        MI_U64 u64Time = 0;

        memset(&stChnPort, 0x0, sizeof(MI_SYS_ChnPort_t));
        stChnPort.eModId = E_MI_MODULE_ID_VIF;
        stChnPort.u32DevId = 0;
        stChnPort.u32ChnId = vifChn;
        stChnPort.u32PortId = 0;

        gettimeofday(&CurTime,NULL);
        u64Time = CurTime.tv_sec*1000;

        sprintf(aName, "/mnt/dump_vif4_port0_%dx%d_pts%llu.yuv",u32CapWidth,u32CapHeight, u64Time);

        fp = fopen(aName,"wb");
        if(fp == NULL)
            printf("file open fail\n");

        while (s32WriteCnt < 10)
        {
            if (MI_SUCCESS == MI_SYS_ChnOutputPortGetBuf(&stChnPort, &stBufInfo, &hHandle))
            {
                int size = stBufInfo.stFrameData.u32BufSize;
                fwrite(stBufInfo.stFrameData.pVirAddr[0], size, 1, fp);
                s32WriteCnt++;
                printf("\t vif(%d) size(%d) get buf cnt (%d)...w(%d)...h(%d)..\n", vifChn, size, s32WriteCnt, stBufInfo.stFrameData.u16Width, stBufInfo.stFrameData.u16Height);
                MI_SYS_ChnOutputPortPutBuf(hHandle);
            }
            usleep(10*1000);
        }
        fclose(fp);
    }
    STCHECKRESULT(ST_Vif_StopPort(vifChn, 0));
    STCHECKRESULT(ST_Vif_DisableDev(vifDev));

    return MI_SUCCESS;
}

MI_BOOL ST_DoChangeHdrRes(MI_U32 u32SensorNum)
{
    MI_S32 s32SnrPad =0;
    MI_SNR_PAD_ID_e eSnrPad;
    MI_SNR_PADInfo_t  stPad0Info;
    MI_SNR_PlaneInfo_t stSnrPlane0Info;
    MI_VIF_DEV vifDev = 0;
    MI_VIF_CHN vifChn = 0;
    MI_VIF_PORT vifPort =0;
    MI_U8  u8VpeChn = 0;
    memset(&stPad0Info, 0x0, sizeof(MI_SNR_PADInfo_t));
    memset(&stSnrPlane0Info, 0x0, sizeof(MI_SNR_PlaneInfo_t));

    ST_VpeChannelAttr_t *pstVpeChnattr = &gstVpeChnattr[u8VpeChn];
    ST_Vif_Attr_t  *pstVifAttr = &gstVifAttr[vifDev];

    MI_U32 u32CapWidth = 0, u32CapHeight = 0;
    MI_SYS_PixelFormat_e ePixFormat;

    MI_S32 select = 0;

    if(u32SensorNum > 1)
    {
        printf("select SensorPad id:");
        scanf("%d", &s32SnrPad);
        ST_Flush();

        if(s32SnrPad >= ST_MAX_SENSOR_NUM)
        {
            printf("s32SnrPad %d > max %d \n", s32SnrPad, ST_MAX_SENSOR_NUM);
            return 0;
        }
    }
    else
    {
        s32SnrPad = 0;
    }

    eSnrPad = (MI_SNR_PAD_ID_e)s32SnrPad;
    vifDev = gstSensorAttr[eSnrPad].u32BindVifDev;
    vifChn = vifDev*4;
    u8VpeChn = gstVifAttr[vifDev].u32BindVpeChan;

    /************************************************
    Step1: VPE Stop ==> wait driver all buffer done
    *************************************************/
    STCHECKRESULT(MI_VPE_StopChannel(u8VpeChn));

    /************************************************
    Step2:  Destory VIF
    *************************************************/
    STCHECKRESULT(MI_VIF_DisableChnPort(vifChn, 0));
    STCHECKRESULT(MI_VIF_DisableDev(vifDev));

    /************************************************
    Step3:  Destory Sensor
    *************************************************/
    STCHECKRESULT(MI_SNR_Disable(eSnrPad));

    /************************************************
    Step4: Choice Hdr Type
    *************************************************/
    printf("Use HDR ?\n 0 not use, 1 use VC, 2 use DOL, 3 use EMBEDDED, 4 use LI\n");
    printf("sony sensor(ex imx307) use DOL, sc sensor(ex sc4238) use VC\n");
    scanf("%d", &select);
    ST_Flush();
    printf("You select %d HDR\n", select);
    
    if(select == 0)
    {
        pstVpeChnattr->eHdrType = E_MI_VPE_HDR_TYPE_OFF;
    }
    else if(select == 1)
    {
        pstVpeChnattr->eHdrType = E_MI_VPE_HDR_TYPE_VC;
    }
    else if(select == 2)
    {
        pstVpeChnattr->eHdrType = E_MI_VPE_HDR_TYPE_DOL;
    }
    else if(select == 3)
    {
        pstVpeChnattr->eHdrType = E_MI_VPE_HDR_TYPE_EMBEDDED;
    }
    else if(select == 4)
    {
        pstVpeChnattr->eHdrType = E_MI_VPE_HDR_TYPE_LI;
    }
    else
    {
        printf("select hdrtype %d not support \n", select);
        return 0;
    }

    /************************************************
    Step5:  Init Sensor
    *************************************************/
    if(pstVpeChnattr->eHdrType > 0)
    {
        STCHECKRESULT(MI_SNR_SetPlaneMode(eSnrPad, TRUE));
    }
    else
    {
        STCHECKRESULT(MI_SNR_SetPlaneMode(eSnrPad, FALSE));
    }

    MI_U8 u8ResIndex =0;
    MI_U32 u32ResCount =0;
    MI_SNR_Res_t stRes;
    memset(&stRes, 0x0, sizeof(MI_SNR_Res_t));

    STCHECKRESULT(MI_SNR_QueryResCount(eSnrPad, &u32ResCount));
    for(u8ResIndex=0; u8ResIndex < u32ResCount; u8ResIndex++)
    {
       MI_SNR_GetRes(eSnrPad, u8ResIndex, &stRes);
       printf("index %d, Crop(%d,%d,%d,%d), outputsize(%d,%d), maxfps %d, minfps %d, ResDesc %s\n",
       u8ResIndex,
       stRes.stCropRect.u16X, stRes.stCropRect.u16Y, stRes.stCropRect.u16Width,stRes.stCropRect.u16Height,
       stRes.stOutputSize.u16Width, stRes.stOutputSize.u16Height,
       stRes.u32MaxFps,stRes.u32MinFps,
       stRes.strResDesc);
    }

    printf("select res\n");
    scanf("%d", &select);
    ST_Flush();

    if((MI_U32)select >= u32ResCount)
        return 0;

    STCHECKRESULT(MI_SNR_SetRes(eSnrPad,(MI_U8)select));
    STCHECKRESULT(MI_SNR_Enable(eSnrPad));

    STCHECKRESULT(MI_SNR_GetPadInfo(eSnrPad, &stPad0Info));
    STCHECKRESULT(MI_SNR_GetPlaneInfo(eSnrPad, 0, &stSnrPlane0Info));
    u32CapWidth = stSnrPlane0Info.stCapRect.u16Width;
    u32CapHeight = stSnrPlane0Info.stCapRect.u16Height;
    ePixFormat = (MI_SYS_PixelFormat_e)RGB_BAYER_PIXEL(stSnrPlane0Info.ePixPrecision, stSnrPlane0Info.eBayerId);

    /************************************************
    Step6:  init VIF
    *************************************************/
    MI_VIF_DevAttr_t stDevAttr;
    memset(&stDevAttr, 0x0, sizeof(MI_VIF_DevAttr_t));

    stDevAttr.eIntfMode = stPad0Info.eIntfMode;
    stDevAttr.eWorkMode = pstVifAttr->eWorkMode;
    stDevAttr.eHDRType = (MI_VIF_HDRType_e)pstVpeChnattr->eHdrType;
    if(stDevAttr.eIntfMode == E_MI_VIF_MODE_BT656)
        stDevAttr.eClkEdge = stPad0Info.unIntfAttr.stBt656Attr.eClkEdge;
    else
        stDevAttr.eClkEdge = E_MI_VIF_CLK_EDGE_DOUBLE;

    if(stDevAttr.eIntfMode == E_MI_VIF_MODE_MIPI)
        stDevAttr.eDataSeq =stPad0Info.unIntfAttr.stMipiAttr.eDataYUVOrder;
    else
        stDevAttr.eDataSeq = E_MI_VIF_INPUT_DATA_YUYV;

    if(stDevAttr.eIntfMode == E_MI_VIF_MODE_BT656)
        memcpy(&stDevAttr.stSyncAttr, &stPad0Info.unIntfAttr.stBt656Attr.stSyncAttr, sizeof(MI_VIF_SyncAttr_t));

    stDevAttr.eBitOrder = E_MI_VIF_BITORDER_NORMAL;

    STCHECKRESULT(MI_VIF_SetDevAttr(vifDev, &stDevAttr));
    STCHECKRESULT(MI_VIF_EnableDev(vifDev));

    MI_VIF_ChnPortAttr_t stVifPortInfo;
    memset(&stVifPortInfo, 0, sizeof(MI_VIF_ChnPortAttr_t));
    stVifPortInfo.stCapRect.u16X = stSnrPlane0Info.stCapRect.u16X;
    stVifPortInfo.stCapRect.u16Y = stSnrPlane0Info.stCapRect.u16Y;
    stVifPortInfo.stCapRect.u16Width =  stSnrPlane0Info.stCapRect.u16Width;
    stVifPortInfo.stCapRect.u16Height = stSnrPlane0Info.stCapRect.u16Height;
    stVifPortInfo.stDestSize.u16Width = u32CapWidth;
    stVifPortInfo.stDestSize.u16Height = u32CapHeight;
    stVifPortInfo.ePixFormat = ePixFormat;
    //stVifPortInfo.u32FrameModeLineCount for lowlantancy mode

    if(stDevAttr.eIntfMode == E_MI_VIF_MODE_BT656)
    {
        stVifPortInfo.eFrameRate = E_MI_VIF_FRAMERATE_FULL;
        stVifPortInfo.eCapSel = E_MI_SYS_FIELDTYPE_BOTH;
        stVifPortInfo.eScanMode = E_MI_SYS_FRAME_SCAN_MODE_PROGRESSIVE;
    }
    STCHECKRESULT(MI_VIF_SetChnPortAttr(vifChn, vifPort, &stVifPortInfo));
    STCHECKRESULT(MI_VIF_EnableChnPort(vifChn, vifPort));

    /************************************************
    Step6: Vpe Start
    *************************************************/
    MI_VPE_ChannelPara_t stVpeChParam;
    memset(&stVpeChParam, 0x0, sizeof(MI_VPE_ChannelPara_t));

    STCHECKRESULT(MI_VPE_GetChannelParam(u8VpeChn, &stVpeChParam));
    stVpeChParam.eHDRType = pstVpeChnattr->eHdrType;
    STCHECKRESULT(MI_VPE_SetChannelParam(u8VpeChn, &stVpeChParam));

    STCHECKRESULT(MI_VPE_StartChannel(u8VpeChn));

    return 0;
}


MI_BOOL ST_DoChangeRotate(MI_U32 u32SensorNum)
{
    MI_S32 s32Rotation = 0;
    MI_S32 s32Mirror = 0;
    MI_S32 s32Flip = 0;
    MI_U8 i=0;
    MI_U32 u32VifDev=0, vifChn=0, VifPort=0;
    MI_S32  VpeChn = 0;
    MI_U32 u32MaxVencWidth =0, u32MaxVencHeight =0;

    if(u32SensorNum > 1)
    {
        printf("select channel id:");
        scanf("%d", &VpeChn);
        ST_Flush();

        if(VpeChn >= ST_MAX_SENSOR_NUM)
        {
            printf("VpeChn %d > max %d \n", VpeChn, ST_MAX_SENSOR_NUM);
            return 0;
        }
    }
    else
    {
        VpeChn = 0;
    }

    u32VifDev = VpeChn;
    vifChn = u32VifDev*4;

    ST_VpeChannelAttr_t *pstVpeChnattr = &gstVpeChnattr[VpeChn];
    ST_VencAttr_t *pstVencattr = gstVencattr;

    printf("rotation(0:0, 1:90, 2:180, 3:270):");
    scanf("%d", &s32Rotation);
    ST_Flush();

    printf("bmirror 0: FALSE, 1:TRUE :");
    scanf("%d", &s32Mirror);
    ST_Flush();

    printf("bFlip 0: FALSE, 1:TRUE :");
    scanf("%d", &s32Flip);
    ST_Flush();

    pstVpeChnattr->eVpeRotate = (MI_SYS_Rotate_e)s32Rotation;
    pstVpeChnattr->bChnFlip = s32Flip;
    pstVpeChnattr->bChnMirror = s32Mirror;

    /************************************************
    Step1: Stop Venc (Because rot will change preview resolution)
    *************************************************/
    if(g_bPreviewByVenc == TRUE)
    {
        ST_VencStop(VpeChn);
    }

    /************************************************
    Step2: Stop Vpe (Wait driver all buffer done)
    *************************************************/
    STCHECKRESULT(MI_VPE_StopChannel(VpeChn));

    /************************************************
    Step3: Disable Vif Port(Realtime mode Change Rot Will Change Isp Cfg, need stop push vif stream)
    *************************************************/
    if(pstVpeChnattr->eRunningMode == E_MI_VPE_RUN_REALTIME_MODE)
        STCHECKRESULT(MI_VIF_DisableChnPort(vifChn, VifPort));

    /************************************************
    Step4: Set Vpe Rot/ChnMirror/ChnFlip
    *************************************************/
    STCHECKRESULT(MI_VPE_SetChannelRotation(VpeChn, pstVpeChnattr->eVpeRotate));

    MI_VPE_ChannelPara_t stChnParam;
    memset(&stChnParam, 0x0, sizeof(MI_VPE_ChannelPara_t));

    STCHECKRESULT(MI_VPE_GetChannelParam(VpeChn, &stChnParam));
    stChnParam.bMirror = pstVpeChnattr->bChnMirror;
    stChnParam.bFlip = pstVpeChnattr->bChnFlip;
    STCHECKRESULT(MI_VPE_SetChannelParam(VpeChn, &stChnParam));

    /************************************************
    Step5: Rot switch Vpe Port size
    *************************************************/
    for(i=0; i<ST_MAX_PORT_NUM; i++)
    {
        MI_VPE_PortMode_t stVpeMode;
        MI_U32 u32VencChn = pstVpeChnattr->stVpePortAttr[i].u32BindVencChan;
        ST_VpePortAttr_t *pstVpePortAttr = &pstVpeChnattr->stVpePortAttr[i];
        MI_U32  u32SclSourceHeight = 0;

        memset(&stVpeMode, 0x0, sizeof(MI_VPE_PortMode_t));

        if(pstVpeChnattr->stVpePortAttr[i].bUsed == TRUE)
        {
            STCHECKRESULT(MI_VPE_GetPortMode(VpeChn , i, &stVpeMode));

            if(pstVpeChnattr->u32ChnPortMode ==0 
                || pstVpeChnattr->stVpeChnCrop.u16Width ==0
                || pstVpeChnattr->stVpeChnCrop.u16Height ==0)
            {
                MI_VIF_ChnPortAttr_t  stChnAttr;
                memset(&stChnAttr, 0x0, sizeof(MI_VIF_ChnPortAttr_t));
                STCHECKRESULT(MI_VIF_GetChnPortAttr(vifChn, 0,  &stChnAttr));

                u32SclSourceHeight = stChnAttr.stCapRect.u16Height;
            }
            else
            {
                u32SclSourceHeight =  pstVpeChnattr->stOrgVpeChnCrop.u16Height;
            }

            if(pstVpeChnattr->eVpeRotate == E_MI_SYS_ROTATE_90
               || pstVpeChnattr->eVpeRotate == E_MI_SYS_ROTATE_270)
            {
                if(pstVpePortAttr->stOrigPortCrop.u16Height !=0)
                    pstVpePortAttr->stPortCrop.u16X = u32SclSourceHeight - pstVpePortAttr->stOrigPortCrop.u16Y - pstVpePortAttr->stOrigPortCrop.u16Height;
                else
                    pstVpePortAttr->stPortCrop.u16X = 0;

                pstVpePortAttr->stPortCrop.u16Y = pstVpePortAttr->stOrigPortCrop.u16X;
                pstVpePortAttr->stPortCrop.u16Width = pstVpePortAttr->stOrigPortCrop.u16Height;
                pstVpePortAttr->stPortCrop.u16Height = pstVpePortAttr->stOrigPortCrop.u16Width;

                pstVpePortAttr->stPortSize.u16Width = pstVpePortAttr->stOrigPortSize.u16Height;
                pstVpePortAttr->stPortSize.u16Height = pstVpePortAttr->stOrigPortSize.u16Width;

                u32MaxVencWidth = 1080;
                u32MaxVencHeight = 1920;
            }
            else
            {
                pstVpePortAttr->stPortCrop.u16X = pstVpePortAttr->stOrigPortCrop.u16X;
                pstVpePortAttr->stPortCrop.u16Y = pstVpePortAttr->stOrigPortCrop.u16Y;
                pstVpePortAttr->stPortCrop.u16Width = pstVpePortAttr->stOrigPortCrop.u16Width;
                pstVpePortAttr->stPortCrop.u16Height = pstVpePortAttr->stOrigPortCrop.u16Height;

                pstVpePortAttr->stPortSize.u16Width = pstVpePortAttr->stOrigPortSize.u16Width;
                pstVpePortAttr->stPortSize.u16Height = pstVpePortAttr->stOrigPortSize.u16Height;

                u32MaxVencWidth = 1920;
                u32MaxVencHeight = 1080;
            }
            if(i < ST_MAX_SCL_NUM)
            {
                STCHECKRESULT(MI_VPE_SetPortCrop(VpeChn, i, &pstVpePortAttr->stPortCrop));
            }
            stVpeMode.u16Width = pstVpePortAttr->stPortSize.u16Width;
            stVpeMode.u16Height = pstVpePortAttr->stPortSize.u16Height;
            STCHECKRESULT(MI_VPE_SetPortMode(VpeChn , i, &stVpeMode));

            pstVencattr[u32VencChn].u32Width = stVpeMode.u16Width;
            pstVencattr[u32VencChn].u32Height = stVpeMode.u16Height;
        }
    }

    /************************************************
    Step7: Start Vif
    *************************************************/
    if(pstVpeChnattr->eRunningMode == E_MI_VPE_RUN_REALTIME_MODE)
        STCHECKRESULT(MI_VIF_EnableChnPort(vifChn, VifPort));

    /************************************************
    Step8: Start Vpe
    *************************************************/
    STCHECKRESULT(MI_VPE_StartChannel (VpeChn));

    /************************************************
    Step9: Start Venc
    *************************************************/
    if(g_bPreviewByVenc == TRUE)
    {
        ST_VencStart(u32MaxVencWidth, u32MaxVencHeight, VpeChn);
    }

    return 0;
}

MI_BOOL ST_ChangePortMode(MI_U32 u32SensorNum)
{
    MI_S32 s32Portid = 0;
    MI_S32 s32PortPixelFormat =0;
    MI_S32 s32PortMirror=0, s32PortFlip=0;
    MI_S32 s32PortW=0, s32PortH=0;
    MI_VPE_PortMode_t  stVpePortMode;
    memset(&stVpePortMode, 0x0, sizeof(MI_VPE_PortMode_t));

    MI_U32  VpeChn =0;

    if(u32SensorNum > 1)
    {
        printf("select channel id:");
        scanf("%d", &VpeChn);
        ST_Flush();

        if(VpeChn >= ST_MAX_SENSOR_NUM)
        {
            printf("VpeChn %d > max %d \n", VpeChn, ST_MAX_SENSOR_NUM);
            return 0;
        }
    }
    else
    {
        VpeChn = 0;
    }

    printf("select port id:");
    scanf("%d", &s32Portid);
    ST_Flush();

    if(s32Portid >= ST_MAX_PORT_NUM)
    {
        printf("port %d, not valid \n", s32Portid);
        return 0;
    }

    printf("port %d bmirror:", s32Portid);
    scanf("%d", &s32PortMirror);
    ST_Flush();
    
    printf("port %d bflip:", s32Portid);
    scanf("%d", &s32PortFlip);
    ST_Flush();
    
    printf("port %d port width:", s32Portid);
    scanf("%d", &s32PortW);
    ST_Flush();
    
    printf("port %d port height:", s32Portid);
    scanf("%d", &s32PortH);
    ST_Flush();

    ST_VpeChannelAttr_t *pstVpeChnattr = &gstVpeChnattr[VpeChn];
    ST_VpePortAttr_t *pstVpePortAttr = &pstVpeChnattr->stVpePortAttr[s32Portid];
    MI_U32 U32VencChn = pstVpePortAttr->u32BindVencChan;
    ST_VencAttr_t *pstVencattr = &gstVencattr[U32VencChn];

    STCHECKRESULT(MI_VPE_GetPortMode(VpeChn, s32Portid, &stVpePortMode));

    if(g_bPreviewByVenc == FALSE)
    {
        printf("port %d port pixel:", s32Portid);
        printf("yuv422:0, argb8888:1, abgr8888:2, bgra8888:3, yuv420:11\n");
        scanf("%d", &s32PortPixelFormat);
        ST_Flush();
    }
    else
        s32PortPixelFormat = stVpePortMode.ePixelFormat;

    /************************************************
    Step1: Stop Venc (Because rot will change preview resolution)
    *************************************************/
    if(g_bPreviewByVenc == TRUE)
    {
        if(pstVencattr->bUsed == TRUE)
            ExecFunc(MI_VENC_StopRecvPic(pstVencattr->vencChn), MI_SUCCESS);
    }

    /************************************************
    Step2: disable Vpe port
    *************************************************/
    STCHECKRESULT(MI_VPE_DisablePort(VpeChn, s32Portid));

    /************************************************
    Step3: Set Port Mode
    *************************************************/
    pstVpePortAttr->bMirror = s32PortMirror;
    pstVpePortAttr->bFlip = s32PortFlip;
    pstVpePortAttr->stOrigPortSize.u16Width = s32PortW;
    pstVpePortAttr->stOrigPortSize.u16Height = s32PortH;
    pstVpePortAttr->ePixelFormat = (MI_SYS_PixelFormat_e)s32PortPixelFormat;
    pstVpePortAttr->bUsed = TRUE;

    stVpePortMode.bMirror = pstVpePortAttr->bMirror;
    stVpePortMode.bFlip = pstVpePortAttr->bFlip;
    stVpePortMode.ePixelFormat = pstVpePortAttr->ePixelFormat;
    if(pstVpeChnattr->eVpeRotate == E_MI_SYS_ROTATE_90
        || pstVpeChnattr->eVpeRotate == E_MI_SYS_ROTATE_270)
    {
        stVpePortMode.u16Width = pstVpePortAttr->stOrigPortSize.u16Height;
        stVpePortMode.u16Height = pstVpePortAttr->stOrigPortSize.u16Width;
    }
    else
    {
        stVpePortMode.u16Width = pstVpePortAttr->stOrigPortSize.u16Width;
        stVpePortMode.u16Height = pstVpePortAttr->stOrigPortSize.u16Height;
    }

    STCHECKRESULT(MI_VPE_SetPortMode(VpeChn, s32Portid, &stVpePortMode));

    /************************************************
    Step4: Set Venc Channel Pitch
    *************************************************/
    if(g_bPreviewByVenc == TRUE)
    {
        MI_VENC_ChnAttr_t stChnAttr;
        memset(&stChnAttr, 0x0, sizeof(MI_VENC_ChnAttr_t));
    
        if(pstVencattr->bUsed == TRUE)
        {
            ExecFunc(MI_VENC_GetChnAttr(pstVencattr->vencChn, &stChnAttr), MI_SUCCESS);
            if(pstVencattr->eType == E_MI_VENC_MODTYPE_H264E)
            {
                stChnAttr.stVeAttr.stAttrH264e.u32PicWidth = stVpePortMode.u16Width;
                stChnAttr.stVeAttr.stAttrH264e.u32PicHeight = stVpePortMode.u16Height;
            }
            else if(pstVencattr->eType == E_MI_VENC_MODTYPE_H264E)
            {
                stChnAttr.stVeAttr.stAttrH265e.u32PicWidth = stVpePortMode.u16Width;
                stChnAttr.stVeAttr.stAttrH265e.u32PicHeight = stVpePortMode.u16Height;
            }
            else if(pstVencattr->eType == E_MI_VENC_MODTYPE_H264E)
            {
                stChnAttr.stVeAttr.stAttrJpeg.u32PicWidth = stVpePortMode.u16Width;
                stChnAttr.stVeAttr.stAttrJpeg.u32PicHeight = stVpePortMode.u16Height;
            }
            ExecFunc(MI_VENC_SetChnAttr(pstVencattr->vencChn, &stChnAttr), MI_SUCCESS);
        }
        else
            printf("port %d, venc buse %d \n", s32Portid, pstVencattr->bUsed);
    }

    /************************************************
    Step5: Enable Vpe Port
    *************************************************/
    STCHECKRESULT(MI_VPE_EnablePort(VpeChn, s32Portid));

    /************************************************
    Step6: Start Venc
    *************************************************/
    if(g_bPreviewByVenc == TRUE)
    {
        if(pstVencattr->bUsed == TRUE)
            ExecFunc(MI_VENC_StartRecvPic(pstVencattr->vencChn), MI_SUCCESS);
    }

    return 0;

}

MI_BOOL ST_DoSetChnCrop(MI_U32 u32SensorNum)
{
    MI_S32 s32ChannelCropX =0, s32ChannelCropY=0,s32ChannelCropW=0,s32ChannelCropH =0;
    MI_SYS_WindowRect_t stVpeChnCrop;
    memset(&stVpeChnCrop, 0x0, sizeof(MI_SYS_WindowRect_t));

    MI_U32  VpeChn =0;

    if(u32SensorNum > 1)
    {
        printf("select channel id:");
        scanf("%d", &VpeChn);
        ST_Flush();

        if(VpeChn >= ST_MAX_SENSOR_NUM)
        {
            printf("VpeChn %d > max %d \n", VpeChn, ST_MAX_SENSOR_NUM);
            return 0;
        }
    }
    else
    {
        VpeChn = 0;
    }

    ST_VpeChannelAttr_t *pstVpeChnattr = &gstVpeChnattr[VpeChn];

    printf("Channel Crop x:");
    scanf("%d", &s32ChannelCropX);
    ST_Flush();

    printf("Channel Crop y:");
    scanf("%d", &s32ChannelCropY);
    ST_Flush();

    printf("Channel Crop width:");
    scanf("%d", &s32ChannelCropW);
    ST_Flush();

    printf("Channel Crop height:");
    scanf("%d", &s32ChannelCropH);
    ST_Flush();

    pstVpeChnattr->stOrgVpeChnCrop.u16X = s32ChannelCropX;
    pstVpeChnattr->stOrgVpeChnCrop.u16Y = s32ChannelCropY;
    pstVpeChnattr->stOrgVpeChnCrop.u16Width = s32ChannelCropW;
    pstVpeChnattr->stOrgVpeChnCrop.u16Height = s32ChannelCropH;

    if(pstVpeChnattr->eVpeRotate == E_MI_SYS_ROTATE_90
        || pstVpeChnattr->eVpeRotate == E_MI_SYS_ROTATE_270)
    {
        stVpeChnCrop.u16X = pstVpeChnattr->stOrgVpeChnCrop.u16Y;
        stVpeChnCrop.u16Y = pstVpeChnattr->stOrgVpeChnCrop.u16X;
        stVpeChnCrop.u16Width = pstVpeChnattr->stOrgVpeChnCrop.u16Height;
        stVpeChnCrop.u16Height = pstVpeChnattr->stOrgVpeChnCrop.u16Width;
    }
    else
    {
        stVpeChnCrop.u16X = pstVpeChnattr->stOrgVpeChnCrop.u16X;
        stVpeChnCrop.u16Y = pstVpeChnattr->stOrgVpeChnCrop.u16Y;
        stVpeChnCrop.u16Width = pstVpeChnattr->stOrgVpeChnCrop.u16Width;
        stVpeChnCrop.u16Height = pstVpeChnattr->stOrgVpeChnCrop.u16Height;
    }

    STCHECKRESULT(MI_VPE_SetChannelCrop(VpeChn,&stVpeChnCrop));

    return 0;
}

MI_BOOL ST_DoSetChnZoom(MI_U32 u32SensorNum)
{
    float r = 16.0/9;
    MI_U16 ystep = 8;
    MI_U16 xstep =ALIGN_UP((MI_U16)(r*ystep), 2);
    int oriW = 0, oriH = 0;
    MI_SYS_WindowRect_t stCropInfo;
    MI_VIF_ChnPortAttr_t stVifPortInfo;
    MI_U32 u32SleepTimeUs = 0;
    MI_U32 u32Fps =0;
    MI_S32 s32ZoomPosition = 0;
    MI_S32 s32PortZoom = 0;
    MI_BOOL bZoomDone = TRUE;
    memset(&stVifPortInfo, 0x0, sizeof(MI_VIF_ChnPortAttr_t));
    memset(&stCropInfo, 0, sizeof(MI_SYS_WindowRect_t));

    MI_U32  VpeChn =0;
    if(u32SensorNum > 1)
    {
        printf("select channel id:");
        scanf("%d", &VpeChn);
        ST_Flush();

        if(VpeChn >= ST_MAX_SENSOR_NUM)
        {
            printf("VpeChn %d > max %d \n", VpeChn, ST_MAX_SENSOR_NUM);
            return 0;
        }
    }
    else
    {
        VpeChn = 0;
    }

    MI_U32 u32VifDev = VpeChn;
    MI_U32 u32VifChn = u32VifDev*4;
    MI_SNR_PAD_ID_e eSnrPadId = E_MI_SNR_PAD_ID_0;
    if(u32VifDev == 0)
        eSnrPadId = E_MI_SNR_PAD_ID_0;
    else if(u32VifDev == 2)
        eSnrPadId = E_MI_SNR_PAD_ID_1;
    else
        MIXER_ERR("VIF DEV ERR %d \n", u32VifDev);

    MI_VIF_GetChnPortAttr(u32VifChn,0,&stVifPortInfo);
    oriW = stVifPortInfo.stCapRect.u16Width;
    oriH = stVifPortInfo.stCapRect.u16Height;

    MI_SNR_GetFps(eSnrPadId, &u32Fps);

    u32SleepTimeUs = 1000000/u32Fps;
    printf("fps %d, sleeptime %d \n", u32Fps, u32SleepTimeUs);

    printf("set zoom position: 1.vif, 2.vpe isp dma, 3.vpe scl pre-crop");
    scanf("%d", &s32ZoomPosition);
    ST_Flush();

    if(s32ZoomPosition == 3)
    {
        printf("select which port zoom: 0:port0, 1:port1, 2:port2, 3: all port \n");
        scanf("%d", &s32PortZoom);
        ST_Flush();
    }

    while(1)
    {
        if(bZoomDone == TRUE)
        {
            stCropInfo.u16X += xstep;
            stCropInfo.u16Y += ystep;
            stCropInfo.u16Width = oriW - (2 * stCropInfo.u16X);
            stCropInfo.u16Height = oriH -(2 * stCropInfo.u16Y);

            stCropInfo.u16Width = ALIGN_UP(stCropInfo.u16Width, 2);
            stCropInfo.u16Height = ALIGN_UP(stCropInfo.u16Height, 2);

            if(stCropInfo.u16Width < 660 || stCropInfo.u16Height < 360)
            {
                bZoomDone = FALSE;
            }
        }
        else
        {
            stCropInfo.u16X -= xstep;
            stCropInfo.u16Y -= ystep;
            stCropInfo.u16Width = oriW - (2 * stCropInfo.u16X);
            stCropInfo.u16Height = oriH -(2 * stCropInfo.u16Y);

            stCropInfo.u16Width = ALIGN_UP(stCropInfo.u16Width, 2);
            stCropInfo.u16Height = ALIGN_UP(stCropInfo.u16Height, 2);

            if(stCropInfo.u16Width > oriW || stCropInfo.u16Height > oriH)
            {
                break;
            }
        }

        if(s32ZoomPosition == 1)
        {
            MI_VIF_ChnPortAttr_t stChnPortAttr;
            ExecFunc(MI_VIF_GetChnPortAttr(u32VifChn, 0, &stChnPortAttr), MI_SUCCESS);

            memcpy(&stChnPortAttr.stCapRect, &stCropInfo, sizeof(MI_SYS_WindowRect_t));

            stChnPortAttr.stDestSize.u16Width = stCropInfo.u16Width;
            stChnPortAttr.stDestSize.u16Height = stCropInfo.u16Height;

            ExecFunc(MI_VIF_SetChnPortAttr(u32VifChn, 0, &stChnPortAttr), MI_SUCCESS);
        }
        else if(s32ZoomPosition == 2)
        {
            STCHECKRESULT(MI_VPE_SetChannelCrop(VpeChn,  &stCropInfo));
            STCHECKRESULT(MI_VPE_GetChannelCrop(VpeChn,  &stCropInfo));
        }
        else if(s32ZoomPosition == 3)
        {
            if(s32PortZoom == 3)
            {
                MI_VPE_SetPortCrop(VpeChn, 0, &stCropInfo);
                MI_VPE_SetPortCrop(VpeChn, 1, &stCropInfo);
                MI_VPE_SetPortCrop(VpeChn, 2, &stCropInfo);
            }
            else
                MI_VPE_SetPortCrop(VpeChn, s32PortZoom, &stCropInfo);
        }
        printf("after crop down x:%d y:%d w:%d h:%d\n", stCropInfo.u16X, stCropInfo.u16Y, stCropInfo.u16Width, stCropInfo.u16Height);

        //ST_Flush();

        usleep(u32SleepTimeUs);
    }

    return 0;
}

MI_BOOL ST_DoSetPortCrop(MI_U32 u32SensorNum)
{
    MI_S32 s32Portid = 0;
    MI_S32 s32PortCropX =0, s32PortCropY=0,s32PortCropW=0,s32PortCropH =0;
    MI_SYS_WindowRect_t stPortCropSize;
    memset(&stPortCropSize, 0x0, sizeof(MI_SYS_WindowRect_t));
    MI_U32  VpeChn =0;

    if(u32SensorNum > 1)
    {
        printf("select channel id:");
        scanf("%d", &VpeChn);
        ST_Flush();

        if(VpeChn >= ST_MAX_SENSOR_NUM)
        {
            printf("VpeChn %d > max %d \n", VpeChn, ST_MAX_SENSOR_NUM);
            return 0;
        }
    }
    else
    {
        VpeChn = 0;
    }

    ST_VpeChannelAttr_t *pstVpeChnattr = &gstVpeChnattr[VpeChn];
    ST_VpePortAttr_t *pstVpePortAttr = pstVpeChnattr->stVpePortAttr;

    printf("select port id:");
    scanf("%d", &s32Portid);
    ST_Flush();

    if(s32Portid >= ST_MAX_PORT_NUM || pstVpePortAttr[s32Portid].bUsed != TRUE)
    {
        printf("port %d, not valid \n", s32Portid);
        return 0;
    }

     printf("port %d port crop x:", s32Portid);
    scanf("%d", &s32PortCropX);
    ST_Flush();

    printf("port %d port crop y:", s32Portid);
    scanf("%d", &s32PortCropY);
    ST_Flush();

    printf("port %d port crop width:", s32Portid);
    scanf("%d", &s32PortCropW);
    ST_Flush();

    printf("port %d port crop height:", s32Portid);
    scanf("%d", &s32PortCropH);
    ST_Flush();

    pstVpePortAttr[s32Portid].stOrigPortCrop.u16X = s32PortCropX;
    pstVpePortAttr[s32Portid].stOrigPortCrop.u16Y = s32PortCropY;
    pstVpePortAttr[s32Portid].stOrigPortCrop.u16Width = s32PortCropW;
    pstVpePortAttr[s32Portid].stOrigPortCrop.u16Height = s32PortCropH;

    if(pstVpeChnattr->eVpeRotate == E_MI_SYS_ROTATE_90
        || pstVpeChnattr->eVpeRotate == E_MI_SYS_ROTATE_270)
    {
        stPortCropSize.u16X = pstVpePortAttr[s32Portid].stOrigPortCrop.u16Y;
        stPortCropSize.u16Y = pstVpePortAttr[s32Portid].stOrigPortCrop.u16X;
        stPortCropSize.u16Width = pstVpePortAttr[s32Portid].stOrigPortCrop.u16Height;
        stPortCropSize.u16Height = pstVpePortAttr[s32Portid].stOrigPortCrop.u16Width;
    }
    else
    {
        stPortCropSize.u16X = pstVpePortAttr[s32Portid].stOrigPortCrop.u16X;
        stPortCropSize.u16Y = pstVpePortAttr[s32Portid].stOrigPortCrop.u16Y;
        stPortCropSize.u16Width = pstVpePortAttr[s32Portid].stOrigPortCrop.u16Width;
        stPortCropSize.u16Height = pstVpePortAttr[s32Portid].stOrigPortCrop.u16Height;
    }
    
    STCHECKRESULT(MI_VPE_SetPortCrop(VpeChn , s32Portid, &stPortCropSize));

    return 0;
}
MI_BOOL ST_DoSetIqBin(MI_VPE_CHANNEL Vpechn,char *pConfigPath)
{
  //  MI_U32 VpeChn = 0;
    MI_ISP_IQ_PARAM_INIT_INFO_TYPE_t status;
    MI_U8  u8ispreadycnt = 0;
    if (strlen(pConfigPath) == 0)
    {
        printf("IQ Bin File path NULL!\n");
        return FALSE;
    }

    do
    {
        if(u8ispreadycnt > 100)
        {
            printf("%s:%d, isp ready time out \n", __FUNCTION__, __LINE__);
            u8ispreadycnt = 0;
            break;
        }

        MI_ISP_IQ_GetParaInitStatus(Vpechn, &status);
        if(status.stParaAPI.bFlag != 1)
        {
            usleep(300*1000);
            u8ispreadycnt++;
            continue;
        }

        u8ispreadycnt = 0;

        printf("loading api bin...path:%s\n",pConfigPath);
        MI_ISP_API_CmdLoadBinFile(Vpechn, (char *)pConfigPath, 1234);

        usleep(10*1000);
    }while(!status.stParaAPI.bFlag);

    return 0;
}

int ST_StartStream(void)
{
    MI_U8 i = 0;
    MI_U32 u32MaxWidth  = 0;
    MI_U32 u32MaxHeight = 0;
    MI_SNR_PAD_ID_e eSnrPad = E_MI_SNR_PAD_ID_0;
    
    for(i = 0; i < ST_MAX_SENSOR_NUM; i++)
    {
        if(gstSensorAttr[i].bUsed != TRUE)
            continue;
        
        eSnrPad = (MI_SNR_PAD_ID_e)i;
        MI_VIF_DEV vifDev = gstSensorAttr[eSnrPad].u32BindVifDev;
        MI_VPE_CHANNEL vpechn = gstVifAttr[vifDev].u32BindVpeChan;

        STCHECKRESULT(ST_BaseModuleInit(eSnrPad));

        if(g_bPreviewByVenc != TRUE)
            continue;  
               
        ST_VpeChannelAttr_t *pstVpeChnattr = &gstVpeChnattr[vpechn];
        u32MaxWidth  = pstVpeChnattr->stVpePortAttr[i].stPortSize.u16Width;
        u32MaxHeight = pstVpeChnattr->stVpePortAttr[i].stPortSize.u16Height;
        printf("%s:%d u32MaxWidth=%d, u32MaxHeight=%d \n", __func__, __LINE__, u32MaxWidth, u32MaxHeight);
        STCHECKRESULT(ST_VencStart(u32MaxWidth,u32MaxHeight, vpechn));            
    }

    ST_RtspServerStart();
    
    return 0;
}

int ST_StopStream(void)
{
    MI_U8 i = 0;
    MI_SNR_PAD_ID_e eSnrPad = E_MI_SNR_PAD_ID_0;
    
    ST_RtspServerStop();
    
    for(i = 0; i < ST_MAX_SENSOR_NUM; i++)
    {
        if(gstSensorAttr[i].bUsed != TRUE)
            continue;
        
        eSnrPad = (MI_SNR_PAD_ID_e)i;
        MI_VIF_DEV vifDev = gstSensorAttr[eSnrPad].u32BindVifDev;
        MI_VPE_CHANNEL vpechn = gstVifAttr[vifDev].u32BindVpeChan;

        if(g_bPreviewByVenc == TRUE)
        {
            STCHECKRESULT(ST_VencStop(vpechn));
        }

        STCHECKRESULT(ST_BaseModuleUnInit(eSnrPad));
    }
    
    return 0;
}

MI_BOOL ST_CommandHandle(MI_U32 u32SensorNum)
{
    MI_BOOL bRet = FALSE;
    MI_U32 u32Select = 0xff;
    MI_VPE_CHANNEL Vpechn = 0;
    char sFilePath[128];

    printf("select 0: change Hdr res \n");
    printf("select 2: change rotate\n");
    printf("select 3: change chancrop\n");
    printf("select 4: change portMode\n");
    printf("select 5: change portcrop\n");
    printf("select 6: Get port buffer\n");
    printf("select 7: disable port \n");
    printf("select 8: get venc out \n");
    printf("select 9: Ldc on/off \n");
    printf("select 10: vpe chn zoom\n");
    printf("select 11: set api bin\n");
    printf("select 12: enter add face feat if fdfr\n");
    printf("select 13: exit\n");
    
    scanf("%d", &u32Select);
    ST_Flush();
    
    switch(u32Select)
    {
        case 0:
            bRet = ST_DoChangeHdrRes(u32SensorNum);
            break;
        case 2:
            bRet = ST_DoChangeRotate(u32SensorNum);
            break;
        case 3:
            bRet = ST_DoSetChnCrop(u32SensorNum);
            break;
        case 4:
            bRet = ST_ChangePortMode(u32SensorNum);
            break;
        case 5:
            bRet = ST_DoSetPortCrop(u32SensorNum);
            break;
        case 6:
            bRet = ST_GetVpeOutputData(u32SensorNum);
            break;
        case 7:
            bRet = ST_VpeDisablePort(u32SensorNum);
            break;
        case 8:
            bRet = ST_GetVencOut();
            break;
        case 9:
            bRet = ST_SetLdcOnOff(u32SensorNum);
            break;
        case 10:
            bRet = ST_DoSetChnZoom(u32SensorNum);
            break;
        case 11:
            {
                printf("Dynamic load IQ bin\n");
                if(u32SensorNum > 1)
                {
                    printf("select channel id:");
                    scanf("%d", &Vpechn);
                    ST_Flush();

                    if(Vpechn >= ST_MAX_SENSOR_NUM)
                    {
                        printf("VpeChn %d > max %d \n", Vpechn, ST_MAX_SENSOR_NUM);
                        return FALSE;
                    }
                }
                else
                {
                    Vpechn = 0;
                }

                printf("write API Bin file path:\n");
                scanf("%s", sFilePath);
                ST_Flush();
                if(strlen(sFilePath)>0)
                {
                    bRet = ST_DoSetIqBin(Vpechn,sFilePath);
                }
            }
            break;
        case 12:
            IPURunCommand(12);
            break;
        case 13:
            bRet = TRUE;
            break;
    }

    return bRet;
}

//					  argb888
//sys->vif->vpe->divp ----------> ipu-|===||
//			|===========================>rgn==========>venc	(1080p)
int main(int argc, char **argv)
{
    MI_U8 i = 0;
    MI_U8 j = 0;
    MI_U8 size = 0;
    MI_U32 u32SensorNum = 0;    
    MI_SNR_PAD_ID_e eSnrPad = E_MI_SNR_PAD_ID_0;

    if(argc < 2)
    {
        printf("please input ini file\n");
        return 0;
    }
    
    for(i = 1; i < argc; i++)
    {
        if(MI_SUCCESS != ST_ParserIni(argv[i]))
        {
            printf("parse init fail \n");
            return 0;
        }
        u32SensorNum++;
    }
    
    printf("sensor NUM %d \n", u32SensorNum);

    ST_StartStream();
    OsdInitAndStart();
    if(gstDlaInfo.bDlaUse == TRUE)
        IPUInitAndStart(gstDlaInfo);
    
#if 0
    pthread_t pIQthread;
    pthread_create(&pIQthread, NULL, ST_IQthread, NULL);
#endif

    //pthread_t pSenVpeDatathread;
    //pthread_create(&pSenVpeDatathread, NULL, ST_SendVpeBufthread, NULL);

    if(g_bPreviewByVenc == FALSE)
    {
        MI_U8 u8PortId[ST_MAX_SENSOR_NUM*ST_MAX_PORT_NUM];

        for(j=0; j< ST_MAX_SENSOR_NUM; j++)
        {
            for(i=0; i< ST_MAX_PORT_NUM; i++)
            {
                u8PortId[j*ST_MAX_SENSOR_NUM+i] = j*ST_MAX_SENSOR_NUM+i;
                pthread_mutex_init(&gstVpeChnattr[j].stVpePortAttr[i].Portmutex, NULL);
                pthread_create(&gstVpeChnattr[j].stVpePortAttr[i].pGetDatathread, NULL, ST_GetVpeOutputDataThread, (void *)(&u8PortId[i]));
            }
        }
    }

    for(i=0; i<ST_MAX_SENSOR_NUM; i++)
    {
        if(gstSensorAttr[i].bUsed == TRUE)
        {
            eSnrPad = (MI_SNR_PAD_ID_e)i;

            MI_SNR_PlaneInfo_t stSnrPlane0Info;
            memset(&stSnrPlane0Info, 0x0, sizeof(MI_SNR_PlaneInfo_t));

            STCHECKRESULT(MI_SNR_GetPlaneInfo(eSnrPad, 0, &stSnrPlane0Info));

            STCHECKRESULT(MI_IQSERVER_Open(stSnrPlane0Info.stCapRect.u16Width, stSnrPlane0Info.stCapRect.u16Height, 0));
        }
    }

    for(i=0; i<ST_MAX_SENSOR_NUM; i++)
    {
        ST_VpeChannelAttr_t *pstVpeChnattr = &gstVpeChnattr[i];
        size = strlen(pstVpeChnattr->IqCfgbin_Path);
         if(0 != size)
        {
            printf("pstVpeChnattr->IqCfgbin_Path = %s, vpechn %d\n",pstVpeChnattr->IqCfgbin_Path,i);
            ST_DoSetIqBin(i,pstVpeChnattr->IqCfgbin_Path);
        }
    }

    while(!g_bExit)
    {
        g_bExit = ST_CommandHandle(u32SensorNum);
        usleep(100 * 1000);
    }

    usleep(100 * 1000);

    if(g_bPreviewByVenc == FALSE)
    {
        for(j=0; j< ST_MAX_SENSOR_NUM; j++)
        {
            for(i=0; i< ST_MAX_PORT_NUM; i++)
            {
                void *retarg = NULL;
                pthread_cancel(gstVpeChnattr[j].stVpePortAttr[i].pGetDatathread);
                pthread_join(gstVpeChnattr[j].stVpePortAttr[i].pGetDatathread, &retarg);
            }

            for(i=0; i< ST_MAX_PORT_NUM; i++)
            {
                pthread_mutex_destroy(&gstVpeChnattr[j].stVpePortAttr[i].Portmutex);
            }
        }
    }

    if(gstDlaInfo.bDlaUse == TRUE)
        IPUUnInitAndStop();
    OsdUnInitAndStop();
    ST_StopStream();

    memset(&gstSensorAttr, 0x0, sizeof(gstSensorAttr));
    memset(&gstVifAttr, 0x0, sizeof(gstVifAttr));
    memset(&gstVpeChnattr, 0x0, sizeof(gstVpeChnattr));
    memset(&gstVencattr, 0x0, sizeof(gstVencattr));

    return 0;
}

