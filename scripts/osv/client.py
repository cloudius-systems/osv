import os
try:
    from urllib.request import urlopen
    from urllib.parse import urlparse
except ImportError:
    from urlparse import urlparse
    from urllib import urlopen

_osv_base = '.'
_default_cert_base = os.path.join(_osv_base, 'modules', 'certs', 'build')

def _pass_if_exists(path):
    if not os.path.exists(path):
        raise Exception('Path does not exist: ' + path)
    return path

class Client(object):
    @staticmethod
    def add_arguments(parser, use_full_url=False):
        parser.add_argument('--cert', help='path to client\'s SSL certifcate')
        parser.add_argument('--key', help='path to client\'s private key')
        parser.add_argument('--cacert', help='path to CA certifcate')
        parser.add_argument('--no-verify', help='skip validation of server''s certificate')
        if use_full_url:
            parser.add_argument("-u", "--url", action="store",
                                  help="Source URL for REST connections",
                                  default="http://localhost:8000/")
        else:
            parser.add_argument("host", nargs='?', default="localhost")
            parser.add_argument("port", nargs='?', default=8000, type=int)

    def __init__(self, args):
        self.args = args
        self.use_full_url = hasattr(args, 'url')

    def get_client_cert_path(self):
        if not self.args.cert:
            raise Exception('client''s certificate not set')
        return _pass_if_exists(self.args.cert)

    def get_client_key_path(self):
        if not self.args.key:
            raise Exception('client''s key not set')
        return _pass_if_exists(self.args.key)

    def get_cacert_path(self):
        if not self.args.cacert:
            raise Exception('CA certificate not set')
        return _pass_if_exists(self.args.cacert)

    def get_host(self):
        return urlparse(self.get_url()).hostname

    def get_port(self):
        return urlparse(self.get_url()).port

    def is_ssl(self):
        return bool(self.args.key)

    def get_url(self):
        if self.use_full_url:
            url = self.args.url
            if url.endswith('/'):
                return url[:-1]
            return url

        protocol = ["http", "https"][self.is_ssl()]
        return protocol + '://%s:%d' % (self.args.host, self.args.port)

    def get_request_kwargs(self):
        """Returns keyword arguments which should be passed to functions from the 'requests' library."""

        if not self.is_ssl():
            return {}

        return {
            'verify': [self.get_cacert_path(), False][bool(self.args.no_verify)],
            'cert': (self.get_client_cert_path(), self.get_client_key_path())
        }
