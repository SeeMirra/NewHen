#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysent.h>
#include <sys/proc.h>

#include <vm/vm.h>
#include <vm/vm_page.h>
#include <vm/pmap.h>
#include <vm/vm_map.h>
#include <vm/vm_param.h>


#include <oni/utils/kdlsym.h>
#include <oni/messaging/messagemanager.h>
#include <oni/plugins/pluginmanager.h>
#include <oni/init/initparams.h>
#include <oni/utils/log/logger.h>
#include <oni/utils/sys_wrappers.h>
#include <oni/rpc/rpcserver.h>


uint8_t* gKernelBase = 0;

struct messagemanager_t* gMessageManager = NULL;
struct rpcserver_t* gRpcServer = NULL;
struct pluginmanager_t* gPluginManager = NULL;
struct logger_t* gLogger = NULL;
struct initparams_t* gInitParams = NULL;

void initkernel();
void kernelstartup(void* args);

int initalize()
{
	syscall(11, initkernel);

	return 1;
}

void initkernel()
{
	gKernelBase = 0xFFFFFFFFDEADC0DE;

	uint8_t* userlandPayload = 0xDEADC0DEDEADC0DE;
	uint32_t userlandPayloadLength = 0xDEADC0DE;

	void(*critical_enter)(void) = kdlsym(critical_enter);
	void(*crtical_exit)(void) = kdlsym(critical_exit);
	vm_offset_t(*kmem_alloc)(vm_map_t map, vm_size_t size) = kdlsym(kmem_alloc);
	void(*printf)(char *format, ...) = kdlsym(printf);
	int(*kproc_create)(void(*func)(void*), void* arg, struct proc** newpp, int flags, int pages, const char* fmt, ...) = kdlsym(kproc_create);
	vm_map_t map = (vm_map_t)(*(uint64_t *)(kdlsym(kernel_map)));

	critical_enter();
	// todo: disable write protection

	// todo: patch kmem_alloc, kmem_back to allow rwx
	// todo: patch copyinstr/copyoutstr to allow kernel stack addresses
	// todo: enable map self
	// todo: patch memcpy
	// todo: patch ptrace

	// todo: enable write protection
	crtical_exit();

	uint8_t* kernelPayload = (uint8_t*)kmem_alloc(map, userlandPayload);
	if (!kernelPayload)
		return;

	uint64_t kernelStartupSlide = (uint64_t)kernelstartup - (uint64_t)userlandPayload;
	uint8_t* kernelStartup = kernelPayload + kernelStartupSlide;
	if (!kernelStartup)
		return;

	mlock((void*)kernelPayload, userlandPayloadLength);

	struct initparams_t* initParams = (struct initparams_t*)kmem_alloc(map, sizeof(struct initparams_t));
	if (!initParams)
		return;

	initParams->payloadBase = (uint64_t)kernelPayload;
	initParams->payloadSize = userlandPayloadLength;
	initParams->process = 0;

	kmemset(kernelPayload, 0, userlandPayloadLength);
	kmemcpy(kernelPayload, userlandPayload, userlandPayloadLength);

	critical_enter();
	kproc_create((void(*)(void*))kernelStartup, initParams, &initParams->process, 0, 0, "MiraHEN");
	crtical_exit();
}

void kernelstartup(void* args)
{
	struct vmspace* (*vmspace_alloc)(vm_offset_t min, vm_offset_t max) = kdlsym(vmspace_alloc);
	void(*pmap_activate)(struct thread *td) = kdlsym(pmap_activate);
	struct sysentvec* sv = kdlsym(self_orbis_sysvec);

	void(*critical_enter)(void) = kdlsym(critical_enter);
	void(*crtical_exit)(void) = kdlsym(critical_exit);

	gLogger = (struct logger_t*)kmalloc(sizeof(struct logger_t));
	if (!gLogger)
	{
		kthread_exit();
		return;
	}
	logger_init(gLogger);

	struct initparams_t* loaderInitParams = (struct initparams_t*)args;
	if (!loaderInitParams)
	{
		kthread_exit();
		return;
	}

	gInitParams = (struct initparams_t*)kmalloc(sizeof(struct initparams_t));
	if (!gInitParams)
	{
		kthread_exit();
		return;
	}

	gInitParams->payloadBase = loaderInitParams->payloadBase;
	gInitParams->payloadSize = loaderInitParams->payloadSize;
	gInitParams->process = loaderInitParams->process;

	vm_offset_t sv_minuser = MAX(sv->sv_minuser, PAGE_SIZE);
	struct vmspace* vmspace = vmspace_alloc(sv_minuser, sv->sv_maxuser);
	if (!vmspace)
	{
		kthread_exit();
		return;
	}

	gInitParams->process->p_vmspace = vmspace;
	if (gInitParams->process == curthread->td_proc)
		pmap_activate(curthread);

	gMessageManager = (struct messagemanager_t*)kmalloc(sizeof(struct messagemanager_t));
	if (!gMessageManager)
	{
		kthread_exit();
		return;
	}
	messagemanager_init(gMessageManager);

	gPluginManager = (struct pluginmanager_t*)kmalloc(sizeof(struct pluginmanager_t));
	if (!gPluginManager)
	{
		kthread_exit();
		return;
	}
	pluginmanager_init(gPluginManager);

	gRpcServer = (struct rpcserver_t*)kmalloc(sizeof(struct rpcserver_t));
	if (!gRpcServer)
	{
		kthread_exit();
		return;
	}
	rpcserver_init(gRpcServer, gInitParams->process);

	if (!rpcserver_startup(gRpcServer, 1337))
	{
		kthread_exit();
		return;
	}

	kthread_exit();
}
