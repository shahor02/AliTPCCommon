//-*- Mode: C++ -*-
// ************************************************************************
// This file is property of and copyright by the ALICE HLT Project        *
// ALICE Experiment at CERN, All rights reserved.                         *
// See cxx source for full Copyright notice                               *
//                                                                        *
//*************************************************************************

#ifndef ALIHLTTPCCANEIGHBOURSCLEANER_H
#define ALIHLTTPCCANEIGHBOURSCLEANER_H


#include "AliHLTTPCCADef.h"

MEM_CLASS_PRE() class AliHLTTPCCATracker;

/**
 * @class AliHLTTPCCANeighboursCleaner
 *
 */
class AliHLTTPCCANeighboursCleaner
{
  public:
    MEM_CLASS_PRE() class AliHLTTPCCASharedMemory
    {
        friend class AliHLTTPCCANeighboursCleaner;
      public:
#if !defined(HLTCA_GPUCODE)
        AliHLTTPCCASharedMemory()
            : fIRow( 0 ), fIRowUp( 0 ), fIRowDn( 0 ), fNHits( 0 ) {}
        AliHLTTPCCASharedMemory( const AliHLTTPCCASharedMemory& /*dummy*/ )
            : fIRow( 0 ), fIRowUp( 0 ), fIRowDn( 0 ), fNHits( 0 ) {}
        AliHLTTPCCASharedMemory& operator=( const AliHLTTPCCASharedMemory& /*dummy*/ ) { return *this; }
#endif //!HLTCA_GPUCODE

      protected:
        int fIRow; // current row index
        int fIRowUp; // current row index
        int fIRowDn; // current row index
        int fNHits; // number of hits
    };

    GPUd() static int NThreadSyncPoints() { return 1; }

    GPUd() static void Thread( int /*nBlocks*/, int nThreads, int iBlock, int iThread, int iSync,
                               MEM_LOCAL(GPUsharedref() AliHLTTPCCASharedMemory) &smem,  MEM_CONSTANT(GPUconstant() AliHLTTPCCATracker) &tracker );
};


#endif //ALIHLTTPCCANEIGHBOURSCLEANER_H
