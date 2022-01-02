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
//   - Added support for ARM processors
//       - Moved code to open msr device to include/x86.h
//       - Generalized reading and resetting all counters
//   - Fixed some small typos
//   - Changed formatting in case the counter does not have a mask

#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>

#define __USE_GNU
#include <fcntl.h>
#include <sched.h>
#include <errno.h>
#include <string.h>
#include <inttypes.h>

#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/resource.h>

#include <asm/unistd.h>
#include <linux/perf_event.h>

#include "speculator.h"

void
usage_and_quit(char** argv) {
    fprintf(stderr, USAGE_FORMAT, basename(argv[0]));
    exit(EXIT_FAILURE);
}

void
init_result_file(char *output_filename, int is_attacker) {
    FILE *o_fd = NULL;
    struct speculator_monitor_data *data;

    data = &victim_data;

    if (is_attacker) {
        data = &attacker_data;
    }

    o_fd = fopen(output_filename, "w");

#ifdef INTEL
    for (int i = 0; i < 3; ++i)
        fprintf(o_fd, "%s|", intel_fixed_counters[i]);
#endif // INTEL

    for (int i = 0; i < data->free; ++i) {
        if (data->mask[i][0] != '\0')
            fprintf(o_fd, "%s.%s|", data->key[i], data->mask[i]);
        else
            fprintf(o_fd, "%s|", data->key[i]);
    }

    fprintf(o_fd,"\n");
    fclose(o_fd);
}

void
start_process(char *filename,
              int core,
              sem_t *sem,
              char** env,
              char **par) {
    int ret = -1;
    cpu_set_t set;
    struct sched_param param;

    CPU_ZERO(&set);
    CPU_SET(core, &set);
    sched_setaffinity(getpid(), sizeof(cpu_set_t), &set);

    if (!mflag) {
        ret = setpriority(PRIO_PROCESS, 0, -20);

        if (ret != 0) {
            fprintf (stderr, "Impossible set priority to child\n");
            exit(EXIT_FAILURE);
        }

        param.sched_priority = 99;
        ret = sched_setscheduler(0, SCHED_RR, &param);

        if (ret != 0) {
            fprintf (stderr, "Impossible set scheduler RR proprity\n");
            exit(EXIT_FAILURE);
        }
    }
    sem_wait(sem);
    sem_post(sem);

    /* HERE TRY TO START OTHER PROCESS */
    execve(filename, par, env);
}

void
set_counters(int fd, int is_attacker) {
    struct speculator_monitor_data *data;

    data = &victim_data;

    if (is_attacker) {
        data = &attacker_data;
    }

#ifdef INTEL
    // Disable all counters
    write_to_IA32_PERF_GLOBAL_CTRL(fd, 0ull);
    // Initialize Fixed Counters
    write_to_IA32_FIXED_CTR_CTRL(fd, (2ull) | (2ull << 4) | (2ull << 8));
    /* Reset fixed counters */
    for (int i = 0; i < 3; ++i)
        write_to_IA32_FIXED_CTRi(fd, i, 0ull);
#endif // INTEL

#if defined(INTEL) || defined(AMD)
    // select counters
    for (int i = 0; i < data->free; ++i)
        write_perf_event_select(fd, i, data->config[i]);
#endif

    reset_perf_event_counters(fd, data->free);
}

void dump_results(char *output_filename, int fd, int is_attacker) {
    FILE *fp = NULL;
    struct speculator_monitor_data *data;

    data = &victim_data;

    if (is_attacker) {
        data = &attacker_data;
    }

    fp = fopen(output_filename, "a+");

    if (fp == NULL) {
        fprintf(stderr, "Impossible to open the outputfile %s\n", output_filename);
        exit(EXIT_FAILURE);
    }

#ifdef INTEL
    for (int i = 0; i < 3; ++i) {
        data->count_fixed[i] = read_IA32_FIXED_CTRi(fd, i);
        fprintf(fp, "%lld|", data->count_fixed[i]);
    }
#endif // INTEL

    read_perf_event_counters(fd, data->count, data->free);

    for (int i = 0; i < data->free; ++i) {
        if (verbflag) {
            if (data->mask[i][0] != '\0')
                printf("######## %s:%s ##########\n", data->key[i], data->mask[i]);
            else
                printf("######## %s ##########\n", data->key[i]);
            debug_print("Counter full: %s\n", data->config_str[i]);
            debug_print("Counter hex: %llx\n", data->config[i]);
            debug_print("Desc: %s\n", data->desc[i]);
            printf("Result: %" PRIu64 "\n", data->count[i]);
            debug_print("-----------------\n");
        }
        fprintf(fp, "%" PRIu64 "|", data->count[i]);
    }

    fprintf(fp, "\n");

    fclose(fp);
}

void
start_monitor_inline(int victim_pid,
                     int attacker_pid,
                     char* output_filename,
                     char* output_filename_attacker,
                     int fd_victim,
                     int fd_attacker) {
    int status = 0;

    if (!mflag) { // Set counters unless monitor-only mode
        set_counters(fd_victim, 0);
        if (aflag && ATTACKER_CORE != VICTIM_CORE)
            set_counters(fd_attacker, 1);
    }

    if (aflag && !iflag) {
        sem_post(sem_attacker);
        if (dflag) {
            usleep (delay);
        }
        if (sflag) {
            waitpid(attacker_pid, &status, 0);
        }
    }

    sem_post(sem_victim);

    if (sflag) {
        waitpid(victim_pid, &status, 0);
    }

    if (aflag && iflag) {
        if(dflag) {
            usleep(delay);
        }
        sem_post(sem_attacker);
        if (sflag) {
            waitpid(attacker_pid, &status, 0);
        }
    }

    // Waiting for victim to return
    // and dump the counters on the cores
    if (!sflag) {
        waitpid(victim_pid, &status, 0);
    }

    if (!mflag) { // Skip dump result if monitor-only
        dump_results(output_filename, fd_victim, 0);
    }

    if (aflag) {
        if (!sflag) {
            waitpid(attacker_pid, &status, 0);
        }
        if (!mflag) { // Skip dump result if monitor-only
            dump_results(output_filename_attacker, fd_attacker, 1);
        }
    }
}

int
main(int argc, char **argv) {
    int opt = 0;
    cpu_set_t set;
    int index = 0;
    int option_index = 0;
    pid_t victim_pid = 0;
    char *env_home = NULL;
    int fd_victim = -1;
    char *env_build = NULL;
    pid_t attacker_pid = 0;
    int fd_attacker = -1;
    char *env_install = NULL;
    int repeat = DEFAULT_REPEAT;
    char *config_filename = NULL;
    char *output_filename = NULL;
    char *victim_filename = NULL;
    char *attacker_filename = NULL;
    char *output_filename_attacker = NULL;

    // retrive environment variables (if any)
    env_home = getenv("SPEC_H");
    if (env_home == NULL)
        debug_print("WARNING: SPEC_H not set\n");
    else
        debug_print("SPEC_H set to %s\n", env_home);

    env_build = getenv("SPEC_B");
    if (env_build == NULL)
        debug_print("WARNING: SPEC_B not set\n");
    else
        debug_print("SPEC_B set to %s\n", env_build);

    env_install = getenv("SPEC_I");
    if (env_install == NULL)
        debug_print("WARNING: SPEC_I not set\n");
    else
        debug_print("SPEC_I set to %s\n", env_install);

#ifdef INTEL
    debug_print("CPU: Intel detected\n");
    write_perf_event_select = write_to_IA32_PERFEVTSELi;
    read_perf_event_counters = read_IA32_PMCs;
    reset_perf_event_counters = reset_IA32_PMCs;
#endif // INTEL

#ifdef AMD
    debug_print("CPU: AMD detected\n");
    write_perf_event_select = write_to_AMD_PERFEVTSELi;
    read_perf_event_counters = read_AMD_PMCs;
    reset_perf_event_counters = reset_AMD_PMCs;
#endif // AMD

#ifdef ARM
    debug_print("CPU: ARM detected\n");
    read_perf_event_counters = read_ARM_PMCs;
    reset_perf_event_counters = reset_ARM_PMCs;
#endif // ARM

    // Set process to run on the first core to don't interfere on child process
    CPU_ZERO(&set);
    CPU_SET(FATHER_CORE, &set);
    sched_setaffinity(getpid(), sizeof(cpu_set_t), &set);

    /* Reading out params */
    while ((opt = getopt_long(argc, argv, "hv:a:c:o:qr:id:sm",
                long_options, &option_index)) != -1) {
        switch (opt) {
            case 'h':
                hflag = 1;
                break;
            case 'v':
                vflag = 1;
                victim_filename = get_complete_path(env_install, optarg);
                break;
            case 'a':
                aflag = 1;
                attacker_filename = get_complete_path(env_install, optarg);
                break;
            case 'c':
                cflag = 1;
                config_filename = get_complete_path(env_install, optarg);
                break;
            case 'r':
                rflag = 1;
                repeat = atoi(optarg);
                break;
            case 'o':
                oflag = 1;
                output_filename = get_complete_path(env_install, optarg);
                break;
            case 'i':
                iflag = 1;
                break;
            case 's':
                sflag = 1;
                break;
            case 'd':
                if (atoi(optarg) > 0) {
                    dflag = 1;
                    delay = atoi(optarg);
                }
                else {
                    fprintf(stderr, "Delay must be positive\n");
                    usage_and_quit(argv);
                }
                break;
            case 'm':
                mflag = 1;
                break;
            case 0: // venv
                aenvflag = 1;
                victim_preload[0] = optarg;
                index = 1;
                while (optind < argc && argv[optind][0] != '-') {
                    victim_preload[index] = argv[optind];
                    optind++;
                    index++;
                }
                break;
            case 1: // aenv
                venvflag = 1;
                index = 1;
                attacker_preload[0] = optarg;
                while (optind < argc && argv[optind][0] != '-') {
                    attacker_preload[index] = argv[optind];
                    optind++;
                    index++;
                }
                break;
            case 2: //vpar
                vparflag = 1;
                index = 2;
                victim_parameters[1] = optarg;
                while (optind < argc && argv[optind][0] != '-') {
                    victim_parameters[index] = argv[optind];
                    optind++;
                    index++;
                }
                break;
            case 3: //apar
                aparflag = 2;
                index = 2;
                attacker_parameters[1] = optarg;
                while (optind < argc && argv[optind][0] != '-') {
                    attacker_parameters[index] = argv[optind];
                    optind++;
                    index++;
                }
                break;
            case 4: //verbose
                verbflag = 1;
                break;
            case '?':
                fprintf(stderr, "Unknown option %c\n", optopt);
                usage_and_quit(argv);
                break;
            case ':':
                fprintf(stderr, "Missing option %c\n", optopt);
                break;
            default:
                usage_and_quit(argv);
        }
    }

    if (hflag || !vflag) {
        usage_and_quit(argv);
    }

    victim_parameters[0] = victim_filename;

    if (aflag) {
        attacker_parameters[0] = attacker_filename;
    }

    if (!aflag && iflag) {
        fprintf(stderr, "Invert option can be specified only in attack/victim mode\n");
        usage_and_quit(argv);
    }

    if (!aflag && dflag) {
        fprintf(stderr, "Delay can be specified only in attack/victim mode\n");
        usage_and_quit(argv);
    }

    if(!mflag && geteuid() != 0) {
        fprintf (stderr, "This program must run as root " \
                         "to be able to read the performance counters\n");
        exit(EXIT_FAILURE);
    }

    if (access(victim_filename, F_OK) == -1) {
        fprintf(stderr, "Error: victim file %s not found!\n", victim_filename);
        usage_and_quit(argv);
    }

    if (access(victim_filename, X_OK) == -1) {
        fprintf(stderr, "Error: victim file %s not executable!\n", victim_filename);
        usage_and_quit(argv);
    }

    if (!cflag) {
        config_filename = get_complete_path(env_install, DEFAULT_CONF_NAME);
    }

    if (!oflag) {
        output_filename = get_complete_path(env_install, DEFAULT_OUTPUT_NAME);
    }

    if (aflag) {
        debug_print("Running in attack/victim mode\n");
    }
    else {
        debug_print("Running in snippet mode\n");
    }

    sem_victim = mmap(NULL, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    sem_attacker = mmap(NULL, sizeof(sem_t), PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    sem_init(sem_victim, 1, 1);
    sem_init(sem_attacker, 1, 1);

    if (!mflag) { // Skip configuration of counters if monitor-only
        parse_config(config_filename);

        recursive_mkdir(output_filename);

        init_result_file(output_filename, 0);

        if (aflag) {
            //TODO change from FILENAME_LENGTH to strlen(output_filename)
            output_filename_attacker = (char *) malloc(sizeof(char) * FILENAME_LENGTH);
            snprintf (output_filename_attacker, FILENAME_LENGTH+1, "%s.attacker", output_filename);
            init_result_file(output_filename_attacker, 1);
        }

#ifdef ARM
        // Setting up the performance counters for the victim
        struct speculator_monitor_data *data;
        data = &victim_data;
        for (int i = 0; i < data->free; ++i) 
            fd_victim = ARM_setup_perf_counter(fd_victim, data->config[i], VICTIM_CORE);

        // Setting up the performance counters for the attacker
        // if running in attacker/victim mode
        if (aflag) {
            data = &attacker_data;
            for (int i = 0; i < data->free; ++i) 
                fd_attacker = ARM_setup_perf_counter(fd_attacker, data->config[i], ATTACKER_CORE);
        }

        // Placing the file descriptors in the parameters so the victim can
        // retrieve the file descriptor to start and stop the counters.
        char fd_victim_str[20];
        char fd_attacker_str[20];

        sprintf(fd_victim_str, "group_fd=%d", fd_victim);
        sprintf(fd_attacker_str, "group_fd=%d", fd_attacker);

        index = -1;
        while (victim_preload[++index] != NULL);
        printf("Index of fd: %d\n", index);
        victim_preload[index] = fd_victim_str;

        index = -1;
        while (attacker_preload[++index] != NULL);
        attacker_preload[index] = fd_attacker_str;
#endif

#if defined(INTEL) || defined(AMD)
        // Opening cpu msr file for the victim cpu
        fd_victim = get_msr_fd(VICTIM_CORE);

        // Opening cpu msr file for the attacker cpu
        // if running in attacker/victim mode
        if (aflag) {
            fd_attacker = get_msr_fd(ATTACKER_CORE);
        }
#endif
    }
    // Repeat X times experiment
    for (int i = 0; i < repeat; ++i) {
#ifdef DUMMY
        int status;
        pid_t tmp_pid;

        tmp_pid = fork();

        if(tmp_pid == 0) {
            debug_print("Starting dummy %s on victim core\n", DUMMY_NAME);
            start_process (DUMMY_NAME, VICTIM_CORE, sem_victim, NULL, NULL);
        }
        else {
            waitpid(tmp_pid, &status, 0);
        }

        if (aflag) {
            tmp_pid = fork();

            if(tmp_pid == 0) {
                debug_print("Starting dummy on attacker core\n");
                start_process (DUMMY_NAME, ATTACKER_CORE, sem_attacker, NULL, NULL);
            }
            else {
                waitpid(tmp_pid, &status, 0);
            }
        }
#endif // DUMMY

        sem_wait(sem_victim);

        if (aflag)
            sem_wait(sem_attacker);

        if (aflag) {
            // STARTING ATTACKER
            attacker_pid = fork();

            if (attacker_pid < 0)
                exit(EXIT_FAILURE);

            if (attacker_pid == 0)
                start_process(attacker_filename, ATTACKER_CORE, sem_attacker, attacker_preload, attacker_parameters);
        }

        // STARTING VICTIM
        victim_pid = fork();

        if (victim_pid < 0)
            exit(EXIT_FAILURE);

        if (victim_pid == 0)
            start_process(victim_filename, VICTIM_CORE, sem_victim,  victim_preload, victim_parameters);

        start_monitor_inline(victim_pid, attacker_pid, output_filename,
                    output_filename_attacker, fd_victim, fd_attacker);
    }

    if (!mflag) { // Skip change ownership if in monitor-only mode
        update_file_owner(output_filename);
        if (aflag)
            update_file_owner(output_filename_attacker);
    }

    //clean-up victim
    for (int i = 0; i < victim_data.free; ++i) {
        free(victim_data.desc[i]);
        free(victim_data.key[i]);
        free(victim_data.mask[i]);
        free(victim_data.config_str[i]);
    }

    //clean-up attacker
    for (int i = 0; i < attacker_data.free; ++i) {
        free(attacker_data.desc[i]);
        free(attacker_data.key[i]);
        free(attacker_data.mask[i]);
        free(attacker_data.config_str[i]);
    }

#ifdef INTEL
    if (!mflag) { // Skip re-enabling if in monitor-only mode
        // RE-ENABLE ALL COUNTERS
        write_to_IA32_PERF_GLOBAL_CTRL(fd_victim, 15ull | (7ull << 32));
        if (aflag)
            write_to_IA32_PERF_GLOBAL_CTRL(fd_attacker, 15ull | (7ull << 32));
    }
#endif //INTEL

    close(fd_victim);

    if (aflag) {
        close(fd_attacker);
        free(output_filename_attacker);
    }

    free(victim_filename);
    free(config_filename);
    free(output_filename);
    free(attacker_filename);

    sem_destroy(sem_victim);
    sem_destroy(sem_attacker);
    munmap(sem_victim, sizeof(sem_t));
    munmap(sem_attacker, sizeof(sem_t));
}
