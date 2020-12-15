#!/bin/bash

if [[ $# < 7 ]]
then
    echo "usage: test_temporal_reduction_app.sh [app prefix] " \
         "[data root] [input regex] [array name] [interval] " \
         "[operator] [steps per file] [mpi exec] [test cores]"
    exit -1
fi

app_prefix=${1}
data_root=${2}
input_regex=${3}
array_name=${4}
interval=${5}
operator=${6}
steps_per_file=${7}

if [[ $# -eq 9 ]]
then
    mpi_exec=${8}
    test_cores=${9}
    launcher="${mpi_exec} -n ${test_cores}"
fi

set -x

test_name=test_temporal_reduction
output_base=${test_name}_${array_name}_${interval}_${operator}

# run the app
time ${launcher} ${app_prefix}/teca_temporal_reduction                  \
    --input_regex "${data_root}/${input_regex}" --interval ${interval}  \
    --operator ${operator} --point_arrays ${array_name}                 \
    --steps_per_file ${steps_per_file} --n_threads 2 --verbose 1        \
    --output_file "${output_base}_%t%.nc"

# don't profile the diff
unset PROFILER_ENABLE

do_test=1
if [[ $do_test -eq 0 ]]
then
    # update the baselines
    cp -vd ${output_base}_*.nc ${data_root}/
else
    # run the diff
    time ${app_prefix}/teca_cartesian_mesh_diff                     \
        --reference_dataset "${data_root}/${output_base}_.*\.nc"    \
        --test_dataset "${output_base}_.*\.nc"                      \
        --arrays ${array_name} --verbose                            \
        --test_tolerance 1.e-5

    # clean up
    rm ${output_base}_*.nc
fi
