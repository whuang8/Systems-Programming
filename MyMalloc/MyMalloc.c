#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <pthread.h>

static pthread_mutex_t mutex;

const int ArenaSize = 2097152;
const int NumberOfFreeLists = 1;

// Header of an object. Used both when the object is allocated and freed
struct ObjectHeader {
    size_t _objectSize;         // Real size of the object.
    int _allocated;             // 1 = yes, 0 = no 2 = sentinel
    struct ObjectHeader * _next;       // Points to the next object in the freelist (if free).
    struct ObjectHeader * _prev;       // Points to the previous object.
};

struct ObjectFooter {
    size_t _objectSize;
    int _allocated;
};

  //STATE of the allocator

  // Size of the heap
  static size_t _heapSize;

  // initial memory pool
  static void * _memStart;

  // number of chunks request from OS
  static int _numChunks;

  // True if heap has been initialized
  static int _initialized;

  // Verbose mode
  static int _verbose;

  // # malloc calls
  static int _mallocCalls;

  // # free calls
  static int _freeCalls;

  // # realloc calls
  static int _reallocCalls;
  
  // # realloc calls
  static int _callocCalls;

  // Free list is a sentinel
  static struct ObjectHeader _freeListSentinel; // Sentinel is used to simplify list operations
  static struct ObjectHeader *_freeList;


  //FUNCTIONS

  //Initializes the heap
  void initialize();

  // Allocates an object 
  void * allocateObject( size_t size );

  // Frees an object
  void freeObject( void * ptr );

  // Returns the size of an object
  size_t objectSize( void * ptr );

  // At exit handler
  void atExitHandler();

  //Prints the heap size and other information about the allocator
  void print();
  void print_list();

  // Gets memory from the OS
  void * getMemoryFromOS( size_t size );

  void increaseMallocCalls() { _mallocCalls++; }

  void increaseReallocCalls() { _reallocCalls++; }

  void increaseCallocCalls() { _callocCalls++; }

  void increaseFreeCalls() { _freeCalls++; }

extern void
atExitHandlerInC()
{
  atExitHandler();
}

void initialize()
{
  // Environment var VERBOSE prints stats at end and turns on debugging
  // Default is on
  _verbose = 1;
  const char * envverbose = getenv( "MALLOCVERBOSE" );
  if ( envverbose && !strcmp( envverbose, "NO") ) {
    _verbose = 0;
  }

  pthread_mutex_init(&mutex, NULL);
  void * _mem = getMemoryFromOS( ArenaSize + (2*sizeof(struct ObjectHeader)) + (2*sizeof(struct ObjectFooter)) );

  // In verbose mode register also printing statistics at exit
  atexit( atExitHandlerInC );

  //establish fence posts
  struct ObjectFooter * fencepost1 = (struct ObjectFooter *)_mem;
  fencepost1->_allocated = 1;
  fencepost1->_objectSize = 123456789;
  char * temp = 
      (char *)_mem + (2*sizeof(struct ObjectFooter)) + sizeof(struct ObjectHeader) + ArenaSize;
  struct ObjectHeader * fencepost2 = (struct ObjectHeader *)temp;
  fencepost2->_allocated = 1;
  fencepost2->_objectSize = 123456789;
  fencepost2->_next = NULL;
  fencepost2->_prev = NULL;

  //initialize the list to point to the _mem
  temp = (char *) _mem + sizeof(struct ObjectFooter);
  struct ObjectHeader * currentHeader = (struct ObjectHeader *) temp;
  temp = (char *)_mem + sizeof(struct ObjectFooter) + sizeof(struct ObjectHeader) + ArenaSize;
  struct ObjectFooter * currentFooter = (struct ObjectFooter *) temp;
  _freeList = &_freeListSentinel;
  currentHeader->_objectSize = ArenaSize + sizeof(struct ObjectHeader) + sizeof(struct ObjectFooter); //2MB
  currentHeader->_allocated = 0;
  currentHeader->_next = _freeList;
  currentHeader->_prev = _freeList;
  currentFooter->_allocated = 0;
  currentFooter->_objectSize = currentHeader->_objectSize;
  _freeList->_prev = currentHeader;
  _freeList->_next = currentHeader; 
  _freeList->_allocated = 2; // sentinel. no coalescing.
  _freeList->_objectSize = 0;
  _memStart = (char*) currentHeader;
}

void * allocateObject( size_t size )
{
  //Make sure that allocator is initialized
  if ( !_initialized ) {
    _initialized = 1;
    initialize();
  }

  // Add the ObjectHeader/Footer to the size and round the total size up to a multiple of
  // 8 bytes for alignment.
  size_t roundedSize = (size + sizeof(struct ObjectHeader) + sizeof(struct ObjectFooter) + 7) & ~7;


  struct ObjectHeader *temp = _freeList->_next;
  int offset = -1;
  // //traverse freelist for block large enough for request
  while(temp->_allocated != 2){
    if (temp->_objectSize >= roundedSize){
      //large enough block found
      offset = temp->_objectSize - roundedSize;
      if (offset > sizeof(struct ObjectHeader) + sizeof(struct ObjectFooter) + 8){
        //can be split
        //create new footer
        char * tmp = (char *) temp + roundedSize - sizeof(struct ObjectFooter);
        struct ObjectFooter * newFooter = (struct ObjectFooter *)tmp;
        newFooter->_objectSize = roundedSize;
        newFooter->_allocated = 1;


        //create new header
        tmp = (char *) temp + roundedSize;
        struct ObjectHeader * newHeader = (struct ObjectHeader *)tmp;
        newHeader->_objectSize = temp->_objectSize - roundedSize;
        newHeader->_allocated = 0;
        newHeader->_next = temp->_next;
        newHeader->_prev = temp->_prev;

        //update footer
        tmp = (char *) temp + temp->_objectSize - sizeof(struct ObjectFooter);
        struct ObjectFooter * footer = (struct ObjectFooter *)tmp;
        footer->_objectSize = newHeader->_objectSize;

        //add new header to the freelist
        temp->_prev->_next = newHeader;
        temp->_next->_prev = newHeader;

        //remove the newly allocated block from newlist
        temp->_objectSize = roundedSize;
        temp->_allocated = 1;
        temp->_next = NULL;
        temp->_prev = NULL;
      }
      else{
        //cannot be split
        temp->_allocated = 1;
        temp->_prev->_next = temp->_next;
        temp->_next->_prev = temp->_prev;
        temp->_next = NULL;
        temp->_prev = NULL;

      }
      pthread_mutex_unlock(&mutex);
      return (void * )(temp+1);
    }
    temp = temp->_next;
  }
  //no chunks big enough. Need to request more from OS.
  
  // Environment var VERBOSE prints stats at end and turns on debugging
  // Default is on
  _verbose = 1;
  const char * envverbose = getenv( "MALLOCVERBOSE" );
  if ( envverbose && !strcmp( envverbose, "NO") ) {
    _verbose = 0;
  }

  pthread_mutex_init(&mutex, NULL);
  void * _mem = getMemoryFromOS( ArenaSize + (2*sizeof(struct ObjectHeader)) + (2*sizeof(struct ObjectFooter)) );


  //establish fence posts
  struct ObjectFooter * fencepost1 = (struct ObjectFooter *)_mem;
  fencepost1->_allocated = 1;
  fencepost1->_objectSize = 123456789;
  char * temp1 = 
      (char *)_mem + (2*sizeof(struct ObjectFooter)) + sizeof(struct ObjectHeader) + ArenaSize;
  struct ObjectHeader * fencepost2 = (struct ObjectHeader *)temp1;
  fencepost2->_allocated = 1;
  fencepost2->_objectSize = 123456789;
  fencepost2->_next = NULL;           
  fencepost2->_prev = NULL;

  //initialize the list to point to the _mem
  temp1 = (char *) _mem + sizeof(struct ObjectFooter);
  struct ObjectHeader * currentHeader = (struct ObjectHeader *) temp1;
  temp1 = (char *)_mem + sizeof(struct ObjectFooter) + sizeof(struct ObjectHeader) + ArenaSize;
  struct ObjectFooter * currentFooter = (struct ObjectFooter *) temp1;
  //_freeList = &_freeListSentinel;
  currentHeader->_objectSize = ArenaSize + sizeof(struct ObjectHeader) + sizeof(struct ObjectFooter); //2MB
  currentHeader->_allocated = 0;
  currentHeader->_next = _freeList;
  currentHeader->_prev = temp->_prev;
  currentFooter->_allocated = 0;
  currentFooter->_objectSize = currentHeader->_objectSize;
  // _freeList->_prev = currentHeader;
  // _freeList->_next = currentHeader; 
  // _freeList->_allocated = 2; // sentinel. no coalescing.
  // _freeList->_objectSize = 0;

  //connect the new block
  temp->_prev->_next = currentHeader;
  temp->_prev = currentHeader;
  temp = currentHeader;

  pthread_mutex_unlock(&mutex);

  // Return a pointer to usable memory
  return (void * )(temp+1);
}

void freeObject( void * ptr )
{
  int previousIsFree = 0;
  int nextIsFree = 0;
  int coalesced = 0;

  //get current header pointer
  char * temp = (char *)ptr - sizeof(struct ObjectHeader);
  struct ObjectHeader * currentHeader = (struct ObjectHeader *)temp;

  //get current footer pointer
  temp = (char *)ptr + currentHeader->_objectSize - sizeof(struct ObjectFooter) - sizeof(struct ObjectHeader);
  struct ObjectFooter * currentFooter = (struct ObjectFooter *)temp;


  //check if block before the current one is free
  temp = (char *)ptr - sizeof(struct ObjectHeader) - sizeof(struct ObjectFooter);
  struct ObjectFooter * previousFooter = (struct ObjectFooter *)temp;
  temp = (char*)ptr - sizeof(struct ObjectHeader) - previousFooter->_objectSize;
  struct ObjectHeader * previousHeader = (struct ObjectHeader *)temp;
  if (previousFooter->_allocated == 0){
    previousIsFree = 1;
  }

  //check if the block after the current one is free
  temp = (char *)ptr + currentHeader->_objectSize - sizeof(struct ObjectHeader);
  struct ObjectHeader * nextHeader = (struct ObjectHeader *)temp;
  temp = (char *)ptr + currentHeader->_objectSize + nextHeader->_objectSize - sizeof(struct ObjectHeader) - sizeof(struct ObjectFooter);
  struct ObjectFooter * nextFooter = (struct ObjectFooter *)temp;
  if (nextHeader->_allocated == 0){
    nextIsFree = 1;
  }

  if(previousIsFree == 1){
    //need to coalesce previous and current
    currentHeader->_allocated = 0;
    currentFooter->_allocated = 0;
    previousHeader->_objectSize += currentHeader->_objectSize;
    currentFooter->_objectSize = previousHeader->_objectSize;
    coalesced = 1;
  }
  if(nextIsFree == 1){
    //need to coalesce next
    if (currentHeader->_allocated == 0){
      //already coalesced with previous block
      previousHeader->_objectSize += nextHeader->_objectSize;
      nextFooter->_objectSize = previousHeader->_objectSize;


      previousHeader->_next = nextHeader->_next;
      nextHeader->_next->_prev = previousHeader;

      nextHeader->_prev->_next = nextHeader->_next;
      nextHeader->_next->_prev = previousHeader;

      nextHeader->_next = NULL;
      nextHeader->_prev = NULL;
    }
    else{
      //need to coalesce only current block and next block
      currentHeader->_allocated = 0;
      currentHeader->_objectSize += nextHeader->_objectSize;
      nextFooter->_objectSize = currentHeader->_objectSize;
      currentHeader->_next = nextHeader->_next;
      currentHeader->_prev = nextHeader->_prev;

      nextHeader->_prev->_next = currentHeader;
      nextHeader->_next->_prev = currentHeader;

      nextHeader->_next = NULL;
      nextHeader->_prev = NULL;
    }
    coalesced = 1;
  }

  struct ObjectHeader * current = _freeList;
  struct ObjectHeader * next = _freeList->_next;

  if (coalesced == 0){
    //no coalescing needed. Insert the freed block back into free list

    //free the current block
    currentHeader->_allocated = 0;
    currentFooter->_allocated = 0;

    //place current block back into free list

    while(next->_allocated != 2){
      if (currentHeader > current && currentHeader < next){
        //insert header into freelist
        currentHeader->_prev = current;
        currentHeader->_next = next;

        current->_next = currentHeader;
        next->_prev = currentHeader;
      }
      current = next;
      next = next->_next;
    }
  }
  return;
}

size_t objectSize( void * ptr )
{
  // Return the size of the object pointed by ptr. We assume that ptr is a valid obejct.
  struct ObjectHeader * o =
    (struct ObjectHeader *) ( (char *) ptr - sizeof(struct ObjectHeader) );

  // Substract the size of the header
  return o->_objectSize;
}

void print()
{
  printf("\n-------------------\n");

  printf("HeapSize:\t%zd bytes\n", _heapSize ); 
  printf("# mallocs:\t%d\n", _mallocCalls );
  printf("# reallocs:\t%d\n", _reallocCalls );
  printf("# callocs:\t%d\n", _callocCalls );
  printf("# frees:\t%d\n", _freeCalls );

  printf("\n-------------------\n");
}

void print_list()
{
  printf("FreeList: ");
  if ( !_initialized ) {
    _initialized = 1;
    initialize();
  }
  struct ObjectHeader * ptr = _freeList->_next;
  while(ptr != _freeList){
      long offset = (long)ptr - (long)_memStart;
      printf("[offset:%ld,size:%zd]",offset,ptr->_objectSize);
      ptr = ptr->_next;
      if(ptr != NULL){
          printf("->");
      }
  }
  printf("\n");
}

void * getMemoryFromOS( size_t size )
{
  // Use sbrk() to get memory from OS
  _heapSize += size;
 
  void * _mem = sbrk( size );

  if(!_initialized){
      _memStart = _mem;
  }

  _numChunks++;

  return _mem;
}

void atExitHandler()
{
  // Print statistics when exit
  if ( _verbose ) {
    print();
  }
}

//
// C interface
//

extern void *
malloc(size_t size)
{
  pthread_mutex_lock(&mutex);
  increaseMallocCalls();
  
  return allocateObject( size );
}

extern void
free(void *ptr)
{
  pthread_mutex_lock(&mutex);
  increaseFreeCalls();
  
  if ( ptr == 0 ) {
    // No object to free
    pthread_mutex_unlock(&mutex);
    return;
  }
  
  freeObject( ptr );
}

extern void *
realloc(void *ptr, size_t size)
{
  pthread_mutex_lock(&mutex);
  increaseReallocCalls();
    
  // Allocate new object
  void * newptr = allocateObject( size );

  // Copy old object only if ptr != 0
  if ( ptr != 0 ) {
    
    // copy only the minimum number of bytes
    size_t sizeToCopy =  objectSize( ptr );
    if ( sizeToCopy > size ) {
      sizeToCopy = size;
    }
    
    memcpy( newptr, ptr, sizeToCopy );

    //Free old object
    freeObject( ptr );
  }

  return newptr;
}

extern void *
calloc(size_t nelem, size_t elsize)
{
  pthread_mutex_lock(&mutex);
  increaseCallocCalls();
    
  // calloc allocates and initializes
  size_t size = nelem * elsize;

  void * ptr = allocateObject( size );

  if ( ptr ) {
    // No error
    // Initialize chunk with 0s
    memset( ptr, 0, size );
  }

  return ptr;
}

