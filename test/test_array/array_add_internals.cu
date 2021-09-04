#include "array_add_internals.h"

#include "teca_cuda_util.h"
#include "array_util.h"

namespace array_add_internals
{
namespace gpu
{
// **************************************************************************
template<typename data_t>
__global__
void add(data_t *result, const data_t *array_1,
    const data_t *array_2, size_t n_vals)
{
    unsigned long i = teca_cuda_util::thread_id_to_array_index();

    if (i >= n_vals)
        return;

    result[i] = array_1[i] + array_2[i];
}
}

// **************************************************************************
int cuda_dispatch(int device_id, p_array &result, const const_p_array &array_1,
    const const_p_array &array_2, size_t n_vals)
{
#ifndef TECA_NDEBUG
    std::cerr << teca_parallel_id()
        << "array_add_internals::cuda_dispatch device_id="
        << device_id << std::endl;
#endif
    // set the device
    cudaError_t ierr = cudaSuccess;
    if ((ierr = cudaSetDevice(device_id)) != cudaSuccess)
    {
        TECA_ERROR("Failed to set the CUDA device to " << device_id)
        return -1;
    }

    // make sure that inputs are on the GPU
    const_p_array tmp_1 = array_util::cuda_accessible(array_1);
    const_p_array tmp_2 = array_util::cuda_accessible(array_2);

    // allocate the memory
    result = array::new_cuda_accessible();
    result->resize(n_vals);

    // determine kernel launch parameters
    dim3 block_grid;
    int n_blocks = 0;
    dim3 thread_grid;
    if (teca_cuda_util::partition_thread_blocks(0, n_vals,
        8, block_grid, n_blocks, thread_grid))
    {
        TECA_ERROR("Failed to partition thread blocks")
        return -1;
    }

    // add the arrays
    array_add_internals::gpu::add<<<block_grid, thread_grid>>>(
        result->get(), tmp_1->get(), tmp_2->get(), n_vals);

    return 0;
}

}

