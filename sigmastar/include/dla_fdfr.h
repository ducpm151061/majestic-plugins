#ifndef _DLA_FDFR_H_
#define _DLA_FDFR_H_

#include "dla_base.h"
#include "mi_vpe_datatype.h"
#include "iout.h"
#include "FaceDatabase.h"
#include "face_recognize.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stFaceInfo_s
{
    char faceName[64];
    unsigned short xPos;
    unsigned short yPos;
    unsigned short faceW;
    unsigned short faceH;
    unsigned short winWid;
    unsigned short winHei;
}stFaceInfo_t;

typedef struct stCountName_s
{
    unsigned int Count;
    std::string Name;
} stCountName_t;

typedef struct stAddPersion_s
{
    bool bDone;
    MI_S32 s32TrackId;
    char NewAddName[256];
} stAddPersion_t;

class CIpuFdfr : public CIpuCommon
{
public:
    CIpuFdfr(IPU_DlaInfo_S &stDlaInfo);
    virtual ~CIpuFdfr();
    void IpuRunProcess();
    void IpuRunCommand(MI_U32 u32CmdCode);
    
protected:
    MI_S32 IpuInit();
    MI_S32 IpuDeInit();
    MI_S32 IpuGetVpeMode(void);
    MI_S32 IpuGetModelBufSize(MI_U32& u32BufSize);    
    MI_S32 IpuDoFd(MI_SYS_BufInfo_t* pstBufInfo, MI_IPU_TensorVector_t* pstTensorVector);
    MI_S32 IpuDoFr(MI_SYS_BufInfo_t* pstBufInfo, MI_IPU_TensorVector_t* pstFdTensorVector);
    MI_U32 IpuDoTrack(std::vector<DetBBox>& vDetectInfos);
    MI_S32 IpuGetRectInfo(ST_DlaRectInfo_T* pstRectInfo);
    MI_S32 IpuDoRecognition(MI_SYS_BufInfo_t* pstBufInfo, 
                                 MI_IPU_TensorVector_t* pstInputVector, 
                                 MI_IPU_TensorVector_t* pstOutputVector);   
    void IpuLoadFaceData();
    void IpuCleanFaceData();
    void IpuGetDetectInfo(MI_IPU_TensorVector_t* pstOutputTensorVector, std::vector<DetBBox>& vDetectInfos);
    void IpuSaveFaceData(TrackBBox& trackBox, std::string strName,int Id);
    void IpuPrintResult();
    void IpuRemoveOldName(char* DelName);
    stCountName_t IpuSearchNameByID(std::map<int, stCountName_t> &MapIdName, int id);
    
protected:
    IPU_ModelInfo_S m_stFrModelInfo;

    FaceDatabase    m_FaceData;
    FaceRecognizeUtils m_FaceRecognizer;
    std::vector<stFaceInfo_t>    m_FaceInfo;
    std::map<int,stCountName_t>  m_MapIdName;
    stAddPersion_t m_AddPersion;
   
    std::vector<Track> m_DetectTrackBBoxs;
    IOUTracker		   m_DetectBBoxTracker;

    MI_VPE_PortMode_t  m_stVpePortMode;
};

#ifdef __cplusplus
}

#endif
#endif
