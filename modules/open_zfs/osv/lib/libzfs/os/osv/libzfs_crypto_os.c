// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv libzfs_crypto_os.c - ZFS encryption key management for OSv.
 *
 * Supports keyformat=raw and keyformat=hex with keylocation=file:///path.
 * Passphrase-based encryption (requires PBKDF2/OpenSSL) is not supported.
 *
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <libzfs.h>
#include <libzfs_core.h>
#include <libzfs_impl.h>

/* Must match WRAPPING_KEY_LEN in zio_crypt.h */
#define	CRYPTO_WRAPPING_KEY_LEN	32

/*
 * Convert a 64-char hex string to 32 raw bytes.
 */
static int
hex_to_raw(const char *hex, uint8_t *out, size_t outlen)
{
	if (strlen(hex) != outlen * 2)
		return (EINVAL);
	for (size_t i = 0; i < outlen; i++) {
		unsigned int hi, lo;
		if (!isxdigit((unsigned char)hex[i * 2]) ||
		    !isxdigit((unsigned char)hex[i * 2 + 1]))
			return (EINVAL);
		(void) sscanf(&hex[i * 2], "%1x%1x", &hi, &lo);
		out[i] = (uint8_t)((hi << 4) | lo);
	}
	return (0);
}

/*
 * Read a wrapping key from keylocation=file:///path.
 * Supports keyformat=raw (32 binary bytes) and keyformat=hex (64 hex chars).
 * Caller must free *key_out.
 */
static int
read_key_from_file(libzfs_handle_t *hdl, const char *keylocation,
    uint64_t keyformat, uint8_t **key_out)
{
	const char *path;
	FILE *f;
	uint8_t *key;
	int ret = 0;

	*key_out = NULL;

	if (strncmp(keylocation, "file://", 7) != 0) {
		zfs_error_aux(hdl, "keylocation must use file:// scheme "
		    "(got '%s')", keylocation);
		return (ENOTSUP);
	}
	path = keylocation + 7;

	f = fopen(path, "re");
	if (f == NULL) {
		ret = errno;
		zfs_error_aux(hdl, "Cannot open key file '%s': %s",
		    path, strerror(ret));
		return (ret);
	}

	key = malloc(CRYPTO_WRAPPING_KEY_LEN);
	if (key == NULL) {
		fclose(f);
		return (ENOMEM);
	}

	if (keyformat == ZFS_KEYFORMAT_RAW) {
		size_t n = fread(key, 1, CRYPTO_WRAPPING_KEY_LEN + 1, f);
		if (n != CRYPTO_WRAPPING_KEY_LEN) {
			zfs_error_aux(hdl, "Key file must be exactly %d bytes "
			    "(got %zu)", CRYPTO_WRAPPING_KEY_LEN, n);
			ret = EINVAL;
			goto error;
		}
	} else if (keyformat == ZFS_KEYFORMAT_HEX) {
		char hexbuf[CRYPTO_WRAPPING_KEY_LEN * 2 + 2];
		size_t n = fread(hexbuf, 1, sizeof (hexbuf) - 1, f);
		hexbuf[n] = '\0';
		if (n > 0 && hexbuf[n - 1] == '\n')
			hexbuf[--n] = '\0';
		ret = hex_to_raw(hexbuf, key, CRYPTO_WRAPPING_KEY_LEN);
		if (ret != 0) {
			zfs_error_aux(hdl, "Invalid hex key in '%s': "
			    "expected %d hex chars", path,
			    CRYPTO_WRAPPING_KEY_LEN * 2);
			goto error;
		}
	} else {
		zfs_error_aux(hdl, "keyformat=passphrase not supported on OSv; "
		    "use keyformat=raw or keyformat=hex with a key file");
		ret = ENOTSUP;
		goto error;
	}

	fclose(f);
	*key_out = key;
	return (0);

error:
	fclose(f);
	free(key);
	return (ret);
}

int
zfs_crypto_get_encryption_root(zfs_handle_t *zhp, boolean_t *is_encroot,
    char *buf)
{
	char prop_encroot[ZFS_MAX_DATASET_NAME_LEN];

	*is_encroot = B_FALSE;

	if (zfs_prop_get_int(zhp, ZFS_PROP_ENCRYPTION) == ZIO_CRYPT_OFF) {
		if (buf != NULL)
			buf[0] = '\0';
		return (0);
	}

	if (zfs_prop_get(zhp, ZFS_PROP_ENCRYPTION_ROOT, prop_encroot,
	    sizeof (prop_encroot), NULL, NULL, 0, B_TRUE) != 0) {
		if (buf != NULL)
			buf[0] = '\0';
		return (0);
	}

	if (buf != NULL)
		(void) strlcpy(buf, prop_encroot, ZFS_MAX_DATASET_NAME_LEN);

	*is_encroot = (strcmp(zfs_get_name(zhp), prop_encroot) == 0);
	return (0);
}

int
zfs_crypto_create(libzfs_handle_t *hdl, char *parent_name, nvlist_t *props,
    nvlist_t *pool_props, boolean_t stdin_available, uint8_t **wkeydata_out,
    uint_t *wkeylen_out)
{
	(void) pool_props; (void) stdin_available;
	uint64_t crypt = ZIO_CRYPT_OFF;
	uint64_t keyformat = ZFS_KEYFORMAT_NONE;
	char *loc_str = NULL;
	uint8_t *key = NULL;
	int ret;

	*wkeydata_out = NULL;
	*wkeylen_out = 0;

	/* Check if encryption is set for this dataset */
	(void) nvlist_lookup_uint64(props,
	    zfs_prop_to_name(ZFS_PROP_ENCRYPTION), &crypt);

	if (crypt == ZIO_CRYPT_OFF) {
		/* Check if parent is encrypted (inheriting) */
		if (parent_name != NULL) {
			zfs_handle_t *parent = zfs_open(hdl, parent_name,
			    ZFS_TYPE_DATASET);
			if (parent != NULL) {
				crypt = zfs_prop_get_int(parent,
				    ZFS_PROP_ENCRYPTION);
				zfs_close(parent);
			}
		}
		if (crypt == ZIO_CRYPT_OFF)
			return (0);  /* unencrypted dataset */
	}

	if (nvlist_lookup_uint64(props,
	    zfs_prop_to_name(ZFS_PROP_KEYFORMAT), &keyformat) != 0 ||
	    keyformat == ZFS_KEYFORMAT_NONE) {
		zfs_error_aux(hdl, "keyformat is required for "
		    "encrypted datasets");
		return (EINVAL);
	}

	if (nvlist_lookup_string(props,
	    zfs_prop_to_name(ZFS_PROP_KEYLOCATION), &loc_str) != 0) {
		zfs_error_aux(hdl, "keylocation is required for "
		    "encrypted datasets");
		return (EINVAL);
	}

	ret = read_key_from_file(hdl, loc_str, keyformat, &key);
	if (ret != 0)
		return (ret);

	*wkeydata_out = key;
	*wkeylen_out = CRYPTO_WRAPPING_KEY_LEN;
	return (0);
}

int
zfs_crypto_clone_check(libzfs_handle_t *hdl, zfs_handle_t *origin_zhp,
    char *parent_name, nvlist_t *props)
{
	(void) hdl; (void) origin_zhp; (void) parent_name; (void) props;
	return (0);
}

int
zfs_crypto_attempt_load_keys(libzfs_handle_t *hdl, const char *fsname)
{
	(void) hdl; (void) fsname;
	return (0);
}

int
zfs_crypto_load_key(zfs_handle_t *zhp, boolean_t noop,
    const char *alt_keylocation)
{
	int ret;
	uint64_t keyformat, keystatus;
	char keylocation[MAXNAMELEN];
	uint8_t *key = NULL;
	boolean_t is_encroot;
	char encroot[ZFS_MAX_DATASET_NAME_LEN];

	keyformat = zfs_prop_get_int(zhp, ZFS_PROP_KEYFORMAT);
	if (keyformat == ZFS_KEYFORMAT_NONE) {
		zfs_error_aux(zhp->zfs_hdl, "'%s' is not encrypted",
		    zfs_get_name(zhp));
		return (EINVAL);
	}

	ret = zfs_crypto_get_encryption_root(zhp, &is_encroot, encroot);
	if (ret != 0 || !is_encroot) {
		zfs_error_aux(zhp->zfs_hdl,
		    "Keys must be loaded for encryption root of '%s' (%s)",
		    zfs_get_name(zhp), encroot);
		return (EINVAL);
	}

	if (!noop) {
		keystatus = zfs_prop_get_int(zhp, ZFS_PROP_KEYSTATUS);
		if (keystatus == ZFS_KEYSTATUS_AVAILABLE) {
			zfs_error_aux(zhp->zfs_hdl,
			    "Key already loaded for '%s'", zfs_get_name(zhp));
			return (EEXIST);
		}
	}

	if (alt_keylocation != NULL) {
		(void) strlcpy(keylocation, alt_keylocation,
		    sizeof (keylocation));
	} else {
		ret = zfs_prop_get(zhp, ZFS_PROP_KEYLOCATION, keylocation,
		    sizeof (keylocation), NULL, NULL, 0, B_TRUE);
		if (ret != 0) {
			zfs_error_aux(zhp->zfs_hdl,
			    "Failed to get keylocation for '%s'",
			    zfs_get_name(zhp));
			return (ret);
		}
	}

	ret = read_key_from_file(zhp->zfs_hdl, keylocation, keyformat, &key);
	if (ret != 0)
		return (ret);

	ret = lzc_load_key(zhp->zfs_name, noop, key, CRYPTO_WRAPPING_KEY_LEN);
	free(key);

	if (ret != 0) {
		zfs_error_aux(zhp->zfs_hdl, "Failed to load key for '%s': %s",
		    zfs_get_name(zhp), strerror(ret));
	}
	return (ret);
}

int
zfs_crypto_unload_key(zfs_handle_t *zhp)
{
	int ret;
	uint64_t keystatus;

	keystatus = zfs_prop_get_int(zhp, ZFS_PROP_KEYSTATUS);
	if (keystatus != ZFS_KEYSTATUS_AVAILABLE) {
		zfs_error_aux(zhp->zfs_hdl, "Key is not loaded for '%s'",
		    zfs_get_name(zhp));
		return (ENOENT);
	}

	ret = lzc_unload_key(zhp->zfs_name);
	if (ret != 0) {
		zfs_error_aux(zhp->zfs_hdl,
		    "Failed to unload key for '%s': %s",
		    zfs_get_name(zhp), strerror(ret));
	}
	return (ret);
}

int
zfs_crypto_rewrap(zfs_handle_t *zhp, nvlist_t *raw_props, boolean_t inheritkey)
{
	(void) zhp; (void) raw_props; (void) inheritkey;
	errno = ENOTSUP;
	return (-1);
}

boolean_t
zfs_is_encrypted(zfs_handle_t *zhp)
{
	return (zfs_prop_get_int(zhp, ZFS_PROP_ENCRYPTION) != ZIO_CRYPT_OFF);
}
