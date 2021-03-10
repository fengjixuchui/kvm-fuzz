#include <iostream>
#include <fstream>
#include <sstream>
#include <thread>
#include "vm.h"
#include "corpus.h"

using namespace std;

void print_stats(const Stats& stats, const Corpus& corpus) {
	chrono::duration<double> elapsed;
	chrono::steady_clock::time_point start = chrono::steady_clock::now();
	uint64_t cases, cov, corpus_n, crashes, unique_crashes;
	double fcps, run_time, reset_time, hypercall_time, corpus_mem,
	       kvm_time, mut_time, mut1_time, mut2_time, set_input_time,
		   reset_pages, vm_exits, vm_exits_hc, update_cov_time, report_cov_time,
		   vm_exits_debug, vm_exits_cov;
	while (true) {
		this_thread::sleep_for(chrono::seconds(1));
		elapsed         = chrono::steady_clock::now() - start;
		cases           = stats.cases;
		cov             = corpus.coverage();
		corpus_n        = corpus.size();
		crashes         = stats.crashes;
		unique_crashes  = corpus.unique_crashes();
		fcps            = (double)cases / elapsed.count();
		corpus_mem      = (double)corpus.memsize() / 1024;
		vm_exits        = (double)stats.vm_exits / stats.cases;
		vm_exits_hc     = (double)stats.vm_exits_hc / stats.cases;
		vm_exits_cov    = (double)stats.vm_exits_cov / stats.cases;
		vm_exits_debug  = (double)stats.vm_exits_debug / stats.cases;
		reset_pages     = (double)stats.reset_pages / stats.cases;
		run_time        = (double)stats.run_cycles / stats.total_cycles;
		reset_time      = (double)stats.reset_cycles / stats.total_cycles;
		mut_time        = (double)stats.mut_cycles / stats.total_cycles;
		set_input_time  = (double)stats.set_input_cycles / stats.total_cycles;
		kvm_time        = (double)stats.kvm_cycles / stats.total_cycles;
		hypercall_time  = (double)stats.hypercall_cycles / stats.total_cycles;
		mut1_time       = (double)stats.mut1_cycles / stats.total_cycles;
		mut2_time       = (double)stats.mut2_cycles / stats.total_cycles;
		update_cov_time = (double)stats.update_cov_cycles / stats.total_cycles;
		report_cov_time = (double)stats.report_cov_cycles / stats.total_cycles;

		// Free stats (no rdtsc)
		printf("[%.3f] cases: %lu, fcps: %.3f, cov: %lu, corpus: %lu/%.3fKB, "
		       "unique crashes: %lu (total: %lu)\n",
		       elapsed.count(), cases, fcps, cov, corpus_n, corpus_mem,
		       unique_crashes, crashes);
		printf("\tvm exits: %.3f (hc: %.3f, cov: %.3f, debug: %.3f), "
		       "reset pages: %.3f\n",
		       vm_exits, vm_exits_hc, vm_exits_cov, vm_exits_debug,
		       reset_pages);

		if (TIMETRACE >= 1)
			printf("\trun: %.3f, reset: %.3f, mut: %.3f, set_input: %.3f, "
			       "report_cov: %.3f\n",
			       run_time, reset_time, mut_time, set_input_time,
			       report_cov_time);

		if (TIMETRACE >= 2) {
			printf("\tkvm: %.3f, hc: %.3f, update_cov: %.3f, mut1: %.3f, "
			       "mut2: %.3f\n",
			       kvm_time, hypercall_time, update_cov_time, mut1_time,
			       mut2_time);
		}
	}
}

void worker(int id, const Vm& base, Corpus& corpus, Stats& stats) {
	// The vm we'll be running
	Vm runner(base);

	// Custom RNG: avoids locks and it's simpler
	Rng rng;

	// Timetracing
	cycle_t cycles_init, cycles;

	Vm::RunEndReason reason;

	while (true) {
		Stats local_stats;
		cycles_init = _rdtsc();

		// Run some time saving stats locally
		while (_rdtsc() - cycles_init < 50000000) {
			// Get new input
			cycles = rdtsc1();
			const string& input = corpus.get_new_input(id, rng, stats);
			local_stats.mut_cycles += rdtsc1() - cycles;

			// Update input file. Make sure kernel has already submitted a
			// buffer so the input is being copied into its memory.
			// TODO: DON'T RESET THIS MEMORY AREA, maybe add other memory slot
			// for memory that we don't want to reset?
			cycles = rdtsc1();
			runner.set_file("test", input, true);
			local_stats.set_input_cycles += rdtsc1() - cycles;

			// Perform run
			cycles = rdtsc1();
			reason = runner.run(local_stats);
			local_stats.cases++;
			local_stats.run_cycles += rdtsc1() - cycles;

			// Check RunEndReason
			if (reason == Vm::RunEndReason::Crash) {
				stats.crashes++;
				corpus.report_crash(id, runner.fault());
			} else if (reason != Vm::RunEndReason::Exit) {
				die("unexpected RunEndReason: %d\n", reason);
			}

#ifdef ENABLE_COVERAGE
			// Report coverage
			cycles = rdtsc1();
			corpus.report_coverage(id, runner.coverage_bitmap());
			runner.reset_coverage();
			local_stats.report_cov_cycles += rdtsc1() - cycles;
#endif

			// Reset vm
			cycles = rdtsc1();
			runner.reset(base, local_stats);
			local_stats.reset_cycles += rdtsc1() - cycles;

			dbgprintf("run ended!\n\n");
		}
		local_stats.total_cycles = _rdtsc() - cycles_init;

		// Update global stats
		stats.update(local_stats);
	}
}

#if DEBUG == 1
#define num_threads 1
#else
#define num_threads 8
#endif

int main(int argc, char** argv) {
	cout << "Number of threads: " << num_threads << endl;
	init_kvm();
	Stats stats;
	Corpus corpus(num_threads, "../corpus");
	Vm vm(
		8 * 1024 * 1024,
		"./kernel/kernel",
		"../test_bins/readelf",
		{"./readelf", "-a", "test"}
	);

	// Virtual file, whose content will be provided by the corpus and will be
	// set before each run. We set its size to the maximum input size so kernel
	// allocs a buffer of that size.
	// Other real files should be set here as well.
	string file(corpus.max_input_size(), 'a');
	vm.set_file("test", file);
	string localtime(read_file("/etc/localtime"));
	vm.set_file("/etc/localtime", localtime);

	/* vm.run(stats);
	return 0; */

	vm.run_until(vm.resolve_symbol("main"), stats);
	//vm.run_until(0x404dd5, stats);
	//vm.run_until(0x401c80, stats); // readelf
	//vm.run_until(0x402520, stats); // objdump

	// Create threads
	vector<thread> threads;
	for (int i = 0; i < num_threads; i++) {
		threads.push_back(thread(worker, i, ref(vm), ref(corpus), ref(stats)));
	}
	threads.push_back(thread(print_stats, ref(stats), ref(corpus)));

	for (thread& t : threads)
		t.join();
	return 0;
}