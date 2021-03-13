#include <unistd.h>
#include <elf.h>
#include "kernel.h"
#include "init.h"
#include "gdt.h"
#include "mem.h"
#include "vector"

// Global kernel state
string m_elf_path;
uintptr_t m_brk;
uintptr_t m_min_brk;
unordered_map<int, File> m_open_files;
unordered_map<string, struct iovec> m_file_contents;

void init_file_contents(size_t n) {
	// For each file, get its filename and its length, allocate a buffer
	// and submit it to the hypervisor, which will write the file content to it
	void* buf;
	size_t size;
	char filename[PATH_MAX];
	for (size_t i = 0; i < n; i++) {
		hc_get_file_name(i, filename);
		size = hc_get_file_len(i);
		buf = kmalloc(size);
		hc_set_file_buf(i, buf);
		m_file_contents[string(filename)] = {
			.iov_base = buf,
			.iov_len  = size,
		};
	}
}

void* prepare_user_stack(int argc, char** argv, const VmInfo& info) {
	// Allocate stack
	uint8_t* user_stack = (uint8_t*)Mem::Virt::alloc_user_stack();

	user_stack -= 16;
	memset(user_stack, 0, 16);

	// Random bytes for auxv
	user_stack -= 16;
	uint8_t* random_bytes = user_stack;
	for (size_t i = 0; i < 16; i++)
		random_bytes[i] = i;

	// Write argv strings saving pointers to each arg
	char* argv_addrs[argc];
	size_t arg_len;
	for (int i = 0; i < argc; i++) {
		arg_len = strlen(argv[i]) + 1;
		user_stack -= arg_len;
		memcpy(user_stack, argv[i], arg_len);
		argv_addrs[i] = (char*)user_stack;
	}

	// Align stack
	user_stack = (uint8_t*)((uintptr_t)user_stack & ~0xF);

	// Set up auxp
	// Note for future Klecko: the only mandatory one seems to be AT_RANDOM.
	// Stop implementing these in an attempt to fix something.
	// Your bug is in another castle.
	Elf64_auxv_t auxv[] = {
		{AT_PHDR,   (uint64_t)info.elf_load_addr + info.phinfo.e_phoff},
		{AT_PHENT,  info.phinfo.e_phentsize},
		{AT_PHNUM,  info.phinfo.e_phnum},
		{AT_PAGESZ, PAGE_SIZE},
		{AT_BASE,   (uint64_t)info.interp_base},
		{AT_ENTRY,  (uint64_t)info.elf_entry},
		{AT_RANDOM, (uint64_t)random_bytes},
		{AT_EXECFN, (uint64_t)argv_addrs[0]},
		{AT_NULL,   0}
	};
	user_stack -= sizeof(auxv);
	memcpy(user_stack, &auxv, sizeof(auxv));

	// Set up envp
	user_stack -= 8;
	*(uint64_t*)user_stack = 0;

	// Set up argv
	user_stack -= sizeof(argv_addrs) + 8;
	memcpy(user_stack, argv_addrs, sizeof(argv_addrs));
	((uint64_t*)user_stack)[argc] = 0;

	// Set up argc
	user_stack -= 8;
	*(uint64_t*)user_stack = argc;

	printf("ARGS:\n");
	for (int i = 0; i < argc; i++)
		printf("\t%d: %s\n", i, argv_addrs[i]);

	return user_stack;
}

void jump_to_user(void* entry, void* stack) {
	printf("Jumping to user at 0x%lx!\n", entry);
	asm volatile (
		// Set user stack, RIP and RFLAGS
		"mov rsp, %[rsp];"
		"mov rcx, %[entry];"
		"mov r11, 0x2;"

		// Clear every other register
		"xor rax, rax;"
		"xor rbx, rbx;"
		"xor rdx, rdx;"
		"xor rdi, rdi;"
		"xor rsi, rsi;"
		"xor rbp, rbp;"
		"xor r8, r8;"
		"xor r9, r9;"
		"xor r10, r10;"
		"xor r12, r12;"
		"xor r13, r13;"
		"xor r14, r14;"
		"xor r15, r15;"

		// Jump to user
		"sysretq;"
		:
		: [rsp]   "a" (stack),
		  [entry] "b" (entry)
		:
	);
}

extern "C" void kmain(int argc, char** argv) {
	// Init kernel stuff
	init_tss();
	init_gdt();
	init_idt();
	init_syscall();

	printf("Hello from kernel\n");

	// Let's init kernel state. We'll need help from the hypervisor
	VmInfo info;
	hc_get_info(&info);

	// First, call constructors
	for (size_t i = 0; i < info.num_constructors; i++) {
		info.constructors[i]();
	}

	// Initialize data members
	m_elf_path   = string(info.elf_path);
	m_brk        = info.brk;
	m_min_brk    = m_brk;
	m_open_files[STDIN_FILENO]  = FileStdin();
	m_open_files[STDOUT_FILENO] = FileStdout();
	m_open_files[STDERR_FILENO] = FileStderr();
	init_file_contents(info.num_files);

	printf("Elf path: %s\n", m_elf_path.c_str());
	printf("Brk: 0x%lx\n", m_brk);
	printf("Files: %d\n", m_file_contents.size());
	for (auto v : m_file_contents) {
		printf("\t%s, length %lu\n", v.f.c_str(), v.s.iov_len);
	}

	void* user_stack = prepare_user_stack(argc, argv, info);
	jump_to_user(info.user_entry, user_stack);
}