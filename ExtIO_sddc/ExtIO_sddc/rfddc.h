#pragma once
#pragma warning(disable : 4996)

// RF DDC globals
#include <iostream>
#include <thread>
#include <vector>
#include <atomic>
#include "fftw3.h"


#define FRAMEN (65536)
#define FFTN (1024)
#define FFTN2 (FFTN/2)
#define FFTDSN (FFTN / DSN)
#define FFTXBUF (FRAMEN/FFTN)
#define NVIA (2)
#define EVENODD (2)

#define D_FFTN (256)   // 256, 128, 64
#define D_FFTN2 (D_FFTN/2)

#define IFLEN (D_FFTN*FFTXBUF)

extern fftwf_complex  outtime[FFTXBUF + 1][D_FFTN];
extern short ins[FRAMEN + FFTN];
extern std::atomic<int> gtunebin;
extern std::atomic<bool> isdying;
extern std::atomic<bool> dataready0;
extern std::atomic<bool> dataready1;

class thread_guard
{
	std::thread& t;
public:
	explicit thread_guard(std::thread& t_) :
		t(t_)
	{}
	~thread_guard()
	{
		if (t.joinable())
		{
			t.join();
		}
	}
	thread_guard(thread_guard const&) = delete;
	thread_guard& operator=(thread_guard const&) = delete;
};


class rfddc
{
public:
	rfddc();
	~rfddc();
	void initp();
	int  arun(short *bufin);
	void tune(int bin);
private:
	std::thread t0;  // initializartion order is important
	std::thread t1;
	thread_guard g0;
	thread_guard g1;
};

