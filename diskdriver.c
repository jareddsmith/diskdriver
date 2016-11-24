#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#include "diskdriver.h"
#include "BoundedBuffer.h"
#include "sectordescriptorcreator.h"
#include "freesectordescriptorstore_full.h"

#define BUFF_SIZE 1024

pthread_t readThread, writeThread;
BoundedBuffer * readBuffer, * writeBuffer;

FreeSectorDescriptorStore * free_secs;
DiskDevice * disk_dev;

struct voucher {
	int status;			/* Status of the voucher */
	int type;			/* 0 for read, 1 for write */
	pthread_mutex_t vMutex;		/* Ensures the sector descriptor is safe from other threads */
	pthread_cond_t vCond;		/* Thread control with waits */
	SectorDescriptor * sDesc;	/* Sector descriptor to be used */
};

static void * runRead(){
	while (1){
		Voucher * vouch = (Voucher *) blockingReadBB(readBuffer);	/* Assigns the voucher with one from the read buffer */
		pthread_mutex_lock(&(vouch->vMutex));				/* Locks the voucher from other threads */
		vouch->status = read_sector(disk_dev, vouch->sDesc);		/* Reads the sector descriptor */
		pthread_cond_signal(&(vouch->vCond));				/* Signals the thread to wait */
		pthread_mutex_unlock(&(vouch->vMutex));				/* Unlocks the voucher for other threads */
	}
	return 0;
}

static void * runWrite(){
	while (1){
		Voucher * vouch = (Voucher *) blockingReadBB(writeBuffer);	/* Assigns the voucher with one from the write buffer */
		pthread_mutex_lock(&(vouch->vMutex));				/* Locks the voucher from other threads */
		vouch->status = write_sector(disk_dev, vouch->sDesc);		/* Writes the sector descriptor */
		pthread_cond_signal(&(vouch->vCond));				/* Signals the thread to wait */
		pthread_mutex_unlock(&(vouch->vMutex));				/* Unlocks the voucher for other threads */
	}
	return 0;
}

void init_disk_driver(DiskDevice *dd, void *mem_start, unsigned long mem_length, FreeSectorDescriptorStore **fsds){
	/* return the FSDS to the code that called you */
	disk_dev = dd;		/* Assigns the disk device to the global variable */
	*fsds = create_fsds();	/* Creates a Free Sector Descriptor Store for the passed in store */
	free_secs = *fsds;	/* Assigns the Free Sector Descriptor Store to the global variable */
	
	/* load FSDS with packet descriptors constructed from mem_start/mem_length */
	create_free_sector_descriptors(*fsds, mem_start, mem_length);
	
	/* create any buffers required by your thread[s] */
	readBuffer = createBB(BUFF_SIZE);
	writeBuffer = createBB(BUFF_SIZE);
	
	/* create any threads you require for your implementation */
	pthread_create(&readThread, NULL, &runRead, NULL);
	pthread_create(&writeThread, NULL, &runWrite, NULL);
}

static void * init_vouch(int type, SectorDescriptor *sd, Voucher **v){
	Voucher * vouch = (Voucher *) malloc(sizeof(Voucher));	/* Initialize a new voucher */
	vouch->status = -1;					/* Initial status of voucher */
	vouch->type = type;					/* Designates the voucher with the type given */	
	pthread_mutex_init(&(vouch->vMutex), NULL);		/* Initialize the voucher's mutex */
	pthread_cond_init(&(vouch->vCond), NULL);		/* Initialize the voucher's condition */
	vouch->sDesc = sd;					/* Initialize the voucher's sector desc. */
								/* as the one passed in */
	*v = (Voucher *) vouch;					/* Assigns the newly created voucher to the */
								/* voucher passed in by the function */
	return 0;
} 

void blocking_write_sector(SectorDescriptor *sd, Voucher **v){
	/* Queue up sector descriptor for a blocking write */
	/* return a Voucher through *v for eventual synchronization by application */
 	/* do not return until it has been successfully queued */
	init_vouch(1, sd, v);
	blockingWriteBB(writeBuffer, *v);		/* Adds a blocking write to the write buffer */
}

int nonblocking_write_sector(SectorDescriptor *sd, Voucher **v){
	/* Queue up sector descriptor for a non-blocking write */
	/* Initialize a new voucher */
	Voucher * vouch = (Voucher *) malloc(sizeof(Voucher));
	
	vouch->status = -1;				/* Initial status of voucher */
	vouch->type = 1;				/* Designates the voucher for writing */
	pthread_mutex_init(&(vouch->vMutex), NULL);	/* Initialize the voucher's mutex */
	pthread_cond_init(&(vouch->vCond), NULL);	/* Initialize the voucher's condition */
	vouch->sDesc = sd;				/* Initialize the voucher's sector desc. */
							/* as the one passed in */								
	
	/* if you are able to queue up sector descriptor immediately */
 	/* return a Voucher through *v and return 1 */
 	/* otherwise, return 0 */
 	
	*v = (Voucher *) vouch;				/* Assigns the newly created voucher to the */
							/* voucher passed in by the function */
												
	return nonblockingWriteBB(writeBuffer, *v);	/* Adds a non-blocking write to the write buffer */
}

void blocking_read_sector(SectorDescriptor *sd, Voucher **v){
	/* Queue up sector descriptor for a blocking read */
	/* Initialize a new voucher */
	Voucher * vouch = (Voucher *) malloc(sizeof(Voucher));
	
	vouch->status = -1;				/* Initial status of voucher */
	vouch->type = 0;				/* Designates the voucher for reading */
	pthread_mutex_init(&(vouch->vMutex), NULL);	/* Initialize the voucher's mutex */
	pthread_cond_init(&(vouch->vCond), NULL);	/* Initialize the voucher's condition */
	vouch->sDesc = sd;				/* Initialize the voucher's sector desc. */
							/* as the one passed in */
	
	/* return a Voucher through *v for eventual synchronization by application */
	/* do not return until it has been successfully queued */
	
	*v = (Voucher *) vouch;				/* Assigns the newly created voucher to the */
							/* voucher passed in by the function */
	
	blockingWriteBB(readBuffer, *v);		/* Adds a blocking read to the read buffer */
}

int nonblocking_read_sector(SectorDescriptor *sd, Voucher **v){
	/* Queue up sector descriptor for a non-blocking read */
	/* Initialize a new voucher */
	Voucher * vouch = (Voucher *) malloc(sizeof(Voucher));
	
	vouch->status = -1; 				/* Initial status of voucher */
	vouch->type = 1; 				/* Designates the voucher for reading */
	pthread_mutex_init(&(vouch->vMutex), NULL);	/* Initialize the voucher's mutex */
	pthread_cond_init(&(vouch->vCond), NULL);	/* Initialize the voucher's condition */
	vouch->sDesc = sd; 				/* Initialize the voucher's sector desc. */
							/* as the one passed in */
	
	/* if you are able to queue up sector descriptor immediately */
 	/* return a Voucher through *v and return 1 */
 	/* otherwise, return 0 */
	*v = (Voucher *) vouch; 			/* Assigns the newly created voucher to the */
							/* voucher passed in by the function */
	
	return nonblockingWriteBB(readBuffer, *v);	/* Adds a non-blocking read to the read buffer */
}

/*
* the following call is used to retrieve the status of the read or write
* the return value is 1 if successful, 0 if not
* the calling application is blocked until the read/write has completed
* if a successful read, the associated SectorDescriptor is returned in *sd
*/
int redeem_voucher(Voucher *v, SectorDescriptor **sd){
	Voucher * vouch = (Voucher *) v;
	
	pthread_mutex_lock(&(vouch->vMutex));
	
	while (vouch->status == -1) pthread_cond_wait(&(vouch->vCond), &(vouch->vMutex));
	if (vouch->type == 1) blocking_put_sd(free_secs, vouch->sDesc);
	else if (vouch->type == 0) *sd = vouch->sDesc;
	
	int ret = vouch->status;
	pthread_mutex_unlock(&(vouch->vMutex));
	pthread_mutex_destroy(&(vouch->vMutex));
	pthread_cond_destroy(&(vouch->vCond));
	
	free(vouch);
	return ret;
}
