#include <stdio.h>
#include <dirent.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/fcntl.h>
#include <sys/errno.h>
#include <stdint.h>

#define MFS_FILENAME_MAXLEN    63

struct mfs_super_block {
    uint64_t version;
    uint64_t magic;
    uint64_t block_size;
    uint64_t inodes_count;
};

struct mfs_inode {
    mode_t   mode;
    uint64_t inode_no;
    uint64_t data_block_number;
    union {
        uint64_t file_size;
        uint64_t dir_children_count;
    };
};

struct mfs_dir_record {
    // Add one for \0
    char filename[MFS_FILENAME_MAXLEN + 1];
    uint64_t inode_no;
};

struct node {
    void * data;
    struct node *next;
};

#define MFS_VERSION            1
#define MFS_MAGIC              0xDEADBEEF
#define MFS_FILENAME_MAXLEN    63
#define MFS_ROOT_INODE_NUMBER  1

#define MFS_SUPERBLOCK_SIZE (uint64_t)sizeof(struct mfs_super_block)
#define MFS_SUPERBLOCK_PADDING(bs) bs - MFS_SUPERBLOCK_SIZE
#define MFS_INODE_SIZE (uint64_t)sizeof(struct mfs_inode)
#define MFS_INODES_PER_BLOCK(bs) bs / MFS_INODE_SIZE
#define MFS_INODE_BLOCK_PADDING(bs) bs - (MFS_INODES_PER_BLOCK(bs) * MFS_INODE_SIZE)

#define MFS_RECORD_SIZE (uint64_t)sizeof(struct mfs_dir_record)
#define MFS_RECORDS_PER_BLOCK(bs) (bs / MFS_RECORD_SIZE)
#define MFS_RECORDS_PADDING(bs) (bs - (MFS_RECORDS_PER_BLOCK(bs) * MFS_RECORD_SIZE))

#define _MFS_RAW_BLOCKS_NEEDED(bs, c) ((c) / (bs))
#define _MFS_MOD_BLOCKS_NEEDED(bs, c) (((c) % (bs)) > 0 ? 1 : 0)
#define MFS_BLOCKS_NEEDED(bs, c) (_MFS_RAW_BLOCKS_NEEDED(bs, c) + _MFS_MOD_BLOCKS_NEEDED(bs, c))

#define MFS_RECORD_BLOCKS_NEEDED(bs, rc) (MFS_BLOCKS_NEEDED(bs, (rc * MFS_RECORD_SIZE)))
#define MFS_INODE_BLOCKS_NEEDED(bs, ic) (MFS_BLOCKS_NEEDED(bs, (ic * MFS_INODE_SIZE)))

#define MFS_INODE_BLOCK(bs, i) (i / MFS_INODES_PER_BLOCK(bs)) + 1
#define MFS_INODE_OFFSET(bs, i) i % (MFS_INODES_PER_BLOCK(bs))

#define MFS_PERMISSIONS      S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH
#define MFS_FILE_PERMISSIONS S_IFREG | MFS_PERMISSIONS
#define MFS_DIR_PERMISSIONS S_IFDIR | MFS_PERMISSIONS

static volatile uint64_t total_bytes = 0;

void wtotal(ssize_t rv) {
    total_bytes += rv;
}

static inline mode_t get_mode(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0)
        return st.st_mode;
    return 0;
}

static inline void pad(int fd, uint64_t padding) {
    char *buf = (char *)malloc(padding);
    ssize_t rv = write(fd, buf, padding);
    wtotal(rv);
    free(buf);
}

int write_initial_superblock(int fd, uint64_t block_size) {

    if (block_size < MFS_SUPERBLOCK_SIZE) {
        fprintf(stderr, "block size must be larger than %llu\n", MFS_SUPERBLOCK_SIZE);
        return -1;
    }
    pad(fd, block_size);
    return 1;
}

int write_superblock(int fd, uint64_t block_size, uint64_t inodes) {
    ssize_t rv = 0;

    printf("Updating superblock with [%llu]\n", inodes);
    struct mfs_super_block mfs_sb = {
            .version      = MFS_VERSION,
            .magic        = MFS_MAGIC,
            .block_size   = block_size,
            .inodes_count = inodes,
    };

    rv = write(fd, &mfs_sb, sizeof(mfs_sb));
    wtotal(rv);

    return 1;
}

struct node *inodes_head = NULL;
struct node *inodes_tail = NULL;
uint64_t inode_index = MFS_ROOT_INODE_NUMBER;

struct mfs_inode *next_inode() {
    struct mfs_inode *inode = (struct mfs_inode *)malloc(sizeof(struct mfs_inode));
    struct node *cur = (struct node *)malloc(sizeof(struct node));

    inode->inode_no = inode_index++;
    inode->mode = 0;
    inode->data_block_number = 0;
    inode->file_size = 0;

    cur->data = inode;
    cur->next = NULL;

    if (inodes_head == NULL) {
        inodes_head = cur;
    }
    if (inodes_tail == NULL) {
        inodes_tail = cur;
    } else {
        inodes_tail->next = cur;
        inodes_tail = cur;
    }

    return inode;
}

typedef struct list {
    struct node *head;
    struct node *tail;
} list;

struct mfs_dir_record *next_rec(list *l) {

    struct mfs_dir_record *rec = (struct mfs_dir_record *)malloc(sizeof(struct mfs_dir_record));
    struct node *cur = (struct node *)malloc(sizeof(struct node));

    rec->inode_no = 0;
    memset(rec->filename, 0, MFS_FILENAME_MAXLEN + 1);

    cur->data = rec;
    cur->next = NULL;

    if (l->head == NULL) {
        l->head = cur;
    }
    if (l->tail == NULL) {
        l->tail = cur;
    } else {
        l->tail->next = cur;
        l->tail = cur;
    }

    return rec;
}

uint64_t write_file(int fd, uint64_t block_size, const char *path) {
    int in;
    ssize_t rb;
    ssize_t rv;
    long len = 0;
    uint64_t total = 0;
    char buf[block_size];

    in = open(path, O_RDONLY);
    if (in == -1) {
        fprintf(stderr, "Error reading '%s'\n", path);
        return 0;
    }

    printf("Adding file %s\n", path);

    while ( (rb = read(in, buf, block_size)) > 0) {
        total += rb;
        rv = write(fd, &buf, rb);
    }
    wtotal(total);

    close(in);

    if ((total % block_size) > 0) {
        pad(fd, block_size - (total % block_size));
    }

    return total;
}

uint64_t write_dir(int fd, struct mfs_inode *parent, uint64_t block_size, uint64_t *blocks, const char *path) {
    list dir_ents = {NULL, NULL};
    struct node *cur;
    DIR *dir;
    struct dirent *entry;
    struct mfs_inode *inode;
    struct mfs_dir_record *rec;
    size_t path_len;
    uint64_t count = 0;
    ssize_t rv = 0;
    uint64_t dir_c = 0;
    mode_t mode = 0;
    char full_name[_POSIX_PATH_MAX + 1];

    if ( (dir = opendir(path)) == NULL ) {
        return 0;
    }

    while ( (entry = readdir(dir)) != NULL ) {
        path_len = strlen(path);

        strcpy(full_name, path);
        if (full_name[path_len - 1] != '/')
            strcat(full_name, "/");
        strcat(full_name, entry->d_name);

        /* Ignore special directories. */
        if ((strcmp(entry->d_name, ".") == 0) ||
            (strcmp(entry->d_name, "..") == 0))
            continue;

        if (strlen(entry->d_name) <= MFS_FILENAME_MAXLEN) {
            // Check type here

            count++;
            inode = next_inode();
            rec = next_rec(&dir_ents);
            memcpy(rec->filename, entry->d_name, strlen(entry->d_name));
            rec->inode_no = inode->inode_no;
            // rec->inode_no = parent->inode_no;

            mode = get_mode(full_name);
            if (S_ISDIR(mode)) {
                inode->mode = MFS_DIR_PERMISSIONS;
                // inode->data_block_number = *blocks;
                inode->dir_children_count = write_dir(fd, inode, block_size, blocks, full_name);
            } else if (S_ISREG(mode)) {
                inode->mode = MFS_FILE_PERMISSIONS;
                inode->data_block_number = *blocks;
                inode->file_size = write_file(fd, block_size, full_name);
                *blocks += MFS_BLOCKS_NEEDED(block_size, inode->file_size);
            } else {
                fprintf(stderr, "Skipping '%s', invalid type: %d\n", entry->d_name, mode);
            }
        } else {
            fprintf(stderr, "Skipping '%s', filename too long.\n", full_name);
        }
    }
    parent->dir_children_count = count;
    parent->data_block_number = *blocks;
    rec = NULL;
    cur = dir_ents.head;
    dir_c = 0;

    //TODO: Free dirs
    while (cur != NULL) {
        rec = (struct mfs_dir_record *)cur->data;

        rv = write(fd, rec, MFS_RECORD_SIZE);
        if (rv != MFS_RECORD_SIZE) {
            fprintf(stderr, "Error writing directory records\n");
            return 0;
        }

        wtotal(rv);

        dir_c++;
        if (dir_c == MFS_RECORDS_PER_BLOCK(block_size)) {
            pad(fd, MFS_RECORDS_PADDING(block_size));
            dir_c = 0;
        }

        cur = cur->next;
    }

    *blocks += MFS_RECORD_BLOCKS_NEEDED(block_size, count);

    if (dir_c != 0) {
        pad(fd, block_size - (dir_c * MFS_RECORD_SIZE));
    }

    return count;
}

// Returns blocks written by all of the files, not the inodes
uint64_t write_fs(int fd, uint64_t block_size, const char* root_path) {
    struct node dir_ents = {NULL, NULL};
    uint64_t blocks = 1; // Set to 1 to account for superblock
    ssize_t rv = 0;
    struct node *cur = NULL;
    struct mfs_inode *inode = next_inode();
    uint64_t count = 0;
    uint64_t inode_counter = 0;

    inode->mode = MFS_DIR_PERMISSIONS;

    write_dir(fd, inode, block_size, &blocks, root_path);

    cur = inodes_head;
    //TODO: Free inodes?
    while (cur != NULL) {
        inode = (struct mfs_inode *)cur->data;
        count++;
        cur = cur->next;

        // printf("Writing inode %llu\n", inode->inode_no);
        // printf("Inode datablock: %llu\n", inode->data_block_number);

        rv = write(fd, inode, MFS_INODE_SIZE);

        if (rv != MFS_INODE_SIZE) {
            fprintf(stderr, "Error writing inodes!\n");
            return 0;
        }

        wtotal(rv);
        inode_counter++;
        if (inode_counter == MFS_INODES_PER_BLOCK(block_size)) {
            pad(fd, MFS_INODE_BLOCK_PADDING(block_size));
            inode_counter = 0;
        }
    }

    if (inode_counter < MFS_INODES_PER_BLOCK(block_size)) {
        pad(fd, block_size - (inode_counter * MFS_INODE_SIZE));
    }

    printf("Wrote %llu inodes\n", count);

    return blocks;
}

int main(int argc, char** argv) {

    const char * input_path = argv[1];
    const char * output_path = argv[2];
    uint64_t block_size = 512;

    uint64_t c = 0;
    int fd;


    fd = open(output_path, O_RDWR | O_CREAT, 0777);
    if (fd == -1) {
        fprintf(stderr, "Error opening %s\n", output_path);
        fprintf(stderr, "Error %s\n", strerror(errno));
        return 1;
    }

    if (write_initial_superblock(fd, block_size) == -1) {
        fprintf(stderr, "Failed to write superblock\n!");
        return 1;
    }
    printf("Wrote initial super block.\n");
    wtotal(0);

    // Write the file system
    c = write_fs(fd, block_size, input_path);

    // printf("Wrote %llu inodes.\n", c);

    printf("Wrote %llu bytes\n", total_bytes);

    lseek(fd, 0, SEEK_SET);
    write_superblock(fd, block_size, c); // We add 1 to account for the superblock

    printf("Updated superblock.\n");

    // Write all of the inodes
    printf("Done.\n");
    // Done (maybe store total size in superblock)

    return 0;
}
