#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/sysmodule.h>
#include <paf.h>
#include <pthread.h>
#include <stdlib.h>

char    sceUserMainThreadName[]          = "vita_moonlight_paf";
int     sceUserMainThreadPriority        = 0x10000100;
int     sceUserMainThreadCpuAffinityMask = 0x70000;
/* UI thread still builds pages and formats strings. GameStream itself runs on a worker. */
SceSize sceUserMainThreadStackSize       = 0x20000;

void *operator new(unsigned int n) {
	return sce_paf_malloc(n);
}

void *operator new[](unsigned int n) {
	return sce_paf_malloc(n);
}

void operator delete(void *ptr) {
	sce_paf_free(ptr);
}

void operator delete[](void *ptr) {
	sce_paf_free(ptr);
}

void operator delete(void *ptr, unsigned int n) {
	sce_paf_free(ptr);
}

void operator delete[](void *ptr, unsigned int n) {
	sce_paf_free(ptr);
}

int paf_runtime_main(void);

extern "C" {

#ifdef MOONLIGHT_SYSTEM_APP
/* Strong C-linkage override consumed by VitaSDK newlib's _init_vita_heap(). */
unsigned int _newlib_heap_size_user = MOONLIGHT_SYSTEM_NEWLIB_KIB * 1024u;
#endif

int _start(SceSize args, void *argp) __attribute__((weak, alias("module_start")));

typedef struct {
	SceSize global_heap_size;
	int a2;
	int a3;
	int cdlg_mode;
	int heap_opt_param1;
	int heap_opt_param2;
} ScePafInit;

/* Custom module_start bypasses VitaSDK crt0; own the newlib/pthread lifecycle. */
void _init_vita_heap(void);
void _init_vita_reent(void);
void _init_vita_malloc(void);
void _init_vita_io(void);
void _free_vita_io(void);
void _free_vita_malloc(void);
void _free_vita_reent(void);
void _free_vita_heap(void);
static int s_pthreads_initialized;

/*
 * -nostartfiles/-nostdlib removes crti/crtn, which normally provide
 * _init/_fini.  This module has its own entry point and initializes
 * newlib explicitly, so these hooks intentionally remain no-ops.
 */
void _init(void)
{
}

void _fini(void)
{
}

void _free_vita_newlib(void)
{
	if (s_pthreads_initialized) {
		pthread_terminate();
		s_pthreads_initialized = 0;
	}
	_free_vita_io();
	_free_vita_malloc();
	_free_vita_reent();
	_free_vita_heap();
}

int module_start(SceSize args, void *argp){

	int load_res;
	ScePafInit init_param;
	SceSysmoduleOpt sysmodule_opt;

#ifdef MOONLIGHT_SYSTEM_APP
	init_param.global_heap_size = MOONLIGHT_SYSTEM_PAF_KIB * 1024u;
#else
	init_param.global_heap_size = 0x1000000;
#endif
	init_param.a2               = 0xEA60;
	init_param.a3               = 0x40000;
	init_param.cdlg_mode        = 0;
	init_param.heap_opt_param1  = 0;
	init_param.heap_opt_param2  = 0;

	load_res = 0xDEADBEEF;
	sysmodule_opt.flags  = 0;
	sysmodule_opt.result = &load_res;

	int res = sceSysmoduleLoadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, sizeof(init_param), &init_param, &sysmodule_opt);
	if((res | load_res) != 0){
		sceClibPrintf("[PAF Moonlight] Failed to load the PAF prx. (return value 0x%x, result code 0x%x )\n", res, load_res);
		return SCE_KERNEL_START_FAILED;
	}

	/* The custom PAF entry point bypasses VitaSDK crt0, so initialize newlib manually. */
	_init_vita_heap();
	_init_vita_reent();
	_init_vita_malloc();
	_init_vita_io();

#ifdef MOONLIGHT_SYSTEM_APP
	/* _init_vita_heap() returns void even on failure. Fail before using the
	 * frontend if the smaller allocator could not be brought up at all. */
	void *heap_probe = malloc(1);
	if (heap_probe == NULL) {
		sceClibPrintf("[PAF Moonlight] System-mode newlib heap unavailable (%u KiB)\n",
			(unsigned int)MOONLIGHT_SYSTEM_NEWLIB_KIB);
		_free_vita_newlib();
		return SCE_KERNEL_START_FAILED;
	}
	free(heap_probe);
#endif

	/* crt0 would run pthread_setup from .init_array. Without it, pthread_self
	 * cannot retain its TLS identity and leaks a thread record on every call.
	 * VitaSDK pthread_init returns nonzero on success (not a POSIX error code).
	 */
	if (pthread_init() == 0) {
		sceClibPrintf("[PAF Moonlight] Failed to initialize pthread runtime\n");
		_free_vita_newlib();
		return SCE_KERNEL_START_FAILED;
	}
	s_pthreads_initialized = 1;
	pthread_t main_thread = pthread_self();
	if (main_thread == 0 || !pthread_equal(main_thread, pthread_self())) {
		sceClibPrintf("[PAF Moonlight] pthread TLS identity unavailable\n");
		_free_vita_newlib();
		return SCE_KERNEL_START_FAILED;
	}

	paf_runtime_main();

	_free_vita_newlib();
	return SCE_KERNEL_START_SUCCESS;
}

}
