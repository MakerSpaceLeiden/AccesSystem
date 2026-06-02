#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_debug_helpers.h>

#ifndef _jsonAllocator_H
#define _jsonAllocator_H

// We're struggling with very fragmented heaps. There are two
// likely culprints - the TLS stack (which we cannot really change)
// and the JSON stack. This customer allocator tries two things - 
// first to see if it can use PSRAM. And if that fails - it tries
// to rely on a fixed, capped, buffer which is never released, 
// and if it could not get that (_SIZE stays 0) it falls back
// on whatever the default malloc/free/realloc is.
//
// We rely on the fact that we know that there should not be
// any nested json going on; and there should be no dangling
// jsons eithers -- so we always return to an empty state with
// never more than one doc. We've not yet wrapped JsonDocument
// in a singleton to police this.
//
struct SpiRamAllocator : ArduinoJson::Allocator {
public:
  SpiRamAllocator() : SpiRamAllocator(8 * 1024) {};
  SpiRamAllocator(size_t s) {
	if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM))
		return;
	if (!(_buff = (unsigned char*) malloc(s))) {
		return;
	};
        _ptr = _buff;
	_SIZE = s;
   };

  void* allocate(size_t size) override {
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM))
         return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!_SIZE)
        return malloc(size);

    if ((unsigned char *)_ptr + size > _buff + _SIZE) {
	Serial.println("MEM out of memory");
	return NULL;
    };

    void * ptr = _ptr; 
    _ptr = (unsigned char *)_ptr + size;

    return ptr;
  }

  void deallocate(void* ptr) override {
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM))
       heap_caps_free(ptr);
    if (!_SIZE)
       free(ptr);

    if (ptr != _buff)
	return;

    Serial.printf("MEM cleanse -- peak %u\n", (unsigned char *)_ptr - _buff);
    _ptr = _buff;
  }

  void* reallocate(void* ptr, size_t new_size) override {
    if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM))
        return heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM);

    if (!_SIZE)
        return realloc(ptr,new_size);

    return allocate(new_size);
  };
 
private:
  size_t _SIZE = 0;
  unsigned char * _buff;
  void  * _ptr = NULL;
};
extern SpiRamAllocator jsonAllocator;
#endif
