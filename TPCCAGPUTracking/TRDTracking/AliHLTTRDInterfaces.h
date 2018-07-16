// $Id$
//* This file is property of and copyright by the ALICE HLT Project        *
//* ALICE Experiment at CERN, All rights reserved.                         *
//* See cxx source for full Copyright notice                               *

#ifndef ALIHLTTRDINTERFACES_H
#define ALIHLTTRDINTERFACES_H

/**
 * @class this is an interface header for making the TRD tracking portable between O2, AliRoot, and HLT standalone framework
 */

#include "AliHLTExternalTrackParam.h"
#include "AliHLTTRDDef.h"
template <typename T> class trackInterface;
template <typename T> class propagatorInterface;

#ifdef HLTCA_BUILD_ALIROOT_LIB //Interface for AliRoot, build only with AliRoot
#include "AliExternalTrackParam.h"
#include "AliTrackerBase.h"

template <> class trackInterface<AliExternalTrackParam> : public AliExternalTrackParam
{

  public:
    trackInterface<AliExternalTrackParam> () : AliExternalTrackParam() {};
    trackInterface<AliExternalTrackParam> (const trackInterface<AliExternalTrackParam> &param) :
      AliExternalTrackParam(param) {};
    trackInterface<AliExternalTrackParam> (const AliExternalTrackParam &param) = delete;
    trackInterface<AliExternalTrackParam> (const AliHLTExternalTrackParam &param) :
      AliExternalTrackParam()
    {
      float paramTmp[5] = { param.fY, param.fZ, param.fSinPhi, param.fTgl, param.fq1Pt };
      Set( param.fX, param.fAlpha, paramTmp, param.fC );
    }

    // parameter + covariance
    float getX()       const { return GetX(); }
    float getAlpha()   const { return GetAlpha(); }
    float getY()       const { return GetY(); }
    float getZ()       const { return GetZ(); }
    float getSnp()     const { return GetSnp(); }
    float getTgl()     const { return GetTgl(); }
    float getQ2Pt()    const { return GetSigned1Pt(); }
    float getEta()     const { return Eta(); }
    float getPt()      const { return Pt(); }
    float getSigmaY2() const { return GetSigmaY2(); }
    float getSigmaZ2() const { return GetSigmaZ2(); }

    const My_Float *getCov() const { return GetCovariance(); }

    // parameter manipulation
    bool update(const My_Float p[2], const My_Float cov[3])                  { return Update(p, cov); }
    float getPredictedChi2(const My_Float p[2], const My_Float cov[3]) const { return GetPredictedChi2(p, cov); }
    bool rotate(float alpha)                                                 { return Rotate(alpha); }

    void set(float x, float alpha, const float param[5], const float cov[15])  { Set(x, alpha, param, cov); }

    typedef AliExternalTrackParam baseClass;
};

template <> class propagatorInterface<AliTrackerBase> : public AliTrackerBase
{

  public:
    propagatorInterface<AliTrackerBase> () : AliTrackerBase(), fParam(nullptr) {};
    propagatorInterface<AliTrackerBase> (const propagatorInterface<AliTrackerBase>&) = delete;
    propagatorInterface<AliTrackerBase>& operator=(const propagatorInterface<AliTrackerBase>&) = delete;

    bool PropagateToX(float x, float maxSnp, float maxStep) {
      return PropagateTrackToBxByBz(fParam, x, 0.13957, maxStep, false, maxSnp);
    }

    void setTrack(trackInterface<AliExternalTrackParam> *trk) { fParam = trk; }

    float getAlpha() { return (fParam) ? fParam->GetAlpha() : 99999.f; }
    bool update(const My_Float p[2], const My_Float cov[3]) { return (fParam) ? fParam->update(p, cov) : false; }
    float getPredictedChi2(const My_Float p[2], const My_Float cov[3]) { return (fParam) ? fParam->getPredictedChi2(p, cov) : 99999.f; }
    bool rotate(float alpha) { return (fParam) ? fParam->rotate(alpha) : false; }

    trackInterface<AliExternalTrackParam> *fParam;
};

#endif

#ifdef HLTCA_BUILD_O2_LIB //Interface for O2, build only with AliRoot
//TODO: Implement!
#endif

#include "AliHLTTPCGMTrackParam.h"
#include "AliHLTTPCGMPropagator.h"
#include "AliHLTTPCGMMerger.h"
#include "AliHLTTPCCAParam.h"

template <> class trackInterface<AliHLTTPCGMTrackParam> : public AliHLTTPCGMTrackParam
{
  public:
    trackInterface<AliHLTTPCGMTrackParam>() : AliHLTTPCGMTrackParam(), fAlpha(0.f) {};
    trackInterface<AliHLTTPCGMTrackParam>(const AliHLTTPCGMTrackParam &param) = delete;
    trackInterface<AliHLTTPCGMTrackParam>(const trackInterface<AliHLTTPCGMTrackParam> &param) :
      AliHLTTPCGMTrackParam(),
      fAlpha(param.fAlpha)
    {
      SetX(param.getX());
      for (int i=0; i<5; i++) {
        SetPar(i, param.GetPar(i));
      }
      for (int j=0; j<15; j++) {
        SetCov(j, param.GetCov(j));
      }
    }
    trackInterface<AliHLTTPCGMTrackParam>(const AliHLTExternalTrackParam &param) :
      AliHLTTPCGMTrackParam(),
      fAlpha(param.fAlpha)
    {
      SetX(param.fX);
      SetPar(0, param.fY);
      SetPar(1, param.fZ);
      SetPar(2, param.fSinPhi);
      SetPar(3, param.fTgl);
      SetPar(4, param.fq1Pt);
      for (int i=0; i<15; i++) {
        SetCov(i, param.fC[i]);
      }
    };

    float getX()       const { return GetX(); }
    float getAlpha()   const { return fAlpha; }
    float getY()       const { return GetY(); }
    float getZ()       const { return GetZ(); }
    float getSnp()     const { return GetSinPhi(); }
    float getTgl()     const { return GetDzDs(); }
    float getQ2Pt()    const { return GetQPt(); }
    float getEta()     const { return -logf( tanf( 0.5 * (0.5 * M_PI - atanf(getTgl())) ) ); }
    float getPt()      const { return fabs(getQ2Pt()) > 0 ? fabs(1./getQ2Pt()) : 99999.f; }
    float getSigmaY2() const { return GetErr2Y(); }
    float getSigmaZ2() const { return GetErr2Z(); }

    const float *getCov() const { return GetCov(); }

    void setAlpha(float alpha) { fAlpha = alpha; }
    void set(float x, float alpha, const float param[5], const float cov[15]) {
      SetX(x);
      for (int i=0; i<5; i++) {
        SetPar(i, param[i]);
      }
      for (int j=0; j<15; j++) {
        SetCov(j, cov[j]);
      }
      setAlpha(alpha);
    }

    typedef AliHLTTPCGMTrackParam baseClass;

  private:
    float fAlpha;
};

template <> class propagatorInterface<AliHLTTPCGMPropagator> : public AliHLTTPCGMPropagator
{
  public:
    propagatorInterface<AliHLTTPCGMPropagator>() : AliHLTTPCGMPropagator(), param(AliHLTTPCCAParam()), fTrack(nullptr) {
      static const float kRho = 1.025e-3f;
      static const float kRadLen = 29.532f;
      static AliHLTTPCGMMerger fMerger;
      this->SetMaterial( kRadLen, kRho );
      this->SetPolynomialField( fMerger.pField() );
      this->SetMaxSinPhi( HLTCA_MAX_SIN_PHI );
      this->SetToyMCEventsFlag(0);
      this->SetFitInProjections(0);
    };
    propagatorInterface<AliHLTTPCGMPropagator>(const propagatorInterface<AliHLTTPCGMPropagator>&) = delete;
    propagatorInterface<AliHLTTPCGMPropagator>& operator=(const propagatorInterface<AliHLTTPCGMPropagator>&) = delete;
    AliHLTTPCCAParam param;
    void setTrack(trackInterface<AliHLTTPCGMTrackParam> *trk) { SetTrack(trk, trk->getAlpha()); fTrack = trk;}
    bool PropagateToX( float x, float maxSnp, float maxStep ) { return PropagateToXAlpha( x, GetAlpha(), true ) == 0 ? true : false; }
    bool rotate(float alpha) {
      if (RotateToAlpha(alpha) == 0) {
        fTrack->setAlpha(alpha);
        return true;
      }
      return false;
    }
    bool update(const My_Float p[2], const My_Float cov[3]) { return Update(p[0], p[1], HLTCA_ROW_COUNT -1, param, 0, false, false); }
    float getAlpha() { return GetAlpha(); }
    float getPredictedChi2(const My_Float p[2], const My_Float cov[3]) const { return 99999.f; } // TODO not available for HLT tracking?

    trackInterface<AliHLTTPCGMTrackParam> *fTrack;
};

#endif