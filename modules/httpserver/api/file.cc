/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "file.hh"
#include "routes.hh"
#include "transformers.hh"
#include "autogen/file.json.hh"
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <dirent.h>
#include <grp.h>
#include <pwd.h>
#include "json/formatter.hh"
#include <system_error>

namespace httpserver {

namespace api {

namespace file {

using namespace json;
using namespace std;
using namespace file_json;

/**
 * A helper function to set the op and path param
 * It validate that both exists and if not, throw an exception
 * @param params the parameters object
 * @param req the request
 * @param op will hold the op parameter
 * @param path will hold the path parameter
 */
static void set_and_validate_params(parameters* params,
                                    const http::server::request& req, string& op, string& path)
{
    op = req.get_query_param("op");
    path = (*params)["path"];
    if (op == "" || path == "") {
        throw bad_param_exception("missing mandatory parameters");
    }
}

/**
 * update stat structure by a file path
 * @param path the full path to a file or directory
 * @param buffer the stat structure to fill
 * throw a not found exception if the file is not found
 * and bad_param_exception on other errors
 */
static void get_stat(const string& path, struct stat& buffer)
{
    if (stat(path.c_str(), &buffer) != 0) {
        if (errno == ENOENT) {
            throw not_found_exception("Not found: '" + path + "'");
        }
        throw bad_param_exception(
            "Error opening: '" + path + "' " + strerror(errno));
    }
}

/**
 * validate that a path exists.
 * if the file or directory not there, throw an not found exception
 * @param path path to the file or directory
 */
static void validate_path(const string& path)
{
    struct stat buffer;
    get_stat(path, buffer);
}

/**
 * a helper function to get a file name from a path
 * @param path the full file name path
 * @return the file name only
 */
static string file_name(const string& path)
{
    unsigned found = path.find_last_of("/");
    if (found == string::npos) {
        return path;
    }
    return path.substr(found + 1);
}

/**
 * A helper function to copy a file, if the destination cannot be open
 * an exception is thrown
 * @param from path to the source file
 * @param to path to the destination file
 */
static void copy(const std::string& from, const std::string& to)
{

    validate_path(from);

    std::ifstream src;
    std::ofstream dst;
    src.exceptions(std::ios::failbit);
    dst.exceptions(std::ios::failbit);
    try {
        src.open(from, std::ios::binary);
    } catch (const std::system_error& e) {
        throw bad_param_exception(
            "Failed opening file '" + from + "' " + e.code().message());
    }
    try {
        dst.open(to, std::ios::binary);
    } catch (const std::system_error& e) {
        throw bad_param_exception(
            "Failed opening file '" + to + "' " + e.code().message());
    }

    try {
        dst << src.rdbuf();
    } catch (const std::system_error& e) {
        throw bad_param_exception(
            "Failed copying '" + from + "' to '" + to + "' "
            + e.code().message());
    }
}

class get_file_handler : public file_interaction_handler {
    virtual bool handle(const std::string& path, parameters* params,
                        const http::server::request& req, http::server::reply& rep)
    {
        string opStr, full_path;
        set_and_validate_params(params, req, opStr, full_path);
        ns_getFile::op op = ns_getFile::str2op(opStr);
        switch (op) {
        case ns_getFile::op::GET:
            return read(full_path, req, rep);
        case ns_getFile::op::GETFILESTATUS:
            return file_status(full_path, req, rep);
        case ns_getFile::op::LISTSTATUS:
            return list_directory(full_path, req, rep);
        default:
            throw bad_param_exception("Bad op parameter " + opStr);
        }
        return true;
    }

    bool file_status(const string& path, const http::server::request& req,
                     http::server::reply& rep)
    {
        struct stat buffer;
        get_stat(path, buffer);
        FileStatusProperties res = get_file_status(path, file_name(path),
                                   buffer);
        rep.content = res.to_json();
        set_headers(rep, "json");
        return true;
    }

    FileStatusProperties get_file_status(const string& path,
                                         const string& name,
                                         const struct stat& buffer)
    {
        FileStatusProperties res;
        res.blockSize = buffer.st_blksize;
        res.modificationTime = buffer.st_mtime;
        res.accessTime = buffer.st_atime;
        struct group gr;
        struct group *result;
        char buf[512];
        if (getgrgid_r(buffer.st_gid, &gr, buf, 512, &result) == 0) {
            res.group = gr.gr_name;
        }
        struct passwd pwd;
        struct passwd *pwdRes;
        if (getpwuid_r(buffer.st_uid, &pwd, buf, 512, &pwdRes) == 0) {
            res.owner = pwd.pw_name;
        }
        sprintf(buf, "%o", buffer.st_mode & 0777);
        res.permission = buf;
        res.replication = buffer.st_nlink;
        res.pathSuffix = name;
        unsigned long lng;
        if ((buffer.st_mode & S_IFMT) == S_IFLNK
                && (lng = readlink(path.c_str(), buf, 512)) > 0) {
            buf[lng] = 0;
            res.symlink = buf;
        }

        switch (buffer.st_mode & S_IFMT) {
        case S_IFDIR:
            res.type = "DIRECTORY";
            break;
        case S_IFLNK:
            res.type = "SYMLINK";
            break;
        default:
            res.type = "FILE";
        }

        return res;
    }

    bool list_directory(const string& path, const http::server::request& req,
                        http::server::reply& rep)
    {
        vector<FileStatusProperties> res;
        validate_path(path);
        auto dirp = opendir(path.c_str());
        if (dirp == nullptr) {
            throw bad_param_exception(
                "Failed listing '" + path + "' " + strerror(errno));
        }
        struct dirent entry;
        struct dirent *result;
        while (readdir_r(dirp, &entry, &result) == 0 && result != nullptr) {
            struct stat buffer;
            string name = path + "/" + entry.d_name;
            if (stat(name.c_str(), &buffer) == 0) {
                res.push_back(get_file_status(name, entry.d_name, buffer));
            }
        }

        (void) closedir(dirp);
        rep.content = formatter::to_json(res);
        set_headers(rep, "json");
        return true;
    }
};

class del_file_handler : public handler_base {
    virtual bool handle(const std::string& path, parameters* params,
                        const http::server::request& req, http::server::reply& rep)
    {
        string opStr, full_path;
        set_and_validate_params(params, req, opStr, full_path);
        ns_delFile::op op = ns_delFile::str2op(opStr);
        if (op == ns_delFile::op::DELETE) {
            validate_path(full_path);
            remove(full_path.c_str());
        } else {
            throw bad_param_exception("Bad op parameter " + opStr);
        }
        set_headers(rep, "json");
        return true;
    }
};

class post_file_handler : public handler_base {
    virtual bool handle(const std::string& path, parameters* params,
                        const http::server::request& req, http::server::reply& rep)
    {
        string full_path = (*params)["path"];
        string from = req.get_header("file_name");
        copy(from, full_path);
        set_headers(rep, "json");
        return true;
    }
};

class put_file_handler : public handler_base {
    virtual bool handle(const std::string& path, parameters* params,
                        const http::server::request& req, http::server::reply& rep)
    {
        string opStr, full_path;
        set_and_validate_params(params, req, opStr, full_path);
        ns_putFile::op op = ns_putFile::str2op(opStr);
        mode_t permission = 0777;
        if ((*params)["permission"] != "") {
            permission = strtol((*params)["permission"].c_str(), nullptr, 8);
        }
        string destination = req.get_query_param("destination");

        switch (op) {
        case ns_putFile::op::MKDIRS:
            if (mkdir(full_path.c_str(), permission) != 0) {
                throw bad_param_exception(
                    string("Failed creating directory ") + strerror(errno));
            }
            break;
        case ns_putFile::op::RENAME:
            if (destination == "") {
                throw bad_param_exception(
                    "Missing mandatory parameter: destination");
            }
            if (rename(full_path.c_str(), destination.c_str())
                    != 0) {
                throw bad_param_exception(
                    string("Failed renaming ") + strerror(errno));
            }
            break;
        case ns_putFile::op::COPY:
            if (destination == "") {
                throw bad_param_exception(
                    "Missing mandatory parameters: destination");
            }
            copy(full_path, destination);
            break;
        default:
            throw bad_param_exception("Bad op parameter " + opStr);
        }
        set_headers(rep, "json");
        return true;
    }
};

void init(routes& routes)
{
    file_json_init_path();

    routes.add_path(getFile, new get_file_handler());
    routes.add_path(delFile, new del_file_handler());
    routes.add_path(putFile, new put_file_handler());
    routes.add_path(upload, new post_file_handler());
}

}
}
}
