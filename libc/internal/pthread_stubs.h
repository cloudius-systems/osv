

/* until we get proper pthread cancellation support */
#undef pthread_cleanup_push
#define pthread_cleanup_push(c, f)	do { } while (0)

#undef pthread_cleanup_pop
#define pthread_cleanup_pop(n	)	do { } while (0)

