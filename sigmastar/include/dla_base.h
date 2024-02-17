#ifndef _DLA_BASE_H_
#define _DLA_BASE_H_

#include <map>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include "mi_common_datatype.h"
#include "mi_ipu_datatype.h"
#include "mi_divp_datatype.h"
using namespace std;

#ifdef __cplusplus
extern "C" {
#endif

#define LABEL_CLASS_COUNT       1200
#define LABEL_NAME_MAX_SIZE     60

#define DETECT_THRESHOLD        0.5
#define MAX_DETECT_RECT_NUM     10
#define INNER_MOST_ALIGNMENT    8

extern pthread_mutex_t g_stMutexOsdUptState;
extern pthread_cond_t  g_condOsdUpadteState;

typedef enum
{
    Model_Type_None = -1,
    Model_Type_Classify,
    Model_Type_Detect,
    Model_Type_FaceReg,
    Model_Type_Hc,
    Model_Type_FdFp,
    Model_Type_Debug,
    Model_Type_Butt,
} IPU_Model_Type_E;

typedef struct
{
    IPU_Model_Type_E enModelType;
    char szIpuFirmware[128];
    char szModelFile[128];
    union _u
    {
        struct _ExtendInfo1
        {
            char szLabelFile[128];
        } ExtendInfo1;
        struct _ExtendInfo2
        {
            char szModelFile1[128];
            char szFaceDBFile[128];
            char szNameListFile[128];
        } ExtendInfo2;
    }u;
} IPU_InitInfo_S;

typedef struct
{
    IPU_InitInfo_S stIpuInitInfo;

    MI_BOOL bDlaUse;
    MI_DIVP_OutputPortAttr_t stDivpPortAttr;
} IPU_DlaInfo_S;

typedef struct
{
    MI_U32 u32IpuChn;
    MI_U32 u32DivpChn;
    MI_U32 u32VpeChn;
    MI_U32 u32VpePort;

    MI_U32 u32InBufDepth;
    MI_U32 u32OutBufDepth;

    MI_S32 s32DivpFd;
        
    MI_DIVP_OutputPortAttr_t stDivpPortAttr;   
    MI_IPU_SubNet_InputOutputDesc_t stIpuDesc;
} IPU_ModelInfo_S;

class CIpuInterface
{
public:
    CIpuInterface(IPU_DlaInfo_S& stDlaInfo)
    {
        memcpy(&m_stDlaInfo, &stDlaInfo, sizeof(IPU_DlaInfo_S));
    }

    virtual ~CIpuInterface(){}
    virtual void IpuRunProcess() = 0;
    virtual void IpuRunCommand(MI_U32 u32CmdCode) = 0;
    
protected:
    IPU_DlaInfo_S    m_stDlaInfo;
};

class CIpuCommon : public CIpuInterface
{
public:
    CIpuCommon(IPU_DlaInfo_S &stDlaInfo);
    virtual ~CIpuCommon();
    void IpuRunProcess(){};
    void IpuRunCommand(MI_U32 u32CmdCode){};
    
protected:
    MI_S32 IpuGetModelBufSize(MI_U32& u32BufSize);
    MI_S32 IpuCreateDevice(MI_U32 u32BufSize);
    MI_S32 IpuDestroyDevice();
    MI_S32 IpuCreateChannel(char* pModelFile, IPU_ModelInfo_S* pstModelInfo);
    MI_S32 IpuDestroyChannel(IPU_ModelInfo_S* pstModelInfo);
    MI_S32 IpuCreateStream(IPU_ModelInfo_S* pstModelInfo);
    MI_S32 IpuDestroyStream(IPU_ModelInfo_S* pstModelInfo);
    MI_S32 IpuGetLabels(char *pLabelFile, map<int,string>& LabelName);
    MI_S32 IpuScaleToModelSize(IPU_ModelInfo_S* pstModelInfo, MI_SYS_BufInfo_t* pstBufInfo, MI_IPU_TensorVector_t* pstInputTensorVector);
    MI_S32 IpuRunAndGetOutputTensor(IPU_ModelInfo_S* pstModelInfo, MI_SYS_BufInfo_t* pstBufInfo, MI_IPU_TensorVector_t* pstTensorVector);
        
protected:
    IPU_ModelInfo_S m_stModelInfo;
    
};

#ifdef __cplusplus
}

#endif
#endif
