#include<stdio.h>
#include<stdlib.h>
#include<time.h>
#include<sys/mman.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<sys/fcntl.h>
#include<unistd.h>
#include<sys/sendfile.h>
#include<assert.h>
#include<string.h>
#include<errno.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <thread>

/* To compile on Linux: g++ tests/tst-sendfile.cc -Wall -lpthread -std=c++0x */

static int tests = 0, fails = 0;

const char *test_filename = "testdata_for_sendfile_input";
int size_test_file = 1024*1024;

/* the fd for the testfile */
int testfile_readfd;
/* the pointer for mmap for testfile*/
char *src;

static void report(bool ok, const char* msg)
{
    ++tests;
    fails += !ok;
    printf("%s: %s\n", (ok ? "PASS" : "FAIL"), msg);
}

bool gen_random_file()
{
    int i;
    srand(time(NULL));
    FILE *fp = fopen(test_filename, "w");
    if (fp == NULL)
        return false;
    for(i = 0; i < size_test_file; i++)
        fprintf(fp, "%c", 'a' + rand() % 26);
    fclose(fp);
    return true;
}

/* Copies count bytes from the input file starting from offset and then verifies it */
int test_sendfile_on_filecopy(off_t *offset, size_t count)
{
    const char *out_file = "testdata_sendfile_output";
    int write_fd = open(out_file, O_RDWR | O_TRUNC | O_CREAT, S_IRWXU);
    if (write_fd == -1) {
        printf("\topen() failed with error message = %s\n",strerror(errno));
        return -1;
    }
    off_t last_position = offset == NULL ? lseek(testfile_readfd, 0 , SEEK_CUR) : *offset;
    int ret = sendfile(write_fd, testfile_readfd, offset, count);
    if (ret == -1) {
        return -1;
    }

    off_t new_position = offset == NULL ? lseek(testfile_readfd, 0 , SEEK_CUR) : *offset;
    if ((size_t)(new_position - last_position) != count){
        return -1;
    }

    char *dst = (char *)mmap(NULL, count, PROT_READ, MAP_SHARED, write_fd, 0);
    if (dst == MAP_FAILED) {
        printf("\tmmap on dst failed, error = %s\n",strerror(errno));
        return -1;
    }
    /* verify the contents */
    for(size_t i = 0; i < count; i++) {
        if (src[i + last_position] != dst[i]) {
            return -1;
        }
    }
    munmap(dst, count);
    close(write_fd);
    return ret;
}

int test_sendfile_on_socket(off_t *offset, size_t count)
{
    int listener_result = 0;
    off_t last_position = offset == NULL ? lseek(testfile_readfd, 0 , SEEK_CUR) : *offset;

    char *dst = (char *) malloc(count);
    int listen_fd = socket(AF_INET, SOCK_STREAM,  0);
    struct sockaddr_in listener_addr,client_addr;
    socklen_t client_len;

    bzero(&listener_addr,sizeof(listener_addr));
    listener_addr.sin_family = AF_INET;
    listener_addr.sin_addr.s_addr=htonl(INADDR_ANY);
    listener_addr.sin_port=htons(1337);

    int optval = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);

    if (bind(listen_fd, (struct sockaddr *)&listener_addr, sizeof(listener_addr)) == -1) {
        printf("could not bind. error = %s\n", strerror(errno));
        return -1;
    }
    if (listen(listen_fd, 1) == -1) {
        printf("could not listen. error = %s\n", strerror(errno));
        return -1;
    }

    std::thread *listener = new std::thread([&] {

        int connfd = accept(listen_fd, (struct sockaddr *)&client_addr, &client_len);

        if (connfd == -1) {
            printf("could not accept. error = %s\n", strerror(errno));
            return;
        }

        int bytes_recv = recv(connfd, dst, count, MSG_WAITALL);
        if (bytes_recv == -1 ) {
            listener_result = -1;
        }
        /* verify the recieved data */
        for(size_t i = 0;i < count; i++) {
            if (src[i + last_position] != dst[i]) {
                listener_result = -1;
                break;
            }
        }
        close(connfd);
        close(listen_fd);
    });

    int write_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in listener_server;
    bzero(&listener_server, sizeof(listener_server));
    listener_server.sin_family = AF_INET;
    listener_server.sin_addr.s_addr=inet_addr("127.0.0.1");
    listener_server.sin_port=htons(1337);

    if (connect(write_fd, (struct sockaddr *)&listener_server, sizeof(listener_server)) < 0) {
        return -1;
    }
    int ret = sendfile(write_fd, testfile_readfd, offset, count);
    if (ret == -1) {
        return -1;
    }

    listener->join();
    delete(listener);

    off_t new_position = offset == NULL ? lseek(testfile_readfd, 0 , SEEK_CUR) : *offset;
    if ((size_t)(new_position - last_position) != count){
        return -1;
    }

    return listener_result == 0 ? ret : -1;
}

int main()
{
    int ret;
    report(gen_random_file(), "Generate a file with random contents.");
    testfile_readfd = open(test_filename, O_RDONLY);
    report(testfile_readfd > 0, "open testfile for reading");
    src = (char *)mmap(NULL, size_test_file, PROT_READ, MAP_SHARED, testfile_readfd, 0);
    report(src != MAP_FAILED, "mmap testfile");
    printf("\n\n");

    int (*test_functions[2]) (off_t*, size_t);
    test_functions[0] = test_sendfile_on_filecopy;
    test_functions[1] = test_sendfile_on_socket;

    off_t offset;
    off_t *offset_p[2];
    offset_p[0] = NULL;
    offset_p[1] = &offset;

    int count_array[] = {10, 1014, 2048, 2560};
    std::string help[4] = {"test sendfile via partial file copy keeping offset as NULL",
    "test sendfile via partial file copy by varying offset",
    "test sendfile via partial file transfer over tcp keeping offset as NULL",
    "test sendfile via partial file transfer over tcp by varying offset"
    };

    for(int i = 0; i < 2; i++){
        for(int j = 0; j < 2; j++) {
            offset = 0;
            printf("%s\n",help[i*2 +j].c_str());
            for(unsigned k = 0; k < (sizeof(count_array)) / (sizeof(int)); k++) {
                std::string message = std::string("out_fd is ") + (i == 0 ? "file" : "socket");
                message += std::string(": copying ") + (k == 0 ? "first" : "next") + " " + std::to_string(count_array[k]) + " bytes";
                message += " ,offset = " + (j == 0 ? "NULL" : std::to_string(offset));
                report(test_functions[i](offset_p[j], count_array[k]) == count_array[k], message.c_str());
            }
            offset = 0;
           report(lseek(testfile_readfd, 0, SEEK_SET) == 0, "set readfd to beginning of file");
            report(test_functions[i](offset_p[j], size_test_file) == size_test_file, "copy entire file");
            printf("\n\n");
        }
    }

    /* force sendfile to fail in rest of test cases */
    ret = sendfile(100, testfile_readfd, NULL, 10);
    report(ret == -1 && errno == EBADF, "test for bad out_fd");

    int write_fd = open("temp_file", O_WRONLY|O_CREAT, 0755);
    report(write_fd > 0, "open dummy testfile in write mode");
    ret = sendfile(write_fd, 100, NULL, 100);
    report(ret == -1 && errno == EBADF, "test for bad in_fd");
    report(close(write_fd) == 0, "close the dummy testfile");

    write_fd = open("temp_file", O_RDONLY);
    report(write_fd > 0, "open dummy testfile in read only mode");
    ret = sendfile(write_fd, testfile_readfd, NULL, 10);
    report(ret == -1 && errno == EBADF, "test for bad mode of out_fd");
    report(close(write_fd) == 0, "close the dummy testfile");

    report(unlink(test_filename) == 0, "remove the testfile");
    printf("SUMMARY: %d tests, %d failures\n", tests, fails);
    munmap(src, size_test_file);
    return 0;
}
