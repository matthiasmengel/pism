#!/bin/bash

# Copyright (C) 2010-2012 Andy Aschwanden


if [ -n "${SCRIPTNAME:+1}" ] ; then
  echo "[SCRIPTNAME=$SCRIPTNAME (already set)]"
  echo ""
else
  SCRIPTNAME="#(canadian.sh)"
fi

echo
echo "# =================================================================================="
echo "# PISM Storglaciaren Flow Line Model -- Canadian Type"
echo "# =================================================================================="
echo

set -e # exit on error

NN=2  # default number of processors
if [ $# -gt 0 ] ; then  # if user says "psg_flowline.sh 8" then NN = 8
  NN="$1"
fi

echo "$SCRIPTNAME              NN = $NN"

# set MPIDO if using different MPI execution command, for example:
#  $ export PISM_MPIDO="aprun -n "
if [ -n "${PISM_MPIDO:+1}" ] ; then  # check if env var is already set
  echo "$SCRIPTNAME      PISM_MPIDO = $PISM_MPIDO  (already set)"
else
  PISM_MPIDO="mpiexec -n "
  echo "$SCRIPTNAME      PISM_MPIDO = $PISM_MPIDO"
fi

# check if env var PISM_DO was set (i.e. PISM_DO=echo for a 'dry' run)
if [ -n "${PISM_DO:+1}" ] ; then  # check if env var DO is already set
  echo "$SCRIPTNAME         PISM_DO = $PISM_DO  (already set)"
else
  PISM_DO="" 
fi

# prefix to pism (not to executables)
if [ -n "${PISM_PREFIX:+1}" ] ; then  # check if env var is already set
  echo "$SCRIPTNAME     PISM_PREFIX = $PISM_PREFIX  (already set)"
else
  PISM_PREFIX=""    # just a guess
  echo "$SCRIPTNAME     PISM_PREFIX = $PISM_PREFIX"
fi

# set PISM_EXEC if using different executables, for example:
#  $ export PISM_EXEC="pismr -cold"
if [ -n "${PISM_EXEC:+1}" ] ; then  # check if env var is already set
  echo "$SCRIPTNAME       PISM_EXEC = $PISM_EXEC  (already set)"
else
  PISM_EXEC="pismr"
  echo "$SCRIPTNAME       PISM_EXEC = $PISM_EXEC"
fi

echo

PCONFIG=psg_config.nc

# cat prefix and exec together
PISM="${PISM_PREFIX}${PISM_EXEC} -cts -config_override $PCONFIG"


DATANAME=storglaciaren_flowline.nc
PISM_DATANAME=pism_$DATANAME
INNAME=$PISM_DATANAME

PISM_TEMPSERIES=delta_T.nc

# coupler settings
COUPLER="-surface elevation -ice_surface_temp -4.5,-7,1200,1600 -climatic_mass_balance -2,1.,1200,1450,1615 -climatic_mass_balance_limits -2,0"
COUPLER_FORCING="-surface elevation,delta_T -surface_delta_T_file $PISM_TEMPSERIES -ice_surface_temp -4.5,-7,1200,1600 -climatic_mass_balance -2,1.,1200,1450,1615 -climatic_mass_balance_limits -2,0"
# grid parameters
FINEGRID="-periodicity y -Mx 792 -My 3 -Mz 201 -Lz 300 -z_spacing equal"  # 5 m grid
FS=5
FINESKIP=5000
COARSEGRID="-periodicity y -Mx 114 -My 3 -Mz 101 -Lz 500 -z_spacing equal"  # 35 m grid
CS=35
COARSESKIP=1000

GRID=$COARSEGRID
SKIP=$COARSESKIP
GS=$CS
echo ""
if [ $# -gt 1 ] ; then
  if [ $2 -eq "2" ] ; then  # if user says "psg_flowline.sh N 1" then use 5m grid:
    echo "$SCRIPTNAME grid: ALL RUNS ON $FS m"
    echo "$SCRIPTNAME       WARNING: VERY LARGE COMPUTATIONAL TIME"
    GRID=$FINEGRID
    SKIP=$FINESKIP
    GS=$FS
  fi
else
    echo "$SCRIPTNAME grid: ALL RUNS ON $CS m"
fi
echo ""



EB="-sia_e 1"
phi=40
#PARAMS="$TILLPHI -pseudo_plastic_uthreshold $uth"
PARAMS="-plastic_phi $phi"

PETSCSTUFF="-pc_type lu -pc_factor_mat_solver_package mumps"
#PETSCSTUFF="-pc_type asm -sub_pc_type lu -ksp_type lgmres -ksp_right_pc"


FULLPHYS="-ssa_sliding -cfbc -thk_eff $PARAMS $PETSCSTUFF"

SMOOTHRUNLENGTH=1
NOMASSRUNLENGTH=1000

STEP=1

EXVARS="enthalpybase,topg,velsurf,velbase,cts,liqfrac,temp_pa,climatic_mass_balance,temppabase,tempicethk,bmelt,bwat,usurf,csurf,mask,hardav,thk" # add mask, so that check_stationarity.py ignores ice-free areas.

PREFIX=canadian_

# bootstrap and do smoothing run to 1 year
OUTNAME=$PREFIX${GS}m_pre$SMOOTHRUNLENGTH.nc
echo
echo "$SCRIPTNAME  bootstrapping plus short smoothing run for ${SMOOTHRUNLENGTH}a"
cmd="$PISM_MPIDO $NN $PISM $EB -skip -skip_max  $SKIP -boot_file $INNAME $GRID \
  $COUPLER -y ${SMOOTHRUNLENGTH} -o $OUTNAME"
$PISM_DO $cmd

# run with -no_mass (no surface change) 
INNAME=$OUTNAME
OUTNAME=$PREFIX${GS}m_steady.nc
EXNAME=ex_${OUTNAME}
EXTIMES=0:25:${NOMASSRUNLENGTH}
echo
echo "$SCRIPTNAME  -no_mass (no surface change) sia run to achieve approximate enthalpy equilibrium, for ${NOMASSRUNLENGTH}a"
cmd="$PISM_MPIDO $NN $PISM $EB -skip -skip_max  $SKIP -i $INNAME $COUPLER \
  -no_mass -y ${NOMASSRUNLENGTH} \
  -extra_file $EXNAME -extra_vars $EXVARS -extra_times $EXTIMES -o $OUTNAME"
$PISM_DO $cmd




STARTYEAR=0
RUNLENGTH=500
ENDTIME=$(($STARTYEAR + $RUNLENGTH))
INNAME=$PREFIX${GS}m_steady.nc
OUTNAME=ssa_${RUNLENGTH}a.nc
OUTNAMEFULL=$PREFIX${GS}m_$OUTNAME
TSNAME=ts_${OUTNAMEFULL}
EXNAME=ex_${OUTNAMEFULL}
TSTIMES=$STARTYEAR:$STEP:$ENDTIME
EXTIMES=$STARTYEAR:$STEP:$ENDTIME

echo
echo "$SCRIPTNAME  SSA run with elevation-dependent mass balance for $RUNLENGTH years on ${GS}m grid"
cmd="$PISM_MPIDO $NN $PISM $EB -skip -skip_max  $SKIP -i $INNAME $COUPLER $FULLPHYS \
     -ts_file $TSNAME -ts_times $TSTIMES -plastic_phi 40 \
     -extra_file $EXNAME -extra_vars $EXVARS -extra_times $EXTIMES \
     -ys $STARTYEAR -y $RUNLENGTH -o_size big -o $OUTNAMEFULL"
$PISM_DO $cmd
echo
$PISM_DO flowline.py -c -o $OUTNAME $OUTNAMEFULL

EX1NAME=$OUTNAMEFULL



COUPLER="-surface elevation -ice_surface_temp -1,-3.5,1200,1600 -climatic_mass_balance -0.5,0.5.,1200,1450,1615 -climatic_mass_balance_limits -0.5,0"

STARTYEAR=0
RUNLENGTH=500
ENDTIME=$(($STARTYEAR + $RUNLENGTH))
INNAME=$EX1NAME
OUTNAME=tplus2_${RUNLENGTH}a.nc
OUTNAMEFULL=$PREFIX${GS}m_$OUTNAME
TSNAME=ts_${OUTNAMEFULL}
EXNAME=ex_${OUTNAMEFULL}
TSTIMES=$STARTYEAR:$STEP:$ENDTIME
EXTIMES=$STARTYEAR:$STEP:$ENDTIME

echo
echo "$SCRIPTNAME  SSA run with elevation-dependent mass balance for $RUNLENGTH years on ${GS}m grid"
cmd="$PISM_MPIDO $NN $PISM $EB -skip -skip_max  $SKIP -i $INNAME $COUPLER $FULLPHYS \
     -ts_file $TSNAME -ts_times $TSTIMES -plastic_phi 40 \
     -extra_file $EXNAME -extra_vars $EXVARS -extra_times $EXTIMES \
     -ys $STARTYEAR -y $RUNLENGTH -o_size big -o $OUTNAMEFULL"
$PISM_DO $cmd
echo
$PISM_DO flowline.py -c -o $OUTNAME $OUTNAMEFULL


STARTYEAR=0
RUNLENGTH=500
ENDTIME=$(($STARTYEAR + $RUNLENGTH))
INNAME=$EX1NAME
OUTNAME=t2c_${RUNLENGTH}a.nc
OUTNAMEFULL=$PREFIX${GS}m_$OUTNAME
TSNAME=ts_${OUTNAMEFULL}
EXNAME=ex_${OUTNAMEFULL}
TSTIMES=$STARTYEAR:$STEP:$ENDTIME
EXTIMES=$STARTYEAR:$STEP:$ENDTIME

echo
echo "$SCRIPTNAME  SSA run with elevation-dependent mass balance for $RUNLENGTH years on ${GS}m grid"
cmd="$PISM_MPIDO $NN $PISM $EB -skip -skip_max  $SKIP -i $INNAME $COUPLER_FORCING $FULLPHYS \
     -ts_file $TSNAME -ts_times $TSTIMES -plastic_phi 40 \
     -extra_file $EXNAME -extra_vars $EXVARS -extra_times $EXTIMES \
     -ys $STARTYEAR -y $RUNLENGTH -o_size big -o $OUTNAMEFULL"
$PISM_DO $cmd
echo
$PISM_DO flowline.py -c -o $OUTNAME $OUTNAMEFULL
