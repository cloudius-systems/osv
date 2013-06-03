
#include <osv/fcntl.h>
#include <sys/dmu.h>
#include <sys/spa.h>
#include <sys/zfs_sa.h>
#include <sys/sa_impl.h>
#include <osv/debug.h>

static int
zfs_space_delta_cb(dmu_object_type_t bonustype, void *data,
    uint64_t *userp, uint64_t *groupp)
{
	int error = 0;

	/*
	 * Is it a valid type of object to track?
	 */
	if (bonustype != DMU_OT_ZNODE && bonustype != DMU_OT_SA)
		return (ENOENT);

	/*
	 * If we have a NULL data pointer
	 * then assume the id's aren't changing and
	 * return EEXIST to the dmu to let it know to
	 * use the same ids
	 */
	if (data == NULL)
		return (EEXIST);

	if (bonustype == DMU_OT_ZNODE) {
		znode_phys_t *znp = data;
		*userp = znp->zp_uid;
		*groupp = znp->zp_gid;
	} else {
		int hdrsize;
		sa_hdr_phys_t *sap = data;
		sa_hdr_phys_t sa = *sap;
		boolean_t swap = B_FALSE;

		ASSERT(bonustype == DMU_OT_SA);

		if (sa.sa_magic == 0) {
			/*
			 * This should only happen for newly created
			 * files that haven't had the znode data filled
			 * in yet.
			 */
			*userp = 0;
			*groupp = 0;
			return (0);
		}
		if (sa.sa_magic == BSWAP_32(SA_MAGIC)) {
			sa.sa_magic = SA_MAGIC;
			sa.sa_layout_info = BSWAP_16(sa.sa_layout_info);
			swap = B_TRUE;
		} else {
			VERIFY3U(sa.sa_magic, ==, SA_MAGIC);
		}

		hdrsize = sa_hdrsize(&sa);
		VERIFY3U(hdrsize, >=, sizeof (sa_hdr_phys_t));
		*userp = *((uint64_t *)((uintptr_t)data + hdrsize +
		    SA_UID_OFFSET));
		*groupp = *((uint64_t *)((uintptr_t)data + hdrsize +
		    SA_GID_OFFSET));
		if (swap) {
			*userp = BSWAP_64(*userp);
			*groupp = BSWAP_64(*groupp);
		}
	}
	return (error);
}

void zfs_init(void *arg)
{
	spa_init(FREAD | FWRITE);
	dmu_objset_register_type(DMU_OST_ZFS, zfs_space_delta_cb);
}
