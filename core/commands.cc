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

        string %= qi::no_skip[+(unesc_char | (char_ - ' ' - ';' - '&' - '!'))];
        quoted_string %= lexeme['"' >> *(unesc_char | (char_ - '"')) >> '"'];

        start %= ((quoted_string | string) % *space) >>
                (char_(';') | qi::string("&!") | char_('&') | char_('!') | qi::eoi);
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
void expand_environ_vars(std::string& word)
{
    size_t pos;
    if ((pos = word.find_first_of('$')) != std::string::npos) {
        std::string new_word = word.substr(0, pos);
        std::string key = word.substr(pos+1);
        auto tmp = getenv(key.c_str());
        //debug("    new_word=%s, key=%s, tmp=%s\n", new_word.c_str(), key.c_str(), tmp);
        if (tmp) {
            new_word += tmp;
        }
        word = new_word;
    }
}

/*
Expand environ vars in each word.
*/
void expand_environ_vars(std::vector<std::vector<std::string>>& result)
{
    std::vector<std::vector<std::string>>::iterator cmd_iter;
    std::vector<std::string>::iterator line_iter;
    for (cmd_iter=result.begin(); cmd_iter!=result.end(); cmd_iter++) {
        for (line_iter=cmd_iter->begin(); line_iter!=cmd_iter->end(); line_iter++) {
            expand_environ_vars(*line_iter);
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
                std::string key, value;
                if (std::string::npos == pos) {
                    // the basic "KEY=value" syntax
                    size_t pos2 = t.find("=");
                    assert(std::string::npos != pos2);
                    key = t.substr(0, pos2);
                    value = t.substr(pos2+1);
                    //debug("Setting in environment (def): %s=%s\n", key, value);
                }
                else {
                    // "KEY?=value", makefile-like syntax, set variable only if not yet set
                    key = t.substr(0, pos);
                    value = t.substr(pos+2);
                    if (nullptr != getenv(key.c_str())) {
                        // key already used, do not overwrite it
                        //debug("NOT setting in environment (makefile): %s=%s\n", key, value);
                        key = "";
                    }
                    else {
                        //debug("Setting in environment (makefile): %s=%s\n", key, value);
                    }
                }

                if (key.length() > 0) {
                    // we have something to set
                    expand_environ_vars(value);
                    debug("Setting in environment: %s=%s\n", key, value);
                    setenv(key.c_str(), value.c_str(), 1);
                }

            }
        }

        cmd.erase(cmd.begin(), cmd.begin() + nr_options);
        result[ii] = cmd;
    }
}

/*
If cmd starts with "runscript file", read content of file and
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
            // process and remove options from command
            runscript_process_options(result3);
            // Options, script command and script command parameters can be set via env vars.
            // Do this later than runscript_process_options, so that
            // runscript with content "--env=PORT?=1111 /usr/lib/mpi_hello.so aaa $PORT ccc"
            // will have argv[2] equal to final value of PORT env var.
            expand_environ_vars(result3);
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
static char* parsed_cmdline = nullptr;

std::string getcmdline()
{
    return std::string(osv_cmdline);
}

#define MY_DEBUG(args...) if(0) printf(args)
/*
loader_parse_cmdline accepts input str - OSv commandline, say:
--env=AA=aa --env=BB=bb1\ bb2 app.so arg1 arg2

The loader options are parsed and saved into argv, up to first not-loader-option
token. argc is set to number of loader options.
app_cmdline is set to unconsumed part of input str.
Example output:
argc = 2
argv[0] = "--env=AA=aa"
argv[1] = "--env=BB=bb1 bb2"
argv[2] = NULL
app_cmdline = "app.so arg1 arg2"

The whitespaces can be escaped with '\' to allow options with spaces.
Notes:
 - _quoting_ loader options with space is not supported.
 - input str is modified.
 - output argv has to be free-d by caller.
 - the strings pointed to by output argv and app_cmdline are in same memory as
   original input str. The caller is not permited to modify or free data at str
   after the call to loader_parse_cmdline, as that would corrupt returned
   results in argv and app_cmdline.

Note that std::string is intentionly not used, as it is not fully functional when
called early during boot.
*/
void loader_parse_cmdline(char* str, int *pargc, char*** pargv, char** app_cmdline) {
    *pargv = nullptr;
    *pargc = 0;
    *app_cmdline = nullptr;

    const char *delim = " \t\n";
    char esc = '\\';

    // parse string
    char *ap;
    char *ap0=nullptr, *apE=nullptr; // first and last token.
    int ntoken = 0;
    ap0 = nullptr;
    while(1) {
        // Did we already consume all loader options?
        // Look at first non-space char - if =='-', than this is loader option.
        // Otherwise, it is application command.
        char *ch = str;
        while (ch && *ch != '\0') {
            if (strchr(delim, *ch)) {
                ch++;
                continue;
            }
            else if (*ch == '-') {
                // this is a loader option, continue with loader parsing
                break;
            }
            else {
                // ch is not space or '-', it is start of application command
                // Save current position and stop loader parsing.
                *app_cmdline = str;
                break;
            }
        }
        if (ch && *ch == '\0') {
            // empty str, contains only spaces
            *app_cmdline = str;
        }
        if (*app_cmdline) {
            break;
        }
        // there are loader options, continue with parsing

        ap = stresep(&str, delim, esc);
        assert(ap);

        MY_DEBUG("  ap = %p %s, *ap=%d\n", ap, ap, *ap);
        if (*ap != '\0') {
            // valid token found
            ntoken++;
            if (ap0 == nullptr) {
                ap0 = ap;
            }
            apE = ap;
        }
        else {
            // Multiple consecutive delimiters found. Stresep will write multiple
            // '\0' into str. Squash them into one, so that argv will be 'nice',
            // in memory consecutive array of C strings.
            if (str) {
                MY_DEBUG("    shift   str %p '%s' <- %p '%s'\n", str-1, str-1, str, str);
                memmove(str-1, str, strlen(str) + 1);
                str--;
            }
        }
        if (str == nullptr) {
            // end of string, last char was delimiter
            *app_cmdline = ap + strlen(ap); // make app_cmdline valid pointer to '\0'.
            MY_DEBUG("    make app_cmdline valid pointer to '\\0' ap=%p '%s', app_cmdline=%p '%s'\n",
                ap, ap, app_cmdline, app_cmdline);
            break;
        }

    }
    MY_DEBUG("  ap0 = %p '%s', apE = %p '%s', ntoken = %d, app_cmdline=%p '%s'\n",
        ap0, ap0, apE, apE, ntoken, *app_cmdline, *app_cmdline);
    *pargv = (char**)malloc(sizeof(char*) * (ntoken+1));
    // str was modified, tokes are separated by exactly one '\0'
    int ii;
    for(ap = ap0, ii = 0; ii < ntoken; ap += strlen(ap)+1, ii++) {
        assert(ap != nullptr);
        assert(*ap != '\0');
        MY_DEBUG("  argv[%d] = %p %s\n", ii, ap, ap);
        (*pargv)[ii] = ap;
    }
    MY_DEBUG("  ntoken = %d, ii = %d\n", ntoken, ii);
    assert(ii == ntoken);
    (*pargv)[ii] = nullptr;
    *pargc = ntoken;
}
#undef MY_DEBUG

int parse_cmdline(const char *p)
{
    if (__loader_argv) {
        // __loader_argv was allocated by loader_parse_cmdline
        free(__loader_argv);
    }

    if (osv_cmdline) {
        free(osv_cmdline);
    }
    osv_cmdline = strdup(p);

    if (parsed_cmdline) {
        free(parsed_cmdline);
    }
    parsed_cmdline = strdup(p);

    loader_parse_cmdline(parsed_cmdline, &__loader_argc, &__loader_argv, &__app_cmdline);
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
