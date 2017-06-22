/*
 ## Cypress USB 3.0 Platform header file (cyfxbulksrcsink.h)
 ## ===========================
 ##
 ##  Copyright Cypress Semiconductor Corporation, 2010-2011,
 ##  All Rights Reserved
 ##  UNPUBLISHED, LICENSED SOFTWARE.
 ##
 ##  CONFIDENTIAL AND PROPRIETARY INFORMATION
 ##  WHICH IS THE PROPERTY OF CYPRESS.
 ##
 ##  Use of this file is governed
 ##  by the license agreement included in the file
 ##
 ##     <install>/license/license.txt
 ##
 ##  where <install> is the Cypress software
 ##  installation root directory path.
 ##
 ## ===========================
*/

/* This file contains the constants used by the bulk source sink application example */

#ifndef _INCLUDED_SDRx3_H_
#define _INCLUDED_SDRx3_H_

#include "cyu3types.h"
#include "cyu3usbconst.h"
#include "cyu3externcstart.h"

#define CY_FX_BULKSRCSINK_DMA_BUF_COUNT      (6)                       /* Bulk channel buffer count */
#define CY_FX_BULKSRCSINK_DMA_TX_SIZE        (0)                       /* DMA transfer size is set to infinite */
#define CY_FX_BULKSRCSINK_THREAD_STACK       (0x1000)                  /* Bulk loop application thread stack size */
#define CY_FX_BULKSRCSINK_THREAD_PRIORITY    (8)                       /* Bulk loop application thread priority */


/* Endpoint and socket definitions for the bulk source sink application */

/* To change the producer and consumer EP enter the appropriate EP numbers for the #defines.
 * In the case of IN endpoints enter EP number along with the direction bit.
 * For eg. EP 6 IN endpoint is 0x86
 *     and EP 6 OUT endpoint is 0x06.
 * To change sockets mention the appropriate socket number in the #defines. */

/* Note: For USB 2.0 the endpoints and corresponding sockets are one-to-one mapped
         i.e. EP 1 is mapped to UIB socket 1 and EP 2 to socket 2 so on */

#define CY_FX_EP_PRODUCER               0x01    /* EP 1 OUT */
#define CY_FX_EP_CONSUMER               0x81    /* EP 1 IN */

#define CY_FX_EP_PRODUCER_SOCKET        CY_U3P_UIB_SOCKET_PROD_1    /* Socket 1 is producer */
#define CY_FX_EP_CONSUMER_SOCKET        CY_U3P_UIB_SOCKET_CONS_1    /* Socket 1 is consumer */

/* Used with FX3 Silicon. */
#define CY_FX_PRODUCER_PPORT_SOCKET    CY_U3P_PIB_SOCKET_0    /* P-port Socket 0 is producer */
#define CY_FX_CONSUMER_PPORT_SOCKET    CY_U3P_PIB_SOCKET_3    /* P-port Socket 3 is consumer */

/* Burst mode definitions: Only for super speed operation. The maximum burst mode 
 * supported is limited by the USB hosts available. The maximum value for this is 16
 * and the minimum (no-burst) is 1. */

#define CY_FX_EP_BURST_LENGTH          (8)     /* Super speed burst length in packets. */

#define CY_FX_VND_REQ1                (uint8_t)(0x05)	/* Vendor request */

/* USB vendor request to read the 8 byte firmware ID. This will return content
 * of glFirmwareID array. */
#define CY_FX_RQT_ID_CHECK                      (0xB0)

/* USB vendor request to write to I2C  connected. The EEPROM page size is
 * fixed to 64 bytes. The I2C EEPROM address is provided in the value field. The
 * memory address to start writing is provided in the index field of the request.
 * The maximum allowed request length is 4KB. */

#define CY_FX_RQT_I2C_WRITE                     (0xBA)
#define CY_FX_RQT_I2C_READ                      (0xBB)
#define CY_FX_RQT_GPIO_WRITE 					(0xBC)
#define CY_FX_RQT_GPIO_PWM                      (0xBD)

typedef struct pwmxio_t
{
    uint32_t  dutycicle;           /* The actual bytes starting  */
} pwmxio_t;

typedef struct outxio_t
{
    uint8_t  buffer[15];         /* The actual bytes starting  */
} outxio_t;

typedef struct i2cxio_t
{
    uint8_t  i2caddr;            /* The i2c device address  */
    uint8_t  regaddr;            /* The device register address  */
    uint8_t  isRead;           	 /* (0) False value WRITE, (1) True value Read */
    uint8_t  lencount;			 /*	 byte count  < 12 */
    uint8_t  databyte[12];
} i2cxio_t;



#define OUTXIO0 (1) 	// LED_OVERLOAD  GPIO21  bit position
#define OUTXIO1 (2) 	// LED_MODEA  GPIO22
#define OUTXIO2 (4) 	// LED_MODEB  GPIO22

#define OUTXIO3 (8)  	// SEL0  GPIO26
#define OUTXIO4 (16) 	// SEL1  GPIO27

#define OUTXIO5 (32)  	// SHDWN  GPIO28
#define OUTXIO6 (64)  	// DITH   GPIO29

#define CY_FX_PWM_PERIOD                 (201600 - 1)   /* PWM time period. */
#define CY_FX_PWM_25P_THRESHOLD          (50400  - 1)   /* PWM threshold value for 25% duty cycle. */

#define LED_OVERLOAD (21)
#define LED_MODEA    (22)
#define LED_MODEB    (23)

#define OUTXIO (0)
#define I2CXIO (1)
#define PWMXIO (2)



/* Extern definitions for the USB Descriptors */
extern const uint8_t CyFxUSB20DeviceDscr[];
extern const uint8_t CyFxUSB30DeviceDscr[];
extern const uint8_t CyFxUSBDeviceQualDscr[];
extern const uint8_t CyFxUSBFSConfigDscr[];
extern const uint8_t CyFxUSBHSConfigDscr[];
extern const uint8_t CyFxUSBBOSDscr[];
extern const uint8_t CyFxUSBSSConfigDscr[];
extern const uint8_t CyFxUSBStringLangIDDscr[];
extern const uint8_t CyFxUSBManufactureDscr[];
extern const uint8_t CyFxUSBProductDscr[];

#include <cyu3externcend.h>

#endif /* _INCLUDED_CYFXBULKSRCSINK_H_ */

/*[]*/
