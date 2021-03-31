/******************************************************************************
Copyright (c) 2020, Farbod Farshidian. All rights reserved.

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

#include "ocs2_sqp/MultipleShootingSolver.h"

#include <iostream>
#include <numeric>

#include <ocs2_core/OCS2NumericTraits.h>
#include <ocs2_core/constraint/RelaxedBarrierPenalty.h>
#include <ocs2_core/control/FeedforwardController.h>
#include <ocs2_core/control/LinearController.h>
#include <ocs2_core/misc/LinearInterpolation.h>

#include "ocs2_sqp/MultipleShootingTranscription.h"

namespace ocs2 {

MultipleShootingSolver::MultipleShootingSolver(Settings settings, const SystemDynamicsBase* systemDynamicsPtr,
                                               const CostFunctionBase* costFunctionPtr, const ConstraintBase* constraintPtr,
                                               const CostFunctionBase* terminalCostFunctionPtr,
                                               const SystemOperatingTrajectoriesBase* operatingTrajectoriesPtr)
    : SolverBase(),
      settings_(std::move(settings)),
      totalNumIterations_(0),
      hpipmInterface_(hpipm_interface::OcpSize(), settings.hpipmSettings) {
  // Multithreading, set up threadpool for N-1 helpers, our main thread is the N-th one.
  if (settings_.nThreads > 1) {
    threadPoolPtr_.reset(new ThreadPool(settings_.nThreads - 1, settings_.threadPriority));
  }
  Eigen::setNbThreads(1);  // No multithreading within Eigen.
  Eigen::initParallel();

  // Dynamics discretization
  discretizer_ = selectDynamicsDiscretization(settings.integratorType);
  sensitivityDiscretizer_ = selectDynamicsSensitivityDiscretization(settings.integratorType);

  // Clone objects to have one for each worker
  for (int w = 0; w < settings.nThreads; w++) {
    systemDynamicsPtr_.emplace_back(systemDynamicsPtr->clone());
    costFunctionPtr_.emplace_back(costFunctionPtr->clone());
    if (constraintPtr != nullptr) {
      constraintPtr_.emplace_back(constraintPtr->clone());
    } else {
      constraintPtr_.emplace_back(nullptr);
    }
  }

  if (constraintPtr != nullptr && settings_.inequalityConstraintMu > 0) {
    penaltyPtr_.reset(new RelaxedBarrierPenalty(settings_.inequalityConstraintMu, settings_.inequalityConstraintDelta));
  }

  if (terminalCostFunctionPtr != nullptr) {
    terminalCostFunctionPtr_.reset(terminalCostFunctionPtr->clone());
  }

  if (operatingTrajectoriesPtr != nullptr) {
    operatingTrajectoriesPtr_.reset(operatingTrajectoriesPtr->clone());
  }
}

MultipleShootingSolver::~MultipleShootingSolver() {
  if (settings_.printSolverStatistics) {
    std::cerr << getBenchmarkingInformation() << std::endl;
  }
}

void MultipleShootingSolver::reset() {
  // Clear solution
  primalSolution_ = PrimalSolution();
  performanceIndeces_.clear();

  // reset timers
  totalNumIterations_ = 0;
  linearQuadraticApproximationTimer_.reset();
  solveQpTimer_.reset();
  linesearchTimer_.reset();
  computeControllerTimer_.reset();
}

std::string MultipleShootingSolver::getBenchmarkingInformation() const {
  const auto linearQuadraticApproximationTotal = linearQuadraticApproximationTimer_.getTotalInMilliseconds();
  const auto solveQpTotal = solveQpTimer_.getTotalInMilliseconds();
  const auto linesearchTotal = linesearchTimer_.getTotalInMilliseconds();
  const auto computeControllerTotal = computeControllerTimer_.getTotalInMilliseconds();

  const auto benchmarkTotal = linearQuadraticApproximationTotal + solveQpTotal + linesearchTotal + computeControllerTotal;

  std::stringstream infoStream;
  if (benchmarkTotal > 0.0) {
    const scalar_t inPercent = 100.0;
    infoStream << "\n########################################################################\n";
    infoStream << "The benchmarking is computed over " << totalNumIterations_ << " iterations. \n";
    infoStream << "SQP Benchmarking\t   :\tAverage time [ms]   (% of total runtime)\n";
    infoStream << "\tLQ Approximation   :\t" << linearQuadraticApproximationTimer_.getAverageInMilliseconds() << " [ms] \t\t("
               << linearQuadraticApproximationTotal / benchmarkTotal * inPercent << "%)\n";
    infoStream << "\tSolve QP           :\t" << solveQpTimer_.getAverageInMilliseconds() << " [ms] \t\t("
               << solveQpTotal / benchmarkTotal * inPercent << "%)\n";
    infoStream << "\tLinesearch         :\t" << linesearchTimer_.getAverageInMilliseconds() << " [ms] \t\t("
               << linesearchTotal / benchmarkTotal * inPercent << "%)\n";
    infoStream << "\tCompute Controller :\t" << computeControllerTimer_.getAverageInMilliseconds() << " [ms] \t\t("
               << computeControllerTotal / benchmarkTotal * inPercent << "%)\n";
  }
  return infoStream.str();
}

const std::vector<PerformanceIndex>& MultipleShootingSolver::getIterationsLog() const {
  if (performanceIndeces_.empty()) {
    throw std::runtime_error("[MultipleShootingSolver]: No performance log yet, no problem solved yet?");
  } else {
    return performanceIndeces_;
  }
}

void MultipleShootingSolver::runImpl(scalar_t initTime, const vector_t& initState, scalar_t finalTime,
                                     const scalar_array_t& partitioningTimes) {
  if (settings_.printSolverStatus || settings_.printLinesearch) {
    std::cerr << "\n++++++++++++++++++++++++++++++++++++++++++++++++++++++";
    std::cerr << "\n+++++++++++++ SQP solver is initialized ++++++++++++++";
    std::cerr << "\n++++++++++++++++++++++++++++++++++++++++++++++++++++++\n";
  }

  // Determine time discretization, taking into account event times.
  scalar_array_t timeDiscretization = multiple_shooting::timeDiscretizationWithEvents(
      initTime, finalTime, settings_.dt, this->getModeSchedule().eventTimes, OCS2NumericTraits<scalar_t>::limitEpsilon());
  const int N = static_cast<int>(timeDiscretization.size()) - 1;

  // Initialize the state and input
  vector_array_t x = initializeStateTrajectory(initState, timeDiscretization, N);
  vector_array_t u = initializeInputTrajectory(timeDiscretization, x, N);

  // Initialize cost
  for (auto& cost : costFunctionPtr_) {
    cost->setCostDesiredTrajectoriesPtr(&this->getCostDesiredTrajectories());
  }
  if (terminalCostFunctionPtr_) {
    terminalCostFunctionPtr_->setCostDesiredTrajectoriesPtr(&this->getCostDesiredTrajectories());
  }

  // Bookkeeping
  performanceIndeces_.clear();

  for (int iter = 0; iter < settings_.sqpIteration; iter++) {
    if (settings_.printSolverStatus || settings_.printLinesearch) {
      std::cerr << "\nSQP iteration: " << iter << "\n";
    }
    // Make QP approximation
    linearQuadraticApproximationTimer_.startTimer();
    performanceIndeces_.push_back(setupQuadraticSubproblem(timeDiscretization, initState, x, u));
    linearQuadraticApproximationTimer_.endTimer();

    // Solve QP
    solveQpTimer_.startTimer();
    const vector_t delta_x0 = initState - x[0];
    vector_array_t delta_x;
    vector_array_t delta_u;
    std::tie(delta_x, delta_u) = getOCPSolution(delta_x0);
    solveQpTimer_.endTimer();

    // Apply step
    linesearchTimer_.startTimer();
    bool converged = takeStep(performanceIndeces_.back(), timeDiscretization, initState, delta_x, delta_u, x, u);
    linesearchTimer_.endTimer();

    totalNumIterations_++;
    if (converged) {
      break;
    }
  }

  computeControllerTimer_.startTimer();
  // Store result in PrimalSolution.
  primalSolution_.timeTrajectory_ = std::move(timeDiscretization);
  primalSolution_.stateTrajectory_ = std::move(x);
  primalSolution_.inputTrajectory_ = std::move(u);
  primalSolution_.inputTrajectory_.push_back(primalSolution_.inputTrajectory_.back());  // repeat last input to make equal length vectors
  primalSolution_.modeSchedule_ = this->getModeSchedule();

  // Compute controller
  if (constraintPtr_.front() && settings_.projectStateInputEqualityConstraints) {
    // see doc/LQR_full.pdf for detailed derivation for feedback terms
    if (settings_.controllerFeedback) {
      matrix_array_t KMatrices = hpipmInterface_.getRiccatiFeedback(dynamics_[0], cost_[0]);
      vector_array_t uff;
      matrix_array_t controllerGain;
      uff.reserve(N + 1);
      controllerGain.reserve(N + 1);
      for (int i = 0; i < N; i++) {
        // Add feedback in u_tilde space
        controllerGain.push_back(std::move(constraints_[i].dfdx));
        controllerGain.back().noalias() += constraints_[i].dfdu * KMatrices[i];
        uff.push_back(primalSolution_.inputTrajectory_[i]);
        uff.back().noalias() -= controllerGain.back() * primalSolution_.stateTrajectory_[i];
      }
      // Copy last one to get correct length
      uff.push_back(uff.back());
      controllerGain.push_back(controllerGain.back());
      primalSolution_.controllerPtr_.reset(
          new LinearController(primalSolution_.timeTrajectory_, std::move(uff), std::move(controllerGain)));
    } else {
      primalSolution_.controllerPtr_.reset(new FeedforwardController(primalSolution_.timeTrajectory_, primalSolution_.inputTrajectory_));
    }
  } else {
    if (settings_.controllerFeedback) {
      // feedback controller
      matrix_array_t KMatrices = hpipmInterface_.getRiccatiFeedback(dynamics_[0], cost_[0]);
      vector_array_t uff;
      matrix_array_t controllerGain;
      uff.reserve(N + 1);
      controllerGain.reserve(N + 1);
      for (int i = 0; i < N; i++) {
        // Linear controller has convention u = uff + K * x;
        // We computed u = u'(t) + K (x - x'(t));
        // >> uff = u'(t) - K x'(t)
        uff.push_back(primalSolution_.inputTrajectory_[i]);
        uff.back().noalias() -= KMatrices[i] * primalSolution_.stateTrajectory_[i];
        controllerGain.push_back(std::move(KMatrices[i]));
      }
      // Copy last one to get correct length
      uff.push_back(uff.back());
      controllerGain.push_back(controllerGain.back());
      primalSolution_.controllerPtr_.reset(
          new LinearController(primalSolution_.timeTrajectory_, std::move(uff), std::move(controllerGain)));
    } else {
      primalSolution_.controllerPtr_.reset(new FeedforwardController(primalSolution_.timeTrajectory_, primalSolution_.inputTrajectory_));
    }
  }
  computeControllerTimer_.endTimer();

  if (settings_.printSolverStatus || settings_.printLinesearch) {
    std::cerr << "\n++++++++++++++++++++++++++++++++++++++++++++++++++++++";
    std::cerr << "\n+++++++++++++ SQP solver has terminated ++++++++++++++";
    std::cerr << "\n++++++++++++++++++++++++++++++++++++++++++++++++++++++\n";
  }
}

void MultipleShootingSolver::runParallel(std::function<void(int)> taskFunction) {
  // Launch tasks in helper threads
  std::vector<std::future<void>> futures;
  if (threadPoolPtr_) {
    int numHelpers = settings_.nThreads - 1;
    futures.reserve(numHelpers);
    for (int i = 0; i < numHelpers; i++) {
      futures.emplace_back(threadPoolPtr_->run(taskFunction));
    }
  }
  // Execute one instance in this thread.
  taskFunction(0);

  // Wait for helpers to finish.
  for (auto&& fut : futures) {
    fut.get();
  }
}

vector_array_t MultipleShootingSolver::initializeInputTrajectory(const scalar_array_t& timeDiscretization,
                                                                 const vector_array_t& stateTrajectory, int N) const {
  const scalar_t interpolateTill = (totalNumIterations_ > 0) ? primalSolution_.timeTrajectory_.back() : timeDiscretization.front();

  vector_array_t u;
  u.reserve(N);
  for (int i = 0; i < N; i++) {
    const scalar_t ti = timeDiscretization[i];
    if (ti < interpolateTill) {
      // Interpolate previous input trajectory
      u.emplace_back(
          LinearInterpolation::interpolate(timeDiscretization[i], primalSolution_.timeTrajectory_, primalSolution_.inputTrajectory_));
    } else {
      // No previous control at this time-point -> fall back to heuristics
      if (operatingTrajectoriesPtr_) {
        // Ask for operating trajectory between t[k] and t[k+1]. Take the returned input at t[k] as our heuristic.
        const scalar_t tNext = timeDiscretization[i + 1];
        scalar_array_t timeArray;
        vector_array_t stateArray;
        vector_array_t inputArray;
        operatingTrajectoriesPtr_->getSystemOperatingTrajectories(stateTrajectory[i], ti, tNext, timeArray, stateArray, inputArray, false);
        u.push_back(std::move(inputArray.front()));
      } else {  // No information at all. Set inputs to zero.
        u.emplace_back(vector_t::Zero(settings_.n_input));
      }
    }
  }

  return u;
}

vector_array_t MultipleShootingSolver::initializeStateTrajectory(const vector_t& initState, const scalar_array_t& timeDiscretization,
                                                                 int N) const {
  if (totalNumIterations_ == 0) {  // first iteration
    return vector_array_t(N + 1, initState);
  } else {  // interpolation of previous solution
    vector_array_t x;
    x.reserve(N + 1);
    x.push_back(initState);  // Force linearization of the first node around the current state
    for (int i = 1; i < (N + 1); i++) {
      x.emplace_back(
          LinearInterpolation::interpolate(timeDiscretization[i], primalSolution_.timeTrajectory_, primalSolution_.stateTrajectory_));
    }
    return x;
  }
}

std::pair<vector_array_t, vector_array_t> MultipleShootingSolver::getOCPSolution(const vector_t& delta_x0) {
  // Solve the QP
  vector_array_t deltaXSol;
  vector_array_t deltaUSol;
  hpipm_status status;
  if (constraintPtr_.front() && !settings_.projectStateInputEqualityConstraints) {
    status = hpipmInterface_.solve(delta_x0, dynamics_, cost_, &constraints_, deltaXSol, deltaUSol, settings_.printSolverStatus);
  } else {  // without constraints, or when using QR decomposition, we have an unconstrained QP.
    status = hpipmInterface_.solve(delta_x0, dynamics_, cost_, nullptr, deltaXSol, deltaUSol, settings_.printSolverStatus);
  }

  if (status != hpipm_status::SUCCESS) {
    throw std::runtime_error("[MultipleShootingSolver] Failed to solve QP");
  }

  // remap the tilde delta u to real delta u
  if (constraintPtr_.front() && settings_.projectStateInputEqualityConstraints) {
    for (int i = 0; i < deltaUSol.size(); i++) {
      deltaUSol[i] = constraints_[i].dfdu * deltaUSol[i];  // creates a temporary because of alias
      deltaUSol[i].noalias() += constraints_[i].dfdx * deltaXSol[i];
      deltaUSol[i] += constraints_[i].f;
    }
  }

  return {deltaXSol, deltaUSol};
}

PerformanceIndex MultipleShootingSolver::setupQuadraticSubproblem(const scalar_array_t& time, const vector_t& initState,
                                                                  const vector_array_t& x, const vector_array_t& u) {
  // Problem horizon
  const int N = static_cast<int>(time.size()) - 1;

  // Set up for constant state input size. Will be adapted based on constraint handling.
  HpipmInterface::OcpSize ocpSize(N, settings_.n_state, settings_.n_input);

  std::vector<PerformanceIndex> performance(settings_.nThreads, PerformanceIndex());
  dynamics_.resize(N);
  cost_.resize(N + 1);
  constraints_.resize(N + 1);

  std::atomic_int workerId{0};
  std::atomic_int timeIndex{0};
  auto parallelTask = [&](int) {
    // Get worker specific resources
    int thisWorker = workerId++;  // assign worker ID (atomic)
    SystemDynamicsBase& systemDynamics = *systemDynamicsPtr_[thisWorker];
    CostFunctionBase& costFunction = *costFunctionPtr_[thisWorker];
    ConstraintBase* constraintPtr = constraintPtr_[thisWorker].get();
    PerformanceIndex workerPerformance;  // Accumulate performance in local variable
    const bool project = settings_.projectStateInputEqualityConstraints;

    int i = timeIndex++;
    while (i < N) {
      auto result =
          multiple_shooting::setupIntermediateNode(systemDynamics, sensitivityDiscretizer_, costFunction, constraintPtr, penaltyPtr_.get(),
                                                   project, time[i], time[i + 1] - time[i], x[i], x[i + 1], u[i]);
      workerPerformance += result.performance;
      dynamics_[i] = std::move(result.dynamics);
      cost_[i] = std::move(result.cost);
      constraints_[i] = std::move(result.constraints);
      i = timeIndex++;
    }

    if (i == N) {  // Only one worker will execute this
      auto result = multiple_shooting::setupTerminalNode(terminalCostFunctionPtr_.get(), constraintPtr, time[N], x[N]);
      workerPerformance += result.performance;
      cost_[i] = std::move(result.cost);
      constraints_[i] = std::move(result.constraints);
    }

    performance[thisWorker] = workerPerformance;
  };
  runParallel(parallelTask);

  // Account for init state in performance
  performance.front().stateEqConstraintISE += (initState - x.front()).squaredNorm();

  // determine sizes
  for (int i = 0; i < N; i++) {
    if (constraintPtr_.front() != nullptr) {
      if (settings_.projectStateInputEqualityConstraints) {
        ocpSize.numInputs[i] = constraints_[i].dfdu.cols();  // obtain size of u_tilde from constraint projection.
      } else {
        ocpSize.numIneqConstraints[i] = constraints_[i].f.rows();  // Declare as general inequalities
      }
    }
  }

  // Prepare solver size
  hpipmInterface_.resize(std::move(ocpSize));

  // Sum performance of the threads
  PerformanceIndex totalPerformance = std::accumulate(std::next(performance.begin()), performance.end(), performance.front());
  totalPerformance.merit = totalPerformance.totalCost + totalPerformance.inequalityConstraintPenalty;
  return totalPerformance;
}

PerformanceIndex MultipleShootingSolver::computePerformance(const scalar_array_t& time, const vector_t& initState, const vector_array_t& x,
                                                            const vector_array_t& u) {
  // Problem horizon
  const int N = static_cast<int>(time.size()) - 1;

  std::vector<PerformanceIndex> performance(settings_.nThreads, PerformanceIndex());
  std::atomic_int workerId{0};
  std::atomic_int timeIndex{0};
  auto parallelTask = [&](int) {
    // Get worker specific resources
    int thisWorker = workerId++;  // assign worker ID (atomic)
    SystemDynamicsBase& systemDynamics = *systemDynamicsPtr_[thisWorker];
    CostFunctionBase& costFunction = *costFunctionPtr_[thisWorker];
    ConstraintBase* constraintPtr = constraintPtr_[thisWorker].get();
    PerformanceIndex workerPerformance;  // Accumulate performance in local variable

    int i = timeIndex++;
    while (i < N) {
      workerPerformance +=
          multiple_shooting::computeIntermediatePerformance(systemDynamics, discretizer_, costFunction, constraintPtr, penaltyPtr_.get(),
                                                            time[i], time[i + 1] - time[i], x[i], x[i + 1], u[i]);
      i = timeIndex++;
    }

    if (i == N && terminalCostFunctionPtr_) {  // Only one worker will execute this
      workerPerformance += multiple_shooting::computeTerminalPerformance(terminalCostFunctionPtr_.get(), constraintPtr, time[N], x[N]);
    }

    performance[thisWorker] = workerPerformance;
  };
  runParallel(parallelTask);

  // Account for init state in performance
  performance.front().stateEqConstraintISE += (initState - x.front()).squaredNorm();

  // Sum performance of the threads
  PerformanceIndex totalPerformance = std::accumulate(std::next(performance.begin()), performance.end(), performance.front());
  totalPerformance.merit = totalPerformance.totalCost + totalPerformance.inequalityConstraintPenalty;
  return totalPerformance;
}

scalar_t MultipleShootingSolver::trajectoryNorm(const vector_array_t& v) {
  scalar_t norm = 0.0;
  for (const auto& vi : v) {
    norm += vi.squaredNorm();
  }
  return std::sqrt(norm);
}

bool MultipleShootingSolver::takeStep(const PerformanceIndex& baseline, const scalar_array_t& timeDiscretization, const vector_t& initState,
                                      const vector_array_t& dx, const vector_array_t& du, vector_array_t& x, vector_array_t& u) {
  /*
   * Filter linesearch based on:
   * "On the implementation of an interior-point filter line-search algorithm for large-scale nonlinear programming"
   * https://link.springer.com/article/10.1007/s10107-004-0559-y
   */
  if (settings_.printLinesearch) {
    std::cerr << std::setprecision(9) << std::fixed;
    std::cerr << "\n=== Linesearch ===\n";
    std::cerr << "Baseline:\n";
    std::cerr << "\tMerit: " << baseline.merit << "\t DynamicsISE: " << baseline.stateEqConstraintISE
              << "\t StateInputISE: " << baseline.stateInputEqConstraintISE << "\t IneqISE: " << baseline.inequalityConstraintISE
              << "\t Penalty: " << baseline.inequalityConstraintPenalty << "\n";
  }

  // Some settings
  const scalar_t alpha_decay = settings_.alpha_decay;
  const scalar_t alpha_min = settings_.alpha_min;
  const scalar_t gamma_c = settings_.gamma_c;
  const scalar_t g_max = settings_.g_max;
  const scalar_t g_min = settings_.g_min;
  const scalar_t costTol = settings_.costTol;

  const int N = static_cast<int>(timeDiscretization.size()) - 1;
  const scalar_t baselineConstraintViolation =
      std::sqrt(baseline.stateEqConstraintISE + baseline.stateInputEqConstraintISE + baseline.inequalityConstraintISE);

  // Update norm
  const scalar_t deltaUnorm = trajectoryNorm(du);
  const scalar_t deltaXnorm = trajectoryNorm(dx);

  scalar_t alpha = 1.0;
  vector_array_t xNew(x.size());
  vector_array_t uNew(u.size());
  while (alpha > alpha_min) {
    // Compute step
    for (int i = 0; i < N; i++) {
      uNew[i] = u[i] + alpha * du[i];
    }
    for (int i = 0; i < (N + 1); i++) {
      xNew[i] = x[i] + alpha * dx[i];
    }

    // Compute cost and constraints
    const PerformanceIndex performanceNew = computePerformance(timeDiscretization, initState, xNew, uNew);
    const scalar_t newConstraintViolation =
        std::sqrt(performanceNew.stateEqConstraintISE + performanceNew.stateInputEqConstraintISE + performanceNew.inequalityConstraintISE);

    bool stepAccepted = [&]() {
      if (newConstraintViolation > g_max) {
        return false;
      } else if (newConstraintViolation < g_min) {
        // With low violation only care about cost, reference paper implements here armijo condition
        return (performanceNew.merit < baseline.merit);
      } else {
        // Medium violation: either merit or constraints decrease (with small gamma_c mixing of old constraints)
        return performanceNew.merit < (baseline.merit - gamma_c * baselineConstraintViolation) ||
               newConstraintViolation < ((1.0 - gamma_c) * baselineConstraintViolation);
      }
    }();

    if (settings_.printLinesearch) {
      std::cerr << "Stepsize = " << alpha << (stepAccepted ? std::string{" (Accepted)"} : std::string{" (Rejected)"}) << "\n";
      std::cerr << "|dx| = " << alpha * deltaXnorm << "\t|du| = " << alpha * deltaUnorm << "\n";
      std::cerr << "\tMerit: " << performanceNew.merit << "\t DynamicsISE: " << performanceNew.stateEqConstraintISE
                << "\t StateInputISE: " << performanceNew.stateInputEqConstraintISE
                << "\t IneqISE: " << performanceNew.inequalityConstraintISE << "\t Penalty: " << performanceNew.inequalityConstraintPenalty
                << "\n";
    }

    // Exit conditions
    const bool stepSizeBelowTol = alpha * deltaUnorm < settings_.deltaTol && alpha * deltaXnorm < settings_.deltaTol;
    // Return if step accepted
    if (stepAccepted) {
      x = std::move(xNew);
      u = std::move(uNew);
      const bool improvementBelowTol = std::abs(baseline.merit - performanceNew.merit) < costTol && newConstraintViolation < g_min;
      return stepSizeBelowTol || improvementBelowTol;
    }
    // Return if steps get too small without being accepted
    if (stepSizeBelowTol) {
      if (settings_.printLinesearch) {
        std::cerr << "Stepsize is smaller than provided deltaTol -> converged \n";
      }
      return true;
    }
    // Try smaller step
    alpha *= alpha_decay;
  }

  return true;  // Alpha_min reached and no improvement found -> Converged
}

}  // namespace ocs2