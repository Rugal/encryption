#ifndef ITEMLIST_H
#define ITEMLIST_H

#include <assert.h>
#include <stdbool.h>

#define LIST_INITIAL_CAPACITY 8
#define ENCRYPTION_MODE 'E'
#define DECRYPTION_MODE 'D'
#define NORMAL_MODE 'N'

//declaration
typedef struct BufferItem BufferItem;
typedef struct ItemList ItemList;

BufferItem* createItem();
void deleteItem(BufferItem* item);
ItemList* createItemList();
bool isEmpty(ItemList* list);
bool isFull(ItemList* list);
void addItem(ItemList* list, BufferItem* item);
BufferItem* removeLastItem(ItemList* list);
void deleteItemList(ItemList* list);
void destroyItemList(ItemList* list);
int nextAvailable(ItemList* buffer, char state);


// definition

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

int nextAvailable(ItemList* buffer, char state) {
  assert(buffer != NULL);
  int i;
  for(i = 0; i < buffer->size; ++i)
    if(buffer->items[i]->state == state)
      return i;
  return -1;
}

#endif
