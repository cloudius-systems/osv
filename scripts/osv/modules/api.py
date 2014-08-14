from osv.modules import resolve
import os

require = resolve.require

class basic_app:
    def prepare_manifest(self, build_dir, manifest_type, manifest):
        pass

    def get_launcher_args(self):
        return []

class run(basic_app):
    def __init__(self, cmdline):
        self.cmdline = cmdline

    def get_launcher_args(self):
        return self.cmdline


class run_on_init(basic_app):
    next_sequence = 0

    def __init__(self, cmdline):
        self.cmdline = cmdline
        self.sequence = run_on_init.next_sequence
        run_on_init.next_sequence += 1

    def prepare_manifest(self, build_dir, manifest_type, manifest):
        if manifest_type == 'usr':
            file_name = '30-auto-%02d' % self.sequence
            local_path = os.path.join(resolve.get_build_path(), file_name)
            with open(local_path, 'w') as cmdline_file:
                cmdline_file.write(self.cmdline)
            manifest.write('/init/%s: %s\n' % (file_name, local_path))

class java_app(object):
    def __init__(self):
        require('java')

    def get_multimain_lines(self):
        return []

    def get_jvm_args(self):
        return []

def get_string_object():
    import sys
    if sys.version < '3':
        return basestring
    else:
        return str

def _to_args_list(text_or_list):
    if not text_or_list:
        return []
    if isinstance(text_or_list, get_string_object()):
        return text_or_list.split()
    return text_or_list

class run_java(java_app):
    def __init__(self, args=None, classpath=None, jvm_args=None):
        super(run_java, self).__init__()
        self.args = _to_args_list(args)
        self.classpath = classpath
        self.jvm_args = _to_args_list(jvm_args)

    def get_multimain_lines(self):
        args = []

        if self.classpath:
            args.append('-cp')
            args.append(':'.join(self.classpath))

        args.extend(self.args)
        return [' '.join(args)]

    def get_jvm_args(self):
        return self.jvm_args

class delayed(java_app):
    def __init__(self, app, delay=0):
        super(delayed, self).__init__()
        self.app = app
        self.delay = delay

    def get_multimain_lines(self):
        return ["sleep %d" % self.delay] + self.app.get_multimain_lines()

    def get_jvm_args(self):
        return self.app.get_jvm_args()
