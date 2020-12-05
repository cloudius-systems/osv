#!/usr/bin/python3

def linux_distribution():
    def parse_file(f):
        res = {}
        for line in f:
            stripped = line.rstrip()
            if stripped:
                k, v = stripped.split('=')
                res[k] = v.strip('"')
        return res

    try:
        with open('/etc/os-release') as f:
            info = parse_file(f)
            return (info['NAME'], info['VERSION_ID'])
    except FileNotFoundError:
        try:
            with open('/etc/lsb-release') as f:
                info = parse_file(f)
                return (info['DISTRIB_ID'], info['DISTRIB_RELEASE'])
        except FileNotFoundError:
            print('Could not find linux distribution file!')
            return ('Unknown', 'Unknown')
