/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

#include <string>
#include <vector>
#include <thread>
#include <osv/run.hh>

namespace uush {

#define UUSH_LINE_MAX 1023

int status; // status for uush itself

class command {
public:
    command(char *line, int len); /* note: alters line via strtok */

    bool background;
    std::vector<std::string> args;
    int ret;
    std::thread thr; /* for when running in background */
};

command::command(char *line, int len) {
    char *token = strtok(line, " ");
    while (token) {
        args.push_back(token);
        token = strtok(NULL, " ");
    }

    /* check if background or not (note, no effect on exit/logout) */
    background = false;
    int argc = args.size();

    if (argc > 1 && args[argc - 1].compare("&") == 0) {
        args.pop_back();
        background = true;
    } else {
        size_t cmdlen = args[argc - 1].length();
        if (args[argc - 1][cmdlen - 1] == '&') {
            args[argc - 1][cmdlen - 1] = '\0';
            background = true;
        }
    }
    ret = 0xdead;
}

std::vector<class command *> bg_tasks; /* commands running in background */

int exec(void *arg)
{
    auto cmd = (class command *)arg;
    try {
        osv::run(cmd->args[0], cmd->args, &cmd->ret);
    } catch (const osv::launch_error& e) {
        fprintf(stderr, "\nuush: failed to execute binary %s: %s\n",
                cmd->args[0].c_str(), e.what());
    }
    fprintf(stderr, "\nuush: task %s ended with status %d.\n",
            cmd->args[0].c_str(), cmd->ret);
    return cmd->ret; /* will be 0xdead if failed to execute */
}

/* do_line returns false when an exit is requested */
bool do_line(char *line, size_t len)
{
    class command *cmd = new command(line, len);

    if (cmd->args.empty()) {
        delete cmd;
        return true;
    }

    /* handle special built-ins exit and logout */
    if (cmd->args[0].compare("exit") == 0 || cmd->args[0].compare("logout") == 0) {
        int argc = cmd->args.size();

        if (argc == 1) {
            uush::status = 0;
        } else if (argc > 2 || sscanf(cmd->args[1].c_str(), "%d", &uush::status) != 1) {
            fprintf(stderr,
                    "\nSyntax: %s [N]\n\n"
                    "\texits the shell, return an optional status N.\n",
                    cmd->args[0].c_str());
            delete cmd;
            return true;
        }

        delete cmd;

        for (auto cmd : uush::bg_tasks) {
            fprintf(stderr, "\nuush: joining with bg task %s\n", cmd->args[0].c_str());
            cmd->thr.join();
            delete cmd;
        }

        return false;
    }

    if (cmd->background) {
        cmd->thr = std::thread(uush::exec, cmd);
        uush::bg_tasks.push_back(cmd);
    } else {
        (void)uush::exec(cmd);
        delete cmd;
    }

    return true;
}

}

int main(int argc, char *argv[])
{
    while (true) {
        char buffer[UUSH_LINE_MAX + 1];
        fprintf(stdout, "\nuush $ ");
        fflush(stdout);

        if (!fgets(buffer, sizeof(buffer), stdin))
            continue;

        size_t len = strlen(buffer);
        if (len < 1)
            continue;
        if (buffer[len - 1] == '\n') {
            buffer[len - 1] = '\0';
            if (--len < 1)
                continue;
        }
        if (!uush::do_line(buffer, len)) {
            break;
        }
    }

    return uush::status;
}
