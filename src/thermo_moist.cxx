/*
 * MicroHH
 * Copyright (c) 2011-2013 Chiel van Heerwaarden
 * Copyright (c) 2011-2013 Thijs Heus
 *
 * This file is part of MicroHH
 *
 * MicroHH is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * MicroHH is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with MicroHH.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <sstream>
#include <algorithm>
#include "grid.h"
#include "fields.h"
#include "thermo_moist.h"
#include "diff_les2s.h"
#include "defines.h"
#include "model.h"
#include "stats.h"
#include "master.h"
#include "cross.h"

#define rd 287.04
#define rv 461.5
#define ep rd/rv
#define cp 1005
#define lv 2.5e6
#define rhow    1.e3
#define tmelt   273.15
#define p0 1.e5
#define grav 9.81

#define ex1 2.85611940298507510698e-06
#define ex2 -1.02018879928714644313e-11
#define ex3 5.82999832046362073082e-17
#define ex4 -3.95621945728655163954e-22
#define ex5 2.93898686274077761686e-27
#define ex6 -2.30925409555411170635e-32
#define ex7 1.88513914720731231360e-37

#define at 17.27
#define bt 35.86
#define es0 610.78

#define c0 0.6105851e+03
#define c1 0.4440316e+02
#define c2 0.1430341e+01
#define c3 0.2641412e-01
#define c4 0.2995057e-03
#define c5 0.2031998e-05
#define c6 0.6936113e-08
#define c7 0.2564861e-11
#define c8 -.3704404e-13

#define NO_OFFSET 0.

cthermo_moist::cthermo_moist(cmodel *modelin) : cthermo(modelin)
{
  swthermo = "moist";

  allocated = false;
}

cthermo_moist::~cthermo_moist()
{
  if(allocated)
  {
    delete[] thl0;
    delete[] qt0;

    delete[] thvref;
    delete[] thvrefh;
    delete[] exner;
    delete[] exnerh;
    delete[] press;
    delete[] pressh;
  }
}

int cthermo_moist::readinifile(cinput *inputin)
{
  int nerror = 0;
  nerror += inputin->getItem(&ps    , "thermo", "ps"    , "");
  // nerror += inputin->getItem(&thvref, "thermo", "thvref", "");

  nerror += fields->initpfld("s", "Liquid water potential temperature", "K");
  nerror += inputin->getItem(&fields->sp["s"]->visc, "fields", "svisc", "s");
  nerror += fields->initpfld("qt", "Total water mixing ratio", "kg kg-1");
  nerror += inputin->getItem(&fields->sp["qt"]->visc, "fields", "svisc", "qt");

  // Read list of cross sections
  nerror += inputin->getList(&crosslist , "thermo", "crosslist" , "");

  nerror += inputin->getItem(&swupdatebasestate, "thermo", "swupdatebasestate", "1");

  return (nerror > 0);
}

int cthermo_moist::init()
{
  stats = model->stats;

  thl0    = new double[grid->kcells];
  qt0     = new double[grid->kcells];

  thvref  = new double[grid->kcells];
  thvrefh = new double[grid->kcells];
  exner   = new double[grid->kcells];
  exnerh  = new double[grid->kcells];
  press   = new double[grid->kcells];
  pressh  = new double[grid->kcells];
  
  // fields for anelastic solver
  //thref   = new double[grid->kcells];
  //qtref   = new double[grid->kcells];
  //pref    = new double[grid->kcells];
  //exner   = new double[grid->kcells];
  //threfh  = new double[grid->kcells];
  //qtrefh  = new double[grid->kcells];
  //prefh   = new double[grid->kcells];
  //exnerh  = new double[grid->kcells];
  // Hydrostatic pressure calculations
  //pmn     = new double[grid->kcells];  // hydrostatic pressure (full levels)
  //pmnh    = new double[grid->kcells];  // hydrostatic pressure (half levels)
  //ql      = new double[grid->kcells];  // liquid water (full levles)

  return 0;
}

int cthermo_moist::create(cinput *inputin)
{
  // CALCULATE THE BASE PROFILES
  // take the initial profile as the reference
  if(inputin->getProf(&thl0[grid->kstart], "s", grid->kmax))
    return 1;
  if(inputin->getProf(&qt0[grid->kstart], "qt", grid->kmax))
    return 1;

  int kstart = grid->kstart;
  int kend   = grid->kend;
  double thl0s, qt0s, thl0t, qt0t;

  // Calculate surface and model top values thl0 and qt
  thl0s = thl0[kstart] - grid->z[kstart]*(thl0[kstart+1]-thl0[kstart])*grid->dzhi[kstart+1];
  qt0s  = qt0[kstart]  - grid->z[kstart]*(qt0[kstart+1] -qt0[kstart] )*grid->dzhi[kstart+1];
  thl0t = thl0[kend-1] + (grid->zh[kend]-grid->z[kend-1])*(thl0[kend-1]-thl0[kend-2])*grid->dzhi[kend-1];
  qt0t  = qt0[kend-1]  + (grid->zh[kend]-grid->z[kend-1])*(qt0[kend-1]- qt0[kend-2] )*grid->dzhi[kend-1];

  // Set the ghost cells for the reference temperature
  thl0[kstart-1]  = 2.*thl0s - thl0[kstart];
  thl0[kend]      = 2.*thl0t - thl0[kend-1];
  qt0[kstart-1]   = 2.*qt0s  - qt0[kstart];
  qt0[kend]       = 2.*qt0t  - qt0[kend-1];

  // Set the reference thv and density:
  calchydropres_2nd(press, pressh, fields->rhoref, fields->rhorefh, thvref, thvrefh, exner, exnerh, thl0, qt0);

  //qtref[kstart-1] = 2.*qtrefh[kstart] - qtref[kstart];
  //qtref[kend]     = 2.*qtrefh[kend]   - qtref[kend-1];

  // extrapolate the profile to get the bottom value
  //threfh[kstart] = thref[kstart] - grid->z[kstart]*(thref[kstart+1]-thref[kstart])*grid->dzhi[kstart+1];
 // qtrefh[kstart] = qtref[kstart] - grid->z[kstart]*(qtref[kstart+1]-qtref[kstart])*grid->dzhi[kstart+1];

  // extrapolate the profile to get the top value
  //threfh[kend] = thref[kend-1] + (grid->zh[kend]-grid->z[kend-1])*(thref[kend-1]-thref[kend-2])*grid->dzhi[kend-1];
  //qtrefh[kend] = qtref[kend-1] + (grid->zh[kend]-grid->z[kend-1])*(qtref[kend-1]-qtref[kend-2])*grid->dzhi[kend-1];

  // set the ghost cells for the reference temperature
  //thref[kstart-1] = 2.*threfh[kstart] - thref[kstart];
  //thref[kend]     = 2.*threfh[kend]   - thref[kend-1];
  //qtref[kstart-1] = 2.*qtrefh[kstart] - qtref[kstart];
  //qtref[kend]     = 2.*qtrefh[kend]   - qtref[kend-1];

  // interpolate the reference temperature profile
  //for(int k=grid->kstart+1; k<grid->kend; ++k)
  //{
  //  threfh[k] = 0.5*(thref[k-1] + thref[k]);
  //  qtrefh[k] = 0.5*(qtref[k-1] + qtref[k]);
  //}


  // TEST TEST TEST -> put exner() and rho() calculatios in calchydropress later....
  //for(int k=grid->kstart; k<grid->kend; ++k)
  //{
  //  exner[k] = std::pow(pref[k]/p0, rd/cp);
  //  qltemp = calcql(thref[k],qtref[k],pref[k],exner[k]);
  //  thref[k] = (thref[k] + lv*qltemp/(cp*exner[k])) * (1. - (1. - rv/rd)*qtref[k] - rv/rd*qltemp);
  //  //thref[k] = (thref[k] * (1. - (1. - rv/rd)*qtref[k]));
  //  fields->rhoref[k] = pref[k] / (rd*exner[k]*thref[k]);
  //}
 
  //for(int k=grid->kstart; k<grid->kend+1; ++k)
  //{
  //  exnerh[k] = std::pow(prefh[k]/p0, rd/cp);
  //  qltemp = calcql(threfh[k],qtrefh[k],prefh[k],exnerh[k]);
  //  threfh[k] = (threfh[k] + lv*qltemp/(cp*exnerh[k])) * (1. - (1. - rv/rd)*qtrefh[k] - rv/rd*qltemp);
  //  //threfh[k] = (threfh[k] * (1. - (1. - rv/rd)*qtrefh[k]));
  //  fields->rhorefh[k] = prefh[k] / (rd*exnerh[k]*threfh[k]);
  //}
 
  //exner[kstart-1] = 2.*exnerh[kstart] - exner[kstart];
  //fields->rhoref[kstart-1] = 2.*fields->rhorefh[kstart] - fields->rhoref[kstart];
  //exner[kend] = 2.*exnerh[kend] - exner[kend-1];
  //fields->rhoref[kend] = 2.*fields->rhorefh[kend] - fields->rhoref[kend-1];

  //// ANELASTIC
  //// calculate the base state pressure and density
  //for(int k=grid->kstart; k<grid->kend; ++k)
  //{
  //  pref [k] = ps*std::exp(-grav/(rd*thref[k])*grid->z[k]);
  //  exner[k] = std::pow(pref[k]/ps, rd/cp);

  //  // set the base density for the entire model
  //  fields->rhoref[k] = pref[k] / (rd*exner[k]*thref[k]);
  //}

  //for(int k=grid->kstart; k<grid->kend+1; ++k)
  //{
  //  prefh [k] = ps*std::exp(-grav/(rd*threfh[k])*grid->zh[k]);
  //  exnerh[k] = std::pow(prefh[k]/ps, rd/cp);

  //  // set the base density for the entire model
  //  fields->rhorefh[k] = prefh[k] / (rd*exnerh[k]*threfh[k]);
  //}

  //// set the ghost cells for the reference variables
  //// CvH for now in 2nd order
  //pref [kstart-1] = 2.*prefh [kstart] - pref [kstart];
  //exner[kstart-1] = 2.*exnerh[kstart] - exner[kstart];
  //fields->rhoref[kstart-1] = 2.*fields->rhorefh[kstart] - fields->rhoref[kstart];

  //pref [kend] = 2.*prefh [kend] - pref [kend-1];
  //exner[kend] = 2.*exnerh[kend] - exner[kend-1];
  //fields->rhoref[kend] = 2.*fields->rhorefh[kend] - fields->rhoref[kend-1];

  //for(int k=0; k<grid->kcells; ++k)
  //  std::printf("%E, %E, %E, %E, %E, %E\n", grid->z[k], thl0[k], qt0[k], exner[k], press[k], fields->rhoref[k]);

  //printf("---");

  for(int k=0; k<grid->kcells; ++k)
    std::printf("%E, %E, %E, %E\n", grid->zh[k], exnerh[k], pressh[k], fields->rhorefh[k]);

  // CONTINUE OLD ROUTINE

  int nerror = 0;
  
  // Enable automated calculation of horizontally averaged fields
  fields->setcalcprofs(true);

  allocated = true;

  // add variables to the statistics
  if(stats->getsw() == "1")
  {
    stats->addprof("b", "Buoyancy", "m s-2", "z");
    for(int n=2; n<5; ++n)
    {
      std::stringstream ss;
      ss << n;
      std::string sn = ss.str();
      stats->addprof("b"+sn, "Moment " +sn+" of the buoyancy", "(m s-2)"+sn,"z");
    }

    stats->addprof("bgrad", "Gradient of the buoyancy", "m s-3", "zh");
    stats->addprof("bw"   , "Turbulent flux of the buoyancy", "m2 s-3", "zh");
    stats->addprof("bdiff", "Diffusive flux of the buoyancy", "m2 s-3", "zh");
    stats->addprof("bflux", "Total flux of the buoyancy", "m2 s-3", "zh");

    stats->addprof("ql", "Liquid water mixing ratio", "kg kg-1", "z");
    stats->addprof("cfrac", "Cloud fraction", "-","z");

    stats->addtseries("lwp", "Liquid water path", "kg m-2");
    stats->addtseries("ccover", "Projected cloud cover", "-");
  }

  // Cross sections (isn't there an easier way to populate this list?)
  allowedcrossvars.push_back("b");
  allowedcrossvars.push_back("bbot");
  allowedcrossvars.push_back("bfluxbot");
  if(grid->swspatialorder == "4")
    allowedcrossvars.push_back("blngrad");
  allowedcrossvars.push_back("ql");
  allowedcrossvars.push_back("qlpath");

  // Check input list of cross variables (crosslist)
  std::vector<std::string>::iterator it=crosslist.begin();
  while(it != crosslist.end())
  {
    if(!std::count(allowedcrossvars.begin(),allowedcrossvars.end(),*it))
    {
      if(master->mpiid == 0) std::printf("WARNING field %s in [thermo][crosslist] is illegal\n", it->c_str());
      it = crosslist.erase(it);  // erase() returns iterator of next element..
    }
    else
      ++it;
  }

  // Sort crosslist to group ql and b variables
  std::sort(crosslist.begin(),crosslist.end());

  return nerror;
}

int cthermo_moist::exec()
{
  int kk,nerror;
  kk = grid->icells*grid->jcells;

  nerror = 0;

  // extend later for gravity vector not normal to surface
  if(grid->swspatialorder == "2")
  {
    if(swupdatebasestate)
      calchydropres_2nd(press, pressh, fields->rhoref, fields->rhorefh, thvref, thvrefh, exner, exnerh, fields->s["s"]->datamean, fields->s["qt"]->datamean);
    calcbuoyancytend_2nd(fields->wt->data, fields->s["s"]->data, fields->s["qt"]->data, press, pressh,
                         &fields->s["tmp2"]->data[0*kk], &fields->s["tmp2"]->data[1*kk], &fields->s["tmp2"]->data[2*kk],
                         thvrefh);
  }
  else if(grid->swspatialorder == "4")
  {
    //calchydropres_4th(press,fields->s["s"]->data,fields->s["s"]->datamean,fields->s["qt"]->data,fields->s["qt"]->datamean);
    calcbuoyancytend_4th(fields->wt->data, fields->s["s"]->data, fields->s["qt"]->data, press,
                         &fields->s["tmp2"]->data[0*kk], &fields->s["tmp2"]->data[1*kk], &fields->s["tmp2"]->data[2*kk],
                         thvrefh);
  }

  return (nerror>0);
}

int cthermo_moist::execstats()
{
  // calc the buoyancy and its surface flux for the profiles
  calcbuoyancy(fields->s["tmp1"]->data, fields->s["s"]->data, fields->s["qt"]->data, press, fields->s["tmp2"]->data, thvref);
  calcbuoyancyfluxbot(fields->s["tmp1"]->datafluxbot, fields->s["s"]->databot, fields->s["s"]->datafluxbot, fields->s["qt"]->databot, fields->s["qt"]->datafluxbot, thvrefh);

  // mean
  stats->calcmean(fields->s["tmp1"]->data, stats->profs["b"].data, NO_OFFSET);

  // moments
  for(int n=2; n<5; ++n)
  {
    std::stringstream ss;
    ss << n;
    std::string sn = ss.str();
    stats->calcmoment(fields->s["tmp1"]->data, stats->profs["b"].data, stats->profs["b"+sn].data, n, 0);
  }

  // calculate the gradients
  if(grid->swspatialorder == "2")
    stats->calcgrad_2nd(fields->s["tmp1"]->data, stats->profs["bgrad"].data, grid->dzhi);
  if(grid->swspatialorder == "4")
    stats->calcgrad_4th(fields->s["tmp1"]->data, stats->profs["bgrad"].data, grid->dzhi4);

  // calculate turbulent fluxes
  if(grid->swspatialorder == "2")
    stats->calcflux_2nd(fields->s["tmp1"]->data, fields->w->data, stats->profs["bw"].data, fields->s["tmp2"]->data, 0, 0);
  if(grid->swspatialorder == "4")
    stats->calcflux_4th(fields->s["tmp1"]->data, fields->w->data, stats->profs["bw"].data, fields->s["tmp2"]->data, 0, 0);

  // calculate diffusive fluxes
  if(model->diff->getname() == "les2s")
  {
    cdiff_les2s *diffptr = static_cast<cdiff_les2s *>(model->diff);
    stats->calcdiff_2nd(fields->s["tmp1"]->data, fields->s["evisc"]->data, stats->profs["bdiff"].data, grid->dzhi, fields->s["tmp1"]->datafluxbot, fields->s["tmp1"]->datafluxtop, diffptr->tPr);
  }
  else
  {
    // take the diffusivity of temperature for that of moisture
    stats->calcdiff_4th(fields->s["tmp1"]->data, stats->profs["bdiff"].data, grid->dzhi4, fields->s["th"]->visc);
  }

  // calculate the total fluxes
  stats->addfluxes(stats->profs["bflux"].data, stats->profs["bw"].data, stats->profs["bdiff"].data);

  // calculate the liquid water stats
  calcqlfield(fields->s["tmp1"]->data, fields->s["s"]->data, fields->s["qt"]->data, press);
  stats->calcmean (fields->s["tmp1"]->data, stats->profs["ql"].data, NO_OFFSET);
  stats->calccount(fields->s["tmp1"]->data, stats->profs["cfrac"].data, 0.);

  stats->calccover(fields->s["tmp1"]->data, &stats->tseries["ccover"].data, 0.);
  stats->calcpath(fields->s["tmp1"]->data, &stats->tseries["lwp"].data);

  return 0;
}

int cthermo_moist::execcross()
{
  int nerror = 0;

  // With one additional temp field, we wouldn't have to re-calculate the ql or b field for simple,lngrad,path, etc.
  for(std::vector<std::string>::iterator it=crosslist.begin(); it<crosslist.end(); ++it)
  {
    if(*it == "b" or *it == "ql")
    {
      getthermofield(fields->s["tmp1"], fields->s["tmp2"], *it);
      nerror += model->cross->crosssimple(fields->s["tmp1"]->data, fields->s["tmp2"]->data, *it);
    }
    else if(*it == "blngrad")
    {
      getthermofield(fields->s["tmp1"], fields->s["tmp2"], "b");
      // Note: tmp1 twice used as argument -> overwritten in crosspath()
      nerror += model->cross->crosslngrad(fields->s["tmp1"]->data, fields->s["tmp2"]->data, fields->s["tmp1"]->data, grid->dzi4, *it);
    }
    else if(*it == "qlpath")
    {
      getthermofield(fields->s["tmp1"], fields->s["tmp2"], "ql");
      // Note: tmp1 twice used as argument -> overwritten in crosspath()
      nerror += model->cross->crosspath(fields->s["tmp1"]->data, fields->s["tmp2"]->data, fields->s["tmp1"]->data, "qlpath");
    }
    else if(*it == "bbot" or *it == "bfluxbot")
    {
      getbuoyancysurf(fields->s["tmp1"]);
      if(*it == "bbot")
        nerror += model->cross->crossplane(fields->s["tmp1"]->databot, fields->s["tmp1"]->data, "bbot");
      else if(*it == "bfluxbot")
        nerror += model->cross->crossplane(fields->s["tmp1"]->datafluxbot, fields->s["tmp1"]->data, "bfluxbot");
    }
  }  

  return nerror; 
}

int cthermo_moist::checkthermofield(std::string name)
{
  if(name == "b" || name == "ql")
    return 0;
  else
    return 1;
}

int cthermo_moist::getthermofield(cfield3d *fld, cfield3d *tmp, std::string name)
{
  // calculate the hydrostatic pressure
  if(swupdatebasestate)
  {
    if(grid->swspatialorder == "2")
      calchydropres_2nd(press, pressh, fields->rhoref, fields->rhorefh, thvref, thvrefh, exner, exnerh, fields->s["s"]->datamean, fields->s["qt"]->datamean);
    else if(grid->swspatialorder == "4")
      calchydropres_4th(press, fields->s["s"]->data,fields->s["s"]->datamean,fields->s["qt"]->data,fields->s["qt"]->datamean);
  }

  if(name == "b")
    calcbuoyancy(fld->data, fields->s["s"]->data, fields->s["qt"]->data, press, tmp->data, thvref);
  else if(name == "ql")
    calcqlfield(fld->data, fields->s["s"]->data, fields->s["qt"]->data, press);
  else if(name == "N2")
    calcN2(fld->data, fields->s["s"]->data, grid->dzi, thvref);
  else
    return 1;

  return 0;
}

int cthermo_moist::getbuoyancysurf(cfield3d *bfield)
{
  calcbuoyancybot(bfield->data         , bfield->databot,
                  fields->s["s" ]->data, fields->s["s" ]->databot,
                  fields->s["qt"]->data, fields->s["qt"]->databot,
                  thvref, thvrefh);
  calcbuoyancyfluxbot(bfield->datafluxbot, fields->s["s"]->databot, fields->s["s"]->datafluxbot, fields->s["qt"]->databot, fields->s["qt"]->datafluxbot, thvrefh);
  return 0;
}

int cthermo_moist::getbuoyancyfluxbot(cfield3d *bfield)
{
  calcbuoyancyfluxbot(bfield->datafluxbot, fields->s["s"]->databot, fields->s["s"]->datafluxbot, fields->s["qt"]->databot, fields->s["qt"]->datafluxbot, thvrefh);
  return 0;
}

int cthermo_moist::getprogvars(std::vector<std::string> *list)
{
  list->push_back("s");
  list->push_back("qt");

  return 0;
}

/**
 * This function calculates the hydrostatic pressure
 * Solves: dpi/dz=-g/thv with pi=cp*(p/p0)**(rd/cp)
 * @return Returns 1 on error, 0 otherwise.
 */
int cthermo_moist::calchydropres_2nd(double * restrict pmn,     double * restrict pmnh,
                                     double * restrict dn,      double * restrict dnh,
                                     double * restrict thv,     double * restrict thvh,
                                     double * restrict ex,      double * restrict exh,
                                     double * restrict thlmean, double * restrict qtmean)
{
  int kstart,kend;
  double ssurf,qtsurf,stop,qttop,ptop,ql,si,qti,qli,thvt;
  double rdcp = rd/cp;

  kstart = grid->kstart;
  kend = grid->kend;

  ssurf  = interp2(thlmean[kstart-1], thlmean[kstart]);
  stop   = interp2(thlmean[kend-1],   thlmean[kend]);
  qtsurf = interp2(qtmean[kstart-1],  qtmean[kstart]);
  qttop  = interp2(qtmean[kend-1],    qtmean[kend]);

  // Calculate surface (half=kstart) values (unsaturated)
  thvh[kstart] = ssurf*(1.+(rv/rd-1)*qtsurf);
  pmnh[kstart] = ps;
  exh[kstart]  = exn(ps);
  dnh[kstart]  = ps / (rd * exh[kstart] * thvh[kstart]);

  // First full grid level pressure
  pmn[kstart] = pow((pow(ps,rdcp) - grav * pow(p0,rdcp) * grid->z[kstart] / (cp * thvh[kstart])),(1./rdcp)); 

  // to-do: re-order loop to prevent all the k-1's
  for(int k=kstart+1; k<kend+1; k++)
  {
    // 1. Calculate values at full level below zh[k] 
    ex[k-1]  = exn(pmn[k-1]);
    ql       = calcql(thlmean[k-1],qtmean[k-1],pmn[k-1],ex[k-1]); 
    thv[k-1] = (thlmean[k-1] + lv*ql/(cp*ex[k-1])) * (1. - (1. - rv/rd)*qtmean[k-1] - rv/rd*ql); 
    dn[k-1]  = pmn[k-1] / (rd * ex[k-1] * thv[k-1]);
 
    // 2. Calculate half level pressure at zh[k] using values at z[k-1]
    pmnh[k]  = pow((pow(pmnh[k-1],rdcp) - grav * pow(p0,rdcp) * grid->dz[k-1] / (cp * thv[k-1])),(1./rdcp));

    // 3. Interpolate conserved variables to zh[k] and calculate virtual temp and ql
    si       = interp2(thlmean[k-1],thlmean[k]);
    qti      = interp2(qtmean[k-1],qtmean[k]);
    exh[k]   = exn(pmnh[k]);
    qli      = calcql(si,qti,pmnh[k],exh[k]);
    thvh[k]  = (si + lv*qli/(cp*exh[k])) * (1. - (1. - rv/rd)*qti - rv/rd*qli); 
    dnh[k]   = pmnh[k] / (rd * exh[k] * thvh[k]); 

    // 4. Calculate full level pressure at z[k]
    pmn[k]   = pow((pow(pmn[k-1],rdcp) - grav * pow(p0,rdcp) * grid->dzh[k] / (cp * thvh[k])),(1./rdcp)); 
  }

  // Fill bottom and top full level ghost cells 
  pmn[kstart-1] = 2.*pmnh[kstart] - pmn[kstart];
  pmn[kend]     = 2.*pmnh[kend]   - pmn[kend-1];
  ex[kstart-1]  = exn(pmn[kstart-1]);
  ex[kend]      = exn(pmn[kend]);
  dn[kstart-1]  = 2.*dnh[kstart]  - dn[kstart];
  dn[kend]      = 2.*dnh[kend]    - dn[kend-1];
  thv[kstart-1] = 2.*thvh[kstart] - thv[kstart];
  thv[kend]     = 2.*thvh[kend]   - thv[kend-1];

  //for(int k=0; k<grid->kcells; ++k)
  //  std::printf("%i, %E, %E, %E, %E, %E\n", k, grid->z[k], ex[k], press[k], dn[k], thv[k]);
  //printf("--half--\n");
  //for(int k=0; k<grid->kcells; ++k)
  //  std::printf("%i, %E, %E, %E, %E, %E\n", k, grid->zh[k], exh[k], pressh[k], dnh[k], thvh[k]);
  //exit(1);

  return 0;
}



/**
 * This function calculates the hydrostatic pressure
 * Solves: dpi/dz=-g/thv with pi=cp*(p/p0)**(rd/cp)
 * @param pmn Pointer to hydrostatic pressure array
 * @param s,smean,qt,qtmean .... 
 * @return Returns 1 on error, 0 otherwise.
 */
//int cthermo_moist::calchydropres_2nd(double * restrict pmn, double * restrict s, double * restrict smean,
//                                 double * restrict qt, double * restrict qtmean)
//{
//  int kstart,kend;
//  double thv,ssurf,qtsurf,stop,qttop,ptop;
//  double rdcp = rd/cp;
//
//  kstart = grid->kstart;
//  kend = grid->kend;
//
//  ssurf  = interp2(smean[kstart-1], smean[kstart]);
//  stop   = interp2(smean[kend-1],   smean[kend]);
//  qtsurf = interp2(qtmean[kstart-1],qtmean[kstart]);
//  qttop  = interp2(qtmean[kend-1],  qtmean[kend]);
//
//  // Calculate lowest full level (kstart) from surface values p,s,qt
//  thv = ssurf*(1.+(rv/rd-1)*qtsurf);
//  pmn[kstart] = pow((pow(ps,rdcp) - grav * pow(p0,rdcp) * grid->z[kstart] / (cp * thv)),(1./rdcp)); 
//
//  for(int k=kstart+1; k<kend; k++)
//  {
//    thv = interp2(smean[k-1],smean[k])*(1.+(rv/rd-1.)*interp2(qtmean[k-1],qtmean[k]));   // BvS: assume no ql for now..
//    pmn[k] = pow((pow(pmn[k-1],rdcp) - grav * pow(p0,rdcp) * grid->dzh[k] / (cp * thv)),(1./rdcp)); 
//  }
//
//  // Calculate pressure at top of domain, needed to fill ghost cells
//  thv = stop*(1.+(rv/rd-1)*qttop);
//  ptop = pow((pow(pmn[kend-1],rdcp) - grav * pow(p0,rdcp) * (grid->zh[kend]-grid->z[kend-1]) / (cp * thv)),(1./rdcp));
//
//  // Fill bottom and top ghost cells 
//  pmn[kstart-1] = 2.*ps - pmn[kstart];
//  pmn[kend] = 2.*ptop - pmn[kend-1];
//
//  return 0;
//}

/**
 * This function calculates the hydrostatic pressure
 * Solves: dpi/dz=-g/thv with pi=cp*(p/p0)**(rd/cp)
 * @param pmn Pointer to hydrostatic pressure array
 * @param s,smean,qt,qtmean .... 
 * @return Returns 1 on error, 0 otherwise.
 */
int cthermo_moist::calchydropres_4th(double * restrict pmn, double * restrict s, double * restrict smean,
                                 double * restrict qt, double * restrict qtmean)
{
  int kstart,kend;
  double thv,ssurf,qtsurf,stop,qttop,ptop;
  double rdcp = rd/cp;

  kstart = grid->kstart;
  kend = grid->kend;

  ssurf  = interp4(smean[kstart-2], smean[kstart-1], smean[kstart], smean[kstart+1]);
  stop   = interp4(smean[kend-2],   smean[kend-1],   smean[kend],   smean[kend+1]);
  qtsurf = interp4(qtmean[kstart-2],qtmean[kstart-1],qtmean[kstart],qtmean[kstart+1]);
  qttop  = interp4(qtmean[kend-2],  qtmean[kend-1],  qtmean[kend],  qtmean[kend+1]);

  // Calculate lowest full level (kstart) from surface values p,s,qt
  thv = ssurf*(1.+(rv/rd-1)*qtsurf);
  pmn[kstart] = pow((pow(ps,rdcp) - grav * pow(p0,rdcp) * grid->z[kstart] / (cp * thv)),(1./rdcp)); 

  for(int k=kstart+1; k<kend; k++)
  {
    thv = interp4(smean[k-2],smean[k-1],smean[k],smean[k+1])*(1.+(rv/rd-1.)*interp4(qtmean[k-2],qtmean[k-1],qtmean[k],qtmean[k+1]));   // BvS: assume no ql for now..
    pmn[k] = pow((pow(pmn[k-1],rdcp) - grav * pow(p0,rdcp) * grid->dzh[k] / (cp * thv)),(1./rdcp)); 
  }

  // Calculate pressure at top of domain, needed to fill ghost cells
  thv = stop*(1.+(rv/rd-1)*qttop);
  ptop = pow((pow(pmn[kend-1],rdcp) - grav * pow(p0,rdcp) * (grid->zh[kend]-grid->z[kend-1]) / (cp * thv)),(1./rdcp));

  // Fill bottom and top ghost cells 
  pmn[kstart-1] = (8./3.)*ps - 2.*pmn[kstart] + (1./3.)*pmn[kstart+1];
  pmn[kstart-2] = 8.*ps - 9.*pmn[kstart] + 2.*pmn[kstart+1];
  pmn[kend] = (8./3.)*ptop - 2.*pmn[kend-1] + (1./3.)*pmn[kend-2];
  pmn[kend+1] = 8.*ptop - 9.*pmn[kend-1] + 2.*pmn[kend-2];

  return 0;
}


int cthermo_moist::calcbuoyancytend_2nd(double * restrict wt, double * restrict s, double * restrict qt, double * restrict p,
                                        double * restrict ph, double * restrict sh, double * restrict qth, double * restrict ql,
                                        double * restrict thvrefh)
{
  int ijk,jj,kk,ij;
  //double tl, ph, exnh;
  double tl, exnh;
  jj = grid->icells;
  kk = grid->icells*grid->jcells;

  // CvH check the usage of the gravity term here, in case of scaled DNS we use one. But thermal expansion coeff??
  for(int k=grid->kstart+1; k<grid->kend; k++)
  {
    //ph   = interp2(p[k-1],p[k]);   // BvS To-do: calculate pressure at full and half levels
    exnh = exn(ph[k]);
    for(int j=grid->jstart; j<grid->jend; j++)
#pragma ivdep
      for(int i=grid->istart; i<grid->iend; i++)
      {
        ijk = i + j*jj + k*kk;
        ij  = i + j*jj;
        sh[ij]  = interp2(s[ijk-kk], s[ijk]);
        qth[ij] = interp2(qt[ijk-kk], qt[ijk]);
        tl      = sh[ij] * exnh;
        // Calculate first estimate of ql using Tl
        // if ql(Tl)>0, saturation adjustment routine needed
        ql[ij]  = qth[ij]-rslf(ph[k],tl);
      }
    for(int j=grid->jstart; j<grid->jend; j++)
#pragma ivdep
      for(int i=grid->istart; i<grid->iend; i++)
      {
        ij  = i + j*jj;
        if(ql[ij]>0)   // already doesn't vectorize because of iteration in calcql()
          ql[ij] = calcql(sh[ij], qth[ij], ph[k], exnh);
        else
          ql[ij] = 0.;
      }
    for(int j=grid->jstart; j<grid->jend; j++)
#pragma ivdep
      for(int i=grid->istart; i<grid->iend; i++)
      {
        ijk = i + j*jj + k*kk;
        ij  = i + j*jj;
        wt[ijk] += bu(ph[k], sh[ij], qth[ij], ql[ij], thvrefh[k]);
      }
  }
  return 0;
}

int cthermo_moist::calcbuoyancytend_4th(double * restrict wt, double * restrict s, double * restrict qt, double * restrict p,
                                        double * restrict sh, double * restrict qth, double * restrict ql,
                                        double * restrict thvrefh)
{
  int ijk,jj,ij;
  int kk1,kk2;
  double tl, ph, exnh;

  jj  = grid->icells;
  kk1 = 1*grid->icells*grid->jcells;
  kk2 = 2*grid->icells*grid->jcells;

  // double thvref = thvs;

  for(int k=grid->kstart+1; k<grid->kend; k++)
  {
    ph  = interp4(p[k-2] , p[k-1] , p[k] , p[k+1]); // BvS To-do: calculate pressure at full and half levels
    exnh = exn2(ph);
    for(int j=grid->jstart; j<grid->jend; j++)
#pragma ivdep
      for(int i=grid->istart; i<grid->iend; i++)
      {
        ijk = i + j*jj + k*kk1;
        ij  = i + j*jj;
        sh[ij]  = interp4(s[ijk-kk2] , s[ijk-kk1] , s[ijk] , s[ijk+kk1]);
        qth[ij] = interp4(qt[ijk-kk2], qt[ijk-kk1], qt[ijk], qt[ijk+kk1]);
        tl      = sh[ij] * exnh;
        // Calculate first estimate of ql using Tl
        // if ql(Tl)>0, saturation adjustment routine needed
        ql[ij]  = qth[ij]-rslf(ph,tl);   
      }
    for(int j=grid->jstart; j<grid->jend; j++)
#pragma ivdep
      for(int i=grid->istart; i<grid->iend; i++)
      {
        ij  = i + j*jj;
        if(ql[ij]>0)   // already doesn't vectorize because of iteration in calcql()
          ql[ij] = calcql(sh[ij], qth[ij], ph, exnh);
        else
          ql[ij] = 0.;
      }
    for(int j=grid->jstart; j<grid->jend; j++)
#pragma ivdep
      for(int i=grid->istart; i<grid->iend; i++)
      {
        ijk = i + j*jj + k*kk1;
        ij  = i + j*jj;
        wt[ijk] += bu(ph, sh[ij], qth[ij], ql[ij], thvrefh[k]);
      }
  }
  return 0;
}

int cthermo_moist::calcbuoyancy(double * restrict b, double * restrict s, double * restrict qt, double * restrict p, double * restrict ql,
                                double * restrict thvref)
{
  int ijk,jj,kk,ij;
  double tl, exn;
  jj = grid->icells;
  kk = grid->icells*grid->jcells;

  // double thvref = thvs;

  for(int k=0; k<grid->kcells; k++)
  {
    exn = exn2(p[k]);
    for(int j=grid->jstart; j<grid->jend; j++)
#pragma ivdep
      for(int i=grid->istart; i<grid->iend; i++)
      {
        ijk = i + j*jj + k*kk;
        ij  = i + j*jj;
        tl  = s[ijk] * exn;
        ql[ij]  = qt[ijk]-rslf(p[k],tl);   // not real ql, just estimate
      }
    for(int j=grid->jstart; j<grid->jend; j++)
#pragma ivdep
      for(int i=grid->istart; i<grid->iend; i++)
      {
        ijk = i + j*jj + k*kk;
        ij  = i + j*jj;
        if(ql[ij] > 0)
          ql[ij] = calcql(s[ijk], qt[ijk], p[k], exn);
        else
          ql[ij] = 0.;
      }
    for(int j=grid->jstart; j<grid->jend; j++)
#pragma ivdep
      for(int i=grid->istart; i<grid->iend; i++)
      {
        ijk = i + j*jj + k*kk;
        ij  = i + j*jj;
        b[ijk] = bu(p[k], s[ijk], qt[ijk], ql[ij], thvref[k]);
      }
  }

  return 0;
}

int cthermo_moist::calcqlfield(double * restrict ql, double * restrict s, double * restrict qt, double * restrict p)
{
  int ijk,jj,kk;
  double exn;

  jj = grid->icells;
  kk = grid->icells*grid->jcells;

  for(int k=grid->kstart; k<grid->kend; k++)
  {
    exn = exn2(p[k]);
    for(int j=grid->jstart; j<grid->jend; j++)
#pragma ivdep
      for(int i=grid->istart; i<grid->iend; i++)
      {
        ijk = i + j*jj + k*kk;
        ql[ijk] = calcql(s[ijk], qt[ijk], p[k], exn);
      }
  }
  return 0;
}

int cthermo_moist::calcN2(double * restrict N2, double * restrict s, double * restrict dzi, double * restrict thvref)
{
  int ijk,jj,kk;
  jj = grid->icells;
  kk = grid->icells*grid->jcells;

  // double thvref = thvs;

  for(int k=0; k<grid->kcells; ++k)
    for(int j=grid->jstart; j<grid->jend; ++j)
#pragma ivdep
      for(int i=grid->istart; i<grid->iend; ++i)
      {
        ijk = i + j*jj + k*kk;
        N2[ijk] = grav/thvref[k]*0.5*(s[ijk+kk] - s[ijk-kk])*dzi[k];
      }

  return 0;
}

int cthermo_moist::calcbuoyancybot(double * restrict b , double * restrict bbot,
                                   double * restrict s , double * restrict sbot,
                                   double * restrict qt, double * restrict qtbot,
                                   double * restrict thvref, double * restrict thvrefh)
{
  int ij,ijk,jj,kk,kstart;
  jj = grid->icells;
  kk = grid->icells*grid->jcells;
  kstart = grid->kstart;

  // double thvref = thvs;

  // assume no liquid water at the lowest model level
  for(int j=0; j<grid->jcells; j++)
#pragma ivdep
    for(int i=0; i<grid->icells; i++)
    {
      ij  = i + j*jj;
      ijk = i + j*jj + kstart*kk;
      bbot[ij ] = bunoql(sbot[ij], qtbot[ij], thvrefh[kstart]);
      b   [ijk] = bunoql(s[ijk], qt[ijk], thvref[kstart]);
    }

  return 0;
}

int cthermo_moist::calcbuoyancyfluxbot(double * restrict bfluxbot, double * restrict sbot, double * restrict sfluxbot, double * restrict qtbot, double * restrict qtfluxbot,
                                       double * restrict thvrefh)
{
  int ij,jj,kstart;
  jj = grid->icells;
  kstart = grid->kstart;

  // double thvref = thvs;

  // assume no liquid water at the lowest model level
  for(int j=0; j<grid->jcells; j++)
#pragma ivdep
    for(int i=0; i<grid->icells; i++)
    {
      ij  = i + j*jj;
      bfluxbot[ij] = bufluxnoql(sbot[ij], sfluxbot[ij], qtbot[ij], qtfluxbot[ij], thvrefh[kstart]);
    }

  return 0;
}

// INLINE FUNCTIONS
inline double cthermo_moist::bu(const double p, const double s, const double qt, const double ql, const double thvref)
{
  return grav * ((s + lv*ql/(cp*exn2(p))) * (1. - (1. - rv/rd)*qt - rv/rd*ql) - thvref) / thvref;
}

inline double cthermo_moist::bunoql(const double s, const double qt, const double thvref)
{
  return grav * (s * (1. - (1. - rv/rd)*qt) - thvref) / thvref;
}

inline double cthermo_moist::bufluxnoql(const double s, const double sflux, const double qt, const double qtflux, const double thvref)
{
  return grav/thvref * (sflux * (1. - (1.-rv/rd)*qt) - (1.-rv/rd)*s*qtflux);
}

inline double cthermo_moist::calcql(const double s, const double qt, const double p, const double exn)
{
  int niter = 0; //, nitermax = 5;
  double ql, tl, tnr_old = 1.e9, tnr, qs;
  tl = s * exn;
  tnr = tl;
  while (std::fabs(tnr-tnr_old)/tnr_old> 1e-5)// && niter < nitermax)
  {
    ++niter;
    tnr_old = tnr;
    qs = rslf(p,tnr);
    tnr = tnr - (tnr+(lv/cp)*qs-tl-(lv/cp)*qt)/(1+(std::pow(lv,2)*qs)/ (rv*cp*std::pow(tnr,2)));
  }
  ql = std::max(0.,qt - qs);
  return ql;
}

inline double cthermo_moist::exn(const double p)
{
  return pow((p/p0),(rd/cp));
}

inline double cthermo_moist::exn2(const double p)
{
  double dp=p-p0;
  return (1+(dp*(ex1+dp*(ex2+dp*(ex3+dp*(ex4+dp*(ex5+dp*(ex6+ex7*dp)))))))); 
}

inline double cthermo_moist::rslf(const double p, const double t)
{
  return ep*esl(t)/(p-(1-ep)*esl(t));
}

inline double cthermo_moist::esl(const double t)
{
  const double x=std::max(-80.,t-tmelt);
  return c0+x*(c1+x*(c2+x*(c3+x*(c4+x*(c5+x*(c6+x*(c7+x*c8)))))));

  //return es0*std::exp(at*(t-tmelt)/(t-bt));
}

inline double cthermo_moist::interp2(const double a, const double b)
{
  return 0.5*(a + b);
}

inline double cthermo_moist::interp4(const double a, const double b, const double c, const double d)
{
  return (-a + 9.*b + 9.*c - d) / 16.;
}


