/*
Family Name:
Given Name:
Section:
Student Number:
CS Login:
*/

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/types.h>

#include "ItemList.h"
#include "log4c.h"

#define NANO_TIME 10000000
int log4c_level = LOG4C_ALL;

typedef struct {
  int key;
  int nIn;
  int nWork;
  int nOut;
  char* in;
  char* out;
  int bufferSize;
} Configuration;

void initializeConfiguration(Configuration* configuration, char** argv) {
  LOG(LOG4C_TRACE, "Read command parameter");
  configuration->key = atoi(argv[1]);
  configuration->nIn = atoi(argv[2]);
  configuration->nWork = atoi(argv[3]);
  configuration->nOut = atoi(argv[4]);
  configuration->in = argv[5];
  configuration->out = argv[6];
  configuration->bufferSize = atoi(argv[7]);
  //TODO validate value
}

void randomSleep(struct timespec* req) {
  LOG(LOG4C_TRACE, "Sleep");
  assert(req != NULL);
  req->tv_nsec = rand() % NANO_TIME;
  nanosleep(req, NULL);
}

typedef struct {
  FILE* in;
  FILE* out;
  ItemList* buffer;
  long fileSize;
  long index[3];
  char state;
  Configuration* configuration;
  pthread_mutex_t readLock;
  pthread_mutex_t writeLock;
  pthread_mutex_t bufferLock;
  pthread_mutex_t indexLock[4];
} Parameter;

void initializeParameter(Parameter* parameter, Configuration* configuration) {
  LOG(LOG4C_TRACE, "Initialize thread parameter");
  assert(parameter != NULL);
  assert(configuration != NULL);
  parameter->configuration = configuration;
  int i;
  for(i = 0; i < 3; ++i) {
    parameter->index[i] = 0;
    pthread_mutex_init(&parameter->index[i], NULL);
  }
  pthread_mutex_init(&parameter->readLock, NULL);
  pthread_mutex_init(&parameter->writeLock, NULL);
  pthread_mutex_init(&parameter->bufferLock, NULL);
  parameter->buffer = createItemList(configuration->bufferSize);
  parameter->in = fopen(configuration->in, "r");
  parameter->out = fopen(configuration->out, "w");

  if (parameter->in == NULL) {
    LOG(LOG4C_ERROR, "Unable to open input file");
    exit(1);
  }
  if (parameter->out == NULL) {
    LOG(LOG4C_ERROR, "Unable to open input file");
    exit(1);
  }
  fseek(parameter->in, 0L, SEEK_END);
  parameter->fileSize = ftell(parameter->in);
  fseek(parameter->in, 0L, SEEK_SET);
}

void encrypt(int key, BufferItem* item) {
  assert(item != NULL);
  LOG(LOG4C_INFO, "Encrypt");
  if(item->state != 'N')
    return;
  if(item->data>31 && item->data<127 )
    item->data = (((int)item->data-32)+2*95+key)%95+32 ;
  item->state = 'E';
}

void decrypt(int key, BufferItem* item) {
  assert(item != NULL);
  LOG(LOG4C_INFO, "Decrypt");
  if(item->state != 'N')
    return;
  if (item->data>31 && item->data<127 )
    item->data = (((int)item->data-32)+2*95-key)%95+32 ;
  item->state = 'D';
}

void doIn(void* p) {
  LOG(LOG4C_INFO, "Start thread IN");
  Parameter* parameter = (Parameter*)p;
  //Each IN thread goes to sleep (use nanosleep) for some random time between 0 and 0.01 seconds upon being created.
  struct timespec req;
  randomSleep(&req);
  while(!feof(parameter->in) && parameter->index[0] < parameter->fileSize) {
    LOG(LOG4C_DEBUG, "Finish in index [%d]", parameter->index[0]);
    int i;
    for(i = 0; i < parameter->buffer->size; ++i) {
      BufferItem* item = parameter->buffer->items[i];
      LOG(LOG4C_DEBUG, "[%c] @ [%d]", item->data, i);
    }
    //Then, it reads the next single byte from the input file
    BufferItem* item = createItem();

    LOG(LOG4C_DEBUG, "Lock input file mutex");
    pthread_mutex_lock(&parameter->readLock);
    item->offset = ftell(parameter->in);
    fscanf(parameter->in, "%c", &item->data);
    LOG(LOG4C_INFO, "Read data [%c]", item->data);
    LOG(LOG4C_DEBUG, "Unlock input file mutex");
    pthread_mutex_unlock(&parameter->readLock);
    //If the buffer is full, IN threads go to sleep (use nanosleep) for some random time between 0 and 0.01 seconds and then go back to check again.
    while(isFull(parameter->buffer)) {
      LOG(LOG4C_TRACE, "Buffer is full");
      randomSleep(&req);
    }
    //and saves that byte and its offset in the file to the next available empty slot in the buffer.
    LOG(LOG4C_DEBUG, "Lock read mutex");
    pthread_mutex_lock(&parameter->indexLock[0]);
    LOG(LOG4C_DEBUG, "Lock buffer mutex");
    pthread_mutex_lock(&parameter->bufferLock);
    LOG(LOG4C_INFO, "Add data [%c] to buffer", item->data);
    addItem(parameter->buffer, item);
    LOG(LOG4C_DEBUG, "Unlock buffer mutex");
    pthread_mutex_unlock(&parameter->bufferLock);
    parameter->index[0]++;
    LOG(LOG4C_DEBUG, "Buffer size [%d]", parameter->buffer->size);
    LOG(LOG4C_DEBUG, "Unlock read mutex");
    pthread_mutex_unlock(&parameter->indexLock[0]);
    //Then, this IN threads goes to sleep (use nanosleep) for some random time between 0 and 0.01 seconds
    randomSleep(&req);
    //and then goes back to read the next byte of the file until the end of file.
  }
  LOG(LOG4C_INFO, "Finish thread IN");
}

void doWork(void* p) {
  LOG(LOG4C_INFO, "Start thread WORK");
  Parameter* parameter = (Parameter*)p;
  //Meanwhile, upon being created each WORK thread sleeps (use nanosleep) for some random time between 0 and 0.01 seconds and it reads next byte in the buffer and process one byte of data, either encrypts or decrypt according to the working mode.
  //Then the WORK thread goes to sleep (use nanosleep) for some random time between 0 and 0.01 seconds and goes back to process next byte in the buffer until the entire file is done.
  struct timespec req;
  randomSleep(&req);

  //If the buffer is empty, the WORK threads go to sleep (use nanosleep) for some random time between 0 and 0.01 seconds and then go back to check again.
  while(parameter->index[1] < parameter->fileSize) {
    LOG(LOG4C_DEBUG, "Finish work index [%d]", parameter->index[1]);
    //If runnIng in the encrypt mode, each WORK thread will encrypt each data byte in the buffer,
    //from original ASCII code to secret code for each character in the file, according to the following formula:
    int i = 0;
    if ((i = nextAvailable(parameter->buffer, parameter->state)) != -1) {
      LOG(LOG4C_DEBUG, "Lock work mutex");
      pthread_mutex_lock(&parameter->indexLock[1]);
      /*LOG(LOG4C_INFO, "Work on data %c", item->data);*/
      LOG(LOG4C_DEBUG, "Lock buffer mutex");
      pthread_mutex_lock(&parameter->bufferLock);
      parameter->state == 'E'
        ? encrypt(parameter->configuration->key, parameter->buffer->items[i])
        : decrypt(parameter->configuration->key, parameter->buffer->items[i]);
      LOG(LOG4C_DEBUG, "Unlock buffer mutex");
      pthread_mutex_unlock(&parameter->bufferLock);
      parameter->index[1]++;
      LOG(LOG4C_DEBUG, "Unlock work mutex");
      pthread_mutex_unlock(&parameter->indexLock[1]);
    } else {
      LOG(LOG4C_TRACE, "No available item");
    }
    randomSleep(&req);
  }
  LOG(LOG4C_INFO, "Finish thread WORK");
}

void doOut(void* p) {
  LOG(LOG4C_INFO, "Start thread OUT");
  Parameter* parameter = (Parameter*)p;
  struct timespec req;
  //Similarly, upon being created, each OUT thread sleeps (use nanosleep) for some random time between 0 and 0.01 seconds
  randomSleep(&req);

  while(parameter->index[2] < parameter->fileSize) {
    LOG(LOG4C_DEBUG, "Finish out index [%d]", parameter->index[2]);
    int i = 0;
    for(i = 0; i < parameter->buffer->size; ++i) {
      BufferItem* item = parameter->buffer->items[i];
      LOG(LOG4C_DEBUG, "[%c] @ [%d]", item->data, i);
    }
    //and it reads a processed byte and its offset from the next available nonempty buffer slot,
    LOG(LOG4C_DEBUG, "Before buffer size [%d]", parameter->buffer->size);
    if ((i = nextAvailable(parameter->buffer, 'N')) != -1) {
      LOG(LOG4C_DEBUG, "Lock out mutex");
      pthread_mutex_lock(&parameter->indexLock[2]);
      //and then writes the byte to that offset in the target file.
      LOG(LOG4C_DEBUG, "Lock buffer mutex");
      pthread_mutex_lock(&parameter->bufferLock);

      BufferItem* item = removeItem(parameter->buffer, i);
      LOG(LOG4C_DEBUG, "Unlock buffer mutex");
      pthread_mutex_unlock(&parameter->bufferLock);

      LOG(LOG4C_DEBUG, "Lock write mutex");
      pthread_mutex_lock(&parameter->writeLock);

      fseek(parameter->out, item->offset, SEEK_SET);
      LOG(LOG4C_INFO, "Write data [%c] to output file", item->data);
      fprintf(parameter->out, "%c", item->data);

      LOG(LOG4C_DEBUG, "Unlock write mutex");
      pthread_mutex_unlock(&parameter->writeLock);
      //remove this item
      parameter->index[2]++;

      LOG(LOG4C_DEBUG, "Unlock out mutex");
      pthread_mutex_unlock(&parameter->indexLock[2]);
    } else {
      LOG(LOG4C_TRACE, "No available item");
    }
    //Then, it also goes to sleep (use nanosleep) for some random time between 0 and 0.01 seconds and goes back to copy next byte until nothing is left.
    //If the buffer is empty, the OUT threads go to sleep (use nanosleep) for some random time between 0 and 0.01 seconds and then go back to check again.
    LOG(LOG4C_DEBUG, "After buffer size [%d]", parameter->buffer->size);
    randomSleep(&req);
  }
  LOG(LOG4C_INFO, "Finish thread OUT");
}

/**
 *  encrypt <KEY> <nIn> <nWork> <nOut> <file_in> <file_out> <bufSize>
 */
int main(int argc, char** argv) {
  Configuration configuration;
  initializeConfiguration(&configuration, argv);
  srand(time(NULL));

  Parameter parameter;
  initializeParameter(&parameter, &configuration);
  parameter.state = 'E';

  pthread_t tin[configuration.nIn];
  pthread_t twork[configuration.nWork];
  pthread_t tout[configuration.nOut];
  //run
  int i;
  LOG(LOG4C_INFO, "Start thread");
  LOG(LOG4C_INFO, "File size is %d", parameter.fileSize);
  for (i = 0; i < configuration.nIn; ++i) {
    pthread_create(&tin[i], NULL, doIn, &parameter);
  }
  /*for (i = 0; i < configuration.nWork; ++i) {*/
    /*pthread_create(&twork[i], NULL, doWork, &parameter);*/
  /*}*/
  for (i = 0; i < configuration.nOut; ++i) {
    pthread_create(&tout[i], NULL, doOut, &parameter);
  }
  //wait for the result
  LOG(LOG4C_INFO, "Join thread");
  for (i = 0; i < configuration.nIn; ++i) {
    pthread_join(tin[i], NULL);
  }
  /*for (i = 0; i < configuration.nWork; ++i) {*/
    /*pthread_join(twork[i], NULL);*/
  /*}*/
  for (i = 0; i < configuration.nOut; ++i) {
    pthread_join(tout[i], NULL);
  }

  LOG(LOG4C_INFO, "Clean");
  destroyItemList(parameter.buffer);
  pthread_mutex_destroy(&parameter.readLock);
  pthread_mutex_destroy(&parameter.writeLock);
  pthread_mutex_destroy(&parameter.bufferLock);
  for(i = 0; i < 3; ++i) {
    pthread_mutex_destroy(&parameter.indexLock[i]);
  }
  fclose(configuration.in);
  fclose(configuration.out);
  return (EXIT_SUCCESS);
}

