// **************************************************************************
// This file is property of and copyright by the ALICE HLT Project          *
// ALICE Experiment at CERN, All rights reserved.                           *
//                                                                          *
// Primary Authors: Sergey Gorbunov <sergey.gorbunov@kip.uni-heidelberg.de> *
//                  Ivan Kisel <kisel@kip.uni-heidelberg.de>                *
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


#include "AliHLTTPCCAStandaloneFramework.h"
#include "AliHLTTPCCATrackParam.h"
#include "AliTPCCommonMath.h"
#include "AliHLTTPCCAClusterData.h"
#include "AliGPUReconstruction.h"

#if defined(HLTCA_BUILD_O2_LIB) & defined(HLTCA_STANDALONE)
#undef HLTCA_STANDALONE //We disable the standalone application features for the O2 lib. This is a hack since the HLTCA_STANDALONE setting is ambigious... In this file it affects standalone application features, in the other files it means independence from aliroot
#endif

#ifdef HLTCA_STANDALONE
#include <omp.h>
#include "include.h"
#ifdef WIN32
#include <conio.h>
#else
#include <pthread.h>
#include <unistd.h>
#include "../cmodules/linux_helpers.h"
#endif
#endif

AliHLTTPCCAStandaloneFramework &AliHLTTPCCAStandaloneFramework::Instance()
{
  // reference to static object
  static AliHLTTPCCAStandaloneFramework gAliHLTTPCCAStandaloneFramework;
  return gAliHLTTPCCAStandaloneFramework;
}

AliHLTTPCCAStandaloneFramework::AliHLTTPCCAStandaloneFramework()
: fMerger(), fClusterData(fInternalClusterData), fOutputControl(), fTracker(NULL), fStatNEvents( 0 ), fDebugLevel(0), fEventDisplay(0), fRunQA(0), fRunMerger(1), fMCLabels(0), fMCInfo(0)
{
  //* constructor

  for ( int i = 0; i < 20; i++ ) {
    fLastTime[i] = 0;
    fStatTime[i] = 0;
  }
  for ( int i = 0;i < fgkNSlices;i++) fSliceOutput[i] = NULL;
}

AliHLTTPCCAStandaloneFramework::AliHLTTPCCAStandaloneFramework( const AliHLTTPCCAStandaloneFramework& )
    : fMerger(), fClusterData(fInternalClusterData), fOutputControl(), fTracker(), fStatNEvents( 0 ), fDebugLevel(0), fEventDisplay(0), fRunQA(0), fRunMerger(1), fMCLabels(0), fMCInfo(0)
{
  //* dummy
  for ( int i = 0; i < 20; i++ ) {
    fLastTime[i] = 0;
    fStatTime[i] = 0;
  }
  for ( int i = 0;i < fgkNSlices;i++) fSliceOutput[i] = NULL;
}

const AliHLTTPCCAStandaloneFramework &AliHLTTPCCAStandaloneFramework::operator=( const AliHLTTPCCAStandaloneFramework& ) const
{
  //* dummy
  return *this;
}

AliHLTTPCCAStandaloneFramework::~AliHLTTPCCAStandaloneFramework()
{
    //* destructor
    if (fOutputControl.fOutputPtr == NULL)
    {
        for (int i = 0;i < fgkNSlices;i++)
        {
            if (fSliceOutput[i]) free(fSliceOutput[i]);
        }
    }
    delete fTracker;
}

int AliHLTTPCCAStandaloneFramework::Initialize(AliGPUReconstruction* rec)
{
  fTracker = new AliHLTTPCCATrackerFramework(rec);
  fTracker->SetOutputControl(&fOutputControl);
  fTracker->SetGPUTracker(rec->IsGPU());
  fTracker->SetGPUTrackerOption("GlobalTracking", rec->GetParam().rec.GlobalTracking);
  fTracker->SetGPUTrackerOption("HelperThreads", rec->GetDeviceProcessingSettings().nDeviceHelperThreads);
  fRunQA = rec->GetDeviceProcessingSettings().runQA;
  fEventDisplay = rec->GetDeviceProcessingSettings().runEventDisplay;
  return 0;
}

void AliHLTTPCCAStandaloneFramework::Uninitialize()
{
    delete fTracker;
    fTracker = NULL;
}

void AliHLTTPCCAStandaloneFramework::StartDataReading( int guessForNumberOfClusters )
{
  //prepare for reading of the event

  int sliceGuess = 2 * guessForNumberOfClusters / fgkNSlices;

  for ( int i = 0; i < fgkNSlices; i++ ) {
    fClusterData[i].StartReading( i, sliceGuess );
  }
  fMCLabels.clear();
  fMCInfo.clear();
}

int AliHLTTPCCAStandaloneFramework::ProcessEvent(bool resetTimers)
{
  // perform the event reconstruction

  fStatNEvents++;

#ifdef HLTCA_STANDALONE
  static HighResTimer timerTracking, timerMerger, timerQA;
  static int nCount = 0;
  if (resetTimers)
  {
      timerTracking.Reset();
      timerMerger.Reset();
      timerQA.Reset();
      nCount = 0;
  }
  timerTracking.Start();

  if (fEventDisplay)
  {
	fTracker->SetKeepData(1);
  }
#endif

  if (fTracker->ProcessSlices(fClusterData, fSliceOutput)) return (1);

#ifdef HLTCA_STANDALONE
  timerTracking.Stop();
#endif

  if (fRunMerger)
  {
#ifdef HLTCA_STANDALONE
      timerMerger.Start();
#endif
	  fMerger.Clear();

	  for ( int i = 0; i < fgkNSlices; i++ ) {
		//printf("slice %d clusters %d tracks %d\n", i, fClusterData[i].NumberOfClusters(), fSliceOutput[i]->NTracks());
		fMerger.SetSliceData( i, fSliceOutput[i] );
	  }

#ifdef HLTCA_GPU_MERGER
	  if (fTracker->GetGPUTracker()->GPUMergerAvailable() && fTracker->GetGPUTracker()->IsInitialized()) fMerger.SetGPUTracker(fTracker->GetGPUTracker());
#endif
      fMerger.SetSliceTrackers(&fTracker->CPUTracker(0));
	  fMerger.Reconstruct(resetTimers);
#ifdef HLTCA_STANDALONE
      timerMerger.Stop();
#endif
  }

#ifdef HLTCA_STANDALONE
#ifdef BUILD_QA
  if (fRunQA || (fEventDisplay && GetNMCInfo()))
  {
    timerQA.Start();
    RunQA(!fRunQA);
    timerQA.Stop();
  }
#endif

  nCount++;
#ifndef HLTCA_BUILD_O2_LIB
  char nAverageInfo[16] = "";
  if (nCount > 1) sprintf(nAverageInfo, " (%d)", nCount);
  printf("Tracking Time: %'d us%s\n", (int) (1000000 * timerTracking.GetElapsedTime() / nCount), nAverageInfo);
  if (fRunMerger) printf("Merging and Refit Time: %'d us\n", (int) (1000000 * timerMerger.GetElapsedTime() / nCount));
  if (fRunQA) printf("QA Time: %'d us\n", (int) (1000000 * timerQA.GetElapsedTime() / nCount));
#endif

  if (fDebugLevel >= 1)
  {
		const char* tmpNames[10] = {"Initialisation", "Neighbours Finder", "Neighbours Cleaner", "Starts Hits Finder", "Start Hits Sorter", "Weight Cleaner", "Tracklet Constructor", "Tracklet Selector", "Global Tracking", "Write Output"};

		for (int i = 0;i < 10;i++)
		{
            double time = 0;
			for ( int iSlice = 0; iSlice < fgkNSlices;iSlice++)
			{
				time += fTracker->GetTimer(iSlice, i);
                if (!HLTCA_TIMING_SUM) fTracker->ResetTimer(iSlice, i);
			}
			time /= fgkNSlices;
#ifdef HLTCA_HAVE_OPENMP
			if (fTracker->GetGPUStatus() < 2) time /= omp_get_max_threads();
#endif

			printf("Execution Time: Task: %20s ", tmpNames[i]);
			printf("Time: %1.0f us", time * 1000000 / nCount);
			printf("\n");
		}
		printf("Execution Time: Task: %20s Time: %1.0f us\n", "Merger", timerMerger.GetElapsedTime() * 1000000. / nCount);
        if (!HLTCA_TIMING_SUM)
        {
            timerTracking.Reset();
            timerMerger.Reset();
            timerQA.Reset();
            nCount = 0;
        }
  }

#ifdef BUILD_EVENT_DISPLAY
  if (fEventDisplay)
  {
    static int displayActive = 0;
	if (!displayActive)
	{
#ifdef WIN32
		semLockDisplay = CreateSemaphore(0, 1, 1, 0);
		HANDLE hThread;
		if ((hThread = CreateThread(NULL, NULL, &OpenGLMain, NULL, NULL, NULL)) == NULL)
#else
		static pthread_t hThread;
		if (pthread_create(&hThread, NULL, OpenGLMain, NULL))
#endif
		{
			printf("Coult not Create GL Thread...\nExiting...\n");
		}
		displayActive = 1;
	}
	else
	{
#ifdef WIN32
		ReleaseSemaphore(semLockDisplay, 1, NULL);
#else
		pthread_mutex_unlock(&semLockDisplay);
#endif
		ShowNextEvent();
	}

	while (kbhit()) getch();
	printf("Press key for next event!\n");

	int iKey;
	do
	{
#ifdef WIN32
		Sleep(10);
#else
		usleep(10000);
#endif
		iKey = kbhit() ? getch() : 0;
		if (iKey == 'q') exitButton = 2;
        else if (iKey == 'n') break;
        else if (iKey)
        {
            while (sendKey != 0)
            {
                #ifdef WIN32
                		Sleep(1);
                #else
                		usleep(1000);
                #endif
            }
            sendKey = iKey;
        }
	} while (exitButton == 0);
	if (exitButton == 2)
    {
        DisplayExit();
        return(2);
    }
	exitButton = 0;
	printf("Loading next event\n");

#ifdef WIN32
	WaitForSingleObject(semLockDisplay, INFINITE);
#else
	pthread_mutex_lock(&semLockDisplay);
#endif

	displayEventNr++;
  }
#endif
#endif

  for ( int i = 0; i < 3; i++ ) fStatTime[i] += fLastTime[i];

  return(0);
}

void AliHLTTPCCAStandaloneFramework::WriteEvent( std::ostream &out ) const
{
  // write event to the file
  for ( int iSlice = 0; iSlice < fgkNSlices; iSlice++ ) {
    fClusterData[iSlice].WriteEvent( out );
  }
}

int AliHLTTPCCAStandaloneFramework::ReadEvent( std::istream &in, bool resetIds, bool addData, float shift, float minZ, float maxZ, bool silent, bool doQA )
{
  //* Read event from file
  int nClusters = 0, nCurrentClusters = 0;
  if (addData) for (int i = 0;i < fgkNSlices;i++) nCurrentClusters += fClusterData[i].NumberOfClusters();
  int nCurrentMCTracks = addData ? fMCInfo.size() : 0;

  int sliceOldClusters[36];
  int sliceNewClusters[36];
  int removed = 0;
  for ( int iSlice = 0; iSlice < fgkNSlices; iSlice++ ) {
    sliceOldClusters[iSlice] = addData ? fClusterData[iSlice].NumberOfClusters() : 0;
    fClusterData[iSlice].ReadEvent( in, addData );
    sliceNewClusters[iSlice] = fClusterData[iSlice].NumberOfClusters() - sliceOldClusters[iSlice];
    if (resetIds)
    {
      for (int i = 0;i < sliceNewClusters[iSlice];i++)
      {
        fClusterData[iSlice].Clusters()[sliceOldClusters[iSlice] + i].fId = nCurrentClusters + nClusters + i;
      }
    }
    if (shift != 0.)
    {
      for (int i = 0;i < sliceNewClusters[iSlice];i++)
      {
        AliHLTTPCCAClusterData::Data& tmp = fClusterData[iSlice].Clusters()[sliceOldClusters[iSlice] + i];
        tmp.fZ += iSlice < 18 ? shift : -shift;
      }
    }
    nClusters += sliceNewClusters[iSlice];
  }
  if (nClusters)
  {
    if (doQA)
    {
      fMCLabels.resize(nCurrentClusters + nClusters);
      in.read((char*) (fMCLabels.data() + nCurrentClusters), nClusters * sizeof(fMCLabels[0]));
    }
    if (!doQA || !in || in.gcount() != nClusters * (int) sizeof(fMCLabels[0]))
    {
      fMCLabels.clear();
      fMCInfo.clear();
    }
    else
    {
      if (addData)
      {
          for (int i = 0;i < nClusters;i++)
          {
              for (int j = 0;j < 3;j++)
              {
                  AliHLTTPCClusterMCWeight& tmp = fMCLabels[nCurrentClusters + i].fClusterID[j];
                  if (tmp.fMCID >= 0) tmp.fMCID += nCurrentMCTracks;
              }
          }
      }
      int nMCTracks = 0;
      in.read((char*) &nMCTracks, sizeof(nMCTracks));
      if (in.eof())
      {
        fMCInfo.clear();
      }
      else
      {
        fMCInfo.resize(nCurrentMCTracks + nMCTracks);
        in.read((char*) (fMCInfo.data() + nCurrentMCTracks), nMCTracks * sizeof(fMCInfo[0]));
        if (in.eof())
        {
            fMCInfo.clear();
        }
        else if (shift != 0.)
        {
            for (int i = 0;i < nMCTracks;i++)
            {
                AliHLTTPCCAMCInfo& tmp = fMCInfo[nCurrentMCTracks + i];
                tmp.fZ += tmp.fZ > 0 ? shift : -shift;
            }
        }
      }
    }
    if (minZ > -1e6 || maxZ > -1e6)
    {
      unsigned int currentCluster = nCurrentClusters;
      unsigned int currentClusterTotal = nCurrentClusters;
      for (int iSlice = 0;iSlice < 36;iSlice++)
      {
        int currentClusterSlice = sliceOldClusters[iSlice];
        for (int i = sliceOldClusters[iSlice];i < sliceOldClusters[iSlice] + sliceNewClusters[iSlice];i++)
        {
          float sign = iSlice < 18 ? 1 : -1;
          if (sign * fClusterData[iSlice].Clusters()[i].fZ >= minZ && sign * fClusterData[iSlice].Clusters()[i].fZ <= maxZ)
          {
            if (currentClusterSlice != i) fClusterData[iSlice].Clusters()[currentClusterSlice] = fClusterData[iSlice].Clusters()[i];
            if (resetIds) fClusterData[iSlice].Clusters()[currentClusterSlice].fId = currentCluster;
            if (fMCLabels.size() > currentClusterTotal && currentCluster != currentClusterTotal) fMCLabels[currentCluster] = fMCLabels[currentClusterTotal];
            //printf("Keeping Cluster ID %d (ID in slice %d) Z=%f (sector %d) --> %d (slice %d)\n", currentClusterTotal, i, fClusterData[iSlice].Clusters()[i].fZ, iSlice, currentCluster, currentClusterSlice);
            currentClusterSlice++;
            currentCluster++;
          }
          else
          {
            //printf("Removing Cluster ID %d (ID in slice %d) Z=%f (sector %d)\n", currentClusterTotal, i, fClusterData[iSlice].Clusters()[i].fZ, iSlice);
            removed++;
          }
          currentClusterTotal++;
        }
        fClusterData[iSlice].SetNumberOfClusters(currentClusterSlice);
        sliceNewClusters[iSlice] = currentClusterSlice - sliceOldClusters[iSlice];
      }
      nClusters = currentCluster - nCurrentClusters;
      if (currentCluster < fMCLabels.size()) fMCLabels.resize(currentCluster);
    }
  }
#ifdef HLTCA_STANDALONE
  if (!silent)
  {
    printf("Read %d Clusters with %d MC labels and %d MC tracks\n", nClusters, (int) (fMCLabels.size() ? (fMCLabels.size() - nCurrentClusters) : 0), (int) fMCInfo.size() - nCurrentMCTracks);
    if (minZ > -1e6 || maxZ > 1e6) printf("Removed %d / %d clusters\n", removed, nClusters + removed);
    if (addData) printf("Total %d Clusters with %d MC labels and %d MC tracks\n", nClusters + nCurrentClusters, (int) fMCLabels.size(), (int) fMCInfo.size());
  }
#endif
  nClusters += nCurrentClusters;
  return(nClusters);
}
