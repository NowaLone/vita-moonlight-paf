#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/sysmodule.h>
#include <paf.h>

char    sceUserMainThreadName[]          = "vita_moonlight_paf";
int     sceUserMainThreadPriority        = 0x10000100;
int     sceUserMainThreadCpuAffinityMask = 0x70000;
/* GameStream/OpenSSL setup can be stack-heavy; keep enough room on the PAF main thread. */
SceSize sceUserMainThreadStackSize       = 0x8000;

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

int _start(SceSize args, void *argp) __attribute__((weak, alias("module_start")));

typedef struct {
	SceSize global_heap_size;
	int a2;
	int a3;
	int cdlg_mode;
	int heap_opt_param1;
	int heap_opt_param2;
} ScePafInit;

/* Custom module_start bypasses VitaSDK crt0, so provide its newlib lifecycle. */
void _init_vita_heap(void);
void _init_vita_reent(void);
void _init_vita_malloc(void);
void _init_vita_io(void);
void _free_vita_io(void);
void _free_vita_malloc(void);
void _free_vita_reent(void);
void _free_vita_heap(void);

void _free_vita_newlib(void)
{
	_free_vita_io();
	_free_vita_malloc();
	_free_vita_reent();
	_free_vita_heap();
}

int module_start(SceSize args, void *argp){

	int load_res;
	ScePafInit init_param;
	SceSysmoduleOpt sysmodule_opt;

	init_param.global_heap_size = 0x1000000;
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

	paf_runtime_main();

	_free_vita_newlib();
	return SCE_KERNEL_START_SUCCESS;
}

}
