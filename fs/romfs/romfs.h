#ifndef _ROMFS_H
#define _ROMFS_H

#include <osv/prex.h>
#include <stdint.h>

/* #define DEBUG_ROMFS 1 */

#ifdef DEBUG_ROMFS
#define DPRINTF(a)	dprintf a
#else
#define DPRINTF(a)	do {} while (0)
#endif

#define ASSERT(e)	assert(e)

#define ROMFS_MAGIC 0x7275
#define ROMFS_MAXFN 128

#define __mkw(h,l) (((h)&0x00ff)<< 8|((l)&0x00ff))
#define __mkl(h,l) (((h)&0xffff)<<16|((l)&0xffff))
#define __mk4(a,b,c,d) htonl(__mkl(__mkw(a,b),__mkw(c,d)))
#define ROMSB_WORD0 __mk4('-','r','o','m')
#define ROMSB_WORD1 __mk4('1','f','s','-')

struct romfs_dsb {
	uint32_t word0;
	uint32_t word1;
	uint32_t size;
	uint32_t checksum;
	char name[0];           /* volume name */
};

struct romfs_dinode {
	uint32_t next;            /* low 4 bits see ROMFH_ */
	uint32_t spec;
	uint32_t size;
	uint32_t checksum;
	char name[0];
};

#define ROMFH_TYPE 7
#define ROMFH_HRD 0
#define ROMFH_DIR 1
#define ROMFH_REG 2
#define ROMFH_SYM 3
#define ROMFH_BLK 4
#define ROMFH_CHR 5
#define ROMFH_SCK 6
#define ROMFH_FIF 7
#define ROMFH_EXEC 8

#define ROMFH_SIZE 16
#define ROMFH_PAD (ROMFH_SIZE-1)
#define ROMFH_MASK (~ROMFH_PAD)

struct romfs_mount {
	size_t	rm_maxsize;
};

struct romfs_node {
	unsigned long	rn_metadata_offset;
	unsigned long	rn_data_offset;
};

__BEGIN_DECLS
int	romfs_read_node(struct vnode *vp, unsigned long pos);
__END_DECLS

#endif /* !_ROMFS_H */
