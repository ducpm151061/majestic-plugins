#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <fstream>
#include <iostream>
#include <map>

#include "st_common.h"
#include "common.h"
#include "mi_ipu.h"
#include "mi_vpe.h"
#include "divp_app.h"
#include "dla_base.h"
#include "sigma_model.h"

using namespace std;

CIpuCommon::CIpuCommon(IPU_DlaInfo_S &stDlaInfo) 
    :CIpuInterface(stDlaInfo)
{
}

CIpuCommon::~CIpuCommon()
{
}

MI_S32 CIpuCommon::IpuGetModelBufSize(MI_U32& u32BufSize)
{
    MI_IPU_OfflineModelStaticInfo_t stOfflineModelInfo;

    ExecFunc(MI_IPU_GetOfflineModeStaticInfo(NULL, m_stDlaInfo.stIpuInitInfo.szModelFile, &stOfflineModelInfo), MI_SUCCESS);

    u32BufSize = stOfflineModelInfo.u32VariableBufferSize;
    
    return MI_SUCCESS;
}

MI_S32 CIpuCommon::IpuCreateDevice(MI_U32 u32BufSize)
{
    MI_IPU_DevAttr_t stDevAttr;
    
    stDevAttr.u32MaxVariableBufSize = u32BufSize;
    stDevAttr.u32YUV420_W_Pitch_Alignment = 16;
    stDevAttr.u32YUV420_H_Pitch_Alignment = 2;
    stDevAttr.u32XRGB_W_Pitch_Alignment = 16;
    
    ExecFunc(MI_IPU_CreateDevice(&stDevAttr, NULL, m_stDlaInfo.stIpuInitInfo.szIpuFirmware, 0), MI_SUCCESS);
    
    return MI_SUCCESS;
}

MI_S32 CIpuCommon::IpuDestroyDevice()
{
    return MI_IPU_DestroyDevice();
}

MI_S32 CIpuCommon::IpuCreateChannel(char* pModelFile, IPU_ModelInfo_S* pstModelInfo)
{
    MI_IPUChnAttr_t stChnAttr;

    memset(&stChnAttr, 0, sizeof(stChnAttr));
    stChnAttr.u32InputBufDepth  = pstModelInfo->u32InBufDepth;
    stChnAttr.u32OutputBufDepth = pstModelInfo->u32OutBufDepth;

    if(strncmp(pModelFile,"enc",3)==0)
		ExecFunc(sigmaLoadModel(&pstModelInfo->u32IpuChn, &stChnAttr, pModelFile), MI_SUCCESS);
	else 
        ExecFunc(MI_IPU_CreateCHN(&pstModelInfo->u32IpuChn, &stChnAttr, NULL, pModelFile), MI_SUCCESS);

    ExecFunc(MI_IPU_GetInOutTensorDesc(pstModelInfo->u32IpuChn, &pstModelInfo->stIpuDesc), MI_SUCCESS);   
    
    MI_S32 iResizeH = pstModelInfo->stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[1];
    MI_S32 iResizeW = pstModelInfo->stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[2];
    MI_S32 iResizeC = pstModelInfo->stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[3];
	MIXER_INFO("iResizeH=%d iResizeW=%d iResizeC=%d\n",iResizeH,iResizeW,iResizeC);
    
    return MI_SUCCESS;
}

MI_S32 CIpuCommon::IpuDestroyChannel(IPU_ModelInfo_S* pstModelInfo)
{
    return MI_IPU_DestroyCHN(pstModelInfo->u32IpuChn);
}

MI_S32 CIpuCommon::IpuCreateStream(IPU_ModelInfo_S* pstModelInfo)
{
    MI_VPE_PortMode_t stVpeMode;
    MI_SYS_WindowRect_t stCropWin;
    Divp_Sys_BindInfo_T stBindInfo;
    MI_SYS_ChnPort_t stChnOutputPort;
    
    memset(&stVpeMode, 0, sizeof(MI_VPE_PortMode_t));
    ExecFunc(MI_VPE_GetPortMode(pstModelInfo->u32VpeChn, pstModelInfo->u32VpePort, &stVpeMode), MI_VPE_OK);

    stCropWin.u16X = 0;
    stCropWin.u16Y = 0;
    stCropWin.u16Width = stVpeMode.u16Width;
    stCropWin.u16Height = stVpeMode.u16Height;
    ExecFunc(Divp_CreatChannel(pstModelInfo->u32DivpChn, (MI_SYS_Rotate_e)0x0, &stCropWin),MI_VPE_OK);

    ExecFunc(Divp_SetOutputAttr(pstModelInfo->u32DivpChn, &pstModelInfo->stDivpPortAttr),MI_SUCCESS);
    ExecFunc(Divp_StartChn(pstModelInfo->u32DivpChn),MI_SUCCESS);

    memset(&stBindInfo, 0x00, sizeof(Divp_Sys_BindInfo_T));

    stBindInfo.stSrcChnPort.eModId    = E_MI_MODULE_ID_VPE;
    stBindInfo.stSrcChnPort.u32DevId  = 0;
    stBindInfo.stSrcChnPort.u32ChnId  = pstModelInfo->u32VpeChn;
    stBindInfo.stSrcChnPort.u32PortId = pstModelInfo->u32VpePort;

    stBindInfo.stDstChnPort.eModId    = E_MI_MODULE_ID_DIVP;
    stBindInfo.stDstChnPort.u32DevId  = 0;
    stBindInfo.stDstChnPort.u32ChnId  = pstModelInfo->u32DivpChn;
    stBindInfo.stDstChnPort.u32PortId = 0;

    ExecFunc(MI_SYS_SetChnOutputPortDepth(&stBindInfo.stDstChnPort, 3, 3), MI_SUCCESS);

    stBindInfo.u32SrcFrmrate = 30; 
    stBindInfo.u32DstFrmrate = 30; 
    stBindInfo.eBindType = E_MI_SYS_BIND_TYPE_FRAME_BASE;

    ExecFunc(Divp_Sys_Bind(&stBindInfo),MI_SUCCESS);

    stChnOutputPort.eModId       = E_MI_MODULE_ID_DIVP;
    stChnOutputPort.u32DevId     = 0;
    stChnOutputPort.u32ChnId     = pstModelInfo->u32DivpChn;
    stChnOutputPort.u32PortId     = 0;
    if(MI_SYS_GetFd(&stChnOutputPort, &pstModelInfo->s32DivpFd) < 0)
    {
        MIXER_ERR("divp ch: %d, get fd. err\n", stChnOutputPort.u32ChnId);
        return E_MI_ERR_FAILED;
    }

    MIXER_DBG("s32DivpFd:%d\n", pstModelInfo->s32DivpFd);

    return MI_SUCCESS;
}

MI_S32 CIpuCommon::IpuDestroyStream(IPU_ModelInfo_S* pstModelInfo)
{
    Divp_Sys_BindInfo_T stBindInfo;

    if (pstModelInfo->s32DivpFd > 0)
    {
        MI_SYS_CloseFd(pstModelInfo->s32DivpFd);
        pstModelInfo->s32DivpFd = -1;
    }

    memset(&stBindInfo, 0x00, sizeof(Divp_Sys_BindInfo_T));

    stBindInfo.stSrcChnPort.eModId    = E_MI_MODULE_ID_VPE;
    stBindInfo.stSrcChnPort.u32DevId  = 0;
    stBindInfo.stSrcChnPort.u32ChnId  = pstModelInfo->u32VpeChn;
    stBindInfo.stSrcChnPort.u32PortId = pstModelInfo->u32VpePort;

    stBindInfo.stDstChnPort.eModId    = E_MI_MODULE_ID_DIVP;
    stBindInfo.stDstChnPort.u32DevId  = 0;
    stBindInfo.stDstChnPort.u32ChnId  = pstModelInfo->u32DivpChn;
    stBindInfo.stDstChnPort.u32PortId = 0;

    stBindInfo.u32SrcFrmrate = 30; 
    stBindInfo.u32DstFrmrate = 15; 
    stBindInfo.eBindType = E_MI_SYS_BIND_TYPE_FRAME_BASE;

    ExecFunc(Divp_Sys_UnBind(&stBindInfo), MI_SUCCESS);
    ExecFunc(Divp_StopChn(pstModelInfo->u32DivpChn), MI_SUCCESS);
    ExecFunc(Divp_DestroyChn(pstModelInfo->u32DivpChn), MI_SUCCESS);

    return MI_SUCCESS;
}

MI_S32 CIpuCommon::IpuGetLabels(char *pLabelFile, map<int,string>& LabelName)
{
    MI_S32 strLen = 0;
    MI_S32 number = 0;
    ifstream LabelFile;
    char label[LABEL_CLASS_COUNT][LABEL_NAME_MAX_SIZE];
    
    LabelFile.open(pLabelFile);
    while(1)
    {
        LabelFile.getline(&label[number][0], LABEL_NAME_MAX_SIZE);
        strLen = strlen(&label[number][0]);

        if(label[number][strLen-1] == '\r' 
         ||label[number][strLen-1] == '\n')
        {
            label[number][strLen-1] = '\0';
        }

        if(strLen > 0)
        {
            LabelName[number] = string(&label[number][0]);
            number++;
        }
        
        if(number >= LABEL_CLASS_COUNT || LabelFile.eof())
        {
            cout<<"the labels have line:"<<number<<" ,it supass the available label array"<<std::endl;
			break;
		}
    }
    LabelFile.close();
    
    return number;
}

MI_S32 CIpuCommon::IpuScaleToModelSize(IPU_ModelInfo_S* pstModelInfo, MI_SYS_BufInfo_t* pstBufInfo, MI_IPU_TensorVector_t* pstInputTensorVector)
{
    int PixerBytes = 1;
    MI_U16 u16Height = 0;
    unsigned char *pSrcImage = NULL;
    unsigned char *pDstImage = NULL;
    cv::Mat resizeMat;
    
    if (!pstModelInfo || !pstInputTensorVector
       || !pstBufInfo || pstBufInfo->eBufType != E_MI_SYS_BUFDATA_FRAME)
    {
        return E_MI_ERR_FAILED;
    }

    if ((pstBufInfo->stFrameData.ePixelFormat == E_MI_SYS_PIXEL_FRAME_ABGR8888) 
      ||(pstBufInfo->stFrameData.ePixelFormat == E_MI_SYS_PIXEL_FRAME_ARGB8888)
      ||(pstBufInfo->stFrameData.ePixelFormat == E_MI_SYS_PIXEL_FRAME_YUV_SEMIPLANAR_420))
    {
        pSrcImage = (unsigned char *)pstBufInfo->stFrameData.pVirAddr[0];
    }

    // model channel
	if (!pSrcImage || pstModelInfo->stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[3] != 3)
        return E_MI_ERR_FAILED;

    if ((pstBufInfo->stFrameData.ePixelFormat == E_MI_SYS_PIXEL_FRAME_ABGR8888) 
      ||(pstBufInfo->stFrameData.ePixelFormat == E_MI_SYS_PIXEL_FRAME_ARGB8888))
    {
        PixerBytes = 4;
        u16Height = pstBufInfo->stFrameData.u16Height;
    } 
    else if (pstBufInfo->stFrameData.ePixelFormat == E_MI_SYS_PIXEL_FRAME_YUV_SEMIPLANAR_420)
    {
        u16Height = pstBufInfo->stFrameData.u16Height*3/2;
    }

    cv::Mat srcMat(u16Height, pstBufInfo->stFrameData.u16Width ,(PixerBytes==4?CV_8UC4:CV_8UC1), pSrcImage);
    resizeMat = srcMat;
    
    if (((MI_U32)srcMat.size().width != pstModelInfo->stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[2]) &&
        ((MI_U32)srcMat.size().height != pstModelInfo->stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[1]))
    {
        cv::resize(srcMat, resizeMat, cv::Size(pstModelInfo->stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[2],
                                               pstModelInfo->stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[1]));
    }

	pSrcImage = (unsigned char *)resizeMat.data;
    pDstImage = (unsigned char *)pstInputTensorVector->astArrayTensors[0].ptTensorData[0];
    memcpy(pDstImage, pSrcImage, resizeMat.size().width * resizeMat.size().height * PixerBytes);

    return MI_SUCCESS;
}

MI_S32 CIpuCommon::IpuRunAndGetOutputTensor(IPU_ModelInfo_S* pstModelInfo, MI_SYS_BufInfo_t* pstBufInfo, MI_IPU_TensorVector_t* pstTensorVector)
{
    MI_S32 s32Ret = MI_SUCCESS;
    MI_IPU_TensorVector_t stInputTensorVector;
    MI_IPU_TensorVector_t stOutputTensorVector;
    
    if (pstBufInfo == NULL)
    {
        return E_MI_ERR_FAILED;
    }

    if ((pstBufInfo->stFrameData.ePixelFormat != E_MI_SYS_PIXEL_FRAME_ABGR8888) 
      &&(pstBufInfo->stFrameData.ePixelFormat != E_MI_SYS_PIXEL_FRAME_ARGB8888)
      &&(pstBufInfo->stFrameData.ePixelFormat != E_MI_SYS_PIXEL_FRAME_YUV_SEMIPLANAR_420))
    {
        return E_MI_ERR_FAILED;
    }

    memset(&stInputTensorVector, 0, sizeof(MI_IPU_TensorVector_t));
	memset(&stOutputTensorVector, 0, sizeof(MI_IPU_TensorVector_t));
    
    s32Ret = MI_IPU_GetInputTensors(pstModelInfo->u32IpuChn, &stInputTensorVector);
    if (s32Ret != MI_SUCCESS)
    {
        MIXER_ERR("MI_IPU_GetInputTensors error, ret[0x%x]\n", s32Ret);
        return s32Ret;
    }
    IpuScaleToModelSize(pstModelInfo, pstBufInfo, &stInputTensorVector);
    
    s32Ret = MI_IPU_GetOutputTensors(pstModelInfo->u32IpuChn, &stOutputTensorVector);
    if (s32Ret != MI_SUCCESS)
    {
        MIXER_ERR("MI_IPU_GetOutputTensors error, ret[0x%x]\n", s32Ret);
        MI_IPU_PutInputTensors(pstModelInfo->u32IpuChn, &stInputTensorVector);
        return s32Ret;
    }
    
    s32Ret = MI_IPU_Invoke(pstModelInfo->u32IpuChn, &stInputTensorVector, &stOutputTensorVector);
    if(s32Ret != MI_SUCCESS)
    {
        MIXER_ERR("MI_IPU_Invoke error, ret[0x%x]\n", s32Ret);
        MI_IPU_PutOutputTensors(pstModelInfo->u32IpuChn,&stOutputTensorVector);
        MI_IPU_PutInputTensors(pstModelInfo->u32IpuChn, &stInputTensorVector);
        return s32Ret;
    }

    *pstTensorVector = stOutputTensorVector;
    
    MI_IPU_PutInputTensors(pstModelInfo->u32IpuChn, &stInputTensorVector);
    //MI_IPU_PutOutputTensors(m_stModelInfo.u32IpuChn,&stOutputTensorVector);

    return MI_SUCCESS;
}

