
#undef weak_alias
#define weak_alias(old, new) \
	extern __typeof(old) new __attribute__((weak, alias(#old)))

#define ATTR_LIBC_VISIBILITY __attribute__((visibility("hidden")))

/* TODO: add real locking */
#define LOCK(x) ((void)(x))
#define UNLOCK(x) ((void)(x))
