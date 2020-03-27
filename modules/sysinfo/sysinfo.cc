#include <iostream>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <functional>
#include <osv/options.hh>

static void usage()
{
    std::cout << "Allowed options:\n";
    std::cout << "  --interval arg (=1000000)   repeat every N microseconds\n";
}

static void handle_parse_error(const std::string &message)
{
    std::cout << message << std::endl;
    usage();
    exit(1);
}

int main(int ac, char** av)
{
    int interval_in_usecs = 1000000;

    auto options_values = options::parse_options_values(ac - 1, av + 1, handle_parse_error);
    if (options::option_value_exists(options_values, "interval")) {
        interval_in_usecs = options::extract_option_int_value(options_values, "interval", handle_parse_error);
    }

    while (1) {
        struct sysinfo info;
        sysinfo(&info);
	std::cout << "--> memory, total: " << info.totalram / 0x100000 << " MiB, used: " << 
             (info.totalram - info.freeram) / 0x100000 << " MiB" << std::endl;
        usleep(interval_in_usecs);
    }
}
