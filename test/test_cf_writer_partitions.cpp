#include "teca_config.h"
#include "teca_l2_norm.h"
#include "teca_cartesian_mesh_source.h"
#include "teca_cf_reader.h"
#include "teca_cf_writer.h"
#include "teca_dataset_diff.h"
#include "teca_file_util.h"
#include "teca_system_util.h"
#include "teca_system_interface.h"
#include "teca_index_executive.h"
#include "teca_array_attributes.h"
#include "teca_mpi_manager.h"
#include "teca_variant_array_impl.h"
#include "teca_variant_array_util.h"

#include <vector>
#include <string>
#include <iostream>

using namespace teca_variant_array_util;

// generates : f(x,y,z) = x - t; or f(x,y,z) = y; or f(x,y,z) = z depending on
// the value of m_dir. this is used to generate a vector field whose magnitude
// is concentric spheres centered at (t,0,0)
struct fxyz
{
    fxyz(char a_dir) : m_dir(a_dir) {}

    char m_dir;

    p_teca_variant_array operator()(const const_p_teca_variant_array &x,
        const const_p_teca_variant_array &y, const const_p_teca_variant_array &z,
        double t)
    {
        // convert from day number to t in [0, 1]
        t /= 365.0;

        size_t nx = x->size();
        size_t ny = y->size();
        size_t nz = z->size();
        size_t nxy = nx*ny;
        size_t nxyz = nxy*nz;

        p_teca_variant_array f = x->new_instance(nxyz);

        VARIANT_ARRAY_DISPATCH_FP(x.get(),

            assert_type<CTT>(y, z);

            auto [pf] = data<TT>(f);
            auto [spx, px, spy, py, spz, pz] = get_cpu_accessible<CTT>(x, y, z);

            for (size_t k = 0; k < nz; ++k)
            {
                for (size_t j = 0; j < ny; ++j)
                {
                    for (size_t i = 0; i < nx; ++i)
                    {
                        NT fval = NT();
                        switch (m_dir)
                        {
                            case 'x':
                                fval = px[i] - NT(t);
                                break;
                            case 'y':
                                fval = py[j];
                                break;
                            case 'z':
                                fval = pz[k];
                                break;
                        }
                        pf[k*nxy + j*nx + i] = fval;
                    }
                }
            }
            )

        return f;
    }
};

int main(int argc, char **argv)
{
    teca_mpi_manager man(argc, argv);
    int rank = man.get_comm_rank();

    teca_system_interface::set_stack_trace_on_error();

    if (argc != 12)
    {
        std::cerr << std::endl << "Usage error:" << std::endl
            << "test_l2_norm [nx] [ny] [nz] [nt] [layout] [partitioner]"
            " [temporal partition size] [number of spatial partitions]"
            " [data root] [out file]" << std::endl << std::endl;
        return -1;
    }

    size_t nx = atoi(argv[1]);
    size_t ny = atoi(argv[2]);
    size_t nz = atoi(argv[3]);
    size_t nt = atoi(argv[4]);
    std::string layout = argv[5];
    std::string partitioner = argv[6];
    int time_partition_size = atoi(argv[7]);
    int n_spatial_partitions = atoi(argv[8]);
    int collective_buffer = atoi(argv[9]);
    std::string data_root = argv[10];
    std::string base_file = argv[11];

    // the cf write will skip dimensions of 1, this will make the z coordinate
    // match the fake one generated by the cf reader in that case
    double z0 = nz == 1 ? 0.0 : -1.0;
    double z1 = nz == 1 ? 0.0 :  1.0;

    p_teca_index_executive exec = teca_index_executive::New();

    // ##############
    // # Pipeline 1
    // ##############
    p_teca_cartesian_mesh_source src = teca_cartesian_mesh_source::New();
    src->set_whole_extents({0, nx - 1, 0, ny - 1, 0, nz - 1, 0, nt - 1});
    src->set_bounds({-4.0, 4.0, -2.0, 2.0, z0, z1, 0.0, 364.99});
    src->set_calendar("standard", "days since 2022-01-01 00:00:00");
    //src->set_verbose(1);

    src->append_field_generator({"U",
        teca_array_attributes(teca_variant_array_code<double>::get(),
            teca_array_attributes::point_centering, 0, "meters",
            "distance", "distance from x=0"),
            fxyz('x')});

    src->append_field_generator({"V",
        teca_array_attributes(teca_variant_array_code<double>::get(),
            teca_array_attributes::point_centering, 0, "meters",
            "distance", "distance from y = 0"),
            fxyz('y')});

    src->append_field_generator({"W",
        teca_array_attributes(teca_variant_array_code<double>::get(),
            teca_array_attributes::point_centering, 0, "meters",
            "distance", "distance from z = 0"),
            fxyz('z')});

    p_teca_l2_norm l2 = teca_l2_norm::New();
    l2->set_input_connection(src->get_output_port());
    l2->set_component_0_variable("U");
    l2->set_component_1_variable("V");
    l2->set_component_2_variable("W");
    l2->set_l2_norm_variable("R");

    std::string test_file = "tmp_" + base_file + "_%t%.nc";

    p_teca_cf_writer cfw = teca_cf_writer::New();
    cfw->set_input_connection(l2->get_output_port());
    cfw->set_file_name(test_file);
    cfw->set_executive(exec);
    cfw->set_point_arrays({"U", "V", "W", "R"});
    cfw->set_thread_pool_size(-1);
    cfw->set_layout(layout);
    cfw->set_partitioner(partitioner);
    cfw->set_temporal_partition_size(time_partition_size);
    cfw->set_number_of_spatial_partitions(n_spatial_partitions);
    cfw->set_collective_buffer(collective_buffer);
    cfw->set_verbose(1);

    // run the pipeline
    cfw->update();

    // make sure the data makes it to disk
    MPI_Barrier(MPI_COMM_WORLD);
#if __APPLE__
    sleep(10);
#endif

    // ##############
    // # Pipeline 2
    // ##############
    if (rank == 0)
    {
        std::string test_file_re = "tmp_" + base_file + ".*\\.nc$";

        // read the data back in
        p_teca_cf_reader cfr = teca_cf_reader::New();
        cfr->set_communicator(MPI_COMM_SELF);
        cfr->set_files_regex(test_file_re);

        if (nz > 1)
        {
            cfr->set_z_axis_variable("plev");
        }

        std::string out_file = data_root + "/" + base_file + ".nc";

        bool do_test = true;
        teca_system_util::get_environment_variable("TECA_DO_TEST", do_test);
        if (do_test && teca_file_util::file_exists(out_file.c_str()))
        {
            // run the test
            std::cerr << "running the test ... " << std::endl;

            std::string base_file_re = data_root + "/" + base_file + ".*\\.nc$";

            exec->set_arrays({"U", "V", "W", "R"});

            p_teca_cf_reader baseline_cfr = teca_cf_reader::New();
            baseline_cfr->set_communicator(MPI_COMM_SELF);
            if (nz > 1)
                baseline_cfr->set_z_axis_variable("plev");
            baseline_cfr->set_files_regex(base_file_re);

            p_teca_dataset_diff diff = teca_dataset_diff::New();
            diff->set_communicator(MPI_COMM_SELF);
            diff->set_input_connection(0, baseline_cfr->get_output_port());
            diff->set_input_connection(1, cfr->get_output_port());
            diff->set_executive(exec);
            diff->set_verbose(1);

            diff->update();
        }
        else
        {
            // make a baseline
            std::cerr << "generating baseline image ... " << base_file << std::endl;

            p_teca_cf_writer baseline_cfw = teca_cf_writer::New();
            baseline_cfw->set_communicator(MPI_COMM_SELF);
            baseline_cfw->set_input_connection(cfr->get_output_port());
            baseline_cfw->set_file_name(out_file);
            baseline_cfw->set_executive(exec);
            baseline_cfw->set_point_arrays({"U", "V", "W", "R"});
            baseline_cfw->set_thread_pool_size(1);
            baseline_cfw->set_layout_to_number_of_steps();
            baseline_cfw->set_steps_per_file(nt);
            baseline_cfw->set_verbose(2);

            // run the pipeline
            baseline_cfw->update();

            return -1;
        }
    }

    return 0;
}
