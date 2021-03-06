// ************************************************************************
// This file is property of and copyright by the ALICE HLT Project        *
// ALICE Experiment at CERN, All rights reserved.                         *
// See cxx source for full Copyright notice                               *
//                                                                        *
//*************************************************************************

#ifndef ALIHLTTPCCAGPUTRACKER_H
#define ALIHLTTPCCAGPUTRACKER_H

#include "AliHLTTPCCADef.h"
#include "AliHLTTPCCASliceOutput.h"
#include <iostream>

class AliHLTTPCCAClusterData;
class AliHLTTPCCASliceOutput;
class AliGPUCAParam;
class AliHLTTPCGMMerger;
class AliHLTTPCCATracker;

//Abstract Interface for GPU Tracker class
class AliHLTTPCCAGPUTracker
{
public:
	AliHLTTPCCAGPUTracker();
	virtual ~AliHLTTPCCAGPUTracker();

	virtual int InitGPU(int forceDeviceID = -1);
	virtual int IsInitialized();
	virtual int Reconstruct(AliHLTTPCCASliceOutput** pOutput, AliHLTTPCCAClusterData* pClusterData);
	virtual int ExitGPU();

	virtual void SetDebugLevel(const int dwLevel, std::ostream* const NewOutFile = NULL);
	virtual int SetGPUTrackerOption(const char* OptionName, int OptionValue);

	virtual double GetTimer(int iSlice, unsigned int i);
	virtual void ResetTimer(int iSlice, unsigned int i);

	virtual int InitializeSliceParam(int iSlice, const AliGPUCAParam *param);
	virtual void SetOutputControl( AliHLTTPCCASliceOutput::outputControlStruct* val);

	virtual const AliHLTTPCCASliceOutput::outputControlStruct* OutputControl() const;
	virtual int GetSliceCount() const;

	virtual int RefitMergedTracks(AliHLTTPCGMMerger* Merger, bool resetTimers);
	virtual char* MergerHostMemory();
	virtual int GPUMergerAvailable();
	virtual const AliHLTTPCCATracker* CPUTracker(int iSlice);

private:
	// disable copy
	AliHLTTPCCAGPUTracker( const AliHLTTPCCAGPUTracker& );
	AliHLTTPCCAGPUTracker &operator=( const AliHLTTPCCAGPUTracker& );
};

#endif //ALIHLTTPCCAGPUTRACKER_H
