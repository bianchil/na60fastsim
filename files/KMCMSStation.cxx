#include "KMCMSStation.h"

MeasPlane1D::MeasPlane1D(float phiSector, float dphih, float rmin, float rmax, float phiMeas, float ptc)
  : Sector(phiSector, dphih, rmin, rmax), pitch(ptc), offset(0), measAngle(phiMeas)
{
  // find offset
  float chan = get1DMeasurement(rMin, rMin*TMath::Tan(dphiH));
  chan = TMath::Min(chan, get1DMeasurement(rMin, -rMin*TMath::Tan(dphiH)));
  chan = TMath::Min(chan, get1DMeasurement(rMax,  rMax*TMath::Tan(dphiH)));
  chan = TMath::Min(chan, get1DMeasurement(rMax, -rMax*TMath::Tan(dphiH)));
  offset = chan;
}

KMCMSSector::KMCMSSector(float phiSector, float dphih, float rmin, float rmax,
			 float phiUV, float pitchUV,
			 float phiW, float pitchW, float _sigR, float _sigRPhi)
  : Sector(phiSector, dphih, rmin, rmax),
    stripPlaneU(phiSector, dphih, rmin, rmax, phiUV, pitchUV),
    stripPlaneV(phiSector, dphih, rmin, rmax, -phiUV, pitchUV),
    wirePlaneW(phiSector, dphih, rmin, rmax, phiW, pitchW),
    cUV(TMath::Cos(2.*phiUV)), sUV(TMath::Sin(2.*phiUV)),
    cUW(TMath::Cos(phiUV - phiW)), sUW(TMath::Sin(phiUV - phiW)),
    cVW(TMath::Cos(-phiUV - phiW)), sVW(TMath::Sin(-phiUV - phiW)),
    sigR(_sigR), sigRPhi(_sigRPhi)
{}
    
bool KMCMSSector::getUVW(float x, float y, float& U, float& V, float& W) const
{
  float xl,yl;
  if (!rotateToSector(x,y, xl, yl)) return false;
  U = stripPlaneU.get1DMeasurement(xl,yl);
  V = stripPlaneV.get1DMeasurement(xl,yl);
  W = wirePlaneW.get1DMeasurement(xl,yl);
  return true;
}

void KMCMSSector::clear()
{
  stripPlaneU.clear();
  stripPlaneV.clear();
  wirePlaneW.clear();
}

void KMCMSStation::init(int nsect, const std::vector<float>& r, const std::vector<float>& _phiUV, const std::vector<float>& _pitchUV,
	    const std::vector<float>& _phiW, const std::vector<float>& _pitchW,
	    const std::vector<float>& _sigR, const std::vector<float>& _sigRPhi)
{
  dPhi = TMath::Pi()*2./nsect;
  nSectors = nsect;
  radii = r;
  nRadSegments = r.size()-1;
  for (int ip=0;ip<nsect;ip++) {
    float phiSect = (ip+0.5)*dPhi;
    for (int ir=0;ir<nRadSegments;ir++) {
      sectors.emplace_back(phiSect, 0.5*dPhi, r[ir], r[ir+1], _phiUV[ir], _pitchUV[ir], _phiW[ir], _pitchW[ir], _sigR[ir], _sigRPhi[ir]);
    }
  }
}

int KMCMSStation::getSectorID(float x, float y) const
{
  float phi = TMath::ATan2(y,x);
  if (phi<0) phi += TMath::Pi()*2;
  int id = phi/dPhi;
  if (id>=nSectors) id = nSectors-1;
  id *= nRadSegments;
  const auto& s0 = sectors[id]; // lowest segment of given sector;
  float xl, yl;
  s0.rotateToSector(x,y, xl,yl); // in this frame local X axis goes along bisector of the sector
  if (xl<s0.rMin) return -1; // not in the acceptance
  for (int ir=0;ir<nRadSegments;ir++) {
    if (xl<sectors[id].rMax) {
      return id;
    }
    id++;
  }
  return -1; // missed from top
}

//__________________________________________________________________________
void KMCMSStation::Print(Option_t *opt) const
{
  KMCLayerFwd::Print(opt);
  for (int ir=0;ir<nRadSegments;ir++) {
    const KMCMSSector* sect =  getSector(ir);
    printf("** %d 3x1D stations for %.1f<R<%.1f, coverage in phi: %.2f\n", nSectors, radii[ir],radii[ir+1], dPhi);
    printf("** UV strip planes with angle: %.2f, pitch: %.1f, W plane with angle: %.2f, pitch: %.2f, sigmaR: %.3f, sigmaRPhi: %.3f\n",
	   sect->stripPlaneU.sectAngle.phi0, sect->stripPlaneU.pitch, sect->wirePlaneW.sectAngle.phi0, sect->wirePlaneW.pitch, sect->sigR, sect->sigRPhi);
  }
}

void KMCMSStation::SetRPhiError(bool)
{
  printf("3x1D station has always R/RPhi errors\n");
  fIsRPhiErr = true;;
}

bool KMCMSStation::AddCluster(double x, double y, double z, Int_t id, bool isBG)
{
  if (!isBG) {
    GetMCCluster()->Kill(true);
  }
  // store randomized cluster local coordinates and phi
  signalSectorID = -1;
  KMCMSSector* sect = getSector(x, y);
  if (!sect) {
    return false;
  }
  double r = TMath::Sqrt(x*x + y*y); 
  double rx,ry;
  gRandom->Rannor(rx,ry);  
  double xerr = rx*sect->sigRPhi, yerr = ry*sect->sigR;
  double phi = TMath::ATan2(y,x);
  r += yerr;
  double rphi = xerr;
  double cs = TMath::Cos(phi), sn = TMath::Sin(phi);
  x = r*cs - rphi*sn;
  y = r*sn + rphi*cs;
  int sid = getSectorID(x,y);
  if (sid>=0) { 
    sect = getSector(sid); // after randomization the sector might have changed, determine once more
  }
  float U,V,W;
  bool res = sect->getUVW(x, y, U, V, W);
  if (!res) {
    return false;
  }
  if (!isBG) {
    signalU = U;
    signalV = V;
    signalW = W;
    signalSectorID = sid;
    GetMCCluster()->Kill(false);
    GetMCCluster()->Set(x, y, z, id);
    
  } else {
    sect->stripPlaneU.hits.emplace_back(U, id);
    sect->stripPlaneV.hits.emplace_back(V, id);
    sect->wirePlaneW.hits.emplace_back(W, id);
    AddBgCluster(x, y, z, id);
  }
  return true;
}

void KMCMSStation::PrepareForTracking()
{
  ResetBgClusters();
  if (signalSectorID>=0) { // temporarily add signal channels to common channels pool
    KMCMSSector* sect = getSector(signalSectorID);
    sect->stripPlaneU.hits.emplace_back(signalU, -1);
    sect->stripPlaneV.hits.emplace_back(signalV, -1);
    sect->wirePlaneW.hits.emplace_back(signalW, -1);
  }
  for (size_t is=0;is<sectors.size();is++) {
    KMCMSSector* sect = getSector(is);
    for (size_t iu=0;iu<sect->stripPlaneU.hits.size();iu++) {
      float hu = (sect->stripPlaneU.hits[iu].channel + sect->stripPlaneU.offset)*sect->stripPlaneU.pitch;
      for (size_t iw=0;iw<sect->wirePlaneW.hits.size();iw++) {
	float hw = (sect->wirePlaneW.hits[iw].channel + sect->wirePlaneW.offset)*sect->wirePlaneW.pitch;
	// check crossing of U and W channels
	float tuw = (hw - hu*sect->cUW)/sect->sUW; // this is a point on the U channel line corresponding to crossing with W channel
	// rotate to sector frame
	float xsUW=0, ysUW=0;
	sect->stripPlaneU.local2sector(sect->stripPlaneU.hits[iu].channel, tuw, xsUW, ysUW);
	/*
	float xsUWlab=0, ysUWlab=0;
	sect->sector2Lab(xsUW,ysUW, xsUWlab,ysUWlab);
	printf("UW check for lbl %d %d: %f %f\n", sect->stripPlaneU.hits[iu].label, sect->wirePlaneW.hits[iw].label, xsUWlab,ysUWlab);
	*/
	if (!sect->isInside(xsUW, ysUW)) continue;
	
	for (size_t iv=0;iv<sect->stripPlaneV.hits.size();iv++) { // check crossing of VW channels
	  float hv = (sect->stripPlaneV.hits[iv].channel + sect->stripPlaneV.offset)*sect->stripPlaneV.pitch;
	  float tvw = (hw - hv*sect->cVW)/sect->sVW; // this is a point on the V channel line corresponding to crossing with W channel
	  // rotate to sector frame
	  float xsVW=0, ysVW=0;
	  sect->stripPlaneV.local2sector(sect->stripPlaneV.hits[iv].channel, tvw, xsVW, ysVW);
	  /*
	  float xsVWlab=0, ysVWlab=0;
	  sect->sector2Lab(xsVW,ysVW, xsVWlab,ysVWlab);
	  printf("VW check for lbl %d %d: %f %f\n", sect->stripPlaneV.hits[iv].label, sect->wirePlaneW.hits[iw].label, xsVWlab,ysVWlab);
	  */
	  if (!sect->isInside(xsVW, ysVW)) continue;
	  //
	  // check distance between UW and VW crossings (should be within 3 sigma to be counted as a coincidence)
	  float dx = xsUW-xsVW, dy = ysUW-ysVW;
	  float chi2 = (dx*dx/sect->sigR + dy*dy/sect->sigRPhi)/2;
	  if (chi2>9) continue;
	  // register coincidence
	  float xlab, ylab;
	  sect->sector2Lab(0.5*(xsUW+xsVW), 0.5*(ysUW+ysVW), xlab, ylab);
	  // determine label
	  int lbl = 0;
	  if (sect->stripPlaneU.hits[iu].label==sect->stripPlaneV.hits[iv].label &&
	      sect->stripPlaneU.hits[iu].label==sect->wirePlaneW.hits[iw].label) {
	    lbl = sect->stripPlaneU.hits[iu].label;
	  } else {
	    lbl = 100000 + sect->stripPlaneU.hits[iu].label;
	  }
	  if (lbl==-1) {
	    fClMC.SetX(xlab);
	    fClMC.SetY(ylab);
	  } else {
	    AddBgCluster(xlab,ylab,GetZ(),lbl);
	  }	  
	  printf("coincidence in sector %d: %f %f, chi2=%f, lbl: %d\n", int(is), xlab, ylab, chi2, lbl);
	}
      }
    }

  }
  
  if (signalSectorID>=0) { // temporarily add signal channels to common channels pool
    KMCMSSector* sect = getSector(signalSectorID);
    sect->stripPlaneU.hits.pop_back();
    sect->stripPlaneV.hits.pop_back();
    sect->wirePlaneW.hits.pop_back();
  }
  SortBGClusters();
}
