// Copyright (C) 2011 Ed Bueler
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

#include "iceModel.hh"
#include "Mask.hh"

//! \file iMhydrology.cc Currently, only the most minimal possible hydrology model: diffusion of stored basal water.

//! Explicit time step for diffusion of subglacial water layer bwat.
/*!
See equation (11) in \ref BBssasliding , namely
  \f[W_t = K \nabla^2 W.\f]
The diffusion constant \f$K\f$ is chosen so that the fundamental solution (Green's
function) of this equation has standard deviation \f$\sigma=L\f$ at time t=\c diffusion_time.
Note that \f$2 \sigma^2 = 4 K t\f$.

The time step restriction for the explicit method for this equation is believed
to be so rare, for most values of \c bwat_diffusion_distance and \c bwat_diffusion_time
that we simply halt execution if it occurs.

Uses vWork2d[0] to temporarily store new values for bwat.
 */
PetscErrorCode IceModel::diffuse_bwat() {
  PetscErrorCode  ierr;

  const PetscScalar
    L = config.get("bwat_diffusion_distance"),
    diffusion_time = config.get("bwat_diffusion_time") * secpera; // convert to seconds

  const PetscScalar K = L * L / (2.0 * diffusion_time),
                    Rx = K * (dt_years_TempAge * secpera) / (grid.dx * grid.dx),
                    Ry = K * (dt_years_TempAge * secpera) / (grid.dy * grid.dy),
                    oneM4R = 1.0 - 2.0 * Rx - 2.0 * Ry;
  if (oneM4R <= 0.0) {
    SETERRQ(1,
       "PISM ERROR: diffuse_bwat() requires 1 - 2Rx - 2Ry <= 0 and an explicit step;\n"
       "  current timestep means this method is unstable; believed so rare that\n"
       "  timestep restriction is not part of the adaptive scheme ... ENDING!\n");
  }

  // communicate ghosted values so neighbors are valid;
  // note that temperatureStep() and enthalpyAndDrainageStep() modify vHmelt,
  // but they do not update ghosts because only the current process needs that
  ierr = vHmelt.beginGhostComm(); CHKERRQ(ierr);
  ierr = vHmelt.endGhostComm(); CHKERRQ(ierr);

  PetscScalar **bwatnew; 
  ierr = vHmelt.begin_access(); CHKERRQ(ierr);
  ierr = vWork2d[0].get_array(bwatnew); CHKERRQ(ierr);
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {
      bwatnew[i][j] = oneM4R * vHmelt(i,j)
                       + Rx * (vHmelt(i+1,j  ) + vHmelt(i-1,j  ))
                       + Ry * (vHmelt(i  ,j+1) + vHmelt(i  ,j-1));
    }
  }
  ierr = vWork2d[0].end_access(); CHKERRQ(ierr);
  ierr = vHmelt.end_access(); CHKERRQ(ierr);

  // finally copy new into vHmelt and communicate ghosts at the same time
  ierr = vWork2d[0].beginGhostComm(vHmelt); CHKERRQ(ierr);
  ierr = vWork2d[0].endGhostComm(vHmelt); CHKERRQ(ierr);

  return 0;
}




//! removes the dry grounding-line wall in bwa
/*!
This is a fiddle that removes the dry grounding-line wall in bwat

Uses vWork2d[0] to temporarily store new values for bwat.
 */
PetscErrorCode IceModel::fixDryWall_bwat() {
  PetscErrorCode  ierr;  
    
    // communicate ghosted values so neighbors are valid;
  // note that temperatureStep() and enthalpyAndDrainageStep() modify vbwat, -->maria: AFTER or BEFORE doing this procedure?
  // but they do not update ghosts because only the current process needs that
  ierr = vHmelt.beginGhostComm(); CHKERRQ(ierr);
  ierr = vHmelt.endGhostComm(); CHKERRQ(ierr);
 
  MaskQuery mask(vMask);
  
  PetscScalar **bwatnew; 
  ierr = vHmelt.begin_access(); CHKERRQ(ierr);
  ierr = vMask.begin_access();  CHKERRQ(ierr);
  ierr =  vbed.begin_access();  CHKERRQ(ierr);
  ierr = vWork2d[0].get_array(bwatnew); CHKERRQ(ierr);
  
  for (PetscInt i=grid.xs; i<grid.xs+grid.xm; ++i) {
    for (PetscInt j=grid.ys; j<grid.ys+grid.ym; ++j) {

// // This is a fiddle that eliminates the "wall" of bwat=0 at the grounding line (the last grounded box)
if( 
  mask.grounded_ice(i, j) && vbed(i, j)<0 
  && (mask.floating_ice(i+1,j  ) ||
//     mask.floating_ice(i+1,j+1) ||
//     mask.floating_ice(i+1,j-1) ||
    mask.floating_ice(i,j+1) ||
    mask.floating_ice(i,j-1) ||
//     mask.floating_ice(i-1,j+1) ||
    mask.floating_ice(i-1,j) //||
//     mask.floating_ice(i-1,j-1)
  )
  && ( 
  (vHmelt(i+1,j  )==2 && mask.grounded_ice(i+1,j  ))
//   || (vHmelt(i+1,j+1)==2 && mask.grounded_ice(i+1,j+1))
//   || (vHmelt(i+1,j-1)==2 && mask.grounded_ice(i+1,j-1))
  || (vHmelt(i,j+1)==2 && mask.grounded_ice(i,j+1))
  || (vHmelt(i,j-1)==2 && mask.grounded_ice(i,j-1))
//   || (vHmelt(i-1,j+1)==2 && mask.grounded_ice(i-1,j+1))
  || (vHmelt(i-1,j)==2 && mask.grounded_ice(i-1,j))
//   || (vHmelt(i-1,j-1)==2 &&mask.grounded_ice(i-1,j-1))
  )
){
	bwatnew[i][j] = 2;	 
}else{
    bwatnew[i][j] = vHmelt(i,j);
}
	
	
		       
    }
  }
  ierr = vWork2d[0].end_access(); CHKERRQ(ierr);
  ierr = vHmelt.end_access(); CHKERRQ(ierr);
  ierr = vMask.end_access(); CHKERRQ(ierr);
  ierr =       vbed.end_access(); CHKERRQ(ierr);

  // finally copy new into vHmelt and communicate ghosts at the same time
  ierr = vWork2d[0].beginGhostComm(vHmelt); CHKERRQ(ierr);
  ierr = vWork2d[0].endGhostComm(vHmelt); CHKERRQ(ierr);


  return 0;
}
