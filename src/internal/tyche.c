// Changes to accomodate running on top of Tyche
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <errno.h>
#include <stdint.h>
#include <limits.h>
#include "unistd.h"
#include "stdio.h"
#include "tyche.h"
#include "stdlib.h"
#include "string.h"
#include "syscall.h"

enum tyche_test_state {
    TTS_INIT,
    TTS_START,
    TTS_DONE,
} tyche_test_state;

int connection_accepted = 0;
int connection_selected = 0;
enum tyche_test_state state = TTS_INIT;

void tyche_debug() {
    /* printf("Tyche Debug :)\n"); */
}

pid_t tyche_getpid() {
    return 1;
}

int tyche_isatty(int fd) {
    return 1;
}

char *tyche_getcwd(char *buf, size_t size) {
    char *pwd = "/tmp/tyche-redis";
    strncpy(buf, pwd, size);
    return strdup(pwd);
}

int tyche_gettimeofday(struct timeval *restrict tv, void *restrict tz) {
    tv->tv_sec = 0;
    tv->tv_usec = 0;
    return 0;
}

int tyche_socket() {
    // We only support a single socket for now
    return TYCHE_SOCKET_FD;
}

int tyche_setsockopt(int fd) {
    printf("setsockopt: %d\n", fd);
    // Ignore all options
    return 0;
}

int tyche_bind(int fd) {
    // Just say it succeeded
    return 0;
}

int tyche_listen(int fd) {
    // Just say it succeeded
    return 0;
}

int tyche_accept(int fd) {
    if (!connection_accepted) {
        printf("Accepting connection\n");
        connection_accepted = 1;
        return TYCHE_CONNECTION_FD;
    } else {
        errno = EAGAIN;
        return -1;
    }
}

int tyche_fcntl(int fd, int flags) {
    if (fd == TYCHE_CONNECTION_FD) {
        if (flags == F_GETFL) {
            printf("  F_GETFL\n");
            return  0x2; // Access rights for the connection
        } else if (flags == F_SETFL) {
            printf("  F_SETFL\n");
            return 0;
        } else if (flags == F_GETFD) {
            printf("  F_GETFD\n");
            return 0;
        } else if (flags == F_SETFD) {
            printf("  F_SETFD\n");
            return 0;
        }
    }
    return 0;
}

int tyche_select(int n, fd_set *restrict rfds, fd_set *restrict wfds) {
    printf("Tyche select\n");

    printf("Read set:\n");
    for (int i = 0; i < 32; i++) {
        if (FD_ISSET(i, rfds)) {
            printf("  %d\n", i);
        }
    }
    printf("Write set:\n");
    for (int i = 0; i < 32; i++) {
        if (FD_ISSET(i, wfds)) {
            printf("  %d\n", i);
        }
    }

    // Clear all bits
    FD_ZERO(rfds);
    FD_ZERO(wfds);

    if (!connection_selected) {
        // Set the bit for the Tyche socket
        FD_SET(TYCHE_SOCKET_FD, rfds);
        connection_selected = 1;
    } else {
        switch (state) {
            case TTS_INIT:
                FD_SET(TYCHE_CONNECTION_FD, rfds);
                state = TTS_START;
                break;
            default:
                printf("Done testing, blocking on select\n");
                while (1) {
                    exit(0);
                }
                break;
        }
    }

    printf("Returning from select\n");
    /* exit(0); */
    return 1;
}

#define REDIS_CMD_PING "PING\r\n"

size_t tyche_read(int fd, void *buff, size_t count) {
    printf("Tyche read: %d, count: %d\n", fd, count);
    switch (state) {
        case TTS_START:
            char *cmd = REDIS_CMD_PING;
            state = TTS_DONE;
            strcpy(buff, cmd);
            printf("  %s", cmd);
            return strlen(cmd);
        default:
            printf("Done reading\n");
            break;
    }
    return 0;
}

size_t tyche_write(int fd, const void *buf, size_t count) {
    printf("Tyche write:\n  %.*s", count, buf);
    return count;
}

#define UNIT SYSCALL_MMAP2_UNIT
#define OFF_MASK ((-0x2000ULL << (8*sizeof(syscall_arg_t)-1)) | (UNIT-1))

#define PAGE_SIZE (0x1000)
#define NB_PAGES  (800)

typedef struct mem_segment {
    size_t len;
    struct mem_segment *next;
} mem_segment_t;

static char mempool[NB_PAGES * PAGE_SIZE];
static mem_segment_t* mempool_head;
static int mempool_is_init = 0;

static size_t align_page_up(size_t val) {
    return (val + (PAGE_SIZE - 1)) & ~(PAGE_SIZE - 1);
}

static void update_linked_list(mem_segment_t *node, mem_segment_t *prev, size_t len) {
    mem_segment_t *next = node->next;

    // Create a new node if needed
    if (node->len > len) {
        mem_segment_t *new_node = (mem_segment_t *)(((char *)node) + len);
        new_node->len = node->len - len;
        new_node->next = node->next;
        next = new_node;
    }

    if (prev != NULL) {
        prev->next = next;
    } else {
        mempool_head = next;
    }
}

static void *alloc_segment(size_t len) {
    // Initialize mempool if not already done
    if (!mempool_is_init) {
        mem_segment_t *head = (mem_segment_t *)mempool;
        head->len = NB_PAGES * PAGE_SIZE;
        head->next = NULL;
        mempool_head = head;
        mempool_is_init = 1;
    }

    // Align size to next page size multiple
    len = align_page_up(len);

    mem_segment_t *node = mempool_head;
    mem_segment_t *prev = NULL;
    while (node != NULL) {
        if (node->len >= len) {
            // Node is big enough, allocating memory
            update_linked_list(node, prev, len);
            /* printf("mmap segment [0x%lx, 0x%lx]\n", (size_t)node, (size_t)node + len); */
            return (void*)node;
        }

        // Else move on to next node
        prev = node;
        node = node->next;
    }

    // Running out of space!
    printf("Running out of mmap-able space!!!");
    return NULL;
}

void *tyche_mmap(void *start, size_t len, int prot, int flags, int fd, off_t off) {
    // We just ignore PROT_NONE as it is used only for guard pages
    if (prot == PROT_NONE) {
        return start;
    }

    // Print a warning if we are mapping a file, this is not supported!
    if (fd != -1) {
        printf("ERROR: mmap-ing a file!\n");
        return NULL;
    }

    return alloc_segment(len);
}

int tyche_munmap(void *start, size_t len) {
    len = align_page_up(len);

    if (align_page_up((size_t)start) != (size_t)start) {
        printf("Invalid unmap: 0x%lx is not page-aligned\n", (size_t)start);
        return 0;
    }

    // For now munmap is left unimplemented, this wastes a lot of memory though...
    return 0;

    mem_segment_t *node = mempool_head;
    mem_segment_t *prev = NULL;
    while (node != NULL && (size_t)node < (size_t)start) {
        prev = node;
        node = node->next;
    }

    // TODO: insert a new node here.
    return 0;
}

#define BRK_NB_PAGES 20
static char brk_pool[BRK_NB_PAGES * PAGE_SIZE];
static char *brk_cursor;
static int brk_is_init = 0;

size_t tyche_brk(void *end) {
    // Initialize if needed
    if (!brk_is_init) {
        brk_cursor = brk_pool;
        brk_is_init = 1;
    }

    if (end == NULL) {
        return (size_t)brk_cursor;
    }

    if ((size_t)end > (size_t)brk_pool + BRK_NB_PAGES * PAGE_SIZE || (size_t)end < (size_t)brk_pool) {
        printf("Invalid brk!!!\n");
        return -ENOMEM;
    } else {
        brk_cursor = end;
        return (size_t)end;
    }
}
