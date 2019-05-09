#define _XOPEN_SOURCE 500
#define _GNU_SOURCE
#include <ftw.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <sys/syscall.h>
#include <linux/aio_abi.h>
#include <linux/limits.h>
#include <errno.h>
#include <malloc.h>

#define errExit(msg) do { perror(msg); exit(EXIT_FAILURE); } while (0)
#define errMsg(msg)  do { perror(msg); } while (0)

static volatile sig_atomic_t gotSIGQUIT = 0;

static void             /* Handler for SIGQUIT */
quitHandler(int sig)
{
    gotSIGQUIT = 1;
}

#define IO_SIGNAL SIGUSR1   /* Signal used to notify I/O completion */

static void                 /* Handler for I/O completion signal */
aioSigHandler(int sig, siginfo_t *si, void *ucontext)
{
    if (si->si_code == SI_ASYNCIO) {
        //write(STDOUT_FILENO, "I/O completion signal received\n", 31);
    }
}

/* linux aio functions */

static inline int io_setup(unsigned nr, aio_context_t *ctxp) {
	return syscall(__NR_io_setup, nr, ctxp);
}

static inline int io_destroy(aio_context_t ctx) {
	return syscall(__NR_io_destroy, ctx);
}

static inline int io_submit(aio_context_t ctx, long nr, struct iocb **iocbpp) {
	return syscall(__NR_io_submit, ctx, nr, iocbpp);
}

static inline int io_getevents(aio_context_t ctx, long min_nr, long max_nr,
		struct io_event *events, struct timespec *timeout) {
	return syscall(__NR_io_getevents, ctx, min_nr, max_nr, events, timeout);
}

/* Globals */
#define max_size 1024
char orig_names[max_size][max_size];
char copy_names[max_size][max_size];
int orig_len;
char *copy_dir;

aio_context_t read_ctx = 0;
struct iocb *read_cbs[max_size];
struct io_event read_events[max_size];
aio_context_t write_ctx = 0;
struct iocb *write_cbs[max_size];
struct io_event write_events[max_size];

int numReadReq = 0;
int numOpenRead = 0;
int numOpenWrite = 0;

/* Get new aligned buffer for O_DIRECT */
const char * malloc_aligned(size_t size) {
	int align = 4096;
	char * buf = memalign(align * 2, size + align);
	if (buf == NULL)
		errExit("malloc in aligning function");
	return buf;
}

/* Handle directories and files for nftw */
static int dispatch_read(const char *fpath, const struct stat *sb,
            int tflag, struct FTW *ftwbuf) {
	char newpath[max_size] = "";
	strcat(strcat(newpath, copy_dir), fpath + orig_len);
	/* Create directories */
	if (tflag == FTW_D) {
		if (mkdir(newpath, 0777) != 0)
			errExit("mkdir");
	}
	/* Dispatch read for files */
	if (tflag == FTW_F) {
		struct iocb * cb = read_cbs[numReadReq];
		//memset(&cb, 0, sizeof(cb));
		char fullpath[PATH_MAX] = "";
		realpath(fpath, fullpath);
		cb->aio_fildes = open(fullpath, O_RDONLY | O_DIRECT);
		if (cb->aio_fildes < 0)
			errExit("open in read");
		// Initiate readahead as early as possible
		if (readahead(cb->aio_fildes, 0, (size_t) sb->st_size) != 0)
			errExit("readahead");
		cb->aio_lio_opcode = IOCB_CMD_PREAD;
		size_t size = (uint64_t) sb->st_size;
		cb->aio_buf = (uint64_t)malloc_aligned(size);
		cb->aio_offset = (int64_t) 0;
		cb->aio_nbytes = size;
		int ret = io_submit(read_ctx, 1, &cb);
		if (ret != 1) {
			if (ret < 0)
				errExit("io_submit in read");
			else
				errExit("io_submit in read failed");
		}
		strcpy(orig_names[numReadReq], fullpath);
		strcpy(copy_names[numReadReq], newpath);
		++numReadReq;
		++numOpenRead;
	}
	return 0;
}

/* Issue a write request corresponding to a finished read request */
void dispatch_write(struct io_event event) {
	// Get the file descriptor from the event struct
	struct iocb * oldcb;
	oldcb = (struct iocb *)(event.obj);
	int old_fd = oldcb->aio_fildes;
	// Get the old path
	char oldpath[PATH_MAX] = "";
	char proc[1024];
	sprintf(proc, "/proc/self/fd/%d", old_fd);
	if(readlink(proc, oldpath, 1024) < 0)
		errExit("readlink");
	// Find the index of the correct path
	char fullpath[PATH_MAX] = "";
	realpath(oldpath, fullpath);
	int i;
	for (i = 0; i < numReadReq; i++)
		if (strcmp(fullpath, orig_names[i]) == 0)
			break;
	// Done with old fd
	close(old_fd);
	// Modify old cb values
	oldcb->aio_fildes = open(copy_names[i], O_WRONLY | O_CREAT | O_DIRECT, 0666);
	if (oldcb->aio_fildes < 0)
		errExit("open in write");
	// fallocate file to quickly create space
	if (fallocate(oldcb->aio_fildes, 0, 0, (off_t) oldcb->aio_nbytes) != 0)
		errExit("fallocate");
	oldcb->aio_lio_opcode = IOCB_CMD_PWRITE;
	// Dispatch write
	int ret = io_submit(write_ctx, 1, &oldcb);
	if (ret != 1) {
		if (ret < 0)
			errExit("io_submit in write");
		else
			errExit("io_submit in write failed");
	}
    ++numOpenWrite;
}

int main(int argc, char *argv[]) {
// DEBUG
    struct sigaction sa;
    int j;
	int nftw_flags = 0;
	struct timespec t1, t2;
	t1.tv_sec = 0;
	t1.tv_nsec = 100000000L;

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <sourcedir> <copydir>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    /* Establish handlers for SIGQUIT and the I/O completion signal */
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = quitHandler;
    if (sigaction(SIGQUIT, &sa, NULL) == -1)
        errExit("sigaction");
    sa.sa_flags = SA_RESTART | SA_SIGINFO;
    sa.sa_sigaction = aioSigHandler;
    if (sigaction(IO_SIGNAL, &sa, NULL) == -1)
        errExit("sigaction");

	/* Setup */
	orig_len = strlen(argv[1]);
	copy_dir = argv[2];
	nftw_flags |= FTW_PHYS; /* don't follow symbolic links */
	if (io_setup(max_size, &read_ctx) < 0 || io_setup(max_size, &write_ctx) < 0)
		errExit("io_setup");
	struct iocb all_read_iocb[max_size];
	for (j = 0; j < max_size; j++){
		memset(&all_read_iocb[j], 0, sizeof(struct iocb));
		read_cbs[j] = &all_read_iocb[j];
	}
	struct iocb all_write_iocb[max_size];
	for (j = 0; j < max_size; j++){
		memset(&all_write_iocb[j], 0, sizeof(struct iocb));
		write_cbs[j] = &all_write_iocb[j];
	}

	/* Dispatch a read request for each file in the source directory tree */
	if (nftw(argv[1], dispatch_read, 20, nftw_flags) == -1)
		errExit("nftw");


	int ret;
	/* Loop and dispatch write requests for each finished read request */
	while (numOpenRead > 0) {
		//nanosleep(&t1, &t2); //Don't need to sleep if using blocking io_getevents() call
		if (gotSIGQUIT)
			errExit("SIGQUIT received");
		/* Reap read completions and dispatch writes until all reads are finished */
		ret = io_getevents(read_ctx, 1, 1, read_events + numOpenRead, NULL);
		if (ret != 1)
			errExit("getevents");
	// DEBUG
	//printf("Got event %lld, fd %d\n", read_events[numOpenRead].res, ((struct iocb *)(read_events[numOpenRead].obj))->aio_fildes);
		dispatch_write(read_events[numOpenRead]);
		--numOpenRead;
    }

	/* Loop until write requests are done */
    while (numOpenWrite > 0) {
        if (gotSIGQUIT)
            errExit("SIGQUIT received");
        /* Reap write requests until finished*/
		ret = io_getevents(write_ctx, 1, 1, write_events + numOpenWrite, NULL);
		if (ret != 1)
			errExit("getevents");
		--numOpenWrite;
    }

	/* Cleanup */
	if (io_destroy(read_ctx) < 0 || io_destroy(write_ctx) < 0)
		errExit("io_destroy");
	return 0;
}
