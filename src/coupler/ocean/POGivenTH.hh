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

#ifndef _POGIVENTH_H_
#define _POGIVENTH_H_

#include "PGivenClimate.hh"
#include "POModifier.hh"

class POGivenTH : public PGivenClimate<POModifier,PISMOceanModel>
{

public:
  POGivenTH(IceGrid &g, const NCConfigVariable &conf);
  virtual ~POGivenTH();

  virtual PetscErrorCode init(PISMVars &vars);
  virtual PetscErrorCode update(PetscReal my_t, PetscReal my_dt);

  virtual PetscErrorCode sea_level_elevation(PetscReal &result);

  virtual PetscErrorCode shelf_base_temperature(IceModelVec2S &result);
  virtual PetscErrorCode shelf_base_mass_flux(IceModelVec2S &result);

  virtual PetscErrorCode calculate_boundlayer_temp_and_salt();

  virtual PetscErrorCode shelf_base_temp_salinity_3eqn(PetscInt i, PetscInt j,
						       PetscReal sal_ocean,
						       PetscReal temp_insitu, PetscReal zice,
						       PetscReal &temp_base, PetscReal &sal_base);

  virtual PetscErrorCode compute_meltrate_3eqn(PetscReal rhow,
					       PetscReal rhoi, PetscReal temp_base, PetscReal sal_base,
					       PetscReal sal_ocean, PetscReal &meltrate);

  virtual PetscErrorCode adlprt(PetscReal salz, PetscReal temp_insitu, PetscReal pres, PetscReal &adlprt_out);
  virtual PetscErrorCode pttmpr(PetscReal salz, PetscReal temp_insitu, PetscReal pres,PetscReal rfpres, PetscReal &thetao);
  virtual PetscErrorCode potit(PetscReal salz,PetscReal thetao,PetscReal pres,PetscReal rfpres, PetscReal &temp_insitu_out);

protected:
  IceModelVec2T *shelfbtemp, *shelfbmassflux;
  IceModelVec2T *temp_boundlayer, *salinity_boundlayer;
  IceModelVec2S *ice_thickness;
  IceModelVec2T *theta_ocean, *salinity_ocean;

private:
  PetscErrorCode allocate_POGivenTH();
};

#endif /* _POGIVENTH_H_ */