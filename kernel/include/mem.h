#ifndef _MEM_H
#define _MEM_H

// Page table stuff
#define PTL4_SHIFT 39
#define PTL4_BITS   9
#define PTL4_SIZE  (1UL << PTL4_SHIFT)
#define PTL4_MASK  (~(PTL4_SIZE - 1))
#define PTRS_PER_PTL4 (1UL << PTL4_BITS)
#define PTL4_INDEX(addr) ((addr >> PTL4_SHIFT) & (PTRS_PER_PTL4 - 1))

#define PTL3_SHIFT 30
#define PTL3_BITS   9
#define PTL3_SIZE  (1UL << PTL3_SHIFT)
#define PTL3_MASK  (~(PTL3_SIZE - 1))
#define PTRS_PER_PTL3 (1UL << PTL3_BITS)
#define PTL3_INDEX(addr) ((addr >> PTL3_SHIFT) & (PTRS_PER_PTL3 - 1))

#define PTL2_SHIFT 21
#define PTL2_BITS   9
#define PTL2_SIZE  (1UL << PTL2_SHIFT)
#define PTL2_MASK  (~(PTL2_SIZE - 1))
#define PTRS_PER_PTL2 (1UL << PTL2_BITS)
#define PTL2_INDEX(addr) ((addr >> PTL2_SHIFT) & (PTRS_PER_PTL2 - 1))

#define PTL1_SHIFT 12
#define PTL1_BITS  9
#define PTL1_SIZE  (1UL << PTL1_SHIFT)
#define PTL1_MASK  (~(PTL1_SIZE - 1))
#define PTRS_PER_PTL1 (1UL << PTL1_BITS)
#define PTL1_INDEX(addr) ((addr >> PTL1_SHIFT) & (PTRS_PER_PTL1 - 1))

#define PAGE_SIZE PTL1_SIZE
#define PAGE_OFFSET(addr) ((addr) & (~PTL1_MASK))
#define PAGE_CEIL(addr) (((addr) + PAGE_SIZE - 1) & PTL1_MASK) //+ 0xFFF & ~0xFFF

#define PHYS_MASK (0x000FFFFFFFFFF000)
#define PHYS_FLAGS(addr) ((addr) & (~PHYS_MASK))

namespace Mem {
	namespace Phys {
		void init_memory();
		uintptr_t alloc_frame();
		void free_frame(uintptr_t frame);
		void* virt(uintptr_t phys);
		size_t amount_free_memory();
	}

	namespace Virt {
		void* alloc(size_t len, uint64_t flags);
		void  alloc(void* addr, size_t len, uint64_t flags);
		void* alloc_user_stack();
		bool is_range_free(void* addr, size_t len);
		void free(void* addr, size_t len);
		void set_flags(void* addr, size_t len, uint64_t flags);
		bool enough_free_memory(size_t length);
	}
}

#endif