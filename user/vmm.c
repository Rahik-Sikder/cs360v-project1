#include <inc/lib.h>
#include <inc/vmx.h>
#include <inc/elf.h>
#include <inc/ept.h>
#include <inc/stdio.h>

#define GUEST_KERN "/vmm/kernel"
#define GUEST_BOOT "/vmm/boot"

#define JOS_ENTRY 0x7000

// Map a region of file fd into the guest at guest physical address gpa.
// The file region to map should start at fileoffset and be length filesz.
// The region to map in the guest should be memsz.  The region can span multiple pages.
//
// Return 0 on success, <0 on failure.
//
// Hint: Call sys_ept_map() for mapping page. 
static int
map_in_guest( envid_t guest, uintptr_t gpa, size_t memsz, 
	      int fd, size_t filesz, off_t fileoffset ) {
	/* Your code here */

	envid_t host_id = sys_getenvid();

	// Loop through all the pages in the region
	for(int i = 0; i < filesz; i+=PGSIZE){

		// Need to malloc page to copy from fd
		void *host_va;
		sys_page_alloc(host_id, host_va, __EPTE_FULL);
		if(host_va == NULL){
			return -E_NO_MEM;
		}

		// Move fd and read
		size_t to_read = MIN(PGSIZE, filesz - i);
		if(readn(fd, host_va, to_read) != to_read){
			free(host_va);
			return -E_INVAL;
		}	
		
		// map
		int result;
		if(result = sys_ept_map(host_id, host_va, guest, (void*)(gpa + i), __EPTE_FULL) < 0){
			free(host_va);
			return result;
		}

	}

	return 0;
} 

// Read the ELF headers of kernel file specified by fname,
// mapping all valid segments into guest physical memory as appropriate.
//
// Return 0 on success, <0 on error
//
// Hint: compare with ELF parsing in env.c, and use map_in_guest for each segment.
static int
copy_guest_kern_gpa( envid_t guest, char* fname ) {
	/* Your code here */

	// Get env
	struct Env *guest_env;
	if(envid2env(guest, &guest_env, true) < 0)
		return -E_BAD_ENV;

	int fd;
	if((fd = open(fname, O_RDONLY)) < 0){
		return fd;
	}

	struct Elf *elf;
	struct Proghdr *ph, *eph;

	elf = malloc(sizeof(struct Elf));
	if(elf == NULL)
		return -E_NO_MEM;

	if(readn(fd, elf, sizeof(struct Elf)) != sizeof(struct Elf)
		|| elf->e_magic != ELF_MAGIC){
		close(fd);
		return -E_INVAL;
	}

	if (elf && elf->e_magic == ELF_MAGIC) {
		ph = (struct Proghdr *)((uint8_t *)elf + elf->e_phoff);
		eph = ph + elf->e_phnum;
		for(;ph < eph; ph++) {
			if (ph->p_type == ELF_PROG_LOAD) {
				int result;
				if(result = map_in_guest(guest, (void *)ph->p_va, ph->p_memsz, fd, ph->p_filesz, ph->p_offset) < 0){
					return result;
				}
			}	
		}

	}
	close(fd);
	free(elf);

	return 0;
}

void
umain(int argc, char **argv) {
	int ret;
	envid_t guest;
	char filename_buffer[50];	//buffer to save the path 
	int vmdisk_number;
	int r;
	if ((ret = sys_env_mkguest( GUEST_MEM_SZ, JOS_ENTRY )) < 0) {
		cprintf("Error creating a guest OS env: %e\n", ret );
		exit();
	}
	guest = ret;

	// Copy the guest kernel code into guest phys mem.
	if((ret = copy_guest_kern_gpa(guest, GUEST_KERN)) < 0) {
		cprintf("Error copying page into the guest - %d\n.", ret);
		exit();
	}

	// Now copy the bootloader.
	int fd;
	if ((fd = open( GUEST_BOOT, O_RDONLY)) < 0 ) {
		cprintf("open %s for read: %e\n", GUEST_BOOT, fd );
		exit();
	}

	// sizeof(bootloader) < 512.
	if ((ret = map_in_guest(guest, JOS_ENTRY, 512, fd, 512, 0)) < 0) {
		cprintf("Error mapping bootloader into the guest - %d\n.", ret);
		exit();
	}
#ifndef VMM_GUEST	
	sys_vmx_incr_vmdisk_number();	//increase the vmdisk number
	//create a new guest disk image
	
	vmdisk_number = sys_vmx_get_vmdisk_number();
	snprintf(filename_buffer, 50, "/vmm/fs%d.img", vmdisk_number);
	
	cprintf("Creating a new virtual HDD at /vmm/fs%d.img\n", vmdisk_number);
        r = copy("vmm/clean-fs.img", filename_buffer);
        
        if (r < 0) {
        	cprintf("Create new virtual HDD failed: %e\n", r);
        	exit();
        }
        
        cprintf("Create VHD finished\n");
#endif
	// Mark the guest as runnable.
	sys_env_set_status(guest, ENV_RUNNABLE);
	wait(guest);
}


