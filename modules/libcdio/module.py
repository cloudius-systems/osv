from osv.modules.filemap import FileMap

VERSION=0.94

usr_files = FileMap()
usr_files.add('${OSV_BASE}/modules/libcdio/build/libcdio-%s/src/.libs' % VERSION).to('/usr/bin') \
	.include('iso-read.so')
usr_files.add('${OSV_BASE}/modules/libcdio/build/libcdio-%s/lib/udf/.libs' % VERSION).to('/usr/lib') \
	.include('libudf.so.0')
usr_files.add('${OSV_BASE}/modules/libcdio/build/libcdio-%s/lib/iso9660/.libs' % VERSION).to('/usr/lib') \
	.include('libiso9660.so.10')
usr_files.add('${OSV_BASE}/modules/libcdio/build/libcdio-%s/lib/driver/.libs' % VERSION).to('/usr/lib') \
	.include('libcdio.so.16')
