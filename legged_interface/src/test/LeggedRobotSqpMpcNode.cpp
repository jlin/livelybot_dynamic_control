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

/********************************************************************************
Modified Copyright (c) 2023-2024, BridgeDP Robotics.Co.Ltd. All rights reserved.

For further information, contact: contact@bridgedp.com or visit our website
at www.bridgedp.com.
********************************************************************************/

#include <ros/init.h>
#include <ros/package.h>

#include "legged_interface/LeggedInterface.h"
#include <ocs2_ros_interfaces/mpc/MPC_ROS_Interface.h>
#include <ocs2_ros_interfaces/synchronized_module/RosReferenceManager.h>
#include <ocs2_ros_interfaces/synchronized_module/SolverObserverRosCallbacks.h>
#include <ocs2_sqp/SqpMpc.h>

// #include "ocs2_legged_robot_ros/gait/GaitReceiver.h"

using namespace ocs2;
using namespace legged_robot;

int main(int argc, char** argv)
{
  const std::string robotName = "legged_robot";

  // Initialize ros node
  ::ros::init(argc, argv, robotName + "_mpc");
  ::ros::NodeHandle nodeHandle;
  // Get node parameters
  bool multiplot = false;
  std::string taskFile, urdfFile, referenceFile;
  nodeHandle.getParam("/multiplot", multiplot);
  nodeHandle.getParam("/taskFile", taskFile);
  nodeHandle.getParam("/urdfFile", urdfFile);
  nodeHandle.getParam("/referenceFile", referenceFile);

  // Robot interface
  legged::LeggedInterface interface(taskFile, urdfFile, referenceFile);
  bool verbose = true;
  interface.setupOptimalControlProblem(taskFile, urdfFile, referenceFile, verbose);

  // ROS ReferenceManager
  auto rosReferenceManagerPtr = std::make_shared<RosReferenceManager>(robotName, interface.getReferenceManagerPtr());
  rosReferenceManagerPtr->subscribe(nodeHandle);

  // MPC
  SqpMpc mpc(interface.mpcSettings(), interface.sqpSettings(), interface.getOptimalControlProblem(),
             interface.getInitializer());
  mpc.getSolverPtr()->setReferenceManager(rosReferenceManagerPtr);

  // observer for zero velocity constraints (only add this for debugging as it slows down the solver)
  if (multiplot)
  {
    auto createStateInputBoundsObserver = [&](const std::string& termName) {
      const ocs2::scalar_array_t observingTimePoints{ 0.0 };
      const std::vector<std::string> topicNames{ "metrics/" + termName + "/0MsLookAhead" };
      auto callback = ocs2::ros::createConstraintCallback(
          nodeHandle, { 0.0 }, topicNames, ocs2::ros::CallbackInterpolationStrategy::linear_interpolation);
      return ocs2::SolverObserver::ConstraintTermObserver(ocs2::SolverObserver::Type::Intermediate, termName,
                                                          std::move(callback));
    };
    for (size_t i = 0; i < interface.getCentroidalModelInfo().numThreeDofContacts; i++)
    {
      const std::string& footName = interface.modelSettings().contactNames3DoF[i];
      mpc.getSolverPtr()->addSolverObserver(createStateInputBoundsObserver(footName + "_zeroVelocity"));
    }
  }

  // Launch MPC ROS node
  MPC_ROS_Interface mpcNode(mpc, robotName);
  mpcNode.launchNodes(nodeHandle);

  // Successful exit
  return 0;
}
