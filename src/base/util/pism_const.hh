// Copyright (C) 2007--2011 Jed Brown, Ed Bueler and Constantine Khroulev
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

#ifndef __pism_const_hh
#define __pism_const_hh

#include <gsl/gsl_math.h>
#include <petsc.h>
#include <string>
#include <vector>
#include <set>
#include "NCVariable.hh"


// use namespace std BUT remove trivial namespace browser from doxygen-erated HTML source browser
/// @cond NAMESPACE_BROWSER
using namespace std;
/// @endcond

extern const char *PISM_Revision;
extern const char *PISM_DefaultConfigFile;

const PetscScalar secpera    = 3.15569259747e7; // The constant used in UDUNITS
						// (src/udunits/pismudunits.dat)
const PetscScalar pi         = M_PI;		// defined in gsl/gsl_math.h

enum PismMask {
  MASK_UNKNOWN          = -1,
  MASK_ICE_FREE_BEDROCK = 0,
  MASK_GROUNDED   = 2,
  MASK_FLOATING         = 3,
  MASK_ICE_FREE_OCEAN   = 4
};

enum PISM_CELL_TYPE {
  GROUNDED_MARGIN_EMPTY   = 0,  // ice-free land next to ice
  GROUNDED_MARGIN_FULL    = 1,  // ice next to ice-free land
  GROUNDED_INTERIOR_EMPTY = 2,  // ice-free land away from ice
  GROUNDED_INTERIOR_FULL  = 3,  // grounded ice interior
  OCEAN_MARGIN_EMPTY      = 4,  // ocean next to ice
  OCEAN_MARGIN_FULL       = 5,  // shelf next to ice-free ocean
  OCEAN_INTERIOR_EMPTY    = 6,  // ocean away from ice
  OCEAN_INTERIOR_FULL     = 7   // ice shelf interior
};

enum PISM_MASK_FLAG {
  FLAG_IS_FULL = 1,
  FLAG_IS_INTERIOR = 2,
  FLAG_IS_OCEAN = 4
};

enum PismIcebergMask {
  ICEBERGMASK_NO_ICEBERG = -3,
  ICEBERGMASK_NOT_SET = 0,
  ICEBERGMASK_ICEBERG_CAND = 2,
  ICEBERGMASK_STOP_OCEAN = 3,
  ICEBERGMASK_STOP_ATTACHED = 4
};

enum PismGroundedMarginMask {
  MASK_ICE_NEAR_BEDROCK = 0,
  MASK_BEDROCK_NEAR_ICE = 1,
};

const PetscInt TEMPORARY_STRING_LENGTH = 32768; // 32KiB ought to be enough.

bool is_increasing(const vector<double> &a);

PetscErrorCode setVerbosityLevel(PetscInt level);
PetscInt       getVerbosityLevel();
PetscErrorCode verbosityLevelFromOptions();
PetscErrorCode verbPrintf(const int thresh, MPI_Comm comm,const char format[],...);

void endPrintRank();

void PISMEnd()  __attribute__((noreturn));
void PISMEndQuiet()  __attribute__((noreturn));

string pism_timestamp();
string pism_username_prefix();
string pism_args_string();
string pism_filename_add_suffix(string filename, string separator, string suffix);

bool ends_with(string str, string suffix);

inline bool set_contains(set<string> S, string name) {
  return (S.find(name) != S.end());
}

//! \brief Convert a quantity from unit1 to unit2.
/*!
 * Example: convert(1, "m/year", "m/s").
 *
 * Please avoid using in computationally-intensive code.
 */
inline double convert(double value, const char spec1[], const char spec2[]) {
  utUnit unit1, unit2;
  double slope, intercept;
  int errcode;

  errcode = utScan(spec1, &unit1);
  if (errcode != 0) {
#ifdef PISM_DEBUG
    PetscPrintf(MPI_COMM_SELF, "utScan failed trying to parse %s\n", spec1);
    PISMEnd();
#endif
    return GSL_NAN;
  }

  errcode = utScan(spec2, &unit2);
  if (errcode != 0) {
#ifdef PISM_DEBUG
    PetscPrintf(MPI_COMM_SELF, "utScan failed trying to parse %s\n", spec2);
    PISMEnd();
#endif
    return GSL_NAN;
  }

  errcode = utConvert(&unit1, &unit2, &slope, &intercept);
  if (errcode != 0) {
#ifdef PISM_DEBUG
    PetscPrintf(MPI_COMM_SELF, "utConvert failed trying to convert %s to %s\n", spec1, spec2);
    PISMEnd();
#endif
    return GSL_NAN;
  }

  return value * slope + intercept;
}

// handy functions for processing options:
PetscErrorCode PISMOptionsList(MPI_Comm com, string opt, string text, set<string> choices,
			       string default_value, string &result, bool &flag);

PetscErrorCode PISMOptionsString(string option, string text,
				 string &result, bool &flag, bool allow_empty_arg = false);
PetscErrorCode PISMOptionsStringArray(string opt, string text, string default_value,
				      vector<string>& result, bool &flag);

PetscErrorCode PISMOptionsInt(string option, string text,
			      PetscInt &result, bool &is_set);
PetscErrorCode PISMOptionsIntArray(string option, string text,
				   vector<PetscInt> &result, bool &is_set);

PetscErrorCode PISMOptionsReal(string option, string text,
			       PetscReal &result, bool &is_set);
PetscErrorCode PISMOptionsRealArray(string option, string text,
				    vector<PetscReal> &result, bool &is_set);

PetscErrorCode PISMOptionsIsSet(string option, bool &result);
PetscErrorCode PISMOptionsIsSet(string option, string descr, bool &result);

PetscErrorCode ignore_option(MPI_Comm com, const char name[]);
PetscErrorCode check_old_option_and_stop(
    MPI_Comm com, const char old_name[], const char new_name[]);
PetscErrorCode stop_if_set(MPI_Comm com, const char name[]);
PetscErrorCode parse_range(
    MPI_Comm com, string str, double *a, double *delta, double *b);
PetscErrorCode parse_times(MPI_Comm com, string str, vector<double> &result);

// usage message and required options; drivers use these
PetscErrorCode stop_on_version_option();
PetscErrorCode show_usage_and_quit(
    MPI_Comm com, const char execname[], const char usage[]);
PetscErrorCode show_usage_check_req_opts(
    MPI_Comm com, const char execname[], vector<string> required_options,
    const char usage[]);

// config file initialization:
PetscErrorCode init_config(MPI_Comm com, PetscMPIInt rank,
			   NCConfigVariable &config, NCConfigVariable &overrides);

// debugging:
PetscErrorCode pism_wait_for_gdb(MPI_Comm com, PetscMPIInt rank);

#endif
