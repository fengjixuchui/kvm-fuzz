#define _GNU_SOURCE
#include "libcpp.h"
#include "hypercalls.h"
#include "mem.h"
#include "safe_mem.h"

void* operator new(size_t size) {
	return kmalloc(size);
}

void* operator new[](size_t size) {
	return kmalloc(size);
}

void operator delete(void* p) {
	return kfree(p);
}

void operator delete[](void* p) {
	return kfree(p);
}

void* kmalloc(size_t size) {
	static const size_t INITIAL_ALLOCATION_SIZE = 0x2000;
	static uint8_t* next_alloc = nullptr;
	static size_t remaining = 0;

	// Initial allocation
	if (!next_alloc) {
		next_alloc = (uint8_t*)hc_get_kernel_brk();
		dbgprintf("Kernel brk: %p\n", next_alloc);
		Mem::Virt::alloc(next_alloc, INITIAL_ALLOCATION_SIZE, PDE64_NX | PDE64_RW);
		remaining = INITIAL_ALLOCATION_SIZE;
	}

	// Request more size if needed
	if (size > remaining) {
		size_t to_alloc = PAGE_CEIL((size_t)(size*1.25));
		Mem::Virt::alloc(next_alloc + remaining, to_alloc, PDE64_NX | PDE64_RW);
		remaining += to_alloc;
	}

	void* ret = next_alloc;
	remaining -= size;
	next_alloc += size;

	//dbgprintf("Allocation of %lu: %p\n", size, ret);
	return ret;
}

void kfree(void* p) {
	return;
}

void __cxa_pure_virtual() {
	asm("hlt");
}

// I have no idea where this should go
ssize_t print_user(UserPtr<const void*> buf, size_t len) {
	char* kernel_buf = new char[len];
	if (!kernel_buf)
		return -ENOMEM;
	ssize_t ret = len;
	if (!copy_from_user(kernel_buf, buf, len))
		ret = -EFAULT;
	else
		hc_print(kernel_buf, len);
	delete[] kernel_buf;
	return ret;
}

bool copy_to_user(UserPtr<void*> dest, const void* src, size_t n) {
	// if (!is_user_range(dest, n))
	// 	return false;
	// ASSERT(is_kernel_range(src, n), "woops");
	return SafeMem::memcpy(dest.ptr(), src, n);
}

bool copy_from_user(void* dest, UserPtr<const void*> src, size_t n) {
	// if (!is_user_range(src, n))
	// 	return false;
	// ASSERT(is_kernel_range(dest, n), "woops");
	return SafeMem::memcpy(dest, src.ptr(), n);
}

bool copy_string_from_user(UserPtr<const char*> src, string& dst) {
	ssize_t length = SafeMem::strlen(src.ptr());
	if (length == -1)
		return false;

	// TODO: This is definitely not safe. The correct way should be allocating a
	// buffer, calling safe_memcpy and then assigning that buffer to the string.
	dst = string(src.ptr(), length);
	return true;
}


size_t strlen(const char* s) {
	size_t len = 0;
	while (*s++) len++;
	return len;
}

char* strncat(char* dest, const char* src, size_t size) {
	char* p = dest + strlen(dest);
	while ((*p++ = *src++) && size--);
	return dest;
}