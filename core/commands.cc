/*
 * Copyright (C) 2013 Nodalink, SARL.
 * Copyright (C) 2014 Cloudius Systems.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <iterator>

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
                (char_(';') | char_('&') | qi::eoi);
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
parse_command_line(const std::string line, bool &ok)
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

// std::string is not fully functional when parse_cmdline is used for the first
// time.  So let's go for a more traditional memory management to avoid testing
// early / not early, etc
char *osv_cmdline = nullptr;
static std::vector<char*> args;

std::string getcmdline()
{
    return std::string(osv_cmdline);
}

int parse_cmdline(char *p)
{
    char* save;

    args.resize(0);
    if (osv_cmdline) {
        free(osv_cmdline);
    }
    osv_cmdline = strdup(p);

    char* cmdline = strdup(p);

    while ((p = strtok_r(cmdline, " \t\n", &save)) != nullptr) {
        args.push_back(p);
        cmdline = nullptr;
    }
    args.push_back(nullptr);
    __argv = args.data();
    __argc = args.size() - 1;

    return 0;
}

void save_cmdline(std::string newcmd)
{
    if (newcmd.size() > max_cmdline) {
        throw std::length_error("command line too long");
    }

    int fd = open("/dev/vblk0", O_WRONLY);
    if (fd < 0) {
        throw std::system_error(std::error_code(errno, std::system_category()), "error opening block device");
    }

    lseek(fd, 512, SEEK_SET);

    // writes to the block device must be 512-byte aligned
    int size = align_up(std::min(max_cmdline, newcmd.size()), 512UL);
    int ret = write(fd, newcmd.c_str(), size);
    close(fd);

    if (ret != size) {
        throw std::system_error(std::error_code(errno, std::system_category()), "error writing command line to disk");
    }

    char buf[newcmd.size() + 1];
    newcmd.copy((char *)buf, newcmd.size(), 0);
    osv::parse_cmdline(buf);
}

}
