#include "array_source_internals.h"

namespace array_source_internals
{
// **************************************************************************
int cpu_dispatch(p_array &a_out, double val, size_t n_vals)
{
#ifndef TECA_NDEBUG
    std::cerr << teca_parallel_id()
        << "array_source_internals::cpu_dispatch" << std::endl;
#endif
    a_out = array::new_cpu_accessible();
    a_out->resize(n_vals);

    std::shared_ptr<double> pa_out = a_out->get_cpu_accessible();

    array_source_internals::cpu::initialize(pa_out.get(), val, n_vals);

    return 0;
}

#if !defined(TECA_HAS_CUDA)
// **************************************************************************
int cuda_dispatch(int device_id, p_array &a_out, double val, size_t n_vals)
{
    (void) device_id;
    (void) a_out;
    (void) val;
    (void) n_vals;

    TECA_ERROR("array_source failed because CUDA is not available")

    return -1;
}
#endif
}
