// Copyright 2021 IBM Corporation
//
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
//
// 2022-01-02: Modified by Jesse De Meulemeester.
//   - Added functions to read and reset all counters.

#ifndef INTEL_H
#define INTEL_H

#define FIXED_COUNTERS 3

const char *intel_fixed_counters[] = {"INSTRUCTIONS_RETIRED", "CYCLES", "UNKNOWN"};

void
write_to_IA32_PERF_GLOBAL_CTRL(int fd,
                               uint64_t val) {
    int rv = 0;

    rv = pwrite(fd, &val, sizeof(val), 0x38F);

    if (rv != sizeof(val)) {
        fprintf (stderr, "Impossible to write IA32_PERF_GLOBAL_CTRL\n");
        exit(EXIT_FAILURE);
    }
}

void
write_to_IA32_PMCi(int fd,
                   uint8_t i,
                   uint64_t val) {
    int rv = 0;

    rv = pwrite(fd, &val, sizeof(val), 0xC1 + i);

    if (rv != sizeof(val)) {
        fprintf (stderr, "Impossible to write IA32_PMCi\n");
        exit(EXIT_FAILURE);
    }
}

void
reset_IA32_PMCs(int fd, int nb_counters) {
    for (int i = 0; i < nb_counters; ++i)
        write_to_IA32_PMCi(fd, i, 0ull);
}

uint64_t
read_IA32_PMCi(int fd,
               uint8_t i) {
    int rv = 0;
    uint64_t ret = -1;

    rv = pread(fd, &ret, sizeof(ret), 0xC1 + i);

    if (rv != sizeof(ret)) {
        fprintf (stderr, "Impossible to read IA32_PMCi\n");
        exit(EXIT_FAILURE);
    }

    return ret;
}

/**
 * Read all performance counters into the destination array.
 *
 * @param fd The file descriptor
 * @param dest The destination array into which to write the results
 * @param nb_counters The total number of counters to read
 */
void
read_IA32_PMCs(int fd, uint64_t *dest, int nb_counters) {
  for (int i = 0; i < nb_counters; ++i) {
    dest[i] = read_IA32_PMCi(fd, i);
  }
}

void
write_to_IA32_FIXED_CTRi(int fd,
                         int i,
                         uint64_t val) {
    int rv;

    rv = pwrite(fd, &val, sizeof(val), 0x309 + i);

    if (rv != sizeof(val)) {
        fprintf (stderr, "Impossible to write IA32_PMCi\n");
        exit(EXIT_FAILURE);
    }
}

void
write_to_IA32_FIXED_CTR_CTRL(int fd,
                             uint64_t val) {
    int rv;

    rv = pwrite(fd, &val, sizeof(val), 0x38D);

    if (rv != sizeof(val)) {
        fprintf (stderr, "Impossible to write IA32_PMCi\n");
        exit(EXIT_FAILURE);
    }
}

uint64_t
read_IA32_FIXED_CTRi(int fd,
                     int i) {
    int rv;
    uint64_t ret = -1;

    rv = pread(fd, &ret, sizeof(ret), 0x309 + i);

    if (rv != sizeof(ret)) {
        fprintf (stderr, "Impossible to read IA32_PMCi\n");
        exit(EXIT_FAILURE);
    }
    return ret;
}

void
write_to_IA32_PERFEVTSELi(int fd,
                          uint8_t i,
                          uint64_t val) {
    int rv;

    rv = pwrite(fd, &val, sizeof(val), 0x186 + i);

    if (rv != sizeof(val)) {
        fprintf (stderr, "Impossible to write IA32_PERFEVTSELi\n");
        exit (EXIT_FAILURE);
    }
}

#endif //INTEL_H
