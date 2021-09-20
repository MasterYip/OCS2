import torch
import numpy as np

from ocs2_mpcnet import size_array, scalar_array, vector_array, SystemObservation, SystemObservationArray,\
    ModeSchedule, ModeScheduleArray, TargetTrajectories, TargetTrajectoriesArray


def bdot(bv1, bv2):
    # batched dot product
    # TODO(areske): find the best implementation
    return torch.sum(torch.mul(bv1, bv2), dim=1)


def bmv(bm, bv):
    # batched matrix-vector product
    # TODO(areske): find the best implementation
    return torch.matmul(bm, bv.unsqueeze(dim=2)).squeeze(dim=2)


def bmm(bm1, bm2):
    # batched matrix-matrix product
    # TODO(areske): find the best implementation
    return torch.matmul(bm1, bm2)


def get_size_array(data):
    my_size_array = size_array()
    my_size_array.resize(len(data))
    for i in range(len(data)):
        my_size_array[i] = data[i]
    return my_size_array


def get_scalar_array(data):
    my_scalar_array = scalar_array()
    my_scalar_array.resize(len(data))
    for i in range(len(data)):
        my_scalar_array[i] = data[i]
    return my_scalar_array


def get_vector_array(data):
    my_vector_array = vector_array()
    my_vector_array.resize(len(data))
    for i in range(len(data)):
        my_vector_array[i] = data[i]
    return my_vector_array


def get_system_observation(mode, time, state, input):
    system_observation = SystemObservation()
    system_observation.mode = mode
    system_observation.time = time
    system_observation.state = state
    system_observation.input = input
    return system_observation


def get_system_observation_array(length):
    system_observation_array = SystemObservationArray()
    system_observation_array.resize(length)
    return system_observation_array


def get_target_trajectories(time_trajectory, state_trajectory, input_trajectory):
    time_trajectory_array = get_scalar_array(time_trajectory)
    state_trajectory_array = get_vector_array(state_trajectory)
    input_trajectory_array = get_vector_array(input_trajectory)
    return TargetTrajectories(time_trajectory_array, state_trajectory_array, input_trajectory_array)


def get_target_trajectories_array(length):
    target_trajectories_array = TargetTrajectoriesArray()
    target_trajectories_array.resize(length)
    return target_trajectories_array


def get_mode_schedule(event_times, mode_sequence):
    event_times_array = get_scalar_array(event_times)
    mode_sequence_array = get_size_array(mode_sequence)
    return ModeSchedule(event_times_array, mode_sequence_array)


def get_mode_schedule_array(length):
    mode_schedule_array = ModeScheduleArray()
    mode_schedule_array.resize(length)
    return mode_schedule_array


def get_event_times_and_mode_sequence(default_mode, duration, event_times_template, mode_sequence_template):
    gait_cycle_duration = event_times_template[-1]
    num_gait_cycles = int(np.floor(duration / gait_cycle_duration))
    event_times = np.array([0.0], dtype=np.float64)
    mode_sequence = np.array([default_mode], dtype=np.uintp)
    for _ in range(num_gait_cycles):
        event_times = np.append(event_times, event_times[-1] * np.ones(len(event_times_template)) + event_times_template)
        mode_sequence = np.append(mode_sequence, mode_sequence_template)
    mode_sequence = np.append(mode_sequence, np.array([default_mode], dtype=np.uintp))
    return event_times, mode_sequence
