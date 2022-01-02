// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ARM_H
#define ARM_H

#include <linux/perf_event.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MAX_NB_COUNTERS 8

struct read_format {
    uint64_t nr;
    uint64_t values[MAX_NB_COUNTERS];
} perf_data;

/**
 * Setup a new performance counter with given config value on the given cpu.
 * If the group file descriptor is -1, this counter will be the group leader.
 *
 * @param group_fd The group file descriptor. If this is -1, this counter will
 *                 become the group leader
 * @param config The config value for which to set up a performance counter
 * @param cpu The cpu on which the performance counter should be active
 *
 * @return The group file descriptor.
 */
int ARM_setup_perf_counter(int group_fd, uint64_t config, int cpu) {
    int fd;
    struct perf_event_attr attr;

    memset(&attr, 0, sizeof(attr));

    // The first event, i.e. when there is no group fd yet, will be the group
    // leader.
    int is_leader = group_fd == -1;

    attr.size = sizeof(attr);

    attr.type = PERF_TYPE_RAW;  // Implementation-specific event
    attr.config = config;       // Specify which implementation-specific event

    attr.disabled = is_leader ? 1 : 0;     // Disable group leader initially
    attr.exclude_kernel = 1;               // Exclude events in kernel space
    attr.exclude_hv = 1;                   // Exclude events in hypervisor
    attr.read_format = PERF_FORMAT_GROUP;  // Read all counters at once

    fd = perf_event_open(&attr, -1, cpu, group_fd, 0);

    if (fd == -1) {
      perror("Impossible to open performance counter");
      exit(EXIT_FAILURE);
    }

    // Return the group file descriptor.
    return is_leader ? fd : group_fd;
}

/**
 * Reset all performance counters in the group with the given grour_fd.
 *
 * @param group_fd The group file descriptor of the group for which to reset all
 *                 counters.
 * @param nb_counters The number of counters in this group.
 *                    This parameter is ignored.
 */
void reset_ARM_PMCs(int group_fd, int nb_counters) {
    int rv = 0;

    rv = ioctl(group_fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);

    if (rv == -1) {
        perror("Impossible to reset the ARM performance counters");
        exit(EXIT_FAILURE);
    }
}

/**
 * Read all performance counters beloning to the given group fd into the
 * destination array.
 *
 * @param group_fd The group file descriptor for which to read the counters
 * @param dest The destination array into which to write the results
 * @param nb_counters The total number of counters in the group
 */
void read_ARM_PMCs(int group_fd, uint64_t *dest, int nb_counters) {
    int rv = 0;

    rv = read(group_fd, &perf_data, (nb_counters + 1) * sizeof(uint64_t));

    if (rv < sizeof(uint64_t)) {
        perror("Impossible to read performance counter");
        exit(EXIT_FAILURE);
    }

    // Check that the number of counters read matches the total number of counters
    if (perf_data.nr != nb_counters) {
        fprintf(stderr, "Number of read counters does not match total number of ");
        fprintf(stderr, "counters\n");
        fprintf(stderr, "Read %ld but expected %d\n", perf_data.nr, nb_counters);
        exit(EXIT_FAILURE);
    }

    // Copy the values into the destination buffer
    memcpy(dest, perf_data.values, nb_counters * sizeof(uint64_t));
}


#endif // ARM_H
