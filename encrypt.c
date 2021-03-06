/*
Family Name:
Given Name:
Section:
Student Number:
CS Login:
*/

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include <sys/types.h>
#include <limits.h>

#define LIST_INITIAL_CAPACITY 8
#define NANO_TIME 10000000
enum Log4CLevel {
  LOG4C_ALL     = INT_MIN,
  LOG4C_TRACE   = 0,
  LOG4C_DEBUG   = 10,
  LOG4C_INFO    = 20,
  LOG4C_WARNING = 30,
  LOG4C_ERROR   = 40,
  LOG4C_OFF     = INT_MAX
};

int log4c_level;

#define LOG(level, fmt, ...) \
  if (level >= log4c_level) { \
    fprintf(stderr, "%s@%-10s#%d:%d: " fmt "\n", __FILE__, __func__, __LINE__, level, ##__VA_ARGS__); \
  }

//declaration
typedef struct BufferItem BufferItem;
typedef struct ItemList ItemList;

struct BufferItem {
  char  data ;
  off_t offset ;
  char state;
};

struct ItemList {
  BufferItem** items;
  int capacity;
  int size;
};

BufferItem* createItem();
void deleteItem(BufferItem* item);
ItemList* createItemList(int capacity);
bool isEmpty(ItemList* list);
bool isFull(ItemList* list);
int addItem(ItemList* list, BufferItem* item);
BufferItem* removeItem(ItemList* list, int index);
void deleteItemList(ItemList* list);
void destroyItemList(ItemList* list);
int nextAvailable(ItemList* buffer, char state);

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
  assert(configuration->key >= -127 && configuration->key <= 127);
  assert(configuration->nIn > 0);
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
    for(i = 0; i < parameter->buffer->capacity; ++i) {
      BufferItem* t = parameter->buffer->items[i];
      if (NULL != t)
        LOG(LOG4C_DEBUG, "[%c] @ [%d]", t->data, i);
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
    for(i = 0; i < parameter->buffer->capacity; ++i) {
      BufferItem* t = parameter->buffer->items[i];
      if (NULL != t)
        LOG(LOG4C_DEBUG, "[%c] @ [%d]", t->data, i);
    }
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
    /*LOG(LOG4C_ERROR, "[%c], [%d]", parameter->state, nextAvailable(parameter->buffer, parameter->state));*/
    if ((i = nextAvailable(parameter->buffer, 'N')) != -1) {
      LOG(LOG4C_DEBUG, "Lock work mutex");
      pthread_mutex_lock(&parameter->indexLock[1]);
      /*LOG(LOG4C_INFO, "Work on data %c", item->data);*/
      LOG(LOG4C_DEBUG, "Lock buffer mutex");
      pthread_mutex_lock(&parameter->bufferLock);
      BufferItem* item = parameter->buffer->items[i];
      LOG(LOG4C_DEBUG, "Start working on encryption/decryption for item [%d] data [%c] with key [%d]", i, item->data, parameter->configuration->key);
      char temp = item->data;
      parameter->state == 'E'
        ? encrypt(parameter->configuration->key, item)
        : decrypt(parameter->configuration->key, item);
      LOG(LOG4C_DEBUG, "%s data [%c] to [%c] with key [%d] @ [%d]", parameter->state == 'E' ? "encrypt" : "decrypt", temp, item->data, parameter->configuration->key, i);
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
    for(i = 0; i < parameter->buffer->capacity; ++i) {
      BufferItem* t = parameter->buffer->items[i];
      if (NULL != t)
        LOG(LOG4C_DEBUG, "[%c] @ [%d]", t->data, i);
    }
    //and it reads a processed byte and its offset from the next available nonempty buffer slot,
    LOG(LOG4C_DEBUG, "Before buffer size [%d]", parameter->buffer->size);
    if ((i = nextAvailable(parameter->buffer, parameter->state)) != -1) {
      LOG(LOG4C_DEBUG, "Lock out mutex");
      pthread_mutex_lock(&parameter->indexLock[2]);
      //and then writes the byte to that offset in the target file.
      LOG(LOG4C_DEBUG, "Lock buffer mutex");
      pthread_mutex_lock(&parameter->bufferLock);

      BufferItem* item = removeItem(parameter->buffer, i);
      LOG(LOG4C_DEBUG, "Remove data [%c]@[#%d] from buffer", item->data, i);
      LOG(LOG4C_DEBUG, "Unlock buffer mutex");
      pthread_mutex_unlock(&parameter->bufferLock);

      LOG(LOG4C_DEBUG, "Lock write mutex");
      pthread_mutex_lock(&parameter->writeLock);

      LOG(LOG4C_DEBUG, "Move file position to [#%d]", item->offset);
      if (fseek(parameter->out, item->offset, SEEK_SET) == -1) {
          LOG(LOG4C_ERROR, "error setting output file position to %d\n", item->offset);
          exit(-1);
      }
      LOG(LOG4C_INFO, "Write data [%c] to output [#%d]", item->data, item->offset);
      if (fputc(item->data, parameter->out) == EOF) {
          LOG(LOG4C_ERROR, "error writing byte %d to output file\n", item->data);
          exit(-1);
      }
      fflush(parameter->out);

      /*fseek(parameter->out, item->offset, SEEK_SET);*/
      /*fprintf(parameter->out, "%c", item->data);*/
      free(item);

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
    for(i = 0; i < parameter->buffer->capacity; ++i) {
      BufferItem* t = parameter->buffer->items[i];
      if (NULL != t)
        LOG(LOG4C_DEBUG, "[%c] @ [%d]", t->data, i);
    }
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
  parameter.configuration = &configuration;

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
  for (i = 0; i < configuration.nWork; ++i) {
    pthread_create(&twork[i], NULL, doWork, &parameter);
  }
  for (i = 0; i < configuration.nOut; ++i) {
    pthread_create(&tout[i], NULL, doOut, &parameter);
  }
  //wait for the result
  LOG(LOG4C_INFO, "Join thread");
  for (i = 0; i < configuration.nIn; ++i) {
    pthread_join(tin[i], NULL);
  }
  for (i = 0; i < configuration.nWork; ++i) {
    pthread_join(twork[i], NULL);
  }
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
  fclose(parameter.in);
  fclose(parameter.out);
  return (EXIT_SUCCESS);
}

// definition


BufferItem* createItem() {
  BufferItem* item = malloc(sizeof(BufferItem));
  assert(item != NULL);
  item->data = '\0';
  item->offset = 0;
  item->state = 'N';
  return item;
}

void deleteItem(BufferItem* item) {
  if(item != NULL)
    free(item);
}

ItemList* createItemList(int capacity) {
  ItemList* list = malloc(sizeof(ItemList));
  assert(list != NULL);
  list->capacity = capacity;
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

int addItem(ItemList* list, BufferItem* item) {
  assert(list != NULL);
  assert(item != NULL);
  if (isFull(list)) {
    return -1;
  }
  int i;
  for (i = 0; i < list->capacity; ++i)
    if (NULL == list->items[i]) {
      list->items[i] = item;
      break;
    }
  list->size++;
  return i;
}

BufferItem* removeItem(ItemList* list, int index) {
  assert(list != NULL);
  assert(index >= 0);
  assert(index < list->capacity);
  BufferItem* item = list->items[index];
  list->items[index] = NULL;
  list->size--;
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
  int i;
  for(i = 0; i < list->capacity; ++i)
    if (list->items[i] != NULL)
      free(list->items[i]);
  deleteItemList(list);
}

int nextAvailable(ItemList* buffer, char target) {
  assert(buffer != NULL);
  int i;
  for(i = 0; i < buffer->capacity; ++i)
    if(buffer->items[i] != NULL && buffer->items[i]->state == target)
      return i;
  return -1;
}

