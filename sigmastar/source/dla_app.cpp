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
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <fstream>
#include <iostream>
#include <map>

#include "st_common.h"
#include "dla_base.h"
#include "dla_detect.h"
#include "dla_fdfr.h"
#include "dla_app.h"
#include "thread_api.h"

MI_BOOL g_DlaExit = FALSE;
pthread_t g_stPthreadDla = -1;

CIpuInterface* g_IpuIntfObject;

void *IPUTask(void *arg)
{
    if(!g_IpuIntfObject)
        return 0;
    
    while(FALSE == g_DlaExit)
    {
       g_IpuIntfObject->IpuRunProcess();
    }
    return 0;
}

void IPURunCommand(MI_U32 u32CmdCode)
{
    if(g_IpuIntfObject)
        g_IpuIntfObject->IpuRunCommand(u32CmdCode);
}

void IPUThreadInit(void)
{   
    g_DlaExit = FALSE;
    Create_thread(&g_stPthreadDla, 80, "IPUTask", IPUTask, (void*)NULL);
}

void IPUThreadUnInit(void)
{     
    g_DlaExit = TRUE; 
	pthread_cancel(g_stPthreadDla);
    pthread_join(g_stPthreadDla, NULL);
}

int IPUModuleInit(IPU_DlaInfo_S stDlaInfo)
{
    if(!stDlaInfo.bDlaUse)
        return E_MI_ERR_FAILED;
        
    switch (stDlaInfo.stIpuInitInfo.enModelType)
    {
        case Model_Type_Detect:
            g_IpuIntfObject = new CIpuDetect(stDlaInfo);
            break;
        case Model_Type_FaceReg:
            g_IpuIntfObject = new CIpuFdfr(stDlaInfo);
            break;
        default:
            g_IpuIntfObject = NULL;
            return E_MI_ERR_FAILED;
    }
    return MI_SUCCESS;
}

void IPUModuleUnInit(void)
{   
    if(g_IpuIntfObject)
    {
        delete g_IpuIntfObject;
        g_IpuIntfObject = NULL;
    }
}

void IPUInitAndStart(IPU_DlaInfo_S stDlaInfo)
{   
	IPUModuleInit(stDlaInfo);
    IPUThreadInit();
}

void IPUUnInitAndStop(void)
{
    IPUThreadUnInit();
    IPUModuleUnInit();
}

