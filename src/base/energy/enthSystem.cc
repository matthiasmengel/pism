// Copyright (C) 2009-2011 Andreas Aschwanden and Ed Bueler and Constantine Khroulev
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

#include "enthSystem.hh"
#include <gsl/gsl_math.h>
#include "NCVariable.hh"
#include "iceModelVec.hh"
#include "Mask.hh"

enthSystemCtx::enthSystemCtx(const NCConfigVariable &config,
                             IceModelVec3 &my_Enth3, int my_Mz, string my_prefix)
      : columnSystemCtx(my_Mz, my_prefix) {  // <- critical: sets size of sys
  Mz = my_Mz;

  // set some values so we can check if init was called
  nuEQ     = -1.0;
  iceRcold = -1.0;
  iceRtemp = -1.0;
  lambda   = -1.0;
  a0 = GSL_NAN;
  a1 = GSL_NAN;
  b  = GSL_NAN;

  ice_rho = config.get("ice_density");
  ice_c   = config.get("ice_specific_heat_capacity");
  ice_k   = config.get("ice_thermal_conductivity");

  ice_K   = ice_k / ice_c;
  ice_K0  = ice_K * config.get("enthalpy_temperate_conductivity_ratio");

  u      = new PetscScalar[Mz];
  v      = new PetscScalar[Mz];
  w      = new PetscScalar[Mz];
  Sigma  = new PetscScalar[Mz];
  Enth   = new PetscScalar[Mz];
  Enth_s = new PetscScalar[Mz];  // enthalpy of pressure-melting-point
  R.resize(Mz);

  Enth3 = &my_Enth3;  // points to IceModelVec3
}


enthSystemCtx::~enthSystemCtx() {
  delete [] u;
  delete [] v;
  delete [] w;
  delete [] Sigma;
  delete [] Enth_s;
  delete [] Enth;
}


PetscErrorCode enthSystemCtx::initAllColumns(PetscScalar my_dx,  PetscScalar my_dy,
                                             PetscScalar my_dtTemp,  PetscScalar my_dzEQ) {
  dx     = my_dx;
  dy     = my_dy;
  dtTemp = my_dtTemp;
  dzEQ   = my_dzEQ;
  nuEQ     = dtTemp / dzEQ;
  iceRcold = (ice_K / ice_rho) * dtTemp / PetscSqr(dzEQ);
  iceRtemp = (ice_K0 / ice_rho) * dtTemp / PetscSqr(dzEQ);
  return 0;
}


/*!
In this implementation, \f$k\f$ does not depend on temperature.
 */
PetscScalar enthSystemCtx::k_from_T(PetscScalar /*T*/) {
  return ice_k;
}


PetscErrorCode enthSystemCtx::initThisColumn(bool my_ismarginal, planeStar<int> my_Msk,
                                             PetscScalar my_lambda,
                                             PetscReal /*ice_thickness*/) {
#if (PISM_DEBUG==1)
  if ((nuEQ < 0.0) || (iceRcold < 0.0) || (iceRtemp < 0.0)) {  SETERRQ(PETSC_COMM_SELF, 2,
     "setSchemeParamsThisColumn() should only be called after\n"
     "  initAllColumns() in enthSystemCtx"); }
  if (lambda >= 0.0) {  SETERRQ(PETSC_COMM_SELF, 3,
     "setSchemeParamsThisColumn() called twice (?) in enthSystemCtx"); }
#endif
  ismarginal = my_ismarginal;
  lambda = my_lambda;
  Msk = my_Msk;
  PetscErrorCode ierr =  assemble_R(); CHKERRQ(ierr);
  return 0;
}


PetscErrorCode enthSystemCtx::setBoundaryValuesThisColumn(
            PetscScalar my_Enth_surface) {
#if (PISM_DEBUG==1)
  if ((nuEQ < 0.0) || (iceRcold < 0.0) || (iceRtemp < 0.0)) {  SETERRQ(PETSC_COMM_SELF, 2,
     "setBoundaryValuesThisColumn() should only be called after\n"
     "  initAllColumns() in enthSystemCtx"); }
#endif
  Enth_ks = my_Enth_surface;
  return 0;
}


PetscErrorCode enthSystemCtx::viewConstants(
                     PetscViewer viewer, bool show_col_dependent) {
  PetscErrorCode ierr;

  if (!viewer) {
    ierr = PetscViewerASCIIGetStdout(PETSC_COMM_SELF,&viewer); CHKERRQ(ierr);
  }

  PetscBool iascii;
  ierr = PetscTypeCompare((PetscObject)viewer,PETSCVIEWERASCII,&iascii); CHKERRQ(ierr);
  if (!iascii) { SETERRQ(PETSC_COMM_SELF, 1,"Only ASCII viewer for enthSystemCtx::viewConstants()\n"); }

  ierr = PetscViewerASCIIPrintf(viewer,
                   "\n<<VIEWING enthSystemCtx with prefix '%s':\n",prefix.c_str()); CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,
                     "for ALL columns:\n"); CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,
                     "  dx,dy,dtTemp,dzEQ = %8.2f,%8.2f,%10.3e,%8.2f\n",
                     dx,dy,dtTemp,dzEQ); CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,
                     "  ice_rho,ice_c,ice_k,ice_K,ice_K0 = %10.3e,%10.3e,%10.3e,%10.3e,%10.3e\n",
                     ice_rho,ice_c,ice_k,ice_K,ice_K0); CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,
                     "  nuEQ = %10.3e\n",
                     nuEQ); CHKERRQ(ierr);
  ierr = PetscViewerASCIIPrintf(viewer,
                     "  iceRcold,iceRtemp = %10.3e,%10.3e,\n",
         iceRcold,iceRtemp); CHKERRQ(ierr);
  if (show_col_dependent) {
    ierr = PetscViewerASCIIPrintf(viewer,
                     "for THIS column:\n"
                     "  i,j,ks = %d,%d,%d\n",
                     i,j,ks); CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,
                     "  ismarginal,lambda = %d,%10.3f\n",
                     (int)ismarginal,lambda); CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,
                     "  Enth_ks = %10.3e\n",
                     Enth_ks); CHKERRQ(ierr);
    ierr = PetscViewerASCIIPrintf(viewer,
                     "  a0,a1,b = %10.3e,%10.3e\n",
                     a0,a1,b); CHKERRQ(ierr);
  }
  ierr = PetscViewerASCIIPrintf(viewer,
                     ">>\n\n"); CHKERRQ(ierr);
  return 0;
}


PetscErrorCode enthSystemCtx::checkReadyToSolve() {
  if ((nuEQ < 0.0) || (iceRcold < 0.0) || (iceRtemp < 0.0)) {
    SETERRQ(PETSC_COMM_SELF, 2,  "not ready to solve: need initAllColumns() in enthSystemCtx"); }
  if (lambda < 0.0) {
    SETERRQ(PETSC_COMM_SELF, 3,  "not ready to solve: need setSchemeParamsThisColumn() in enthSystemCtx"); }
  return 0;
}


//! Set coefficients in discrete equation for \f$E = Y\f$ at base of ice.
/*!
This method should only be called if everything but the basal boundary condition
is already set.
 */
PetscErrorCode enthSystemCtx::setDirichletBasal(PetscScalar Y) {
#if (PISM_DEBUG==1)
  PetscErrorCode ierr;
  ierr = checkReadyToSolve(); CHKERRQ(ierr);
  if ((!gsl_isnan(a0)) || (!gsl_isnan(a1)) || (!gsl_isnan(b))) {
    SETERRQ(PETSC_COMM_SELF, 1, "setting basal boundary conditions twice in enthSystemCtx");
  }
#endif
  a0 = 1.0;
  a1 = 0.0;
  b  = Y;
  return 0;
}


//! Set coefficients in discrete equation for Neumann condition at base of ice.
/*!
This method generates the Neumann boundary condition for the linear system.

The Neumann boundary condition is
   \f[ \frac{\partial E}{\partial z} = - \frac{\phi}{K} \f]
where \f$\phi\f$ is the heat flux.  Here \f$K\f$ is allowed to vary, and takes
its value from the value computed in assemble_R().

The boundary condition is combined with the partial differential equation by the
technique of introducing an imaginary point at \f$z=-\Delta z\f$ and then
eliminating it.

The error in the pure conductive and smooth conductivity case is \f$O(\Delta z^2)\f$.

This method should only be called if everything but the basal boundary condition
is already set.
 */
PetscErrorCode enthSystemCtx::setBasalHeatFlux(PetscScalar hf) {
 PetscErrorCode ierr;
#if (PISM_DEBUG==1)
  ierr = checkReadyToSolve(); CHKERRQ(ierr);
  if ((!gsl_isnan(a0)) || (!gsl_isnan(a1)) || (!gsl_isnan(b))) {
    SETERRQ(PETSC_COMM_SELF, 1, "setting basal boundary conditions twice in enthSystemCtx");
  }
#endif
  // extract K from R[0], so this code works even if K=K(T)
  // recall:   R = (ice_K / ice_rho) * dtTemp / PetscSqr(dzEQ)
  const PetscScalar
    K = (ice_rho * PetscSqr(dzEQ) * R[0]) / dtTemp,
    Y = - hf / K;
  const PetscScalar
    Rc = R[0],
    Rr = R[1],
    Rminus = Rc,
    Rplus  = 0.5 * (Rc + Rr);
  a0 = 1.0 + Rminus + Rplus;  // = D[0]
  a1 = - Rminus - Rplus;      // = U[0]
  // next line says
  //   (E(+dz) - E(-dz)) / (2 dz) = Y
  // or equivalently
  //   E(-dz) = E(+dz) + X
  const PetscScalar X = - 2.0 * dzEQ * Y;
  // zero vertical velocity contribution
  b = Enth[0] + Rminus * X;   // = rhs[0]

  planeStar<PetscScalar> ss;

  ierr = Enth3->getPlaneStar_fine(i,j,0,&ss); CHKERRQ(ierr);

  if (!ismarginal) {
  UpEnthu = (u[0] < 0) ? u[0] * (ss.e -  ss.ij) / dx :
                                             u[0] * (ss.ij  - ss.w) / dx;
  UpEnthv = (v[0] < 0) ? v[0] * (ss.n -  ss.ij) / dy :
                                             v[0] * (ss.ij  - ss.s) / dy;
  } else {
    ierr = getMarginalEnth(ss, u[0], v[0], Msk, UpEnthu, UpEnthv); CHKERRQ(ierr);
  }

  b += dtTemp * ((Sigma[0] / ice_rho) - UpEnthu - UpEnthv);  // = rhs[0]

  return 0;
}


//! \brief Assemble the R array.  The R value switches at the CTS.
/*!  In a simple abstract diffusion
  \f[ \frac{\partial u}{\partial t} = D \frac{\partial^2 u}{\partial z^2}, \f]
with time steps \f$\Delta t\f$ and spatial steps \f$\Delta z\f$ we define
  \f[ R = \frac{D \Delta t}{\Delta z^2}. \f]
This is used in an implicit method to write each line in the linear system, for
example [\ref MortonMayers]:
  \f[ -R U_{j-1}^{n+1} + (1+2R) U_j^{n+1} - R U_{j+1}^{n+1} = U_j^n. \f]

In the case of conservation of energy [\ref AschwandenBuelerKhroulevBlatter],
  \f[ u=E \qquad \text{ and } \qquad D = \frac{K}{\rho} \qquad \text{ and } \qquad K = \frac{k}{c}. \f]
Thus
  \f[ R = \frac{k \Delta t}{\rho c \Delta z^2}. \f]
 */
PetscErrorCode enthSystemCtx::assemble_R() {

  for (PetscInt k = 0; k <= ks; k++)
    R[k] = (Enth[k] < Enth_s[k]) ? iceRcold : iceRtemp;

  // R[k] for k > ks are never used
#if (PISM_DEBUG==1)
  for (int k = ks + 1; k < Mz; ++k)
    R[k] = GSL_NAN;
#endif
  return 0;
}


/*! \brief Solve the tridiagonal system, in a single column, which determines
the new values of the ice enthalpy. */
PetscErrorCode enthSystemCtx::solveThisColumn(PetscScalar **x, PetscErrorCode &pivoterrorindex) {
  PetscErrorCode ierr;
#if (PISM_DEBUG==1)
  ierr = checkReadyToSolve(); CHKERRQ(ierr);
  if ((gsl_isnan(a0)) || (gsl_isnan(a1)) || (gsl_isnan(b))) {
    SETERRQ(PETSC_COMM_SELF, 1, "solveThisColumn() should only be called after\n"
               "  setting basal boundary condition in enthSystemCtx"); }
#endif
  // k=0 equation is already established
  // L[0] = 0.0;  // not allocated
  D[0] = a0;
  U[0] = a1;
  rhs[0] = b;

  // generic ice segment in k location (if any; only runs if ks >= 2)
  for (PetscInt k = 1; k < ks; k++) {
    const PetscScalar
        Rminus = 0.5 * (R[k-1] + R[k]  ),
        Rplus  = 0.5 * (R[k]   + R[k+1]);
    L[k] = - Rminus;
    D[k] = 1.0 + Rminus + Rplus;
    U[k] = - Rplus;
    const PetscScalar AA = nuEQ * w[k];
    if (w[k] >= 0.0) {  // velocity upward
      L[k] -= AA * (1.0 - lambda/2.0);
      D[k] += AA * (1.0 - lambda);
      U[k] += AA * (lambda/2.0);
    } else {            // velocity downward
      L[k] -= AA * (lambda/2.0);
      D[k] -= AA * (1.0 - lambda);
      U[k] += AA * (1.0 - lambda/2.0);
    }
    rhs[k] = Enth[k];

    planeStar<PetscScalar> ss;
    ierr = Enth3->getPlaneStar_fine(i,j,0,&ss); CHKERRQ(ierr);

    if (!ismarginal) {
      UpEnthu = (u[k] < 0) ? u[k] * (ss.e -  ss.ij) / dx :
                                               u[k] * (ss.ij  - ss.w) / dx;
      UpEnthv = (v[k] < 0) ? v[k] * (ss.n -  ss.ij) / dy :
                                               v[k] * (ss.ij  - ss.s) / dy;
    } else {
      ierr = getMarginalEnth(ss, u[k], v[k], Msk, UpEnthu, UpEnthv); CHKERRQ(ierr);
    }

    rhs[k] += dtTemp * ((Sigma[k] / ice_rho) - UpEnthu - UpEnthv);

  }

  // set Dirichlet boundary condition at top
  if (ks > 0) L[ks] = 0.0;
  D[ks] = 1.0;
  if (ks < Mz-1) U[ks] = 0.0;
  rhs[ks] = Enth_ks;

  // solve it; note drainage is not addressed yet and post-processing may occur
  pivoterrorindex = solveTridiagonalSystem(ks+1, x);

  // air above
  for (PetscInt k = ks+1; k < Mz; k++) {
    (*x)[k] = Enth_ks;
  }

#if (PISM_DEBUG==1)
  if (pivoterrorindex == 0) {
    // if success, mark column as done by making scheme params and b.c. coeffs invalid
    lambda  = -1.0;
    a0 = GSL_NAN;
    a1 = GSL_NAN;
    b  = GSL_NAN;
  }
#endif
  return 0;
}

//! View the tridiagonal system A x = b to a PETSc viewer, both A as a full matrix and b as a vector.
PetscErrorCode enthSystemCtx::viewSystem(PetscViewer viewer) const {
  PetscErrorCode ierr;
  string info;
  info = prefix + "_A";
  ierr = viewMatrix(viewer,info.c_str()); CHKERRQ(ierr);
  info = prefix + "_rhs";
  ierr = viewVectorValues(viewer,rhs,nmax,info.c_str()); CHKERRQ(ierr);
  info = prefix + "_R";
  ierr = viewVectorValues(viewer,&R[0],Mz,info.c_str()); CHKERRQ(ierr);
  return 0;
}

PetscErrorCode enthSystemCtx::getMarginalEnth(planeStar<PetscScalar> ss, PetscScalar u0, PetscScalar v0,
                                              planeStar<int> M,
                                              PetscScalar &UpEnthu, PetscScalar &UpEnthv) {
  Mask m;

  if (u0 < 0 && !m.ice_free(M.e)){
    UpEnthu = u0 * (ss.e -  ss.ij) / dx;
  } else if (u0 > 0  && !m.ice_free(M.w)){
    UpEnthu = u0 * (ss.ij -  ss.w) / dx;
  } else {
    UpEnthu = 0.0;
  }

  if (v0 < 0 && !m.ice_free(M.n)){
    UpEnthv = v0 * (ss.n -  ss.ij) / dx;
  } else if (v0 > 0  && !m.ice_free(M.s)){
    UpEnthv = v0 * (ss.ij -  ss.s) / dx;
  } else {
    UpEnthv = 0.0;
  }
  return 0;
}
