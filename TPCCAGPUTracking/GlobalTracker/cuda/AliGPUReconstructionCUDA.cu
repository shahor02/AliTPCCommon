#include "AliGPUReconstructionCUDA.h"
#include "AliHLTTPCCAGPUTrackerNVCC.h"

#ifdef HAVE_O2HEADERS
#include "ITStrackingCUDA/TrackerTraitsNV.h"
#else
namespace o2 { namespace ITS { class TrackerTraits {}; class TrackerTraitsNV : public TrackerTraits {}; }}
#endif

AliGPUReconstructionCUDA::AliGPUReconstructionCUDA() : AliGPUReconstruction(CUDA)
{
    mTPCTracker.reset(new AliHLTTPCCAGPUTrackerNVCC);
    mITSTrackerTraits.reset(new o2::ITS::TrackerTraitsNV);
}

AliGPUReconstructionCUDA::~AliGPUReconstructionCUDA()
{
    
}

AliGPUReconstruction* AliGPUReconstruction_Create_CUDA()
{
	return new AliGPUReconstructionCUDA;
}