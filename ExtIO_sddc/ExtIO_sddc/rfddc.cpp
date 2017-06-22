// Oscar Steila 2017
#include "rfddc.h"
extern bool gR820Ton;

fftwf_complex  intimefft[EVENODD][NVIA][FFTXBUF + 1][FFTN];
fftwf_complex  outfreqfft[EVENODD][NVIA][FFTXBUF + 1][FFTN];
fftwf_complex  infreqfft[EVENODD][NVIA][FFTXBUF + 1][D_FFTN];
fftwf_complex  outtimefft[EVENODD][NVIA][FFTXBUF + 1][D_FFTN];
float wnd[FFTN];
fftwf_complex  outtime[FFTXBUF + 1][D_FFTN];
fftwf_plan ptime2freq[EVENODD][NVIA][FFTXBUF + 1];     // fftw plan buffers per thread 
fftwf_plan pfreq2time[EVENODD][NVIA][FFTXBUF + 1];     // fftw plan buffers per thread 
short ins[FRAMEN + FFTN];
fftwf_complex  Htfilter[D_FFTN];  // lpb filter


std::atomic<bool> isdying(false);
std::atomic<bool> dataready0(false);
std::atomic<bool> dataready1(false);
std::atomic<bool> evenodd(false);
std::atomic<int> gtunebin(0);

float htfilter129[129] =   // ht 129 sample 0.45 use FFTN == 256
{
	-0.000000000016F,  0.000000000136F, -0.000000000407F,  0.000000000650F, -0.000000000000F, -0.000000003822F,  0.000000015349F, -0.000000041847F,
	0.000000092529F, -0.000000175271F,  0.000000289472F, -0.000000414201F,  0.000000492207F, -0.000000412804F,  0.000000000000F,  0.000000984140F,
	-0.000002808295F,  0.000005700312F, -0.000009727295F,  0.000014630037F, -0.000019631495F,  0.000023260842F, -0.000023257435F,  0.000016636264F,
	-0.000000000000F, -0.000029835803F,  0.000074884845F, -0.000134756870F,  0.000205307121F, -0.000277435990F,  0.000336400930F, -0.000362052401F,
	0.000330379676F, -0.000216626387F,  0.000000000000F,  0.000330333735F, -0.000768672126F,  0.001286699274F, -0.001829294596F,  0.002313723926F,
	-0.002633567416F,  0.002668288956F, -0.002298599502F,  0.001426776350F, -0.000000000000F, -0.001966261013F,  0.004368542866F, -0.007003365658F,
	0.009566569761F, -0.011666658002F,  0.012853137483F, -0.012658740589F,  0.010652113943F, -0.006495452691F,  0.000000000000F,  0.008828344586F,
	-0.019760642636F,  0.032339520267F, -0.045899922116F,  0.059615877543F, -0.072569586046F,  0.083835852339F, -0.092572422387F,  0.098105509581F,
	0.899999999992F,  0.098105509581F, -0.092572422387F,  0.083835852339F, -0.072569586046F,  0.059615877543F, -0.045899922116F,  0.032339520267F,
	-0.019760642636F,  0.008828344586F,  0.000000000000F, -0.006495452691F,  0.010652113943F, -0.012658740589F,  0.012853137483F, -0.011666658002F,
	0.009566569761F, -0.007003365658F,  0.004368542866F, -0.001966261013F, -0.000000000000F,  0.001426776350F, -0.002298599502F,  0.002668288956F,
	-0.002633567416F,  0.002313723926F, -0.001829294596F,  0.001286699274F, -0.000768672126F,  0.000330333735F,  0.000000000000F, -0.000216626387F,
	0.000330379676F, -0.000362052401F,  0.000336400930F, -0.000277435990F,  0.000205307121F, -0.000134756870F,  0.000074884845F, -0.000029835803F,
	-0.000000000000F,  0.000016636264F, -0.000023257435F,  0.000023260842F, -0.000019631495F,  0.000014630037F, -0.000009727295F,  0.000005700312F,
	-0.000002808295F,  0.000000984140F,  0.000000000000F, -0.000000412804F,  0.000000492207F, -0.000000414201F,  0.000000289472F, -0.000000175271F,
	0.000000092529F, -0.000000041847F,  0.000000015349F, -0.000000003822F, -0.000000000000F,  0.000000000650F, -0.000000000407F,  0.000000000136F,
	-0.000000000016F
};

float htfilter65[65] =  // ht 65 sample 0.42 use FFTN == 128
{
	0.000000005437F, 0.000000019750F, -0.000000383468F, 0.000001783791F, -0.000004979907F, 0.000009464401F, -0.000011107482F, 0.000000000000F,
	0.000038683041F, -0.000117318195F, 0.000230422153F, -0.000335041261F, 0.000338311654F, -0.000109045756F, -0.000472156508F, 0.001424216585F,
	-0.002563423888F, 0.003439472384F, -0.003373566972F, 0.001645103700F, 0.002178940892F, -0.007853031243F, 0.014149915970F, -0.018818313088F,
	0.018925432801F, -0.011586254959F, -0.005078424150F, 0.031048382651F, -0.063912978779F, 0.099037750433F, -0.130444706443F, 0.152212824175F,
	0.840000004559F, 0.152212824175F, -0.130444706443F, 0.099037750433F, -0.063912978779F, 0.031048382651F, -0.005078424150F, -0.011586254959F,
	0.018925432801F, -0.018818313088F, 0.014149915970F, -0.007853031243F, 0.002178940892F, 0.001645103700F, -0.003373566972F, 0.003439472384F,
	-0.002563423888F, 0.001424216585F, -0.000472156508F, -0.000109045756F, 0.000338311654F, -0.000335041261F, 0.000230422153F, -0.000117318195F,
	0.000038683041F, 0.000000000000F, -0.000011107482F, 0.000009464401F, -0.000004979907F, 0.000001783791F, -0.000000383468F, 0.000000019750F,
	0.000000005437F
};


float htfilter33[33] =  // ht 33 sample 0.42 use FFTN == 64
{
	-0.000052388424F, 0.000213327781F, -0.000397867753F, 0.000305829080F, 0.000573604033F, -0.002725960948F, 0.006150113658F, -0.009841958818F,
	0.011536069359F, -0.008016130355F, -0.003898905405F, 0.025933065625F, -0.057057848021F, 0.092983818405F, -0.126867797105F, 0.151163480843F,
	0.839999096090F, 0.151163480843F, -0.126867797105F, 0.092983818405F, -0.057057848021F, 0.025933065625F, -0.003898905405F, -0.008016130355F,
	0.011536069359F, -0.009841958818F, 0.006150113658F, -0.002725960948F, 0.000573604033F, 0.000305829080F, -0.000397867753F, 0.000213327781F,
	-0.000052388424F
};


void f_thread0()
{
	while (!isdying)
	{
		if (dataready0)
		{
			int e = evenodd;
			int me = e ^ 1;
			for (int m = 1; m < (FFTXBUF + 1); m++)
			{
				for (int w = 0, h = 0; w < FFTN / 16; w++)   
				{
					intimefft[e][0][m][h][0] = ins[m*FFTN + h] * wnd[h++];
					intimefft[e][0][m][h][0] = ins[m*FFTN + h] * wnd[h++];
					intimefft[e][0][m][h][0] = ins[m*FFTN + h] * wnd[h++];
					intimefft[e][0][m][h][0] = ins[m*FFTN + h] * wnd[h++];
					intimefft[e][0][m][h][0] = ins[m*FFTN + h] * wnd[h++];
					intimefft[e][0][m][h][0] = ins[m*FFTN + h] * wnd[h++];
					intimefft[e][0][m][h][0] = ins[m*FFTN + h] * wnd[h++];
					intimefft[e][0][m][h][0] = ins[m*FFTN + h] * wnd[h++];
				}
				fftwf_execute(ptime2freq[e][0][m]);
				fftwf_execute(pfreq2time[e][0][m]);
			}	
			for (int h = D_FFTN2; h < D_FFTN; )   // 1/2 D_FFTN copy to new frame interleaving
			{
				outtimefft[e][0][0][h][0] = outtimefft[me][0][FFTXBUF][h][0];
				outtimefft[e][0][0][h][1] = outtimefft[me][0][FFTXBUF][h++][1];
				outtimefft[e][0][0][h][0] = outtimefft[me][0][FFTXBUF][h][0];
				outtimefft[e][0][0][h][1] = outtimefft[me][0][FFTXBUF][h++][1];
				outtimefft[e][0][0][h][0] = outtimefft[me][0][FFTXBUF][h][0];
				outtimefft[e][0][0][h][1] = outtimefft[me][0][FFTXBUF][h++][1];
				outtimefft[e][0][0][h][0] = outtimefft[me][0][FFTXBUF][h][0];
				outtimefft[e][0][0][h][1] = outtimefft[me][0][FFTXBUF][h++][1];
			}
			fftwf_complex * pin1 = &outtimefft[me][0][0][D_FFTN2];
			fftwf_complex * pin2 = &outtimefft[me][1][0][0];
			fftwf_complex * pout = &outtime[0][0];
			if (gR820Ton == false)
			{
				for (int m = 0, k = 0; m < (FFTXBUF * D_FFTN) / 16; m++)
				{
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = pin1[k][1] + pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = pin1[k][1] + pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = pin1[k][1] + pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = pin1[k][1] + pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = pin1[k][1] + pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = pin1[k][1] + pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = pin1[k][1] + pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = pin1[k][1] + pin2[k++][1];
				}
			}
			else // reverse IQ spectrum
			{
				for (int m = 0, k = 0; m < (FFTXBUF * D_FFTN) / 16; m++)
				{
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = - pin1[k][1] - pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = - pin1[k][1] - pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = - pin1[k][1] - pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = - pin1[k][1] - pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = - pin1[k][1] - pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = - pin1[k][1] - pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = - pin1[k][1] - pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = - pin1[k][1] - pin2[k++][1];
				}
			}
			dataready0 = false;
		}
		
		std::this_thread::yield();
	}
};
void f_thread1()
{
	while (!isdying)
	{
		if (dataready1)
		{
			int e = evenodd;
			int me = e ^ 1;
			for (int m = 0; m < FFTXBUF; m++)
			{
				for (int w = 0, h = 0; w < FFTN / 16; w++)
				{
					intimefft[e][1][m][h][0] = ins[m*FFTN + FFTN2 + h] * wnd[h++];
					intimefft[e][1][m][h][0] = ins[m*FFTN + FFTN2 + h] * wnd[h++];
					intimefft[e][1][m][h][0] = ins[m*FFTN + FFTN2 + h] * wnd[h++];
					intimefft[e][1][m][h][0] = ins[m*FFTN + FFTN2 + h] * wnd[h++];
					intimefft[e][1][m][h][0] = ins[m*FFTN + FFTN2 + h] * wnd[h++];
					intimefft[e][1][m][h][0] = ins[m*FFTN + FFTN2 + h] * wnd[h++];
					intimefft[e][1][m][h][0] = ins[m*FFTN + FFTN2 + h] * wnd[h++];
					intimefft[e][1][m][h][0] = ins[m*FFTN + FFTN2 + h] * wnd[h++];
				}
				fftwf_execute(ptime2freq[e][1][m]);
				fftwf_execute(pfreq2time[e][1][m]);
			}
			fftwf_complex * pin1 = &outtimefft[me][0][FFTXBUF / 2][D_FFTN2];
			fftwf_complex * pin2 = &outtimefft[me][1][FFTXBUF / 2][0];
			fftwf_complex * pout = &outtime[FFTXBUF / 2][0];
			if (gR820Ton == false)
			{
				for (int m = 0, k = 0; m < (FFTXBUF * D_FFTN) / 16; m++)
				{
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = pin1[k][1] + pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = pin1[k][1] + pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = pin1[k][1] + pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = pin1[k][1] + pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = pin1[k][1] + pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = pin1[k][1] + pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = pin1[k][1] + pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = pin1[k][1] + pin2[k++][1];
				}
			} 
			else  // reverse IQ spectrum
			{
				for (int m = 0, k = 0; m < (FFTXBUF * D_FFTN) / 16; m++)
				{
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = -pin1[k][1] - pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = -pin1[k][1] - pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = -pin1[k][1] - pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = -pin1[k][1] - pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = -pin1[k][1] - pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = -pin1[k][1] - pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = -pin1[k][1] - pin2[k++][1];
					pout[k][0] = pin1[k][0] + pin2[k][0];
					pout[k][1] = -pin1[k][1] - pin2[k++][1];
				}
			}
			dataready1 = false;
		}
		
		std::this_thread::yield();
	}
};


rfddc::rfddc() :
	t0(f_thread0),
	t1(f_thread1),
	g0(t0),
	g1(t1)
{

};


void rfddc::initp()
{
	// init setup

	for (int f = 0; f < NVIA; f++)
	{
		for (int m = 0; m < FFTXBUF + 1; m++)
		{
			for (int e = 0; e < EVENODD; e++)
			{
				ptime2freq[e][f][m] = fftwf_plan_dft_1d(FFTN, intimefft[e][f][m], outfreqfft[e][f][m], FFTW_FORWARD, FFTW_MEASURE);
				pfreq2time[e][f][m] = fftwf_plan_dft_1d(D_FFTN, infreqfft[e][f][m], outtimefft[e][f][m], FFTW_BACKWARD, FFTW_MEASURE);
			}
		}
	}
	//init window
	int N = FFTN - 1;
	float pi = (float)4 * (float)atan(1.0);
	for (int n = 0; n < FFTN2; n++) {
		wnd[2 * FFTN2 - n - 1] = wnd[n] = (float) .149e-6;// ((.5 - .5*cos(2 * pi*n / N)))*(1.49e-8);  // 1/(32736*FFTN); // hANN
	}
	memset(intimefft, 0, sizeof(intimefft));
	memset(infreqfft, 0, sizeof(infreqfft));
	memset(outfreqfft, 0, sizeof(outfreqfft));
	memset(outtimefft, 0, sizeof(outtimefft));
	{
		fftwf_complex  htintime[D_FFTN];
		fftwf_plan ptime2Ht;     // fftw plan 

		ptime2Ht = fftwf_plan_dft_1d(D_FFTN, htintime, Htfilter, FFTW_FORWARD, FFTW_ESTIMATE);
		memset(htintime, 0, sizeof(htintime));
		for (int t = 0; t < D_FFTN / 2; t++)
		{
			switch (D_FFTN)
			{
			case 64:
				htintime[t][0] = htfilter33[t];
				break;
			case 128:
				htintime[t][0] = htfilter65[t];
				break;
			default:
				htintime[t][0] = htfilter129[t];
				break;
			}
			htintime[t][1] = 0.0F; //   htintime[t][1] * mwnd[t];
		}
		fftwf_execute(ptime2Ht);
	}
};

rfddc::~rfddc()
{
	isdying = true;   // fluss all threads
};


int rfddc::arun(short *bufin)
{
	evenodd = (!evenodd); // phase oddeven
	int e = evenodd;
	int v = !evenodd;
	int j;
	
	int tunebin = gtunebin;


	// start threads
	
	memcpy(ins, &ins[FRAMEN], sizeof(short)*FFTN);  // save last FFTN block
	memcpy(&ins[FFTN], bufin, sizeof(short)*FRAMEN);

	dataready0 = true;
	dataready1 = true;

	//  Tuning and decimation in frequency 

	memset(&infreqfft[v][0][0][0][0], 0, sizeof(infreqfft)/2);
	for (int f = 0; f < NVIA; f++)
	{
		for (int m = 0; m < FFTXBUF + 1; m++)
		{
			if ((tunebin % 2 != 0) && (f != 0))
			{
				for (int k = 0; k < D_FFTN2; k++)
				{
					j = tunebin + k;
					if (j >= FFTN)
						j -= FFTN;
					if (j < 0)
						j += FFTN;
					infreqfft[v][f][m][k][0] = outfreqfft[v][f][m][j][0] * Htfilter[k][0] - outfreqfft[v][f][m][j][1] * Htfilter[k][1];
					infreqfft[v][f][m][k][1] = outfreqfft[v][f][m][j][1] * Htfilter[k][0] + outfreqfft[v][f][m][j][0] * Htfilter[k][1];
				}
				for (int k = D_FFTN2; k < D_FFTN; k++)
				{
					j = tunebin - D_FFTN + k;
					if (j < 0)
						j += FFTN;
					if (j >= FFTN)
						j -= FFTN;
					infreqfft[v][f][m][k][0] = outfreqfft[v][f][m][j][0] * Htfilter[k][0] - outfreqfft[v][f][m][j][1] * Htfilter[k][1];
					infreqfft[v][f][m][k][1] = outfreqfft[v][f][m][j][1] * Htfilter[k][0] + outfreqfft[v][f][m][j][0] * Htfilter[k][1];
				}

			}
			else
				// invert phase of every interleaved frame if tunebin is odd
			{
				for (int k = 0; k < D_FFTN2; k++)
				{
					j = tunebin + k;
					if (j >= FFTN)
						j -= FFTN;
					if (j < 0)
						j += FFTN;
					infreqfft[v][f][m][k][0] = -(outfreqfft[v][f][m][j][0] * Htfilter[k][0] - outfreqfft[v][f][m][j][1] * Htfilter[k][1]);
					infreqfft[v][f][m][k][1] = -(outfreqfft[v][f][m][j][1] * Htfilter[k][0] + outfreqfft[v][f][m][j][0] * Htfilter[k][1]);
				}
				
				for (int k = D_FFTN2; k < D_FFTN; k++)
				{
					j = tunebin - D_FFTN + k;
					if (j < 0)
						j += FFTN;
					if (j >= FFTN)
						j -= FFTN;
					infreqfft[v][f][m][k][0] = -(outfreqfft[v][f][m][j][0] * Htfilter[k][0] - outfreqfft[v][f][m][j][1] * Htfilter[k][1]);
					infreqfft[v][f][m][k][1] = -(outfreqfft[v][f][m][j][1] * Htfilter[k][0] + outfreqfft[v][f][m][j][0] * Htfilter[k][1]);
				}
				
			}
		}
	}
	while ( (dataready0) || (dataready1) )
	{
		std::this_thread::yield();
	}
	return 0;
};
