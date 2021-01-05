void *debug_malloc(size_t size, char *file, int line);
void debug_free(void *ptr, char *file, int line);
void free_foreign(void *ptr);


#ifdef DEBUG
  #define free(ptr) debug_free(ptr,__FILE__,__LINE__)
  #define mymalloc(size) debug_malloc(size, __FILE__, __LINE__)
#else
  #define free_foreign free
  #define malloc_foreign malloc
#endif
