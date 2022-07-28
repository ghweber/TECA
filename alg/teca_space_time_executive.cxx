#include "teca_space_time_executive.h"
#include "teca_config.h"
#include "teca_common.h"
#include "teca_coordinate_util.h"
#if defined(TECA_HAS_CUDA)
#include "teca_cuda_util.h"
#endif

#include <string>
#include <sstream>
#include <iostream>
#include <utility>
#include <array>
#include <deque>

using teca_coordinate_util::temporal_extent_t;
using teca_coordinate_util::spatial_extent_t;

// --------------------------------------------------------------------------
teca_space_time_executive::teca_space_time_executive() : first_step(0),
    last_step(-1), number_of_spatial_partitions(0),
    number_of_temporal_partitions(1), temporal_partition_size(0),
    index_executive_compatability(0)
{
}

// --------------------------------------------------------------------------
void teca_space_time_executive::set_number_of_spatial_partitions(long n_partitions)
{
    this->number_of_spatial_partitions = n_partitions;
}

// --------------------------------------------------------------------------
void teca_space_time_executive::set_number_of_temporal_partitions(long n_partitions)
{
    this->number_of_temporal_partitions = n_partitions;
}

// --------------------------------------------------------------------------
void teca_space_time_executive::set_temporal_partition_size(long n_steps)
{
    if (n_steps >= 1)
        this->number_of_temporal_partitions = 0;

    this->temporal_partition_size = n_steps;
}

// --------------------------------------------------------------------------
void teca_space_time_executive::set_time_step(long s)
{
    this->first_step = std::max(0l, s);
    this->last_step = s;
}

// --------------------------------------------------------------------------
void teca_space_time_executive::set_first_step(long s)
{
    this->first_step = std::max(0l, s);
}

// --------------------------------------------------------------------------
void teca_space_time_executive::set_last_step(long s)
{
    this->last_step = s;
}

// --------------------------------------------------------------------------
void teca_space_time_executive::set_extent(unsigned long *ext)
{
    this->set_extent({ext[0], ext[1], ext[2], ext[3], ext[4], ext[4]});
}

// --------------------------------------------------------------------------
void teca_space_time_executive::set_extent(const std::vector<unsigned long> &ext)
{
    this->extent = ext;
}

// --------------------------------------------------------------------------
void teca_space_time_executive::set_bounds(double *bds)
{
    this->set_bounds({bds[0], bds[1], bds[2], bds[3], bds[4], bds[5]});
}

// --------------------------------------------------------------------------
void teca_space_time_executive::set_bounds(const std::vector<double> &bds)
{
    this->bounds = bds;
}

// --------------------------------------------------------------------------
void teca_space_time_executive::set_arrays(const std::vector<std::string> &v)
{
    this->arrays = v;
}

// --------------------------------------------------------------------------
int teca_space_time_executive::initialize(MPI_Comm comm, const teca_metadata &md)
{
    this->requests.clear();

    size_t rank = 0;
    size_t n_ranks = 1;
#if defined(TECA_HAS_MPI)
    int is_init = 0;
    MPI_Initialized(&is_init);
    if (is_init)
    {
        int tmp = 0;
        MPI_Comm_size(comm, &tmp);
        n_ranks = tmp;
        MPI_Comm_rank(comm, &tmp);
        rank = tmp;
    }
#else
    (void)comm;
#endif

    // get the index extent of the spatial component of the data.
    spatial_extent_t whole_extent;
    if (md.get("whole_extent", whole_extent))
    {
        TECA_ERROR("Failed to get whole_extent")
        return -1;
    }

    // determine the extent to process taking into account user provided
    // subsets
    spatial_extent_t working_extent;
    if (!this->bounds.empty())
    {
        // subset by the optional user provided bounding box
        if (teca_coordinate_util::bounds_to_extent(this->bounds.data(),
            md, working_extent.data()))
        {
            TECA_ERROR("Failed to convert the specified bounds "
                << this->bounds << " to a working extent")
            return -1;
        }

    }
    else if (!this->extent.empty())
    {
        // use the user provided extent
        if (!((this->extent[0] >= whole_extent[0]) &&
            (this->extent[1] <= whole_extent[1]) &&
            (this->extent[2] >= whole_extent[2]) &&
            (this->extent[3] <= whole_extent[3]) &&
            (this->extent[4] >= whole_extent[4]) &&
            (this->extent[5] <= whole_extent[5])))
        {
            TECA_ERROR("The specified extent " << this->extent
                << " is not convered by the available extent "
                << whole_extent)
            return -1;
        }
        working_extent = teca_coordinate_util::as_spatial_extent(this->extent);
    }
    else
    {
        // partition the available spatial index extent
        working_extent = whole_extent;
    }

    // partition the spatial domain. if not set then partition such that each
    // rank has a unique subset of the domain
    size_t n_spatial = (this->number_of_spatial_partitions >= 1 ?
        this->number_of_spatial_partitions : n_ranks);

    std::deque<spatial_extent_t> spatial_partition;
    if (teca_coordinate_util::partition(working_extent, n_spatial, spatial_partition))
    {
        TECA_ERROR("Failed to partition the spatial domain")
        return -1;
    }

    // get the pipeline control keys
    if (md.get("index_initializer_key", this->index_initializer_key))
    {
        TECA_ERROR("No index initializer key has been specified")
        return -1;
    }

    if (md.get("index_request_key", this->index_request_key))
    {
        TECA_ERROR("No index request key has been specified")
        return -1;
    }

    // get the number of time steps
    long n_indices = 0;
    if (md.get(this->index_initializer_key, n_indices))
    {
        TECA_ERROR("metadata is missing the initializer key \""
            << this->index_initializer_key << "\"")
        return -1;
    }

    // apply restriction
    unsigned long last
        = this->last_step >= 0 ? this->last_step : n_indices - 1;

    unsigned long first
        = ((this->first_step >= 0) && (size_t(this->first_step) <= last))
            ? this->first_step : 0;

    std::vector<temporal_extent_t> temporal_partition;
    if (teca_coordinate_util::partition({first, last},
        this->number_of_temporal_partitions, this->temporal_partition_size,
        temporal_partition))
    {
        TECA_ERROR("Failed to partition the temporal domain")
        return -1;
    }

    // now we have space and time partitons. the total work to do is the
    // Cartesian product of the two sets. bellow assign elements of the super
    // set to individual MPI ranks
    n_spatial = spatial_partition.size();
    size_t n_time = temporal_partition.size();
    size_t n_partitions = n_spatial * n_time;

    // partition work across MPI ranks. each rank will end up with a unique
    // subset of the work to process.
    size_t n_big = n_partitions % n_ranks;

    // compute this rank's work assignment
    size_t block_size = n_partitions / n_ranks + (rank < n_big ? 1 : 0);
    size_t block_start = block_size * rank + (rank < n_big ? 0 : n_big);

    // if the temporal block size is 1 and we have an index_request_key
    // request a single index rather than an extent so that this executive
    // can be used with algorithms written for the teca_index_executive
    // to parallelize over space and time.
    if (this->index_executive_compatability && (n_time != (last - first + 1)))
    {
        TECA_ERROR("index executive compatability failed because the"
            " temporal block size is greater than 1")
        return -1;
    }

    // determine the available CUDA GPUs
#if defined(TECA_HAS_CUDA)
    int ranks_per_device = -1;
    std::vector<int> device_ids;
    if (teca_cuda_util::get_local_cuda_devices(comm,
        ranks_per_device, device_ids))
    {
        TECA_WARNING("Failed to determine the local CUDA device_ids."
            " Falling back to the default device.")
        device_ids.resize(1, 0);
    }
    int n_devices = device_ids.size();
    size_t qq = 0;
#endif

    // consrtuct base request
    teca_metadata base_req;
    base_req.set("arrays", this->arrays);

    // apply the base request to local work.
    for (size_t q = 0; q < block_size; ++q)
    {
        size_t index = q + block_start;

        size_t j = index / n_spatial;
        size_t i = index % n_spatial;

        int device_id = -1;
#if defined(TECA_HAS_CUDA)
        // assign eaach request a device to execute on
        if (n_devices > 0)
            device_id = device_ids[qq % n_devices];

        ++qq;
#endif
        teca_metadata req(base_req);

        // request a spatial subset
        req.set("extent", spatial_partition[i]);

        // request a temporal subset
        req.set(this->index_request_key, temporal_partition[j]);

        // assign a GPU
        req.set("device_id", device_id);

        this->requests.emplace_back(std::move(req));
    }

    // print some info about the set of requests
    if (this->get_verbose())
    {
        std::ostringstream oss;
        oss << teca_parallel_id()
            << " teca_space_time_executive::initialize index_initializer_key="
            << this->index_initializer_key << " " << this->index_initializer_key
            << "=" << n_indices << " index_request_key=" << this->index_request_key
            << " first=" << this->first_step << " last=" << this->last_step
            << " block_start=" << block_start + first << " block_size=" << block_size;
        std::cerr << oss.str() << std::endl;
    }

    return 0;
}

// --------------------------------------------------------------------------
teca_metadata teca_space_time_executive::get_next_request()
{
    teca_metadata req;
    if (!this->requests.empty())
    {
        req = this->requests.back();
        this->requests.pop_back();

        // print the details of the current request. this is a execution
        // progress indicator of where the run is at
        if (this->get_verbose())
        {
            std::vector<unsigned long> extent;
            req.get("extent", extent);

            unsigned long temporal_extent[2] = {0};
            req.get(this->index_request_key, temporal_extent, 2);

            std::ostringstream oss;
            oss << teca_parallel_id()
               << " teca_space_time_executive::get_next_request "
               << this->index_request_key << " = [" << temporal_extent
               << "] extent = [" << extent << "]";

            std::cerr << oss.str() << std::endl;
        }
    }

    return req;
}
