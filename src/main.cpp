#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/sysmodule.h>
#include <paf.h>

char    sceUserMainThreadName[]          = "vita_moonlight_paf";
int     sceUserMainThreadPriority        = 0x10000100;
int     sceUserMainThreadCpuAffinityMask = 0x70000;
SceSize sceUserMainThreadStackSize       = 0x4000;

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

int paf_sample_main(void);

extern "C" {

typedef struct {
	SceSize global_heap_size;
	int a2;
	int a3;
	int cdlg_mode;
	int heap_opt_param1;
	int heap_opt_param2;
} ScePafInit;
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
	}

	paf_sample_main();

	// sceKernelExitProcess(0);

	return SCE_KERNEL_START_SUCCESS;
}

}
