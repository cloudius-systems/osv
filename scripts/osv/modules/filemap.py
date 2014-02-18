import os
import re
from itertools import ifilter

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

    def add(self, host_path, to=None):
        mapping = Mapping(os.path.expandvars(host_path))
        self.mappings.append(mapping)
        return mapping

    def expand(self):
        """
        Returns series of tuples in the form (guest-path, host-path)

        """
        guest_to_host = {}

        def add(guest, host):
            if guest in guest_to_host:
                old_mapping = guest_to_host[guest]
                if old_mapping != host:
                    raise Exception("Guest path '%s' already mapped to '%s', tried to remap to '%s'"
                        % (guest, old_mapping, host))
            guest_to_host[guest] = host

        for m in self.mappings:
            root = m.host_path
            if not os.path.isabs(root):
                raise Exception('Relative paths not allowed: ' + root)

            if not m.guest_path:
                raise Exception('Unfinished mapping for %s. Did you forget to call .to()?' % root)

            if not os.path.exists(root):
                raise Exception('Path does not exist: ' + root)

            if os.path.isfile(root):
                if m.filters:
                    raise Exception('Filters only allowed when adding directory')
                add(m.guest_path, root)
            else:
                for dirpath, dirnames, filenames in os.walk(root):
                    for filename in filenames:
                        host_path = os.path.join(dirpath, filename)
                        rel_path = os.path.relpath(host_path, root)
                        if m.includes_path(rel_path):
                            add(os.path.join(m.guest_path, rel_path), host_path)

        for mapping in guest_to_host.iteritems():
            yield mapping


class Mapping(object):
    def __init__(self, host_path):
        if _path_has_pattern(host_path):
            raise Exception('Host path must not be a pattern: ' + host_path)

        self.host_path = host_path
        self.has_include = False
        self.filters = []
        self.guest_path = None

    def include(self, path):
        self.filters.append(PathFilter(path, exclude=False))
        return self

    def exclude(self, path):
        self.filters.append(PathFilter(path, exclude=True))
        return self

    def includes_path(self, path):
        has_includes = any(ifilter(PathFilter.is_include, self.filters))
        include = not has_includes

        for filter in self.filters:
            if filter(path):
                include = filter.is_include()

        return include

    def to(self, guest_path):
        if self.guest_path:
            raise Exception('Already mapped to %s, tried to remap to %s' % (self.guest_path, guest_path))
        self.guest_path = guest_path
        return self


def as_manifest(filemap, appender):
    for mapping in filemap.expand():
        appender("%s: %s\n" % mapping)

def save_as_manifest(filemap, filename):
    with open(filename, 'w') as file:
        as_manifest(filemap, file.write)
