#ifndef _THREAD_API_H_
#define _THREAD_API_H_
#include <pthread.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef void *(*thread_task)(void*);
                                    
MI_S32 Get_thread_policy(pthread_attr_t *attr);
void   Set_thread_policy(pthread_attr_t *attr,int policy);
MI_S32 Get_thread_priority(pthread_attr_t *attr);
void   Show_thread_priority(pthread_attr_t *attr,int policy);

void Create_thread(pthread_t *thread, int priority, const char *name,
                       void *(*thread_task)(void*), void *arg);
void Destroy_thread(pthread_t thread);

#ifdef __cplusplus
}
#endif

#endif