// Copyright (C) 2011 Matthias Mengel and Torsten Albrecht
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

#include <cmath>
#include <cstring>
#include <petscda.h>
#include "iceModel.hh"
#include "Mask.hh"
#include "PISMStressBalance.hh"
#include "PISMOcean.hh"

//! \brief This method implements melting at grounded ocean margins.
/*!
  TODO: Give some detailed description here.
*/

PetscErrorCode IceModel::groundedCalving() {
  PetscErrorCode ierr;
  bool calvMethod_set;
  string GroundedCalvingMethod;
  set<string> GroundCalv_choices;
  GroundCalv_choices.insert("constant");
  GroundCalv_choices.insert("eigen");
  ierr = PISMOptionsList(grid.com,"-grounded_calving", "specifies the grounded calving calculation method",
         GroundCalv_choices, "constant", GroundedCalvingMethod, calvMethod_set); CHKERRQ(ierr);
  if (!calvMethod_set) {
    ierr = PetscPrintf(grid.com,
           "PISM ERROR: Please specify an method for grounded calving.\n");
    CHKERRQ(ierr);
    PISMEnd();
  }
  
  if (GroundedCalvingMethod == "constant"){
    ierr = groundedCalvingConst(); CHKERRQ(ierr);
  }
  if (GroundedCalvingMethod == "eigen"){
    ierr = groundedEigenCalving(); CHKERRQ(ierr);
  }
  
  return 0;
}


//! \brief This method implements melting at grounded ocean margins.
/*!
  TODO: Give some detailed description here.
*/
PetscErrorCode IceModel::groundedEigenCalving() {
  const PetscScalar   dx = grid.dx, dy = grid.dy;
  PetscErrorCode ierr;
  ierr = verbPrintf(4, grid.com, "######### groundedEigenCalving() start \n");    CHKERRQ(ierr);

  bool eigcalv_ground_factor_set;
  PetscReal eigcalv_ground_factor;
  ierr = PISMOptionsReal("-eigcalv_ground_factor", "specifies eigen calving factor for grounded margins.", eigcalv_ground_factor,  eigcalv_ground_factor_set); CHKERRQ(ierr);
  if (!eigcalv_ground_factor_set) {
    ierr = PetscPrintf(grid.com, "PISM ERROR: Please specify eigen calving factor for grounded margins.\n");
    CHKERRQ(ierr);
    PISMEnd();
  }
  bool thresh_coeff_set;
  PetscReal thresh_coeff = 1.0;
  ierr = PISMOptionsReal("-thresh_coeff", "specifies a coefficient to avoid oscillations between HrefG and full cell", thresh_coeff, thresh_coeff_set); CHKERRQ(ierr);

  bool landeigencalving;
  ierr = PISMOptionsIsSet("-landeigencalving", "Use eigenCalvingGround also on land above SL.", landeigencalving); CHKERRQ(ierr);

  double ocean_rho = config.get("sea_water_density");
  double ice_rho   = config.get("ice_density");
  double rhofrac   = ice_rho/ocean_rho;
  // Distance (grid cells) from calving front where strain rate is evaluated
  PetscInt offset = 2;

  PetscReal sea_level = 0;
  if (ocean != NULL) {
    ierr = ocean->sea_level_elevation(sea_level); CHKERRQ(ierr);
  } else { SETERRQ(2, "PISM ERROR: ocean == NULL"); }

  MaskQuery mask(vMask);

  IceModelVec2S vHnew = vWork2d[0];
  ierr = vH.copy_to(vHnew); CHKERRQ(ierr);
  ierr = vH.begin_access(); CHKERRQ(ierr);
  ierr = vHnew.begin_access(); CHKERRQ(ierr);
  ierr = vHavgGround.begin_access(); CHKERRQ(ierr);
  ierr = vHrefGround.begin_access(); CHKERRQ(ierr);
  ierr = vTestVar.begin_access(); CHKERRQ(ierr);
  ierr = vbed.begin_access(); CHKERRQ(ierr);
  ierr = vPrinStrain1.begin_access(); CHKERRQ(ierr);
  ierr = vPrinStrain2.begin_access(); CHKERRQ(ierr);
  ierr = vMask.begin_access(); CHKERRQ(ierr);

  IceModelVec2S vDiffCalvHeight = vWork2d[1];
  ierr = vDiffCalvHeight.set(0.0); CHKERRQ(ierr);
  ierr = vDiffCalvHeight.begin_access(); CHKERRQ(ierr);

  for (PetscInt i = grid.xs; i < grid.xs + grid.xm; ++i) {
    for (PetscInt j = grid.ys; j < grid.ys + grid.ym; ++j) {   
      // we should substitute these definitions, can we have a more flexible mask class
      // that we can use to create a mask object that is not the default mask and can
      // handle part grid cells?
      bool grounded_ice     = vH(i,j) > 0.0 && vbed(i,j) > (sea_level - rhofrac*vH(i,j));
      bool part_grid_cell   = vHrefGround(i,j) > 0.0;
      bool below_sealevel   = (vbed(i,j) + sea_level) < 0;
      bool free_ocean       = vH(i,j) == 0.0 && below_sealevel && !part_grid_cell;
      bool grounded_e = vH(i+1,j)>0.0 && vbed(i+1,j)>(sea_level-rhofrac*vH(i+1,j)) && (vbed(i+1,j)+sea_level)<0;
      bool grounded_w = vH(i-1,j)>0.0 && vbed(i-1,j)>(sea_level-rhofrac*vH(i-1,j)) && (vbed(i-1,j)+sea_level)<0;
      bool grounded_n = vH(i,j+1)>0.0 && vbed(i,j+1)>(sea_level-rhofrac*vH(i,j+1)) && (vbed(i,j+1)+sea_level)<0;
      bool grounded_s = vH(i,j-1)>0.0 && vbed(i,j-1)>(sea_level-rhofrac*vH(i,j-1)) && (vbed(i,j-1)+sea_level)<0;
      bool next_to_grounded = grounded_e || grounded_w || grounded_n || grounded_s;
      // ocean front is where no partially filled grid cell is in front.
      bool at_ocean_front_e = ( vH(i+1,j) == 0.0 && ((vbed(i+1,j) + sea_level) < 0 && vHrefGround(i+1,j) == 0.0) );
      bool at_ocean_front_w = ( vH(i-1,j) == 0.0 && ((vbed(i-1,j) + sea_level) < 0 && vHrefGround(i-1,j) == 0.0) );
      bool at_ocean_front_n = ( vH(i,j+1) == 0.0 && ((vbed(i,j+1) + sea_level) < 0 && vHrefGround(i,j+1) == 0.0) );
      bool at_ocean_front_s = ( vH(i,j-1) == 0.0 && ((vbed(i,j-1) + sea_level) < 0 && vHrefGround(i,j-1) == 0.0) );
      bool at_ocean_front   = at_ocean_front_e || at_ocean_front_w || at_ocean_front_n || at_ocean_front_s;

//       vTestVar(i,j) = vbed(i,j) - (sea_level - rhofrac*vH(i,j));

                               
      if( part_grid_cell && (at_ocean_front && below_sealevel || landeigencalving)){
        PetscScalar dHref = 0.0, Face = 0.0, calvrateHorizontal = 0.0,
        eigen1 = 0.0, eigen2 = 0.0, 
        eigenCalvOffset = 0.0; // if it's not exactly the zero line of
                               // transition from compressive to extensive flow
                               // regime;
        // Counting adjacent grounded boxes (with distance "offset")
        PetscInt M = 0;

        if ( at_ocean_front_e ) { Face+= 1.0/dy; }
        if ( at_ocean_front_w ) { Face+= 1.0/dy; }
        if ( at_ocean_front_n ) { Face+= 1.0/dx; }
        if ( at_ocean_front_s ) { Face+= 1.0/dx; }

        // make this less rough if in use for future.
        if ( landeigencalving ) Face = 1.0/dx;

        if ( mask.grounded_ice(i + offset, j) && !mask.ice_margin(i + offset, j)){
          eigen1 += vPrinStrain1(i + offset, j);
          eigen2 += vPrinStrain2(i + offset, j);
          M += 1;
        }
        if ( mask.grounded_ice(i - offset, j) && !mask.ice_margin(i - offset, j)){
          eigen1 += vPrinStrain1(i - offset, j);
          eigen2 += vPrinStrain2(i - offset, j);
          M += 1;
        }
        if ( mask.grounded_ice(i, j + offset) && !mask.ice_margin(i , j + offset)){
          eigen1 += vPrinStrain1(i, j + offset);
          eigen2 += vPrinStrain2(i, j + offset);
          M += 1;
        }
        if ( mask.grounded_ice(i, j - offset) && !mask.ice_margin(i , j - offset)){
          eigen1 += vPrinStrain1(i, j - offset);
          eigen2 += vPrinStrain2(i, j - offset);
          M += 1;
        }
        if (M > 0) {
          eigen1 /= M;
          eigen2 /= M;
        }


        // calving law
        if ( eigen2 > eigenCalvOffset && eigen1 > 0.0) { // if spreading in all directions
          calvrateHorizontal = eigcalv_ground_factor * eigen1 * (eigen2 - eigenCalvOffset);
          // eigen1 * eigen2 has units [s^ - 2] and calvrateHorizontal [m*s^1]
          // hence, eigcalv_ground_factor has units [m*s]
        } else calvrateHorizontal = 0.0;

        // calculate mass loss with respect to the associated ice thickness and the grid size:
        PetscScalar calvrate = calvrateHorizontal * vHavgGround(i,j) * Face; // in m/s
        // dHref corresponds to the height we have to cut off to mimic a
        // a constant horizontal retreat of a part grid cell.
        // volume_partgrid = Href * dx*dy
        // area_partgrid   = volume_partgrid/Havg = Href/Havg * dx*dy
        // calv_velocity   = const * d/dt(area_partgrid/dy) = const * dHref/dt * dx/Havg    
        dHref = calvrate * dt;
        ierr = verbPrintf(2, grid.com,"dHref=%e at i=%d, j=%d\n",dHref,i,j); CHKERRQ(ierr);
        if( vHrefGround(i,j) > dHref ){
          // enough ice to calv from partial cell
          vHrefGround(i,j) -= dHref;
        } else {
        PetscInt N = 0;
        // count grounded neighbours
        if ( grounded_e ) N++;
        if ( grounded_w ) N++;
        if ( grounded_n ) N++;
        if ( grounded_s ) N++;
//         vTestVar(i,j) = N;
        // kill partial cell and save for redistribution to grounded neighbours
        // isolated (N==0) PGG cell at ocean front is killed without redistribution.
        if (N > 0) vDiffCalvHeight(i,j) = (dHref - vHrefGround(i,j))/N;
        vHrefGround(i,j)     = 0.0;
        }
      }
    }
  }

  ierr = vDiffCalvHeight.end_access(); CHKERRQ(ierr);

  ierr = vDiffCalvHeight.beginGhostComm(); CHKERRQ(ierr);
  ierr = vDiffCalvHeight.endGhostComm(); CHKERRQ(ierr);

  ierr = vDiffCalvHeight.begin_access(); CHKERRQ(ierr);
  ierr = vHrefThresh.begin_access(); CHKERRQ(ierr);
  for (PetscInt i = grid.xs; i < grid.xs + grid.xm; ++i) {
    for (PetscInt j = grid.ys; j < grid.ys + grid.ym; ++j) {

      PetscScalar restCalvHeight = 0.0;
      bool grounded_ice   = vH(i,j) > 0.0 && vbed(i,j) > (sea_level - rhofrac*vH(i,j));
      bool below_sealevel = (vbed(i,j) + sea_level) < 0;
      // if grounded and DiffCalv in a neighbouring cell
      if ( grounded_ice && below_sealevel &&
           (vDiffCalvHeight(i + 1, j) > 0.0 || vDiffCalvHeight(i - 1, j) > 0.0 ||
            vDiffCalvHeight(i, j + 1) > 0.0 || vDiffCalvHeight(i, j - 1) > 0.0 )) {

        restCalvHeight = vDiffCalvHeight(i + 1, j) + vDiffCalvHeight(i - 1, j) +
                         vDiffCalvHeight(i, j + 1) + vDiffCalvHeight(i, j - 1);

        vHrefGround(i, j) = vH(i, j) - restCalvHeight; // in m
        PetscSynchronizedPrintf(grid.com,"make Hnew=%e a Href=%e cell with rCalv= %e at i=%d, j=%d\n",vH(i, j),vHrefGround(i, j), restCalvHeight,i,j);

//         vTestVar(i,j) = restCalvHeight;

        vHrefThresh(i,j) = vH(i, j) * thresh_coeff;
        vHnew(i, j)      = 0.0;

        if(vHrefGround(i, j) < 0.0) { // i.e. terminal floating ice grid cell has calved off completely.
          // We do not account for further calving ice-inwards!
          vHrefGround(i, j) = 0.0;
        }
      }
    }
  }

  // finally copy vHnew into vH and communicate ghosted values
  ierr = vHnew.beginGhostComm(vH); CHKERRQ(ierr);
  ierr = vHnew.endGhostComm(vH); CHKERRQ(ierr);

  ierr = vHavgGround.end_access(); CHKERRQ(ierr);
  ierr = vHrefGround.end_access(); CHKERRQ(ierr);
  ierr = vHrefThresh.end_access(); CHKERRQ(ierr);
  ierr = vDiffCalvHeight.end_access(); CHKERRQ(ierr);
  ierr = vHnew.end_access(); CHKERRQ(ierr);
  ierr = vH.end_access(); CHKERRQ(ierr);
  ierr = vbed.end_access(); CHKERRQ(ierr);
  ierr = vTestVar.end_access(); CHKERRQ(ierr);
  ierr = vPrinStrain1.end_access(); CHKERRQ(ierr);
  ierr = vPrinStrain2.end_access(); CHKERRQ(ierr);
  ierr = vMask.end_access(); CHKERRQ(ierr);

  return 0;
}


//! \brief This method implements melting at grounded ocean margins.
/*!
  TODO: Give some detailed description here.
*/
PetscErrorCode IceModel::groundedCalvingConst() {
  const PetscScalar   dx = grid.dx, dy = grid.dy;
  PetscErrorCode ierr;
  ierr = verbPrintf(4, grid.com, "######### groundedCalvingConst() start \n");    CHKERRQ(ierr);

  bool melt_factor_set;
  PetscReal ocean_melt_factor;
  ierr = PISMOptionsReal("-ocean_melt_factor", "specifies constant melt factor for oceanic melt at grounded margins.", ocean_melt_factor,  melt_factor_set); CHKERRQ(ierr);
  if (!melt_factor_set) {
    ierr = PetscPrintf(grid.com, "PISM ERROR: Please specify melt coefficient for ocean melt.\n");
    CHKERRQ(ierr);
    PISMEnd();
  }
  bool thresh_coeff_set;
  PetscReal thresh_coeff = 1.0;
  ierr = PISMOptionsReal("-thresh_coeff", "specifies a coefficient to avoid oscillations between HrefG and full cell", thresh_coeff, thresh_coeff_set); CHKERRQ(ierr);

  double ocean_rho = config.get("sea_water_density");
  double ice_rho   = config.get("ice_density");
  double rhofrac   = ice_rho/ocean_rho;
  
  PetscReal sea_level = 0;
  if (ocean != NULL) {
    ierr = ocean->sea_level_elevation(sea_level); CHKERRQ(ierr);
  } else { SETERRQ(2, "PISM ERROR: ocean == NULL"); }

  IceModelVec2S vHnew = vWork2d[0];
  ierr = vH.copy_to(vHnew); CHKERRQ(ierr);
  ierr = vH.begin_access(); CHKERRQ(ierr);
  ierr = vHnew.begin_access(); CHKERRQ(ierr);
  ierr = vHavgGround.begin_access(); CHKERRQ(ierr);
  ierr = vHrefGround.begin_access(); CHKERRQ(ierr);
  ierr = vTestVar.begin_access(); CHKERRQ(ierr);
  ierr = vbed.begin_access(); CHKERRQ(ierr);
  
  IceModelVec2S vDiffCalvHeight = vWork2d[1];
  ierr = vDiffCalvHeight.set(0.0); CHKERRQ(ierr);
  ierr = vDiffCalvHeight.begin_access(); CHKERRQ(ierr);
  
  for (PetscInt i = grid.xs; i < grid.xs + grid.xm; ++i) {
    for (PetscInt j = grid.ys; j < grid.ys + grid.ym; ++j) {
      // we should substitute these definitions, can we have a more flexible mask class
      // that we can use to create a mask object that is not the default mask and can
      // handle part grid cells?
      bool grounded_ice     = vH(i,j) > 0.0 && vbed(i,j) > (sea_level - rhofrac*vH(i,j));
      bool part_grid_cell   = vHrefGround(i,j) > 0.0;
      bool below_sealevel   = (vbed(i,j) + sea_level) < 0;
      bool free_ocean       = vH(i,j) == 0.0 && below_sealevel && !part_grid_cell;

      
      bool grounded_e = vH(i+1,j)>0.0 && vbed(i+1,j)>(sea_level-rhofrac*vH(i+1,j)) && (vbed(i+1,j)+sea_level)<0;
      bool grounded_w = vH(i-1,j)>0.0 && vbed(i-1,j)>(sea_level-rhofrac*vH(i-1,j)) && (vbed(i-1,j)+sea_level)<0;
      bool grounded_n = vH(i,j+1)>0.0 && vbed(i,j+1)>(sea_level-rhofrac*vH(i,j+1)) && (vbed(i,j+1)+sea_level)<0;
      bool grounded_s = vH(i,j-1)>0.0 && vbed(i,j-1)>(sea_level-rhofrac*vH(i,j-1)) && (vbed(i,j-1)+sea_level)<0;
      bool next_to_grounded = grounded_e || grounded_w || grounded_n || grounded_s;
      // ocean front is where no partially filled grid cell is in front.
      bool at_ocean_front_e = ( vH(i+1,j) == 0.0 && ((vbed(i+1,j) + sea_level) < 0 && vHrefGround(i+1,j) == 0.0) );
      bool at_ocean_front_w = ( vH(i-1,j) == 0.0 && ((vbed(i-1,j) + sea_level) < 0 && vHrefGround(i-1,j) == 0.0) );
      bool at_ocean_front_n = ( vH(i,j+1) == 0.0 && ((vbed(i,j+1) + sea_level) < 0 && vHrefGround(i,j+1) == 0.0) );
      bool at_ocean_front_s = ( vH(i,j-1) == 0.0 && ((vbed(i,j-1) + sea_level) < 0 && vHrefGround(i,j-1) == 0.0) );
      bool at_ocean_front   = at_ocean_front_e || at_ocean_front_w || at_ocean_front_n || at_ocean_front_s;

//       vTestVar(i,j) = vbed(i,j) - (sea_level - rhofrac*vH(i,j));

      if( part_grid_cell && at_ocean_front && below_sealevel ){
        PetscReal dHref = 0.0;
        if ( at_ocean_front_e ) { dHref+= 1/dy; }
        if ( at_ocean_front_w ) { dHref+= 1/dy; }
        if ( at_ocean_front_n ) { dHref+= 1/dx; }
        if ( at_ocean_front_s ) { dHref+= 1/dx; }
        // dHref corresponds to the height we have to cut off to mimic a
        // a constant horizontal retreat of a part grid cell.
        // volume_partgrid = Href * dx*dy
        // area_partgrid   = volume_partgrid/Havg = Href/Havg * dx*dy
        // calv_velocity   = const * d/dt(area_partgrid/dy) = const * dHref/dt * dx/Havg
        if (vHavgGround(i,j) == 0.0) vTestVar(i,j) = 1.0;
        dHref = dHref * vHavgGround(i,j) * ocean_melt_factor * dt/secpera;
        ierr = verbPrintf(2, grid.com,"dHref=%e at i=%d, j=%d\n",dHref,i,j); CHKERRQ(ierr);
        if( vHrefGround(i,j) > dHref ){
          // enough ice to calv from partial cell
          vHrefGround(i,j) -= dHref;
        } else {
        PetscInt N = 0;
        // count grounded neighbours
        if ( grounded_e ) N++;
        if ( grounded_w ) N++;
        if ( grounded_n ) N++;
        if ( grounded_s ) N++;
//         vTestVar(i,j) = N;
        // kill partial cell and save for redistribution to grounded neighbours
        // isolated (N==0) PGG cell at ocean front is killed without redistribution.
        if (N > 0) vDiffCalvHeight(i,j) = (dHref - vHrefGround(i,j))/N;
        vHrefGround(i,j)     = 0.0;
        }
      }
    }
  }

  ierr = vDiffCalvHeight.end_access(); CHKERRQ(ierr);

  ierr = vDiffCalvHeight.beginGhostComm(); CHKERRQ(ierr);
  ierr = vDiffCalvHeight.endGhostComm(); CHKERRQ(ierr);

  ierr = vDiffCalvHeight.begin_access(); CHKERRQ(ierr);
  ierr = vHrefThresh.begin_access(); CHKERRQ(ierr);
  for (PetscInt i = grid.xs; i < grid.xs + grid.xm; ++i) {
    for (PetscInt j = grid.ys; j < grid.ys + grid.ym; ++j) {

      PetscScalar restCalvHeight = 0.0;
      bool grounded_ice = vH(i,j) > 0.0 && vbed(i,j) > (sea_level - rhofrac*vH(i,j));
      bool below_sealevel   = (vbed(i,j) + sea_level) < 0;
      // if grounded and DiffCalv in a neighbouring cell
      if ( grounded_ice && below_sealevel &&
           (vDiffCalvHeight(i + 1, j) > 0.0 || vDiffCalvHeight(i - 1, j) > 0.0 ||
            vDiffCalvHeight(i, j + 1) > 0.0 || vDiffCalvHeight(i, j - 1) > 0.0 )) {

        restCalvHeight = vDiffCalvHeight(i + 1, j) + vDiffCalvHeight(i - 1, j) +
                         vDiffCalvHeight(i, j + 1) + vDiffCalvHeight(i, j - 1);

        vHrefGround(i, j) = vH(i, j) - restCalvHeight; // in m
        PetscSynchronizedPrintf(grid.com,"make Hnew=%e a Href=%e cell with rCalv= %e at i=%d, j=%d\n",vH(i, j),vHrefGround(i, j), restCalvHeight,i,j);
      
//         vTestVar(i,j) = restCalvHeight;
      
        vHrefThresh(i,j) = vH(i, j) * thresh_coeff;
        vHnew(i, j)      = 0.0;

        if(vHrefGround(i, j) < 0.0) { // i.e. terminal floating ice grid cell has calved off completely.
          // We do not account for further calving ice-inwards!
          vHrefGround(i, j) = 0.0;
        }
      }
    }
  }

  // finally copy vHnew into vH and communicate ghosted values
  ierr = vHnew.beginGhostComm(vH); CHKERRQ(ierr);
  ierr = vHnew.endGhostComm(vH); CHKERRQ(ierr);

  ierr = vHavgGround.end_access(); CHKERRQ(ierr);
  ierr = vHrefGround.end_access(); CHKERRQ(ierr);
  ierr = vHrefThresh.end_access(); CHKERRQ(ierr);
  ierr = vDiffCalvHeight.end_access(); CHKERRQ(ierr);
  ierr = vHnew.end_access(); CHKERRQ(ierr);
  ierr = vH.end_access(); CHKERRQ(ierr);
  ierr = vbed.end_access(); CHKERRQ(ierr);
  ierr = vTestVar.end_access(); CHKERRQ(ierr);
  
  return 0;
}

//! \brief This method implements melting at grounded ocean margins.
/*!
  TODO: Give some detailed description here.
*/
PetscErrorCode IceModel::groundedCalvingOld() {
  const PetscScalar   dx = grid.dx, dy = grid.dy;
  PetscErrorCode ierr;
  ierr = verbPrintf(4, grid.com, "######### groundedCalving() start \n");    CHKERRQ(ierr);

  bool melt_factor_set;
  PetscReal ocean_melt_factor;
  ierr = PISMOptionsReal("-ocean_melt_factor", "specifies constant melt factor for oceanic melt at grounded margins.", ocean_melt_factor,  melt_factor_set); CHKERRQ(ierr);
  if (!melt_factor_set) {
    ierr = PetscPrintf(grid.com, "PISM ERROR: Please specify melt coefficient for ocean melt.\n");
    CHKERRQ(ierr);
    PISMEnd();
  }
  
  PetscScalar my_discharge_flux = 0, discharge_flux = 0;

  // is ghost communication really needed here?
  ierr = vH.beginGhostComm(); CHKERRQ(ierr);
  ierr = vH.endGhostComm(); CHKERRQ(ierr);
//   ierr = vMask.beginGhostComm(); CHKERRQ(ierr);
//   ierr = vMask.endGhostComm(); CHKERRQ(ierr);

  double ocean_rho = config.get("sea_water_density");
  double ice_rho = config.get("ice_density");

//   const PetscScalar eigenCalvFactor = config.get("eigen_calving_K");

  PetscReal sea_level = 0;
  if (ocean != NULL) {
    ierr = ocean->sea_level_elevation(sea_level); CHKERRQ(ierr);
  } else { SETERRQ(2, "PISM ERROR: ocean == NULL"); }

  IceModelVec2S vHnew = vWork2d[0];
  ierr = vH.copy_to(vHnew); CHKERRQ(ierr);

//   IceModelVec2S vGroundCalvHeight = vWork2d[1];
//   IceModelVec2S vGroundCalvHeight = vWork2d[1];
//   IceModelVec2S vDiffCalvHeight   = vWork2d[1];
//   ierr = vDiffCalvHeight.set(0.0); CHKERRQ(ierr);
  
//   update_mask_forpartgrid();
//   MaskQuery mask(vMaskPartGrid);

  ierr = vH.begin_access(); CHKERRQ(ierr);
  ierr = vHnew.begin_access(); CHKERRQ(ierr); // vHnew = vH at this point
//   ierr = vMaskPartGrid.begin_access(); CHKERRQ(ierr);
  ierr = vbed.begin_access(); CHKERRQ(ierr);
  ierr = vHrefGround.begin_access(); CHKERRQ(ierr);
  ierr = vTestVar.begin_access(); CHKERRQ(ierr);
  ierr = vGroundCalvHeight.begin_access(); CHKERRQ(ierr);
  ierr = vDiffCalvHeight.begin_access(); CHKERRQ(ierr);

//   IceModelVec2Int part_grid_cell, below_sealevel, free_ocean, grounded_ice, at_ocean_front;
//   ierr = part_grid_cell.begin_access(); CHKERRQ(ierr);

/*  for (PetscInt i = grid.xs; i < grid.xs + grid.xm; ++i) {
    for (PetscInt j = grid.ys; j < grid.ys + grid.ym; ++j) {
//       part_grid_cell(i,j)  = 0;//( vHrefGround(i,j) > 0.0 );
      below_sealevel(i,j)  = ( (vbed(i,j) + sea_level) < 0 );
/*      free_ocean(i,j)      = ( vH(i, j) == 0.0 && below_sealevel(i,j) && !part_grid_cell(i,j) );
      grounded_ice(i,j)    = ( vbed(i + 1, j) > (sea_level - ice_rho / ocean_rho*vH(i,j)) );*/
//       at_ocean_front(i,j)  = (grounded_ice(i,j) || part_grid_cell(i,j)) &&
//                                     ( free_ocean(i+1,j) || free_ocean(i-1,j) ||
//                                       free_ocean(i,j+1) || free_ocean(i,j-1) );

//     }
//   }
//  */ ierr = part_grid_cell.end_access(); CHKERRQ(ierr);

                           
  for (PetscInt i = grid.xs; i < grid.xs + grid.xm; ++i) {
    for (PetscInt j = grid.ys; j < grid.ys + grid.ym; ++j) {
      vTestVar(i,j) = 0.0;
      vGroundCalvHeight(i,j) = 0.0;
      vDiffCalvHeight(i,j)   = 0.0;
      // we should substitute these definitions, can we have a more flexible mask class
      // that we can use to create a mask object that is not the default mask and can
      // handle part grid cells?
      bool grounded_ice     = vbed(i,j) > (sea_level - ice_rho / ocean_rho*vH(i,j));
      bool part_grid_cell   = vHrefGround(i,j) > 0.0;
      bool below_sealevel   = (vbed(i,j) + sea_level) < 0;
      bool free_ocean       = vH(i,j) == 0.0 && below_sealevel && !part_grid_cell;

      // ocean front is where no partially filled grid cell is in front.
      bool at_ocean_front_e = ( (grounded_ice || part_grid_cell) && 
                                (vH(i+1,j) == 0.0 && ((vbed(i+1,j) + sea_level) < 0) && vHrefGround(i+1,j) == 0.0) );
      bool at_ocean_front_w = ( (grounded_ice || part_grid_cell) &&
                                (vH(i-1,j) == 0.0 && ((vbed(i-1,j) + sea_level) < 0) && vHrefGround(i-1,j) == 0.0) );
      bool at_ocean_front_n = ( (grounded_ice || part_grid_cell) &&
                                (vH(i,j+1) == 0.0 && ((vbed(i,j+1) + sea_level) < 0) && vHrefGround(i,j+1) == 0.0) );
      bool at_ocean_front_s = ( (grounded_ice || part_grid_cell) &&
                                (vH(i,j-1) == 0.0 && ((vbed(i,j-1) + sea_level) < 0) && vHrefGround(i,j-1) == 0.0) );
      bool at_ocean_front   = at_ocean_front_e || at_ocean_front_w || at_ocean_front_n || at_ocean_front_s;
/*
      if( part_grid_cell) vTestVar(i,j) += 1.0;
      if( below_sealevel) vTestVar(i,j) += 2.0;
      if( free_ocean)     vTestVar(i,j) += 4.0;
      if( grounded_ice)   vTestVar(i,j) += 8.0;
      if( at_ocean_front) vTestVar(i,j) += 20.0;
      */
//       if( grounded_ice  ) vTestVar(i,j) += 20.0;
//       if( part_grid_cell) vTestVar(i,j) += 40.0;
//       if( grounded_ice_e) vTestVar(i,j) += 1.0;
//       if( grounded_ice_w) vTestVar(i,j) += 2.0;
//       if( grounded_ice_n) vTestVar(i,j) += 4.0;
//       if( grounded_ice_s) vTestVar(i,j) += 8.0;
  
      if( at_ocean_front && below_sealevel ){
//         vTestVar(i,j) += 2.0;
//         PetscReal wannaMelt = 0.0;
//         PetscReal oceanMelt = 0.0;
        PetscReal meltArea  = 0.0;
//         if( grounded_ice   ) { wannaMelt = vH(i,j); }
//         if( part_grid_cell ) { wannaMelt = vHrefGround(i,j); }
//         vTestVar(i,j) = wannaMelt;

        if ( at_ocean_front_e ) { meltArea += dy; }
        if ( at_ocean_front_w ) { meltArea += dy; }
        if ( at_ocean_front_n ) { meltArea += dx; }
        if ( at_ocean_front_s ) { meltArea += dx; }

        if ( at_ocean_front_e ) { vTestVar(i,j) +=1; }
        if ( at_ocean_front_w ) { vTestVar(i,j) +=2; }
        if ( at_ocean_front_n ) { vTestVar(i,j) +=4; }
        if ( at_ocean_front_s ) { vTestVar(i,j) +=8; }
        
        vGroundCalvHeight(i,j) = meltArea * ocean_melt_factor * dt/secpera;
//         ierr = verbPrintf(2, grid.com,"!!! GroundCalvHeight=%e, meltArea=%e, dt/secpera=%e at i=%d, j=%d\n",vGroundCalvHeight(i,j),meltArea,dt/secpera,i,j); CHKERRQ(ierr);
      }
        
//         ierr = verbPrintf(2, grid.com,"!!! GroundCalvHeight=%e, oceanMeltdt=%e at i=%d, j=%d\n",vGroundCalvHeight(i,j),oceanMelt*dt,i,j); CHKERRQ(ierr);
//         
//         if( grounded_ice   ) {
//           ierr = verbPrintf(2, grid.com,"!!! grounded_ice, oceanMeltdt=%e,wannaMelt=%e,vH=%e,vHrefGround=%e at i=%d, j=%d\n",oceanMelt*dt,wannaMelt,vH(i,j),vHrefGround(i,j),i,j); CHKERRQ(ierr);
//         }
//         if( part_grid_cell   ) {
//           ierr = verbPrintf(2, grid.com,"!!! part grid cell, oceanMeltdt=%e,wannaMelt=%e,vH=%e,vHrefGround=%e at i=%d, j=%d\n",oceanMelt*dt,wannaMelt,vH(i,j),vHrefGround(i,j),i,j); CHKERRQ(ierr);
//         }
//         if( !part_grid_cell && !grounded_ice ){
//           ierr = verbPrintf(2, grid.com,"!!! shouldnt arrive here, oceanMeltdt=%e,wannaMelt=%e,vH=%e,vHrefGround=%e at i=%d, j=%d\n",oceanMelt*dt,wannaMelt,vH(i,j),vHrefGround(i,j),i,j); CHKERRQ(ierr);
//         }
    
    }
  }

  ierr = vGroundCalvHeight.end_access(); CHKERRQ(ierr);
  ierr = vGroundCalvHeight.beginGhostComm(); CHKERRQ(ierr);
  ierr = vGroundCalvHeight.endGhostComm(); CHKERRQ(ierr);
  ierr = vGroundCalvHeight.begin_access(); CHKERRQ(ierr);
  
      
  for (PetscInt i = grid.xs; i < grid.xs + grid.xm; ++i) {
    for (PetscInt j = grid.ys; j < grid.ys + grid.ym; ++j) {
      
      bool part_grid_cell   = vHrefGround(i,j) > 0.0;
      bool grounded_ice     = vbed(i,j) > (sea_level - ice_rho / ocean_rho*vH(i,j));
      bool grounded_ice_e   = vbed(i+1,j) > (sea_level - ice_rho / ocean_rho*vH(i+1,j));
      bool grounded_ice_w   = vbed(i-1,j) > (sea_level - ice_rho / ocean_rho*vH(i-1,j));
      bool grounded_ice_n   = vbed(i,j+1) > (sea_level - ice_rho / ocean_rho*vH(i,j+1));
      bool grounded_ice_s   = vbed(i,j-1) > (sea_level - ice_rho / ocean_rho*vH(i,j-1));

      if( part_grid_cell && (vGroundCalvHeight(i,j) < vHrefGround(i,j)) ) {
        // enough mass in part grid cell to survive
        vHrefGround(i,j) -= vGroundCalvHeight(i,j);
      } else if( grounded_ice && (vGroundCalvHeight(i,j) < vHnew(i,j)) ) {
        // enough mass in ice cell to survive
        vHnew(i,j)       -= vGroundCalvHeight(i,j);
      } else if( part_grid_cell && (vGroundCalvHeight(i,j) > vHrefGround(i,j)) ){
        // kill partial cell and redistribute to grounded neighbours
        PetscReal restCalv = vGroundCalvHeight(i,j) - vHrefGround(i,j);
        // count the neighbours we can take mass from.
        PetscInt N = 0;
        if ( grounded_ice_e ) { N += 1; }
        if ( grounded_ice_w ) { N += 1; }
        if ( grounded_ice_n ) { N += 1; }
        if ( grounded_ice_s ) { N += 1; }
        if ( N == 0 ) {
          ierr = verbPrintf(2, grid.com,"!!! PISM_WARNING: no grounded neighbour at i=%d, j=%d\n",i,j); CHKERRQ(ierr);
          vTestVar(i,j) += 40.0;
        }

        if ( grounded_ice_e ) { vDiffCalvHeight(i+1,j) += restCalv/N; }
        if ( grounded_ice_w ) { vDiffCalvHeight(i-1,j) += restCalv/N; }
        if ( grounded_ice_n ) { vDiffCalvHeight(i,j+1) += restCalv/N; }
        if ( grounded_ice_s ) { vDiffCalvHeight(i,j-1) += restCalv/N; }
        vHrefGround(i,j) = 0.0;
        vTestVar(i,j) += 20.0;
      } else if( grounded_ice && (vGroundCalvHeight(i,j) > vHnew(i,j)) ){
        ierr = verbPrintf(2, grid.com,"!!! PISM_WARNING: we should not arrive here, as this should be converted to a partial grid cell first, i=%d, j=%d\n",i,j); CHKERRQ(ierr);
      }
    }
  }

  for (PetscInt i = grid.xs; i < grid.xs + grid.xm; ++i) {
    for (PetscInt j = grid.ys; j < grid.ys + grid.ym; ++j) {

      if( vDiffCalvHeight(i,j) != 0 ){
//         vTestVar(i,j) += 1;
        if( vHnew(i,j) == 0 ){
          ierr = verbPrintf(2, grid.com,"!!! PISM_WARNING: there should be grounded ice at the cell i=%d, j=%d\n",i,j); CHKERRQ(ierr);
//           vTestVar(i,j) += 2;
        }

        vHnew(i,j) -= vDiffCalvHeight(i,j);

        if ( vHnew(i,j) < 0 ) {
          ierr = verbPrintf(2, grid.com,"!!! PISM_WARNING: redistribution from grounded calving too high, vHnew=%e is smaller zero, set to zero now, creates mass. i=%d, j=%d\n",vHnew(i,j),i,j); CHKERRQ(ierr);
          vHnew(i,j) = 0.0;
//           vTestVar(i,j) += 4;
        }
      }       
    }
  }

  ierr = vHnew.end_access(); CHKERRQ(ierr);
  ierr = vH.end_access(); CHKERRQ(ierr);
//   ierr = vMaskPartGrid.end_access(); CHKERRQ(ierr);
  ierr = vbed.end_access(); CHKERRQ(ierr);
  ierr = vHrefGround.end_access(); CHKERRQ(ierr);
  ierr = vGroundCalvHeight.end_access(); CHKERRQ(ierr);
  ierr = vDiffCalvHeight.begin_access(); CHKERRQ(ierr);
  ierr = vTestVar.end_access(); CHKERRQ(ierr);

/*
  ierr = PISMGlobalSum(&my_discharge_flux,     &discharge_flux,     grid.com); CHKERRQ(ierr);
  PetscScalar factor = config.get("ice_density") * (dx * dy);
  cumulative_discharge_flux     += discharge_flux     * factor;*/

  ierr = vHnew.beginGhostComm(vH); CHKERRQ(ierr);
  ierr = vHnew.endGhostComm(vH); CHKERRQ(ierr);

  return 0;
}

