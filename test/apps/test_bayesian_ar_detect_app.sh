#!/bin/bash

if [[ $# < 3 ]]
then
    echo "usage: test_bayesian_ar_detect_app.sh [app prefix] "   \
         "[data root] [num threads] [mpiexec] [num ranks]"
    exit -1
fi

app_prefix=${1}
data_root=${2}
n_threads=${3}

if [[ $# -eq 5 ]]
then
    mpi_exec=${4}
    test_cores=${5}
    launcher="${mpi_exec} -n ${test_cores}"
fi

set -x

# run the app
${launcher} ${app_prefix}/teca_bayesian_ar_detect                \
    --input_regex "${data_root}/ARTMIP_MERRA_2D_2017-05.*\.nc$"  \
    --n_threads ${n_threads} --verbose                           \
    --output_file test_bayesian_ar_detect_app_output.nc

# run the diff
${app_prefix}/teca_cartesian_mesh_diff                           \
    "${data_root}/test_bayesian_ar_detect_app_ref.bin"           \
    "test_bayesian_ar_detect_app_output.*\.nc"

# clean up
rm test_bayesian_ar_detect_app_output*.nc
