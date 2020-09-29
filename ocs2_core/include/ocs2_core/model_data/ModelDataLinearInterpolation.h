/******************************************************************************
Copyright (c) 2017, Farbod Farshidian. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this
  list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice,
  this list of conditions and the following disclaimer in the documentation
  and/or other materials provided with the distribution.

* Neither the name of the copyright holder nor the names of its
  contributors may be used to endorse or promote products derived from
  this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
******************************************************************************/

#pragma once

#include <ocs2_core/misc/LinearInterpolation.h>
#include <ocs2_core/model_data/ModelDataBase.h>

/*
 * @file
 * The linear interpolation of cost inpute-state derivative, Pm, at index-alpha pair given modelDataTrajectory can
 * be computed as:
 *
 * ModelData::interpolate(indexAlpha, Pm, &modelDataTrajectory, ModelData::cost_dfdux);
 */

/*
 * Declares an access function of name FIELD such as time, dynamics, dynamicsBias, ...
 * For example the signature of function for dynamics is:
 * const vector_t& dynamics(const std::vector<ocs2::ModelDataBase>& vec, size_t n) {
 *   return vec[n].dynamic_;
 * }
 */
#define CREATE_INTERPOLATION_ACCESS_FUNCTION(FIELD)                                                                \
  inline auto FIELD(const std::vector<ocs2::ModelDataBase>& vec, size_t ind)->const decltype(vec[ind].FIELD##_)& { \
    return vec[ind].FIELD##_;                                                                                      \
  }

#define CREATE_INTERPOLATION_ACCESS_FUNCTION_SUBFIELD(FIELD, SUBFIELD)                                                                   \
  inline auto FIELD##_##SUBFIELD(const std::vector<ocs2::ModelDataBase>& vec, size_t ind)->const decltype(vec[ind].FIELD##_.SUBFIELD)& { \
    return vec[ind].FIELD##_.SUBFIELD;                                                                                                   \
  }

namespace ocs2 {
namespace ModelData {

/**
 * Access method for different subfields of the ModelData.
 */

// time
CREATE_INTERPOLATION_ACCESS_FUNCTION(time)

// dynamics
CREATE_INTERPOLATION_ACCESS_FUNCTION_SUBFIELD(dynamics, f)
CREATE_INTERPOLATION_ACCESS_FUNCTION(dynamicsBias)
CREATE_INTERPOLATION_ACCESS_FUNCTION_SUBFIELD(dynamics, dfdx)
CREATE_INTERPOLATION_ACCESS_FUNCTION_SUBFIELD(dynamics, dfdu)
CREATE_INTERPOLATION_ACCESS_FUNCTION(dynamicsCovariance)

// cost
CREATE_INTERPOLATION_ACCESS_FUNCTION_SUBFIELD(cost, f)
CREATE_INTERPOLATION_ACCESS_FUNCTION_SUBFIELD(cost, dfdx)
CREATE_INTERPOLATION_ACCESS_FUNCTION_SUBFIELD(cost, dfdu)
CREATE_INTERPOLATION_ACCESS_FUNCTION_SUBFIELD(cost, dfdxx)
CREATE_INTERPOLATION_ACCESS_FUNCTION_SUBFIELD(cost, dfduu)
CREATE_INTERPOLATION_ACCESS_FUNCTION_SUBFIELD(cost, dfdux)

// state equality constraints
CREATE_INTERPOLATION_ACCESS_FUNCTION_SUBFIELD(stateEqConstr, f)
CREATE_INTERPOLATION_ACCESS_FUNCTION_SUBFIELD(stateEqConstr, dfdx)

// state-input equality constraints
CREATE_INTERPOLATION_ACCESS_FUNCTION_SUBFIELD(stateInputEqConstr, f)
CREATE_INTERPOLATION_ACCESS_FUNCTION_SUBFIELD(stateInputEqConstr, dfdx)
CREATE_INTERPOLATION_ACCESS_FUNCTION_SUBFIELD(stateInputEqConstr, dfdu)

}  // namespace ModelData
}  // namespace ocs2

#undef CREATE_INTERPOLATION_ACCESS_FUNCTION
#undef CREATE_INTERPOLATION_ACCESS_FUNCTION_SUBFIELD
