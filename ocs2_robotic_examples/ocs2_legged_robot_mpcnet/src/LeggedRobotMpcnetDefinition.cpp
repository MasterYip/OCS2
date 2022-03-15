/******************************************************************************
Copyright (c) 2022, Farbod Farshidian. All rights reserved.

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

#include "ocs2_legged_robot_mpcnet/LeggedRobotMpcnetDefinition.h"

#include <iostream>

#include <ocs2_legged_robot/gait/LegLogic.h>
#include <ocs2_robotic_tools/common/RotationTransforms.h>

namespace ocs2 {
namespace legged_robot {

vector_t LeggedRobotMpcnetDefinition::getGeneralizedTime(scalar_t t, const ModeSchedule& modeSchedule) {
  feet_array_t<LegPhase> swingPhasePerLeg = getSwingPhasePerLeg(t, modeSchedule);
  vector_t generalizedTime;
  generalizedTime.resize(3 * NUM_CONTACT_POINTS, Eigen::NoChange);
  // phase
  for (int i = 0 * NUM_CONTACT_POINTS; i < 1 * NUM_CONTACT_POINTS; i++) {
    if (swingPhasePerLeg[i % NUM_CONTACT_POINTS].phase < 0.0) {
      generalizedTime[i] = 0.0;
    } else {
      generalizedTime[i] = swingPhasePerLeg[i % NUM_CONTACT_POINTS].phase;
    }
  }
  // phase rate
  for (int i = 1 * NUM_CONTACT_POINTS; i < 2 * NUM_CONTACT_POINTS; i++) {
    if (swingPhasePerLeg[i % NUM_CONTACT_POINTS].phase < 0.0) {
      generalizedTime[i] = 0.0;
    } else {
      generalizedTime[i] = 1.0 / swingPhasePerLeg[i % NUM_CONTACT_POINTS].duration;
    }
  }
  // sin(pi * phase)
  for (int i = 2 * NUM_CONTACT_POINTS; i < 3 * NUM_CONTACT_POINTS; i++) {
    if (swingPhasePerLeg[i % NUM_CONTACT_POINTS].phase < 0.0) {
      generalizedTime[i] = 0.0;
    } else {
      generalizedTime[i] = std::sin(M_PI * swingPhasePerLeg[i % NUM_CONTACT_POINTS].phase);
    }
  }
  return generalizedTime;
}

vector_t LeggedRobotMpcnetDefinition::getRelativeState(scalar_t t, const vector_t& x, const TargetTrajectories& targetTrajectories) {
  vector_t relativeState = x - targetTrajectories.getDesiredState(t);
  matrix3_t R = getRotationMatrixFromZyxEulerAngles<scalar_t>(x.segment<3>(9)).transpose();
  relativeState.segment<3>(0) = R * relativeState.segment<3>(0);
  relativeState.segment<3>(3) = R * relativeState.segment<3>(3);
  relativeState.segment<3>(6) = R * relativeState.segment<3>(6);
  // TODO(areske): use quaternionDistance() for orientation error?
  return relativeState;
}

matrix_t LeggedRobotMpcnetDefinition::getInputTransformation(scalar_t t, const vector_t& x) {
  matrix3_t R = getRotationMatrixFromZyxEulerAngles<scalar_t>(x.segment<3>(9));
  matrix_t inputTransformation = matrix_t::Identity(24, 24);
  inputTransformation.block<3, 3>(0, 0) = R;
  inputTransformation.block<3, 3>(3, 3) = R;
  inputTransformation.block<3, 3>(6, 6) = R;
  inputTransformation.block<3, 3>(9, 9) = R;
  return inputTransformation;
}

bool LeggedRobotMpcnetDefinition::validState(const vector_t& x) {
  vector_t deviation = x - defaultState_;
  if (std::abs(deviation[8]) > 0.2) {
    std::cerr << "LeggedRobotMpcnetDefinition::validState Height diverged: " << x[8] << std::endl;
    return false;
  } else if (std::abs(deviation[10]) > 30.0 * M_PI / 180.0) {
    std::cerr << "LeggedRobotMpcnetDefinition::validState Pitch diverged: " << x[10] << std::endl;
    return false;
  } else if (std::abs(deviation[11]) > 30.0 * M_PI / 180.0) {
    std::cerr << "LeggedRobotMpcnetDefinition::validState Roll diverged: " << x[11] << std::endl;
    return false;
  } else {
    return true;
  }
}

}  // namespace legged_robot
}  // namespace ocs2
