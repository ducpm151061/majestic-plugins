#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <fstream>
#include <iostream>
#include <map>

#include "osd.h"
#include "iout.h"
#include "mi_ipu.h"
#include "mi_vpe.h"
#include "dla_base.h"
#include "dla_fdfr.h"

using namespace std;
//#define SAVE_CROP_IMAGE

CIpuFdfr::CIpuFdfr(IPU_DlaInfo_S &stDlaInfo) 
    :CIpuCommon(stDlaInfo)
{
    memset(&m_stModelInfo, 0, sizeof(IPU_ModelInfo_S));
    memset(&m_stFrModelInfo, 0, sizeof(IPU_ModelInfo_S));

    m_stModelInfo.u32IpuChn  = 0;
    m_stModelInfo.u32DivpChn = 0;
    m_stModelInfo.u32VpeChn  = 0;
    m_stModelInfo.u32VpePort = 2;
    m_stModelInfo.u32InBufDepth  = 1;
    m_stModelInfo.u32OutBufDepth = 1;
    m_stModelInfo.s32DivpFd = -1;   
    memcpy(&m_stModelInfo.stDivpPortAttr, &stDlaInfo.stDivpPortAttr, sizeof(MI_DIVP_OutputPortAttr_t));
    
    m_stFrModelInfo.u32IpuChn  = 1;
    m_stFrModelInfo.u32DivpChn = 1;
    m_stFrModelInfo.u32VpeChn  = 0;
    m_stFrModelInfo.u32VpePort = 2;
    m_stFrModelInfo.u32InBufDepth  = 1;
    m_stFrModelInfo.u32OutBufDepth = 1;
    m_stFrModelInfo.s32DivpFd = -1;   
    memcpy(&m_stFrModelInfo.stDivpPortAttr, &stDlaInfo.stDivpPortAttr, sizeof(MI_DIVP_OutputPortAttr_t));
    //m_stFrModelInfo.stDivpPortAttr.u32Width = 1920;
    //m_stFrModelInfo.stDivpPortAttr.u32Height = 1080;
    IpuInit();
}

CIpuFdfr::~CIpuFdfr()
{
    IpuDeInit();
}

MI_S32 CIpuFdfr::IpuInit()
{
    MI_U32 u32BufSize = 0;

    IpuGetVpeMode();
	if(strncmp(m_stDlaInfo.stIpuInitInfo.szModelFile,"enc",3)==0)
        u32BufSize = 0x200000;
    else
        ExecFunc(IpuGetModelBufSize(u32BufSize), MI_SUCCESS);
    ExecFunc(IpuCreateDevice(u32BufSize), MI_SUCCESS);

    ExecFunc(IpuCreateChannel(m_stDlaInfo.stIpuInitInfo.szModelFile, &m_stModelInfo), MI_SUCCESS);
    ExecFunc(IpuCreateStream(&m_stModelInfo), MI_SUCCESS);

    ExecFunc(IpuCreateChannel(m_stDlaInfo.stIpuInitInfo.u.ExtendInfo2.szModelFile1, &m_stFrModelInfo), MI_SUCCESS);
    ExecFunc(IpuCreateStream(&m_stFrModelInfo), MI_SUCCESS);

    IpuLoadFaceData();
    return MI_SUCCESS;
}

MI_S32 CIpuFdfr::IpuDeInit()
{
    ExecFunc(IpuDestroyStream(&m_stModelInfo), MI_SUCCESS);
    ExecFunc(IpuDestroyChannel(&m_stModelInfo), MI_SUCCESS);

    ExecFunc(IpuDestroyStream(&m_stFrModelInfo), MI_SUCCESS);
    ExecFunc(IpuDestroyChannel(&m_stFrModelInfo), MI_SUCCESS);
    
    ExecFunc(IpuDestroyDevice(), MI_SUCCESS);

    IpuCleanFaceData();
    return MI_SUCCESS;
}

MI_S32 CIpuFdfr::IpuGetVpeMode(void)
{
    MI_S32 s32Chn = 0;
    MI_S32 s32Port = 0;
    ExecFunc(MI_VPE_GetPortMode(s32Chn, s32Port, &m_stVpePortMode), MI_SUCCESS);
    
    return MI_SUCCESS;
}

MI_S32 CIpuFdfr::IpuGetModelBufSize(MI_U32& u32BufSize)
{
    MI_IPU_OfflineModelStaticInfo_t stOfflineModelInfo;

    ExecFunc(MI_IPU_GetOfflineModeStaticInfo(NULL, m_stDlaInfo.stIpuInitInfo.szModelFile, &stOfflineModelInfo), MI_SUCCESS);
    u32BufSize = stOfflineModelInfo.u32VariableBufferSize;

    ExecFunc(MI_IPU_GetOfflineModeStaticInfo(NULL, m_stDlaInfo.stIpuInitInfo.u.ExtendInfo2.szModelFile1, &stOfflineModelInfo), MI_SUCCESS);
    u32BufSize = MAX(u32BufSize, stOfflineModelInfo.u32VariableBufferSize);
        
    return MI_SUCCESS;
}

void CIpuFdfr::IpuLoadFaceData()
{    
    MI_S32 s32FeatNum = 0;
    MI_S32 s32PersonNum = 0;

    m_FaceData.LoadFromFileBinay(m_stDlaInfo.stIpuInitInfo.u.ExtendInfo2.szFaceDBFile, m_stDlaInfo.stIpuInitInfo.u.ExtendInfo2.szNameListFile);

    s32PersonNum = m_FaceData.persons.size();
    MIXER_DBG("s32PersonNum = %d \n", s32PersonNum);
    
    for (int i = 0; i < s32PersonNum; i++)
    {
        MIXER_DBG("person[%d]:%s \n", i, m_FaceData.persons[i].name.c_str());
        
        s32FeatNum = m_FaceData.persons[i].features.size();
        for (int j = 0; j < s32FeatNum; j++)
        {
            MIXER_DBG("Feat Len:%d [%f %f] \n", 
                m_FaceData.persons[i].features[j].length,
                m_FaceData.persons[i].features[j].pData[0],
                m_FaceData.persons[i].features[j].pData[1]);
        }
    }
}

void CIpuFdfr::IpuCleanFaceData()
{
    m_FaceData.Clear();
}

void CIpuFdfr::IpuRunProcess()
{   
    #define TRY_TIMES 5
    MI_S32 s32Ret = MI_SUCCESS;
    MI_S32 s32FdBufHandle;
    MI_S32 s32FrBufHandle;
    MI_SYS_BufInfo_t stFdBufInfo;
    MI_SYS_BufInfo_t stFrBufInfo;
    MI_SYS_ChnPort_t stChnOutputPort;
    MI_BOOL bFdDone = FALSE;
    MI_BOOL bFrDone = FALSE;
    MI_IPU_TensorVector_t stFdTensorVector;
    
    stChnOutputPort.u32DevId  = 0;
    stChnOutputPort.u32PortId = 0;
    stChnOutputPort.eModId    = E_MI_MODULE_ID_DIVP;
    stChnOutputPort.u32ChnId  = m_stModelInfo.u32DivpChn;   
                
    for(int i = 0; i < TRY_TIMES; i++)
    {
        memset(&stFdBufInfo, 0, sizeof(MI_SYS_BufInfo_t));
        s32Ret = MI_SYS_ChnOutputPortGetBuf(&stChnOutputPort, &stFdBufInfo, &s32FdBufHandle);
        if (s32Ret == MI_SUCCESS)
        {
            bFdDone = TRUE;
            break;
        }
        usleep(10000);
    } 

    if(bFdDone == FALSE)
        return;
    
    stChnOutputPort.u32ChnId = m_stFrModelInfo.u32DivpChn;
    
    for(int i = 0; i < TRY_TIMES; i++)
    {
        memset(&stFrBufInfo, 0, sizeof(MI_SYS_BufInfo_t));
        s32Ret = MI_SYS_ChnOutputPortGetBuf(&stChnOutputPort, &stFrBufInfo, &s32FrBufHandle);
        if (s32Ret == MI_SUCCESS)
        {
            bFrDone = TRUE;
            break;
        }
        usleep(10000);
    } 
    
    if (bFdDone && bFrDone)
    {    
        if(IpuDoFd(&stFdBufInfo, &stFdTensorVector) == MI_SUCCESS)
        {
            IpuDoFr(&stFrBufInfo, &stFdTensorVector);
            IpuPrintResult();
            MI_IPU_PutOutputTensors(m_stModelInfo.u32IpuChn, &stFdTensorVector);
        }
    }

    if(bFdDone)
        MI_SYS_ChnOutputPortPutBuf(s32FdBufHandle);
    if (bFrDone)
        MI_SYS_ChnOutputPortPutBuf(s32FrBufHandle);
}

void CIpuFdfr::IpuRunCommand(MI_U32 u32CmdCode)
{   
    MI_S32 ret;
    MI_U32 trackid;
    char  username[256];  
    #define ADD_DEL_FACE_FEAT 12

    if(u32CmdCode != ADD_DEL_FACE_FEAT)
        return;

    while(1)
    {
        printf("add or delete face feat: intput:a (add), d (del), q (exit)\n");
        // when mixer process is backgroud running,getchar() will not block and return -1,so need sleep
        sleep(1);
        
        MI_S8 ch = getchar();
        if('q' == ch)
        {
            break;//exit
        }
        else if('a' == ch)
        {
            printf("please input trackid:");
            fflush(stdin);
            ret = scanf("%d",&trackid);
            getchar();
            while((ret != 1) || trackid<0)
            {
                printf("input  parameter error.\n");
                printf("please input trackid:");
                fflush(stdin);
                ret = scanf("%d",&trackid);
                getchar();
            }
            printf("please input username:");
            fflush(stdin);
            memset(username, 0, 256);
            ret = scanf("%s", username);
            getchar();
            while((ret != 1) || strlen(username)<=0||strlen(username)>=100)
            {
                printf("invalid usernamer.\n");
                printf("please input username:");
                fflush(stdin);
                ret = scanf("%s",username);
                getchar();
            }

            m_AddPersion.bDone = TRUE;
            m_AddPersion.s32TrackId = trackid;
            memcpy(m_AddPersion.NewAddName, username, strlen(username));
            printf("input name :%s trackid:%d\n",username,trackid);
        }
        else if('d' == ch)
        {
            printf("please input username to del:");
            fflush(stdin);
            memset(username, 0, 256);
            ret = scanf("%s", username);
            getchar();
            while((ret != 1) || strlen(username)<=0||strlen(username)>=100)
            {
                printf("invalid usernamer.\n");
                printf("please input username to del:");
                fflush(stdin);
                ret = scanf("%s",username);
                getchar();
            }
            printf("delete name :%s\n",username);
            m_FaceData.DelPerson((const char*)username);
            m_FaceData.SaveToFileBinary(m_stDlaInfo.stIpuInitInfo.u.ExtendInfo2.szFaceDBFile, m_stDlaInfo.stIpuInitInfo.u.ExtendInfo2.szNameListFile);
            IpuRemoveOldName(username);
        }
    }

    memset(&m_AddPersion, 0, sizeof(stAddPersion_t));
}

MI_S32 CIpuFdfr::IpuDoFd(MI_SYS_BufInfo_t* pstBufInfo, MI_IPU_TensorVector_t* pstTensorVector)
{
    MI_S32 s32Ret = MI_SUCCESS;

    if (!pstBufInfo|| !pstTensorVector)
    {
        return E_MI_ERR_FAILED;
    }

    s32Ret = IpuRunAndGetOutputTensor(&m_stModelInfo, pstBufInfo, pstTensorVector);
    
    return s32Ret;
}

MI_S32 CIpuFdfr::IpuDoFr(MI_SYS_BufInfo_t* pstBufInfo, MI_IPU_TensorVector_t* pstFdTensorVector)
{
    MI_S32 s32Ret = MI_SUCCESS;
    std::vector<DetBBox> vDetectInfos;
    MI_IPU_TensorVector_t stInputTensorVector;
    MI_IPU_TensorVector_t stOutputTensorVector;

    if (!pstBufInfo|| !pstFdTensorVector)
    {
        return E_MI_ERR_FAILED;
    }

    memset(&stInputTensorVector, 0, sizeof(MI_IPU_TensorVector_t));
	memset(&stOutputTensorVector, 0, sizeof(MI_IPU_TensorVector_t));
    
    s32Ret = MI_IPU_GetInputTensors(m_stFrModelInfo.u32IpuChn, &stInputTensorVector);
    if (s32Ret != MI_SUCCESS)
    {
        MIXER_ERR("MI_IPU_GetInputTensors error, ret[0x%x]\n", s32Ret);
        return s32Ret;
    }
    
    s32Ret = MI_IPU_GetOutputTensors(m_stFrModelInfo.u32IpuChn, &stOutputTensorVector);
    if (s32Ret != MI_SUCCESS)
    {
        MIXER_ERR("MI_IPU_GetOutputTensors error, ret[0x%x]\n", s32Ret);
        MI_IPU_PutInputTensors(m_stFrModelInfo.u32IpuChn, &stInputTensorVector);
        return s32Ret;
    }

    IpuGetDetectInfo(pstFdTensorVector, vDetectInfos);
    if(IpuDoTrack(vDetectInfos))
        s32Ret = IpuDoRecognition(pstBufInfo, &stInputTensorVector, &stOutputTensorVector);

    MI_IPU_PutInputTensors(m_stFrModelInfo.u32IpuChn, &stInputTensorVector);
    MI_IPU_PutOutputTensors(m_stFrModelInfo.u32IpuChn, &stOutputTensorVector);
    
    return s32Ret;
}

void CIpuFdfr::IpuGetDetectInfo(MI_IPU_TensorVector_t* pstOutputTensorVector, std::vector<DetBBox>& vDetectInfos)
{
    int DetectCount = 0;
    
    float *pfBBox    = (float*)pstOutputTensorVector->astArrayTensors[0].ptTensorData[0];
    //float *pfClass   = (float*)pstOutputTensorVector->astArrayTensors[1].ptTensorData[0];
    float *pfScore   = (float*)pstOutputTensorVector->astArrayTensors[2].ptTensorData[0];
    float *pfDetect  = (float*)pstOutputTensorVector->astArrayTensors[3].ptTensorData[0];
    float *pIndex    = (float*)pstOutputTensorVector->astArrayTensors[4].ptTensorData[0];
    float *pfFeature = (float*)pstOutputTensorVector->astArrayTensors[5].ptTensorData[0];

    vDetectInfos.clear();
    DetectCount = round(*pfDetect);    
    
    for(int i = 0; i < DetectCount; i++)
    {
        int index = 0;
        DetBBox stDetectBox;

        memset(&stDetectBox,0,sizeof(DetBBox));      
        //score
        stDetectBox.score = *(pfScore+i);
        if(stDetectBox.score < DETECT_THRESHOLD)
            continue;

        index = (int)(*(pIndex + i));
        stDetectBox.y1 = *(pfBBox + i * ALIGN_UP(4, INNER_MOST_ALIGNMENT)+0) * m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[1];
        stDetectBox.x1 = *(pfBBox + i * ALIGN_UP(4, INNER_MOST_ALIGNMENT)+1) * m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[2];
        stDetectBox.y2 = *(pfBBox + i * ALIGN_UP(4, INNER_MOST_ALIGNMENT)+2) * m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[1];
        stDetectBox.x2 = *(pfBBox + i * ALIGN_UP(4, INNER_MOST_ALIGNMENT)+3) * m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[2];
        stDetectBox.lm1_x = *(pfFeature + index * ALIGN_UP(10,INNER_MOST_ALIGNMENT) + 0) * m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[2];
        stDetectBox.lm1_y = *(pfFeature + index * ALIGN_UP(10,INNER_MOST_ALIGNMENT) + 1) * m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[1];
        stDetectBox.lm2_x = *(pfFeature + index * ALIGN_UP(10,INNER_MOST_ALIGNMENT) + 2) * m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[2];
        stDetectBox.lm2_y = *(pfFeature + index * ALIGN_UP(10,INNER_MOST_ALIGNMENT) + 3) * m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[1];
        stDetectBox.lm3_x = *(pfFeature + index * ALIGN_UP(10,INNER_MOST_ALIGNMENT) + 4) * m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[2];
        stDetectBox.lm3_y = *(pfFeature + index * ALIGN_UP(10,INNER_MOST_ALIGNMENT) + 5) * m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[1];
        stDetectBox.lm4_x = *(pfFeature + index * ALIGN_UP(10,INNER_MOST_ALIGNMENT) + 6) * m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[2];
        stDetectBox.lm4_y = *(pfFeature + index * ALIGN_UP(10,INNER_MOST_ALIGNMENT) + 7) * m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[1];
        stDetectBox.lm5_x = *(pfFeature + index * ALIGN_UP(10,INNER_MOST_ALIGNMENT) + 8) * m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[2];
        stDetectBox.lm5_y = *(pfFeature + index * ALIGN_UP(10,INNER_MOST_ALIGNMENT) + 9) * m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[1];
       
        vDetectInfos.push_back(stDetectBox);
    }
}

MI_U32 CIpuFdfr::IpuDoTrack(std::vector<DetBBox>& vDetectInfos)
{
    std::vector <TrackBBox> detFrameData;
    std::vector <std::vector<TrackBBox>> detFrameDatas;
    
    for (size_t i = 0; i < vDetectInfos.size(); i++)
    {
        DetBBox  stDetBbox;
        TrackBBox stTrkBbox;

        stDetBbox = vDetectInfos[i];       
        if(stDetBbox.score < DETECT_THRESHOLD
          || stDetBbox.x1 < 0 
          || stDetBbox.y1 < 0 
          || stDetBbox.x2 > (float)m_stModelInfo.stDivpPortAttr.u32Width 
          || stDetBbox.y2 > (float)m_stModelInfo.stDivpPortAttr.u32Height)
        {
            continue;
        }

        memset(&stTrkBbox,0x00,sizeof(TrackBBox));
        stTrkBbox.x = stDetBbox.x1;
        stTrkBbox.y = stDetBbox.y1;
        stTrkBbox.w = stDetBbox.x2 - stDetBbox.x1;
        stTrkBbox.h = stDetBbox.y2 - stDetBbox.y1;
        stTrkBbox.score = stDetBbox.score;
		stTrkBbox.lm1_x = stDetBbox.lm1_x;
	    stTrkBbox.lm1_y = stDetBbox.lm1_y;
		stTrkBbox.lm2_x = stDetBbox.lm2_x;
		stTrkBbox.lm2_y = stDetBbox.lm2_y;
		stTrkBbox.lm3_x = stDetBbox.lm3_x;
		stTrkBbox.lm3_y = stDetBbox.lm3_y;
		stTrkBbox.lm4_x = stDetBbox.lm4_x;
		stTrkBbox.lm4_y = stDetBbox.lm4_y;
		stTrkBbox.lm5_x = stDetBbox.lm5_x;
		stTrkBbox.lm5_y = stDetBbox.lm5_y;
		stTrkBbox.classID = 0;

        detFrameData.push_back(stTrkBbox);
        
        MIXER_DBG("score=%f [%.2f %.2f %.2f %.2f] \n", stDetBbox.score, stTrkBbox.x, stTrkBbox.y, stTrkBbox.w, stTrkBbox.h);
    }
     
    if(detFrameData.size())
    {
        detFrameDatas.push_back(detFrameData);
        m_DetectTrackBBoxs = m_DetectBBoxTracker.track_iou(detFrameDatas);
    }

    return detFrameDatas.size();
}

stCountName_t CIpuFdfr::IpuSearchNameByID(std::map<int, stCountName_t> &MapIdName, int id)
{
    std::map<int, stCountName_t>::iterator iter;
    iter = MapIdName.find(id);
    if(iter == MapIdName.end())
    {
       stCountName_t stCountName;
       stCountName.Count = 0;
       stCountName.Name = "";
       return stCountName;
    }
    else
    {
        return iter->second;
    }
}

void CIpuFdfr::IpuRemoveOldName(char* DelName)
{
    std::map<int, stCountName_t>::iterator iter_end = m_MapIdName.begin();
    for (; iter_end != m_MapIdName.end(); ) 
    {
        if(DelName != NULL&&strcmp(iter_end->second.Name.c_str(), DelName)==0)
        {
            m_MapIdName.erase(iter_end++);
            continue;
        }
        
        iter_end->second.Count++;
        if (iter_end->second.Count > 10 && strcmp(iter_end->second.Name.c_str(), "unknown")==0)
        {
            m_MapIdName.erase(iter_end++);
        }
        else 
        {
            if(iter_end->second.Count > 1000)
                m_MapIdName.erase(iter_end++);
            else
                iter_end++;
        }
    }
}

MI_S32 CIpuFdfr::IpuDoRecognition(MI_SYS_BufInfo_t* pstBufInfo,
                                MI_IPU_TensorVector_t* pstInputVector,
                                MI_IPU_TensorVector_t* pstOutputVector)
{
    #define CROP_SIZE 112      //our model is h*w(112x112)
    unsigned char *pSrcImage = NULL;
    unsigned char *pDstImage = NULL;
    
    if (!pstInputVector || !pstOutputVector
       || !pstBufInfo || pstBufInfo->eBufType != E_MI_SYS_BUFDATA_FRAME)
    {
        return E_MI_ERR_FAILED;
    }

    if ((pstBufInfo->stFrameData.ePixelFormat != E_MI_SYS_PIXEL_FRAME_ABGR8888) 
      &&(pstBufInfo->stFrameData.ePixelFormat != E_MI_SYS_PIXEL_FRAME_ARGB8888)
      &&(pstBufInfo->stFrameData.ePixelFormat != E_MI_SYS_PIXEL_FRAME_YUV_SEMIPLANAR_420))
    {
        return E_MI_ERR_FAILED;
    }
      
    pSrcImage = (unsigned char *)pstBufInfo->stFrameData.pVirAddr[0];
    pDstImage = (unsigned char* )pstInputVector->astArrayTensors[0].ptTensorData[0];
    
    for(MI_U8 i = 0; i < m_DetectTrackBBoxs.size(); i++)
	{
        bool bForceFr = false;
        stCountName_t stCountName;
        int id = m_DetectTrackBBoxs[i].id;
	    int index = m_DetectTrackBBoxs[i].boxes.size()-1;
		TrackBBox& trackBox = m_DetectTrackBBoxs[i].boxes[index];

        if(m_AddPersion.bDone&&m_AddPersion.s32TrackId==id)
            bForceFr = true;
        
        stCountName = IpuSearchNameByID(m_MapIdName, id);
        if(!stCountName.Name.empty()&&!bForceFr)
        {
            IpuSaveFaceData(trackBox, stCountName.Name, id);
            continue;
        }

        if (trackBox.h < 120 && trackBox.w < 120)
        {
            stCountName.Count = 0;
            IpuSaveFaceData(trackBox, stCountName.Name, id);
            continue;
        }

        float Face5Point[10] =
        {
            trackBox.lm1_x, trackBox.lm1_y,
            trackBox.lm2_x, trackBox.lm2_y,
            trackBox.lm3_x, trackBox.lm3_y,
            trackBox.lm4_x, trackBox.lm4_y,
            trackBox.lm5_x, trackBox.lm5_y
        };
                    
        if ((pstBufInfo->stFrameData.ePixelFormat == E_MI_SYS_PIXEL_FRAME_ABGR8888) 
          ||(pstBufInfo->stFrameData.ePixelFormat == E_MI_SYS_PIXEL_FRAME_ARGB8888))
        {
            cv::Mat srcMat(pstBufInfo->stFrameData.u16Height, pstBufInfo->stFrameData.u16Width , CV_8UC4, pSrcImage);
            cv::Mat cropMat(CROP_SIZE, CROP_SIZE, CV_8UC4, pDstImage);
            FaceRecognizeUtils::CropImage_112x112<float>(srcMat, Face5Point, cropMat);

            #ifdef SAVE_CROP_IMAGE
                static int SaveCount = 0;
                char cSavePath[32];
                sprintf(cSavePath, "crop/crop_%d.jpg", SaveCount++);

                cv::imwrite(cSavePath, cropMat);
                if (SaveCount == 50)
                   SaveCount = 0;
            #endif
        } 
        else if (pstBufInfo->stFrameData.ePixelFormat == E_MI_SYS_PIXEL_FRAME_YUV_SEMIPLANAR_420)
        {
            MI_U16 u16Height = pstBufInfo->stFrameData.u16Height*3/2;
            cv::Mat srcMat(u16Height, pstBufInfo->stFrameData.u16Width , CV_8UC1, pSrcImage);
            u16Height = CROP_SIZE*3/2;
            cv::Mat cropMat(u16Height, CROP_SIZE, CV_8UC1, pDstImage,ALIGN_UP(CROP_SIZE,16));
            FaceRecognizeUtils::CropImage_112x112_YUV420_NV12(srcMat, Face5Point, cropMat);
        }

        if(MI_IPU_Invoke(m_stFrModelInfo.u32IpuChn, pstInputVector, pstOutputVector) != MI_SUCCESS)
        {
            continue;
        }

        stCountName.Name = m_FaceRecognizer.find_name(m_FaceData,(float*)pstOutputVector->astArrayTensors[0].ptTensorData[0]);

        stCountName.Count = 0;
        if(stCountName.Name.empty())
            stCountName.Name = "unknown";
        
        m_MapIdName.insert({id, stCountName});

        if(bForceFr&&(!strcmp(stCountName.Name.c_str(), "unknown")||strcmp(stCountName.Name.c_str(),m_AddPersion.NewAddName)==0))
        {
            m_FaceData.AddPersonFeature(m_AddPersion.NewAddName, (float*)pstOutputVector->astArrayTensors[0].ptTensorData[0]);
            m_FaceData.SaveToFileBinary(m_stDlaInfo.stIpuInitInfo.u.ExtendInfo2.szFaceDBFile, m_stDlaInfo.stIpuInitInfo.u.ExtendInfo2.szNameListFile);
            memset(&m_AddPersion, 0, sizeof(stAddPersion_t));
            bForceFr = false;
        }
        
        IpuSaveFaceData(trackBox, stCountName.Name, id);       
    }

    IpuRemoveOldName(NULL);
    
    return MI_SUCCESS;
}

void CIpuFdfr::IpuSaveFaceData(TrackBBox& trackBox, std::string strName,int Id)
{
    stFaceInfo_t stFaceInfo;

    memset(&stFaceInfo, 0, sizeof(stFaceInfo_t));

    stFaceInfo.faceH = trackBox.h;
    stFaceInfo.faceW = trackBox.w;
    stFaceInfo.xPos  = trackBox.x;
    stFaceInfo.yPos  = trackBox.y;
    
    if(strName.empty())
        strName = "unknown";
    
    snprintf(stFaceInfo.faceName, sizeof(stFaceInfo.faceName) - 1, "%sID:%d", strName.c_str(), Id);
    m_FaceInfo.push_back(stFaceInfo);
}

MI_S32 CIpuFdfr::IpuGetRectInfo(ST_DlaRectInfo_T* pstRectInfo)
{
    MI_S32 count = 0;

    for (size_t i = 0; i < m_FaceInfo.size(); i++)
	{
	  	memset(&pstRectInfo[count], 0, sizeof(ST_DlaRectInfo_T));
        
		//convert to 1920x1080
		pstRectInfo[count].rect.u32X = m_FaceInfo[i].xPos * m_stVpePortMode.u16Width/m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[2];
		pstRectInfo[count].rect.u32Y = m_FaceInfo[i].yPos * m_stVpePortMode.u16Height/m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[1];
		pstRectInfo[count].rect.u16PicW = m_FaceInfo[i].faceW * m_stVpePortMode.u16Width/m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[2];
		pstRectInfo[count].rect.u16PicH = m_FaceInfo[i].faceH * m_stVpePortMode.u16Height/m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[1];    
        memcpy(pstRectInfo[count].szObjName, m_FaceInfo[i].faceName, strlen(m_FaceInfo[i].faceName));
		MIXER_DBG("stRectInfo[count].rect.u32X =%d    stRectInfo[count].rect.u32Y=%d\n", pstRectInfo[count].rect.u32X, pstRectInfo[count].rect.u32Y);
		MIXER_DBG("stRectInfo[count].rect.u16PicW =%d	stRectInfo[count].rect.u16PicH=%d\n", pstRectInfo[count].rect.u16PicW, pstRectInfo[count].rect.u16PicH);

        if (++count >= MAX_DETECT_RECT_NUM)
		{
	        break;
		}
    }
    m_FaceInfo.clear();
    
    return count;
}

void CIpuFdfr::IpuPrintResult()
{
    MI_S32 count = 0;
    ST_DlaRectInfo_T stRectInfo[MAX_DETECT_RECT_NUM];

    count = IpuGetRectInfo(stRectInfo);
    if(count)
    {
        MI_S8 s8Ret = OsdAddDlaRectData(0, count, stRectInfo, TRUE, TRUE);
        if(s8Ret != MI_SUCCESS)
        {
            return;
        }
        
        pthread_mutex_lock(&g_stMutexOsdUptState);      
        pthread_cond_signal(&g_condOsdUpadteState);  
        pthread_mutex_unlock(&g_stMutexOsdUptState);  
    }
}

