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

#define NANO_TIME 10000000

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
  pthread_mutex_t indexLock[3];
} Parameter;

void initializeParameter(Parameter* parameter, Configuration* configuration) {
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
  parameter->buffer = createItemList();
  parameter->in = fopen(configuration->in, "r");
  parameter->out = fopen(configuration->out, "w");

  if (parameter->in == NULL) {
    perror("Unable to open input file\n");
    exit(1);
  }
  if (parameter->out == NULL) {
    perror("Unable to open output file\n");
    exit(1);
  }
  fseek(parameter->in, 0L, SEEK_END);
  parameter->fileSize = ftell(parameter->in);
  fseek(parameter->in, 0L, SEEK_SET);
}

void encrypt(int key, BufferItem* item) {
  assert(item != NULL);
  printf("ENCRYPT\n");
  if(item->state != 'E')
    return;
  if(item->data>31 && item->data<127 )
    item->data = (((int)item->data-32)+2*95+key)%95+32 ;
  item->state = 'N';
}

void decrypt(int key, BufferItem* item) {
  assert(item != NULL);
  if(item->state != DECRYPTION_MODE)
    return;
  if (item->data>31 && item->data<127 )
    item->data = (((int)item->data-32)+2*95-key)%95+32 ;
  item->state = NORMAL_MODE;
}

void doIn(void* p) {
  printf("In\n");
  Parameter* parameter = (Parameter*)p;
  //Each IN thread goes to sleep (use nanosleep) for some random time between 0 and 0.01 seconds upon being created.
  struct timespec req;
  randomSleep(&req);
  while(!feof(parameter->in) && parameter->index[0] < parameter->fileSize) {
    //Then, it reads the next single byte from the input file
    BufferItem* item = createItem();

    pthread_mutex_lock(&parameter->readLock);
    printf("Lock 0\n");
    item->offset = ftell(parameter->in);
    printf("Read data\n");
    fscanf(parameter->in, "%c", &item->data);
    /*printf("[%c] @ %d\n", item->data,);*/
    printf("Unlock 0\n");
    pthread_mutex_unlock(&parameter->readLock);
    //If the buffer is full, IN threads go to sleep (use nanosleep) for some random time between 0 and 0.01 seconds and then go back to check again.
    while(isFull(parameter->buffer)) {
      randomSleep(&req);
    }
    //and saves that byte and its offset in the file to the next available empty slot in the buffer.
    pthread_mutex_lock(&parameter->indexLock[0]);
    printf("Lock 0\n");
    printf("Add data\n");
    addItem(parameter->buffer, item);
    parameter->index[0]++;
    printf("Unlock 0\n");
    pthread_mutex_unlock(&parameter->indexLock[0]);
    //Then, this IN threads goes to sleep (use nanosleep) for some random time between 0 and 0.01 seconds
    randomSleep(&req);
    //and then goes back to read the next byte of the file until the end of file.
  }
  printf("Finish In\n");
}

void doWork(void* p) {
  printf("Work\n");
  Parameter* parameter = (Parameter*)p;
  //Meanwhile, upon being created each WORK thread sleeps (use nanosleep) for some random time between 0 and 0.01 seconds and it reads next byte in the buffer and process one byte of data, either encrypts or decrypt according to the working mode.
  //Then the WORK thread goes to sleep (use nanosleep) for some random time between 0 and 0.01 seconds and goes back to process next byte in the buffer until the entire file is done.
  struct timespec req;
  randomSleep(&req);

  //If the buffer is empty, the WORK threads go to sleep (use nanosleep) for some random time between 0 and 0.01 seconds and then go back to check again.
  while(parameter->index[1] < parameter->fileSize) {
    //If runnIng in the encrypt mode, each WORK thread will encrypt each data byte in the buffer,
    //from original ASCII code to secret code for each character in the file, according to the following formula:
    int i = 0;
    if ((i = nextAvailable(parameter->buffer, parameter->state)) != -1) {
      printf("Lock 1\n");
      pthread_mutex_lock(&parameter->indexLock[1]);
      /*printf("Work data %d\n", parameter->configuration->key);*/
      parameter->state == 'E'
        ? encrypt(parameter->configuration->key, parameter->buffer->items[i])
        : decrypt(parameter->configuration->key, parameter->buffer->items[i]);
      parameter->index[1]++;
      printf("Unlock 1\n");
      pthread_mutex_unlock(&parameter->indexLock[1]);
    }
    randomSleep(&req);
  }
  printf("Finish Work\n");
}

void doOut(void* p) {
  printf("Out\n");
  Parameter* parameter = (Parameter*)p;
  struct timespec req;
  //Similarly, upon being created, each OUT thread sleeps (use nanosleep) for some random time between 0 and 0.01 seconds
  randomSleep(&req);

  while(parameter->index[2] < parameter->fileSize) {
    int i = 0;
    //and it reads a processed byte and its offset from the next available nonempty buffer slot,
    if ((i = nextAvailable(parameter->buffer, NORMAL_MODE)) != -1) {
      printf("Lock 2\n");
      pthread_mutex_lock(&parameter->indexLock[2]);
      //and then writes the byte to that offset in the target file.
      BufferItem* item = parameter->buffer->items[i];
      fseek(parameter->out, item->offset, SEEK_SET);
      printf("Write data\n");
      fprintf(parameter->out, "%c", item->data);
      //remove this item
      removeItem(parameter->buffer, i);
      parameter->index[2]++;
      printf("Unlock 2\n");
      pthread_mutex_unlock(&parameter->indexLock[2]);
    }
    //Then, it also goes to sleep (use nanosleep) for some random time between 0 and 0.01 seconds and goes back to copy next byte until nothing is left.
    //If the buffer is empty, the OUT threads go to sleep (use nanosleep) for some random time between 0 and 0.01 seconds and then go back to check again.
    randomSleep(&req);
  }
  printf("Finish Out\n");
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
  printf("Start thread\n");
  for (i = 0; i < configuration.nWork; ++i) {
    pthread_create(&twork[i], NULL, doWork, &parameter);
  }
  for (i = 0; i < configuration.nIn; ++i) {
    pthread_create(&tin[i], NULL, doIn, &parameter);
  }
  for (i = 0; i < configuration.nOut; ++i) {
    pthread_create(&tout[i], NULL, doOut, &parameter);
  }
  //wait for the result
  printf("Join thread\n");
  for (i = 0; i < configuration.nOut; ++i) {
    pthread_join(tout[i], NULL);
  }
  for (i = 0; i < configuration.nIn; ++i) {
    pthread_join(tin[i], NULL);
  }
  for (i = 0; i < configuration.nWork; ++i) {
    pthread_join(twork[i], NULL);
  }

  printf("Clean\n");
  destroyItemList(parameter.buffer);
  pthread_mutex_destroy(&parameter.readLock);
  pthread_mutex_destroy(&parameter.writeLock);
  for(i = 0; i < 3; ++i) {
    pthread_mutex_destroy(&parameter.indexLock[i]);
  }
  fclose(configuration.in);
  fclose(configuration.out);
  return (EXIT_SUCCESS);
}

