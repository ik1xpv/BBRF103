
#define EXTIO_EXPORTS		1
#define HWNAME				"SDDC-0.80"
#define HWMODEL				"BBRF103 ik1xpv"
#define SETTINGS_IDENTIFIER	"SDDC-1.0"

#define LO_MIN				0LL
#define LO_MAX				2000000000LL
#define HF_HIGH             32000000L
#define LO_PRECISION		25000L
// EXT_BLOCKLEN   short  samplePCM[EXT_BLOCKLEN * 2];    charlen  EXT_BLOCKLEN * 2 * 2 
#define EXT_BLOCKLEN		(1024*16*2)			/* complex sample len  only multiples of 512 */
#define EXT_BLOCKLEN_UCHAR	(EXT_BLOCKLEN * 4)	/* 2 short * 2 complex */
//#define BORLAND				0

#define QSIZE (16)


#include "ExtIO_sddc.h"
//---------------------------------------------------------------------------
// #define WIN32_LEAN_AND_MEAN             // Selten verwendete Teile der Windows-Header nicht einbinden.
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include <windows.h>
#include <string.h>
#include <stdio.h>
#include <math.h> 
#include <wtypes.h>							// used by CyAPI
#include "CyAPI.h"							// Cypress FX3 API
#include "i2c_si5351.h"
#include "i2c_r820t2.h"
#include "rfddc.h"
//---------------------------------------------------------------------------


static bool SDR_supports_settings = false;  // assume not supported
static bool SDR_settings_valid = false;		// assume settings are for some other ExtIO

static char SDR_progname[32+1] = "\0";		// HDSDR
static int  SDR_ver_major = -1;				// 2
static int  SDR_ver_minor = -1;				// 76

static unsigned	gCustomSamplerate = 8000000;
static int		gHwType = exthwUSBfloat32;

static int		giExtSrateIdx = 0;
static unsigned gExtSampleRate = 8000000;


volatile int64_t  glLOfreq = 8000000L;   // set 8Msps default
volatile uint32_t gsmpfreq = FRQSMP/2;
volatile uint32_t gADCfreq = 0;
volatile uint32_t gR820Tref = 0;


bool	gbInitHW = false;
int		giAttIdx = 0;
int		giAttIdxHF = 0;
int		giAttIdxVHF = 0;
int		giDefaultAttIdx = 0;	// 0 dB
int		giMgcIdx = 0;
int		giDefaultMgcIdx = 0;	// 0 dB
int		giAgcIdx = 0;
int		giDefaultAgcIdx = 1;	// Auto
int		giMixGainIdx = 0;
int		giVGAGainIdx = 0;
int		giDefaultMixGainIdx = 0;	// Default IF gain 0
int		giDefaultVGAGainIdx = 0;    // Default VGA gain 0
int		giWhatIdx = 0;

bool    gbsettings = false;

pfnExtIOCallback	pfnCallback = 0;

std::atomic<bool> 	gbExitThread (false);
std::atomic<bool> 	gbThreadRunning (false);

bool gR820Ton = false;

static CCyUSBDevice			*USBDevice;
static HANDLE				DeviceHandle;
static bool					bHighSpeedDevice;
static bool					bSuperSpeedDevice;
static const int			MAX_QUEUE_SZ = 64;
static int					QueueSize;
static int					PPX;
static CCyUSBEndPoint		*EndPt;
static UCHAR                eptAddr;
static int					TimeOut;
static bool					bStreaming;



static UCHAR mybuffers[QSIZE][ 1024*16*64 ];
PUCHAR  buffers[QSIZE];
PUCHAR	contexts[QSIZE];

// ventor sequence cmds
#define OUTXIO (0)
#define I2CXIO (1)
#define PWMXIO (2)

//Target GPIO
#define LED_OVERLOAD (1)// LED_OVERLOAD  GPIO21  bit position
#define LED_MODEA (2) 	// LED_MODEA  GPIO22
#define LED_MODEB (4) 	// LED_MODEB  GPIO22
#define SEL0 (8)  		// SEL0  GPIO26
#define SEL1 (16) 		// SEL1  GPIO27
#define SHDWN (32)  	// SHDWN  GPIO28
#define DITH (64)		// DITH   GPIO29

#define CY_FX_PWM_PERIOD                 (201600 - 1)   /* PWM time period. */


static bool	gbmode;  // true = HF, false = VHF  

static UINT8 ggpioo;
static bool gled_overload;
static bool gled_modea;
static bool gled_modeb;
static bool gsel0;
static bool gsel1;
static bool gshdwn;
static bool gdith;



rfddc* psdrddc;
//---------------------------------------------------------------------------

	bool Ep0VendorCommand(vendorCmdData cmdData)
	{
		if (USBDevice != NULL)
		{
			USBDevice->ControlEndPt->Target = TGT_DEVICE;
			USBDevice->ControlEndPt->ReqType = REQ_VENDOR;
			USBDevice->ControlEndPt->ReqCode = cmdData.opCode;
			USBDevice->ControlEndPt->Direction = (cmdData.isRead) ? DIR_FROM_DEVICE : DIR_TO_DEVICE;

			USBDevice->ControlEndPt->Value = (cmdData.addr & 0xFFFF);
			USBDevice->ControlEndPt->Index = ((cmdData.addr >> 16) & 0xFFFF);

			int maxpkt = USBDevice->ControlEndPt->MaxPktSize;

			long len = cmdData.size;

			/* Handle the case where transfer length is 0 (used to send the Program Entry) */
			if (cmdData.size == 0)
				return USBDevice->ControlEndPt->XferData(cmdData.buf, len, NULL);
			else
			{
				bool bRetCode = false;
				long Stagelen = 0;
				int BufIndex = 0;
				while (len > 0)
				{
					if (len >= 65535)
						Stagelen = 65535;
					else
						Stagelen = (len) % 65535;

					/* Allocate the buffer */
					PUCHAR StageBuf = new UCHAR[Stagelen];

					if (!cmdData.isRead)
					{
						/*write operation */
						for (int i = 0; i < Stagelen; i++)
							StageBuf[i] = cmdData.buf[BufIndex + i];
					}
					else
					{
						bRetCode = false;
					}

					bRetCode = USBDevice->ControlEndPt->XferData(StageBuf, Stagelen, NULL);
					if (!bRetCode)
					{
						if (StageBuf)
							delete[] StageBuf;

						return false;
					}

					if (cmdData.isRead)
					{
						/*read operation */
						for (int i = 0; i < Stagelen; i++)
							cmdData.buf[BufIndex + i] = StageBuf[i];
					}

					if (StageBuf)
						delete[] StageBuf;

					len -= Stagelen;
					BufIndex += Stagelen;
				}

			}

			return true;
		}
		return false;
	}

	/* Function to transmit data on the control endpoint. */
bool DownloadBufferToDevice(UINT start_addr, USHORT count, UCHAR *data_buf, UCHAR opCode)
	{
		vendorCmdData cmdData;
		cmdData.addr = start_addr;
		cmdData.isRead = 0;
		cmdData.buf = data_buf;
		cmdData.opCode = opCode;
		cmdData.size = count;
		return Ep0VendorCommand(cmdData);
	}
/* Function to receive data on the control endpoint. */

bool UploadBufferFromDevice(UINT start_addr, USHORT count, UCHAR *data_buf, UCHAR opCode)
	{
		vendorCmdData cmdData;
		cmdData.addr = start_addr;
		cmdData.isRead = 1;
		cmdData.buf = data_buf;
		cmdData.opCode = opCode;
		cmdData.size = count;
		return Ep0VendorCommand(cmdData);
	}

bool UpdateGPIO()
{
	UINT8 ddata[2];
	ddata[1] = (gled_overload * LED_OVERLOAD) |
		(gled_modea * LED_MODEA) |
		(gled_modeb * LED_MODEB) |
		(gsel0 * SEL0) |
		(gsel1 * SEL1) |
		(gshdwn * SHDWN) |
		(gdith * DITH);
	ggpioo = ddata[1];
	ddata[0] = OUTXIO;
	return DownloadBufferToDevice(0, 2, ddata, CY_FX_RQT_GPIO_WRITE);
}

bool UpdateAGCPWM(double zero_one )
{
	UINT8 ddata[5];
	INT32 dutycycle;
	ddata[0] = PWMXIO;
	dutycycle = (INT32)((double)CY_FX_PWM_PERIOD * ( zero_one));
	if (dutycycle <= 5) 
		dutycycle = 5;
	if (dutycycle > CY_FX_PWM_PERIOD) 
		dutycycle = CY_FX_PWM_PERIOD-1;
	*((UINT32*)&ddata[1]) = dutycycle;
	return DownloadBufferToDevice(0, 5, ddata, CY_FX_RQT_GPIO_PWM);
}

bool SendI2cbyte(UINT8 i2caddr, UINT8 regaddr, UINT8 data)
{
	return SendI2cbytes(i2caddr, regaddr, &data, 1);
}

bool SendI2cbytes(UINT8 i2caddr, UINT8 regaddr, UINT8 * pdata, UINT8 len)
{
	return DownloadBufferToDevice( i2caddr + (regaddr << 16), len , pdata, CY_FX_RQT_I2C_WRITE);
}

bool ReadI2cbytes(UINT8 i2caddr, UINT8 regaddr, UINT8 * pdata, UINT8 len)
{
	return UploadBufferFromDevice(i2caddr + (regaddr << 16), len, pdata, CY_FX_RQT_I2C_READ);
}

//---------------------------------------------------------------------------
static void AbortXferLoop(int pending, PUCHAR *buffers,  PUCHAR *contexts, OVERLAPPED inOvLap[])
{
	//EndPt->Abort(); - This is disabled to make sure that while application is doing IO and user unplug the device, this function hang the app.
	long len = EndPt->MaxPktSize * PPX;
	EndPt->Abort();
	for (int j = 0; j< QueueSize; j++)
	{
		if (j<pending)
		{
			EndPt->WaitForXfer(&inOvLap[j], TimeOut);
			/*{
				EndPt->Abort();
				if (EndPt->LastError == ERROR_IO_PENDING)
				WaitForSingleObject(inOvLap[j].hEvent,2000);
			}*/
			EndPt->FinishDataXfer(buffers[j], len, &inOvLap[j], contexts[j]);
		}
		CloseHandle(inOvLap[j].hEvent);
	}

	bStreaming = false;
	DbgPrintf("\nAbortXferLoop()\n");
}



DWORD WINAPI USBThreadProc(__in  LPVOID lpParameter)
{
	int i, ii,iii;
	unsigned long Successes = 0;
	unsigned long Failures = 0;

	DbgPrintf("\n\nUSBThreadProc start");

	isdying = false;	// must be false before rfdcc creation
	dataready0 = false;
	dataready1 = false;
	rfddc sdrddc;

	OVERLAPPED		inOvLap[MAX_QUEUE_SZ];

	long len = EndPt->MaxPktSize * PPX; // Each xfer request will get PPX packets
	if (len != EXT_BLOCKLEN_UCHAR)
	{
		DbgPrintf("\n\nERROR\tlen = EndPt->MaxPktSize * PPX = %d\n\texpected %d\n", len, EXT_BLOCKLEN_UCHAR);
		return -1 ;
	}
	EndPt->SetXferSize(len);

	// Allocate all the buffers for the queues
	for (i = 0; i < QueueSize; i++)
	{
		buffers[i] = &mybuffers[i][0];
		inOvLap[i].hEvent = CreateEvent(NULL, false, false, NULL);
		memset(buffers[i], 0x00, len);  // init buffer for debug
	}

	// Queue-up the first batch of transfer requests
	for (i = 0; i < QueueSize; i++)
	{
		contexts[i] = EndPt->BeginDataXfer(buffers[i], len, &inOvLap[i]);
		if (EndPt->NtStatus || EndPt->UsbdStatus) // BeginDataXfer failed
		{
			DbgPrintf("Xfer request rejected. NTSTATUS = %04x", EndPt->NtStatus);
			AbortXferLoop(i + 1, buffers, contexts, inOvLap);
			return -1;
		}
	}
#ifdef NO_TRACE
	DbgPrintf("\n\nAllocated %d queques of %ld IQ samples short", QueueSize, len / (sizeof(short) * 2));
	DbgPrintf("\nSample per sec = %10.1f ", (double)gExtSampleRate);
	DbgPrintf("\nLatency time = %f \n\n", (((double)QueueSize* len) / (4.0*gExtSampleRate)));
#endif
	unsigned long generatorCount = 0;

	sdrddc.initp();

	ii = 0; iii = 0;

	while (!gbExitThread)
	{
		/*
		if (giParameterUsed != giParameterSetNo)
		{
			++giParameterUsed;
			LocalSampleRate = gExtSampleRate;
		}
		*/
		//  get buffers

		long rLen = len;	// Reset this each time through because
							// FinishDataXfer may modify it
	
		if (!EndPt->WaitForXfer(&inOvLap[ii], TimeOut))
		{
			EndPt->Abort();
			if (EndPt->LastError == ERROR_IO_PENDING)
				WaitForSingleObject(inOvLap[ii].hEvent, TimeOut*2);
		}
		if (EndPt->Attributes == 2) // Bulk Endpoint
		{
			if (EndPt->FinishDataXfer(buffers[ii], rLen, &inOvLap[ii], contexts[ii]))
			{
#ifdef _DEBUG
				Successes++;
#endif
				pfnCallback((IFLEN), 0, 0.0F, &outtime[0][0][0]); // previous output frame 
				sdrddc.arun(&((short *)buffers[ii])[0]);		  // input buffers pointer
			}
#ifdef _DEBUG
			else
				Failures++;
#endif
		}

		// Re-submit this queue element to keep the queue full
		contexts[ii] = EndPt->BeginDataXfer(buffers[ii], len, &inOvLap[ii]);

		if (EndPt->NtStatus || EndPt->UsbdStatus) // BeginDataXfer failed
		{
			DbgPrintf("Xfer request rejected. NTSTATUS = %04x", EndPt->NtStatus);
			AbortXferLoop(QueueSize, buffers, contexts, inOvLap);
			return  -1;
		}

		if (++ii == QueueSize) 
		{
			ii = 0;
			iii++;

#ifdef _DEBUG			
			if ((Failures > 0)&&((iii & 0x3fff )==0))//Only update the display once each time through the Queue
			{
				DbgPrintf("\nxfers ok %d  failed %d  \n", Successes, Failures);
				Successes = Failures = 0;
			}
#endif			
		}
	
	}
	// Memory clean-up
	AbortXferLoop(QueueSize, buffers, contexts, inOvLap);
	gbThreadRunning = false;
	return 0;
}


static void stopThread()
{
	if ( gbThreadRunning )
	{
		gbExitThread = true;
		while ( gbThreadRunning )
		{
			SleepEx( 10, FALSE );
		}
		DbgPrintf("\nStop running thread");
	}
}

static void startThread()
{
	gbExitThread = false;
	gbThreadRunning = true;
	HANDLE ghx =CreateThread( NULL	// LPSECURITY_ATTRIBUTES lpThreadAttributes
		, (SIZE_T)(64 * 1024 * 64)	// SIZE_T dwStackSize
		, USBThreadProc	// LPTHREAD_START_ROUTINE lpStartAddress
		, NULL					// LPVOID lpParameter
		, 0						// DWORD dwCreationFlags
		, NULL					// LPDWORD lpThreadId
		);
	DbgPrintf("\nStartThread Handle %x", ghx);
}


//---------------------------------------------------------------------------
HMODULE hInst;

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
					 )
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		hInst = hModule;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

//---------------------------------------------------------------------------
extern "C"
bool __declspec(dllexport) __stdcall InitHW(char *name, char *model, int& type)
{
	
	type = gHwType;
	strcpy(name, HWNAME);
	strcpy(model, HWMODEL);
	if (!gbInitHW)
	{
		// do initialization
#ifdef _MYDEBUG
		if (AllocConsole())
		{
			FILE* f;
			freopen_s(&f, "CONOUT$", "wt", stdout);
			SetConsoleTitle(TEXT("Debug Black Box Console ExtIO_sspeed " VERNUM));
			CONSOLE_FONT_INFOEX font;
			GetCurrentConsoleFontEx(GetStdHandle(STD_OUTPUT_HANDLE), false, &font);
			font.dwFontSize.X = 10;
			font.dwFontSize.Y = 16;
			SetCurrentConsoleFontEx(GetStdHandle(STD_OUTPUT_HANDLE), false, &font);
			SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), FOREGROUND_GREEN | FOREGROUND_INTENSITY);
			SetWindowPos(GetConsoleWindow(), HWND_TOPMOST, 0, 0, 640, 420, SWP_SHOWWINDOW);
			DbgPrintf("Oscar Steila IK1XPV fecit MMXVII\n");
		}
#endif

		glLOfreq = 8000000L;	// just a default value
		// .......... init here the hardware controlled by the DLL

		QueueSize = QSIZE;
		PPX = 16;
		TimeOut = 1000;
		bStreaming = false;

		//		init GPIO status
		gled_overload = 0;
		gled_modea = 1;
		gled_modeb = 1;
		gsel0 = 1;
		gsel1 = 0;
		gshdwn = 0;
		gdith = 0;
		USBDevice = new CCyUSBDevice(&DeviceHandle, CYUSBDRV_GUID, true);

		if (USBDevice == NULL) {
			DbgPrintf("\nHardware %s %s not ready \nExtIO_dll is not active", name, model);
			return false;
		}

		int n = USBDevice->DeviceCount();
		DbgPrintf("\nDevice count = %d",n);
		int idx = 0;
		for (int i = 0; i < n; i++)
		{
			USBDevice->Open(i);
			DbgPrintf("\n( %04X %04X ) %s ", USBDevice->VendorID, USBDevice->ProductID, USBDevice->FriendlyName);
			if (strncmp(USBDevice->FriendlyName,"Cypress FX3 USB StreamerExample Device", 38) == 0)
				idx = i;
		}
		if (n > 0) {
			DbgPrintf("\nDevice n  = %d\n",idx);
			USBDevice->Open(idx);
		}

		//if ((USBDevice->VendorID == VENDOR_ID) && (USBDevice->ProductID == PRODUCT_ID)) 
		{
			int interfaces = USBDevice->AltIntfcCount() + 1;
			bHighSpeedDevice = USBDevice->bHighSpeed;
			bSuperSpeedDevice = USBDevice->bSuperSpeed;

			for (int i = 0; i < interfaces; i++)
			{
				if (USBDevice->SetAltIntfc(i) == true)
				{
					int eptCnt = USBDevice->EndPointCount();
					// Fill the EndPointsBox
					for (int e = 1; e < eptCnt; e++)
					{
						CCyUSBEndPoint *ept = USBDevice->EndPoints[e];
						// INTR, BULK and ISO endpoints are supported.
						if ((ept->Attributes >= 1) && (ept->Attributes <= 3))
						{
							DbgPrintf("\n");
							DbgPrintf((ept->Attributes == 1) ? "ISOC " :
								((ept->Attributes == 2) ? "BULK " : "INTR "));
							DbgPrintf((ept->bIn) ? "IN, " : "OUT, ");
							DbgPrintf("%d Bytes,", ept->MaxPktSize);
							if (USBDevice->BcdUSB == USB30MAJORVER)
							{
								DbgPrintf("%d MaxBurst,", ept->ssmaxburst);
							}
							DbgPrintf("  ( %d  - 0x%02X )", i, ept->Address);
							eptAddr = ept->Address;
						}
					}
				}
			}
		}

		EndPt = USBDevice->EndPointOf(eptAddr);
		//	USBDevice->Close();

		// ......... init here the DLL graphical interface, if any

		giAttIdx = giDefaultAttIdx;
//		giAttIdxHF = giAttIdx;
		giMgcIdx = giDefaultMgcIdx;
		giAgcIdx = giDefaultAgcIdx;
		giMixGainIdx = giDefaultMixGainIdx;
		giVGAGainIdx = giDefaultVGAGainIdx;

		if (EndPt == nullptr)
		{
			DbgPrintf("\nBBRF103 not found.\nPlease verify connection.\n"); 
			gbInitHW = false;
		}
		else
		{   
			long len = EndPt->MaxPktSize * PPX; // Each xfer request will get PPX packets
			if (len != EXT_BLOCKLEN_UCHAR)
			{
				DbgPrintf("\n\nERROR\tlen = EndPt->MaxPktSize * PPX = %d\n\texpected %d\n", len, EXT_BLOCKLEN_UCHAR);
				gbInitHW = false;
			}
			else
			{
				// init BBRF103
				UpdateGPIO();
				UpdateAGCPWM(0.4F); //eee
				Si5351_init();
				gADCfreq = gsmpfreq;
				si5351aSetFrequency(gsmpfreq, gR820Tref);
				R820T2_set_stdby();
 	//			R820T2_init();
				gbInitHW = true;
			}
		}
	}

	return gbInitHW;
}

//---------------------------------------------------------------------------
extern "C"
bool EXTIO_API OpenHW(void)
{
	// .... display here the DLL panel ,if any....
	// .....if no graphical interface, delete the following statement
	//::SetWindowPos(F->handle, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

	if (pfnCallback)
	{
		pfnCallback(-1, extHw_Changed_ATT, 0.0F, 0);
	}
	// in the above statement, F->handle is the window handle of the panel displayed 
	// by the DLL, if such a panel exists
	return gbInitHW;
}

//---------------------------------------------------------------------------
extern "C"
int  EXTIO_API StartHW(long LOfreq)
{
	int64_t ret = StartHW64( (int64_t)LOfreq );
	return (int)ret;
}

//---------------------------------------------------------------------------
extern "C"
int64_t EXTIO_API StartHW64(int64_t LOfreq)
{
	if (!gbInitHW)
		return 0;
	if (EndPt == nullptr)
		return 0;
	stopThread();
	if (gbsettings == false)
	{
		LOfreq = glLOfreq;
		SetHWLO64(LOfreq);
		pfnCallback(-1, extHw_Changed_LO, 0.0F, 0);
	}
	else
	{
		SetHWLO64(LOfreq);
	}
	startThread();

	// number of complex elements returned each
	// invocation of the callback routine
	return (IFLEN);
}

//---------------------------------------------------------------------------
extern "C"
void EXTIO_API StopHW(void)
{
	stopThread();
	return;  // nothing to do with this specific HW
}

//---------------------------------------------------------------------------
extern "C"
void EXTIO_API CloseHW(void)
{
	// ..... here you can shutdown your graphical interface, if any............
	if (gbInitHW )
	{
		/* close port */
	}
	gbInitHW = false;
}

//---------------------------------------------------------------------------
extern "C"
int  EXTIO_API SetHWLO(long LOfreq)
{
	int64_t ret = SetHWLO64( (int64_t)LOfreq );
	return (ret & 0xFFFFFFFF);
}

extern "C"
int64_t EXTIO_API SetHWLO64(int64_t LOfreq)
{
	// ..... set here the LO frequency in the controlled hardware
	// Set here the frequency of the controlled hardware to LOfreq
    const int64_t wishedLO = LOfreq;
	int64_t ret = 0;

	int64_t loprecision;
	loprecision = FRQSMP / gsmpfreq;
	
	// calculate nearest possible frequency
	// - emulate receiver which don't have 1 Hz resolution
	LOfreq += loprecision/2 ;
	LOfreq /= loprecision;
	LOfreq *= loprecision;

	// same LO - but user wanted change?
	if ( LOfreq == glLOfreq )
	{
		if ( wishedLO < glLOfreq )
			LOfreq -= loprecision;
		else if ( wishedLO > glLOfreq )
			LOfreq += loprecision;
	}

	// check limits
	if ( LOfreq < LO_MIN )
	{
		LOfreq = LO_MIN;
		ret = -LO_MIN;
	}
	else if ( LOfreq > LO_MAX )
	{
		LOfreq = LO_MAX;
		ret = LO_MAX;
	}

	// take frequency
	glLOfreq = LOfreq;

	if ( gbInitHW )
	{
		// tune to that frequency
		// @TODO: recalc / modify carrier frequencies???
		// int64_t err = wishedLO - glLOfreq;

	}

	if ( (wishedLO != LOfreq)  &&  pfnCallback )
		pfnCallback( -1, extHw_Changed_LO, 0.0F, 0 );
	if (LOfreq < HF_HIGH)
	{
		gtunebin = (int)((LOfreq*FFTN) / gsmpfreq);
		giAttIdx = giAttIdxHF;
		if ((gR820Ton == true) && (pfnCallback))
		{
			gR820Ton = false;
	DbgPrintf("\nHF mode :");
			gR820Tref = 0;
			SetAttenuator(giAttIdxHF);

			R820T2_set_freq(200000000);
			si5351aSetFrequency(gsmpfreq, gR820Tref);					  // no R820T clk
			gADCfreq = gsmpfreq;
			R820T2_set_stdby();

			pfnCallback(-1, extHw_Changed_SampleRate, 0.0F, 0);   // renew sample rate
			pfnCallback(-1, extHw_Changed_RF_IF, 0.0F, 0);        // renew RF LNA ATT
		
		}
	}
	else
	{  // R820T active 
		gtunebin = (int)(((int64_t)IFR820T*FFTN) / gsmpfreq ); // IF R820T
		giAttIdx = giAttIdxVHF;
		if ((gR820Ton == false) && (pfnCallback)) // setup
		{
			gR820Ton = true;
			gR820Tref = R820T_FREQ;
			gsel0 = 0; // tuner input
			gsel1 = 0;
			UpdateGPIO();
		DbgPrintf("\nVHF mode:");
			si5351aSetFrequency(gsmpfreq, gR820Tref);
			gADCfreq = gsmpfreq;
			R820T2_init();
			R820T2_set_freq(LOfreq);

			SetAttenuator(giAttIdxVHF);
			R820T2_set_mixer_gain(giMgcIdx);

			gtunebin = (int)(((int64_t)IFR820T*FFTN) / gsmpfreq); // new IF bin R820T
			pfnCallback(-1, extHw_Changed_SampleRate, 0.0F, 0);   // renew sample rate
			pfnCallback(-1, extHw_Changed_RF_IF, 0.0F, 0);        // renew RF LNA ATT
	
		}
		R820T2_set_freq(LOfreq); // tuning
	}
	// 0 The function did complete without errors.
	// < 0 (a negative number N)
	//     The specified frequency  is  lower than the minimum that the hardware  is capable to generate.
	//     The absolute value of N indicates what is the minimum supported by the HW.
	// > 0 (a positive number N) The specified frequency is greater than the maximum that the hardware
	//     is capable to generate.
	//     The value of N indicates what is the maximum supported by the HW.
	return ret;
}

//---------------------------------------------------------------------------
extern "C"
int  EXTIO_API GetStatus(void)
{
	return 0;  // status not supported by this specific HW,
}

//---------------------------------------------------------------------------
extern "C"
void EXTIO_API SetCallback( pfnExtIOCallback funcptr ) 
{
	pfnCallback = funcptr;
	return;
}


//---------------------------------------------------------------------------
extern "C"
long EXTIO_API GetHWLO(void)
{
	return (long)( glLOfreq & 0xFFFFFFFF );
}

extern "C"
int64_t EXTIO_API GetHWLO64(void)
{
	return glLOfreq;
}

//---------------------------------------------------------------------------
extern "C"
long EXTIO_API GetHWSR(void)
{
	return gExtSampleRate;
}


//---------------------------------------------------------------------------

// extern "C" long EXTIO_API GetTune(void);
// extern "C" void EXTIO_API GetFilters(int& loCut, int& hiCut, int& pitch);
// extern "C" char EXTIO_API GetMode(void);
// extern "C" void EXTIO_API ModeChanged(char mode);
// extern "C" void EXTIO_API IFLimitsChanged(long low, long high);
// extern "C" void EXTIO_API TuneChanged(long freq);

// extern "C" void    EXTIO_API TuneChanged64(int64_t freq);
// extern "C" int64_t EXTIO_API GetTune64(void);
// extern "C" void    EXTIO_API IFLimitsChanged64(int64_t low, int64_t high);

//---------------------------------------------------------------------------

// extern "C" void EXTIO_API RawDataReady(long samprate, int *Ldata, int *Rdata, int numsamples)

//---------------------------------------------------------------------------
extern "C"
void EXTIO_API VersionInfo(const char * progname, int ver_major, int ver_minor)
{
  SDR_progname[0] = 0;
  SDR_ver_major = -1;
  SDR_ver_minor = -1;

  if ( progname )
  {
    strncpy( SDR_progname, progname, sizeof(SDR_progname) -1 );
    SDR_ver_major = ver_major;
    SDR_ver_minor = ver_minor;

	// possibility to check program's capabilities
	// depending on SDR program name and version,
	// f.e. if specific extHWstatusT enums are supported
  }
}

//---------------------------------------------------------------------------

// following "Attenuator"s visible on "RF" button
int  GetAttenuatorsHF(int atten_idx, float * attenuation)
{
	// fill in attenuation
	// use positive attenuation levels if signal is amplified (LNA)
	// use negative attenuation levels if signal is attenuated
	// sort by attenuation: use idx 0 for highest attenuation / most damping
	// this functions is called with incrementing idx
	//    - until this functions return != 0 for no more attenuator setting
	
		switch (atten_idx)
		{
		case 0:		*attenuation = -20.0F;	return 0;
		case 1:		*attenuation = -10.0F;	return 0;
		case 2:		*attenuation = 0.0F;	return 0;
		default:	return 1;
		}
}

int  GetAttenuatorsVHF(int atten_idx, float * attenuation)
{
	// fill in attenuation
	// use positive attenuation levels if signal is amplified (LNA)
	// use negative attenuation levels if signal is attenuated
	// sort by attenuation: use idx 0 for highest attenuation / most damping
	// this functions is called with incrementing idx
	//    - until this functions return != 0 for no more attenuator setting
	if ((atten_idx >= 0) && (atten_idx < 15))  // 15 steps 
	{
		*attenuation = r820t2_lna_gain_steps[atten_idx];	
		return 0;
	}
	return 1;
}



extern "C"
int EXTIO_API GetAttenuators( int atten_idx, float * attenuation )
{
	// fill in attenuation
	// use positive attenuation levels if signal is amplified (LNA)
	// use negative attenuation levels if signal is attenuated
	// sort by attenuation: use idx 0 for highest attenuation / most damping
	// this functions is called with incrementing idx
	//    - until this functions return != 0 for no more attenuator setting
	if (gR820Ton == false)
		return GetAttenuatorsHF( atten_idx, attenuation);
	else
		return GetAttenuatorsVHF( atten_idx, attenuation);
}



extern "C"
int EXTIO_API GetActualAttIdx(void)
{
	return giAttIdx;	// returns -1 on error
}

extern "C"
int EXTIO_API SetAttenuator( int atten_idx )
{
	int iPrevAttIdx = giAttIdx;
	if (gR820Ton == false)
	{
		switch (atten_idx)
		{
		case 0:
			gsel0 = 1; //1 -uint8_t
			gsel1 = 0;
			UpdateGPIO();
			giAttIdx = atten_idx;
			giAttIdxHF = atten_idx;
			return 0;
		case 1:
			gsel0 = 1;
			gsel1 = 1;
			UpdateGPIO();
			giAttIdx = atten_idx;
			giAttIdxHF = atten_idx;
			return 0;
		case 2:
			gsel0 = 0;
			gsel1 = 1;
			UpdateGPIO();
			giAttIdx = atten_idx;
			giAttIdxHF = atten_idx;
			return 0;

		default:
			return 1;	// ERROR
		}
		return 1;	// ERROR
	}
	else
	{
		if ((atten_idx >=0) && (atten_idx < 15))  // 15 steps 0-14
		{
			R820T2_set_lna_gain(atten_idx);
	//		DbgPrintf("\nR820T2_set_lna_gain %d", atten_idx);
			giAttIdx = atten_idx;
			giAttIdxVHF = atten_idx;
			return 0;
		}
	}
	return 1;
}
//---------------------------------------------------------------------------

//---------------------------------------------------------------------------

// optional function to get AGC Mode: AGC_OFF (always agc_index = 0), AGC_SLOW, AGC_MEDIUM, AGC_FAST, ...
// this functions is called with incrementing idx
//    - until this functions returns != 0, which means that all agc modes are already delivered

extern "C"
int EXTIO_API ExtIoGetAGCs(int agc_idx, char * text)	// text limited to max 16 char
{
	switch (agc_idx)
	{
	case 0:		strcpy(text, "Mix");	return 0;  // shows "IF"
	case 1:		strcpy(text, "VGA");	return 0;
	default:	return 1;
	}
	return 1;
}


extern "C"
int EXTIO_API ExtIoGetActualAGCidx(void)
{
	return giAgcIdx;	// returns -1 on error
}

extern "C"
int EXTIO_API ExtIoSetAGC(int agc_idx)
{
	int iPrevAgcIdx = giAgcIdx;
	switch (agc_idx)
	{
	case 0:
	case 1:
		giAgcIdx = agc_idx;
		if (iPrevAgcIdx != giAgcIdx)
		{
			if (pfnCallback)
				EXTIO_STATUS_CHANGE(pfnCallback, extHw_Changed_RF_IF);
		}
		return 0;
	default:
		return 1;	// ERROR
	}
	return 1;	// ERROR
}

// optional: HDSDR >= 2.62
extern "C"
int EXTIO_API ExtIoShowMGC(int agc_idx)		// return 1, to continue showing MGC slider on AGC
											// return 0, is default for not showing MGC slider
{
	switch (agc_idx)
	{
	case 0:	return 1;	// MiX (IF)
	case 1:	return 1;	// VGA
	default:
		return 0;	// ERROR
	}
	return 0;	// ERROR
}

//---------------------------------------------------------------------------

// following "MGC"s visible on "IF" button

extern "C"
int EXTIO_API ExtIoGetMGCs(int mgc_idx, float * gain)
{
	// fill in gain
	// sort by ascending gain: use idx 0 for lowest gain
	// this functions is called with incrementing idx
	//    - until this functions returns != 0, which means that all gains are already delivered

	switch (giAgcIdx)
	{
	case 0:	// Mix  (IF)
		if ((mgc_idx >= 0) && (mgc_idx < 16))
		{
			*gain = (float) mgc_idx ;	return 0;
		}else
			return 1;
		break;
	case 1:	// VGA
		if ((mgc_idx >= 0) && (mgc_idx < 16))
		{
			*gain = (float)mgc_idx*3.0F;	return 0;
		}
		else
			return 1;
		break;
	}
	return 1;
}

extern "C"
int EXTIO_API ExtIoGetActualMgcIdx(void)
{
	switch (giAgcIdx)
	{
	case 0:	// Mix
		return giMixGainIdx;
	case 1:	// VGA
		return giVGAGainIdx;
	}
	return -1;
}

extern "C"
int EXTIO_API ExtIoSetMGC(int mgc_idx)
{
	int iPrevMgcIdx = giMgcIdx;
	int iPrevMixIdx = giMixGainIdx;
	int iPrevVGAIdx = giVGAGainIdx;
	//
	double x = 0.0F;
	switch (giAgcIdx)
	{
	case 0:	// Mix
		if ((mgc_idx >= 0) && (mgc_idx < 16))
		{
			giMixGainIdx = mgc_idx;
			if (iPrevMixIdx != giMixGainIdx)
			{
				if (pfnCallback)
					EXTIO_STATUS_CHANGE(pfnCallback, extHw_Changed_RF_IF);
				if (gR820Ton == true)
					R820T2_set_mixer_gain(giMixGainIdx);
			}
			return 0;
		}
		else
			return 1;	// ERROR
		break;

	case 1:	// VGA
		if ((mgc_idx >= 0) && (mgc_idx < 16))
		{
			giVGAGainIdx = mgc_idx;
			if (iPrevVGAIdx != giVGAGainIdx)
			{
				if (pfnCallback)
					EXTIO_STATUS_CHANGE(pfnCallback, extHw_Changed_RF_IF);
				if (gR820Ton == true)
					R820T2_set_vga_gain(giVGAGainIdx);
			}
			return 0;
		}
		else
			return 1;	// ERROR
		break;
	}
	return 1;	// ERROR
}

//---------------------------------------------------------------------------

extern "C"
int EXTIO_API ExtIoGetSrates( int srate_idx, double * samplerate )
{
	uint32_t  tmp = gsmpfreq;
	switch ( srate_idx )
	{
		case 0:		*samplerate = 8000000.0;    break;
		case 1:		*samplerate = 16000000.0;   break;
		default:	return 1;	// ERROR
	}
	return 0;	// OK
}

extern "C"
int  EXTIO_API ExtIoGetActualSrateIdx(void)
{
	return giExtSrateIdx;
}

extern "C"
int  EXTIO_API ExtIoSetSrate( int srate_idx )
{
	double newSrate = 0.0;
	if ( 0 == ExtIoGetSrates( srate_idx, &newSrate ) )
	{
		giExtSrateIdx = srate_idx;
		gExtSampleRate = (unsigned)( newSrate + 0.5 );
		switch (srate_idx)
		{
		case 0:		gsmpfreq = FRQSMP / 2;     break;
		case 1:		gsmpfreq = FRQSMP;			break;
		default:	return 1;	// ERROR
		}
		if ((pfnCallback)&&(gADCfreq != gsmpfreq))
		{
			if (gR820Ton == true)
				gR820Tref = R820T_FREQ;
			else
				gR820Tref = 0;
			si5351aSetFrequency(gsmpfreq, gR820Tref);
			gADCfreq = gsmpfreq;
			gtunebin = (int)(((int64_t)IFR820T*FFTN) / gsmpfreq); // new IF bin R820T
			pfnCallback(-1, extHw_Changed_SampleRate, 0.0F, 0);   // renew sample rate
		}
		return 0;
	}
	return 1;	// ERROR
}

extern "C"
long EXTIO_API ExtIoGetBandwidth( int srate_idx )
{
	double newSrate = 0.0;
	long ret = -1L;
	if ( 0 == ExtIoGetSrates( srate_idx, &newSrate ) )
	{
		switch ( srate_idx )
		{
			case 0:		ret = 7500000L;	break;
			default:	ret = -1L;		break;
		}
		return ( ret >= newSrate || ret <= 0L ) ? -1L : ret;
	}
	return -1L;	// ERROR
}

//---------------------------------------------------------------------------

extern "C"
int  EXTIO_API ExtIoGetSetting( int idx, char * description, char * value )
{
	const char * hwTypeStr = 0;
	switch (gHwType)
	{
	default:
	case exthwUSBfloat32:	hwTypeStr = "FLOAT";	break;
	}

	switch ( idx )
	{
	case 0: snprintf( description, 1024, "%s", "Identifier" );		snprintf( value, 1024, "%s", SETTINGS_IDENTIFIER );	return 0;
	case 1:	snprintf( description, 1024, "%s", "SampleRateIdx" );	snprintf( value, 1024, "%d", giExtSrateIdx );		return 0;
	case 2:	snprintf( description, 1024, "%s", "AttenuationIdxHF" );	snprintf( value, 1024, "%d", giAttIdxHF );		return 0;
	case 3:	snprintf( description, 1024, "%s", "AttenuationIdxVHF");	snprintf( value, 1024, "%d", giAttIdxVHF);	    return 0;
	case 4:	snprintf( description, 1024, "%s", "0_Level_dB" );			return 0;
	case 5:	snprintf( description, 1024, "%s", "1_Freq_Hz" );			return 0;
	case 6:	snprintf( description, 1024, "%s", "1_Level_dB" );			return 0;
	case 7:		return 0;
	case 8:		return 0;
	default:	return -1;	// ERROR
	}
	return -1;	// ERROR
}


extern "C"
void EXTIO_API ExtIoSetSetting( int idx, const char * value )
{
	double newSrate;
	float  newAtten = 0.0F;
	int tempInt;
	// now we know that there's no need to save our settings into some (.ini) file,
	// what won't be possible without admin rights!!!,
	// if the program (and ExtIO) is installed in C:\Program files\..
	SDR_supports_settings = true;
	if (idx != 0 && !SDR_settings_valid)
	{
		gbsettings = false;
		return;	// ignore settings for some other ExtIO
	}
	gbsettings = true;
	switch ( idx )
	{
	case 0:		SDR_settings_valid = ( value && !strcmp( value, SETTINGS_IDENTIFIER ) );
				// make identifier version specific??? - or not ==> never change order of idx!
				break;
	case 1:		tempInt = atoi( value );
				if ( 0 == ExtIoGetSrates( tempInt, &newSrate ) )
				{
					giExtSrateIdx = tempInt;
					gExtSampleRate = (unsigned)( newSrate + 0.5 );
				}
				break;
	case 2:		tempInt = atoi( value );
				if ( 0 == GetAttenuatorsHF( tempInt,&newAtten ) )
					//giDefaultAttIdx = giAttIdx = 
					giAttIdxHF = tempInt;
				break;
	case 3:		
				tempInt = atoi(value);
				if (0 == GetAttenuatorsVHF(tempInt, &newAtten))
					// giDefaultAttIdx = giAttIdx =
					giAttIdxVHF = tempInt;		
				break;
	case 4:			break;
	case 5:			break;
	case 6:			break;
	case 7:			break;
	case 8:		if ( atoi(value) > 0 )
					gCustomSamplerate = atoi(value);
				break;
	}

}

//---------------------------------------------------------------------------
