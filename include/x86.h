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
// 2022-01-02: Created by Jesse De Meulemeester.
//   - Moved x86 specific functions and structures here from speculator.h
//     and speculator_monitor.c

#ifndef X86_H
#define X86_H

#define MSR_FORMAT "/dev/cpu/%ld/msr"

typedef struct cpuinfo {
    uint32_t eax;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
} cpuinfo;

void cpuid(uint32_t idx, cpuinfo *info) {
    asm volatile("cpuid"
            : "=a" (info->eax), "=b" (info->ebx),
              "=c" (info->ecx), "=d" (info->edx)
            : "a" (idx));
}

int
get_msr_fd(int core) {
    msr_path = (char *) malloc(sizeof(char) * (strlen(MSR_FORMAT)+1));
    snprintf(msr_path, strlen(MSR_FORMAT)+1, MSR_FORMAT, core);

    debug_print("Opening %s device for victim\n", msr_path);

    // Get fd to MSR register
    msr_fd = open(msr_path, O_RDWR | O_CLOEXEC);

    if (msr_fd < 0) {
        fprintf(stderr, "Impossible to open the %s device\n", msr_path);
        free(msr_path);
        exit(EXIT_FAILURE);
    }

    free(msr_path);

    return msr_fd;
}

#endif // X86_H

