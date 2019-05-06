#define _XOPEN_SOURCE 500
#include <ftw.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <aio.h>
#include <signal.h>
#include <string.h>
#include <stdint.h>
#define errExit(msg) do { perror(msg); exit(EXIT_FAILURE); } while (0)
#define errMsg(msg)  do { perror(msg); } while (0)

struct ioRequest {      /* Application-defined structure for tracking
                           I/O requests */
    int           reqNum;
    int           status;
    struct aiocb *aiocbp;
};

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

/* Globals */
#define max_size 1024
char copy_names[max_size][max_size];
int orig_len;
char *copy_dir;
struct ioRequest *ioReadList;
struct aiocb *aiocbReadList;
int numReadReq = 0;
int numOpenRead = 0;
struct ioRequest *ioWriteList;
struct aiocb *aiocbWriteList;
int numOpenWrite = 0;

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
//(intmax_t) sb->st_size
	/* Dispatch read for files */
	if (tflag == FTW_F) {
		ioReadList[numReadReq].reqNum = numReadReq;
        ioReadList[numReadReq].status = EINPROGRESS;
        ioReadList[numReadReq].aiocbp = &aiocbReadList[numReadReq];

        ioReadList[numReadReq].aiocbp->aio_fildes = open(fpath, O_RDONLY);
        if (ioReadList[numReadReq].aiocbp->aio_fildes == -1)
            errExit("open");
        ioReadList[numReadReq].aiocbp->aio_buf = malloc((intmax_t) sb->st_size);
        if (ioReadList[numReadReq].aiocbp->aio_buf == NULL)
            errExit("malloc");

        ioReadList[numReadReq].aiocbp->aio_nbytes = (intmax_t) sb->st_size;
        ioReadList[numReadReq].aiocbp->aio_reqprio = 0;
        ioReadList[numReadReq].aiocbp->aio_offset = 0;
        ioReadList[numReadReq].aiocbp->aio_sigevent.sigev_notify = SIGEV_SIGNAL;
        ioReadList[numReadReq].aiocbp->aio_sigevent.sigev_signo = IO_SIGNAL;
        ioReadList[numReadReq].aiocbp->aio_sigevent.sigev_value.sival_ptr =
                                &ioReadList[numReadReq];

        int s = aio_read(ioReadList[numReadReq].aiocbp);
        if (s == -1)
            errExit("aio_read");
		strcpy(copy_names[numReadReq], newpath);
		++numReadReq;
		++numOpenRead;
	}
	return 0;
}

void dispatch_write(int index) {
    ioWriteList[index].reqNum = numOpenWrite;
    ioWriteList[index].status = EINPROGRESS;
    ioWriteList[index].aiocbp = &aiocbWriteList[index];

    ioWriteList[index].aiocbp->aio_fildes = open(copy_names[index], O_WRONLY | O_CREAT, 0666);
    if (ioWriteList[index].aiocbp->aio_fildes == -1)
        errExit("open");
    ioWriteList[index].aiocbp->aio_buf = ioReadList[index].aiocbp->aio_buf;

    ioWriteList[index].aiocbp->aio_nbytes = ioReadList[index].aiocbp->aio_nbytes;
    ioWriteList[index].aiocbp->aio_reqprio = 0;
    ioWriteList[index].aiocbp->aio_offset = 0;
    ioWriteList[index].aiocbp->aio_sigevent.sigev_notify = SIGEV_SIGNAL;
    ioWriteList[index].aiocbp->aio_sigevent.sigev_signo = IO_SIGNAL;
    ioWriteList[index].aiocbp->aio_sigevent.sigev_value.sival_ptr =
                            &ioWriteList[index];

    int s = aio_write(ioWriteList[index].aiocbp);
    if (s == -1)
        errExit("aio_write");
    ++numOpenWrite;
}

int main(int argc, char *argv[]) {
    struct sigaction sa;
    int j;
	int nftw_flags = 0;

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

	/* vars */
	orig_len = strlen(argv[1]);
	copy_dir = argv[2];
	nftw_flags |= FTW_PHYS; /* don't follow symbolic links */

    /* Allocate our arrays */
    ioReadList = calloc(max_size, sizeof(struct ioRequest));
    if (ioReadList == NULL)
        errExit("calloc");
    aiocbReadList = calloc(max_size, sizeof(struct aiocb));
    if (aiocbReadList == NULL)
        errExit("calloc");
    ioWriteList = calloc(max_size, sizeof(struct ioRequest));
    if (ioWriteList == NULL)
        errExit("calloc");
    aiocbWriteList = calloc(max_size, sizeof(struct aiocb));
    if (aiocbWriteList == NULL)
        errExit("calloc");

	/* Dispatch a read request for each file in the source directory tree */
	if (nftw(argv[1], dispatch_read, 20, nftw_flags) == -1)
		errExit("nftw");

	/* Loop and dispatch write requests for each finished read request */
	while (numOpenRead > 0) {
		if (gotSIGQUIT)
			errExit("SIGQUIT received");

		/* Check status of read requests */
        for (j = 0; j < numReadReq; j++) {
            if (ioReadList[j].status == EINPROGRESS) {
                ioReadList[j].status = aio_error(ioReadList[j].aiocbp);

                switch (ioReadList[j].status) {
                case 0:
                    printf("Read succeeded\n");
					dispatch_write(j);
                    break;
                case EINPROGRESS:
                    break;
                case ECANCELED:
                    break;
                default:
                    errMsg("aio_error");
                    break;
                }

                if (ioReadList[j].status != EINPROGRESS)
                    --numOpenRead;
            }
        }
    }

	/* Loop until write requests are done */
    while (numOpenWrite > 0) {
        if (gotSIGQUIT)
            errExit("SIGQUIT received");

        /* Check status of write requests */
        for (j = 0; j < numReadReq; j++) {
            if (ioWriteList[j].status == EINPROGRESS) {
                ioWriteList[j].status = aio_error(ioWriteList[j].aiocbp);

                switch (ioWriteList[j].status) {
                case 0:
                    printf("Write succeeded\n");
                    break;
                case EINPROGRESS:
                    break;
                case ECANCELED:
                    break;
                default:
                    errMsg("aio_error");
                    break;
                }

                if (ioWriteList[j].status != EINPROGRESS)
                    --numOpenWrite;
            }
        }
    }

	printf("Copying completed.\n");
	return 0;
}
#if 0
    for (j = 0; j < numReqs; j++) {
        ioList[j].reqNum = j;
        ioList[j].status = EINPROGRESS;
        ioList[j].aiocbp = &aiocbList[j];

        ioList[j].aiocbp->aio_fildes = open(argv[j + 1], O_RDONLY);
        if (ioList[j].aiocbp->aio_fildes == -1)
            errExit("open");
        printf("opened %s on descriptor %d\n", argv[j + 1],
                ioList[j].aiocbp->aio_fildes);

        ioList[j].aiocbp->aio_buf = malloc(BUF_SIZE);
        if (ioList[j].aiocbp->aio_buf == NULL)
            errExit("malloc");

        ioList[j].aiocbp->aio_nbytes = BUF_SIZE;
        ioList[j].aiocbp->aio_reqprio = 0;
        ioList[j].aiocbp->aio_offset = 0;
        ioList[j].aiocbp->aio_sigevent.sigev_notify = SIGEV_SIGNAL;
        ioList[j].aiocbp->aio_sigevent.sigev_signo = IO_SIGNAL;
        ioList[j].aiocbp->aio_sigevent.sigev_value.sival_ptr =
                                &ioList[j];

        s = aio_read(ioList[j].aiocbp);
        if (s == -1)
            errExit("aio_read");
    }

    openReqs = numReqs;

    /* Loop, monitoring status of I/O requests */

    while (openReqs > 0) {
        sleep(3);       /* Delay between each monitoring step */

        if (gotSIGQUIT) {

            /* On receipt of SIGQUIT, attempt to cancel each of the
               outstanding I/O requests, and display status returned
               from the cancellation requests */

            printf("got SIGQUIT; canceling I/O requests: \n");

            for (j = 0; j < numReqs; j++) {
                if (ioList[j].status == EINPROGRESS) {
                    printf("    Request %d on descriptor %d:", j,
                            ioList[j].aiocbp->aio_fildes);
                    s = aio_cancel(ioList[j].aiocbp->aio_fildes,
                            ioList[j].aiocbp);
                    if (s == AIO_CANCELED)
                        printf("I/O canceled\n");
                    else if (s == AIO_NOTCANCELED)
                        printf("I/O not canceled\n");
                    else if (s == AIO_ALLDONE)
                        printf("I/O all done\n");
                    else
                        errMsg("aio_cancel");
                }
            }

            gotSIGQUIT = 0;
        }

        /* Check the status of each I/O request that is still
           in progress */

        printf("aio_error():\n");
        for (j = 0; j < numReqs; j++) {
            if (ioList[j].status == EINPROGRESS) {
                printf("    for request %d (descriptor %d): ",
                        j, ioList[j].aiocbp->aio_fildes);
                ioList[j].status = aio_error(ioList[j].aiocbp);

                switch (ioList[j].status) {
                case 0:
                    printf("I/O succeeded\n");
                    break;
                case EINPROGRESS:
                    printf("In progress\n");
                    break;
                case ECANCELED:
                    printf("Canceled\n");
                    break;
                default:
                    errMsg("aio_error");
                    break;
                }

                if (ioList[j].status != EINPROGRESS)
                    openReqs--;
            }
        }
    }

    printf("All I/O requests completed\n");

    /* Check status return of all I/O requests */

    printf("aio_return():\n");
    for (j = 0; j < numReqs; j++) {
        ssize_t s;

        s = aio_return(ioList[j].aiocbp);
        printf("    for request %d (descriptor %d): %zd\n",
                j, ioList[j].aiocbp->aio_fildes, s);
    }

    exit(EXIT_SUCCESS);
}
#endif
