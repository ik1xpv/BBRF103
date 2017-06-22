/*
 ## Cypress USB 3.0 Platform source file (SDRx3.c)
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

/* This file illustrates the bulk source sink application example using the DMA MANUAL_IN
 * and DMA MANUAL_OUT mode */

/*
   This example illustrates USB endpoint data source and data sink mechanism. The example
   comprises of vendor class USB enumeration descriptors with 2 bulk endpoints. A bulk OUT
   endpoint acts as the producer of data and acts as the sink to the host. A bulk IN endpoint
   acts as the consumer of data and acts as the source to the host.

   The data source and sink is achieved with the help of a DMA MANUAL IN channel and a DMA
   MANUAL OUT channel. A DMA MANUAL IN channel is created between the producer USB bulk
   endpoint and the CPU. A DMA MANUAL OUT channel is created between the CPU and the consumer
   USB bulk endpoint. Data is received in the IN channel DMA buffer from the host through the
   producer endpoint. CPU is signalled of the data reception using DMA callbacks. The CPU
   discards this buffer. This leads to the sink mechanism. A constant patern data is loaded
   onto the OUT Channel DMA buffer whenever the buffer is available. CPU issues commit of
   the DMA data transfer to the consumer endpoint which then gets transferred to the host.
   This leads to a constant source mechanism.

   The DMA buffer size is defined based on the USB speed. 64 for full speed, 512 for high speed
   and 1024 for super speed. CY_FX_BULKSRCSINK_DMA_BUF_COUNT in the header file defines the
   number of DMA buffers.

   For performance optimizations refer the readme.txt
 */

#include "cyu3system.h"
#include "cyu3os.h"
#include "cyu3dma.h"
#include "cyu3error.h"
#include "cyu3usb.h"
#include "cyu3uart.h"
#include "cyu3gpif.h"
#include "cyu3gpio.h"
#include "cyu3pib.h"
#include "pib_regs.h"
#include "i2cmodule.h"
#include "cyfxgpif2config.h"
#include "sdrx3.h"

uint8_t  glEp0Buffer[32];
uint16_t glRecvdLen;
CyU3PThread     SDR_AppThread;	 /* Application thread structure */
// CyU3PDmaChannel glChHandleBulkSink;      /* DMA MANUAL_IN channel handle.          */
CyU3PDmaMultiChannel glChHandleBulkSrc;       /* DMA MANUAL_OUT channel handle.         */

CyBool_t glIsApplnActive = CyFalse;      /* Whether the source sink application is active or not. */
CyBool_t glStartSDRGpif = CyFalse;
uint32_t glDMARxCount = 0;               /* Counter to track the number of buffers received. */
uint32_t glDMATxCount = 0;               /* Counter to track the number of buffers transmitted. */

/* Firmware ID variable that may be used to verify I2C firmware. */
const uint8_t glFirmwareID[32] __attribute__ ((aligned (32))) = { 'S', 'D', 'D', 'C', 'B', '0', '1', '\0' };


/* Application Error Handler */
void
SDR_ErrorHandler (
        CyU3PReturnStatus_t apiRetStatus    /* API return status */
        )
{
    /* Application failed with the error code apiRetStatus */
// LED_OVERLOAD GPIO21 start
// LED_MODEA 	GPIO22 ==> 0
// LED_MODEB 	GPIO23 ==> 1
	uint8_t i,x,y;

    /* Loop Indefinitely */
	x = (uint8_t) apiRetStatus;
    for (;;)
    {
    	y = x;
    	CyU3PGpioSetValue (LED_OVERLOAD, 1);
        CyU3PThreadSleep (300);   /* Thread sleep : 200 ms */
    	CyU3PGpioSetValue (LED_OVERLOAD, 0);
    	CyU3PThreadSleep (200);   /* Thread sleep : 100 ms */
        for (i =0; i < 8 ;i++)
        {
        	if ((y & 0x80) == 0)
        	{
        		CyU3PGpioSetValue (LED_MODEA, 1);
        		CyU3PGpioSetValue (LED_MODEB, 0);
        	}
        	else
        	{
        		CyU3PGpioSetValue (LED_MODEA, 0);
        		CyU3PGpioSetValue (LED_MODEB, 1);
        	}
        	CyU3PThreadSleep (500);   /* Thread sleep : 200 ms */
    		CyU3PGpioSetValue (LED_MODEA, 0);
    		CyU3PGpioSetValue (LED_MODEB, 0);
        	CyU3PThreadSleep (200);   /* Thread sleep : 100 ms */
        	y = y << 1 ;
        }
    }
}




/* GPIO interrupt callback handler. This is received from
 * the interrupt context. So DebugPrint API is not available
 * from here. Set an event in the event group so that the
 * GPIO thread can print the event information. */
void SDR_GpioIntrCb (
        uint8_t gpioId /* Indicates the pin that triggered the interrupt */
        )
{
    CyBool_t gpioValue = CyFalse;
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;

    /* Get the status of the pin */
    apiRetStatus = CyU3PGpioGetValue (gpioId, &gpioValue);
    if (apiRetStatus == CY_U3P_SUCCESS)
    {
        /* Check status of the pin */
        if (gpioValue == CyTrue)
        {
            /* Set GPIO high event */
        }
        else
        {
            /* Set GPIO low Event */
        }
    }
}
CyU3PReturnStatus_t
ConfGPIOsimpleout( uint8_t gpioid)
{
	 CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
	 CyU3PGpioSimpleConfig_t gpioConfig;

	  apiRetStatus = CyU3PDeviceGpioOverride (gpioid, CyTrue);
	    if (apiRetStatus != 0)
	    {
	        /* Error Handling */
	        SDR_ErrorHandler (apiRetStatus);
	    }
	    /* Configure GPIO gpioid as output */
	      gpioConfig.outValue = CyFalse;
	      gpioConfig.driveLowEn = CyTrue;
	      gpioConfig.driveHighEn = CyTrue;
	      gpioConfig.inputEn = CyFalse;
	      gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
	      apiRetStatus = CyU3PGpioSetSimpleConfig(gpioid , &gpioConfig);
	      if (apiRetStatus != CY_U3P_SUCCESS)
	      {
	          /* Error handling */
	          SDR_ErrorHandler (apiRetStatus);
	      }
	 return apiRetStatus;
}

CyU3PReturnStatus_t
ConfGPIOPWM (uint8_t gpioid ,uint32_t value)
{
	  CyU3PGpioComplexConfig_t gpioConfig;
	    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
	    apiRetStatus = CyU3PDeviceGpioOverride ( gpioid, CyTrue);
	   	    if (apiRetStatus != 0)
	   	    {
	   	        // Error Handling
	   	        SDR_ErrorHandler (apiRetStatus);
	   	    }
    // Configure GPIO  as PWM output
    gpioConfig.outValue = CyFalse;
    gpioConfig.inputEn = CyFalse;
    gpioConfig.driveLowEn = CyTrue;
    gpioConfig.driveHighEn = CyTrue;
    gpioConfig.pinMode = CY_U3P_GPIO_MODE_PWM;
    gpioConfig.intrMode = CY_U3P_GPIO_NO_INTR;
    gpioConfig.timerMode = CY_U3P_GPIO_TIMER_HIGH_FREQ;
    gpioConfig.timer = 0;
    gpioConfig.period = CY_FX_PWM_PERIOD;
    gpioConfig.threshold = value;
    apiRetStatus = CyU3PGpioSetComplexConfig(gpioid, &gpioConfig);
    return  apiRetStatus;
}

/* Init GPIOs  */
void
SDR_GpioInit (void)
{
    CyU3PGpioClock_t gpioClock;
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;

    /* Init the GPIO module */
    gpioClock.fastClkDiv = 2;
    gpioClock.slowClkDiv = 0;
    gpioClock.simpleDiv = CY_U3P_GPIO_SIMPLE_DIV_BY_2;
    gpioClock.clkSrc = CY_U3P_SYS_CLK;
    gpioClock.halfDiv = 0;

    apiRetStatus = CyU3PGpioInit(&gpioClock, SDR_GpioIntrCb);
    if (apiRetStatus != 0)
    {
        /* Error Handling */
        CyU3PDebugPrint (4, "CyU3PGpioInit failed, error code = %d\n", apiRetStatus);
        SDR_ErrorHandler (apiRetStatus);
    }

    // Configure GPIO  as PWM output
    ConfGPIOPWM(24,CY_FX_PWM_25P_THRESHOLD);
    ConfGPIOsimpleout(21);
    ConfGPIOsimpleout(22);
    ConfGPIOsimpleout(23);
    ConfGPIOsimpleout(26);
    ConfGPIOsimpleout(27);
    ConfGPIOsimpleout(28);
    ConfGPIOsimpleout(29);

    CyU3PGpioSetValue (21, 0);
    CyU3PGpioSetValue (22, 1);
    CyU3PGpioSetValue (28, 1);
}


/* This function initializes the debug module. The debug prints
 * are routed to the UART and can be seen using a UART console
 * running at 115200 baud rate. */
void
SDR_ApplnDebugInit (void)
{
    CyU3PUartConfig_t uartConfig;
    CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;

    /* Initialize the UART for printing debug messages */
    apiRetStatus = CyU3PUartInit();
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        /* Error handling */
        SDR_ErrorHandler(apiRetStatus);
    }

    /* Set UART configuration */
    CyU3PMemSet ((uint8_t *)&uartConfig, 0, sizeof (uartConfig));
    uartConfig.baudRate = CY_U3P_UART_BAUDRATE_115200;
    uartConfig.stopBit = CY_U3P_UART_ONE_STOP_BIT;
    uartConfig.parity = CY_U3P_UART_NO_PARITY;
    uartConfig.txEnable = CyTrue;
    uartConfig.rxEnable = CyFalse;
    uartConfig.flowCtrl = CyFalse;
    uartConfig.isDma = CyTrue;

    apiRetStatus = CyU3PUartSetConfig (&uartConfig, NULL);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        SDR_ErrorHandler(apiRetStatus);
    }

    /* Set the UART transfer to a really large value. */
    apiRetStatus = CyU3PUartTxSetBlockXfer (0xFFFFFFFF);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        SDR_ErrorHandler(apiRetStatus);
    }

    /* Initialize the debug module. */
    apiRetStatus = CyU3PDebugInit (CY_U3P_LPP_SOCKET_UART_CONS, 8);
    if (apiRetStatus != CY_U3P_SUCCESS)
    {
        SDR_ErrorHandler(apiRetStatus);
    }

    CyU3PDebugPreamble(CyFalse);
}

/* This function starts the application. This is called
 * when a SET_CONF event is received from the USB host. The endpoints
 * are configured and the DMA pipe is setup in this function. */
void
SDR_ApplnStart (
		void)
{
	uint16_t size = 0;
	CyU3PEpConfig_t epCfg;


	CyU3PDmaMultiChannelConfig_t dmaCfg;
	CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
	CyU3PUSBSpeed_t usbSpeed = CyU3PUsbGetSpeed();

	/* First identify the usb speed. Once that is identified,
	 * create a DMA channel and start the transfer on this. */

	/* Based on the Bus Speed configure the endpoint packet size */
	switch (usbSpeed)
	{
	case CY_U3P_FULL_SPEED:
		size = 64;
		break;

	case CY_U3P_HIGH_SPEED:
		size = 512;
		break;

	case CY_U3P_SUPER_SPEED:
		size = 1024;
		break;

	default:
		CyU3PDebugPrint (4, "Error! Invalid USB speed.\n");
		SDR_ErrorHandler (CY_U3P_ERROR_FAILURE);
		break;
	}

	CyU3PMemSet ((uint8_t *)&epCfg, 0, sizeof (epCfg));
	epCfg.enable = CyTrue;
	epCfg.epType = CY_U3P_USB_EP_BULK;
	epCfg.burstLen = (usbSpeed == CY_U3P_SUPER_SPEED) ?
			(CY_FX_EP_BURST_LENGTH) : 1;
	epCfg.streams = 0;
	epCfg.pcktSize = size;

	/* Consumer endpoint configuration */
	apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_CONSUMER, &epCfg);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "CyU3PSetEpConfig failed, Error code = %d\n", apiRetStatus);
		SDR_ErrorHandler (apiRetStatus);
	}

	/* Create a DMA MANUAL_IN channel for the producer socket. */
	CyU3PMemSet ((uint8_t *)&dmaCfg, 0, sizeof (dmaCfg));
	/* The buffer size will be same as packet size for the
	 * full speed, high speed and super speed non-burst modes.
	 * For super speed burst mode of operation, the buffers will be
	 * 1024 * burst length so that a full burst can be completed.
	 * This will mean that a buffer will be available only after it
	 * has been filled or when a short packet is received. */

	dmaCfg.size  = (size * CY_FX_EP_BURST_LENGTH );  // 16 * 1024   !!!!!!!!!!
	dmaCfg.count = CY_FX_BULKSRCSINK_DMA_BUF_COUNT;  // 6
	dmaCfg.validSckCount = 2;

	dmaCfg.dmaMode = CY_U3P_DMA_MODE_BYTE;  		// 2
	dmaCfg.notification = 0;
	dmaCfg.cb = NULL;
	dmaCfg.prodHeader = 0;
	dmaCfg.prodFooter = 0;
	dmaCfg.consHeader = 0;
	dmaCfg.prodAvailCount = 0;

	/* Create a DMA MANUAL_OUT channel for the consumer socket. */
	dmaCfg.prodSckId[0] = CY_U3P_PIB_SOCKET_0;
	dmaCfg.prodSckId[1] = CY_U3P_PIB_SOCKET_1;
	dmaCfg.consSckId[0] = CY_FX_EP_CONSUMER_SOCKET;

	apiRetStatus = CyU3PDmaMultiChannelCreate (&glChHandleBulkSrc, CY_U3P_DMA_TYPE_AUTO_MANY_TO_ONE , &dmaCfg);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "CyU3PDmaChannelCreate failed, Error code = %d\n", apiRetStatus);
		SDR_ErrorHandler(apiRetStatus);
	}

	/* Set DMA Channel transfer size */

	apiRetStatus = CyU3PDmaMultiChannelSetXfer (&glChHandleBulkSrc, CY_FX_BULKSRCSINK_DMA_TX_SIZE, 0);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "CyU3PDmaChannelSetXfer failed, Error code = %d\n", apiRetStatus);
		SDR_ErrorHandler(apiRetStatus);
	}

	/* Flush the endpoint memory */
		CyU3PUsbFlushEp(CY_FX_EP_CONSUMER);

	glIsApplnActive = CyTrue;
}

/* This function stops the application. This shall be called whenever a RESET
 * or DISCONNECT event is received from the USB host. The endpoints are
 * disabled and the DMA pipe is destroyed by this function. */
void
SDR_ApplnStop (
		void)
{
	CyU3PEpConfig_t epCfg;
	CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;

	/* Update the flag so that the application thread is notified of this. */
	glIsApplnActive = CyFalse;

	/* Disable endpoints. */
	CyU3PMemSet ((uint8_t *)&epCfg, 0, sizeof (epCfg));
	epCfg.enable = CyFalse;

	CyU3PDmaMultiChannelDestroy (&glChHandleBulkSrc);
	CyU3PUsbFlushEp(CY_FX_EP_CONSUMER);
	/* Consumer endpoint configuration. */
	apiRetStatus = CyU3PSetEpConfig(CY_FX_EP_CONSUMER, &epCfg);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "CyU3PSetEpConfig failed, Error code = %d\n", apiRetStatus);
		SDR_ErrorHandler (apiRetStatus);
	}

}

/* Callback to handle the USB setup requests. */
CyBool_t
SDR_ApplnUSBSetupCB (
		uint32_t setupdat0, /* SETUP Data 0 */
		uint32_t setupdat1  /* SETUP Data 1 */
)
{
	uint8_t  bRequest, bReqType;
	uint8_t  bType; // bTarget;
	uint16_t wValue;
	uint16_t wLength;
	uint16_t wIndex;
    outxio_t * pdata;
    pwmxio_t * ppwm;
    uint32_t value;
    CyU3PReturnStatus_t apiRetStatus = 0;
    CyBool_t isHandled = CyFalse;


	/* Decode the fields from the setup request. */
	bReqType = (setupdat0 & CY_U3P_USB_REQUEST_TYPE_MASK);
	bType    = (bReqType & CY_U3P_USB_TYPE_MASK);
//	bTarget  = (bReqType & CY_U3P_USB_TARGET_MASK);
	bRequest = ((setupdat0 & CY_U3P_USB_REQUEST_MASK) >> CY_U3P_USB_REQUEST_POS);
	wValue   = ((setupdat0 & CY_U3P_USB_VALUE_MASK)   >> CY_U3P_USB_VALUE_POS);
	wLength   = ((setupdat1 & CY_U3P_USB_LENGTH_MASK)   >> CY_U3P_USB_LENGTH_POS);
	wIndex   = ((setupdat1 & CY_U3P_USB_INDEX_MASK)   >> CY_U3P_USB_INDEX_POS);


	if (bType == CY_U3P_USB_VENDOR_RQT)
	{
		isHandled = CyTrue;
		switch (bRequest)
		{
			case CY_FX_RQT_ID_CHECK:
				CyU3PUsbSendEP0Data (8, (uint8_t *)glFirmwareID);
				break;

			case CY_FX_RQT_GPIO_WRITE:
				if(CyU3PUsbGetEP0Data(wLength, glEp0Buffer, NULL)== CY_U3P_SUCCESS)
				{
					pdata = (outxio_t *) &glEp0Buffer[1];
					CyU3PGpioSetValue (21, (pdata->buffer[0] & OUTXIO0) == OUTXIO0 );
					CyU3PGpioSetValue (22, (pdata->buffer[0] & OUTXIO1) == OUTXIO1 );
					CyU3PGpioSetValue (23, (pdata->buffer[0] & OUTXIO2) == OUTXIO2 );
					CyU3PGpioSetValue (26, (pdata->buffer[0] & OUTXIO3) == OUTXIO3 );
					CyU3PGpioSetValue (27, (pdata->buffer[0] & OUTXIO4) == OUTXIO4 );
					CyU3PGpioSetValue (28, (pdata->buffer[0] & OUTXIO5) == OUTXIO5 );
					CyU3PGpioSetValue (29, (pdata->buffer[0] & OUTXIO6) == OUTXIO6 );
				}
				break;

			case CY_FX_RQT_GPIO_PWM:
				if(CyU3PUsbGetEP0Data(wLength, glEp0Buffer, NULL)== CY_U3P_SUCCESS)
				{
					ppwm = (pwmxio_t *)&glEp0Buffer[1];
					value = ppwm->dutycicle;
					if (value >= CY_FX_PWM_PERIOD)
						value = CY_FX_PWM_PERIOD -1;
					apiRetStatus = ConfGPIOPWM(24,value);
					if (apiRetStatus != CY_U3P_SUCCESS)
					{
						isHandled = CyFalse;
						CyU3PDebugPrint (4, "\n CY_FX_RQT_GPIO_PWM failed, error code = %d\n",
								apiRetStatus);
						SDR_ErrorHandler(apiRetStatus);
					}
				}
				break;

			case CY_FX_RQT_I2C_WRITE:
				if(CyU3PUsbGetEP0Data(wLength, glEp0Buffer, NULL)== CY_U3P_SUCCESS)
				{
					apiRetStatus = CyFxUsbI2cTransfer ( wIndex, wValue, wLength, glEp0Buffer, CyFalse);
					if (apiRetStatus != CY_U3P_SUCCESS)
					{
						isHandled = CyFalse;
						CyU3PDebugPrint (4, "\n\rCY_FX_RQT_I2C_WRITE failed, error code = 0x%X\n",apiRetStatus);
					}
				}
				break;

			case CY_FX_RQT_I2C_READ:
				CyU3PMemSet (glEp0Buffer, 0, sizeof (glEp0Buffer));
				if (wLength >= 64) wLength = 64;
				apiRetStatus = CyFxUsbI2cTransfer (wIndex, wValue, wLength,glEp0Buffer, CyTrue);
				if (apiRetStatus == CY_U3P_SUCCESS)
				{
					apiRetStatus = CyU3PUsbSendEP0Data(wLength, glEp0Buffer);
				}
				if (apiRetStatus != CY_U3P_SUCCESS)
				{
					isHandled = CyFalse;
					CyU3PDebugPrint (4, "\n\rCY_FX_RQT_I2C_READ failed, error code = 0x%X\n",apiRetStatus);
				}
				break;

			default:
				/* This is unknown request. */
				isHandled = CyFalse;
				break;
		}
		return isHandled;
	}
	/* Fast enumeration is used. Only class, vendor and unknown requests
	 * are received by this function. These are not handled in this
	 * application. Hence return CyFalse. */
	return CyFalse;
}
/* This is a callback function to handle gpif events */
void
SDR_ApplnGPIFEventCB (
		CyU3PGpifEventType event,               /* Event type that is being notified. */
		uint8_t            currentState         /* Current state of the State Machine. */
)
{
	switch (event)
	{
	case CYU3P_GPIF_EVT_SM_INTERRUPT:
	{
		CyU3PDebugPrint (4, "\n\r GPIF overflow INT received\n");

	}
	break;

	default:
		break;
	}
}

/* This is the callback function to handle the USB events. */
void
SDR_ApplnUSBEventCB (
		CyU3PUsbEventType_t evtype, /* Event type */
		uint16_t            evdata  /* Event data */
)
{
	switch (evtype)
	{
	case CY_U3P_USB_EVENT_SETCONF:
		/* If the application is already active
		 * stop it before re-enabling. */
		if (glIsApplnActive)
		{
			SDR_ApplnStop ();
		}
		/* Start the source sink function. */
		SDR_ApplnStart ();
		break;

	case CY_U3P_USB_EVENT_RESET:
	case CY_U3P_USB_EVENT_DISCONNECT:
		/* Stop the source sink function. */
		if (glIsApplnActive)
		{
			SDR_ApplnStop ();
		}
		break;

	default:
		break;
	}
}

void SDR_StartGpif(void)
{
	CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
/* Start the state machine. */
	    	apiRetStatus = CyU3PGpifSMStart (RESET, ALPHA_RESET);
	    	if (apiRetStatus != CY_U3P_SUCCESS)
	    	{
	    		CyU3PDebugPrint (4, "\n\rCyU3PGpifSMStart failed, Error Code = %d\n",apiRetStatus);

	    	}
}

/* This function initializes the USB Module, sets the enumeration descriptors.
 * This function does not start the bulk streaming and this is done only when
 * SET_CONF event is received. */
void
SDR_ApplnInit (void)
{
	CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
	/**************************************GPIF****************************************************/
	CyU3PPibClock_t pibClock;

	/* Initialize the p-port block. */
	pibClock.clkDiv = 2;
	pibClock.clkSrc = CY_U3P_SYS_CLK;
	pibClock.isHalfDiv = CyFalse;
	/* Disable DLL for sync GPIF */
	pibClock.isDllEnable = CyFalse;
	apiRetStatus = CyU3PPibInit(CyTrue, &pibClock);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "P-port Initialization failed, Error Code = %d\n",apiRetStatus);
		SDR_ErrorHandler(apiRetStatus);
	}

	/* Load the GPIF configuration for Slave FIFO sync mode. */
	apiRetStatus = CyU3PGpifLoad (&CyFxGpifConfig);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "CyU3PGpifLoad failed, Error Code = %d\n",apiRetStatus);
		SDR_ErrorHandler(apiRetStatus);
	}


	CyU3PGpifRegisterCallback(SDR_ApplnGPIFEventCB);

	/**********************************************************************************************/
	/* Start the USB functionality. */
	apiRetStatus = CyU3PUsbStart();
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "CyU3PUsbStart failed to Start, Error code = %d\n", apiRetStatus);
		SDR_ErrorHandler(apiRetStatus);
	}

	/* The fast enumeration is the easiest way to setup a USB connection,
	 * where all enumeration phase is handled by the library. Only the
	 * class / vendor requests need to be handled by the application. */
	CyU3PUsbRegisterSetupCallback(SDR_ApplnUSBSetupCB, CyTrue);

	/* Setup the callback to handle the USB events. */
	CyU3PUsbRegisterEventCallback(SDR_ApplnUSBEventCB);

	/* Set the USB Enumeration descriptors */

	/* Super speed device descriptor. */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_DEVICE_DESCR, (int) NULL, (uint8_t *)CyFxUSB30DeviceDscr);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "USB set device descriptor failed, Error code = %d\n", apiRetStatus);
		SDR_ErrorHandler(apiRetStatus);
	}

	/* High speed device descriptor. */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_HS_DEVICE_DESCR,(int) NULL, (uint8_t *)CyFxUSB20DeviceDscr);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "USB set device descriptor failed, Error code = %d\n", apiRetStatus);
		SDR_ErrorHandler(apiRetStatus);
	}

	/* BOS descriptor */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_BOS_DESCR,(int)NULL, (uint8_t *)CyFxUSBBOSDscr);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "USB set configuration descriptor failed, Error code = %d\n", apiRetStatus);
		SDR_ErrorHandler(apiRetStatus);
	}

	/* Device qualifier descriptor */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_DEVQUAL_DESCR,(int) NULL, (uint8_t *)CyFxUSBDeviceQualDscr);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "USB set device qualifier descriptor failed, Error code = %d\n", apiRetStatus);
		SDR_ErrorHandler(apiRetStatus);
	}

	/* Super speed configuration descriptor */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_SS_CONFIG_DESCR,(int) NULL, (uint8_t *)CyFxUSBSSConfigDscr);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "USB set configuration descriptor failed, Error code = %d\n", apiRetStatus);
		SDR_ErrorHandler(apiRetStatus);
	}

	/* High speed configuration descriptor */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_HS_CONFIG_DESCR,(int) NULL, (uint8_t *)CyFxUSBHSConfigDscr);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "USB Set Other Speed Descriptor failed, Error Code = %d\n", apiRetStatus);
		SDR_ErrorHandler(apiRetStatus);
	}

	/* Full speed configuration descriptor */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_FS_CONFIG_DESCR,(int) NULL, (uint8_t *)CyFxUSBFSConfigDscr);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "USB Set Configuration Descriptor failed, Error Code = %d\n", apiRetStatus);
		SDR_ErrorHandler(apiRetStatus);
	}

	/* String descriptor 0 */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 0, (uint8_t *)CyFxUSBStringLangIDDscr);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "USB set string descriptor failed, Error code = %d\n", apiRetStatus);
		SDR_ErrorHandler(apiRetStatus);
	}

	/* String descriptor 1 */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 1, (uint8_t *)CyFxUSBManufactureDscr);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "USB set string descriptor failed, Error code = %d\n", apiRetStatus);
		SDR_ErrorHandler(apiRetStatus);
	}

	/* String descriptor 2 */
	apiRetStatus = CyU3PUsbSetDesc(CY_U3P_USB_SET_STRING_DESCR, 2, (uint8_t *)CyFxUSBProductDscr);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "USB set string descriptor failed, Error code = %d\n", apiRetStatus);
		SDR_ErrorHandler(apiRetStatus);
	}

	/* Connect the USB Pins with super speed operation enabled. */
	apiRetStatus = CyU3PConnectState(CyTrue, CyTrue);
	if (apiRetStatus != CY_U3P_SUCCESS)
	{
		CyU3PDebugPrint (4, "USB Connect failed, Error code = %d\n", apiRetStatus);
		SDR_ErrorHandler(apiRetStatus);
	}


}

/* Entry function for the SDR_AppThread. */
void
SDR_AppThread_Entry (
		uint32_t input)
{
	CyU3PReturnStatus_t apiRetStatus = CY_U3P_SUCCESS;
	/* Initialize the debug module */
	SDR_ApplnDebugInit();

	/* Initialize GPIO IO */
	SDR_GpioInit();

    apiRetStatus = CyFxI2cInit ();
	if (apiRetStatus != CY_U3P_SUCCESS)
		 {
		   // Error handling
		   CyU3PDebugPrint (4, "\n\r Error CyFxI2cInit() = 0x%X\n",apiRetStatus);
		 }

	CyU3PDebugPrint (4, "\n\r Init IOs");

	/* Initialize the application */
	SDR_ApplnInit();

//	SDR_ErrorHandler(0xca);

	for (;;)
	{
		CyU3PThreadSleep (2000);

		if (glIsApplnActive)
		{
			if(!glStartSDRGpif)
			{
				glStartSDRGpif = CyTrue;
				SDR_StartGpif();
			}
		}
        /* Allow other ready threads to run. */
   //     CyU3PThreadRelinquish ();
	}
}

/* Application define function which creates the threads. */
void
CyFxApplicationDefine (
		void)
{
	void *ptr = NULL;
	uint32_t retThrdCreate = CY_U3P_SUCCESS;

	/* Allocate the memory for the threads */
	ptr = CyU3PMemAlloc (CY_FX_BULKSRCSINK_THREAD_STACK);

	/* Create the thread for the application */
	retThrdCreate = CyU3PThreadCreate (&SDR_AppThread,      /* App thread structure */
			"21:Bulk_src_sink",                      /* Thread ID and thread name */
			SDR_AppThread_Entry,              /* App thread entry function */
			0,                                       /* No input parameter to thread */
			ptr,                                     /* Pointer to the allocated thread stack */
			CY_FX_BULKSRCSINK_THREAD_STACK,          /* App thread stack size */
			CY_FX_BULKSRCSINK_THREAD_PRIORITY,       /* App thread priority */
			CY_FX_BULKSRCSINK_THREAD_PRIORITY,       /* App thread priority */
			CYU3P_NO_TIME_SLICE,                     /* No time slice for the application thread */
			CYU3P_AUTO_START                         /* Start the thread immediately */
	);

	/* Check the return code */
	if (retThrdCreate != 0)
	{
		/* Thread Creation failed with the error code retThrdCreate */

		/* Add custom recovery or debug actions here */

		/* Application cannot continue */
		/* Loop indefinitely */
		while(1);
	}
}

/*
 * Main function
 */
int
main (void)
{
	CyU3PIoMatrixConfig_t io_cfg;
	CyU3PReturnStatus_t status = CY_U3P_SUCCESS;

	/* Initialize the device */
	status = CyU3PDeviceInit (NULL);
	if (status != CY_U3P_SUCCESS)
	{
		goto handle_fatal_error;
	}

	/* Initialize the caches. Enable instruction cache and keep data cache disabled.
	 * The data cache is useful only when there is a large amount of CPU based memory
	 * accesses. When used in simple cases, it can decrease performance due to large
	 * number of cache flushes and cleans and also it adds to the complexity of the
	 * code. */
	status = CyU3PDeviceCacheControl (CyTrue, CyFalse, CyFalse);
	if (status != CY_U3P_SUCCESS)
	{
		goto handle_fatal_error;
	}

	/* Configure the IO matrix for the device. On the FX3 DVK board, the COM port
	 * is connected to the IO(53:56). This means that either DQ32 mode should be
	 * selected or lppMode should be set to UART_ONLY. Here we are choosing
	 * UART_ONLY configuration. */
	io_cfg.isDQ32Bit = CyTrue;
	io_cfg.useUart   = CyTrue;
#ifdef I2C_ACTIVE
	io_cfg.useI2C    = CyTrue;
#else
	io_cfg.useI2C    = CyFalse;
#endif
	io_cfg.useI2S    = CyFalse;
	io_cfg.useSpi    = CyFalse;
	io_cfg.lppMode   = CY_U3P_IO_MATRIX_LPP_DEFAULT;
	/* No GPIOs are enabled. */
	io_cfg.gpioSimpleEn[0]  = 0;
	io_cfg.gpioSimpleEn[1]  = 0;
    /* GPIOs 50 is used as complex GPIO. */
    io_cfg.gpioComplexEn[0] = 0x01000000;  // GPIO24 PWM
    io_cfg.gpioComplexEn[1] = 0x00000000;

	status = CyU3PDeviceConfigureIOMatrix (&io_cfg);
	if (status != CY_U3P_SUCCESS)
	{
		goto handle_fatal_error;
	}

	/* This is a non returnable call for initializing the RTOS kernel */
	CyU3PKernelEntry ();

	/* Dummy return to make the compiler happy */
	return 0;

	handle_fatal_error:

	/* Cannot recover from this error. */
	while (1);
}

/* [ ] */

