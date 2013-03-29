#include <cstdlib>
#include <cstdio>
#include <cmath>
#include "grid.h"
#include "fields.h"
#include "defines.h"

cfields::cfields(cgrid *gridin, cmpi *mpiin)
{
  grid = gridin;
  mpi  = mpiin;

  allocated = false;
}

cfields::~cfields()
{
  if(allocated)
  {
    // DEALLOCATE ALL THE FIELDS
    // deallocate the prognostic velocity fields
    for(fieldmap::iterator it=mp.begin(); it!=mp.end(); ++it)
      delete it->second;

    // deallocate the velocity tendency fields
    for(fieldmap::iterator it=mt.begin(); it!=mt.end(); ++it)
      delete it->second;

    // deallocate the prognostic scalar fields
    for(fieldmap::iterator it=sp.begin(); it!=sp.end(); ++it)
      delete it->second;

    // deallocate the scalar tendency fields
    for(fieldmap::iterator it=st.begin(); it!=st.end(); ++it)
      delete it->second;

    // deallocate the diagnostic scalars
    for(fieldmap::iterator it=sd.begin(); it!=sd.end(); ++it)
      delete it->second;
  }
}

int cfields::readinifile(cinput *inputin)
{
  // input parameters
  int n = 0;

  // obligatory parameters
  n += inputin->getItem(&visc , "fields", "visc" );

  // LES
  n += inputin->getItem(&tPr, "fields", "tPr", 1./3.);

  // read the name of the passive scalars
  std::vector<std::string> slist;
  n += inputin->getItem(&slist, "fields", "slist", "");

  // initialize the scalars
  for(std::vector<std::string>::iterator it = slist.begin(); it!=slist.end(); ++it)
  {
    if(initpfld(*it))
      return 1;
    n += inputin->getItem(&sp[*it]->visc, "fields", "svisc", *it);
  }

  // initialize the basic set of fields
  n += initmomfld(u, ut, "u");
  n += initmomfld(v, vt, "v");
  n += initmomfld(w, wt, "w");
  n += initdfld("p");
  n += initdfld("tmp1");
  n += initdfld("tmp2");

  if(n > 0)
    return 1;

  return 0;
}

int cfields::init()
{
  if(mpi->mpiid == 0) std::printf("Initializing fields\n");

  int n = 0;

  // ALLOCATE ALL THE FIELDS
  // allocate the prognostic velocity fields
  for(fieldmap::iterator it=mp.begin(); it!=mp.end(); ++it)
    n += it->second->init();

  // allocate the velocity tendency fields
  for(fieldmap::iterator it=mt.begin(); it!=mt.end(); ++it)
    n += it->second->init();

  // allocate the prognostic scalar fields
  for(fieldmap::iterator it=sp.begin(); it!=sp.end(); ++it)
    n += it->second->init();

  // allocate the scalar tendency fields
  for(fieldmap::iterator it=st.begin(); it!=st.end(); ++it)
    n += it->second->init();

  // allocate the diagnostic scalars
  for(fieldmap::iterator it=sd.begin(); it!=sd.end(); ++it)
    n += it->second->init();

  if(n > 0)
    return 1;

  allocated = true;

  return 0;
}

int cfields::initmomfld(cfield3d *&fld, cfield3d *&fldt, std::string fldname)
{
  if (mp.find(fldname)!=mp.end())
  {
    std::printf("ERROR \"%s\" already exists\n", fldname.c_str());
    return 1;
  }
  
  std::string fldtname = fldname + "t";
  
  mp[fldname] = new cfield3d(grid, mpi, fldname);
  // mp[fldname]->init();

  mt[fldname] = new cfield3d(grid, mpi, fldtname);
  // mt[fldname]->init();

  //m[fldname] = mp[fldname];

  fld  = mp[fldname];
  fldt = mt[fldname];

  return 0;
}

int cfields::initpfld(std::string fldname)
{
  if (s.find(fldname)!=s.end())
  {
    std::printf("ERROR \"%s\" already exists\n", fldname.c_str());
    return 1;
  }
  
  std::string fldtname = fldname + "t";
  
  sp[fldname] = new cfield3d(grid, mpi, fldname);
  //sp[fldname]->init();
  
  st[fldname] = new cfield3d(grid, mpi, fldtname);
  //st[fldname]->init();

  s[fldname] = sp[fldname];
  
  return 0;
}

int cfields::initdfld(std::string fldname)
{
  if (s.find(fldname)!=s.end())
  {
    std::printf("ERROR \"%s\" already exists\n", fldname.c_str());
    return 1;
  }

  sd[fldname] = new cfield3d(grid, mpi, fldname );
  // sd[fldname]->init();

  s[fldname] = sd[fldname];

  // fld = s[fldname];
  
  return 0;  
}

int cfields::create(cinput *inputin)
{
  if(mpi->mpiid == 0) std::printf("Creating fields\n");
  
  int n = 0;
  
  // Randomnize the momentum
  for(fieldmap::iterator it=mp.begin(); it!=mp.end(); ++it)
    n +=  randomnize(inputin, it->first, it->second->data);
  
  // Randomnize the scalars
  for(fieldmap::iterator it=sp.begin(); it!=sp.end(); ++it)
    n +=  randomnize(inputin, it->first, it->second->data);
  
  // Add Vortices
  n += addvortexpair(inputin);
  
  // Add the mean profiles to the fields
  n +=  addmeanprofile(inputin, "u", mp["u"]->data);
  n +=  addmeanprofile(inputin, "v", mp["v"]->data);
 
  for(fieldmap::iterator it=sp.begin(); it!=sp.end(); ++it)
    n +=  addmeanprofile(inputin, it->first, it->second->data);
  
  // set w equal to zero at the boundaries, just to be sure
  int lbot = grid->kstart*grid->icells*grid->jcells;
  int ltop = grid->kend  *grid->icells*grid->jcells;
  for(int l=0; l<grid->icells*grid->jcells; ++l)
  {
    w->data[lbot + l] = 0.;
    w->data[ltop + l] = 0.;
  }
  
  return (n>0);
}

int cfields::randomnize(cinput *inputin, std::string fld, double * restrict data)
{
  int n = 0;
    // set mpiid as random seed to avoid having the same field at all procs
  int static seed = 0;
  if (seed)
  {
    seed = mpi->mpiid;
    std::srand(seed);
  }
  
  int ijk,jj,kk;
  int kendrnd;
  double rndfac, rndfach;

  jj = grid->icells;
  kk = grid->icells*grid->jcells;
  
  // look up the specific randomnizer variables
  n += inputin->getItem(&rndamp , "fields", "rndamp" , 0.,fld);
  n += inputin->getItem(&rndz   , "fields", "rndz"   , 0.,fld);
  n += inputin->getItem(&rndbeta, "fields", "rndbeta", 0.,fld);

  // find the location of the randomizer height
  kendrnd = grid->kstart;
  while(grid->zh[kendrnd+1] < rndz)
    ++kendrnd;

  if(kendrnd > grid->kend)
  {
    printf("ERROR: Randomnizer height rndz (%f) higher than domain top (%f)\n", grid->z[kendrnd],grid->zsize);
    return 1;
  }
  
  if((int) kendrnd == grid->kstart)
    kendrnd = grid->kend;

  for(int k=grid->kstart; k<kendrnd; ++k)
  {
    rndfac  = std::pow((rndz-grid->z [k])/rndz, rndbeta);
    rndfach = std::pow((rndz-grid->zh[k])/rndz, rndbeta);
    for(int j=grid->jstart; j<grid->jend; ++j)
      for(int i=grid->istart; i<grid->iend; ++i)
      {
        ijk = i + j*jj + k*kk;
        data[ijk] = rndfac  * rndamp * ((double) std::rand() / (double) RAND_MAX - 0.5);
      }
  }

  return (n>0);
}

int cfields::addvortexpair(cinput *inputin)
{
  // add a double vortex to the initial conditions
  const double pi = std::acos((double)-1.);
  int n, ijk, jj, kk;

  jj = grid->icells;
  kk = grid->icells*grid->jcells;
    
  // optional parameters
  n += inputin->getItem(&nvortexpair, "fields", "nvortexpair", 0    );
  n += inputin->getItem(&vortexamp  , "fields", "vortexamp"  , 1.e-3);
  n += inputin->getItem(&vortexaxis , "fields", "vortexaxis" , 1    );

  if(nvortexpair > 0)
  {
    if(vortexaxis == 0)
      for(int k=grid->kstart; k<grid->kend; ++k)
        for(int j=grid->jstart; j<grid->jend; ++j)
          for(int i=grid->istart; i<grid->iend; ++i)
          {
            ijk = i + j*jj + k*kk;
            u->data[ijk] +=  vortexamp*std::sin(nvortexpair*2.*pi*(grid->xh[i])/grid->xsize)*std::cos(pi*grid->z [k]/grid->zsize);
            w->data[ijk] += -vortexamp*std::cos(nvortexpair*2.*pi*(grid->x [i])/grid->xsize)*std::sin(pi*grid->zh[k]/grid->zsize);
          }
    else if(vortexaxis == 1)
      for(int k=grid->kstart; k<grid->kend; ++k)
        for(int j=grid->jstart; j<grid->jend; ++j)
          for(int i=grid->istart; i<grid->iend; ++i)
          {
            ijk = i + j*jj + k*kk;
            v->data[ijk] +=  vortexamp*std::sin(nvortexpair*2.*pi*(grid->yh[j])/grid->ysize)*std::cos(pi*grid->z [k]/grid->zsize);
            w->data[ijk] += -vortexamp*std::cos(nvortexpair*2.*pi*(grid->y [j])/grid->ysize)*std::sin(pi*grid->zh[k]/grid->zsize);
          }
  }
  
  return (n>0);
}

int cfields::addmeanprofile(cinput *inputin, std::string fld, double * restrict data)
{
  int n;
  int ijk, jj, kk;
  double proftemp[grid->kmax];

  jj = grid->icells;
  kk = grid->icells*grid->jcells;
  
  if(inputin->getProf(proftemp, fld, grid->kmax))
    return 1;

  for(int k=grid->kstart; k<grid->kend; ++k)
    for(int j=grid->jstart; j<grid->jend; ++j)
      for(int i=grid->istart; i<grid->iend; ++i)
      {
        ijk = i + j*jj + k*kk;
        data[ijk] += proftemp[k-grid->kstart];
      }
      
  return (n>0);
}

int cfields::load(int n)
{
  // check them all before returning error
  int nerror = 0;
  nerror += u->load(n, sd["tmp1"]->data, sd["tmp2"]->data);
  nerror += v->load(n, sd["tmp1"]->data, sd["tmp2"]->data);
  nerror += w->load(n, sd["tmp1"]->data, sd["tmp2"]->data);
  for(fieldmap::iterator itProg = sp.begin(); itProg!=sp.end(); ++itProg)
    nerror += itProg->second->load(n, sd["tmp1"]->data, sd["tmp2"]->data);

  if(nerror > 0)
    return 1;

  return 0;
}

int cfields::save(int n)
{
  u->save(n, sd["tmp1"]->data, sd["tmp2"]->data);
  v->save(n, sd["tmp1"]->data, sd["tmp2"]->data);
  w->save(n, sd["tmp1"]->data, sd["tmp2"]->data);
  // p->save(n);
  for(fieldmap::iterator itProg = sp.begin(); itProg!=sp.end(); ++itProg)
    itProg->second->save(n, sd["tmp1"]->data, sd["tmp2"]->data);

  return 0;
}

/*
int cfields::boundary()
{
  u->boundary_bottop(0);
  v->boundary_bottop(0);
  // w->boundary_bottop(0);
  s->boundary_bottop(1);

  // u->boundary_cyclic();
  // v->boundary_cyclic();
  // w->boundary_cyclic();
  // s->boundary_cyclic();
  
  grid->boundary_cyclic(u->data);
  grid->boundary_cyclic(v->data);
  grid->boundary_cyclic(w->data);
  grid->boundary_cyclic(s->data);
  mpi->waitall();

  // for(int k=grid->kstart-grid->kgc; k<grid->kend+grid->kgc; ++k)
  //   std::printf("%4d %9.6f %9.6f %9.6f %9.6f %9.6f\n", k, grid->z[k], grid->zh[k], u->data[k*grid->icells*grid->jcells], v->data[k*grid->icells*grid->jcells], w->data[k*grid->icells*grid->jcells]);

  return 0;
}
*/

double cfields::checkmom()
{
  return calcmom_2nd(u->data, v->data, w->data, grid->dz);
}

double cfields::checktke()
{
  return calctke_2nd(u->data, v->data, w->data, grid->dz);
}

double cfields::checkmass()
{
  // CvH for now, do the mass check on the first scalar... Do we want to change this?
  fieldmap::iterator itProg=sp.begin();
  if(sp.begin() != sp.end())
    return calcmass(itProg->second->data, grid->dz);
  else
    return 0.;
}

double cfields::calcmass(double * restrict s, double * restrict dz)
{
  int ijk,jj,kk;

  jj = grid->icells;
  kk = grid->icells*grid->jcells;

  double mass = 0;

  for(int k=grid->kstart; k<grid->kend; ++k)
    for(int j=grid->jstart; j<grid->jend; ++j)
#pragma ivdep
      for(int i=grid->istart; i<grid->iend; ++i)
      {
        ijk = i + j*jj + k*kk;
        mass += s[ijk]*dz[k];
      }

  grid->getsum(&mass);

  mass /= (grid->itot*grid->jtot*grid->zsize);

  return mass;
}

double cfields::calcmom_2nd(double * restrict u, double * restrict v, double * restrict w, double * restrict dz)
{
  int ijk,ii,jj,kk;

  ii = 1;
  jj = grid->icells;
  kk = grid->icells*grid->jcells;

  double momentum;
  momentum = 0;

  for(int k=grid->kstart; k<grid->kend; ++k)
    for(int j=grid->jstart; j<grid->jend; ++j)
#pragma ivdep
      for(int i=grid->istart; i<grid->iend; ++i)
      {
        ijk = i + j*jj + k*kk;
        momentum += (interp2(u[ijk], u[ijk+ii]) + interp2(v[ijk], v[ijk+jj]) + interp2(w[ijk], w[ijk+kk]))*dz[k];
      }

  grid->getsum(&momentum);

  momentum /= (grid->itot*grid->jtot*grid->zsize);

  return momentum;
}

double cfields::calctke_2nd(double * restrict u, double * restrict v, double * restrict w, double * restrict dz)
{
  int ijk,ii,jj,kk;

  ii = 1;
  jj = grid->icells;
  kk = grid->icells*grid->jcells;

  double tke = 0;

  for(int k=grid->kstart; k<grid->kend; ++k)
    for(int j=grid->jstart; j<grid->jend; ++j)
#pragma ivdep
      for(int i=grid->istart; i<grid->iend; ++i)
      {
        ijk = i + j*jj + k*kk;
        tke += ( interp2(u[ijk]*u[ijk], u[ijk+ii]*u[ijk+ii]) 
               + interp2(v[ijk]*v[ijk], v[ijk+jj]*v[ijk+jj]) 
               + interp2(w[ijk]*w[ijk], w[ijk+kk]*w[ijk+kk]))*dz[k];
      }

  grid->getsum(&tke);

  tke /= (grid->itot*grid->jtot*grid->zsize);
  tke *= 0.5;

  return tke;
}

inline double cfields::interp2(const double a, const double b)
{
  return 0.5*(a + b);
}

