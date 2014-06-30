#include <sys/eventfd.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <poll.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define TMP_FILE	"/tmp/f1"

#define handle_perror(msg) \
   do { perror(msg); printf("\n"); exit(EXIT_FAILURE); } while (0)

#define handle_error(msg) \
   do { fprintf(stderr, "%s\n", msg); exit(EXIT_FAILURE); } while (0)

int simple_test(void)
{
    int      efd;
    uint64_t c;
    uint64_t u;
    ssize_t  s;

    printf("eventfd: running basic test: ");
    fflush(stdout);
    c   = 5;
    efd = eventfd(c, EFD_CLOEXEC | EFD_NONBLOCK);
    if (efd == -1) {
        handle_perror("eventfd");
    }

    s = read(efd, &u, sizeof(u));
    if (s != sizeof(u)) {
        handle_perror("read");
    }

    if (c != u) {
        handle_error("Incorrect value read.");
    }

    s = read(efd, &u, sizeof(u));
    if (s < 0) {
        int e = errno;
        errno = e;
        if (errno != EAGAIN) {
            handle_error("EAGAIN expected");
        }
    } else {
        handle_error("read failure and EAGAIN expected");
    }

    s = write(efd, &c, sizeof(c));
    if (s != sizeof(c)) {
        handle_perror("write");
    }

    s = read(efd, &u, sizeof(u));
    if (s != sizeof(u)) {
        handle_perror("read");
    }

    if (c != u) {
        handle_perror("Incorrect value read.");
    }

    close(efd);
    printf(" PASS\n");
    fflush(stdout);
    return (0);
}

int semaphore_test(void)
{
    int      efd;
    uint64_t c;
    uint64_t u;
    ssize_t  s;
    uint64_t i;

    printf("eventfd: Running semaphore_test: ");
    fflush(stdout);
    c   = 5;
    efd = eventfd(c, EFD_CLOEXEC | EFD_NONBLOCK | EFD_SEMAPHORE);
    if (efd == -1) {
        handle_perror("eventfd");
    }

    for (i = 0; i < c; i++) {
        s = read(efd, &u, sizeof(u));
        if (s != sizeof(u)) {
            handle_perror("read");
        }

        if (u != 1) {
            handle_error("Semaphore read count 1 expected.");
        }
    }

    s = read(efd, &u, sizeof(u));
    if (s < 0) {
        if (errno != EAGAIN) {
            handle_error("EAGAIN expected");
        }
    } else {
        handle_error("read failure and EAGAIN expected");
    }

    printf(" PASS\n");
    fflush(stdout);
    close(efd);
    return (0);
}

struct thread_data {
    uint64_t ev_count;
    int      loop;
    int      efd;
};

void *thread_write(void *arg)
{
    struct thread_data *td = (struct thread_data *) arg;
    ssize_t s;
    int     i;

    for (i = 0; i < td->loop; i++) {
        s = write(td->efd, &td->ev_count, sizeof(td->ev_count));
        if (s != sizeof(td->ev_count)) {
            handle_perror("write");
        }
        usleep(100);
    }
    return (NULL);
}

int threaded_test(void)
{
    uint64_t  count[] = {
        2,   3,   5,   7,  11,  13,  17,  19,  23,  29,
        31,  37,  41,  43,  47,  53,  59,  61,  67,  71,
        73,  79,  83,  89,  97, 101, 103, 107, 109, 113,
        127, 131, 137, 139, 149, 151, 157, 163, 167, 173,
        179, 181, 191, 193, 197, 199, 211, 223, 227, 229,
        233, 239, 241, 251, 257, 263, 269, 271, 277, 281,
        283, 293, 307, 311, 313, 317, 331, 337, 347, 349,
        353, 359, 367, 373, 379, 383, 389, 397, 401, 409,
        419, 421, 431, 433, 439, 443, 449, 457, 461, 463,
        467, 479, 487, 491, 499, 503, 509, 521, 523, 541,
        547, 557, 563, 569, 571, 577, 587, 593, 599, 601,
        607, 613, 617, 619, 631, 641, 643, 647, 653, 659,
        661, 673, 677, 683, 691, 701, 709, 719, 727, 733,
        739, 743, 751, 757, 761, 769, 773, 787, 797, 809,
        811, 821, 823, 827, 829, 839, 853, 857, 859, 863,
        877, 881, 883, 887, 907, 911, 919, 929, 937, 941,
        947, 953, 967, 971, 977, 983, 991, 997, 1009,
    };
    const int LOOP = 1000;
    const int THREADS = sizeof(count) / sizeof(count[0]);
    int efd;
    pthread_t thread[THREADS];
    struct thread_data td[THREADS];
    uint64_t total;
    int      i;
    int      rc;
    ssize_t  s;
    uint64_t u;
    uint64_t v;

    printf("eventfd: running simple threaded test: ");
    fflush(stdout);
    efd = eventfd(0, EFD_CLOEXEC);
    if (efd == -1) {
        handle_perror("eventfd");
    }

    total = 0;
    for (i = 0; i < THREADS; i++) {
        td[i].efd       = efd;
        td[i].ev_count  = count[i];
        td[i].loop      = LOOP;
        total          += (count[i] * LOOP);
    }

    for (i = 0; i < THREADS; i++) {
        rc = pthread_create(&thread[i], NULL, thread_write, &td[i]);
        if (rc != 0) {
            handle_perror("pthread_create");
        }
    }

    v = 0;
    while (total != v) {
        s = read(efd, &u, sizeof(u));
        if (s != sizeof(u)) {
            handle_perror("read");
        }

        v += u;
    }

    if (v != total) {
        handle_error("Unexpected value read");
    }

    /* verify all threads have finished */
    for (i = 0; i < THREADS; i++) {
        rc = pthread_join(thread[i], NULL);
        if (rc != 0) {
            handle_perror("pthread_join");
        }
    }
    close(efd);
    printf(" PASS\n");
    fflush(stdout);
    return (0);
}

int poll_test(void)
{
    int efd;
    ssize_t s;
    uint64_t c;
    uint64_t u;
    struct pollfd pfd[2];
    int rc;

    printf("eventfd: running poll test: ");
    fflush(stdout);
    efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (efd == -1) {
        handle_error("eventfd");
    }

    s = read(efd, &c, sizeof(c));
    if (s < 0) {
        if (errno != EAGAIN) {
            handle_error("EAGAIN expected");
        }
    } else {
        handle_error("read failure and EAGAIN expected");
    }

    /* no event count - read poll */
    pfd[0].fd = efd;
    pfd[0].events = POLLIN;
    rc = poll(pfd, 1, 10);
    if (rc != 0) {
        handle_error("expected timeout.\n");
    }

    if ((pfd[0].revents & POLLIN) != 0) {
        handle_error("no read event - POLLIN must not be set");
    }

    if ((pfd[0].revents & POLLOUT) != 0) {
        handle_error("write event on read fd is not expected");
    }

    /* no event count - write poll */
    pfd[0].fd = efd;
    pfd[0].events = POLLOUT;
    rc = poll(pfd, 1, -1);
    if (rc != 1 || ((pfd[0].revents & POLLOUT) == 0)) {
        handle_error("expected write event.");
    }

    /* combined read - write poll */
    pfd[0].fd = efd;
    pfd[0].events = POLLOUT;
    pfd[1].fd = efd;
    pfd[1].events = POLLIN;
    rc = poll(pfd, 2, -1);
    if (rc == 1) {
        if ((pfd[0].revents & POLLOUT) == 0) {
            handle_error("expected write event");
        }

        if ((pfd[0].revents & POLLIN) != 0) {
            handle_error("read event on write fd is not expected");
        }

        if (((pfd[1].revents & POLLOUT) != 0) ||
                ((pfd[1].revents & POLLIN) != 0) ) {
            handle_error("expected no events on read fd.");
        }
    } else {
        handle_error("one event expected.");
    }

    /* write to event and check read poll */
    c = 1;
    s = write(efd, &c, sizeof(c));
    if (s != sizeof(s)) {
        handle_error("");
    }

    pfd[0].fd = efd;
    pfd[0].events = POLLOUT;
    pfd[1].fd = efd;
    pfd[1].events = POLLIN;
    rc = poll(pfd, 2, -1);
    if (rc == 2) {
        if ((pfd[0].revents & POLLOUT) == 0) {
            handle_error("expected write event");
        }

        if ((pfd[0].revents & POLLIN) != 0) {
            handle_error("read event on write fd is not expected");
        }

        if ((pfd[1].revents & POLLOUT) != 0) {
            handle_error("write event on read fd is not expected");
        }

        if ((pfd[1].revents & POLLIN) == 0) {
            handle_error("expected read event");
        }
    } else {
        handle_error("expected two event.");
    }

    s = read(efd, &u, sizeof(u));
    if (s != sizeof(u)) {
        handle_perror("read");
    }

    /* max value boundary condition checking */
    c = ULLONG_MAX - 2;
    s = write(efd, &c, sizeof(c));
    if (s != sizeof(c)) {
        handle_perror("write");
    }

    pfd[0].fd = efd;
    pfd[0].events = POLLOUT;
    pfd[1].fd = efd;
    pfd[1].events = POLLIN;
    rc = poll(pfd, 2, -1);
    if (rc == 2) {
        if ((pfd[0].revents & POLLOUT) == 0) {
            handle_error("expected write event");
        }

        if ((pfd[0].revents & POLLIN) != 0) {
            handle_error("read event on write fd is not expected");
        }

        if ((pfd[1].revents & POLLOUT) != 0) {
            handle_error("write event on read fd is not expected");
        }

        if ((pfd[1].revents & POLLIN) == 0) {
            handle_error("expected read event");
        }
    } else {
        handle_error("expected two event.");
    }

    c = 2;
    s = write(efd, &c, sizeof(c));
    if (s < 0) {
        if (errno != EAGAIN) {
            handle_error("write - expected EAGAIN");
        }
    } else {
        handle_error("write failure and EAGAIN expected");
    }

    c = 1;
    s = write(efd, &c, sizeof(c));
    if (s != sizeof(c)) {
        handle_perror("write");
    }

    pfd[0].fd = efd;
    pfd[0].events = POLLOUT;
    pfd[1].fd = efd;
    pfd[1].events = POLLIN;
    rc = poll(pfd, 2, -1);
    if (rc == 1) {
        if ((pfd[0].revents & POLLOUT) != 0) {
            handle_error("write event not expected");
        }

        if ((pfd[0].revents & POLLIN) != 0) {
            handle_error("read event on write fd is not expected");
        }

        if ((pfd[1].revents & POLLOUT) != 0) {
            handle_error("write event on read fd is not expected");
        }

        if ((pfd[1].revents & POLLIN) == 0) {
            handle_error("expected read event");
        }
    } else {
        handle_error("expected two event.");
    }

    c = 1;
    s = write(efd, &c, sizeof(c));
    if (s < 0) {
        if (errno != EAGAIN) {
            handle_error("write - expected EAGAIN");
        }
    } else {
        handle_error("write failure and EAGAIN expected");
    }

    s = read(efd, &u, sizeof(u));
    if (s != sizeof(u)) {
        handle_perror("read");
    }

    if (u != ULLONG_MAX - 1) {
        handle_error("Incorrect value read");
    }

    close(efd);
    printf(" PASS\n");
    fflush(stdout);
    return (0);
}

int api_test(void)
{
    int       efd;
    int       rc;
    eventfd_t v;
    eventfd_t u;
    int       fd;

    printf("eventfd: running API test: ");
    fflush(stdout);
    efd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (efd == -1) {
        handle_error("eventfd");
    }

    rc = eventfd_read(efd, &v);
    if (rc < 0) {
        if (errno != EAGAIN) {
            handle_perror("eventfd_read");
        }
    }

    u  = 10;
    rc = eventfd_write(efd, u);
    if (rc < 0) {
        handle_perror("eventfd_write");
    }

    rc = eventfd_read(efd, &v);
    if (rc < 0) {
        handle_perror("eventfd_read");
    }

    close(efd);

    /* check errors */
    rc = eventfd_read(efd, &v);
    if (rc < 0) {
        if (errno != EBADF) {
            handle_perror("eventfd_read");
        }
    }

    rc = eventfd_write(efd, u);
    if (rc < 0) {
        if (errno != EBADF) {
            handle_perror("eventfd_write");
        }
    }

    fd = creat(TMP_FILE, 0777);
    if (fd < 0) {
        handle_perror("open");
    }

    rc = eventfd_read(fd, &v);
    if (rc < 0) {
        if (errno != EBADF) {
            handle_perror("eventfd_read");
        }
    }

    rc = eventfd_write(fd, u);
    if (rc < 0) {
        if (errno != EBADF) {
            handle_perror("eventfd_write");
        }
    }
    close(fd);
    unlink(TMP_FILE);
    printf(" PASS\n");
    fflush(stdout);
    return (0);
}

int main(void)
{
    simple_test();
    semaphore_test();
    threaded_test();
    poll_test();
    api_test();
    return (0);
}
