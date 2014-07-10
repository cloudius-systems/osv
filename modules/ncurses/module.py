from osv.modules.filemap import FileMap

VERSION = '5.9'

usr_files = FileMap()
usr_files.add('${OSV_BASE}/modules/ncurses/build/ncurses-%s/lib' % VERSION).to('/usr/lib') \
	.include('lib*.so.?')

usr_files.add('${OSV_BASE}/modules/ncurses/out/terminfo').to('/usr/share/terminfo')
