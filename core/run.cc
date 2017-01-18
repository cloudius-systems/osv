#include <osv/run.hh>

namespace osv {

std::shared_ptr<osv::application> run(std::string path,
                     std::vector<std::string> args,
                     int* return_code,
                     bool new_program,
                     const std::unordered_map<std::string, std::string> *env)
{
    auto app = osv::application::run_and_join(path, args, new_program, env);
    if (return_code) {
        *return_code = app->get_return_code();
    }
    return app;
}

std::shared_ptr<osv::application> run(std::string path,
                                 int argc, const char* const* argv, int *return_code)
{
    std::vector<std::string> args;
    for (int i = 0; i < argc; i++) {
        args.push_back(argv[i]);
    }
    return run(path, args, return_code);
}

}
