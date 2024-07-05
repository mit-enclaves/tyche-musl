#include <unistd.h>
#include "syscall.h"
#include "tyche.h"

ssize_t read(int fd, void *buf, size_t count)
{
    return syscall_cp(SYS_read, fd, buf, count);
}
