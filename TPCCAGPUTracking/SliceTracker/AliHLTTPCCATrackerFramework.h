//-*- Mode: C++ -*-
// @(#) $Id: AliHLTTPCCATracker.h 33907 2009-07-23 13:52:49Z sgorbuno $
// ************************************************************************
// This file is property of and copyright by the ALICE HLT Project        *
// ALICE Experiment at CERN, All rights reserved.                         *
// See cxx source for full Copyright notice                               *
//                                                                        *
//*************************************************************************

#ifndef ALIHLTTPCCATRACKERFRAMEWORK_H
#define ALIHLTTPCCATRACKERFRAMEWORK_H

#include "AliHLTTPCCATracker.h"
#include "AliHLTTPCCAGPUTracker.h"
#include "AliGPUCAParam.h"
#include "AliHLTTPCCASliceOutput.h"
#include "AliCAGPULogging.h"
#include <iostream>
#include <string.h>

class AliHLTTPCCASliceOutput;
class AliHLTTPCCAClusterData;
class AliGPUReconstruction;

class AliHLTTPCCATrackerFramework : AliCAGPULogging
{
public:
	AliHLTTPCCATrackerFramework(AliGPUReconstruction* rec);
	~AliHLTTPCCATrackerFramework();

	void SetGPUDebugLevel(int Level, std::ostream *OutFile = NULL, std::ostream *GPUOutFile = NULL);
	int SetGPUTrackerOption(const char* OptionName, int OptionValue) {if (strcmp(OptionName, "GlobalTracking") == 0) fGlobalTracking = OptionValue;return(fGPUTracker->SetGPUTrackerOption(OptionName, OptionValue));}
	int SetGPUTracker(bool enable);

	int InitializeSliceParam(int iSlice, const AliGPUCAParam *param);

	const AliHLTTPCCASliceOutput::outputControlStruct* OutputControl() const { return fOutputControl; }
	void SetOutputControl( AliHLTTPCCASliceOutput::outputControlStruct* val);

	int ProcessSlices(AliHLTTPCCAClusterData* pClusterData, AliHLTTPCCASliceOutput** pOutput);
	double GetTimer(int iSlice, int iTimer);
	void ResetTimer(int iSlice, int iTimer);

	int MaxSliceCount() const { return(fUseGPUTracker ? (fGPUTrackerAvailable ? fGPUTracker->GetSliceCount() : 0) : fCPUSliceCount); }
	int GetGPUStatus() const { return(fGPUTrackerAvailable + fUseGPUTracker); }

	const AliGPUCAParam& Param(int iSlice) const { return(fCPUTrackers[iSlice].Param()); }
	const AliGPUCAParam& GetParam(int iSlice) const { return(*((AliGPUCAParam*)fCPUTrackers[iSlice].pParam())); }
	const AliHLTTPCCARow& Row(int iSlice, int iRow) const { return(fCPUTrackers[iSlice].Row(iRow)); }  //TODO: Should be changed to return only row parameters

	void SetKeepData(bool v) {fKeepData = v;}

	AliHLTTPCCAGPUTracker* GetGPUTracker() {return(fGPUTracker);}
	const AliHLTTPCCATracker& CPUTracker(int iSlice) {return(fUseGPUTracker ? *(fGPUTracker->CPUTracker(iSlice)) : fCPUTrackers[iSlice]);}

private:
  int ExitGPU();

  static const int fgkNSlices = 36;       //* N slices

  char fGPULibAvailable;	//Is the Library with the GPU code available at all?
  char fGPUTrackerAvailable; // Is the GPU Tracker Available?
  char fUseGPUTracker; // use the GPU tracker
  int fGPUDebugLevel;  // debug level for the GPU code
  AliHLTTPCCAGPUTracker* fGPUTracker;	//Pointer to GPU Tracker Object
  void* fGPULib;		//Pointer to GPU Library

  AliHLTTPCCASliceOutput::outputControlStruct* fOutputControl;

  AliHLTTPCCATracker fCPUTrackers[fgkNSlices];
  static const int fCPUSliceCount = 36;

  char fKeepData;		//Keep temporary data and do not free memory imediately, used for Standalone Debug Event Display
  char fGlobalTracking;	//Use global tracking

  AliHLTTPCCATrackerFramework( const AliHLTTPCCATrackerFramework& );
  AliHLTTPCCATrackerFramework &operator=( const AliHLTTPCCATrackerFramework& );

  ClassDef( AliHLTTPCCATrackerFramework, 0 )

};

#endif //ALIHLTTPCCATRACKERFRAMEWORK_H
