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
If cmd starts with "runcript file", read content of file and
update cmd with the content.
ok flag is set to false on parse error, and left unchanged otherwise.
*/
void runscript_expand(std::vector<std::string>& cmd, bool &ok)
{
    if (cmd[0] == "runscript") {
        if (cmd.size()<2) {
            puts("Failed expanding runscript - filename missing.");
            ok = false;
            return;
        }
        auto fn = cmd[1];

        std::ifstream in(fn);
        std::string line;
        // only first line up to \n is used.
        getline(in, line);
        std::vector<std::vector<std::string> > result;
        bool ok2;
        result = parse_command_line_min(line, ok2);
        debug("runscript expand fn='%s' line='%s'\n", fn.c_str(), line.c_str());
        if (ok2 == false) {
            printf("Failed expanding runscript file='%s' line='%s'.\n", fn.c_str(), line.c_str());
            ok = false;
        }
        else {
            cmd = result[0];
        }
    }
}

std::vector<std::vector<std::string>>
parse_command_line(const std::string line,  bool &ok)
{
    std::vector<std::vector<std::string> > result;
    result = parse_command_line_min(line, ok);

    /*
    If command starts with runscript, we need to read actual command to
    execute from the given file.
    */
    std::vector<std::vector<std::string>>::iterator cmd_iter;
    for (cmd_iter=result.begin(); ok && cmd_iter!=result.end(); cmd_iter++) {
        runscript_expand(*cmd_iter, ok);
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
