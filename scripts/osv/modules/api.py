from osv.modules import resolve

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

class java_app(object):
    def __init__(self):
        require('java')

    def get_multimain_lines(self):
        return []

    def get_jvm_args(self):
        return []

class run_java(java_app):
    def __init__(self, classpath=[], args=[], jvm_args=[]):
        super(run_java, self).__init__()
        self.classpath = classpath
        self.args = args
        self.jvm_args = jvm_args

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
