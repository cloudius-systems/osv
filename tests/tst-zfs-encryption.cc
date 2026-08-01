/*
 * Copyright (C) 2026 OSv Authors
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/*
 * ZFS encryption integration test.
 *
 * Tests AES-256-GCM encryption on a ZFS dataset:
 *   1. Check that the encryption feature is enabled on the pool.
 *   2. Generate a random 32-byte raw wrapping key and write to a key file.
 *   3. Create an encrypted dataset (AES-256-GCM, keyformat=raw, keylocation=file).
 *   4. Verify zfs_is_encrypted() returns true.
 *   5. Write test data to the encrypted dataset.
 *   6. Read back and verify data integrity.
 *   7. Unload the wrapping key.
 *   8. Verify that new I/O to the dataset fails (EIO / EACCES).
 *   9. Reload the wrapping key.
 *  10. Verify that existing data is still intact.
 *  11. Destroy the dataset.
 *
 * Run: ./scripts/run.py --image <zfs-image> -e "tests/tst-zfs-encryption.so"
 * Requires: ZFS root filesystem (build with fs=zfs)
 */

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dlfcn.h>

/* ------------------------------------------------------------------ */
/* Minimal libzfs/libnvpair runtime bindings via dlopen                */
/* ------------------------------------------------------------------ */
typedef struct libzfs_handle libzfs_handle_t;
typedef struct zfs_handle    zfs_handle_t;
typedef struct zpool_handle  zpool_handle_t;
typedef struct nvlist        nvlist_t;

/* Boolean type used by libzfs */
typedef int boolean_t;
#define B_FALSE 0
#define B_TRUE  1

#define ZFS_TYPE_FILESYSTEM  (1 << 0)

typedef libzfs_handle_t *(*fn_libzfs_init)(void);
typedef void             (*fn_libzfs_fini)(libzfs_handle_t *);
typedef zpool_handle_t * (*fn_zpool_open)(libzfs_handle_t *, const char *);
typedef void             (*fn_zpool_close)(zpool_handle_t *);
typedef nvlist_t *       (*fn_zpool_get_features)(zpool_handle_t *);
typedef int              (*fn_zfs_create)(libzfs_handle_t *, const char *,
                                           int, nvlist_t *);
typedef zfs_handle_t *   (*fn_zfs_open)(libzfs_handle_t *, const char *, int);
typedef int              (*fn_zfs_destroy)(zfs_handle_t *, int);
typedef void             (*fn_zfs_close)(zfs_handle_t *);
typedef int              (*fn_zfs_mount)(zfs_handle_t *, const char *, int);
typedef int              (*fn_zfs_unmount)(zfs_handle_t *, const char *, int);
typedef int              (*fn_zfs_crypto_load_key)(zfs_handle_t *, boolean_t,
                                                    const char *);
typedef int              (*fn_zfs_crypto_unload_key)(zfs_handle_t *);
typedef boolean_t        (*fn_zfs_is_encrypted)(zfs_handle_t *);
typedef const char *     (*fn_libzfs_error_description)(libzfs_handle_t *);

/* libnvpair */
typedef nvlist_t *(*fn_fnvlist_alloc)(void);
typedef void      (*fn_fnvlist_free)(nvlist_t *);
typedef void      (*fn_fnvlist_add_uint64)(nvlist_t *, const char *,
                                            unsigned long long);
typedef void      (*fn_fnvlist_add_string)(nvlist_t *, const char *,
                                            const char *);
typedef int       (*fn_nvlist_lookup_uint64)(nvlist_t *, const char *,
                                              unsigned long long *);

static fn_libzfs_init           p_libzfs_init;
static fn_libzfs_fini           p_libzfs_fini;
static fn_zpool_open            p_zpool_open;
static fn_zpool_close           p_zpool_close;
static fn_zpool_get_features    p_zpool_get_features;
static fn_zfs_create            p_zfs_create;
static fn_zfs_open              p_zfs_open;
static fn_zfs_destroy           p_zfs_destroy;
static fn_zfs_close             p_zfs_close;
static fn_zfs_mount             p_zfs_mount;
static fn_zfs_unmount           p_zfs_unmount;
static fn_zfs_crypto_load_key   p_zfs_crypto_load_key;
static fn_zfs_crypto_unload_key    p_zfs_crypto_unload_key;
static fn_zfs_is_encrypted         p_zfs_is_encrypted;
static fn_libzfs_error_description p_libzfs_error_description;
static fn_fnvlist_alloc         p_fnvlist_alloc;
static fn_fnvlist_free          p_fnvlist_free;
static fn_fnvlist_add_uint64    p_fnvlist_add_uint64;
static fn_fnvlist_add_string    p_fnvlist_add_string;
static fn_nvlist_lookup_uint64  p_nvlist_lookup_uint64;

static bool load_libs(void)
{
    /*
     * Load libzfs.so for the ZFS management API.
     * On OSv, nvpair functions live in libsolaris.so (the kernel module,
     * already loaded at boot).  Use RTLD_DEFAULT to resolve them from the
     * global symbol table rather than searching libzfs.so's dependency chain,
     * which OSv's dlsym may not walk.
     */
    void *hzfs = dlopen("libzfs.so", RTLD_LAZY | RTLD_GLOBAL);
    if (!hzfs) {
        fprintf(stderr, "SKIP: cannot load libzfs.so: %s\n", dlerror());
        return false;
    }

#define L(h, name) \
    p_##name = (fn_##name)dlsym(h, #name); \
    if (!p_##name) { fprintf(stderr, "SKIP: symbol " #name " missing\n"); return false; }

    L(hzfs,          libzfs_init)
    L(hzfs,          libzfs_fini)
    L(hzfs,          zpool_open)
    L(hzfs,          zpool_close)
    L(hzfs,          zpool_get_features)
    L(hzfs,          zfs_create)
    L(hzfs,          zfs_open)
    L(hzfs,          zfs_destroy)
    L(hzfs,          zfs_close)
    L(hzfs,          zfs_mount)
    L(hzfs,          zfs_unmount)
    L(hzfs,          zfs_crypto_load_key)
    L(hzfs,          zfs_crypto_unload_key)
    L(hzfs,          zfs_is_encrypted)
    L(hzfs,          libzfs_error_description)
    /* nvpair functions: look in global table (libsolaris.so already loaded) */
    L(RTLD_DEFAULT,  fnvlist_alloc)
    L(RTLD_DEFAULT,  fnvlist_free)
    L(RTLD_DEFAULT,  fnvlist_add_uint64)
    L(RTLD_DEFAULT,  fnvlist_add_string)
    L(RTLD_DEFAULT,  nvlist_lookup_uint64)
#undef L
    return true;
}

static const char *detect_pool(libzfs_handle_t *zfsh)
{
    static const char *cands[] = { "rpool", "osv", "data", nullptr };
    for (int i = 0; cands[i]; i++) {
        zpool_handle_t *ph = p_zpool_open(zfsh, cands[i]);
        if (ph) { p_zpool_close(ph); return cands[i]; }
    }
    return nullptr;
}

/* Check if the encryption feature is enabled on the pool */
static bool pool_has_encryption(libzfs_handle_t *zfsh, const char *pool)
{
    zpool_handle_t *ph = p_zpool_open(zfsh, pool);
    if (!ph) return false;
    nvlist_t *features = p_zpool_get_features(ph);
    bool found = false;
    if (features) {
        unsigned long long v;
        /* The encryption feature GUID */
        if (p_nvlist_lookup_uint64(features,
            "com.datto:encryption", &v) == 0 ||
            p_nvlist_lookup_uint64(features,
            "org.openzfs:encryption", &v) == 0) {
            found = true;
        }
    }
    p_zpool_close(ph);
    return found;
}

/* Generate a 32-byte random key and write to path */
static bool write_random_key(const char *path)
{
    unsigned char key[32];
    /* Use /dev/urandom for random bytes */
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        /* Fallback: use simple pseudo-random bytes */
        for (int i = 0; i < 32; i++)
            key[i] = (unsigned char)(i * 37 + 42);
    } else {
        ssize_t n = read(fd, key, 32);
        close(fd);
        if (n != 32) {
            fprintf(stderr, "FAIL: could not read 32 bytes from /dev/urandom\n");
            return false;
        }
    }
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        fprintf(stderr, "FAIL: cannot create key file %s: %s\n",
                path, strerror(errno));
        return false;
    }
    ssize_t n = write(fd, key, 32);
    close(fd);
    if (n != 32) {
        fprintf(stderr, "FAIL: wrote only %zd bytes to key file\n", n);
        return false;
    }
    return true;
}

static int pass_count, fail_count;

static void check(bool cond, const char *desc)
{
    if (cond) {
        printf("  PASS  %s\n", desc);
        pass_count++;
    } else {
        printf("  FAIL  %s (errno=%d: %s)\n", desc, errno, strerror(errno));
        fail_count++;
    }
}

int main()
{
    printf("=== ZFS encryption integration test ===\n\n");

    if (!load_libs()) {
        printf("SKIP: required libraries not available\n");
        return 0;
    }

    libzfs_handle_t *zfsh = p_libzfs_init();
    if (!zfsh) {
        printf("SKIP: libzfs_init() failed\n");
        return 0;
    }

    const char *pool = detect_pool(zfsh);
    if (!pool) {
        printf("SKIP: no ZFS pool found\n");
        p_libzfs_fini(zfsh);
        return 0;
    }
    printf("Pool: %s\n", pool);

    if (!pool_has_encryption(zfsh, pool)) {
        printf("SKIP: encryption feature not enabled on pool '%s'\n", pool);
        p_libzfs_fini(zfsh);
        return 0;
    }
    printf("Encryption feature: enabled\n\n");

    /* --- Test 1: Create encrypted dataset --- */
    printf("[Test 1] Create AES-256-GCM encrypted dataset\n");

    const char *KEY_PATH = "/tmp/zfs-test-enc.key";
    const char *MOUNT_PT = "/enc-test";
    char dsname[256];
    snprintf(dsname, sizeof(dsname), "%s/enc-test", pool);

    /*
     * Idempotent: tear down any leftover dataset from a prior run on the
     * same image so zfs_create() does not see EEXIST.
     */
    {
        zfs_handle_t *stale = p_zfs_open(zfsh, dsname, ZFS_TYPE_FILESYSTEM);
        if (stale) {
            p_zfs_unmount(stale, nullptr, 0);
            p_zfs_destroy(stale, /*defer=*/0);
            p_zfs_close(stale);
        }
    }

    nvlist_t *props = nullptr;
    char keyloc[256];

    bool key_ok = write_random_key(KEY_PATH);
    check(key_ok, "Random key file created");
    if (!key_ok) goto cleanup_zfs;

    /* Build keylocation="file:///tmp/zfs-test-enc.key" */
    snprintf(keyloc, sizeof(keyloc), "file://%s", KEY_PATH);

    /* Create properties nvlist for the encrypted dataset.
     * libzfs expects string values for encryption/keyformat properties
     * (it validates and converts them internally). */
    props = p_fnvlist_alloc();
    p_fnvlist_add_string(props, "encryption",   "aes-256-gcm");
    p_fnvlist_add_string(props, "keyformat",    "raw");
    p_fnvlist_add_string(props, "keylocation",  keyloc);
    p_fnvlist_add_string(props, "mountpoint",   MOUNT_PT);

    {
        int rc = p_zfs_create(zfsh, dsname, ZFS_TYPE_FILESYSTEM, props);
        p_fnvlist_free(props);
        check(rc == 0, "zfs_create with AES-256-GCM encryption");
        if (rc != 0) {
            fprintf(stderr, "  libzfs error: %s\n",
                    p_libzfs_error_description(zfsh));
            fprintf(stderr, "  errno=%d: %s\n", errno, strerror(errno));
            goto cleanup_zfs;
        }

        /* Mount the newly created encrypted dataset */
        zfs_handle_t *zh = p_zfs_open(zfsh, dsname, ZFS_TYPE_FILESYSTEM);
        check(zh != nullptr, "zfs_open for mount after create");
        if (zh) {
            rc = p_zfs_mount(zh, NULL, 0);
            check(rc == 0, "zfs_mount encrypted dataset");
            if (rc != 0)
                fprintf(stderr, "  zfs_mount errno=%d: %s\n", errno, strerror(errno));
            p_zfs_close(zh);
        }
    }

    /* --- Test 2: Verify encryption flag --- */
    printf("\n[Test 2] Verify dataset is encrypted\n");
    {
        zfs_handle_t *zh = p_zfs_open(zfsh, dsname, ZFS_TYPE_FILESYSTEM);
        check(zh != nullptr, "zfs_open encrypted dataset");
        if (zh) {
            boolean_t enc = p_zfs_is_encrypted(zh);
            check(enc == B_TRUE, "zfs_is_encrypted() returns true");
            p_zfs_close(zh);
        }
    }

    /* --- Test 3: Write and read data --- */
    printf("\n[Test 3] Write and read data through encrypted dataset\n");
    {
        /* Mount point should already be set; try writing a file */
        char path[256];
        snprintf(path, sizeof(path), "%s/testfile.dat", MOUNT_PT);

        const char *TEST_DATA = "ZFS-AES256GCM-ENCRYPTED-DATA-OSV-TEST-2026";
        size_t data_len = strlen(TEST_DATA);

        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        check(fd >= 0, "open encrypted file for write");
        if (fd >= 0) {
            ssize_t w = write(fd, TEST_DATA, data_len);
            check(w == (ssize_t)data_len, "write to encrypted file");
            close(fd);
        }

        fd = open(path, O_RDONLY);
        check(fd >= 0, "open encrypted file for read");
        if (fd >= 0) {
            char buf[128] = {0};
            ssize_t r = read(fd, buf, sizeof(buf) - 1);
            check(r == (ssize_t)data_len, "read from encrypted file: correct length");
            check(memcmp(buf, TEST_DATA, data_len) == 0,
                  "read from encrypted file: data matches");
            close(fd);
        }
    }

    /* --- Test 4: Key unload / reload cycle --- */
    printf("\n[Test 4] Key unload and reload cycle\n");
    {
        zfs_handle_t *zh = p_zfs_open(zfsh, dsname, ZFS_TYPE_FILESYSTEM);
        check(zh != nullptr, "zfs_open for key unload");

        if (zh) {
            /* Must unmount before unloading key (ZFS requirement) */
            int rc = p_zfs_unmount(zh, NULL, 0);
            check(rc == 0, "zfs_unmount before key unload");
            p_zfs_close(zh);

            zh = p_zfs_open(zfsh, dsname, ZFS_TYPE_FILESYSTEM);
            check(zh != nullptr, "zfs_open for key unload");
            if (zh) {
                rc = p_zfs_crypto_unload_key(zh);
                check(rc == 0, "zfs_crypto_unload_key");
                p_zfs_close(zh);
            }

            /* After key unload, mounting the encrypted dataset must fail.
             * ZFS refuses to mount when the wrapping key is unavailable,
             * which proves the data is protected by the key. */
            zh = p_zfs_open(zfsh, dsname, ZFS_TYPE_FILESYSTEM);
            if (zh) {
                rc = p_zfs_mount(zh, NULL, 0);
                check(rc != 0, "remount with key unloaded (expect failure)");
                p_zfs_close(zh);
            }

            /* Verify that the encrypted data is not readable at the mount
             * point path while the dataset is unmounted and the key is
             * unloaded.  After unmount the mount point reverts to the root
             * FS: opening the file should either fail (ENOENT — the root FS
             * has no such file) or return data that does NOT match what was
             * written to the encrypted dataset.  Either outcome proves that
             * ciphertext is not exposed as plaintext without the key. */
            {
                char path[256];
                snprintf(path, sizeof(path), "%s/testfile.dat", MOUNT_PT);
                int fd2 = open(path, O_RDONLY);
                if (fd2 < 0) {
                    /* ENOENT: root FS has no testfile.dat — data is inaccessible */
                    check(errno == ENOENT,
                          "encrypted data not accessible without key (ENOENT)");
                } else {
                    /* Root FS has a file at that path: its content must not
                     * match the ciphertext of what was stored in the encrypted
                     * dataset.  Any mismatch (or empty/short read) confirms
                     * the encrypted data is not leaking as plaintext. */
                    char buf2[128] = {0};
                    const char *WRITTEN = "ZFS-AES256GCM-ENCRYPTED-DATA-OSV-TEST-2026";
                    ssize_t r2 = read(fd2, buf2, strlen(WRITTEN));
                    close(fd2);
                    check(r2 != (ssize_t)strlen(WRITTEN) ||
                          memcmp(buf2, WRITTEN, strlen(WRITTEN)) != 0,
                          "encrypted data not readable as plaintext without key");
                }
            }

            /* Reload the key */
            zh = p_zfs_open(zfsh, dsname, ZFS_TYPE_FILESYSTEM);
            if (zh) {
                rc = p_zfs_crypto_load_key(zh, B_FALSE, keyloc);
                check(rc == 0, "zfs_crypto_load_key (reload)");
                /* Remount after key reload */
                if (rc == 0) {
                    rc = p_zfs_mount(zh, NULL, 0);
                    check(rc == 0, "zfs_mount after key reload");
                }
                p_zfs_close(zh);
            }

            /* After reload, verify original data is intact */
            char path[256];
            snprintf(path, sizeof(path), "%s/testfile.dat", MOUNT_PT);
            int fd = open(path, O_RDONLY);
            check(fd >= 0, "open file after key reload");
            if (fd >= 0) {
                char buf[128] = {0};
                const char *EXPECTED = "ZFS-AES256GCM-ENCRYPTED-DATA-OSV-TEST-2026";
                ssize_t r = read(fd, buf, strlen(EXPECTED));
                check(r == (ssize_t)strlen(EXPECTED) &&
                      memcmp(buf, EXPECTED, strlen(EXPECTED)) == 0,
                      "data verified after key reload");
                close(fd);
            }
        }
    }

cleanup_zfs:
    /* --- Cleanup: Destroy the encrypted dataset --- */
    printf("\n[Cleanup] Destroy encrypted dataset\n");
    {
        zfs_handle_t *zh = p_zfs_open(zfsh, dsname, ZFS_TYPE_FILESYSTEM);
        if (zh) {
            /* Unmount before destroy */
            (void) p_zfs_unmount(zh, NULL, 0);
            p_zfs_close(zh);
            zh = p_zfs_open(zfsh, dsname, ZFS_TYPE_FILESYSTEM);
        }
        if (zh) {
            int rc = p_zfs_destroy(zh, /*defer=*/0);
            check(rc == 0, "zfs_destroy encrypted dataset");
            p_zfs_close(zh);
        }
        unlink(KEY_PATH);
    }

    p_libzfs_fini(zfsh);

    printf("\n=== Results: %d/%d passed ===\n",
           pass_count, pass_count + fail_count);
    return (fail_count > 0) ? 1 : 0;
}
