/*
 * Copyright (C) 2013 Nodalink, SARL.
 * Copyright (C) 2014 Cloudius Systems.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <iterator>
#include <fstream>
#include <osv/debug.hh>

#include <boost/config/warning_disable.hpp>
#include <boost/spirit/include/qi.hpp>
#include <boost/program_options.hpp>
#include <osv/power.hh>
#include <osv/commands.hh>
#include <osv/align.hh>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace qi = boost::spirit::qi;
namespace ascii = boost::spirit::ascii;
using boost::spirit::ascii::space;

namespace osv {

typedef std::string::const_iterator sciter;

struct command : qi::grammar<sciter,
                             std::vector<std::string>(),
                             ascii::space_type>
{
    command() : command::base_type(start)
    {
        using qi::lexeme;
        using ascii::char_;
        unesc_char.add("\\a", '\a')("\\b", '\b')("\\f", '\f')("\\n", '\n')
                       ("\\r", '\r')("\\t", '\t')("\\v", '\v')("\\\\", '\\')
                       ("\\\'", '\'')("\\\"", '\"');

        string %= qi::no_skip[+(unesc_char | (char_ - ' ' - ';' - '&'))];
        quoted_string %= lexeme['"' >> *(unesc_char | (char_ - '"')) >> '"'];

        start %= ((quoted_string | string) % *space) >>
                (char_(';') | qi::string("&!") | char_('&') | qi::eoi);
    }

    qi::rule<sciter, std::string(), ascii::space_type> string;
    qi::rule<sciter, std::string(), ascii::space_type> quoted_string;
    qi::rule<sciter, std::vector<std::string>(), ascii::space_type> start;
    qi::symbols<char const, char const> unesc_char;
};

struct commands : qi::grammar<sciter,
                              std::vector<std::vector<std::string> >(),
                              ascii::space_type>
{
    commands() : commands::base_type(start)
    {
        start %= cmd % *space;
    }

    command cmd;
    qi::rule<sciter,
             std::vector<std::vector<std::string> >(),
             ascii::space_type> start;
};

std::vector<std::vector<std::string> >
parse_command_line_min(const std::string line, bool &ok)
{
    std::vector<std::vector<std::string> > result;

    // Lines with only {blank char or ;} are ignored.
    if (std::string::npos == line.find_first_not_of(" \f\n\r\t\v;")) {
        ok = true;
        return result;
    }

    commands g;
    sciter iter = std::begin(line);
    sciter end = std::end(line);
    ok = phrase_parse(iter,
                      end,
                      g,
                      space,
                      result);

    return result;
}

/*
Everything after first $ is assumed to be env var.
So with AA=aa, "$AA" -> "aa", and "X$AA" => "Xaa"
More than one $ is not supported, and "${AA}" is also not.
*/
void expand_environ_vars(std::vector<std::vector<std::string>>& result)
{
    std::vector<std::vector<std::string>>::iterator cmd_iter;
    std::vector<std::string>::iterator line_iter;
    for (cmd_iter=result.begin(); cmd_iter!=result.end(); cmd_iter++) {
        for (line_iter=cmd_iter->begin(); line_iter!=cmd_iter->end(); line_iter++) {
            size_t pos;
            if ((pos = line_iter->find_first_of('$')) != std::string::npos) {
                std::string new_word = line_iter->substr(0, pos);
                std::string key = line_iter->substr(pos+1);
                auto tmp = getenv(key.c_str());
                //debug("    new_word=%s, key=%s, tmp=%s\n", new_word.c_str(), key.c_str(), tmp);
                if (tmp) {
                    new_word += tmp;
                }
                *line_iter = new_word;
            }
        }
    }
}

/*
In each runscript line, first N args starting with - are options.
Parse options and remove them from result.

Options are applied immediately, just as in loader.cc parse_options().
So if two scripts set the same environment variable, then the last one wins.
Applying all options before running any command is also safer than trying to
apply options for each script at script execution (second script would modify
environment setup by the first script, causing a race).
*/
static void runscript_process_options(std::vector<std::vector<std::string> >& result) {
    namespace bpo = boost::program_options;
    namespace bpos = boost::program_options::command_line_style;
    // don't allow --foo bar (require --foo=bar) so we can find the first non-option
    // argument
    int style = bpos::unix_style & ~(bpos::long_allow_next | bpos::short_allow_next);
    bpo::options_description desc("OSv runscript options");
    desc.add_options()
        ("env", bpo::value<std::vector<std::string>>(), "set Unix-like environment variable (putenv())");

    for (size_t ii=0; ii<result.size(); ii++) {
        auto cmd = result[ii];
        bpo::variables_map vars;

        std::vector<const char*> args = { "dummy-string" };
        // due to https://svn.boost.org/trac/boost/ticket/6991, we can't terminate
        // command line parsing on the executable name, so we need to look for it
        // ourselves
        auto ac = cmd.size();
        auto av = std::vector<const char*>();
        av.reserve(ac);
        for (auto& prm: cmd) {
            av.push_back(prm.c_str());
        }
        auto nr_options = std::find_if(av.data(), av.data() + ac,
                                       [](const char* arg) { return arg[0] != '-'; }) - av.data();
        std::copy(av.data(), av.data() + nr_options, std::back_inserter(args));

        try {
            bpo::store(bpo::parse_command_line(args.size(), args.data(), desc, style), vars);
        } catch(std::exception &e) {
            std::cout << e.what() << '\n';
            std::cout << desc << '\n';
            osv::poweroff();
        }
        bpo::notify(vars);

        if (vars.count("env")) {
            for (auto t : vars["env"].as<std::vector<std::string>>()) {
                size_t pos = t.find("?=");
                if (std::string::npos == pos) {
                    // the basic "KEY=value" syntax
                    debug("Setting in environment: %s\n", t);
                    putenv(strdup(t.c_str()));
                }
                else {
                    // "KEY?=value", makefile-like syntax, set variable only if not yet set
                    auto key = t.substr(0, pos);
                    auto value = t.substr(pos+2);
                    if (nullptr == getenv(key.c_str())) {
                        debug("Setting in environment: %s=%s\n", key, value);
                        setenv(key.c_str(), value.c_str(), 1);
                    }
                }

            }
        }

        cmd.erase(cmd.begin(), cmd.begin() + nr_options);
        result[ii] = cmd;
    }
}

/*
If cmd starts with "runcript file", read content of file and
return vector of all programs to be run.
File can contain multiple commands per line.
ok flag is set to false on parse error, and left unchanged otherwise.

If cmd doesn't start with runscript, then vector with size 0 is returned.
*/
std::vector<std::vector<std::string>>
runscript_expand(const std::vector<std::string>& cmd, bool &ok, bool &is_runscript)
{
    std::vector<std::vector<std::string> > result2, result3;
    if (cmd[0] == "runscript") {
        is_runscript = true;
        /*
        The cmd vector ends with additional ";" or "\0" element.
        */
        if (cmd.size() != 3 && cmd[2].c_str()[0] != 0x00) {
            puts("Failed expanding runscript - filename missing or extra parameters present.");
            ok = false;
            return result2;
        }
        auto fn = cmd[1];

        std::ifstream in(fn);
        if (!in.good()) {
            printf("Failed to open runscript file '%s'.\n", fn.c_str());
            ok = false;
            return result2;
        }
        std::string line;
        size_t line_num = 0;
        while (!in.eof()) {
            getline(in, line);
            bool ok2;
            result3 = parse_command_line_min(line, ok2);
            debug("runscript expand fn='%s' line=%d '%s'\n", fn.c_str(), line_num, line.c_str());
            if (ok2 == false) {
                printf("Failed expanding runscript file='%s' line=%d '%s'.\n", fn.c_str(), line_num, line.c_str());
                result2.clear();
                ok = false;
                return result2;
            }
            // Replace env vars found inside script.
            // Options, script command and script command parameters can be set via env vars.
            expand_environ_vars(result3);
            // process and remove options from command
            runscript_process_options(result3);
            result2.insert(result2.end(), result3.begin(), result3.end());
            line_num++;
        }
    }
    else {
        is_runscript = false;
    }
    return result2;
}

std::vector<std::vector<std::string>>
parse_command_line(const std::string line,  bool &ok)
{
    std::vector<std::vector<std::string> > result, result2;
    result = parse_command_line_min(line, ok);
    // First replace environ variables in input command line.
    expand_environ_vars(result);

    /*
    If command starts with runscript, we need to read actual command to
    execute from the given file.
    */
    std::vector<std::vector<std::string>>::iterator cmd_iter;
    for (cmd_iter=result.begin(); ok && cmd_iter!=result.end(); ) {
        bool is_runscript;
        result2 = runscript_expand(*cmd_iter, ok, is_runscript);
        if (is_runscript) {
            cmd_iter = result.erase(cmd_iter);
            if (result2.size() > 0) {
                int pos;
                pos = cmd_iter - result.begin();
                result.insert(cmd_iter, result2.begin(), result2.end());
                cmd_iter = result.begin() + pos + result2.size();
            }
        }
        else {
            cmd_iter++;
        }
    }

    return result;
}

// std::string is not fully functional when parse_cmdline is used for the first
// time.  So let's go for a more traditional memory management to avoid testing
// early / not early, etc
char *osv_cmdline = nullptr;
static std::vector<char*> args;

std::string getcmdline()
{
    return std::string(osv_cmdline);
}

int parse_cmdline(const char *p)
{
    char* save;

    if (args.size()) {
        // From the strtok manpage, we see that: "The first call to strtok()
        // sets this pointer to point to the first byte of the string." It
        // follows from this that the first argument contains the address we
        // should use to free the memory allocated for the string
        free(args[0]);
    }

    args.resize(0);
    if (osv_cmdline) {
        free(osv_cmdline);
    }
    osv_cmdline = strdup(p);

    char* cmdline = strdup(p);

    while ((p = strtok_r(cmdline, " \t\n", &save)) != nullptr) {
        args.push_back(const_cast<char *>(p));
        cmdline = nullptr;
    }
    args.push_back(nullptr);
    __argv = args.data();
    __argc = args.size() - 1;

    return 0;
}

void save_cmdline(std::string newcmd)
{
    if (newcmd.size() >= max_cmdline) {
        throw std::length_error("command line too long");
    }

    int fd = open("/dev/vblk0", O_WRONLY);
    if (fd < 0) {
        throw std::system_error(std::error_code(errno, std::system_category()), "error opening block device");
    }

    lseek(fd, 512, SEEK_SET);

    // writes to the block device must be 512-byte aligned
    int size = align_up(newcmd.size() + 1, 512UL);
    int ret = write(fd, newcmd.c_str(), size);
    close(fd);

    if (ret != size) {
        throw std::system_error(std::error_code(errno, std::system_category()), "error writing command line to disk");
    }

    osv::parse_cmdline(newcmd.c_str());
}

}
