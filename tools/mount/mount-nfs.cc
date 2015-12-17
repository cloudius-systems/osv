#include <cstring>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    // Check number of arguments
    if (argc != 3) {
        std::cout << "Usage:" << std::endl;
        std::cout << "\t" << argv[0] <<
                     " nfs://<server|ipv4|ipv6>/path[?arg=val[&arg=val]*]" <<
                     " /mount_point" << std::endl;
        return(1);
    }

    // fetch arguments
    std::string url(argv[1]);
    std::string mount_point(argv[2]);

    // create the mount point as a convenience if it does not already exists
    mkdir(mount_point.c_str(), 0777);
    // Mount and process error
    int ret = mount(url.c_str(), mount_point.c_str(), "nfs", 0, nullptr);
    if (ret) {
        int my_errno = errno;
        std::cout << "Error: " << strerror(my_errno) << "(" << my_errno << ")"
                  << std::endl;
        return(1);
    }

    return(0);
}
