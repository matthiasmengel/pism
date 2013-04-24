// Copyright (C) 2011, 2012 PISM Authors
//
// This file is part of PISM.
//
// PISM is free software; you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation; either version 2 of the License, or (at your option) any later
// version.
//
// PISM is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License
// along with PISM; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

#include "POGivenTH.hh"
#include "IceGrid.hh"
#include "PISMVars.hh"

/* TO DO:
 *
 * Translate all comments into English.
 *
 * Add detailed references ("this implements equations (3)-(7) in Smith and
 * Jones, 2001" and similar).
 *
 * Give meaningful names to class methods (potit? pttmpr? adlprt?). Long names
 * are OK.
 *
 * All computationally expensive code should be called from the update() method.
 *
 * Make sure that the code compiles without warnings.
 *
 * Remove unused code. (It can always be recovered from an earlier version of
 * the code.)
 */

POGivenTH::POGivenTH(IceGrid &g, const NCConfigVariable &conf)
  : PGivenClimate<POModifier,PISMOceanModel>(g, conf, NULL)
{
  PetscErrorCode ierr = allocate_POGivenTH(); CHKERRCONTINUE(ierr);
  if (ierr != 0)
    PISMEnd();
}

POGivenTH::~POGivenTH() {
  // empty
}

PetscErrorCode POGivenTH::allocate_POGivenTH() {
  PetscErrorCode ierr;
  option_prefix   = "-ocean_th";

  // will be de-allocated by the parent's destructor
  theta_ocean    = new IceModelVec2T;
  salinity_ocean = new IceModelVec2T;
  shelfbtemp     = new IceModelVec2T;
  shelfbmassflux = new IceModelVec2T;

  m_fields["theta_ocean"]     = theta_ocean;
  m_fields["salinity_ocean"]  = salinity_ocean;

  ierr = process_options(); CHKERRQ(ierr);

  map<string, string> standard_names;
  ierr = set_vec_parameters(standard_names); CHKERRQ(ierr);

  ierr = theta_ocean->create(grid, "theta_ocean", false); CHKERRQ(ierr);
  ierr = salinity_ocean->create(grid, "salinity_ocean", false); CHKERRQ(ierr);
  ierr = shelfbtemp->create(grid, "shelfbtemp", false); CHKERRQ(ierr);
  ierr = shelfbmassflux->create(grid, "shelfbmassflux", false); CHKERRQ(ierr);

  ierr = theta_ocean->set_attrs("climate_forcing",
                        "absolute potential temperature of the adjacent ocean",
                        "Kelvin", ""); CHKERRQ(ierr);
  ierr = salinity_ocean->set_attrs("climate_forcing",
			     "salinity of the adjacent ocean",
			     "g/kg", ""); CHKERRQ(ierr);
  ierr = shelfbtemp->set_attrs("climate_forcing",
                        "absolute temperature at ice shelf base",
                        "Kelvin", ""); CHKERRQ(ierr);
  ierr = shelfbmassflux->set_attrs("climate_forcing",
			     "ice mass flux from ice shelf base (positive flux is loss from ice shelf)",
			     "m s-1", ""); CHKERRQ(ierr);


  return 0;
}

PetscErrorCode POGivenTH::init(PISMVars &vars) {
  PetscErrorCode ierr;

  t = dt = GSL_NAN;  // every re-init restarts the clock

  ierr = verbPrintf(2, grid.com,
                    "* Initializing the 3eqn melting parameterization ocean model\n"
                    "  reading ocean temperature and salinity from a file...\n"); CHKERRQ(ierr);

  ice_thickness = dynamic_cast<IceModelVec2S*>(vars.get("land_ice_thickness"));
  if (!ice_thickness) {SETERRQ(grid.com, 1, "ERROR: ice thickness is not available");}
  ierr = theta_ocean   -> init(filename, bc_period, bc_reference_time); CHKERRQ(ierr);
  ierr = salinity_ocean-> init(filename, bc_period, bc_reference_time); CHKERRQ(ierr);

  // read time-independent data right away:
  if (theta_ocean->get_n_records() == 1 && salinity_ocean->get_n_records() == 1) {
    ierr = update(grid.time->current(), 0); CHKERRQ(ierr); // dt is irrelevant
  }

  return 0;
}

PetscErrorCode POGivenTH::update(PetscReal my_t, PetscReal my_dt) {

  PetscErrorCode ierr = update_internal(my_t, my_dt); CHKERRQ(ierr);

  ierr = theta_ocean->average(t, dt); CHKERRQ(ierr);
  ierr = salinity_ocean->average(t, dt); CHKERRQ(ierr);

  ierr = calculate_boundlayer_temp_and_salt(); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode POGivenTH::shelf_base_temperature(IceModelVec2S &result) {
  PetscErrorCode ierr = shelfbtemp->copy_to(result); CHKERRQ(ierr);
  return 0;
}

PetscErrorCode POGivenTH::calculate_boundlayer_temp_and_salt() {

  PetscErrorCode ierr;

  const PetscScalar rhoi = config.get("ice_density");
  const PetscScalar rhow = config.get("sea_water_density");
  const PetscScalar reference_pressure = 1.01325; // pressure of atmosphere in bar


  PetscReal pressure_at_shelf_base, bmeltrate, temp_insitu, temp_base, sal_base;

  ierr = ice_thickness->begin_access();   CHKERRQ(ierr);
  ierr = theta_ocean->begin_access(); CHKERRQ(ierr);
  ierr = salinity_ocean->begin_access(); CHKERRQ(ierr);
  ierr = shelfbmassflux->begin_access(); CHKERRQ(ierr);
  ierr = shelfbtemp->begin_access(); CHKERRQ(ierr);

  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      
      pressure_at_shelf_base = (rhoi * (*ice_thickness)(i,j))/1000 + reference_pressure; // in bar

      // convert potential to insitu temperature 
      potit((*salinity_ocean)(i,j), (*theta_ocean)(i,j) - 273.15, pressure_at_shelf_base, reference_pressure, temp_insitu);

      shelf_base_temp_salinity_3eqn((*salinity_ocean)(i,j), temp_insitu, (*ice_thickness)(i,j), temp_base, sal_base);
      compute_meltrate_3eqn(rhow, rhoi, temp_base, sal_base, (*salinity_ocean)(i,j), bmeltrate);
      ierr = verbPrintf(2, grid.com, "temp_insitu=%f, salt_ocean=%f\n", temp_insitu,(*salinity_ocean)(i,j)); CHKERRQ(ierr);
      ierr = verbPrintf(2, grid.com, "bound temp=%f, salt=%f,bmelt=%f\n", temp_base,sal_base,bmeltrate); CHKERRQ(ierr);
      
      // the ice/ocean boundary layer temperature is seen by PISM as shelfbtemp.
      (*shelfbtemp)(i,j)     = temp_base + 273.15; // to Kelvin
      (*shelfbmassflux)(i,j) = -1 * bmeltrate;

    }
  }

  ierr = ice_thickness->end_access(); CHKERRQ(ierr);
  ierr = theta_ocean->end_access(); CHKERRQ(ierr);
  ierr = salinity_ocean->end_access(); CHKERRQ(ierr);
  ierr = shelfbmassflux->end_access(); CHKERRQ(ierr);
  ierr = shelfbtemp->end_access(); CHKERRQ(ierr);

  return 0;
}

PetscErrorCode POGivenTH::shelf_base_mass_flux(IceModelVec2S &result) {
  PetscErrorCode ierr = shelfbmassflux->copy_to(result); CHKERRQ(ierr);
  return 0;
}

// Ported from Matthias
PetscErrorCode POGivenTH::shelf_base_temp_salinity_3eqn(PetscReal sal_ocean,
                                                        PetscReal temp_insitu, PetscReal zice,
							                                          PetscReal &temp_base, PetscReal &sal_base){
//PetscErrorCode POGivenTH::shelf_base_temp_salinity_3eqn(PetscReal gat, PetscReal sal_ocean,
//                                                        PetscReal temp_insitu, PetscReal zice, PetscReal &temp_base,
//                                                        PetscReal &sal_base){

  // The three-equation model of ice-shelf ocean interaction (Hellmer and Olbers, 1989).
  // Code derived from BRIOS subroutine iceshelf (which goes back to H.Hellmer's 2D ice shelf model code)
  // and adjusted for use in FESOM by Ralph Timmermann, 16.02.2011
  // adapted for PISM by matthias.mengel@pik-potsdam.de

  PetscErrorCode ierr;
  PetscReal rhor, heat_flux, water_flux;
  PetscReal gats1, gats2;
  // PetscReal gats1, gats2, gas, gat;
  PetscReal ep1,ep2,ep3,ep4,ep5;
  PetscReal ex1,ex2,ex3,ex4,ex5;
  PetscReal vt1,sr1,sr2,sf1,sf2,tf1,tf2,tf,sf,seta,re;
  PetscInt n, n3, nk;

  PetscReal a   = -0.0575;                // Foldvik&Kvinge (1974) [°C/psu]
  PetscReal b   =  0.0901;                // [°C]
  PetscReal c   =  7.61e-4;               // [°C/m]

  PetscReal pr  =  13.8;                  //Prandtl number      [dimensionless]
  PetscReal sc  =  2432.;                 //Schmidt number      [dimensionless]
  PetscReal ak  =  2.50e-3;               //dimensionless drag coeff.
  PetscReal sak1=  sqrt(ak);
  PetscReal un  =  1.95e-6;               //kinematic viscosity [m2/s]
  PetscReal pr1 =  pow(pr,(2./3.));       //Jenkins (1991)
  PetscReal sc1 =  pow(sc,(2./3.));
  PetscReal tob=  -20.;                   //temperature at the ice surface
  //   PetscReal rhoi=  920.;               //mean ice density
  PetscReal cpw =  4180.0;                //Barnier et al. (1995)
  PetscReal lhf =  3.33e+5;               //latent heat of fusion
  PetscReal atk =  273.15;                //0 deg C in Kelvin
  //FIXME: can use PISMs surface temp for tob?
  PetscReal cpi =  152.5+7.122*(atk+tob); //Paterson:"The Physics of Glaciers"

  PetscReal L    = 334000.;               // [J/Kg]

  // Prescribe the turbulent heat and salt transfer coeff. GAT and GAS
  PetscReal gat  = 1.00e-4;   //[m/s] RG3417 Default value from Hellmer and Olbers 89
  PetscReal gas  = 5.05e-7;   //[m/s] RG3417 Default value from Hellmer and Olbers 89

  // Calculate
  // density in the boundary layer: rhow
  // and interface pressure pg [dbar],
  // Solve a quadratic equation for the interface salinity sb
  // to determine the melting/freezing rate seta.

  // FIXME: need to calculate water density instead of const value.
  //  call fcn_density(thetao,sal,zice,rho)
  //  rhow = density_0+rho  //was rhow= rho0+rho(i,j,N)
  //  rhow = 1028.0;
  //  rhor= rhoi/rhow;

  ep1 = cpw*gat;
  ep2 = cpi*gas;
  ep3 = lhf*gas;
  ep4 = b-c*zice;
  //  ep5 = gas/rhor;
  // negative heat flux term in the ice (due to -kappa/D)
  ex1 = a*(ep1-ep2);
  ex2 = ep1*(ep4-temp_insitu)+ep2*(tob+a*sal_ocean-ep4)-ep3;
  ex3 = sal_ocean*(ep2*(ep4-tob)+ep3);
  ex4 = ex2/ex1;
  ex5 = ex3/ex1;

  sr1 = 0.25*ex4*ex4-ex5;
  sr2 = -0.5*ex4;
  sf1 = sr2+sqrt(sr1);
  tf1 = a*sf1+ep4;
  sf2 = sr2-sqrt(sr1);
  tf2 = a*sf2+ep4;

  // Salinities < 0 psu are not defined, therefore pick the positive of the two solutions:
  if(sf1 > 0.){
    tf = tf1;
    sf = sf1;
  }else{
    tf = tf2;
    sf = sf2;
  }

  temp_base = tf;
  sal_base  = sf;

  return 0;
}

PetscErrorCode POGivenTH::compute_meltrate_3eqn( PetscReal rhow, PetscReal rhoi,
                                                 PetscReal temp_base, PetscReal sal_base,
                                                 PetscReal sal_ocean, PetscReal &meltrate){

  // The three-equation model of ice-shelf ocean interaction (Hellmer and Olbers, 1989).
  // Code derived from BRIOS subroutine iceshelf (which goes back to H.Hellmer's 2D ice shelf model code)
  // and adjusted for use in FESOM by Ralph Timmermann, 16.02.2011
  // adapted for PISM by matthias.mengel@pik-potsdam.de

  //PetscErrorCode ierr;
  PetscReal rhor, heat_flux, water_flux;
  PetscReal gat;
  //PetscReal gas, gat;
  PetscReal ep5;

  PetscReal tob=  -20.;                        //temperature at the ice surface
  //   PetscReal rhoi=  920.;                      //mean ice density
  PetscReal cpw =  4180.0;                    //Barnier et al. (1995)
  PetscReal lhf =  3.33e+5;                   //latent heat of fusion
  PetscReal atk =  273.15;                    //0 deg C in Kelvin
  //FIXME: can use PISMs surface temp for tob?
  PetscReal cpi =  152.5+7.122*(atk+tob);     //Paterson:"The Physics of Glaciers"

  PetscReal L    = 334000.;                   // [J/Kg]

  // Prescribe the turbulent heat and salt transfer coeff. GAT and GAS

  //gat  = 1.00e-4;   //[m/s] RG3417
  //if (gamma_T_separate_set) {
  //    PetscReal gat_PIG_north = gat_array[0];
  //    PetscReal gat_PIG_south = gat_array[1];
  //    PetscReal gat_TG = gat_array[2];
  //}

  PetscReal gas  = 5.05e-7;   //[m/s] RG3417

  // Calculate
  // density in the boundary layer: rhow
  // and interface pressure pg [dbar],
  // Solve a quadratic equation for the interface salinity sb
  // to determine the melting/freezing rate seta.

  // FIXME: need to calculate water density instead of const value.
  //  call fcn_density(thetao,sal,zice,rho)
  //  rhow = density_0+rho  //was rhow= rho0+rho(i,j,N)
  //  rhow = 1028.0;
  rhor= rhoi/rhow;
  ep5 = gas/rhor;

  // Calculate the melting/freezing rate [m/s]
  meltrate = ep5*(1.0-sal_ocean/sal_base);     //rt thinks this is not needed

  //   heat_flux  = rhow*cpw*gat*(temp_insitu-tf);      // [W/m2]
  //   water_flux =          gas*(sf-sal)/sf;   // [m/s]

  return 0;
}

//FIXME: Is this method really needed???
/* This method is called by pttmpr(), which is called by potit(), which is called
   by calculate_boundlayer_temp_and_salt().*/
PetscErrorCode POGivenTH::adlprt(PetscReal salz,PetscReal temp_insitu, PetscReal pres, PetscReal &adlprt_out){
  // Berechnet aus dem Salzgehalt/psu (SALZ), der in-situ Temperatur/degC
  // (TEMP) und dem in-situ Druck/dbar (PRES) den adiabatischen Temperatur-
  // gradienten/(K Dbar^-1) ADLPRT.
  // Checkwert: ADLPRT =     3.255976E-4 K dbar^-1
  //       fuer SALZ   =    40.0 psu
  //            TEMP   =    40.0 DegC
  //            PRES   = 10000.000 dbar

  PetscReal ds;
  const PetscReal s0 = 35.0;
  const PetscReal a0 = 3.5803e-5,   a1 = 8.5258e-6,   a2 = -6.8360e-8, a3 = 6.6228e-10;
  const PetscReal b0 = 1.8932e-6,   b1 = -4.2393e-8;
  const PetscReal c0 = 1.8741e-8,   c1 = -6.7795e-10, c2 = 8.7330e-12, c3 = -5.4481e-14;
  const PetscReal d0 = -1.1351e-10, d1 = 2.7759e-12;
  const PetscReal e0 = -4.6206e-13, e1 = 1.8676e-14,  e2 = -2.1687e-16;

  ds = salz-s0;
  adlprt_out = (( ( (e2*temp_insitu + e1)*temp_insitu + e0 )*pres + ( (d1*temp_insitu + d0)*ds
                                                                      + ( (c3*temp_insitu + c2)*temp_insitu + c1 )*temp_insitu + c0 ) )*pres
                + (b1*temp_insitu + b0)*ds +  ( (a3*temp_insitu + a2)*temp_insitu + a1 )*temp_insitu + a0);

  return 0;
}

PetscErrorCode POGivenTH::pttmpr(PetscReal salz,PetscReal temp_insitu,PetscReal pres,PetscReal rfpres,
                                 PetscReal& thetao){
  // Berechnet aus dem Salzgehalt/psu (SALZ), der in-situ Temperatur/degC
  // (TEMP) und dem in-situ Druck/dbar (PRES) die potentielle Temperatur/
  // degC (PTTMPR) bezogen auf den Referenzdruck/dbar (RFPRES). Es wird
  // ein Runge-Kutta Verfahren vierter Ordnung verwendet.
  // Checkwert: PTTMPR = 36.89073 DegC
  //       fuer SALZ   =    40.0 psu
  //            TEMP   =    40.0 DegC
  //            PRES   = 10000.000 dbar
  //            RFPRES =     0.000 dbar

  PetscReal ct2  = 0.29289322 , ct3  = 1.707106781;
  PetscReal cq2a = 0.58578644 , cq2b = 0.121320344;
  PetscReal cq3a = 3.414213562, cq3b = -4.121320344;

  //real salz,temp_insitu,pres,rfpres
  PetscReal p,t,dp,dt,q, dd;
  //real adlprt

  p  = pres;
  t  = temp_insitu;
  dp = rfpres-pres;
  adlprt(salz,t,p,dd);
  dt = dp*dd;
  t  = t +0.5*dt;
  q = dt;
  p  = p +0.5*dp;
  adlprt(salz,t,p,dd);
  dt = dp*dd;
  t  = t + ct2*(dt-q);
  q  = cq2a*dt + cq2b*q;
  adlprt(salz,t,p,dd);
  dt = dp*dd;
  t  = t + ct3*(dt-q);
  q  = cq3a*dt + cq3b*q;
  p  = rfpres;
  adlprt(salz,t,p,dd);
  dt = dp*dd;
  thetao = t+ (dt-q-q)/6.0;

  return 0;
}

PetscErrorCode POGivenTH::potit(PetscReal salz,PetscReal thetao,PetscReal pres,PetscReal rfpres, PetscReal &temp_insitu_out){
  // *********************************************************************
  // Berechnet aus dem Salzgehalt[psu] (SALZ), der pot. Temperatur[oC]
  // (PT) und dem Referenzdruck[dbar] (REFPRES) die in-situ Temperatur
  // [oC] (TIN) bezogen auf den in-situ Druck[dbar] (PRES) mit Hilfe
  // eines Iterationsverfahrens aus.

  //integer iter
  //real salz,thetao,pres,rfpres,tin
  //real epsi,tpmd,pt1,ptd,pttmpr

  //tpmd / 0.001 /
  PetscReal tpmd = 0.001, epsi = 0., tin, pt1, ptd;

  for (PetscInt iter=0; iter<101; ++iter){
    //do iter=1,100
    tin  = thetao+epsi;
    //     print "tin=" + str(tin)
    pttmpr(salz,tin,pres,rfpres,pt1);
    //     PetscErrorCode ierr = verbPrintf(2, grid.com, "pt1=%e\n", pt1); CHKERRQ(ierr);
    //     print "pt1=" + str(pt1)
    ptd  = pt1-thetao;
    //     print "ptd=" + str(ptd)
    if(PetscAbs(ptd) < tpmd){
      break;
    }else{
      epsi = epsi-ptd;
    }
    if(iter==100){ SETERRQ(grid.com, 1, "in situ temperature calculation not converging."); }
  }

  temp_insitu_out = tin;
  return 0;
}

PetscErrorCode POGivenTH::sea_level_elevation(PetscReal &result) {
  result = sea_level;
  return 0;
}


