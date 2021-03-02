#include <iostream>
#include <linux/limits.h>
#include <sys/mman.h>
#include "vm.h"

using namespace std;

// Keep this the same as in the kernel!
enum Hypercall : size_t {
	Test,
	Mmap,
	Ready,
	Print,
	GetInfo,
	GetFileLen,
	GetFileName,
	SetFileBuf,
	EndRun,
};

vaddr_t Vm::do_hc_mmap(vaddr_t addr, vsize_t size, uint64_t page_flags, int flags) {
	int supported_flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED;
	ASSERT((flags & supported_flags) == flags, "flags 0x%x", flags);
	ASSERT(flags & MAP_PRIVATE, "shared mapping");
	ASSERT(flags & MAP_ANONYMOUS, "file backed mapping");
	ASSERT((size & PTL1_MASK) == size, "not aligned size %lx", size);
	uint64_t ret = 0;
	if (flags & MAP_FIXED) {
		m_mmu.alloc(addr, size, page_flags);
		ret = addr;
	} else {
		ret = m_mmu.alloc(size, page_flags);
	}
	dbgprintf("hc mmap %lu at 0x%lx with page flags 0x%lx\n", size, ret, page_flags);
	return ret;
}

void Vm::do_hc_print(vaddr_t msg_addr) {
	string msg = m_mmu.read_string(msg_addr);
	cout << "[KERNEL] " << msg;
}

struct VmInfo {
	char elf_path[PATH_MAX];
	vaddr_t brk;
	vsize_t num_files;
	vaddr_t constructors;
	vsize_t num_constructors;
};

void Vm::do_hc_get_info(vaddr_t info_addr) {
	// Get absolute elf path, brk and other stuff
	VmInfo info;
	ERROR_ON(!realpath(m_elf.path().c_str(), info.elf_path), "elf realpath");
	info.brk = m_elf.initial_brk();
	info.num_files = m_file_contents.size();

	info.constructors = 0;
	info.num_constructors = 0;
	for (section_t& section : m_kernel.sections()) {
		if (section.name == ".ctors") {
			info.constructors = section.addr;
			info.num_constructors = section.size / sizeof(vaddr_t);
			break;
		}
	}

	m_mmu.write(info_addr, info);
}

vsize_t Vm::do_hc_get_file_len(size_t n) {
	ASSERT(n < m_file_contents.size(), "OOB n: %lu", n);
	auto it = m_file_contents.begin();
	advance(it, n);
	return it->second.length + 1;
}

void Vm::do_hc_get_file_name(size_t n, vaddr_t buf_addr) {
	ASSERT(n < m_file_contents.size(), "OOB n: %lu", n);
	auto it = m_file_contents.begin();
	advance(it, n);
	m_mmu.write_mem(buf_addr, it->first.c_str(), it->first.size() + 1);
}

void Vm::do_hc_set_file_buf(size_t n, vaddr_t buf_addr) {
	ASSERT(n < m_file_contents.size(), "OOB n: %lu", n);
	auto it = m_file_contents.begin();
	advance(it, n);
	it->second.guest_buf = buf_addr;
	m_mmu.write_mem(buf_addr, it->second.data, it->second.length + 1);
	dbgprintf("kernel set buf addr for file %s: 0x%lx\n", it->first.c_str(), buf_addr);
}

void Vm::handle_hypercall() {
	uint64_t ret = 0;
	switch (m_regs->rax) {
		case Hypercall::Test:
			die("Hypercall test, arg=0x%llx\n", m_regs->rdi);
		case Hypercall::Mmap:
			ret = do_hc_mmap(m_regs->rdi, m_regs->rsi, m_regs->rdx, m_regs->rcx);
			break;
		case Hypercall::Ready:
			m_running = false;
			return;
		case Hypercall::Print:
			do_hc_print(m_regs->rdi);
			break;
		case Hypercall::GetInfo:
			do_hc_get_info(m_regs->rdi);
			break;
		case Hypercall::GetFileLen:
			ret = do_hc_get_file_len(m_regs->rdi);
			break;
		case Hypercall::GetFileName:
			do_hc_get_file_name(m_regs->rdi, m_regs->rsi);
			break;
		case Hypercall::SetFileBuf:
			do_hc_set_file_buf(m_regs->rdi, m_regs->rsi);
			break;
		case Hypercall::EndRun:
			m_running = false;
			return;
		default:
			ASSERT(false, "unknown hypercall: %llu", m_regs->rax);
	}

	m_regs->rax = ret;
	set_regs_dirty();
}