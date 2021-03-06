#ifndef CLUSTERNATIVEACCESSEXT_H
#define CLUSTERNATIVEACCESSEXT_H

#include "AliHLTTPCCAGPUConfig.h"

#ifdef HAVE_O2HEADERS
#include "DataFormatsTPC/ClusterNative.h"
#else
namespace o2 { namespace TPC { struct ClusterNative {}; struct ClusterNativeAccessFullTPC {ClusterNative* clusters[36][HLTCA_ROW_COUNT]; unsigned int nClusters[36][HLTCA_ROW_COUNT];}; }}
#endif

struct ClusterNativeAccessExt : public o2::TPC::ClusterNativeAccessFullTPC
{
	unsigned int clusterOffset[36][HLTCA_ROW_COUNT];
};

#endif
