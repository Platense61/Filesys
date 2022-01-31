#include "crash.h"

static int crash_now, crash_sleep;
static pthread_mutex_t crash_mutex;
static pthread_t crash_thread;

void init_crasher() {
	srand(time(NULL));
	crash_sleep = 5+(int) (50.0*rand()/(RAND_MAX+1.0));

	crash_now = FALSE;

	pthread_mutex_init(&(crash_mutex), NULL);
	if (pthread_create(&(crash_thread), NULL, crash_return, NULL) != 0) {
		fprintf(stderr,"Didn't init crasher thread\n");
	}
	pthread_detach(crash_thread);
}

/*Use this function to read from the disk, instead of reading from it directly*/
ssize_t crash_read(int vdisk, void * buf, size_t num_bytes) {
	pthread_mutex_lock(&(crash_mutex));
	if (crash_now == FALSE) {
		pthread_mutex_unlock(&(crash_mutex));
		return read(vdisk, buf, num_bytes);
	} else {
		pthread_mutex_unlock(&(crash_mutex));
		fprintf(stderr, "Disk read failure!!!!!\n");
		int garbage = open("/dev/urandom", O_RDWR);
		if (garbage != -1) {
			dup2(garbage,vdisk);
		} else {
			close(vdisk);
		}
		read(vdisk, buf, num_bytes);
		return -1;
	}
}

/*Use this function to write to the disk, instead of writing to it directly*/
ssize_t crash_write(int vdisk, void * buf, size_t num_bytes) {
	pthread_mutex_lock(&(crash_mutex));
	if (crash_now == FALSE) {
		pthread_mutex_unlock(&(crash_mutex));
		return write(vdisk, buf, num_bytes);
	} else {
		pthread_mutex_unlock(&(crash_mutex));
		fprintf(stderr, "CRASH!!!!!\n");
		exit(-1);
	}
}

void * crash_return(void * args) {
	fprintf(stderr, "crash sleeping for %d\n", 
		crash_sleep * CRASHES_IN_100);

	sleep(crash_sleep * CRASHES_IN_100);
	
	pthread_mutex_lock(&(crash_mutex));
	crash_now = TRUE;
	pthread_mutex_unlock(&(crash_mutex));

	return NULL;
}
