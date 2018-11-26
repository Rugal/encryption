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
#include <stdbool.h>
#include <sys/types.h>

#define LIST_INITIAL_CAPACITY 8
#define NANO_TIME 10000000
#define ENCRYPTION_MODE 'E'
#define DECRYPTION_MODE 'D'
#define NORMAL_MODE 'N'

typedef struct {
  int key;
  int nIN;
  int nWORK;
  int nOUT;
  char* in;
  char* out;
  int bufferSize;
} Configuration;

void initializeConfiguration(Configuration* configuration, char** argv) {
  configuration->key = atoi(argv[1]);
  configuration->nIN = atoi(argv[2]);
  configuration->nWORK = atoi(argv[3]);
  configuration->nOUT = atoi(argv[4]);
  configuration->in = argv[5];
  configuration->out = argv[6];
  configuration->bufferSize = atoi(argv[7]);
  //TODO validate value
}

typedef struct {
  char  data ;
  off_t offset ;
  char state;
} BufferItem;

BufferItem* createItem() {
  BufferItem* item = malloc(sizeof(BufferItem));
  assert(item != NULL);
  item->data = '\0';
  item->offset = 0;
  item->state = ENCRYPTION_MODE;
  return item;
}

void deleteItem(BufferItem* item) {
  if(item != NULL)
    free(item);
}

typedef struct {
  BufferItem** items;
  int capacity;
  int size;
} ItemList;

ItemList* createItemList() {
  ItemList* list = malloc(sizeof(ItemList));
  assert(list != NULL);
  list->capacity = LIST_INITIAL_CAPACITY;
  list->size = 0;
  list->items = malloc(list->capacity * sizeof(BufferItem*));
  assert(list->items != NULL);
  return list;
}

bool isEmpty(ItemList* list) {
  assert(list != NULL);
  return list->size == 0;
}

bool isFull(ItemList* list) {
  assert(list != NULL);
  return list->size >= list->capacity;
}

void addItem(ItemList* list, BufferItem* item) {
  assert(list != NULL);
  assert(item != NULL);
  if (isFull(list)) {
    list->capacity *= 2;
    list->items = realloc(list->items, list->capacity * sizeof (BufferItem*));
  }
  list->items[list->size++] = item;
}

BufferItem* removeLastItem(ItemList* list) {
  assert(list != NULL);
  BufferItem* item = list->items[--list->size];
  assert(item != NULL);
  list->size = list->size < 0 ? 0 : list->size;
  return item;
}

void deleteItemList(ItemList* list) {
  if(list == NULL)
    return;
  free(list->items);
  free(list);
}

void destroyItemList(ItemList* list) {
  if(list == NULL)
    return;
  while(!isEmpty(list)) {
    BufferItem* item = removeLastItem(list);
    free(item);
  }
  deleteItemList(list);
}

void randomSleep(struct timespec* req) {
  assert(req != NULL);
  req->tv_nsec = rand() % NANO_TIME;
  nanosleep(req, NULL);
}

typedef struct {
  FILE* in;
  ItemList* buffer;
  long fileSize;
  long index;
  char state;
  Configuration* configuration;
} Parameter;

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

int nextAvailable(ItemList* buffer, char state) {
  assert(buffer != NULL);
  int i;
  for(i = 0; i < buffer->size; ++i)
    if(buffer->items[i]->state == state)
      return i;
  return -1;
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
  parameter->fileSize = ftell(parameter->in);
}

void doWork(void* p) {
  Parameter* parameter = (Parameter*)p;
  //Meanwhile, upon being created each WORK thread sleeps (use nanosleep) for some random time between 0 and 0.01 seconds and it reads next byte in the buffer and process one byte of data, either encrypts or decrypt according to the working mode.
  //Then the WORK thread goes to sleep (use nanosleep) for some random time between 0 and 0.01 seconds and goes back to process next byte in the buffer until the entire file is done.
  struct timespec req;
  randomSleep(&req);

  //If the buffer is empty, the WORK threads go to sleep (use nanosleep) for some random time between 0 and 0.01 seconds and then go back to check again.
  while(parameter->index < parameter->fileSize) {
    //If running in the encrypt mode, each WORK thread will encrypt each data byte in the buffer,
    //from original ASCII code to secret code for each character in the file, according to the following formula:
    int i = 0;
    while ((i = nextAvailable(parameter->buffer, parameter->state)) == -1) {
      randomSleep(&req);
    }
    parameter->state == ENCRYPTION_MODE
      ? encrypt(parameter->configuration->key, parameter->buffer->items[i])
      : decrypt(parameter->configuration->key, parameter->buffer->items[i]);

    parameter->index++;
    randomSleep(&req);
  }
}

/**
 *  encrypt <KEY> <nIN> <nWORK> <nOUT> <file_in> <file_out> <bufSize>
 */
int main(int argc, char** argv) {
  Configuration configuration;
  initializeConfiguration(&configuration, argv);
  srand(time(NULL));

  FILE* sourceFile = fopen(configuration.in, "r");
  /*FILE* targetFile = fopen(configuration.out, "r");*/
  if (sourceFile == NULL) {
    perror("Unable to open source file\n");
    exit(1);
  }
  /*if (targetFile == NULL) {*/
    /*perror("Unable to open target file\n");*/
    /*exit(1);*/
  /*}*/
  pthread_t tin[configuration.nIN];
  pthread_t twork[configuration.nWORK];
  pthread_t tout[configuration.nOUT];
  ItemList* list = createItemList();

  Parameter parameter;
  parameter.in = sourceFile;
  parameter.buffer = list;
  parameter.index = 0;
  parameter.state = ENCRYPTION_MODE;
  parameter.configuration = &configuration;
  //run
  int i;
  for (i = 0; i < configuration.nIN; ++i) {
    pthread_create(&tin[i], NULL, doIn, &parameter);
  }
  for (i = 0; i < configuration.nWORK; ++i) {
    pthread_create(&twork[i], NULL, doWork, &parameter);
  }
  //wait for the result
  for (i = 0; i < configuration.nIN; ++i) {
    pthread_join(tin[i], NULL);
  }
  for (i = 0; i < configuration.nWORK; ++i) {
    pthread_join(twork[i], NULL);
  }

  destroyItemList(list);
  return (EXIT_SUCCESS);
}

