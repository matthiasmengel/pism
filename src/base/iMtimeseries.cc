// Copyright (C) 2009-2011 Constantine Khroulev
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
#include <sstream>
#include <algorithm>
#include "PISMIO.hh"

//! Initializes the code writing scalar time-series.
PetscErrorCode IceModel::init_timeseries() {
  PetscErrorCode ierr;
  bool ts_file_set, ts_times_set, ts_vars_set;
  string times, vars;
  bool append;

  ierr = PetscOptionsBegin(grid.com, "", "Options controlling scalar diagnostic time-series", ""); CHKERRQ(ierr);
  {
    ierr = PISMOptionsString("-ts_file", "Specifies the time-series output file name",
			     ts_filename, ts_file_set); CHKERRQ(ierr);

    ierr = PISMOptionsString("-ts_times", "Specifies a MATLAB-style range or a list of requested times",
			     times, ts_times_set); CHKERRQ(ierr);

    ierr = PISMOptionsString("-ts_vars", "Specifies a comma-separated list of veriables to save",
			     vars, ts_vars_set); CHKERRQ(ierr);

    // default behavior is to move the file aside if it exists already; option allows appending
    ierr = PISMOptionsIsSet("-ts_append", append); CHKERRQ(ierr);
  }
  ierr = PetscOptionsEnd(); CHKERRQ(ierr);


  if (ts_file_set ^ ts_times_set) {
    ierr = PetscPrintf(grid.com,
      "PISM ERROR: you need to specity both -ts_file and -ts_times to save"
      "diagnostic time-series.\n");
    CHKERRQ(ierr);
    PISMEnd();
  }

  // If neither -ts_filename nor -ts_times is set, we're done.
  if (!ts_file_set && !ts_times_set) {
    save_ts = false;
    return 0;
  }
  
  save_ts = true;

  ierr = parse_times(grid.com, times, ts_times);
  if (ierr != 0) {
    ierr = PetscPrintf(grid.com, "PISM ERROR: parsing the -ts_times argument failed.\n"); CHKERRQ(ierr);
    PISMEnd();
  }

  if (ts_times.size() == 0) {
    PetscPrintf(grid.com, "PISM ERROR: no argument for -ts_times option.\n");
    PISMEnd();
  }

  ierr = verbPrintf(2, grid.com, "saving scalar time-series to '%s'; ",
		    ts_filename.c_str()); CHKERRQ(ierr);

  ierr = verbPrintf(2, grid.com, "times requested: %s\n", times.c_str()); CHKERRQ(ierr);

  current_ts = 0;


  string var_name;
  if (ts_vars_set) {
    ierr = verbPrintf(2, grid.com, "variables requested: %s\n", vars.c_str()); CHKERRQ(ierr);
    istringstream arg(vars);

    while (getline(arg, var_name, ','))
      ts_vars.insert(var_name);

  } else {
    var_name = config.get_string("ts_default_variables");
    istringstream arg(var_name);
  
    while (getline(arg, var_name, ' ')) {
      if (!var_name.empty()) // this ignores multiple spaces separating variable names
	ts_vars.insert(var_name);
    }
  }


  PISMIO nc(&grid);
  ierr = nc.open_for_writing(ts_filename.c_str(), (append==PETSC_TRUE), false); CHKERRQ(ierr);
  ierr = nc.close(); CHKERRQ(ierr);

  ierr = create_timeseries(); CHKERRQ(ierr);
  
  return 0;
}

//! \brief Creates DiagnosticTimeseries objects used to store and report scalar
//! diagnostic quantities.
PetscErrorCode IceModel::create_timeseries() {

  string time_units = "years since " + config.get_string("reference_date");
  
  if (find(ts_vars.begin(), ts_vars.end(), "ivol") != ts_vars.end()) {
    DiagnosticTimeseries *ivol = new DiagnosticTimeseries(&grid, "ivol", "t");

    ivol->set_units("m3", "");
    ivol->set_dimension_units(time_units, "");
    ivol->output_filename = ts_filename;

    ivol->set_attr("long_name", "total ice volume");
    ivol->set_attr("valid_min", 0.0);

    timeseries.push_back(ivol);
  }  
  
  if (find(ts_vars.begin(), ts_vars.end(), "slvol") != ts_vars.end()) {
    DiagnosticTimeseries *slvol = new DiagnosticTimeseries(&grid, "slvol", "t");

    slvol->set_units("m", "");
    slvol->set_dimension_units(time_units, "");
    slvol->output_filename = ts_filename;

    slvol->set_attr("long_name", "total sea-level relevant ice IN SEA-LEVEL EQUIVALENT");
    slvol->set_attr("valid_min", 0.0);

    timeseries.push_back(slvol);
  }

  if (find(ts_vars.begin(), ts_vars.end(), "ivoltemp") != ts_vars.end()) {
    DiagnosticTimeseries *ivoltemp = new DiagnosticTimeseries(&grid, "ivoltemp", "t");

    ivoltemp->set_units("m3", "");
    ivoltemp->set_dimension_units(time_units, "");
    ivoltemp->output_filename = ts_filename;

    ivoltemp->set_attr("long_name", "temperate ice volume");
    ivoltemp->set_attr("valid_min", 0.0);

    timeseries.push_back(ivoltemp);
  }

  if (find(ts_vars.begin(), ts_vars.end(), "ivoltempf") != ts_vars.end()) {
    DiagnosticTimeseries *ivoltempf = new DiagnosticTimeseries(&grid, "ivoltempf", "t");

    ivoltempf->set_units("1", "");
    ivoltempf->set_dimension_units(time_units, "");
    ivoltempf->output_filename = ts_filename;

    ivoltempf->set_attr("long_name", "temperate ice volume fraction");
    ivoltempf->set_attr("valid_min", 0.0);

    timeseries.push_back(ivoltempf);
  }

  if (find(ts_vars.begin(), ts_vars.end(), "ivolcold") != ts_vars.end()) {
    DiagnosticTimeseries *ivolcold = new DiagnosticTimeseries(&grid, "ivolcold", "t");

    ivolcold->set_units("m3", "");
    ivolcold->set_dimension_units(time_units, "");
    ivolcold->output_filename = ts_filename;

    ivolcold->set_attr("long_name", "cold ice volume");
    ivolcold->set_attr("valid_min", 0.0);

    timeseries.push_back(ivolcold);
  }

  if (find(ts_vars.begin(), ts_vars.end(), "ivolcoldf") != ts_vars.end()) {
    DiagnosticTimeseries *ivolcoldf = new DiagnosticTimeseries(&grid, "ivolcoldf", "t");

    ivolcoldf->set_units("1", "");
    ivolcoldf->set_dimension_units(time_units, "");
    ivolcoldf->output_filename = ts_filename;

    ivolcoldf->set_attr("long_name", "cold ice volume fraction");
    ivolcoldf->set_attr("valid_min", 0.0);

    timeseries.push_back(ivolcoldf);
  }

  if (find(ts_vars.begin(), ts_vars.end(), "ienthalpy") != ts_vars.end()) {
    DiagnosticTimeseries *ienthalpy = new DiagnosticTimeseries(&grid, "ienthalpy", "t");

    ienthalpy->set_units("J", "");
    ienthalpy->set_dimension_units(time_units, "");
    ienthalpy->output_filename = ts_filename;

    ienthalpy->set_attr("long_name", "total ice enthalpy");
    ienthalpy->set_attr("valid_min", 0.0);

    timeseries.push_back(ienthalpy);
  }

  if (find(ts_vars.begin(), ts_vars.end(), "imass") != ts_vars.end()) {
    DiagnosticTimeseries *imass = new DiagnosticTimeseries(&grid, "imass", "t");

    imass->set_units("kg", "");
    imass->set_dimension_units(time_units, "");
    imass->output_filename = ts_filename;

    imass->set_attr("long_name", "total ice mass");
    imass->set_attr("valid_min", 0.0);

    timeseries.push_back(imass);
  }

  if (find(ts_vars.begin(), ts_vars.end(), "iarea") != ts_vars.end()) {
    DiagnosticTimeseries *iarea = new DiagnosticTimeseries(&grid, "iarea", "t");

    iarea->set_units("m2", "");
    iarea->set_dimension_units(time_units, "");
    iarea->output_filename = ts_filename;

    iarea->set_attr("long_name", "ice area");
    iarea->set_attr("valid_min", 0.0);

    timeseries.push_back(iarea);
  }

  if (find(ts_vars.begin(), ts_vars.end(), "iareatemp") != ts_vars.end()) {
    DiagnosticTimeseries *iareatemp = new DiagnosticTimeseries(&grid, "iareatemp", "t");

    iareatemp->set_units("m2", "");
    iareatemp->set_dimension_units(time_units, "");
    iareatemp->output_filename = ts_filename;

    iareatemp->set_attr("long_name", "ice area temperate");
    iareatemp->set_attr("valid_min", 0.0);

    timeseries.push_back(iareatemp);
  }

  if (find(ts_vars.begin(), ts_vars.end(), "iareatempf") != ts_vars.end()) {
    DiagnosticTimeseries *iareatempf = new DiagnosticTimeseries(&grid, "iareatempf", "t");

    iareatempf->set_units("1", "");
    iareatempf->set_dimension_units(time_units, "");
    iareatempf->output_filename = ts_filename;

    iareatempf->set_attr("long_name", "ice area temperate fraction");
    iareatempf->set_attr("valid_min", 0.0);

    timeseries.push_back(iareatempf);
  }

  if (find(ts_vars.begin(), ts_vars.end(), "iareacold") != ts_vars.end()) {
    DiagnosticTimeseries *iareacold = new DiagnosticTimeseries(&grid, "iareacold", "t");

    iareacold->set_units("m2", "");
    iareacold->set_dimension_units(time_units, "");
    iareacold->output_filename = ts_filename;

    iareacold->set_attr("long_name", "ice area cold");
    iareacold->set_attr("valid_min", 0.0);

    timeseries.push_back(iareacold);
  }

  if (find(ts_vars.begin(), ts_vars.end(), "iareacoldf") != ts_vars.end()) {
    DiagnosticTimeseries *iareacoldf = new DiagnosticTimeseries(&grid, "iareacoldf", "t");

    iareacoldf->set_units("1", "");
    iareacoldf->set_dimension_units(time_units, "");
    iareacoldf->output_filename = ts_filename;

    iareacoldf->set_attr("long_name", "ice area cold fraction");
    iareacoldf->set_attr("valid_min", 0.0);

    timeseries.push_back(iareacoldf);
  }

  if (find(ts_vars.begin(), ts_vars.end(), "iareag") != ts_vars.end()) {
    DiagnosticTimeseries *iareag = new DiagnosticTimeseries(&grid, "iareag", "t");

    iareag->set_units("m2", "");
    iareag->set_dimension_units(time_units, "");
    iareag->output_filename = ts_filename;

    iareag->set_attr("long_name", "grounded ice area");
    iareag->set_attr("valid_min", 0.0);

    timeseries.push_back(iareag);
  }

  if (find(ts_vars.begin(), ts_vars.end(), "iareaf") != ts_vars.end()) {
    DiagnosticTimeseries *iareaf = new DiagnosticTimeseries(&grid, "iareaf", "t");

    iareaf->set_units("m2", "");
    iareaf->set_dimension_units(time_units, "");
    iareaf->output_filename = ts_filename;

    iareaf->set_attr("long_name", "floating ice area");
    iareaf->set_attr("valid_min", 0.0);

    timeseries.push_back(iareaf);
  }

  if (find(ts_vars.begin(), ts_vars.end(), "dt") != ts_vars.end()) {
    DiagnosticTimeseries *delta_t = new DiagnosticTimeseries(&grid, "dt", "t");

    delta_t->set_units("s", "years");
    delta_t->set_dimension_units(time_units, "");
    delta_t->output_filename = ts_filename;

    delta_t->set_attr("long_name", "mass continuity time-step");
    delta_t->set_attr("valid_min", 0.0);

    timeseries.push_back(delta_t);
  }

  // The following are in  config.get("ts_bad_set")  list.

  if (find(ts_vars.begin(), ts_vars.end(), "divoldt") != ts_vars.end()) {
    DiagnosticTimeseries *divoldt = new DiagnosticTimeseries(&grid, "divoldt", "t");

    divoldt->set_units("m3 s-1", "");
    divoldt->set_dimension_units(time_units, "");
    divoldt->output_filename = ts_filename;

    divoldt->set_attr("long_name", "total ice volume rate of change");

    timeseries.push_back(divoldt);
  }

  if (find(ts_vars.begin(), ts_vars.end(), "dimassdt") != ts_vars.end()) {
    DiagnosticTimeseries *dimassdt = new DiagnosticTimeseries(&grid, "dimassdt", "t");

    dimassdt->set_units("kg s-1", "");
    dimassdt->set_dimension_units(time_units, "");
    dimassdt->output_filename = ts_filename;

    dimassdt->set_attr("long_name", "total ice mass rate of change");

    timeseries.push_back(dimassdt);
  }

  if (find(ts_vars.begin(), ts_vars.end(), "surface_ice_flux") != ts_vars.end()) {
    DiagnosticTimeseries *tsif = new DiagnosticTimeseries(&grid, "surface_ice_flux", "t");

    tsif->set_units("kg s-1", "");
    tsif->set_dimension_units(time_units, "");
    tsif->output_filename = ts_filename;

    tsif->set_attr("long_name", "total over ice domain of top surface ice mass flux");
    tsif->set_attr("comment", "positive means ice gain");

    timeseries.push_back(tsif);
  }

  if (find(ts_vars.begin(), ts_vars.end(), "basal_ice_flux") != ts_vars.end()) {
    DiagnosticTimeseries *tbif = new DiagnosticTimeseries(&grid, "basal_ice_flux", "t");

    tbif->set_units("kg s-1", "");
    tbif->set_dimension_units(time_units, "");
    tbif->output_filename = ts_filename;

    tbif->set_attr("long_name", "total over ice domain of basal surface ice mass flux");
    tbif->set_attr("comment", "positive means ice gain");

    timeseries.push_back(tbif);
  }

  if (find(ts_vars.begin(), ts_vars.end(), "sub_shelf_ice_flux") != ts_vars.end()) {
    DiagnosticTimeseries *tssif = new DiagnosticTimeseries(&grid, "sub_shelf_ice_flux", "t");

    tssif->set_units("kg s-1", "");
    tssif->set_dimension_units(time_units, "");
    tssif->output_filename = ts_filename;

    tssif->set_attr("long_name", "total over ice domain of sub-ice-shelf ice mass flux");
    tssif->set_attr("comment", "positive means ice gain");

    timeseries.push_back(tssif);
  }

  if (find(ts_vars.begin(), ts_vars.end(), "nonneg_rule_flux") != ts_vars.end()) {
    DiagnosticTimeseries *ts = new DiagnosticTimeseries(&grid, "nonneg_rule_flux", "t");

    ts->set_units("kg s-1", "");
    ts->set_dimension_units(time_units, "");
    ts->output_filename = ts_filename;

    ts->set_attr("long_name", "total over ice domain of ice mass gain by application of non-negative thickness rule");
    ts->set_attr("comment", "positive means ice gain");

    timeseries.push_back(ts);
  }

  if (find(ts_vars.begin(), ts_vars.end(), "ocean_kill_flux") != ts_vars.end()) {
    DiagnosticTimeseries *ts = new DiagnosticTimeseries(&grid, "ocean_kill_flux", "t");

    ts->set_units("kg s-1", "");
    ts->set_dimension_units(time_units, "");
    ts->output_filename = ts_filename;

    ts->set_attr("long_name", "total over ice domain of ice mass gain by calving by application of -ocean_kill mechanism");
    ts->set_attr("comment", "positive means ice gain");

    timeseries.push_back(ts);
  }

  if (find(ts_vars.begin(), ts_vars.end(), "float_kill_flux") != ts_vars.end()) {
    DiagnosticTimeseries *ts = new DiagnosticTimeseries(&grid, "float_kill_flux", "t");

    ts->set_units("kg s-1", "");
    ts->set_dimension_units(time_units, "");
    ts->output_filename = ts_filename;

    ts->set_attr("long_name", "total over ice domain of ice mass gain by calving by application of -float_kill mechanism");
    ts->set_attr("comment", "positive means ice gain");

    timeseries.push_back(ts);
  }

  // as noted above, the variables listed in the bad_set may require a user to 
  // very carefully interpret; thus the need to add a warning
  string warning = config.get_string("ts_bad_set_warning"),
         tmp;
  istringstream arg(config.get_string("ts_bad_set_variables")); // string becomes stream
  set<string> bad_vars;
  while (getline(arg, tmp, ' ')) {
    if (!tmp.empty()) // this ignores multiple spaces separating variable names
      bad_vars.insert(tmp);
  }
  for (size_t i = 0; i < timeseries.size(); i++) {
    if (set_contains(bad_vars,timeseries[i]->short_name)) {
      timeseries[i]->set_attr("interpretation_warning", warning);
    }
  }

  return 0;
}

//! Write time-series.
PetscErrorCode IceModel::write_timeseries() {
  PetscErrorCode ierr;
  vector<DiagnosticTimeseries*>::iterator i;

  // return if no time-series requested
  if (!save_ts) return 0;

  // return if wrote all the records already
  if (current_ts == ts_times.size())
    return 0;

  // return if did not yet reach the time we need to save at
  if (ts_times[current_ts] > grid.year)
    return 0;

  // compute values of requested scalar quantities:
  for (i = timeseries.begin(); i < timeseries.end(); ++i) {
    PetscScalar tmp;

    ierr = compute_by_name((*i)->short_name, tmp); CHKERRQ(ierr);
    
    (*i)->append(grid.year, tmp);
  }

  // Interpolate to put them on requested times:
  while ((current_ts < ts_times.size()) &&
         (ts_times[current_ts] <= grid.year)) {
    
    for (i = timeseries.begin(); i < timeseries.end(); ++i) {
      ierr = (*i)->interp(ts_times[current_ts]); CHKERRQ(ierr);
    }

    current_ts++;
  }

  return 0;
}


//! Initialize the code saving spatially-variable diagnostic quantities.
PetscErrorCode IceModel::init_extras() {
  PetscErrorCode ierr;
  bool split, times_set, file_set, save_vars;
  string times, vars;
  current_extra = 0;

  ierr = PetscOptionsBegin(grid.com, "", "Options controlling 2D and 3D diagnostic output", ""); CHKERRQ(ierr);
  {
    ierr = PISMOptionsString("-extra_file", "Specifies the output file",
			     extra_filename, file_set); CHKERRQ(ierr);

    ierr = PISMOptionsString("-extra_times", "Specifies times to save at",
			     times, times_set); CHKERRQ(ierr);
			     
    ierr = PISMOptionsString("-extra_vars", "Spacifies a comma-separated list of variables to save",
			     vars, save_vars); CHKERRQ(ierr);

    ierr = PISMOptionsIsSet("-extra_split", "Specifies whether to save to separate files",
			    split); CHKERRQ(ierr);
  }
  ierr = PetscOptionsEnd(); CHKERRQ(ierr);

  if (file_set ^ times_set) {
    PetscPrintf(grid.com,
      "PISM ERROR: you need to specify both -extra_file and -extra_times to save spatial time-series.\n");
    PISMEnd();
  }

  if (!file_set && !times_set) {
    save_extra = false;
    return 0;
  }

  ierr = parse_times(grid.com, times, extra_times);
  if (ierr != 0) {
    PetscPrintf(grid.com, "PISM ERROR: parsing the -extra_times argument failed.\n");
    PISMEnd();
  }
  if (extra_times.size() == 0) {
    PetscPrintf(grid.com, "PISM ERROR: no argument for -extra_times option.\n");
    PISMEnd();
  }

  save_extra = true;
  extra_file_is_ready = false;
  split_extra = false;

  if (split) {
    split_extra = true;
  } else if (!ends_with(extra_filename, ".nc")) {
    ierr = verbPrintf(2, grid.com,
		      "PISM WARNING: spatial time-series file name '%s' does not have the '.nc' suffix!\n",
		      extra_filename.c_str());
    CHKERRQ(ierr);
  }
  
  if (split) {
    ierr = verbPrintf(2, grid.com, "saving spatial time-series to '%s+year.nc'; ",
		      extra_filename.c_str()); CHKERRQ(ierr);
  } else {
    ierr = verbPrintf(2, grid.com, "saving spatial time-series to '%s'; ",
		      extra_filename.c_str()); CHKERRQ(ierr);
  }

  if (extra_times.size() > 500) {
    ierr = verbPrintf(2, grid.com,
		      "PISM WARNING: more than 500 times requested. This might fill your hard-drive!\n");
    CHKERRQ(ierr);
  }

  ierr = verbPrintf(2, grid.com, "times requested: %s\n", times.c_str()); CHKERRQ(ierr);

  string var_name;
  if (save_vars) {
    ierr = verbPrintf(2, grid.com, "variables requested: %s\n", vars.c_str()); CHKERRQ(ierr);
    istringstream arg(vars);

    while (getline(arg, var_name, ','))
      extra_vars.insert(var_name);

  } else {
    ierr = verbPrintf(2, grid.com, "PISM WARNING: -extra_vars was not set."
                      " Writing model_state, mapping and climate_steady variables...\n"); CHKERRQ(ierr);

    set<string> vars_set = variables.keys();

    set<string>::iterator i = vars_set.begin();
    while (i != vars_set.end()) {
      IceModelVec *var = variables.get(*i);
      
      string intent = var->string_attr("pism_intent");
      if ((intent == "model_state") ||
          (intent == "mapping") ||
          (intent == "climate_steady")) {
	extra_vars.insert(*i);
      }
      i++;
    }

    if (stress_balance)
      stress_balance->add_vars_to_output("small", extra_vars);

  }
  if (extra_vars.size() == 0) {
    ierr = verbPrintf(2, grid.com, 
       "PISM WARNING: no variables list after -extra_vars ... writing empty file ...\n"); CHKERRQ(ierr);
  }

  return 0;
}

//! Write spatially-variable diagnostic quantities.
PetscErrorCode IceModel::write_extras() {
  PetscErrorCode ierr;
  PISMIO nc(&grid);
  double saving_after = -1.0e30; // initialize to avoid compiler warning; this
				 // value is never used, because saving_after
				 // is only used if save_now == true, and in
				 // this case saving_after is guaranteed to be
				 // initialized. See the code below.
  char filename[PETSC_MAX_PATH_LEN];

  // determine if the user set the -save_at and -save_to options
  if (!save_extra)
    return 0;

  // do we need to save *now*?
  if ( current_extra < extra_times.size() && grid.year >= extra_times[current_extra] ) {
    saving_after = extra_times[current_extra];

    while (current_extra < extra_times.size() && extra_times[current_extra] <= grid.year)
      current_extra++;
  } else {
    // we don't need to save now, so just return
    return 0;
  }

  if (saving_after < grid.start_year) {
    // Suppose a user tells PISM to write data at times 0:1000:10000. Suppose
    // also that PISM writes a backup file at year 2500 and gets stopped.
    // 
    // When restarted, PISM will decide that it's time to write data for time
    // 2000, but
    // * that record was written already and
    // * PISM will end up writing at year 2500, producing a file containing one
    //   more record than necessary.
    // 
    // This check makes sure that this never happens.
    return 0;
  }

  if (split_extra) {
    extra_file_is_ready = false;	// each time-series record is written to a separate file
    snprintf(filename, PETSC_MAX_PATH_LEN, "%s-%06.0f.nc",
	     extra_filename.c_str(), grid.year);
  } else {
    strncpy(filename, extra_filename.c_str(), PETSC_MAX_PATH_LEN);
  }

  ierr = verbPrintf(3, grid.com, 
		    "\nsaving spatial time-series to %s at %.5f a\n\n",
		    filename, grid.year);
  CHKERRQ(ierr);

  // create line for history in .nc file, including time of write
  string date_str = pism_timestamp();
  char tmp[TEMPORARY_STRING_LENGTH];
  snprintf(tmp, TEMPORARY_STRING_LENGTH,
	   "%s: %s saving spatial time-series record at %10.5f a\n",
	   date_str.c_str(), executable_short_name.c_str(), grid.year);

  if (!extra_file_is_ready) {

    // default behavior is to move the file aside if it exists already; option allows appending
    bool append;
    ierr = PISMOptionsIsSet("-extra_append", append); CHKERRQ(ierr);

    // Prepare the file:
    ierr = nc.open_for_writing(filename, (append==PETSC_TRUE), true); CHKERRQ(ierr); // check_dims == true
    ierr = nc.close(); CHKERRQ(ierr);

    ierr = write_metadata(filename); CHKERRQ(ierr); 

    extra_file_is_ready = true;
  }
    
  ierr = nc.open_for_writing(filename, true, true); CHKERRQ(ierr);
  // append == true, check_dims == true
  ierr = nc.append_time(grid.year); CHKERRQ(ierr);
  ierr = nc.write_history(tmp); CHKERRQ(ierr); // append the history
  ierr = nc.close(); CHKERRQ(ierr);

  ierr = write_variables(filename, extra_vars, NC_FLOAT);  CHKERRQ(ierr);
    
  return 0;
}

//! Computes the maximum time-step we can take and still hit all the requested years.
/*!
  Sets dt_years to -1 if any time-step is OK.
 */
PetscErrorCode IceModel::extras_max_timestep(double t_years, double& dt_years) {

  if (!save_extra) {
    dt_years = -1;
    return 0;
  }

  bool force_times;
  force_times = config.get_flag("force_output_times");

  if (!force_times) {
    dt_years = -1;
    return 0;
  }

  vector<double>::iterator j;
  j = upper_bound(extra_times.begin(), extra_times.end(), t_years);

  if (j == extra_times.end()) {
    dt_years = -1;
    return 0;
  }

  dt_years = *j - t_years;
  return 0;
}

//! Computes the maximum time-step we can take and still hit all the requested years.
/*!
  Sets dt_years to -1 if any time-step is OK.
 */
PetscErrorCode IceModel::ts_max_timestep(double t_years, double& dt_years) {

  if (!save_ts) {
    dt_years = -1;
    return 0;
  }

  bool force_times;
  force_times = config.get_flag("force_output_times");

  if (!force_times) {
    dt_years = -1;
    return 0;
  }

  vector<double>::iterator j;
  j = upper_bound(ts_times.begin(), ts_times.end(), t_years);

  if (j == ts_times.end()) {
    dt_years = -1;
    return 0;
  }

  dt_years = *j - t_years;
  return 0;
}

//! Flush scalar time-series.
PetscErrorCode IceModel::flush_timeseries() {
  // flush all the time-series buffers:
  vector<DiagnosticTimeseries*>::iterator i;
  for (i = timeseries.begin(); i < timeseries.end(); ++i) {
    (*i)->flush();
  }

  return 0;
}
