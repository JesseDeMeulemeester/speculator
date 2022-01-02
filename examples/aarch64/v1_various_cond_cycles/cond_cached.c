#include <linux/perf_event.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#define NOP asm volatile("nop");
#define NOP2 NOP NOP
#define NOP4 NOP2 NOP2
#define NOP8 NOP4 NOP4
#define NOP16 NOP8 NOP8
#define NOP32 NOP16 NOP16
#define NOP64 NOP32 NOP32
#define NOP128 NOP64 NOP64

int main(int argc, char *argv[], char *envp[]) {
  int fd = atoi(getenv("group_fd"));

  int warmup_cnt = 0;

  // ##### DATA STARTS HERE #####


  // #####  DATA ENDS HERE  #####

loop:
  ioctl(fd, PERF_EVENT_IOC_RESET, PERF_IOC_FLAG_GROUP);
  ioctl(fd, PERF_EVENT_IOC_ENABLE, PERF_IOC_FLAG_GROUP);

  NOP128

  if (warmup_cnt == 12)
    goto skip;

  // ##### SNIPPET STARTS HERE #####


  // #####  SNIPPET ENDS HERE  #####  

  asm volatile("fmov s1, #1.00000000");  // Marker

skip:
  NOP128

  ioctl(fd, PERF_EVENT_IOC_DISABLE, PERF_IOC_FLAG_GROUP);

  if (++warmup_cnt < 13)
    goto loop;

end:
  return 0;
}
