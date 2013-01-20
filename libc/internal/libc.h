
#undef weak_alias
#define weak_alias(old, new) \
	extern __typeof(old) new __attribute__((weak, alias(#old)))

#define ATTR_LIBC_VISIBILITY __attribute__((visibility("hidden")))

/* TODO: add real locking */
#define LOCK(x) ((void)(x))
#define UNLOCK(x) ((void)(x))

#undef LFS64_2
#define LFS64_2(x, y) weak_alias(x, y)

#undef LFS64
#define LFS64(x) LFS64_2(x, x##64)
