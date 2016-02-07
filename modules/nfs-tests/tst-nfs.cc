#include <cassert>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mount.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>

#include <iostream>
#include <fstream>
#include <string>
#include <thread>

#include <boost/program_options.hpp>

#include <osv/app.hh>
#include <osv/run.hh>

namespace po = boost::program_options;

void assert_mount_error(int ret)
{
    assert(ret != EACCES);
    assert(ret != EBUSY);
    assert(ret != EFAULT);
    assert(ret != EINVAL);
    assert(ret != ELOOP);
    assert(ret != EMFILE);
    assert(ret != ENAMETOOLONG);
    assert(ret != ENODEV);
    assert(ret != ENOENT);
    assert(ret != ENOMEM);
    assert(ret != ENOTBLK);
    assert(ret != ENOTDIR);
    assert(ret != ENXIO);
    assert(ret != EPERM);
}

static void test_bogus_url_mount(std::string server, std::string share)
{
    std::string mount_point("/bogus");
    mkdir(mount_point.c_str(), 0777);
    // Did you notice OSv didn't supported gluster ?
    std::string url("gluster://" + server + share);
    int ret = mount(url.c_str(), mount_point.c_str(), "nfs", 0, nullptr);
    assert(ret && errno == EINVAL);
}

static void test_mount(std::string server, std::string share,
                       std::string mount_point)
{
    mkdir(mount_point.c_str(), 0777);
    std::string url("nfs://" + server + share);
    int ret = mount(url.c_str(), mount_point.c_str(), "nfs", 0, nullptr);
    if (ret) {
        int my_errno = errno;
        std::cout << "Error: " << strerror(my_errno) << "(" << my_errno << ")"
                  << std::endl;
        assert_mount_error(my_errno);
    }
    assert(!ret);
}

static void test_umount(std::string &mount_point)
{
    int ret = umount(mount_point.c_str());
    assert(!ret);
}

static void test_mkdir(std::string mount_point, std::string dir)
{
    std::string path = mount_point + "/" + dir;
    int ret = mkdir(path.c_str(), 0777);
    assert(!ret);
}

static void test_rmdir(std::string mount_point, std::string dir,
                       int result, int err_no)
{
    std::string path = mount_point + "/" + dir;
    int ret = rmdir(path.c_str());
    assert(ret == result);
    if (ret) {
        assert(err_no = errno);
    }
}

static void test_creat_and_close(std::string mount_point, std::string path)
{
    std::string full_path = mount_point + "/" + path;
    int fd = creat(full_path.c_str(), 0500);
    assert(fd != -1);
    int ret = close(fd);
    assert(!ret);
}

static void test_remove(std::string mount_point, std::string path)
{
    std::string full_path = mount_point + "/" + path;
    int ret = unlink(full_path.c_str());
    assert(!ret);
}

static void test_rename(std::string mount_point, std::string src,
                        std::string dst)
{
    std::string full_src = mount_point + "/" + src;
    std::string full_dst = mount_point + "/" + dst;
    int fd = creat(full_src.c_str(), 0500);
    assert(fd != -1);

    int ret = rename(full_src.c_str(), full_dst.c_str());
    assert(!ret);

    ret = close(fd);
    assert(!ret);
    ret = unlink(full_dst.c_str());
    assert(!ret);
}

static void test_truncate(std::string mount_point, std::string path)
{
    std::string full_path = mount_point + "/" + path;
    int fd = creat(full_path.c_str(), 0500);
    struct stat st;

    assert(fd != -1);

    int ret = ftruncate(fd, 333);
    assert(!ret);

    ret = close(fd);
    assert(!ret);

    ret = stat(full_path.c_str(), &st);
    assert(!ret);
    assert(st.st_size == 333);

    ret = unlink(full_path.c_str());
    assert(!ret);
}

//
// Cannot be tested with unfsd3 because it does not export symlink as they are.
//
//static void test_symlink_readlink(std::string mount_point, std::string path,
//                                  std::string alias)
//{
//    std::string full_path = mount_point + "/" + path;
//    std::string full_alias = mount_point + "/" + alias;
//    int fd = creat(full_path.c_str(), 0500);
//    assert(fd != -1);
//    int ret = close(fd);
//    assert(!ret);
//
//    ret = symlink(full_path.c_str(), full_alias.c_str());
//    assert(!ret);
//
//    char buf[PATH_MAX];
//    ret = readlink(full_alias.c_str(), buf, sizeof(buf));
//    assert(ret != -1);
//    assert(full_alias == std::string(buf));
//
//    ret = unlink(full_alias.c_str());
//    assert(!ret);
//
//    ret = unlink(full_path.c_str());
//    assert(!ret);
//}

void test_write_read(std::string mount_point, std::string path)
{
    std::string full_path = mount_point + "/" + path;
    const std::string msg("hadok, tournesol et milou");

    std::ofstream f;
    assert(f);
    f.open (full_path, std::ofstream::out | std::ofstream::app);
    f << msg;
    f.flush();
    f.close();

    std::ifstream g(full_path);
    assert(g);
    std::string result((std::istreambuf_iterator<char>(g)),
                       std::istreambuf_iterator<char>());
    g.close();

    assert(result == msg);
}

void test_readdir(std::string mount_point, std::string path)
{
    std::set<std::string> set;
    set.insert(".");
    set.insert("..");
    set.insert("alan");
    set.insert("dupont");

    std::string full_path = mount_point + "/" + path;
    // create the directory and fill it
    int ret = mkdir(full_path.c_str(), 0755);
    assert(!ret);

    std::string alan = full_path + "/" + "alan";
    std::string dupont = full_path + "/" + "dupont";

    int fd = creat(alan.c_str(), 0444);
    assert(fd != -1);
    ret = close(fd);
    assert(!ret);

    fd = creat(dupont.c_str(), 0444);
    assert(fd != -1);
    ret = close(fd);
    assert(!ret);

    // open the directory and list the name of the files in it
    auto dir = opendir(full_path.c_str());
    assert(dir);

    while (auto dirent  = readdir(dir)) {
        assert(set.count(std::string(dirent->d_name)) == 1);
    }

    // close the directory
    ret = closedir(dir);
    assert(!ret);
}

// make sure the mount() command openned a nfs lib directory handle
// for /mount_point
void test_root_readdir(std::string mount_point)
{
    std::string full_path = mount_point;

    // open the directory and list the name of the files in it
    auto dir = opendir(full_path.c_str());
    assert(dir);

    while (readdir(dir)) {
    }

    // close the directory
    int ret = closedir(dir);
    assert(!ret);
}

void test_fsx(std::string mount_point, std::string path)
{
    std::string full_path = mount_point + "/" + path;
    std::vector<std::string> args;

    args.push_back("fsx-linux.so");
    args.push_back("-d");
    args.push_back("-N");
    args.push_back("1000");
    args.push_back(full_path);

    int ret = 0;
    auto app = osv::run("/fsx-linux.so", args, &ret, true);
    assert(!ret);
}

void stress_thread(std::string path)
{
    std::ofstream f;
    assert(f);

    for (auto j = 0; j < 10000; j++) {
        f.open (path, std::ofstream::out | std::ofstream::app);
        f << "fooo";
        f.flush();
        f.close();
    }
}

void test_threaded(std::string mount_point, std::string path)
{
    std::string full_path = mount_point + "/" + path;

    std::thread t1(stress_thread, full_path);
    std::thread t2(stress_thread, full_path);
    std::thread t3(stress_thread, full_path);

    t1.join();
    t2.join();
    t3.join();
}

int main(int argc, char **argv)
{
    po::options_description desc("Allowed options");
    desc.add_options()
        ("help", "produce help message")
        ("server", po::value<std::string>(), "set server ip")
        ("share", po::value<std::string>(), "set remote share")
    ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << desc << std::endl;
        return 1;
    }

    std::string server;
    if (vm.count("server")) {
        server = vm["server"].as<std::string>();
    } else {
        std::cout << desc << std::endl;
        return 1;
    }

    std::string share;
    if (vm.count("share")) {
        share = vm["share"].as<std::string>();
    } else {
        std::cout << desc << std::endl;
        return 1;
    }

    test_bogus_url_mount(server, share);

    std::string mount_point("/nfs");

    // Testing mount/umount
    test_mount(server, share, mount_point);
    test_umount(mount_point);

    // Testing mkdir and rmdir
    test_mount(server, share, mount_point);
    // Test to rmdir something not existing
    test_rmdir(mount_point, "bar", -1, ENOENT);
    // mkdir followed by rmdir
    test_mkdir(mount_point, "foo");
    test_rmdir(mount_point, "foo", 0, 0);

    // Testing creat and close
    test_creat_and_close(mount_point, "zorglub");

    // Testing remove
    test_remove(mount_point, "zorglub");

    test_rename(mount_point, "fantasio", "spirou");

    test_truncate(mount_point, "secotine");

    // Cannot be tested with unsfd3
    //test_symlink_readlink(mount_point, "champignac", "zorglonde");

    test_write_read(mount_point, "tintin");

    test_readdir(mount_point, "castafiore");

    test_root_readdir(mount_point);

    test_threaded(mount_point, "zut");

    test_fsx(mount_point, "sanzo");

    test_umount(mount_point);
    return 0;
}
