
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <osv/device.h>
#include <osv/bio.h>

struct bio *
alloc_bio(void)
{
	struct bio *bio = malloc(sizeof(*bio));
	if (!bio)
		return NULL;
	memset(bio, 0, sizeof(*bio));

	list_init(&bio->bio_list);
	pthread_mutex_init(&bio->bio_mutex, NULL);
	pthread_cond_init(&bio->bio_wait, NULL);
	return bio;
}

void
destroy_bio(struct bio *bio)
{
	pthread_cond_destroy(&bio->bio_wait);
//	pthread_mutex_destroy(&bio->bio_mutex);
	free(bio);
}

void
biodone(struct bio *bio)
{
	void (*bio_done)(struct bio *);

	pthread_mutex_lock(&bio->bio_mutex);
	bio->bio_flags |= BIO_DONE;
	bio_done = bio->bio_done;
	if (!bio_done) {
		pthread_cond_signal(&bio->bio_wait);
		pthread_mutex_unlock(&bio->bio_mutex);
	} else {
		pthread_mutex_unlock(&bio->bio_mutex);
		bio_done(bio);
	}
}
