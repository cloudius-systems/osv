/*
 * Copyright (C) 2015 Scylla, Ltd.
 *
 * Based on ramfs code Copyright (c) 2006-2007, Kohsuke Ohtani
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/debug.hh>
#include <osv/mount.h>
#include <osv/mutex.h>

#include "nfs.hh"

mount_context::mount_context(const char *url)
    : _valid(false)
    , _errno(0)
    , _raw_url(url)
    , _nfs(nullptr, nfs_destroy_context)
    , _url(nullptr, nfs_destroy_url)
{
    /* Create the NFS context */
    _nfs.reset(nfs_init_context());
    if (!_nfs) {
        debug("mount_context(): failed to create NFS context\n");
        _errno = ENOMEM;
        return;
    }

    // parse the url while taking care of freeing it when needed
    _url.reset(nfs_parse_url_dir(_nfs.get(), url));
    if (!_url) {
        debug(std::string("mount_context(): ") +
              nfs_get_error(_nfs.get()) + "\n");
        _errno = EINVAL;
        return;
    }

    _errno = -nfs_mount(_nfs.get(), _url->server, _url->path);
    if (_errno) {
        return;
    }

    _valid = true;
}

// __thread is not used for the following because it would give the error
// non-local variable ‘_lock’ declared ‘__thread’ has a non-trivial destructor
thread_local mutex _lock;
thread_local std::unordered_map<std::string,
                                std::unique_ptr<mount_context>> _map;

struct mount_context *get_mount_context(struct mount *mp, int &err_no)
{
    auto m_path = static_cast<const char*>(mp->m_path);
    std::string mount_point(m_path);
    err_no = 0;

    SCOPE_LOCK(_lock);

    // if not mounted at all mount it
    if (!_map.count(mount_point)) {
        _map[mount_point] =
            std::unique_ptr<mount_context>(new mount_context(mp->m_special));
    }

    // if we remounted with a different url change the mount point
    if (!_map[mount_point]->same_url(mp->m_special)) {
        _map.erase(mount_point);
        _map[mount_point] =
            std::unique_ptr<mount_context>(new mount_context(mp->m_special));
    }

    // clear the mess if the mount point is invalid
    if (!_map[mount_point]->is_valid(err_no)) {
        _map.erase(mount_point);
        return nullptr;
    }

    return _map[mount_point].get();
}
