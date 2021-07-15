#ifndef teca_apply_tempest_remap_h
#define teca_apply_tempest_remap_h

#include "teca_shared_object.h"
#include "teca_algorithm.h"
#include "teca_metadata.h"

#include <string>
#include <vector>

TECA_SHARED_OBJECT_FORWARD_DECL(teca_apply_tempest_remap)

/**
 * @brief Moves data from one mesh to anotehr using remapping weights
 * generated by TempestRemap
 *
 * @details The algorithm has 3 inputs:
 *
 * 1) the source mesh, src
 * 2) the target mesh, tgt
 * 3) the matrix of weights, S, row indices, ri, column indices ci
 *
 * @code{.py)
 * for i in 0,len(S):
 *   tgt[row[i]] = tgt[ri[i]] + S[i] * src[ci[i]]
 * @endcode
 */
class teca_apply_tempest_remap : public teca_algorithm
{
public:
    TECA_ALGORITHM_STATIC_NEW(teca_apply_tempest_remap)
    TECA_ALGORITHM_DELETE_COPY_ASSIGN(teca_apply_tempest_remap)
    TECA_ALGORITHM_CLASS_NAME(teca_apply_tempest_remap)
    ~teca_apply_tempest_remap();

    // report/initialize to/from Boost program options
    // objects.
    TECA_GET_ALGORITHM_PROPERTIES_DESCRIPTION()
    TECA_SET_ALGORITHM_PROPERTIES()

    /** @name weights_variable
     * Set the name of the variable containing the remap weights
     */
    ///@{
    TECA_ALGORITHM_PROPERTY(std::string, weights_variable)
    ///@}

    /** @name row_variable
     * Set the name of the variable containing the row indices
     */
    ///@{
    TECA_ALGORITHM_PROPERTY(std::string, row_variable)
    ///@}

    /** @name column_variable
     * Set the name of the variable containing the column indices
     */
    ///@{
    TECA_ALGORITHM_PROPERTY(std::string, column_variable)
    ///@}

    /** @name target_mask_variable
     * Set the name of the variable containing a valid value mask telling which
     * indices in the target are valid. This is used on GOES data where some
     * mesh points are invalid. When supplied the result of the remap operation
     * is coppied in order to the valid locations skipping over mesh locations
     * marked as invalid.
     */
    ///@{
    TECA_ALGORITHM_PROPERTY(std::string, target_mask_variable)
    ///@}

    /** @name static_target_mesh
     * Force requesting time step 0 of the target mesh.
     */
    ///@{
    TECA_ALGORITHM_PROPERTY(int, static_target_mesh)
    ///@}

protected:
    teca_apply_tempest_remap();

private:
    teca_metadata get_output_metadata(unsigned int port,
        const std::vector<teca_metadata> &input_md) override;

    std::vector<teca_metadata> get_upstream_request(
        unsigned int port, const std::vector<teca_metadata> &input_md,
        const teca_metadata &request) override;

    const_p_teca_dataset execute(
        unsigned int port, const std::vector<const_p_teca_dataset> &input_data,
        const teca_metadata &request) override;
private:
    std::string weights_variable;
    std::string row_variable;
    std::string column_variable;
    std::string target_mask_variable;
    int static_target_mesh;
};

#endif
