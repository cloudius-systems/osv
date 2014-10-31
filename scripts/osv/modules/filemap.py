import os
import re

def _path_has_pattern(path):
    return '*' in path or '?' in path

def _reduce_path(path, regex, substitution):
    while True:
        old_path = path
        path = re.sub(regex, substitution, path)
        if old_path == path:
            break
    return path

def _pattern_to_regex(path):
    bad_token = re.search(r'(([^/]\*\*)|(\*\*[^/]))', path)
    if bad_token:
        raise Exception("'**' pattern may only be a in place of a full path component, but found: %s"
             % bad_token.group(1))

    path = '^' + re.escape(path) + '$'

    # Normalize path
    path = re.sub(r'(\\/)+', '\/', path)

    # Merge consecutive **/ components
    path = _reduce_path(path, r'\\\*\\\*\\/\\\*\\\*', '\*\*')

    # Transform ** component
    path = re.sub(r'(\^|\\/)\\\*\\\*(\\/|\$)', '((^|\/).*($|\/|^))', path)

    path = path.replace('\\*', '[^/]*')
    path = path.replace('\\?', '.')
    return path


class PathFilter(object):
    def __init__(self, path_pattern, exclude):
        self.path_regex = _pattern_to_regex(path_pattern)
        self.exclude = exclude

    def __call__(self, path):
        return bool(re.match(self.path_regex, path))

    def is_include(self):
        return not self.exclude


class FileMap(object):
    def __init__(self):
        self.mappings = []
        self.symlinks = []

    def add(self, host_path, to=None):
        mapping = Mapping(os.path.expandvars(host_path))
        self.mappings.append(mapping)
        return mapping

    def link(self, guest_path):
        m = SymlinkMapping(guest_path)
        self.symlinks.append(m)
        return m

    def expand(self):
        """
        Returns series of tuples in the form (guest-path, host-path)

        """
        guest_to_host = {}

        def add(guest, host, allow_symlink):
            if guest in guest_to_host:
                old_mapping = guest_to_host[guest]
                if old_mapping != host:
                    raise Exception("Guest path '%s' already mapped to '%s', tried to remap to '%s'"
                        % (guest, old_mapping, host))
            if allow_symlink and os.path.islink(host):
                host = '!' + host
            guest_to_host[guest] = host

        for m in self.mappings:
            root = m.host_path
            if not os.path.isabs(root):
                raise Exception('Relative paths not allowed: ' + root)

            if not m.guest_path:
                raise Exception('Unfinished mapping for %s. Did you forget to call .to()?' % root)

            if not os.path.lexists(root):
                raise Exception('Path does not exist: ' + root)

            if (os.path.isfile(root)
                or (os.path.islink(root) and m._allow_symlink)):
                if m.filters:
                    raise Exception('Filters only allowed when adding directory')
                if m._allow_symlink:
                    root = '!' + root
                add(m.guest_path, root, m._allow_symlink)
            else:
                for dirpath, dirnames, filenames in os.walk(root):
                    for filename in filenames:
                        host_path = os.path.join(dirpath, filename)
                        rel_path = os.path.relpath(host_path, root)
                        if m.includes_path(rel_path):
                            add(os.path.join(m.guest_path, rel_path), host_path, m._allow_symlink)

        for mapping in guest_to_host.items():
            yield mapping

    def expand_symlinks(self):
        for s in self.symlinks:
            if not s.old_path:
                raise Exception('Unfinished symlink mapping for ' + mapping.new_path)
            yield (s.new_path, s.old_path)

class Mapping(object):
    def __init__(self, host_path):
        if _path_has_pattern(host_path):
            raise Exception('Host path must not be a pattern: ' + host_path)

        self.host_path = host_path
        self.has_include = False
        self.filters = []
        self.guest_path = None
        self._allow_symlink = False

    def include(self, path):
        self.filters.append(PathFilter(path, exclude=False))
        return self

    def exclude(self, path):
        self.filters.append(PathFilter(path, exclude=True))
        return self

    def includes_path(self, path):
        has_includes = any(filter(PathFilter.is_include, self.filters))
        include = not has_includes

        for f in self.filters:
            if f(path):
                include = f.is_include()

        return include

    def to(self, guest_path):
        if self.guest_path:
            raise Exception('Already mapped to %s, tried to remap to %s' % (self.guest_path, guest_path))
        self.guest_path = guest_path
        return self

    def allow_symlink(self):
        self._allow_symlink = True

class SymlinkMapping(object):
    def __init__(self, new_path):
        self.new_path = new_path

    def to(self, old_path):
        self.old_path = old_path

def as_manifest(filemap, appender):
    for mapping in filemap.expand():
        appender("%s: %s\n" % mapping)

    for mapping in filemap.expand_symlinks():
        appender("%s: ->%s\n" % mapping)

def save_as_manifest(filemap, filename):
    with open(filename, 'w') as file:
        as_manifest(filemap, file.write)
