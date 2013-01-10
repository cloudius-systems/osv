#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <algorithm>
#include <dirent.h>
#include "libc.hh"
#include "fs/fs.hh"

class std_file : public _IO_FILE {
public:
    explicit std_file(int fd);
    ~std_file();
};

class __dirstream {
public:
    explicit __dirstream(fileref file);
    fileref dir();
    int idx = 0;
private:
    fileref _file;
};

std_file* from_libc(FILE* file)
{
    return static_cast<std_file*>(file);
}

std_file::std_file(int fd)
{
    _fileno = fd;
}

std_file::~std_file()
{
    ::close(_fileno);
}

__dirstream::__dirstream(fileref file)
    : _file(file)
{
}

fileref __dirstream::dir()
{
	return _file;
}

FILE* fopen(const char* fname, const char* fmode)
{
    static struct conv {
        const char* fmode;
        int mode;
    } modes[] = {
        { "r", O_RDONLY },
        { "r+", O_RDWR },
        { "w", O_WRONLY | O_CREAT | O_TRUNC },
        { "w+", O_RDWR | O_CREAT | O_TRUNC },
        { "a", O_WRONLY | O_APPEND | O_CREAT | O_TRUNC },
        { "a+", O_RDWR | O_APPEND | O_CREAT | O_TRUNC },
    };
    auto p = std::find_if(modes, modes + 6,
                 [=](conv c) { return strcmp(fmode, c.fmode) == 0; } );
    if (p == modes + 6) {
        return nullptr;
    }
    auto fd = ::open(fname, p->mode);
    if (fd == -1) {
        return nullptr;
    }
    return new std_file(fd);
}

char *fgets(char *s, int size, FILE *stream)
{
    char* orig = s;
    while (size > 1) {
        int r = ::read(stream->_fileno, s, 1);
        assert(r != -1);
        if (r == 0) {
            break;
        }
        ++s;
        --size;
        if (s[-1] == '\n') {
            break;
        }
    }
    if (size) {
        *s = '\0';
    }
    return s == orig ? nullptr : orig;
}

int fclose(FILE* fp)
{
    delete from_libc(fp);
    return 0;
}

DIR* opendir(const char* fname)
{
    auto f = rootfs->open(fname);
    if (!f) {
        return libc_error_ptr<DIR>(ENOENT);
    }
    return new DIR(f);
}

int closedir(DIR* dir)
{
//	rootfs->close(dir->_file);   once we implement close
	delete dir;
	return 0;
}

struct dirent *readdir(DIR* dir)
{
	static struct dirent entry, *result;	// XXX: tls?
	int ret;

	ret = readdir_r(dir, &entry, &result);
	if (ret)
		return libc_error_ptr<struct dirent>(ret);

	errno = 0;
	return result;
}

int readdir_r(DIR* dir, struct dirent* entry, struct dirent** result)
{
	int ret;

	ret = dir->dir()->getdent(entry, dir->idx++);
	if (ret == 0)
		*result = entry;
	else
		*result = NULL;
	return 0;
}
