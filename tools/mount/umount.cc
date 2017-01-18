#include <cstring>
#include <sys/mount.h>

#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    // Check number of arguments
    if (argc != 2) {
        std::cout << "Usage:" << std::endl;
        std::cout << "\t" << argv[0] <<
                     " /mount_point" << std::endl;
        return(0);
    }

    // Umount and process error
    int ret = umount(argv[1]);
    if (ret) {
        int my_errno = errno;
        std::cout << "Error: " << strerror(my_errno) << "(" << my_errno << ")"
                  << std::endl;
        return(1);
    }

    return(0);
}
