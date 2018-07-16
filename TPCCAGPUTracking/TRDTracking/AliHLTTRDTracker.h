#ifndef ALIHLTTRDTRACKER_H
#define ALIHLTTRDTRACKER_H
/* Copyright(c) 2007-2009, ALICE Experiment at CERN, All rights reserved. *
 * See cxx source for full Copyright notice                               */

 #include "AliHLTTRDDef.h"
class AliHLTTRDTrackerDebug;
class AliHLTTRDTrackletWord;
class AliHLTTRDGeometry;
class AliExternalTrackParam;
class AliMCEvent;

//-------------------------------------------------------------------------
class AliHLTTRDTracker {
 public:

  AliHLTTRDTracker();
  AliHLTTRDTracker(const AliHLTTRDTracker &tracker) = delete;
  AliHLTTRDTracker & operator=(const AliHLTTRDTracker &tracker) = delete;
  ~AliHLTTRDTracker();

  enum EHLTTRDTracker {
    kNLayers = 6,
    kNStacks = 5,
    kNSectors = 18,
    kNChambers = 540
  };

  // struct to hold the information on the space points
  struct AliHLTTRDSpacePointInternal {
    float fR;                 // x position (7mm above anode wires)
    float fX[2];              // y and z position (sector coordinates)
    My_Float fCov[3];         // sigma_y^2, sigma_yz, sigma_z^2
    float fDy;                // deflection over drift length
    int fId;                  // index
    int fLabel[3];            // MC labels
    unsigned short fVolumeId; // basically derived from TRD chamber number
  };

  struct Hypothesis {
    float fChi2;
    int fLayers;
    int fCandidateId;
    int fTrackletId;
  };

  static bool Hypothesis_Sort(const Hypothesis &lhs, const Hypothesis &rhs) {
    if (lhs.fLayers < 1 || rhs.fLayers < 1) {
      return ( lhs.fChi2 < rhs.fChi2 );
    }
    else {
      return ( (lhs.fChi2/lhs.fLayers) < (rhs.fChi2/rhs.fLayers) );
    }
  }

  void Init();
  void Reset();
  void StartLoadTracklets(const int nTrklts);
  void LoadTracklet(const AliHLTTRDTrackletWord &tracklet);
  void DoTracking(HLTTRDTrack *tracksTPC, int *tracksTPClab, int nTPCtracks, int *tracksTPCnTrklts = 0x0, int *tracksTRDlabel = 0x0);
  bool CalculateSpacePoints();
  bool FollowProlongation(HLTTRDPropagator *prop, HLTTRDTrack *t, int nTPCtracks);
  int GetDetectorNumber(const float zPos, const float alpha, const int layer) const;
  bool AdjustSector(HLTTRDPropagator *prop, HLTTRDTrack *t, const int layer) const;
  int GetSector(float alpha) const;
  float GetAlphaOfSector(const int sec) const;
  float GetRPhiRes(float snp) const { return (0.04f*0.04f+0.33f*0.33f*(snp-0.126f)*(snp-0.126f)); } // parametrization obtained from track-tracklet residuals
  void RecalcTrkltCov(const int trkltIdx, const float tilt, const float snp, const float rowSize);
  void CountMatches(const int trackID, std::vector<int> *matches) const;
  void CheckTrackRefs(const int trackID, bool *findableMC) const;
  void FindChambersInRoad(const HLTTRDTrack *t, const float roadY, const float roadZ, const int iLayer, std::vector<int> &det, const float zMax, const float alpha) const;
  bool IsGeoFindable(const HLTTRDTrack *t, const int layer, const float alpha) const;

  // settings
  void SetMCEvent(AliMCEvent* mc)       { fMCEvent = mc;}
  void EnableDebugOutput()              { fDebugOutput = true; }
  void SetPtThreshold(float minPt)      { fMinPt = minPt; }
  void SetMaxEta(float maxEta)          { fMaxEta = maxEta; }
  void SetChi2Threshold(float chi2)     { fMaxChi2 = chi2; }
  void SetChi2Penalty(float chi2)       { fChi2Penalty = chi2; }
  void SetMaxMissingLayers(int ly)      { fMaxMissingLy = ly; }
  void SetNCandidates(int n)            { if (!fIsInitialized) fNCandidates = n; else Error("SetNCandidates", "Cannot change fNCandidates after initialization"); }

  AliMCEvent * GetMCEvent()   const { return fMCEvent; }
  bool  GetIsDebugOutputOn()  const { return fDebugOutput; }
  float GetPtThreshold()      const { return fMinPt; }
  float GetMaxEta()           const { return fMaxEta; }
  float GetChi2Threshold()    const { return fMaxChi2; }
  float GetChi2Penalty()      const { return fChi2Penalty; }
  int   GetMaxMissingLayers() const { return fMaxMissingLy; }
  int   GetNCandidates()      const { return fNCandidates; }
  void  PrintSettings() const;

  // output
  HLTTRDTrack *Tracks()                       const { return fTracks;}
  int NTracks()                               const { return fNTracks;}
  AliHLTTRDSpacePointInternal *SpacePoints()  const { return fSpacePoints; }

 protected:

  static const float fgkX0[kNLayers];        // default values of anode wires
  static const float fgkXshift;              // online tracklets evaluated above anode wire

  float *fR;                                  // rough radial position of each TRD layer
  bool fIsInitialized;                        // flag is set upon initialization
  HLTTRDTrack *fTracks;                       // array of trd-updated tracks
  int fNCandidates;                           // max. track hypothesis per layer
  int fNTracks;                               // number of TPC tracks to be matched
  int fNEvents;                               // number of processed events
  AliHLTTRDTrackletWord *fTracklets;          // array of all tracklets, later sorted by HCId
  int fNtrackletsMax;                         // max number of tracklets
  int fNTracklets;                            // total number of tracklets in event
  int *fNtrackletsInChamber;                  // number of tracklets in each chamber
  int *fTrackletIndexArray;                   // index of first tracklet for each chamber
  Hypothesis *fHypothesis;                    // array with multiple track hypothesis
  HLTTRDTrack *fCandidates;                   // array of tracks for multiple hypothesis tracking
  AliHLTTRDSpacePointInternal *fSpacePoints;  // array with tracklet coordinates in global tracking frame
  AliHLTTRDGeometry *fGeo;                    // TRD geometry
  bool fDebugOutput;                          // store debug output
  float fMinPt;                               // min pt of TPC tracks for tracking
  float fMaxEta;                              // TPC tracks with higher eta are ignored
  float fMaxChi2;                             // max chi2 for tracklets
  int fMaxMissingLy;                          // max number of missing layers per track
  float fChi2Penalty;                         // chi2 added to the track for no update
  float fZCorrCoefNRC;                        // tracklet z-position depends linearly on track dip angle
  int fNhypothesis;                           // number of track hypothesis per layer
  std::vector<int> fMaskedChambers;           // vector holding bad TRD chambers
  AliMCEvent* fMCEvent;                       //! externaly supplied optional MC event
  AliHLTTRDTrackerDebug *fDebug;              // debug output

};

#endif