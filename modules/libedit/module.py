from osv.modules.filemap import FileMap

VERSION = '20140620-3.1'

usr_files = FileMap()
usr_files.add('${OSV_BASE}/modules/libedit/build/libedit-%s/src/.libs' % VERSION).to('/usr/lib') \
	.include('lib*.so.?')
