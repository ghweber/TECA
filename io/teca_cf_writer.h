#ifndef teca_cf_writer_h
#define teca_cf_writer_h

#include "teca_shared_object.h"
#include "teca_threaded_algorithm.h"
#include "teca_metadata.h"

#include <vector>
#include <string>

TECA_SHARED_OBJECT_FORWARD_DECL(teca_cf_writer)


/// A writer for Cartesian meshes in NetCDF CF2 format.
/**
 * Writes data to NetCDF CF2 format. This algorithm is conceptually an
 * execution engine capable of driving the above pipeline with our without
 * threads and stream results in the order that they are generated placing them
 * in the correct location in the output dataset. The output dataset is a
 * collection of files each with a user specified number of time steps per
 * file. The total number of time steps in the dataset is determined by the
 * combination of the number of time steps in the input dataset and user
 * defined subsetting if any. The writer uses MPI collective I/O to produce the
 * files. In parallel time steps are mapped to ranks such that each rank has
 * approximately the same number of time steps. Incoming steps are mapped to
 * files. A given MPI rank may be writing to multiple files. The use of
 * MPI collectives implies care must be taken in its use to avoid deadlocks.
 *
 * Due to the use of MPI collectives I/O certain information must be known
 * during the report phase of pipeline execution, before the execute phase of
 * pipeline execution begins. The information that is needed is:
 *
 * number of time steps - specified by the pipeline control index_initializer
 *                        key found in metadata produced by the source
 *                        (e.g CF reader)
 *
 * extent - 6 64 bit integers defining the 3 spatial dimensions of each
 *          timestep found in metadata produced by the source (e.g CF reader)
 *
 * point arrays - list of strings naming the point centered arrays that will be
 *                written. set by the user prior to execution by writer
 *                properties.
 *
 * information arrays - list of strings naming the non-geometric arrays that
 *                      will written. set by the user prior to execution by
 *                      writer properties. See also size attribute below.
 *
 * type_code - the teca_variant_array_code naming the type of each array. this
 *             will be in the array attributes metadata generated by the
 *             producer of the array (e.g any algorithm that adds an array
 *             should provide this metadata).
 *
 * size - a 64 bit integer declaring the size of each information array. this
 *             will be in the array attributes metadata generated by the 
 *             producer of the array (e.g any algorithm that adds an array
 *             should provide this metadata).
 */
class teca_cf_writer : public teca_threaded_algorithm
{
public:
    TECA_ALGORITHM_STATIC_NEW(teca_cf_writer)
    TECA_ALGORITHM_DELETE_COPY_ASSIGN(teca_cf_writer)
    TECA_ALGORITHM_CLASS_NAME(teca_cf_writer)
    ~teca_cf_writer();

    // report/initialize to/from Boost program options
    // objects.
    TECA_GET_ALGORITHM_PROPERTIES_DESCRIPTION()
    TECA_SET_ALGORITHM_PROPERTIES()

    // set the output filename. for time series the substring %t% is replaced
    // with the current time step or date. See comments on date_format below
    // for info about date formatting.
    TECA_ALGORITHM_PROPERTY(std::string, file_name)

    // set the format for the date to write in the filename. this requires the
    // input dataset to have unit/calendar information if none are available,
    // the time index is used instead. (%F-%HZ)
    TECA_ALGORITHM_PROPERTY(std::string, date_format)

    // set the range of time step to process.
    TECA_ALGORITHM_PROPERTY(long, first_step)
    TECA_ALGORITHM_PROPERTY(long, last_step)

    // set how many time steps are written to each file. Note that upstream is
    // parallelized over files rather than time steps.  this has the affect of
    // reducing the available opportunity for MPI parallelization by this
    // factor. For example if there are 16 time steps and steps_per_file is 8,
    // 2 MPI ranks each running 8 or more threads would be optimal. One
    // should make such calculations when planning large runs if optimal
    // performance is desired. time steps are gathered before the file is
    // written, thus available memory per MPI rank is the limiting factor in
    // how many steps can be stored in a single file (128).
    TECA_ALGORITHM_PROPERTY(unsigned int, steps_per_file)

    // sets the flags passed to NetCDF during file creation. (NC_CLOBBER)
    TECA_ALGORITHM_PROPERTY(int, mode_flags)

    // if set the slowest varying dimension is specified to be NC_UNLIMITED.
    // This has a negative impact on performance when reading the values in a
    // single pass. However, unlimited dimensions are used ubiquitously thus
    // by default it is set. For data being consumed by TECA performance will
    // be better when using fixed dimensions. (1) This feature requires
    // collective writes and is incompatible with out of order execution,
    // and hence currently not supported.
    TECA_ALGORITHM_PROPERTY(int, use_unlimited_dim)

    // sets the compression level used for each variable compression is not
    // used if the value is less than or equal to 0. This feature requires
    // collective writes and is incompatible with out of order execution,
    // and hence currently not supported.
    TECA_ALGORITHM_PROPERTY(int, compression_level)

    // flush files before closing them, this may be necessary if accessing data
    // immediately.
    TECA_ALGORITHM_PROPERTY(int, flush_files);

    // specify the arrays to write. a data array is only written to disk if
    // it is included in this list. it is an error to not specify at least
    // one point centered array to write
    TECA_ALGORITHM_VECTOR_PROPERTY(std::string, point_array)
    TECA_ALGORITHM_VECTOR_PROPERTY(std::string, information_array)

protected:
    teca_cf_writer();

private:
    const_p_teca_dataset execute(unsigned int port,
        const std::vector<const_p_teca_dataset> &input_data,
        const teca_metadata &request, int streaming) override;

    teca_metadata get_output_metadata(unsigned int port,
        const std::vector<teca_metadata> &input_md) override;

    std::vector<teca_metadata> get_upstream_request(unsigned int port,
        const std::vector<teca_metadata> &input_md,
        const teca_metadata &request) override;

    // flush data to disk. this may be necessary if accessing data
    // immediately.
    int flush();

private:
    std::string file_name;
    std::string date_format;
    long first_step;
    long last_step;
    unsigned int steps_per_file;
    int mode_flags;
    int use_unlimited_dim;
    int compression_level;
    int flush_files;

    std::vector<std::string> point_arrays;
    std::vector<std::string> information_arrays;

    class internals_t;
    internals_t *internals;
};

#endif
