#ifndef ALIGPURECONSTRUCTIONCONVERT_H
#define ALIGPURECONSTRUCTIONCONVERT_H

#include "AliHLTTPCCAClusterData.h"
#include <memory>

struct ClusterNativeAccessExt;
namespace ali_tpc_common { namespace tpc_fast_transformation { class TPCFastTransform; }}
using TPCFastTransform = ali_tpc_common::tpc_fast_transformation::TPCFastTransform;

class AliGPUReconstructionConvert
{
public:
	constexpr static unsigned int NSLICES = 36;
	static void ConvertNativeToClusterData(ClusterNativeAccessExt* native, std::unique_ptr<AliHLTTPCCAClusterData::Data[]>* clusters, unsigned int* nClusters, const TPCFastTransform* transform, int continuousMaxTimeBin = 0);
};

#endif
