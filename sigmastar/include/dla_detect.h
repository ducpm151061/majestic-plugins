#ifndef _DLA_DETECT_H_
#define _DLA_DETECT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "dla_base.h"
#include "mi_vpe_datatype.h"
#include "osd.h"
#include "iout.h"

typedef struct
{
    float xmin;
    float ymin;
    float xmax;
    float ymax;
    float score;
    int   classID;
}DetectInfo_S;

class CIpuDetect : public CIpuCommon
{
public:
    CIpuDetect(IPU_DlaInfo_S &stDlaInfo);
    virtual ~CIpuDetect();
    void IpuRunProcess();
    
protected:
    MI_S32 IpuInit();
    MI_S32 IpuDeInit();
    MI_S32 IpuGetVpeMode(void);
    MI_S32 IpuDoDetect(MI_SYS_BufInfo_t* pstBufInfo);
    MI_S32 IpuGetRectInfo(ST_DlaRectInfo_T* pstRectInfo);
    MI_U32 IpuDoTrack(std::vector<DetectInfo_S>& vDetectInfos);
    void IpuPrintResult(MI_IPU_TensorVector_t* pstOutputTensorVector);
    void IpuGetDetectInfo(MI_IPU_TensorVector_t* pstOutputTensorVector, std::vector<DetectInfo_S>& vDetectInfos);
    
    
protected:
    MI_S32 m_s32LabelCount;
    map<int, string>   m_szLabelName;
    std::vector<Track> m_DetectTrackBBoxs;
    IOUTracker		   m_DetectBBoxTracker;

    MI_VPE_PortMode_t  m_stVpePortMode;
};

#ifdef __cplusplus
}

#endif
#endif
