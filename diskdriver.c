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
	int status;
	int type;
	pthread_mutex_t vMutex;
	pthread_cond_t vCond;
	SectorDescriptor * sDesc; 
};

static void * runRead(){
	while (1){
		Voucher * vouch = (Voucher *) blockingReadBB(readBuffer);
		vouch->status = read_sector(disk_dev, vouch->sDesc);
		pthread_mutex_lock(&(vouch->vMutex));
		pthread_mutex_unlock(&(vouch->vMutex));
		pthread_cond_signal(&(vouch->vCond));
	}
	return 0;
}

static void * runWrite(){
	while (1){
		Voucher * vouch = (Voucher *) blockingReadBB(writeBuffer);
		vouch->status = write_sector(disk_dev, vouch->sDesc);
		pthread_mutex_lock(&(vouch->vMutex));
		pthread_mutex_unlock(&(vouch->vMutex));
		pthread_cond_signal(&(vouch->vCond));
	}
	return 0;
}

void init_disk_driver(DiskDevice *dd, void *mem_start, unsigned long mem_length, FreeSectorDescriptorStore **fsds){
	/* create Free Sector Descriptor Store */
	/* return the FSDS to the code that called you */
	disk_dev = dd;
	*fsds = create_fsds();
	free_secs = *fsds;
	
	/* load FSDS with packet descriptors constructed from mem_start/mem_length */
	create_free_sector_descriptors(*fsds, mem_start, mem_length);
	
	/* create any buffers required by your thread[s] */
	readBuffer = createBB(BUFF_SIZE);
	writeBuffer = createBB(BUFF_SIZE);
	
	/* create any threads you require for your implementation */
	pthread_create(&readThread, NULL, &runRead, NULL);
	pthread_create(&writeThread, NULL, &runWrite, NULL);
}

void blocking_write_sector(SectorDescriptor *sd, Voucher **v){
	/* queue up sector descriptor for writing */
	Voucher * vouch = (Voucher *) malloc(sizeof(Voucher));
	
	vouch->status = -1;
	vouch->type = 1;
	pthread_mutex_init(&(vouch->vMutex), NULL);
	pthread_cond_init(&(vouch->vCond), NULL);
	vouch->sDesc = sd;
	
	/* return a Voucher through *v for eventual synchronization by application */
 	/* do not return until it has been successfully queued */
	*v = (Voucher *) vouch;
	blockingWriteBB(writeBuffer, *v);
}

int nonblocking_write_sector(SectorDescriptor *sd, Voucher **v){
	Voucher * vouch = (Voucher *) malloc(sizeof(Voucher));
	
	vouch->status = -1;
	vouch->type = 1;
	pthread_mutex_init(&(vouch->vMutex), NULL);
	pthread_cond_init(&(vouch->vCond), NULL);
	vouch->sDesc = sd;
	
	/* if you are able to queue up sector descriptor immediately */
 	/* return a Voucher through *v and return 1 */
 	/* otherwise, return 0 */
	*v = (Voucher *) vouch;
	return nonblockingWriteBB(writeBuffer, *v);
}

void blocking_read_sector(SectorDescriptor *sd, Voucher **v){
	/* queue up sector descriptor for reading */
	Voucher * vouch = (Voucher *) malloc(sizeof(Voucher));
	
	vouch->status = -1;
	vouch->type = 0;
	pthread_mutex_init(&(vouch->vMutex), NULL);
	pthread_cond_init(&(vouch->vCond), NULL);
	vouch->sDesc = sd;
	
	/* return a Voucher through *v for eventual synchronization by application */
	/* do not return until it has been successfully queued */
	*v = (Voucher *) vouch;
	blockingWriteBB(readBuffer, *v);
}

int nonblocking_read_sector(SectorDescriptor *sd, Voucher **v){
	Voucher * vouch = (Voucher *) malloc(sizeof(Voucher));
	
	vouch->status = -1;
	vouch->type = 1;
	pthread_mutex_init(&(vouch->vMutex), NULL);
	pthread_cond_init(&(vouch->vCond), NULL);
	vouch->sDesc = sd;
	
	/* if you are able to queue up sector descriptor immediately */
 	/* return a Voucher through *v and return 1 */
 	/* otherwise, return 0 */
	*v = (Voucher *) vouch;
	return nonblockingWriteBB(readBuffer, *v);
}

int redeem_voucher(Voucher *v, SectorDescriptor **sd){
	Voucher * vouch = (Voucher *) v;
	
	if (vouch->status == -1) pthread_cond_wait(&(vouch->vCond), &(vouch->vMutex));
	if (vouch->type == 1) blocking_put_sd(free_secs, vouch->sDesc);
	if (vouch->type == 0) *sd = vouch->sDesc;
	
	int ret = vouch->status;
	pthread_mutex_unlock(&(vouch->vMutex));
	pthread_mutex_destroy(&(vouch->vMutex));
	pthread_cond_destroy(&(vouch->vCond));
	
	free(vouch);
	return ret;
}
