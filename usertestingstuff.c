#include "types.h"
#include "stat.h"
#include "user.h"
#include "fs.h"

int
main(int argc, char *argv[])
{
    int frames[100];
    int pids[100];
    dump_physmem(frames, pids, 100);
    for (int i = 0; i < 100; i++) {
        printf(1, "Frame: %d | Pid: %d\n", frames[i], pids[i]);
    }
  exit();
}
