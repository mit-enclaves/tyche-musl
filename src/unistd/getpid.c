#include <unistd.h>
#include "syscall.h"
#include "stdio.h"
#include "tyche.h"

pid_t getpid(void)
{
    return tyche_getpid();
	return __syscall(SYS_getpid);
}
