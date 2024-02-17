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

#include <opencv2/highgui/highgui.hpp>
#include "mi_common_datatype.h"
#include "thread_api.h"

MI_S32 Get_thread_policy(pthread_attr_t *attr)
{
    int policy;
    int rs = pthread_attr_getschedpolicy(attr, &policy);
    assert (rs == 0);
    switch (policy)
    {
        case SCHED_FIFO:
            //MIXER_DBG("policy = SCHED_FIFO\n");
            break;
        case SCHED_RR:
            //MIXER_DBG("policy = SCHED_RR");
            break;
        case SCHED_OTHER:
            //MIXER_DBG("policy = SCHED_OTHER\n");
            break;
        default:
            //MIXER_DBG("policy = UNKNOWN\n");
            break; 
    }
    return policy;

}

void Show_thread_priority(pthread_attr_t *attr,int policy)
{
    int priority = sched_get_priority_max(policy);
    assert (priority != -1);
    //MIXER_DBG("max_priority = %d\n", priority);
    priority = sched_get_priority_min (policy);
    assert (priority != -1);
    //MIXER_DBG("min_priority = %d\n", priority);
}

MI_S32 Get_thread_priority(pthread_attr_t *attr)
{
    struct sched_param param;
    int rs = pthread_attr_getschedparam(attr, &param);
    assert (rs == 0);
    //MIXER_DBG("priority = %d\n", param.__sched_priority);
    return param.__sched_priority;
}

void Set_thread_policy(pthread_attr_t *attr,int policy)
{
    MI_S32 rs = pthread_attr_setschedpolicy(attr, policy);
    assert (rs == 0);
    Get_thread_policy(attr);
}

void Create_thread(pthread_t *thread, int priority, const char *name,
                       void *(*thread_task)(void*), void *arg)
{
    pthread_attr_t attr;
    struct sched_param s_parm;
    MI_S32 S32Policy = SCHED_FIFO;

    pthread_attr_init(&attr);
    Set_thread_policy(&attr,S32Policy);
    s_parm.sched_priority = priority;
    if(priority <= 0)
        s_parm.sched_priority = sched_get_priority_max(S32Policy);
    pthread_attr_setschedparam(&attr, &s_parm);
	S32Policy = Get_thread_policy(&attr);
    Show_thread_priority(&attr, S32Policy);
    Get_thread_priority(&attr);
    pthread_create(thread, &attr, thread_task, arg);
    pthread_setname_np(*thread , name);
}
                       
void Destroy_thread(pthread_t thread)
{
    pthread_cancel(thread);
    pthread_join(thread, NULL);
}

