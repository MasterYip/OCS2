

#include <gtest/gtest.h>

#include <ocs2_comm_interfaces/ocs2_ros_interfaces/mpc/MPC_ROS_Interface.h>
#include <ocs2_comm_interfaces/ocs2_ros_interfaces/mrt/MRT_ROS_Dummy_Loop.h>

#include "ocs2_ballbot_example/BallbotInterface.h"
#include "ocs2_ballbot_example/definitions.h"

using namespace ocs2;

TEST(BallbotIntegrationTest, createDummyMRT) {
  std::string taskFileFolderName = "mpc";
  ballbot::BallbotInterface ballbotInterface(taskFileFolderName);

  MRT_ROS_Interface mrt("ballbot");

  // Dummy ballbot
  MRT_ROS_Dummy_Loop dummyBallbot(mrt, ballbotInterface.mpcSettings().mrtDesiredFrequency_,
                                  ballbotInterface.mpcSettings().mpcDesiredFrequency_);

  // Initialize dummy
  SystemObservation initObservation;
  initObservation.state() = ballbotInterface.getInitialState();
  initObservation.input().setZero(ocs2::ballbot::INPUT_DIM);
  initObservation.time() = 0.0;
}

TEST(BallbotIntegrationTest, createMPC) {
  std::string taskFileFolderName = "mpc";
  ballbot::BallbotInterface ballbotInterface(taskFileFolderName);

  // Create MPC ROS node
  auto mpcPtr = ballbotInterface.getMpc();
  MPC_ROS_Interface mpcNode(*mpcPtr, "ballbot");
}

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
