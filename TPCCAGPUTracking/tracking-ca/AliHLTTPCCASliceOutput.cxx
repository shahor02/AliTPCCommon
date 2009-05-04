// @(#) $Id$
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

#include "AliHLTTPCCASliceOutput.h"
#include "MemoryAssignmentHelpers.h"

GPUhd() int AliHLTTPCCASliceOutput::EstimateSize( int nOfTracks, int nOfTrackClusters )
{
  // calculate the amount of memory [bytes] needed for the event

  const int kClusterDataSize = sizeof(  int ) + sizeof( unsigned short ) + sizeof( float2 ) + sizeof( float ) + sizeof( UChar_t ) + sizeof( UChar_t );

  return sizeof( AliHLTTPCCASliceOutput ) + sizeof( AliHLTTPCCASliceTrack )*nOfTracks + kClusterDataSize*nOfTrackClusters;
}


GPUhd() void AliHLTTPCCASliceOutput::SetPointers()
{
  // set all pointers

  char *mem = &fMemory[0];
  AssignMemory( fTracks,            mem, fNTracks );
  AssignMemory( fClusterUnpackedYZ, mem, fNTrackClusters );
  AssignMemory( fClusterUnpackedX,  mem, fNTrackClusters );
  AssignMemory( fClusterId,         mem, fNTrackClusters );
  AssignMemory( fClusterPackedYZ,   mem, fNTrackClusters );
  AssignMemory( fClusterRow,        mem, fNTrackClusters );
  AssignMemory( fClusterPackedAmp,  mem, fNTrackClusters );
}
