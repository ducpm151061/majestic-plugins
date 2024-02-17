/*
* mid_utils.h- Sigmastar
*
* Copyright (C) 2018 Sigmastar Technology Corp.
*
* Author: XXXX <XXXX@sigmastar.com.cn>
*
* This software is licensed under the terms of the GNU General Public
* License version 2, as published by the Free Software Foundation, and
* may be copied, distributed, and modified under those terms.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
*/

#ifndef _COMMON_H_
#define _COMMON_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "mi_sys_datatype.h"
#include "st_common.h"

#define DBGLV_NONE           0   //disable all the debug message
#define DBGLV_INFO           1   //information
#define DBGLV_NOTICE         2   //normal but significant condition
#define DBGLV_DEBUG          3   //debug-level messages
#define DBGLV_WARNING        4   //warning conditions
#define DBGLV_ERR            5   //error conditions
#define DBGLV_CRIT           6   //critical conditions
#define DBGLV_ALERT          7   //action must be taken immediately
#define DBGLV_EMERG          8   //system is unusable

#define COLOR_NONE                 "\033[0m"
#define COLOR_BLACK                "\033[0;30m"
#define COLOR_BLUE                 "\033[0;34m"
#define COLOR_GREEN                "\033[0;32m"
#define COLOR_CYAN                 "\033[0;36m"
#define COLOR_RED                  "\033[0;31m"
#define COLOR_YELLOW               "\033[1;33m"
#define COLOR_WHITE                "\033[1;37m"

static int g_dbglevel = DBGLV_ERR;

#define MIXER_NOP(fmt, args...)
#define MIXER_DBG(fmt, args...) \
    if(g_dbglevel <= DBGLV_DEBUG)  \
        do { \
            printf(COLOR_GREEN "[DBG]:%s[%d]:  " COLOR_NONE, __FUNCTION__,__LINE__); \
            printf(fmt, ##args); \
        }while(0)

#define MIXER_WARN(fmt, args...) \
    if(g_dbglevel <= DBGLV_WARNING)  \
        do { \
            printf(COLOR_YELLOW "[WARN]:%s[%d]: " COLOR_NONE, __FUNCTION__,__LINE__); \
            printf(fmt, ##args); \
        }while(0)

#define MIXER_INFO(fmt, args...) \
    if(g_dbglevel <= DBGLV_INFO)  \
        do { \
            printf("[INFO]:%s[%d]: \n", __FUNCTION__,__LINE__); \
            printf(fmt, ##args); \
        }while(0)

#define MIXER_ERR(fmt, args...) \
    if(g_dbglevel <= DBGLV_ERR)  \
        do { \
            printf(COLOR_RED "[ERR]:%s[%d]: " COLOR_NONE, __FUNCTION__,__LINE__); \
            printf(fmt, ##args); \
        }while(0)


#define _abs(x)  ((x) > 0 ? (x) : (-(x)))
        
#ifndef MAX
#define MAX(a,b) ((a)>(b) ? (a) : (b))
#endif
        
#ifndef MIN
#define MIN(a,b) ((a)<(b) ? (a) : (b))
#endif
        
#ifndef ALIGN_UP
#define ALIGN_UP(value, align)    ((value+align-1) / align * align)
#endif
        
#ifndef ALIGN_DOWN
#define ALIGN_DOWN(value, align)  (value / align * align)
#endif
        
#ifndef ALIGN_2xUP
#define ALIGN_2xUP(x)               (((x+1) / 2) * 2)
#endif
        
#ifndef ALIGN_4xUP
#define ALIGN_4xUP(x)               (((x+1) / 4) * 4)
#endif

#ifdef __cplusplus
}
#endif

#endif //_MID_UTILS_H_

