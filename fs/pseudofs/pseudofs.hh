/*
 * Copyright (C) 2019 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef PSEUDOFS_HH
#define PSEUDOFS_HH

#include <osv/dentry.h>
#include <osv/vnode.h>

#include <functional>
#include <memory>
#include <map>
#include "fs/vfs/vfs.h"

namespace pseudofs {

using namespace std;

class pseudo_node {
public:
    pseudo_node(uint64_t ino, int type) : _ino(ino), _type(type) {}

    virtual ~pseudo_node() {}

    typedef map <string, shared_ptr<pseudo_node>> nmap;

    uint64_t ino() const { return _ino; };

    int type() const { return _type; };

    virtual off_t size() const = 0;

    virtual mode_t mode() const = 0;

private:
    uint64_t _ino;
    int _type;
};

class pseudo_file_node : public pseudo_node {
public:
    pseudo_file_node(uint64_t ino, function<string()> gen)
            : pseudo_node(ino, VREG), _gen(gen) {}

    virtual off_t size() const override {
        return 0;
    }

    virtual mode_t mode() const override {
        return S_IRUSR | S_IRGRP | S_IROTH;
    }

    string *data() const {
        return new string(_gen());
    }

private:
    function<string()> _gen;
};

class pseudo_dir_node : public pseudo_node {
public:
    pseudo_dir_node(uint64_t ino) : pseudo_node(ino, VDIR) {}

    shared_ptr <pseudo_node> lookup(string name) {
        auto it = _children.find(name);
        if (it == _children.end()) {
            return nullptr;
        }
        return it->second;
    }

    pseudo_node::nmap::iterator dir_entries_begin() {
        return _children.begin();
    }

    pseudo_node::nmap::iterator dir_entries_end() {
        return _children.end();
    }

    bool is_empty() {
        return _children.empty();
    }

    void add(string name, uint64_t ino, function<string()> gen) {
        _children.insert({name, make_shared<pseudo_file_node>(ino, gen)});
    }

    void add(string name, shared_ptr <pseudo_node> np) {
        _children.insert({name, np});
    }

    virtual off_t size() const override {
        return 0;
    }

    virtual mode_t mode() const override {
        return S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
    }

private:
    pseudo_node::nmap _children;
};

int open(file *fp);

int close(vnode *vp, file *fp);

int read(vnode *vp, file *fp, uio *uio, int ioflags);

int write(vnode *vp, uio *uio, int ioflags);

int ioctl(vnode *vp, file *fp, u_long cmd, void *arg);

int lookup(vnode *dvp, char *name, vnode **vpp);

int readdir(vnode *vp, file *fp, dirent *dir);

int getattr(vnode *vp, vattr *attr);
}

#endif
