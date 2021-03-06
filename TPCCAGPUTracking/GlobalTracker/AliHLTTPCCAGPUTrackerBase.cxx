// **************************************************************************
// This file is property of and copyright by the ALICE HLT Project          *
// ALICE Experiment at CERN, All rights reserved.                           *
//                                                                          *
// Primary Authors: Sergey Gorbunov <sergey.gorbunov@kip.uni-heidelberg.de> *
//                  Ivan Kisel <kisel@kip.uni-heidelberg.de>                *
//					David Rohr <drohr@kip.uni-heidelberg.de>				*
//                  for The ALICE HLT Project.                              *
//                                                                          *
// Permission to use, copy, modify and distribute this software and its     *
// documentation strictly for non-commercial purposes is hereby granted     *
// without fee, provided that the above copyright notice appears in all     *
// copies and that both the copyright notice and this permission notice     *
// appear in the supporting documentation. The authors make no claims       *
// about the suitability of this software for any purpose. It is            *
// provided "as is" without express or implied warranty.                    *
//                                                                          *
//***************************************************************************

#include <string.h>
#ifndef WIN32
#include <unistd.h>
#endif
#include "AliHLTTPCCAGPUTrackerBase.h"
#include "AliHLTTPCCAClusterData.h"
#include "AliHLTTPCCAGPUTrackerCommon.h"

ClassImp( AliHLTTPCCAGPUTrackerBase )

#ifdef HLTCA_ENABLE_GPU_TRACKER

int AliHLTTPCCAGPUTrackerBase::GlobalTracking(int iSlice, int threadId, AliHLTTPCCAGPUTrackerBase::helperParam* hParam)
{
	if (fDebugLevel >= 3) {CAGPUDebug("GPU Tracker running Global Tracking for slice %d on thread %d\n", iSlice, threadId);}

	int sliceLeft = (iSlice + (fgkNSlices / 2 - 1)) % (fgkNSlices / 2);
	int sliceRight = (iSlice + 1) % (fgkNSlices / 2);
	if (iSlice >= fgkNSlices / 2)
	{
		sliceLeft += fgkNSlices / 2;
		sliceRight += fgkNSlices / 2;
	}
	while (fSliceOutputReady < iSlice || fSliceOutputReady < sliceLeft || fSliceOutputReady < sliceRight)
	{
		if (hParam != NULL && hParam->fReset) return(1);
	}

	pthread_mutex_lock(&((pthread_mutex_t*) fSliceGlobalMutexes)[sliceLeft]);
	pthread_mutex_lock(&((pthread_mutex_t*) fSliceGlobalMutexes)[sliceRight]);
	fSlaveTrackers[iSlice].PerformGlobalTracking(fSlaveTrackers[sliceLeft], fSlaveTrackers[sliceRight], HLTCA_GPU_MAX_TRACKS, HLTCA_GPU_MAX_TRACKS);
	pthread_mutex_unlock(&((pthread_mutex_t*) fSliceGlobalMutexes)[sliceLeft]);
	pthread_mutex_unlock(&((pthread_mutex_t*) fSliceGlobalMutexes)[sliceRight]);

	fSliceLeftGlobalReady[sliceLeft] = 1;
	fSliceRightGlobalReady[sliceRight] = 1;
	if (fDebugLevel >= 3) {CAGPUDebug("GPU Tracker finished Global Tracking for slice %d on thread %d\n", iSlice, threadId);}
	return(0);
}

void* AliHLTTPCCAGPUTrackerBase::helperWrapper(void* arg)
{
	AliHLTTPCCAGPUTrackerBase::helperParam* par = (AliHLTTPCCAGPUTrackerBase::helperParam*) arg;
	AliHLTTPCCAGPUTrackerBase* cls = par->fCls;

	AliHLTTPCCATracker* tmpTracker = new AliHLTTPCCATracker;

#ifdef HLTCA_STANDALONE
	if (cls->fDebugLevel >= 2) CAGPUInfo("\tHelper thread %d starting", par->fNum);
#endif

#if defined(HLTCA_STANDALONE) & !defined(WIN32)
	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(par->fNum * 2 + 2, &mask);
	//sched_setaffinity(0, sizeof(mask), &mask);
#endif

	while(pthread_mutex_lock(&((pthread_mutex_t*) par->fMutex)[0]) == 0 && par->fTerminate == false)
	{
		int mustRunSlice19 = 0;
		for (int i = par->fNum + 1;i < 36;i += cls->fNHelperThreads + 1)
		{
			//if (cls->fDebugLevel >= 3) CAGPUInfo("\tHelper Thread %d Running, Slice %d+%d, Phase %d", par->fNum, i, par->fPhase);
			if (par->fPhase)
			{
				if (cls->fGlobalTracking)
				{
					int realSlice = i + 1;
					if (realSlice % (fgkNSlices / 2) < 1) realSlice -= fgkNSlices / 2;

					if (realSlice % (fgkNSlices / 2) != 1)
					{
						cls->GlobalTracking(realSlice, par->fNum + 1, par);
					}

					if (realSlice == 19)
					{
						mustRunSlice19 = 1;
					}
					else
					{
						while (cls->fSliceLeftGlobalReady[realSlice] == 0 || cls->fSliceRightGlobalReady[realSlice] == 0)
						{
							if (par->fReset) goto ResetHelperThread;
						}
						cls->WriteOutput(par->pOutput, realSlice, par->fNum + 1);
					}
				}
				else
				{
					while (cls->fSliceOutputReady < i)
					{
						if (par->fReset) goto ResetHelperThread;
					}
					cls->WriteOutput(par->pOutput, i, par->fNum + 1);
				}
			}
			else
			{
				if (cls->ReadEvent(par->pClusterData, i, par->fNum + 1)) par->fError = 1;
				par->fDone = i + 1;
			}
			//if (cls->fDebugLevel >= 3) CAGPUInfo("\tHelper Thread %d Finished, Slice %d+%d, Phase %d", par->fNum, i, par->fPhase);
		}
		if (mustRunSlice19)
		{
			while (cls->fSliceLeftGlobalReady[19] == 0 || cls->fSliceRightGlobalReady[19] == 0)
			{
				if (par->fReset) goto ResetHelperThread;
			}
			cls->WriteOutput(par->pOutput, 19, par->fNum + 1);
		}
ResetHelperThread:
		cls->ResetThisHelperThread(par);
	}
#ifdef HLTCA_STANDALONE
	if (cls->fDebugLevel >= 2) CAGPUInfo("\tHelper thread %d terminating", par->fNum);
#endif
	delete tmpTracker;
	pthread_mutex_unlock(&((pthread_mutex_t*) par->fMutex)[1]);
	pthread_exit(NULL);
	return(NULL);
}

void AliHLTTPCCAGPUTrackerBase::ResetThisHelperThread(AliHLTTPCCAGPUTrackerBase::helperParam* par)
{
	if (par->fReset) CAGPUImportant("GPU Helper Thread %d reseting", par->fNum);
	par->fReset = false;
	pthread_mutex_unlock(&((pthread_mutex_t*) par->fMutex)[1]);
}

#define SemLockName "AliceHLTTPCCAGPUTrackerInitLockSem"

AliHLTTPCCAGPUTrackerBase::AliHLTTPCCAGPUTrackerBase() :
fGpuTracker(NULL),
fGPUMemory(NULL),
fHostLockedMemory(NULL),
fGPUMergerMemory(NULL),
fGPUMergerHostMemory(NULL),
fGPUMergerMaxMemory(0),
fDebugLevel(0),
fDebugMask(0xFFFFFFFF),
fOutFile(NULL),
fGPUMemSize(0),
fHostMemSize(0),
fCudaDevice(0),
fOutputControl(NULL),
fThreadId(0),
fCudaInitialized(0),
fSelfheal(0),
fConstructorBlockCount(30),
fSelectorBlockCount(30),
fConstructorThreadCount(256),
fNHelperThreads(HLTCA_GPU_DEFAULT_HELPER_THREADS),
fHelperParams(NULL),
fHelperMemMutex(NULL),
fSliceOutputReady(0),
fSliceGlobalMutexes(NULL),
fGlobalTracking(0),
fNSlaveThreads(0),
fStuckProtection(0),
fGPUStuck(0),
fParam(NULL)
{}

AliHLTTPCCAGPUTrackerBase::~AliHLTTPCCAGPUTrackerBase()
{
}

void AliHLTTPCCAGPUTrackerBase::ReleaseGlobalLock(void* sem)
{
	//Release the global named semaphore that locks GPU Initialization
#ifdef WIN32
	HANDLE* h = (HANDLE*) sem;
	ReleaseSemaphore(*h, 1, NULL);
	CloseHandle(*h);
	delete h;
#else
	sem_t* pSem = (sem_t*) sem;
	sem_post(pSem);
	sem_unlink(SemLockName);
#endif
}

int AliHLTTPCCAGPUTrackerBase::CheckMemorySizes(int sliceCount)
{
	//Check constants for correct memory sizes
	if (sizeof(AliHLTTPCCATracker) * sliceCount > HLTCA_GPU_TRACKER_OBJECT_MEMORY)
	{
		CAGPUError("Insufficiant Tracker Object Memory for %d slices", sliceCount);
		return(1);
	}

	if (fgkNSlices * AliHLTTPCCATracker::CommonMemorySize() > HLTCA_GPU_COMMON_MEMORY)
	{
		CAGPUError("Insufficiant Common Memory");
		return(1);
	}

	if (fgkNSlices * (HLTCA_ROW_COUNT + 1) * sizeof(AliHLTTPCCARow) > HLTCA_GPU_ROWS_MEMORY)
	{
		CAGPUError("Insufficiant Row Memory");
		return(1);
	}

	if (fDebugLevel >= 3)
	{
		CAGPUInfo("Memory usage: Tracker Object %lld / %lld, Common Memory %lld / %lld, Row Memory %lld / %lld",
			(long long int) sizeof(AliHLTTPCCATracker) * sliceCount, (long long int) HLTCA_GPU_TRACKER_OBJECT_MEMORY,
			(long long int) (fgkNSlices * AliHLTTPCCATracker::CommonMemorySize()), (long long int) HLTCA_GPU_COMMON_MEMORY,
			(long long int) (fgkNSlices * (HLTCA_ROW_COUNT + 1) * sizeof(AliHLTTPCCARow)), (long long int) HLTCA_GPU_ROWS_MEMORY);
	}
	return(0);
}

void AliHLTTPCCAGPUTrackerBase::SetDebugLevel(const int dwLevel, std::ostream* const NewOutFile)
{
	//Set Debug Level and Debug output File if applicable
	fDebugLevel = dwLevel;
	if (NewOutFile) fOutFile = NewOutFile;
	for (int i = 0;i < fgkNSlices;i++) fSlaveTrackers[i].SetGPUDebugLevel(dwLevel > 0); //Set at least to 1 to collect timing information
}

int AliHLTTPCCAGPUTrackerBase::SetGPUTrackerOption(const char* OptionName, int OptionValue)
{
	//Set a specific GPU Tracker Option
	if (strcmp(OptionName, "DebugMask") == 0)
	{
		fDebugMask = OptionValue;
	}
	else if (strcmp(OptionName, "HelperThreads") == 0)
	{
		fNHelperThreads = OptionValue;
	}
	else if (strcmp(OptionName, "GlobalTracking") == 0)
	{
		fGlobalTracking = OptionValue;
	}
	else if (strcmp(OptionName, "StuckProtection") == 0)
	{
		fStuckProtection = OptionValue;
	}
	else
	{
		CAGPUError("Unknown Option: %s", OptionName);
		return(1);
	}

	if (fNHelperThreads > fNSlaveThreads && fCudaInitialized)
	{
		CAGPUInfo("Insufficient Slave Threads available (%d), creating additional Slave Threads (%d)\n", fNSlaveThreads, fNHelperThreads);
		StopHelperThreads();
		StartHelperThreads();
	}

	return(0);
}

int AliHLTTPCCAGPUTrackerBase::ReadEvent(AliHLTTPCCAClusterData* pClusterData, int iSlice, int threadId)
{
	fSlaveTrackers[iSlice].SetGPUSliceDataMemory(SliceDataMemory(fHostLockedMemory, iSlice), RowMemory(fHostLockedMemory, iSlice));
#ifdef HLTCA_GPU_TIME_PROFILE
	unsigned long long int a, b;
	AliHLTTPCCATracker::StandaloneQueryTime(&a);
#endif
	if (fSlaveTrackers[iSlice].ReadEvent(&pClusterData[iSlice])) return(1);
#ifdef HLTCA_GPU_TIME_PROFILE
	AliHLTTPCCATracker::StandaloneQueryTime(&b);
	CAGPUInfo("Read %d %f %f\n", threadId, ((double) b - (double) a) / (double) fProfTimeC, ((double) a - (double) fProfTimeD) / (double) fProfTimeC);
#endif
	return(0);
}

void AliHLTTPCCAGPUTrackerBase::WriteOutput(AliHLTTPCCASliceOutput** pOutput, int iSlice, int threadId)
{
	if (fDebugLevel >= 3) {CAGPUDebug("GPU Tracker running WriteOutput for slice %d on thread %d\n", iSlice, threadId);}
	fSlaveTrackers[iSlice].SetOutput(&pOutput[iSlice]);
#ifdef HLTCA_GPU_TIME_PROFILE
	unsigned long long int a, b;
	AliHLTTPCCATracker::StandaloneQueryTime(&a);
#endif
	if (fNHelperThreads) pthread_mutex_lock((pthread_mutex_t*) fHelperMemMutex);
	fSlaveTrackers[iSlice].WriteOutputPrepare();
	if (fNHelperThreads) pthread_mutex_unlock((pthread_mutex_t*) fHelperMemMutex);
	fSlaveTrackers[iSlice].WriteOutput();
#ifdef HLTCA_GPU_TIME_PROFILE
	AliHLTTPCCATracker::StandaloneQueryTime(&b);
	CAGPUInfo("Write %d %f %f\n", threadId, ((double) b - (double) a) / (double) fProfTimeC, ((double) a - (double) fProfTimeD) / (double) fProfTimeC);
#endif
	if (fDebugLevel >= 3) {CAGPUDebug("GPU Tracker finished WriteOutput for slice %d on thread %d\n", iSlice, threadId);}
}

int AliHLTTPCCAGPUTrackerBase::InitializeSliceParam(int iSlice, const AliGPUCAParam *param)
{
	//Initialize Slice Tracker Parameter for a slave tracker
	fSlaveTrackers[iSlice].Initialize(param, iSlice);
	fParam = param;
	return(0);
}

void AliHLTTPCCAGPUTrackerBase::ResetHelperThreads(int helpers)
{
	CAGPUImportant("Error occurred, GPU tracker helper threads will be reset (Number of threads %d (%d))", fNHelperThreads, fNSlaveThreads);
	SynchronizeGPU();
	ReleaseThreadContext();
	for (int i = 0;i < fNHelperThreads;i++)
	{
		fHelperParams[i].fReset = true;
		if (helpers || i >= fNHelperThreads) pthread_mutex_lock(&((pthread_mutex_t*) fHelperParams[i].fMutex)[1]);
	}
	CAGPUImportant("GPU Tracker helper threads have ben reset");
}

int AliHLTTPCCAGPUTrackerBase::StartHelperThreads()
{
	int nThreads = fNHelperThreads;
	if (nThreads)
	{
		fHelperParams = new helperParam[nThreads];
		if (fHelperParams == NULL)
		{
			CAGPUError("Memory allocation error");
			ExitGPU();
			return(1);
		}
		for (int i = 0;i < nThreads;i++)
		{
			fHelperParams[i].fCls = this;
			fHelperParams[i].fTerminate = false;
			fHelperParams[i].fReset = false;
			fHelperParams[i].fNum = i;
			fHelperParams[i].fMutex = malloc(2 * sizeof(pthread_mutex_t));
			if (fHelperParams[i].fMutex == NULL)
			{
				CAGPUError("Memory allocation error");
				ExitGPU();
				return(1);
			}
			for (int j = 0;j < 2;j++)
			{
				if (pthread_mutex_init(&((pthread_mutex_t*) fHelperParams[i].fMutex)[j], NULL))
				{
					CAGPUError("Error creating pthread mutex");
					ExitGPU();
					return(1);
				}

				pthread_mutex_lock(&((pthread_mutex_t*) fHelperParams[i].fMutex)[j]);
			}
			fHelperParams[i].fThreadId = (void*) malloc(sizeof(pthread_t));

			if (pthread_create((pthread_t*) fHelperParams[i].fThreadId, NULL, helperWrapper, &fHelperParams[i]))
			{
				CAGPUError("Error starting slave thread");
				ExitGPU();
				return(1);
			}
		}
	}
	fNSlaveThreads = nThreads;
	return(0);
}

int AliHLTTPCCAGPUTrackerBase::StopHelperThreads()
{
	if (fNSlaveThreads)
	{
		for (int i = 0;i < fNSlaveThreads;i++)
		{
			fHelperParams[i].fTerminate = true;
			if (pthread_mutex_unlock(&((pthread_mutex_t*) fHelperParams[i].fMutex)[0]))
			{
				CAGPUError("Error unlocking mutex to terminate slave");
				return(1);
			}
			if (pthread_mutex_lock(&((pthread_mutex_t*) fHelperParams[i].fMutex)[1]))
			{
				CAGPUError("Error locking mutex");
				return(1);
			}
			if (pthread_join( *((pthread_t*) fHelperParams[i].fThreadId), NULL))
			{
				CAGPUError("Error waiting for thread to terminate");
				return(1);
			}
			free(fHelperParams[i].fThreadId);
			for (int j = 0;j < 2;j++)
			{
				if (pthread_mutex_unlock(&((pthread_mutex_t*) fHelperParams[i].fMutex)[j]))
				{
					CAGPUError("Error unlocking mutex before destroying");
					return(1);
				}
				pthread_mutex_destroy(&((pthread_mutex_t*) fHelperParams[i].fMutex)[j]);
			}
			free(fHelperParams[i].fMutex);
		}
		delete[] fHelperParams;
	}
	fNSlaveThreads = 0;
	return(0);
}

void AliHLTTPCCAGPUTrackerBase::SetOutputControl( AliHLTTPCCASliceOutput::outputControlStruct* val)
{
	//Set Output Control Pointers
	fOutputControl = val;
	for (int i = 0;i < fgkNSlices;i++)
	{
		fSlaveTrackers[i].SetOutputControl(val);
	}
}

int AliHLTTPCCAGPUTrackerBase::GetThread()
{
	//Get Thread ID
#ifdef WIN32
	return((int) (size_t) GetCurrentThread());
#else
	return((int) syscall (SYS_gettid));
#endif
}

const AliHLTTPCCASliceOutput::outputControlStruct* AliHLTTPCCAGPUTrackerBase::OutputControl() const
{
	//Return Pointer to Output Control Structure
	return fOutputControl;
}

int AliHLTTPCCAGPUTrackerBase::IsInitialized()
{
	return(fCudaInitialized);
}

int AliHLTTPCCAGPUTrackerBase::InitGPU(int forceDeviceID)
{
#if defined(HLTCA_STANDALONE) & !defined(WIN32)
	cpu_set_t mask;
	CPU_ZERO(&mask);
	CPU_SET(0, &mask);
	//sched_setaffinity(0, sizeof(mask), &mask);
#endif

	if (CheckMemorySizes(36)) return(1);

#ifdef WIN32
	HANDLE* semLock = new HANDLE;
	*semLock = CreateSemaphore(NULL, 1, 1, SemLockName);
	if (*semLock == NULL)
	{
		CAGPUError("Error creating GPUInit Semaphore");
		return(1);
	}
	WaitForSingleObject(*semLock, INFINITE);
#else
	sem_t* semLock = sem_open(SemLockName, O_CREAT, 0x01B6, 1);
	if (semLock == SEM_FAILED)
	{
		CAGPUError("Error creating GPUInit Semaphore");
		return(1);
	}
	timespec semtime;
	clock_gettime(CLOCK_REALTIME, &semtime);
	semtime.tv_sec += 10;
	while (sem_timedwait(semLock, &semtime) != 0)
	{
		CAGPUError("Global Lock for GPU initialisation was not released for 10 seconds, assuming another thread died");
		CAGPUWarning("Resetting the global lock");
		sem_post(semLock);
	}
#endif

	fThreadId = GetThread();

#ifdef HLTCA_GPU_MERGER
	fGPUMergerMaxMemory = HLTCA_GPU_MERGER_MEMORY;
#endif
	const size_t trackerGPUMem  = HLTCA_GPU_ROWS_MEMORY + HLTCA_GPU_COMMON_MEMORY + 36 * (HLTCA_GPU_SLICE_DATA_MEMORY + HLTCA_GPU_GLOBAL_MEMORY);
	const size_t trackerHostMem = HLTCA_GPU_ROWS_MEMORY + HLTCA_GPU_COMMON_MEMORY + 36 * (HLTCA_GPU_SLICE_DATA_MEMORY + HLTCA_GPU_TRACKS_MEMORY) + HLTCA_GPU_TRACKER_OBJECT_MEMORY;
	fGPUMemSize = trackerGPUMem + fGPUMergerMaxMemory + HLTCA_GPU_MEMALIGN;
	fHostMemSize = trackerHostMem + fGPUMergerMaxMemory + HLTCA_GPU_MEMALIGN;
	int retVal = InitGPU_Runtime(forceDeviceID);
	ReleaseGlobalLock(semLock);
	fGPUMergerMemory = alignPointer(((char*) fGPUMemory) + trackerGPUMem, HLTCA_GPU_MEMALIGN);
	fGPUMergerHostMemory = alignPointer(((char*) fHostLockedMemory) + trackerHostMem, HLTCA_GPU_MEMALIGN);

	if (retVal)
	{
		CAGPUImportant("GPU Tracker initialization failed");
		return(1);
	}

	//Don't run constructor / destructor here, this will be just local memcopy of Tracker in GPU Memory
	fGpuTracker = (AliHLTTPCCATracker*) TrackerMemory(fHostLockedMemory, 0);

	for (int i = 0;i < fgkNSlices;i++)
	{
		fSlaveTrackers[i].SetGPUTracker();
		fSlaveTrackers[i].SetGPUTrackerCommonMemory((char*) CommonMemory(fHostLockedMemory, i));
		fSlaveTrackers[i].SetGPUSliceDataMemory(SliceDataMemory(fHostLockedMemory, i), RowMemory(fHostLockedMemory, i));
	}

	if (StartHelperThreads()) return(1);

	fHelperMemMutex = malloc(sizeof(pthread_mutex_t));
	if (fHelperMemMutex == NULL)
	{
		CAGPUError("Memory allocation error");
		ExitGPU_Runtime();
		return(1);
	}

	if (pthread_mutex_init((pthread_mutex_t*) fHelperMemMutex, NULL))
	{
		CAGPUError("Error creating pthread mutex");
		ExitGPU_Runtime();
		free(fHelperMemMutex);
		return(1);
	}

	fSliceGlobalMutexes = malloc(sizeof(pthread_mutex_t) * fgkNSlices);
	if (fSliceGlobalMutexes == NULL)
	{
		CAGPUError("Memory allocation error");
		ExitGPU_Runtime();
		return(1);
	}
	for (int i = 0;i < fgkNSlices;i++)
	{
		if (pthread_mutex_init(&((pthread_mutex_t*) fSliceGlobalMutexes)[i], NULL))
		{
			CAGPUError("Error creating pthread mutex");
			ExitGPU_Runtime();
			return(1);
		}
	}

	fCudaInitialized = 1;
	CAGPUInfo("GPU Tracker initialization successfull"); //Verbosity reduced because GPU backend will print CAGPUImportant message!

	return(retVal);
}

int AliHLTTPCCAGPUTrackerBase::ExitGPU()
{
	if (StopHelperThreads()) return(1);
	pthread_mutex_destroy((pthread_mutex_t*) fHelperMemMutex);
	free(fHelperMemMutex);

	for (int i = 0;i < fgkNSlices;i++) pthread_mutex_destroy(&((pthread_mutex_t*) fSliceGlobalMutexes)[i]);
	free(fSliceGlobalMutexes);

	return(ExitGPU_Runtime());
}

int AliHLTTPCCAGPUTrackerBase::Reconstruct_Base_FinishSlices(AliHLTTPCCASliceOutput** pOutput, int& iSlice)
{
	fSlaveTrackers[iSlice].CommonMemory()->fNLocalTracks = fSlaveTrackers[iSlice].CommonMemory()->fNTracks;
	fSlaveTrackers[iSlice].CommonMemory()->fNLocalTrackHits = fSlaveTrackers[iSlice].CommonMemory()->fNTrackHits;
	if (fGlobalTracking) fSlaveTrackers[iSlice].CommonMemory()->fNTracklets = 1;

	if (fDebugLevel >= 3) CAGPUInfo("Data ready for slice %d, helper thread %d", iSlice, iSlice % (fNHelperThreads + 1));
	fSliceOutputReady = iSlice;

	if (fGlobalTracking)
	{
		if (iSlice % (fgkNSlices / 2) == 2)
		{
			int tmpId = iSlice % (fgkNSlices / 2) - 1;
			if (iSlice >= fgkNSlices / 2) tmpId += fgkNSlices / 2;
			GlobalTracking(tmpId, 0, NULL);
			fGlobalTrackingDone[tmpId] = 1;
		}
		for (int tmpSlice3a = 0;tmpSlice3a < iSlice;tmpSlice3a += fNHelperThreads + 1)
		{
			int tmpSlice3 = tmpSlice3a + 1;
			if (tmpSlice3 % (fgkNSlices / 2) < 1) tmpSlice3 -= (fgkNSlices / 2);
			if (tmpSlice3 >= iSlice) break;

			int sliceLeft = (tmpSlice3 + (fgkNSlices / 2 - 1)) % (fgkNSlices / 2);
			int sliceRight = (tmpSlice3 + 1) % (fgkNSlices / 2);
			if (tmpSlice3 >= fgkNSlices / 2)
			{
				sliceLeft += fgkNSlices / 2;
				sliceRight += fgkNSlices / 2;
			}

			if (tmpSlice3 % (fgkNSlices / 2) != 1 && fGlobalTrackingDone[tmpSlice3] == 0 && sliceLeft < iSlice && sliceRight < iSlice)
			{
				GlobalTracking(tmpSlice3, 0, NULL);
				fGlobalTrackingDone[tmpSlice3] = 1;
			}

			if (fWriteOutputDone[tmpSlice3] == 0 && fSliceLeftGlobalReady[tmpSlice3] && fSliceRightGlobalReady[tmpSlice3])
			{
				WriteOutput(pOutput, tmpSlice3, 0);
				fWriteOutputDone[tmpSlice3] = 1;
			}
		}
	}
	else
	{
		if (iSlice % (fNHelperThreads + 1) == 0)
		{
			WriteOutput(pOutput, iSlice, 0);
		}
	}
	return(0);
}

int AliHLTTPCCAGPUTrackerBase::Reconstruct_Base_Finalize(AliHLTTPCCASliceOutput** pOutput, char*& tmpMemoryGlobalTracking)
{
	if (fGlobalTracking)
	{
		for (int tmpSlice3a = 0;tmpSlice3a < fgkNSlices;tmpSlice3a += fNHelperThreads + 1)
		{
			int tmpSlice3 = (tmpSlice3a + 1);
			if (tmpSlice3 % (fgkNSlices / 2) < 1) tmpSlice3 -= (fgkNSlices / 2);
			if (fGlobalTrackingDone[tmpSlice3] == 0) GlobalTracking(tmpSlice3, 0, NULL);
		}
		for (int tmpSlice3a = 0;tmpSlice3a < fgkNSlices;tmpSlice3a += fNHelperThreads + 1)
		{
			int tmpSlice3 = (tmpSlice3a + 1);
			if (tmpSlice3 % (fgkNSlices / 2) < 1) tmpSlice3 -= (fgkNSlices / 2);
			if (fWriteOutputDone[tmpSlice3] == 0)
			{
				while (fSliceLeftGlobalReady[tmpSlice3] == 0 || fSliceRightGlobalReady[tmpSlice3] == 0);
				WriteOutput(pOutput, tmpSlice3, 0);
			}
		}
	}

	for (int i = 0;i < fNHelperThreads;i++)
	{
		pthread_mutex_lock(&((pthread_mutex_t*) fHelperParams[i].fMutex)[1]);
	}

	if (fGlobalTracking)
	{
		free(tmpMemoryGlobalTracking);
		if (fDebugLevel >= 3)
		{
			for (int iSlice = 0;iSlice < fgkNSlices;iSlice++)
			{
				CAGPUDebug("Slice %d - Tracks: Local %d Global %d - Hits: Local %d Global %d\n", iSlice, fSlaveTrackers[iSlice].CommonMemory()->fNLocalTracks, fSlaveTrackers[iSlice].CommonMemory()->fNTracks, fSlaveTrackers[iSlice].CommonMemory()->fNLocalTrackHits, fSlaveTrackers[iSlice].CommonMemory()->fNTrackHits);
			}
		}
	}

	if (fDebugLevel >= 3) CAGPUInfo("GPU Reconstruction finished");
	return(0);
}

int AliHLTTPCCAGPUTrackerBase::Reconstruct_Base_StartGlobal(AliHLTTPCCASliceOutput** pOutput, char*& tmpMemoryGlobalTracking)
{
	if (fGlobalTracking)
	{
		int tmpmemSize = sizeof(AliHLTTPCCATracklet)
#ifdef EXTERN_ROW_HITS
		+ HLTCA_ROW_COUNT * sizeof(int)
#endif
		+ 16;
		tmpMemoryGlobalTracking = (char*) malloc(tmpmemSize * fgkNSlices);
		for (int i = 0;i < fgkNSlices;i++)
		{
			fSliceLeftGlobalReady[i] = 0;
			fSliceRightGlobalReady[i] = 0;
		}
		memset(fGlobalTrackingDone, 0, fgkNSlices);
		memset(fWriteOutputDone, 0, fgkNSlices);

		for (int iSlice = 0;iSlice < fgkNSlices;iSlice++)
		{
			fSlaveTrackers[iSlice].SetGPUTrackerTrackletsMemory(tmpMemoryGlobalTracking + (tmpmemSize * iSlice), 1);
		}
	}
	for (int i = 0;i < fNHelperThreads;i++)
	{
		fHelperParams[i].fPhase = 1;
		fHelperParams[i].pOutput = pOutput;
		pthread_mutex_unlock(&((pthread_mutex_t*) fHelperParams[i].fMutex)[0]);
	}
	return(0);
}

int AliHLTTPCCAGPUTrackerBase::Reconstruct_Base_SliceInit(AliHLTTPCCAClusterData* pClusterData, int& iSlice)
{
	//Initialize GPU Slave Tracker
	if (fDebugLevel >= 3) CAGPUInfo("Creating Slice Data (Slice %d)", iSlice);
	if (iSlice % (fNHelperThreads + 1) == 0)
	{
		if (ReadEvent(pClusterData, iSlice, 0))
		{
			CAGPUError("Error reading event");
			ResetHelperThreads(1);
			return(1);
		}
	}
	else
	{
		if (fDebugLevel >= 3) CAGPUInfo("Waiting for helper thread %d", iSlice % (fNHelperThreads + 1) - 1);
		while(fHelperParams[iSlice % (fNHelperThreads + 1) - 1].fDone < iSlice);
		if (fHelperParams[iSlice % (fNHelperThreads + 1) - 1].fError)
		{
			ResetHelperThreads(1);
			return(1);
		}
	}

	if (fDebugLevel >= 4)
	{
#ifndef BITWISE_COMPATIBLE_DEBUG_OUTPUT
		*fOutFile << std::endl << std::endl << "Reconstruction: Slice " << iSlice << "/" << 36 << std::endl;
#endif
		if (fDebugMask & 1) fSlaveTrackers[iSlice].DumpSliceData(*fOutFile);
	}

	if (fSlaveTrackers[iSlice].Data().MemorySize() > HLTCA_GPU_SLICE_DATA_MEMORY RANDOM_ERROR)
	{
		CAGPUError("Insufficiant Slice Data Memory: Slice %d, Needed %lld, Available %lld", iSlice, (long long int) fSlaveTrackers[iSlice].Data().MemorySize(), (long long int) HLTCA_GPU_SLICE_DATA_MEMORY);
		ResetHelperThreads(1);
		return(1);
	}

	if (fDebugLevel >= 3)
	{
		CAGPUInfo("GPU Slice Data Memory Used: %lld/%lld", (long long int) fSlaveTrackers[iSlice].Data().MemorySize(), (long long int) HLTCA_GPU_SLICE_DATA_MEMORY);
	}
	return(0);
}

int AliHLTTPCCAGPUTrackerBase::Reconstruct_Base_Init(AliHLTTPCCASliceOutput** pOutput, AliHLTTPCCAClusterData* pClusterData)
{
	if (!fCudaInitialized)
	{
		CAGPUError("GPUTracker not initialized");
		return(1);
	}
	if (fThreadId != GetThread())
	{
		CAGPUDebug("CUDA thread changed, migrating context, Previous Thread: %d, New Thread: %d", fThreadId, GetThread());
		fThreadId = GetThread();
	}

	if (fDebugLevel >= 2) CAGPUInfo("Running GPU Tracker");

	ActivateThreadContext();

	memcpy((void*) fGpuTracker, (void*) fSlaveTrackers, sizeof(AliHLTTPCCATracker) * 36);

	if (fDebugLevel >= 3) CAGPUInfo("Allocating GPU Tracker memory and initializing constants");

#ifdef HLTCA_GPU_TIME_PROFILE
	AliHLTTPCCATracker::StandaloneQueryFreq(&fProfTimeC);
	AliHLTTPCCATracker::StandaloneQueryTime(&fProfTimeD);
#endif

	for (int iSlice = 0;iSlice < 36;iSlice++)
	{
		//Make this a GPU Tracker
		fGpuTracker[iSlice].SetGPUTracker();
		fGpuTracker[iSlice].SetGPUTrackerCommonMemory((char*) CommonMemory(fGPUMemory, iSlice));
		fGpuTracker[iSlice].SetGPUSliceDataMemory(SliceDataMemory(fGPUMemory, iSlice), RowMemory(fGPUMemory, iSlice));
		fGpuTracker[iSlice].SetPointersSliceData(&pClusterData[iSlice], false);
		fGpuTracker[iSlice].GPUParametersConst()->fGPUMem = (char*) fGPUMemory;

		//Set Pointers to GPU Memory
		char* tmpMem = (char*) GlobalMemory(fGPUMemory, iSlice);

		if (fDebugLevel >= 3) CAGPUInfo("Initialising GPU Hits Memory");
		tmpMem = fGpuTracker[iSlice].SetGPUTrackerHitsMemory(tmpMem, pClusterData[iSlice].NumberOfClusters());
		tmpMem = alignPointer(tmpMem, HLTCA_GPU_MEMALIGN);

		if (fDebugLevel >= 3) CAGPUInfo("Initialising GPU Tracklet Memory");
		tmpMem = fGpuTracker[iSlice].SetGPUTrackerTrackletsMemory(tmpMem, HLTCA_GPU_MAX_TRACKLETS);
		tmpMem = alignPointer(tmpMem, HLTCA_GPU_MEMALIGN);

		if (fDebugLevel >= 3) CAGPUInfo("Initialising GPU Track Memory");
		tmpMem = fGpuTracker[iSlice].SetGPUTrackerTracksMemory(tmpMem, HLTCA_GPU_MAX_TRACKS, pClusterData[iSlice].NumberOfClusters());
		tmpMem = alignPointer(tmpMem, HLTCA_GPU_MEMALIGN);

		if (fGpuTracker[iSlice].TrackMemorySize() >= HLTCA_GPU_TRACKS_MEMORY RANDOM_ERROR)
		{
			CAGPUError("Insufficiant Track Memory");
			ResetHelperThreads(0);
			return(1);
		}

		if ((size_t) (tmpMem - (char*) GlobalMemory(fGPUMemory, iSlice)) > HLTCA_GPU_GLOBAL_MEMORY RANDOM_ERROR)
		{
			CAGPUError("Insufficiant Global Memory (%lld < %lld)", (long long int) (size_t) (tmpMem - (char*) GlobalMemory(fGPUMemory, iSlice)), (long long int) HLTCA_GPU_GLOBAL_MEMORY);
			ResetHelperThreads(0);
			return(1);
		}

		if (fDebugLevel >= 3)
		{
			CAGPUInfo("GPU Global Memory Used: %lld/%lld, Page Locked Tracks Memory used: %lld / %lld", (long long int) (tmpMem - (char*) GlobalMemory(fGPUMemory, iSlice)), (long long int) HLTCA_GPU_GLOBAL_MEMORY, (long long int) fGpuTracker[iSlice].TrackMemorySize(), (long long int) HLTCA_GPU_TRACKS_MEMORY);
		}

		//Initialize Startup Constants
		*fSlaveTrackers[iSlice].NTracklets() = 0;
		*fSlaveTrackers[iSlice].NTracks() = 0;
		*fSlaveTrackers[iSlice].NTrackHits() = 0;
		fGpuTracker[iSlice].GPUParametersConst()->fGPUFixedBlockCount = 36 > fConstructorBlockCount ? (iSlice < fConstructorBlockCount) : fConstructorBlockCount * (iSlice + 1) / 36 - fConstructorBlockCount * (iSlice) / 36;
		if (fDebugLevel >= 3) CAGPUInfo("Blocks for Slice %d: %d", iSlice, fGpuTracker[iSlice].GPUParametersConst()->fGPUFixedBlockCount);
		fGpuTracker[iSlice].GPUParametersConst()->fGPUiSlice = iSlice;
		fSlaveTrackers[iSlice].GPUParameters()->fGPUError = 0;
		fSlaveTrackers[iSlice].GPUParameters()->fNextTracklet = ((fConstructorBlockCount + 36 - 1 - iSlice) / 36) * fConstructorThreadCount;
		fGpuTracker[iSlice].SetGPUTextureBase(fGpuTracker[0].Data().Memory());
	}

	for (int i = 0;i < fNHelperThreads;i++)
	{
		fHelperParams[i].fDone = 0;
		fHelperParams[i].fError = 0;
		fHelperParams[i].fPhase = 0;
		fHelperParams[i].pClusterData = pClusterData;
		pthread_mutex_unlock(&((pthread_mutex_t*) fHelperParams[i].fMutex)[0]);
	}

	return(0);
}

double AliHLTTPCCAGPUTrackerBase::GetTimer(int iSlice, unsigned int iTimer) {return fSlaveTrackers[iSlice].GetTimer(iTimer) / ((iTimer == 0 || iTimer >= 8) ? (fNHelperThreads + 1) : 1);}
void AliHLTTPCCAGPUTrackerBase::ResetTimer(int iSlice, unsigned int iTimer) {fSlaveTrackers[iSlice].ResetTimer(iTimer);}
const AliHLTTPCCATracker* AliHLTTPCCAGPUTrackerBase::CPUTracker(int iSlice) {return &fSlaveTrackers[iSlice];}

#endif
