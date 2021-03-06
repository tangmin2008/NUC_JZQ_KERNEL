/*
 * drivers/char/hndl_char_devices/hnos_defines_hntt1800x.h 
 *
 *
 * The source code in this file can be freely used, adapted,
 * and redistributed in source or binary form, so long as an
 * acknowledgment appears in derived source files.  The citation
 * should list that the code comes from the book "Linux Device
 * Drivers" by Alessandro Rubini and Jonathan Corbet, published
 * by O'Reilly & Associates.   No warranty is attached;
 * we cannot take responsibility for errors or fitness for use.
 *
 */
#ifndef __HNOS_DEFINES_HNTT1800X_H 
#define __HNOS_DEFINES_HNTT1800X_H 

/* HNTT1800X 遥信 通道定义 */
#define		INPUT_STATUS_0			(1 << 0)      /* 状态量1 */
#define		INPUT_STATUS_1			(1 << 1)      /* 状态量2 */
#define		INPUT_OPEN_COVER		(1 << 2)      /* 端盖开盖检测 */
#define		INPUT_OPEN_GLASS		(1 << 3)      /* 下透镜开盖检测 */
#define		INPUT_TDK6513_STATE		(1 << 4)      /* 校表状态 */
#define		INPUT_ADSORB_IRDA		(1 << 5)      /* 红外判定 */

#define		INPUT_SMCBUS_OFFSET		16              /* (总线扩展)遥信输入从第16路开始 */
#define		INPUT_SMCBUS_SIZE		16              /* (总线扩展)遥信共计16路 */

/* HNTT1800X 遥控 通道定义 */
#define		OUTPUT_CTRL_0			(1 << 0)      /* 负荷控制输出 */
#define		OUTPUT_CTRL_1			(1 << 1)      /* 告警输出 */
#define		OUTPUT_REMOTE_POWER		(1 << 2)      /* 遥控告警电源控制, 写1打开电源 */
#define		OUTPUT_REMOTE_ENABLE		(1 << 3)      /* 遥控告警允许, 写0允许 */
#define		OUTPUT_PLC_POWER		(1 << 4)      /* 载波电源控制 */

#define		OUTPUT_SMCBUS_OFFSET		16              /* (总线扩展)遥控输出从第16路开始 */
#define		OUTPUT_SMCBUS_SIZE		16              /* (总线扩展)遥信共计16路 */

#define		NCHANNEL_PER_SMCBUS		8      
#define		NR_SMCBUS			2

/* HNTT1800U 2.0 遥控 通道定义 */
#define		OUTPUT_BEEP_ENABLE		(1 << 0)      /* 控制蜂鸣器报警，高电平使蜂鸣器，上电默认状态设置为低电平*/
#define		OUTPUT_MAC_ENABLE		(1 << 1)      /* MAC地址设置使能，低电平有效，信号有效时，使能载波模块MAC地址设置 */
#define		OUTPUT_PLC_RESET		(1 << 2)      /* 载波模块复位控制（低电平有效）*/
#define		OUTPUT_ETHERNET_RESET	(1 << 3)      /* 以太网复位控制引脚, 低有效 */


#endif
