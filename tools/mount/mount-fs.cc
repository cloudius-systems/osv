#include <cstring>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    // Check number of arguments
    if (argc != 4) {
        std::cout << "Usage:" << std::endl;
        std::cout << "\t" << argv[0] <<
                     " nfs" <<
                     " nfs://<server|ipv4|ipv6>/path[?arg=val[&arg=val]*]" <<
                     " /mount_point" << std::endl;
        return(1);
    }

    // fetch arguments
    std::string fs_type(argv[1]);
    std::string url(argv[2]);
    std::string mount_point(argv[3]);

    // create the mount point as a convenience if it does not already exists
    mkdir(mount_point.c_str(), 0777);
    // Mount and process error
    int ret = mount(url.c_str(), mount_point.c_str(), fs_type.c_str(), 0, nullptr);
    if (ret) {
        int my_errno = errno;
        std::cout << "Error in mount(): " << strerror(my_errno) << "(" << my_errno << ")"
                  << std::endl;
        return(1);
    }
    else {
        std::cout << "Mounted " << url << " at " << mount_point << std::endl;
    }

    return(0);
}
