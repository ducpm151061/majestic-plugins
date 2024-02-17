#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>
#include <fstream>
#include <iostream>
#include <map>

#include "mi_ipu.h"
#include "mi_vpe.h"
#include "dla_base.h"
#include "dla_detect.h"

using namespace std;

CIpuDetect::CIpuDetect(IPU_DlaInfo_S &stDlaInfo) 
    :CIpuCommon(stDlaInfo)
{
    memset(&m_stModelInfo, 0, sizeof(IPU_ModelInfo_S));

    m_stModelInfo.u32IpuChn  = 0;
    m_stModelInfo.u32DivpChn = 0;
    m_stModelInfo.u32VpeChn  = 0;
    m_stModelInfo.u32VpePort = 2;    
    m_stModelInfo.u32InBufDepth  = 2;
    m_stModelInfo.u32OutBufDepth = 2;
    m_stModelInfo.s32DivpFd = -1;
        
    memcpy(&m_stModelInfo.stDivpPortAttr, &stDlaInfo.stDivpPortAttr, sizeof(MI_DIVP_OutputPortAttr_t));
    IpuInit();
}

CIpuDetect::~CIpuDetect()
{
    IpuDeInit();
}

MI_S32 CIpuDetect::IpuInit()
{
    MI_U32 u32BufSize = 0;

    IpuGetVpeMode();
    if(strncmp(m_stDlaInfo.stIpuInitInfo.szModelFile,"enc",3)==0)
        u32BufSize = 0x100000;
    else
        ExecFunc(IpuGetModelBufSize(u32BufSize), MI_SUCCESS);
    ExecFunc(IpuCreateDevice(u32BufSize), MI_SUCCESS);
    ExecFunc(IpuCreateChannel(m_stDlaInfo.stIpuInitInfo.szModelFile, &m_stModelInfo), MI_SUCCESS);
    ExecFunc(IpuCreateStream(&m_stModelInfo), MI_SUCCESS);
    m_s32LabelCount = IpuGetLabels(m_stDlaInfo.stIpuInitInfo.u.ExtendInfo1.szLabelFile, m_szLabelName);

    return MI_SUCCESS;
}

MI_S32 CIpuDetect::IpuDeInit()
{
    ExecFunc(IpuDestroyStream(&m_stModelInfo), MI_SUCCESS);
    ExecFunc(IpuDestroyChannel(&m_stModelInfo), MI_SUCCESS);
    ExecFunc(IpuDestroyDevice(), MI_SUCCESS);

    return MI_SUCCESS;
}

MI_S32 CIpuDetect::IpuGetVpeMode(void)
{
    MI_S32 s32Chn = 0;
    MI_S32 s32Port = 0;
    ExecFunc(MI_VPE_GetPortMode(s32Chn, s32Port, &m_stVpePortMode), MI_SUCCESS);
    
    return MI_SUCCESS;
}

void CIpuDetect::IpuRunProcess()
{
    MI_S32 s32Ret = 0;  
    fd_set read_fds;
    struct timeval tv;
    MI_SYS_BufInfo_t stBufInfo;
    MI_SYS_ChnPort_t stChnOutputPort;
    MI_SYS_BUF_HANDLE stBufHandle;       
    
    stChnOutputPort.eModId    = E_MI_MODULE_ID_DIVP;
    stChnOutputPort.u32DevId  = 0;
    stChnOutputPort.u32ChnId  = m_stModelInfo.u32DivpChn;
    stChnOutputPort.u32PortId = 0;

    FD_ZERO(&read_fds);
    FD_SET(m_stModelInfo.s32DivpFd, &read_fds);

    tv.tv_sec = 0;
    tv.tv_usec = 100 * 1000;
    s32Ret = select(m_stModelInfo.s32DivpFd+1, &read_fds, NULL, NULL, &tv);
    if (s32Ret <= 0) 
    {
        return;
    }

    if(FD_ISSET(m_stModelInfo.s32DivpFd, &read_fds))
    {
        memset(&stBufInfo, 0x0, sizeof(MI_SYS_BufInfo_t));
        if (MI_SUCCESS == MI_SYS_ChnOutputPortGetBuf(&stChnOutputPort, &stBufInfo, &stBufHandle))
        {
            IpuDoDetect(&stBufInfo);
            if (MI_SUCCESS != MI_SYS_ChnOutputPortPutBuf(stBufHandle))
            {
                MIXER_ERR("MI_SYS_ChnOutputPortPutBuf error\n");
            }
        }
        else
        {
            usleep(1000); 
        }
    }
}

void CIpuDetect::IpuGetDetectInfo(MI_IPU_TensorVector_t* pstOutputTensorVector, std::vector<DetectInfo_S>& vDetectInfos)
{
    int DetectCount = 0;
    
    float *pfBBox  = (float *)pstOutputTensorVector->astArrayTensors[0].ptTensorData[0];
    float *pfClass = (float *)pstOutputTensorVector->astArrayTensors[1].ptTensorData[0];
    float *pfScore = (float *)pstOutputTensorVector->astArrayTensors[2].ptTensorData[0];
    float *pfDetect = (float *)pstOutputTensorVector->astArrayTensors[3].ptTensorData[0];

    vDetectInfos.clear();
    DetectCount = round(*pfDetect);  
    
    for(int i = 0; i < DetectCount; i++)
    {
        DetectInfo_S stDetectIfno;

        memset(&stDetectIfno,0,sizeof(DetectInfo_S));        
        //score
        stDetectIfno.score = *(pfScore+i);
        if(stDetectIfno.score < DETECT_THRESHOLD)
            continue;

        //box class
        stDetectIfno.classID = round(*(pfClass+i));
        
        //box coordinate
        stDetectIfno.ymin =  *(pfBBox+(i*ALIGN_UP(4,INNER_MOST_ALIGNMENT))+0);
        stDetectIfno.xmin =  *(pfBBox+(i*ALIGN_UP(4,INNER_MOST_ALIGNMENT))+1);
        stDetectIfno.ymax =  *(pfBBox+(i*ALIGN_UP(4,INNER_MOST_ALIGNMENT))+2);
        stDetectIfno.xmax =  *(pfBBox+(i*ALIGN_UP(4,INNER_MOST_ALIGNMENT))+3);

        vDetectInfos.push_back(stDetectIfno);
    }
}

MI_U32 CIpuDetect::IpuDoTrack(std::vector<DetectInfo_S>& vDetectInfos)
{
    std::vector <TrackBBox> detFrameData;
    std::vector <std::vector<TrackBBox>> detFrameDatas;
    
    for (size_t i = 0; i < vDetectInfos.size(); i++)
    {
        TrackBBox stTrkBbox;
        DetectInfo_S stDetBbox;

        const int label = vDetectInfos[i].classID;
        const float score = vDetectInfos[i].score;
        
        if (score < DETECT_THRESHOLD /*|| label == 0*/)
        {
            continue;
        }
 
        stDetBbox.xmin = vDetectInfos[i].xmin * m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[2]; // width/col
        stDetBbox.xmin = stDetBbox.xmin < 0 ? 0 : stDetBbox.xmin;

        stDetBbox.ymin = vDetectInfos[i].ymin * m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[1]; // height/row
        stDetBbox.ymin = stDetBbox.ymin < 0 ? 0 : stDetBbox.ymin;

        stDetBbox.xmax = vDetectInfos[i].xmax * m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[2];
        stDetBbox.xmax = (stDetBbox.xmax > m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[2]) ?
                    m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[2] : stDetBbox.xmax;

        stDetBbox.ymax = vDetectInfos[i].ymax * m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[1];
        stDetBbox.ymax = (stDetBbox.ymax > m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[1]) ?
                    m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[1] : stDetBbox.ymax ;

        stDetBbox.score = score;

        memset(&stTrkBbox,0x00,sizeof(TrackBBox));
		stTrkBbox.x = stDetBbox.xmin;
		stTrkBbox.y = stDetBbox.ymin;
		stTrkBbox.w = stDetBbox.xmax-stDetBbox.xmin;
		stTrkBbox.h = stDetBbox.ymax-stDetBbox.ymin;
		stTrkBbox.score = stDetBbox.score;
	    stTrkBbox.classID = label;

		detFrameData.push_back(stTrkBbox);
		
		MIXER_DBG("label = %d, score=%f [%.2f %.2f %.2f %.2f] \n",label, score, stTrkBbox.x, stTrkBbox.y, stTrkBbox.w, stTrkBbox.h);
    }
     
    if(detFrameData.size())
    {
	    detFrameDatas.push_back(detFrameData);
        m_DetectTrackBBoxs = m_DetectBBoxTracker.track_iou(detFrameDatas);
    }

    return detFrameDatas.size();
}

MI_S32 CIpuDetect::IpuGetRectInfo(ST_DlaRectInfo_T* pstRectInfo)
{
    MI_S32 count = 0;
    string strLabelName = "";

    for(MI_U8 i = 0; i < m_DetectTrackBBoxs.size(); i++)
	{
	    int index = m_DetectTrackBBoxs[i].boxes.size()-1;
		TrackBBox& trackBox = m_DetectTrackBBoxs[i].boxes[index];
          
		if ((trackBox.classID >= 0) && (trackBox.classID <= m_s32LabelCount))
        {
            strLabelName = m_szLabelName[trackBox.classID];
        }

	  	memset(&pstRectInfo[count], 0, sizeof(ST_DlaRectInfo_T));
		//convert to 1920x1080
		pstRectInfo[count].rect.u32X = trackBox.x * m_stVpePortMode.u16Width/m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[2];
		pstRectInfo[count].rect.u32Y = trackBox.y * m_stVpePortMode.u16Height/m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[1];
		pstRectInfo[count].rect.u16PicW = trackBox.w * m_stVpePortMode.u16Width/m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[2];
		pstRectInfo[count].rect.u16PicH = trackBox.h * m_stVpePortMode.u16Height/m_stModelInfo.stIpuDesc.astMI_InputTensorDescs[0].u32TensorShape[1];
		snprintf(pstRectInfo[count].szObjName, sizeof(pstRectInfo[count].szObjName) - 1, "%s ", strLabelName.c_str());

		MIXER_DBG("stRectInfo[count].rect.u32X =%d    stRectInfo[count].rect.u32Y=%d\n", pstRectInfo[count].rect.u32X, pstRectInfo[count].rect.u32Y);
		MIXER_DBG("stRectInfo[count].rect.u16PicW =%d	stRectInfo[count].rect.u16PicH=%d\n", pstRectInfo[count].rect.u16PicW, pstRectInfo[count].rect.u16PicH);
		MIXER_DBG("bboxes.classID=%d  name:%s\n",trackBox.classID,strLabelName.c_str());

        if (++count >= MAX_DETECT_RECT_NUM)
		{
	        break;
		}
    }

    return count;
}

MI_S32 CIpuDetect::IpuDoDetect(MI_SYS_BufInfo_t* pstBufInfo)
{
    MI_IPU_TensorVector_t stOutputTensorVector;
    
    if (pstBufInfo == NULL)
    {
        return E_MI_ERR_FAILED;
    }
    
    if(IpuRunAndGetOutputTensor(&m_stModelInfo, pstBufInfo, &stOutputTensorVector) == MI_SUCCESS)
    {
        IpuPrintResult(&stOutputTensorVector);
        MI_IPU_PutOutputTensors(m_stModelInfo.u32IpuChn,&stOutputTensorVector);
    }

    return MI_SUCCESS;
}

void CIpuDetect::IpuPrintResult(MI_IPU_TensorVector_t* pstOutputTensorVector)
{
    MI_S32 count = 0;
    std::vector<DetectInfo_S> vDetectInfos;
	ST_DlaRectInfo_T stRectInfo[MAX_DETECT_RECT_NUM];
        
    IpuGetDetectInfo(pstOutputTensorVector, vDetectInfos);
    if(IpuDoTrack(vDetectInfos))
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

    
