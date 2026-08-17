// SPDX-License-Identifier: CDDL-1.0
/*
 * ZFS encryption OS-specific stubs for OSv.
 *
 * NOTE: This file is NOT compiled.  It is kept as a reference only.
 *
 * OSv ZFS encryption is fully implemented via zio_crypt_impl.c, which is
 * an OSv-adapted copy of the Linux port (module/os/linux/zfs/zio_crypt.c).
 * That file provides the real AES-256-GCM encrypt/decrypt pipeline using
 * the ICP (Illumos Crypto Provider) - the same crypto layer already
 * compiled for ZFS checksumming (module/icp/).
 *
 * zio_crypt_impl.c is listed in openzfs_sources.mk under openzfs-osv and
 * replaces the entire role of both this file and the platform-independent
 * module/zfs/zio_crypt.c.  The OSv adaptations relative to the Linux port
 * are described at the top of zio_crypt_impl.c.
 *
 * The stubs below (returning ENOTSUP) were created during the initial port
 * to satisfy the linker before the real implementation existed.  They are
 * superseded by zio_crypt_impl.c and must NOT be added to the build.
 */

#include <sys/types.h>
#include <sys/zio_crypt.h>
#include <sys/abd.h>
#include <sys/errno.h>
#include <string.h>

/* ---- key lifecycle ---- */

void
zio_crypt_key_destroy(zio_crypt_key_t *key)
{
	(void) key;
}

int
zio_crypt_key_init(uint64_t crypt, zio_crypt_key_t *key)
{
	(void) crypt; (void) key;
	return (SET_ERROR(ENOTSUP));
}

int
zio_crypt_key_get_salt(zio_crypt_key_t *key, uint8_t *salt_out)
{
	(void) key; (void) salt_out;
	return (SET_ERROR(ENOTSUP));
}

int
zio_crypt_key_wrap(crypto_key_t *cwkey, zio_crypt_key_t *key, uint8_t *iv,
    uint8_t *mac, uint8_t *keydata_out, uint8_t *hmac_keydata_out)
{
	(void) cwkey; (void) key; (void) iv; (void) mac;
	(void) keydata_out; (void) hmac_keydata_out;
	return (SET_ERROR(ENOTSUP));
}

int
zio_crypt_key_unwrap(crypto_key_t *cwkey, uint64_t crypt, uint64_t version,
    uint64_t guid, uint8_t *keydata, uint8_t *hmac_keydata, uint8_t *iv,
    uint8_t *mac, zio_crypt_key_t *key)
{
	(void) cwkey; (void) crypt; (void) version; (void) guid;
	(void) keydata; (void) hmac_keydata; (void) iv; (void) mac; (void) key;
	return (SET_ERROR(ENOTSUP));
}

/* ---- IV / salt generation ---- */

int
zio_crypt_generate_iv(uint8_t *ivbuf)
{
	(void) ivbuf;
	return (SET_ERROR(ENOTSUP));
}

int
zio_crypt_generate_iv_salt_dedup(zio_crypt_key_t *key, uint8_t *data,
    uint_t datalen, uint8_t *ivbuf, uint8_t *salt)
{
	(void) key; (void) data; (void) datalen; (void) ivbuf; (void) salt;
	return (SET_ERROR(ENOTSUP));
}

/* ---- block-pointer encode / decode (void, zero outputs) ---- */

void
zio_crypt_encode_params_bp(blkptr_t *bp, uint8_t *salt, uint8_t *iv)
{
	(void) bp; (void) salt; (void) iv;
}

void
zio_crypt_decode_params_bp(const blkptr_t *bp, uint8_t *salt, uint8_t *iv)
{
	(void) bp;
	memset(salt, 0, ZIO_DATA_SALT_LEN);
	memset(iv,   0, ZIO_DATA_IV_LEN);
}

void
zio_crypt_encode_mac_bp(blkptr_t *bp, uint8_t *mac)
{
	(void) bp; (void) mac;
}

void
zio_crypt_decode_mac_bp(const blkptr_t *bp, uint8_t *mac)
{
	(void) bp;
	memset(mac, 0, ZIO_DATA_MAC_LEN);
}

void
zio_crypt_encode_mac_zil(void *data, uint8_t *mac)
{
	(void) data; (void) mac;
}

void
zio_crypt_decode_mac_zil(const void *data, uint8_t *mac)
{
	(void) data;
	memset(mac, 0, ZIO_DATA_MAC_LEN);
}

void
zio_crypt_copy_dnode_bonus(abd_t *src_abd, uint8_t *dst, uint_t datalen)
{
	(void) src_abd; (void) dst; (void) datalen;
}

/* ---- MAC / HMAC operations ---- */

int
zio_crypt_do_hmac(zio_crypt_key_t *key, uint8_t *data, uint_t datalen,
    uint8_t *digestbuf, uint_t digestlen)
{
	(void) key; (void) data; (void) datalen; (void) digestbuf;
	(void) digestlen;
	return (SET_ERROR(ENOTSUP));
}

int
zio_crypt_do_objset_hmacs(zio_crypt_key_t *key, void *data, uint_t datalen,
    boolean_t byteswap, uint8_t *portable_mac, uint8_t *local_mac)
{
	(void) key; (void) data; (void) datalen; (void) byteswap;
	(void) portable_mac; (void) local_mac;
	return (SET_ERROR(ENOTSUP));
}

int
zio_crypt_do_indirect_mac_checksum(boolean_t generate, void *buf,
    uint_t datalen, boolean_t byteswap, uint8_t *cksum)
{
	(void) generate; (void) buf; (void) datalen; (void) byteswap;
	(void) cksum;
	return (SET_ERROR(ENOTSUP));
}

int
zio_crypt_do_indirect_mac_checksum_abd(boolean_t generate, abd_t *abd,
    uint_t datalen, boolean_t byteswap, uint8_t *cksum)
{
	(void) generate; (void) abd; (void) datalen; (void) byteswap;
	(void) cksum;
	return (SET_ERROR(ENOTSUP));
}

/* ---- actual encryption / decryption ---- */

int
zio_do_crypt_data(boolean_t encrypt, zio_crypt_key_t *key,
    dmu_object_type_t ot, boolean_t byteswap, uint8_t *salt, uint8_t *iv,
    uint8_t *mac, uint_t datalen, uint8_t *plainbuf, uint8_t *cipherbuf,
    boolean_t *no_crypt)
{
	(void) encrypt; (void) key; (void) ot; (void) byteswap; (void) salt;
	(void) iv; (void) mac; (void) datalen; (void) plainbuf;
	(void) cipherbuf; (void) no_crypt;
	return (SET_ERROR(ENOTSUP));
}

int
zio_do_crypt_abd(boolean_t encrypt, zio_crypt_key_t *key, dmu_object_type_t ot,
    boolean_t byteswap, uint8_t *salt, uint8_t *iv, uint8_t *mac,
    uint_t datalen, abd_t *pabd, abd_t *cabd, boolean_t *no_crypt)
{
	(void) encrypt; (void) key; (void) ot; (void) byteswap; (void) salt;
	(void) iv; (void) mac; (void) datalen; (void) pabd; (void) cabd;
	(void) no_crypt;
	return (SET_ERROR(ENOTSUP));
}
