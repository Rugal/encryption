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
  long workIndex;
  long outIndex;
  char state;
  Configuration* configuration;
} Parameter;

void initializeParameter(Parameter* parameter, Configuration* configuration) {
  assert(parameter != NULL);
  assert(configuration != NULL);
  parameter->configuration = configuration;
  parameter->state = ENCRYPTION_MODE;
  parameter->workIndex = 0;
  parameter->outIndex = 0;
  parameter->buffer = list;
  parameter->in = fopen(configuration.in, "r");
  parameter->out = fopen(configuration.out, "w");

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
  if(item->state != ENCRYPTION_MODE)
    return;
  if(item->data>31 && item->data<127 )
    item->data = (((int)item->data-32)+2*95+key)%95+32 ;
  item->state = NORMAL_MODE;
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
  Parameter* parameter = (Parameter*)p;
  //Each IN thread goes to sleep (use nanosleep) for some random time between 0 and 0.01 seconds upon being created.
  struct timespec req;
  randomSleep(&req);
  while(!feof(parameter->in)) {
    //Then, it reads the next single byte from the input file
    char c;
    fscanf(parameter->in, "%c", &c);
    //If the buffer is full, IN threads go to sleep (use nanosleep) for some random time between 0 and 0.01 seconds and then go back to check again.
    while(isFull(parameter->buffer)) {
      randomSleep(&req);
    }
    //and saves that byte and its offset in the file to the next available empty slot in the buffer.
    BufferItem* item = createItem();
    item->offset = ftell(parameter->in);
    item->data = c;
    addItem(parameter->buffer, item);
    //Then, this IN threads goes to sleep (use nanosleep) for some random time between 0 and 0.01 seconds
    randomSleep(&req);
    //and then goes back to read the next byte of the file until the end of file.
  }
}

void doWork(void* p) {
  Parameter* parameter = (Parameter*)p;
  //Meanwhile, upon being created each WORK thread sleeps (use nanosleep) for some random time between 0 and 0.01 seconds and it reads next byte in the buffer and process one byte of data, either encrypts or decrypt according to the working mode.
  //Then the WORK thread goes to sleep (use nanosleep) for some random time between 0 and 0.01 seconds and goes back to process next byte in the buffer until the entire file is done.
  struct timespec req;
  randomSleep(&req);

  //If the buffer is empty, the WORK threads go to sleep (use nanosleep) for some random time between 0 and 0.01 seconds and then go back to check again.
  while(parameter->workIndex < parameter->fileSize) {
    //If runnIng in the encrypt mode, each WORK thread will encrypt each data byte in the buffer,
    //from original ASCII code to secret code for each character in the file, according to the following formula:
    int i = 0;
    if ((i = nextAvailable(parameter->buffer, parameter->state)) != -1) {
      parameter->state == ENCRYPTION_MODE
        ? encrypt(parameter->configuration->key, parameter->buffer->items[i])
        : decrypt(parameter->configuration->key, parameter->buffer->items[i]);

      //TODO critical area
      parameter->workIndex++;
    }
    randomSleep(&req);
  }
}

void doOut(void* p) {
  Parameter* parameter = (Parameter*)p;
  struct timespec req;
  //Similarly, upon being created, each OUT thread sleeps (use nanosleep) for some random time between 0 and 0.01 seconds
  randomSleep(&req);

  while(parameter->outIndex < parameter->fileSize) {
    int i = 0;
    //and it reads a processed byte and its offset from the next available nonempty buffer slot,
    if ((i = nextAvailable(parameter->buffer, NORMAL_MODE)) != -1) {
      //and then writes the byte to that offset in the target file.
      BufferItem* item = parameter->buffer[i];
      fseek(parameter->out, item->offset, SEEK_SET);
      fprintf(parameter-out, "%c", item->data);
      //TODO critical area
      parameter->workIndex++;
    }
    //Then, it also goes to sleep (use nanosleep) for some random time between 0 and 0.01 seconds and goes back to copy next byte until nothing is left.
    //If the buffer is empty, the OUT threads go to sleep (use nanosleep) for some random time between 0 and 0.01 seconds and then go back to check again.
    randomSleep(&req);
  }
}

/**
 *  encrypt <KEY> <nIn> <nWork> <nOut> <file_in> <file_out> <bufSize>
 */
int main(int argc, char** argv) {
  Configuration configuration;
  initializeConfiguration(&configuration, argv);
  srand(time(NULL));

  ItemList* list = createItemList();

  Parameter parameter;
  initializeParameter(&parameter, &configuration);

  pthread_t tin[configuration.nIn];
  pthread_t twork[configuration.nWork];
  pthread_t tout[configuration.nOut];
  //run
  int i;
  for (i = 0; i < configuration.nIn; ++i) {
    pthread_create(&tin[i], NULL, doIn, &parameter);
  }
  for (i = 0; i < configuration.nWork; ++i) {
    pthread_create(&twork[i], NULL, doWork, &parameter);
  }
  for (i = 0; i < configuration.nOut; ++i) {
    pthread_create(&tout[i], NULL, doOut, &parameter);
  }
  //wait for the result
  for (i = 0; i < configuration.nIn; ++i) {
    pthread_join(tin[i], NULL);
  }
  for (i = 0; i < configuration.nWork; ++i) {
    pthread_join(twork[i], NULL);
  }
  for (i = 0; i < configuration.nOut; ++i) {
    pthread_join(tout[i], NULL);
  }

  destroyItemList(list);
  fclose(configuration.in);
  fclose(configuration.out);
  return (EXIT_SUCCESS);
}

