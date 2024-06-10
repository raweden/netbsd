
#include <sys/stdint.h>
#include <sys/namei.h>
#include <sys/vnode.h>
#include <sys/exec.h>

#include <stddef.h>

#include <sys/errno.h>
#include <sys/kmem.h>
#include "arch/wasm/mm/mm.h"
#include "libwasm.h"
#include <sys/malloc.h>
#include <sys/mutex.h>
#include <sys/null.h>

#include "arch/wasm/libwasm/rtld.h"
#include "stdbool.h"
#include "wasmloader.h"
#include "loader.h"
#include "kld_ioctl.h"

// FIXME: hacky path
#include <wasm/../mm/mm.h>
#include <wasm/../../../../libexec/ld.wasm/rtld_paths.h>

// breaking away from the initial concept for loading dynamic linked executable.
// from the begining it was implemented enterly in the kernel, using kernel memory..
// 
// since every dynamic linked executable is going to need rtld awyways, the new concept
// is to simply reloc the rtld which will take over the loading of everything else.

static const char __rtldmodule_path[] = "/libexec/ld-wasm.so.1";

#define RTLD_CACHE_UNINIT 0
#define RTLD_CACHE_LOADED 1
#define RTLD_CACHE_FALURE 3

#define NBDL_SUBSEC_MODULES 0x01
#define NBDL_SUBSEC_RLOCEXT 0x07
#define NBDL_SUBSEC_RLOCINT 0x08

struct wasm_dylink0_subsec {
    uint32_t type;
    uint32_t size;
    uint32_t file_offset;
};

struct ldwasm_cache {
    uint32_t state;                     // state of the cache.
    kmutex_t lock;
    struct mm_arena *arena;
    struct wash_exechdr_rt *exechdr;
    struct wasm_module_rt *module;
    struct wasm_dylink0_subsec *dylink0_sections;
    int dylink0_count;
    struct {
        uint8_t *min_leb_p;     // pointer to the start of the uleb which stores memory.initial
        uint8_t min_lebsz;
        uint8_t max_lebsz;
    } mem;
    uint8_t *exec_data;         // memory cache of the entire binary of rtld
};

union i32_value {
    uint32_t value;
    unsigned char bytes[4];
};

#undef DEBUG_DL_LOADING
#define DEBUG_DL_LOADING 1

#if defined (DEBUG_DL_LOADING) && DEBUG_DL_LOADING
#define dbg_loading(...) printf(__VA_ARGS__)
#else
#define dbg_loading(...)
#endif

static struct ldwasm_cache rtld_cache;

int _rtld_find_import_memory(uint8_t *data_start, uint8_t *data_end, uint32_t off, struct wasm_loader_meminfo *meminfo);
struct wasm_exechdr_secinfo *_rtld_exechdr_find_section(struct wash_exechdr_rt *exehdr, int sectype, const char *secname);

int load_ldwasm_read_dylink0(struct wasm_module_rt *, struct wash_exechdr_rt *, uint8_t *);
int load_ldwasm_decode_dylink0_modules(struct wasm_module_rt *, uint8_t *);

int
load_rtld_module_from_disk(void)
{
    struct wasm_exechdr_secinfo *sec;
    struct wash_exechdr_rt *exechdr;
    struct wasm_module_rt *module;
    struct wasm_loader_meminfo meminfo;
    struct nameidata nd;
	struct pathbuf *pb;
	struct vnode   *vp;
	struct vattr vap;
	struct mm_page *pg;
	struct lwp *l;
	uint32_t pg_flags;
	uint32_t ver;
    uint32_t off, fbufsz, bsize, fsize;
    int32_t execfd;
    size_t skiplen;
    uint32_t pathlen;
    char *path;
    char *errstr;
    char *ptr;
    char *modbuf;
    uint8_t *modbufp;
    uint8_t *exechdr_p;
    union {
        struct wasm_loader_cmd_mkbuf mkbuf;
        struct wasm_loader_cmd_wrbuf wrbuf;
        struct wasm_loader_cmd_compile ccbuf;
        struct wasm_loader_cmd_run run;
        struct wasm_loader_cmd_cp_kmem_to_umem cp_kmem;
    } exec_cmd;
    uint32_t bufsz;
    uint32_t count;
    size_t exechdr_off, exechdr_size;
	int error;

    modbuf = NULL;

    mutex_spin_enter(&rtld_cache.lock);

    if (rtld_cache.state == RTLD_CACHE_FALURE) {
        mutex_spin_exit(&rtld_cache.lock);
        return ENOEXEC;
    } else if (rtld_cache.state != RTLD_CACHE_UNINIT) {
        mutex_spin_exit(&rtld_cache.lock);
        return EBUSY;
    }

    module = NULL;

	l = (struct lwp *)curlwp;
    path = PNBUF_GET();
    pathlen = strlen(__rtldmodule_path);
    strlcpy(path, __rtldmodule_path, pathlen + 1);
    pb = pathbuf_assimilate(path);
	NDINIT(&nd, LOOKUP, FOLLOW | TRYEMULROOT, pb);

	/* first get the vnode */
	if ((error = namei(&nd)) != 0) {
        pathbuf_destroy(pb);
        rtld_cache.state = RTLD_CACHE_FALURE;
        mutex_spin_exit(&rtld_cache.lock);
        dbg_loading("error: could not find %s error = %d\n", __rtldmodule_path, error);
		return error;
    }

	vp = nd.ni_vp;

    vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);

	/* XXX VOP_GETATTR is the only thing that needs LK_EXCLUSIVE here */
	if ((error = VOP_GETATTR(vp, &vap, l->l_cred)) != 0)
		goto loading_failure;

	// the allocated size of ep_hdr size is of exec_maxhdrsz which is set at init by computing the max value from all
    // the defined exec handlers. Its set trough exec_sw->es_hdrsz

#if 0
	/* now we have the file, get the exec header */
	error = vn_rdwr(UIO_READ, vp, epp->ep_hdr, epp->ep_hdrlen, 0,
			UIO_SYSSPACE, IO_NODELOCKED, l->l_cred, &resid, NULL);
	if (error)
		goto bad1;
#endif

	// If we have come this far try to load the first chunk of the file and check signature.
	//vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);

#if 0
    if (*((uint32_t *)hdrstr) != WASM_HDR_SIGN_LE) {
        printf("not a wasm executable %d at %p\n", *((uint32_t *)hdrstr), hdrstr);
        return ENOEXEC;
    }

    ver = ((uint32_t *)hdrstr)[1];
    if (ver != 0x01) {
        return ENOEXEC;
    }
#endif

    // if we reached here we have a binary with a valid signature.
    fsize = vap.va_size;
    bsize = vap.va_blocksize;

    rtld_cache.arena = mm_arena_create_simple("ldwasm_cache", NULL);
    if (rtld_cache.arena == NULL) {
        printf("%s failed to create allocation zone for ldwasm-cache\n", __func__);
        error = ENOMEM;
        goto loading_failure;
    }

    modbuf = mm_arena_alloc_simple(rtld_cache.arena, fsize, NULL);
    if (modbuf == NULL) {
        printf("%s failed to alloc pages for module-size = %d\n", __func__, fsize);
        error = ENOMEM;
        goto loading_failure;
    }

    modbufp = (uint8_t *)modbuf;
    rtld_cache.exec_data = (uint8_t *)modbufp;

    // TODO: when trying to use a dynld which uses reloc, try to alloc pages to cover it within kernel memory
    //       and perform all ops on that memory, and once its ready read in the essential section into execbuf.

    exec_read((struct lwp *)curlwp, vp, 0, modbufp, fsize, IO_NODELOCKED);

    VOP_UNLOCK(vp);

    exechdr_off = 0;
    if (wasm_loader_has_exechdr_in_buf(modbuf, fsize, &exechdr_off, &exechdr_size) == false) {
        printf("%s missing rtld.exec-hdr as first section\n", __func__);
        goto loading_failure;;
    }

    exechdr = (struct wash_exechdr_rt *)(modbuf + exechdr_off);
    if (exechdr->hdr_version != 1) {
        printf("%s unsupported rtld.exec-hdr version; %d\n", __func__, exechdr->hdr_version);
        goto loading_failure;
    }

    rtld_reloc_exechdr(modbuf + exechdr_off);


    printf("%s modbuf = %p\n", __func__, modbuf);

    // 2. find memory import
    sec = _rtld_exechdr_find_section(exechdr, WASM_SECTION_IMPORT, NULL);
    if (sec == NULL) {
        printf("%s no import memory\n", __func__);
        goto loading_failure;
    }

    modbufp = (uint8_t *)modbuf;

    error = _rtld_find_import_memory(modbufp + sec->file_offset, modbufp + (sec->file_offset + sec->sec_size), sec->file_offset, &meminfo);
    if (error != 0) {
        printf("%s got error = %d from rtld_find_module_memory()\n", __func__, error);
        return EINVAL;
    }
    printf("%s meminfo min = %d max = %d shared = %d min_file_offset = %d min_lebsz = %d max_lebsz %d\n", __func__, meminfo.min, meminfo.max, meminfo.shared, meminfo.min_file_offset, meminfo.min_lebsz, meminfo.max_lebsz);

    rtld_cache.mem.min_leb_p = modbufp + meminfo.min_file_offset;
    rtld_cache.mem.min_lebsz = meminfo.min_lebsz;
    rtld_cache.mem.max_lebsz = meminfo.max_lebsz;

#if 0
    // 3. find data-segment info (just offset + size)
    sec = wasm_find_section(wa_mod, WASM_SECTION_DATA, NULL);
    if (sec) {
        rtld_read_data_segments_info(dlctx, wa_mod, sec);
    }
#endif

    module = mm_arena_zalloc_simple(rtld_cache.arena, sizeof(struct wasm_module_rt), NULL);
    if (module == NULL) {
        error = ENOMEM;
        goto loading_failure;
    }

    module->exechdr = exechdr;
    rtld_cache.module = module;
    rtld_cache.exechdr = exechdr;

    // finding & reading rtld.dylink.0 section
    error = load_ldwasm_read_dylink0(module, exechdr, modbufp);
    if (error != 0) {
        goto loading_failure;
    }

    error = load_ldwasm_decode_dylink0_modules(module, modbufp);
    if (error != 0) {
        goto loading_failure;
    }

    pathbuf_destroy(pb);    // pathbuf calls PNBUF_PUT on path

    vput(vp);

	return 0;

loading_failure:

    rtld_cache.state = RTLD_CACHE_FALURE;
    mutex_spin_exit(&rtld_cache.lock);

    if (rtld_cache.arena != NULL) {
        mm_arena_destroy_simple(rtld_cache.arena);
    }

    pathbuf_destroy(pb);    // pathbuf calls PNBUF_PUT on path

    return error;
}

int
load_ldwasm_read_dylink0(struct wasm_module_rt *module, struct wash_exechdr_rt *exechdr, uint8_t *data_start)
{
    struct wasm_exechdr_secinfo *sec;
    struct wasm_dylink0_subsec *subsec, *subsec_p;
    uint32_t namesz;
    uint8_t kind;
    uint32_t count, lebsz;
    uint8_t *ptr, *ptr_start, *end;
    const char *errstr;
    int error;


    // finding rtld.dylink.0 section
    sec = _rtld_exechdr_find_section(exechdr, WASM_SECTION_CUSTOM, "rtld.dylink.0");
    if (sec == NULL) {
        printf("%s missing dylink.0 section.. cannot link!", __func__);
        error = ENOEXEC;
        goto loading_failure;
    }
    
    errstr = NULL;

    ptr_start = data_start + sec->file_offset;
    end = ptr_start + sec->sec_size;
    ptr = ptr_start + sec->hdrsz;

    namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;
    if (namesz != 13 || strncmp((const char *)ptr, "rtld.dylink.0", namesz) != 0) {
        printf("%s not a rtld.dylink.0 section..\n", __func__);
        return EINVAL;
    }
    ptr += namesz;

    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    if (count > 8) {
        printf("%s current ABI specifies 8 segment types and all unique, why is there %d\n",__func__, count);
        return EINVAL;
    }
    
    subsec = mm_arena_zalloc_simple(rtld_cache.arena, sizeof(struct wasm_dylink0_subsec) * count, NULL);
    if (subsec == NULL) {
        printf("%s not enough memory for %d wasm_dylink0_subsec\n", __func__, count);
        error = ENOMEM;
        goto loading_failure;
    }

    rtld_cache.dylink0_count = count;
    rtld_cache.dylink0_sections = subsec;
    subsec_p = subsec;

    for (int i = 0; i < count; i++) {
        uint32_t size;
        uint32_t kind = *(ptr);
        ptr++;
        size = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        subsec_p->type = kind;
        subsec_p->size = size;
        subsec_p->file_offset = (uint32_t)(ptr - data_start);

        printf("%s dylink-subsection kind = %d size = %d src_offset = %d\n", __func__, subsec_p->type, subsec_p->size, subsec_p->file_offset);

        subsec_p++;
        ptr += size;
    }

    return (0);

loading_failure:

    return error;
}

struct wasm_dylink0_subsec *
load_ldwasm_find_dylink0_subsection(uint32_t type)
{
    struct wasm_dylink0_subsec *subsec_p;
    uint32_t count;

    count = rtld_cache.dylink0_count;
    subsec_p = rtld_cache.dylink0_sections;
    
    if (count == 0) {
        return NULL;
    }

    for (int i = 0; i < count; i++) {
        if (subsec_p->type == type)
            return subsec_p;

        subsec_p++;
    }

    return NULL;
}

struct rtld_segment *
rtld_find_data_segment(struct wasm_module_rt *module, const char *name)
{
    struct rtld_segment *segment;
    uint32_t namesz;
    uint32_t count;

    if (module == NULL || name == NULL)
        return NULL;

    count = module->data_segments_count;
    if (count == 0)
        return NULL;

    namesz = strlen(name);
    segment = module->data_segments;

    for (int i = 0; i < count; i++) {
        if (segment->namesz == namesz && strncmp(name, segment->name, namesz) == 0) {
            return segment;
        }
        segment++;
    }

    return NULL;
}

struct wasm_exechdr_secinfo *
_rtld_exechdr_find_section(struct wash_exechdr_rt *exehdr, int sectype, const char *secname)
{
    struct wasm_exechdr_secinfo *sec;
    uint32_t namesz, count;

    if (exehdr->section_cnt == 0 || exehdr->secdata == NULL) {
        return NULL;
    }

    if (sectype == WASM_SECTION_CUSTOM) {

        if (secname == NULL) {
            return NULL;
        }

        namesz = strlen(secname);

        sec = exehdr->secdata;
        count = exehdr->section_cnt;
        for (int i = 0; i < count; i++) {
            if (sec->wasm_type == WASM_SECTION_CUSTOM && sec->namesz == namesz && strncmp(sec->name, secname, namesz) == 0) {
                return sec;
            }
            sec++;
        }

    } else {

        sec = exehdr->secdata;
        count = exehdr->section_cnt;
        for (int i = 0; i < count; i++) {
            printf("%s sec->type %d\n", __func__, sec->wasm_type);
            if (sec->wasm_type == sectype) {
                return sec;
            }
            sec++;
        }
    }

    return NULL;
}

/**
 * Retrives the following parameters from the `netbsd.dylink0` section:
 * - self module name + version
 * - needed module names + version
 * - element segments info.
 * - data segments info.
 */
int
load_ldwasm_decode_dylink0_modules(struct wasm_module_rt *module, uint8_t *file_start)
{
    struct mm_arena *dl_arena;
    struct wasm_exechdr_secinfo *data_sec;
    struct wasm_dylink0_subsec *section;
    struct rtld_segment *wa_elem;
    struct _rtld_needed_entry *wa_needed;
    struct rtld_segment *arr;
    struct rtld_segment *seg;
    char *vers;
    char *name;
    uint32_t verssz;
    uint32_t namesz;
    uint32_t count, lebsz;
    uint32_t min_data_off, max_data_off, data_off_start;
    uint8_t *ptr, *end, *ptr_start;
    const char *errstr;
    int error;

    // find section
    section = load_ldwasm_find_dylink0_subsection(NBDL_SUBSEC_MODULES);
    if (section == NULL) {
        return EINVAL;
    }

    ptr_start = file_start + section->file_offset;
    end = ptr_start + section->size;
    ptr = ptr_start;
    errstr = NULL;
    
    name = NULL;
    namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;
    if (namesz != 0) {
        name = mm_arena_alloc_simple(rtld_cache.arena ,namesz + 1, NULL);
        strlcpy(name, (const char *)ptr, namesz + 1);
        ptr += namesz;
    }
    module->dso_name = name;
    module->dso_namesz = namesz;

    vers = NULL;
    verssz = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;
    if (verssz != 0) {
        vers = mm_arena_alloc_simple(rtld_cache.arena, verssz + 1, NULL);
        strlcpy(vers, (const char *)ptr, verssz + 1);
        ptr += verssz;
    }
    module->dso_vers = vers;
    module->dso_verssz = verssz;

    printf("%s module-name = '%s' module-vers = '%s'\n", __func__, module->dso_name, module->dso_vers);

    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    if (count > 0) {
        struct _rtld_needed_entry *prev_ent;

        wa_needed = mm_arena_zalloc_simple(rtld_cache.arena, count * sizeof(struct _rtld_needed_entry), NULL);
        if (wa_needed == NULL) {
            printf("%s failed to alloc wa_needed..\n", __func__);
            error = ENOMEM;
            goto loading_failure;
        }

        module->needed = wa_needed;
        prev_ent = NULL;

        for (int i = 0; i < count; i++) {
            uint8_t type, vers_type;
            uint32_t vers_count;

            if (prev_ent)
                prev_ent->next = wa_needed;

            type = *(ptr);
            ptr++;
            namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;
            if (namesz != 0) {
                name = mm_arena_alloc_simple(rtld_cache.arena, namesz + 1, NULL);
                strlcpy(name, (const char *)ptr, namesz + 1);
                ptr += namesz;
                wa_needed->name = name;
                wa_needed->namesz = namesz;
            }
            vers_count = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;
            if (vers_count != 0) {
                vers_type = *(ptr);
                ptr++;
                if (vers_type == 1) {
                    vers = NULL;
                    verssz = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                    if (verssz != 0) {
                        vers = mm_arena_alloc_simple(rtld_cache.arena, verssz + 1, NULL);
                        if (vers != NULL)
                            strlcpy(vers, (const char *)ptr, verssz + 1);
                        ptr += verssz;
                    }
                    wa_needed->vers = vers;
                    wa_needed->verssz = verssz;
                } else if (vers_type == 2) {
                    vers = NULL;
                    verssz = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                    if (verssz != 0) {
                        ptr += verssz;
                    }
                    vers = NULL;
                    verssz = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                    if (verssz != 0) {
                        ptr += verssz;
                    }
                }
            }

            printf("%s needed module name '%s' vers = '%s'\n", __func__, wa_needed->name, wa_needed->vers);
            prev_ent = wa_needed;
            wa_needed++;
        }
    }

    // element segments
    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    if (count > 0) {

        if (module->elem_segments == NULL) {
            wa_elem = mm_arena_zalloc_simple(rtld_cache.arena, sizeof(struct rtld_segment) * count, NULL);
            if (wa_elem == NULL) {
                printf("%s failed to allocate wa_elem_segments vector..\n", __func__);
                return ENOMEM;
            }
            module->elem_segments = wa_elem;
        }

        module->elem_segments_count = count;

        for (int i = 0; i < count; i++) {
            uint8_t type, vers_type;
            uint32_t segidx, seg_align, seg_size, seg_dataSize;
            type = *(ptr);
            ptr++;
            namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            name = NULL;
            if (namesz != 0) {
                name = mm_arena_alloc_simple(rtld_cache.arena, namesz + 1, NULL);
                if (name != NULL)
                    strlcpy(name, (const char *)ptr, namesz + 1);
                ptr += namesz;
            }
            segidx = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            seg_align = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;
            
            seg_size = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            seg_dataSize = 0; // Todo: enable this!

            wa_elem->namesz = namesz;
            wa_elem->name = name;
            wa_elem->size = seg_size;
            wa_elem->align = seg_align;
            //wa_elem->size = seg_dataSize;

            dbg_loading("%s element-segment @%p type = %d name = %s (namesz = %d) size = %lu (data-size: %d) align = %d\n", __func__, wa_elem, type, wa_elem->name, namesz, wa_elem->size, 0, wa_elem->align);

            wa_elem++;
        }
    }

    // data segments
    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    if (count > 0) {

        arr = mm_arena_zalloc_simple(rtld_cache.arena, sizeof(struct rtld_segment) * count, NULL);
        if (arr == NULL) {
            return ENOMEM;
        }
        seg = arr;
        data_sec = _rtld_exechdr_find_section(module->exechdr, WASM_SECTION_DATA, NULL);
        // data_sec might be null if binary only uses .bss

        if (data_sec != NULL) {
            min_data_off = 1;                       // TODO: + lebsz for count
            max_data_off = data_sec->sec_size;      // TODO: - lebsz for count
            data_off_start = data_sec->file_offset + data_sec->hdrsz;
        } else {
            min_data_off = 1;
            max_data_off = 0;
            data_off_start = 0;
        }

        for(int i = 0; i < count; i++) {
            uint32_t dataoff, flags, max_align, size;
            dataoff = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;
            flags = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;
            max_align = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;
            size = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;
            namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            name = NULL;
            
            if (namesz != 0) {
                name = mm_arena_alloc_simple(rtld_cache.arena, namesz + 1, NULL);
                strlcpy(name, (const char *)ptr, namesz + 1);
                ptr += namesz;
            }
            if (dataoff >= min_data_off && dataoff < max_data_off) {
                dataoff += data_off_start;
            } else if (dataoff != 0) {
                printf("%s data-offset %d for data segment is out of range (min: %d max: %d)\n", __func__, dataoff, min_data_off, max_data_off);
                return EINVAL;
            }

            seg->flags = flags;
            seg->align = max_align;
            seg->size = size;
            seg->namesz = namesz;
            seg->name = name;
            seg->src_offset = dataoff;

            dbg_loading("%s index = %d name %s size = %lu align = %d data-offset = %d\n", __func__, i, seg->name, seg->size, seg->align, seg->src_offset);
            seg++;
        }

        module->data_segments_count = count;
        module->data_segments = arr;
    }

    // uleb count

    // u8 type
    // uleb name-sz 
    // bytes name
    // uleb count
    // u8 vers-type
    // if == 1
    // uleb name-sz bytes name
    // if == 2
    // min | uleb vers-sz bytes vers 
    // max | uleb vers-sz bytes vers

    // followed by element-segment metadata
    // count
    // u8 type
    // uleb name-sz
    // bytes name
    // uleb segidx
    // uleb max_align
    // uleb size

    // followed by data-segment metadata
    // count
    // uleb segidx
    // uleb flags
    // uleb max_align
    // uleb size
    // uleb name-sz
    // bytes name

    return (0);

loading_failure:

    return (error);
}

static uintptr_t
alignUp(uintptr_t addr, uint32_t align, uint32_t *pad)
{
    uintptr_t rem;
    if (addr != 0 && align != 0) {
        rem = addr % align;
        if (rem != 0) {
            rem = align - rem;
            addr += rem;
            if (pad) {
                *pad += rem;
            }
        }
    }

    return addr;
}

int
reloc_ldwasm_place_segments(struct wasm_module_rt *module, uintptr_t *data_base, uintptr_t *ftbl_base)
{
    struct rtld_segment *data_seg;
    struct rtld_segment *elem_seg;
    struct wash_exechdr_rt *hdr;
    uint32_t wapgs;
    uint32_t segcount;
    int error;
    union {
        struct wasm_loader_cmd_cp_kmem_to_umem cp_kmem;
        struct wasm_loader_cmd_mk_umem mkmem;
        struct wasm_loader_cmd_umem_grow mgrow;
        struct wasm_loader_cmd_umem_grow tblgrow;
        struct wasm_loader_cmd_mk_table mktbl;
    } exec_cmd;
    uintptr_t mem_start = 1024;
    uintptr_t tbl_start = 1;

    if (data_base)
        mem_start = *data_base;

    if (ftbl_base)
        tbl_start = *ftbl_base;

    uintptr_t mem_off = mem_start;
    uint32_t mem_pad = 0;
    uint32_t tbl_off = tbl_start;
    uint32_t tbl_pad = 0;
    int32_t tbl_min = 1;
    int32_t tbl_max = -1;
    uint32_t module_rt_size, module_rt_off;
    struct wasm_module_rt *rtld_module_src;

    // compute initial data-segments
    segcount = module->data_segments_count;
    data_seg = module->data_segments;
    printf("%s module-name = %s address = %p\n", __func__, module->dso_name, module);
    for (int x = 0; x < segcount; x++) {
        
        mem_off = alignUp(mem_off, data_seg->align, &mem_pad);
        data_seg->addr = mem_off;
        mem_off += data_seg->size;
        printf("%s placing %s %s at %lu\n", __func__, module->dso_name, data_seg->name, data_seg->addr);

        data_seg++;
    }

    // computing needed memory for all struct wasm_module_rt
    // used in dynamic-library info struct for dlsym/dlinfo/dlopen etc.
    module_rt_size = sizeof(struct wasm_module_rt);
    mem_off = alignUp(mem_off, 8, &mem_pad);
    module_rt_off = mem_off;
    mem_off += module_rt_size;

    // compute initial element-segments
    // all objc_indirect_functions are appended after each other
    segcount = module->elem_segments_count;
    elem_seg = module->elem_segments;
    for (int x = 0; x < segcount; x++) {
        tbl_off = alignUp(tbl_off, elem_seg->align, &tbl_pad);
        elem_seg->addr = tbl_off;
        tbl_off += elem_seg->size;
        printf("%s placing %s %s at %lu\n", __func__, module->dso_name, elem_seg->name, elem_seg->addr);
        elem_seg++;
    }

    // create env.__linear_memory
    exec_cmd.mkmem.min = 10;
    exec_cmd.mkmem.max = 4096;      // FIXME: take from wa_mod->meminfo
    exec_cmd.mkmem.shared = true;
    exec_cmd.mkmem.bits = 32;
    error = wasm_exec_ioctl(EXEC_IOCTL_MAKE_UMEM, &exec_cmd);
    if (error != 0) {
        printf("%s got error = %d after growing memory with delta = %d (WASM_PAGE_SIZE)\n", __func__, error, wapgs);
        goto reloc_failure;
    }
    // FIXME: should store metadata about memory

    // grow env.__linear_memory
    wapgs = howmany(mem_off, WASM_PAGE_SIZE);
    exec_cmd.mgrow.grow_size = wapgs;
    exec_cmd.mgrow.grow_ret = 0;
    error = wasm_exec_ioctl(EXEC_IOCTL_UMEM_GROW, &exec_cmd);
    if (error != 0) {
        printf("%s got error = %d after growing memory with delta = %d (WASM_PAGE_SIZE)\n", __func__, error, wapgs);
        goto reloc_failure;
    }

    // create env.__indirect_table
    exec_cmd.mktbl.min = tbl_min;
    exec_cmd.mktbl.max = tbl_max;
    exec_cmd.mktbl.reftype = 0x70;
    error = wasm_exec_ioctl(EXEC_IOCTL_UTBL_MAKE, &exec_cmd);
    if (error != 0) {
        printf("%s got error = %d after creating table with desc {min: %d max: %d reftype: %d}\n", __func__, error, exec_cmd.mktbl.min, exec_cmd.mktbl.max, exec_cmd.mktbl.reftype);
        goto reloc_failure;
    }

    // grow env.__indirect_table
    exec_cmd.tblgrow.grow_size = tbl_off - tbl_start;
    exec_cmd.tblgrow.grow_ret = 0;
    error = wasm_exec_ioctl(EXEC_IOCTL_UTBL_GROW, &exec_cmd);
    if (error != 0) {
        printf("%s got error = %d after growing table with delta = %d", __func__, error, exec_cmd.tblgrow.grow_size);
        goto reloc_failure;
    }

    if (data_base)
        *data_base = mem_off;

    if (ftbl_base)
        *ftbl_base = tbl_off;

    return (0);

reloc_failure:

    return (error);
}

int
ldwasm_copyout(const void *src, uint32_t size, uintptr_t dst)
{
    struct wasm_loader_cmd_cp_kmem_to_umem cp_kmem; 

    cp_kmem.buffer = -1;
    cp_kmem.dst_offset = dst;
    cp_kmem.src = src;
    cp_kmem.size = size;

    return wasm_exec_ioctl(EXEC_IOCTL_CP_KMEM_TO_UMEM, &cp_kmem);
}

uintptr_t
reloc_ldwasm_find_symbol(struct wasm_module_rt *module, const char *name)
{
    struct rtld_segment *seg_p, *dynsym_seg, *dynstr_seg;
    struct dlsym_rt *sym_p, *sym_end;
    uint8_t *file_start;
    uintptr_t dynstr_rloc, dynstr_addr;
    char *dynstr_file;
    char *dynstr;
    int count;
    int namesz;

    namesz = strlen(name);

    dynsym_seg = rtld_find_data_segment(module, ".dynsym");
    dynstr_seg = rtld_find_data_segment(module, ".dynstr");

    if (dynsym_seg == NULL || dynstr_seg == NULL) {
        return 0;
    }

    file_start = rtld_cache.exec_data;
    sym_p = (void *)(file_start + dynsym_seg->src_offset);
    sym_end = (void *)(file_start + dynsym_seg->src_offset + dynsym_seg->size);
    dynstr_rloc = dynstr_seg->addr;
    dynstr_file = (char *)(file_start + dynstr_seg->src_offset);

    while (sym_p < sym_end) {
        if (sym_p->namesz == namesz) {
            dynstr_addr = (uintptr_t)(sym_p->name - dynstr_rloc);
            dynstr = dynstr_file + dynstr_addr;

            if (strncmp(name, dynstr, namesz) == 0) {
                return sym_p->addr;
            }
        }
        
        sym_p++;
    }

    return (0);
}

int
reloc_ldwasm_copy_object(struct wasm_module_rt *module, uintptr_t *data_base, int32_t mem_min, int32_t mem_max)
{
    struct wasm_module_rt dst_module;
    struct rtld_memory_descriptor dst_memdesc;
    struct rtld_segment dst_segdesc;
    struct rtld_segment *seg_p;
    uintptr_t mem_off = *data_base;
    uintptr_t module_off;
    uintptr_t memdesc_off;
    uintptr_t segdesc_off;
    uintptr_t strtblsz, strtbl_off, stroff;
    uint32_t strsz;
    int count, segcount;
    int error;

    strtblsz = 0;
    memcpy(&dst_module, module, sizeof(struct wasm_module_rt));

    if (module->dso_name != NULL)
        strtblsz += module->dso_namesz + 1;

    if (module->dso_vers != NULL)
        strtblsz += module->dso_verssz + 1;

    if (module->filepath != NULL)
        strtblsz += strlen(module->filepath) + 1;

    // element segments names
    count = module->elem_segments_count;
    segcount = count;
    if (count != 0) {
        seg_p = module->elem_segments;
        for (int i = 0; i < count; i++) {
            if (seg_p->name != NULL) {
                strtblsz += seg_p->namesz + 1;
            }
            seg_p++;
        }
    }

    // data segments names
    count = module->data_segments_count;
    if (count != 0) {
        segcount += count;
        seg_p = module->data_segments;
        for (int i = 0; i < count; i++) {
            if (seg_p->name != NULL) {
                strtblsz += seg_p->namesz + 1;
            }
            seg_p++;
        }
    }

    module_off = reloc_ldwasm_find_symbol(module, "__rtld_objself");
    memdesc_off = reloc_ldwasm_find_symbol(module, "__rtld_memdesc");

    printf("%s reloc_ldwasm_find_symbol() returned module_off = %lu\n", __func__, module_off);
    printf("%s reloc_ldwasm_find_symbol() returned memdesc_off = %lu\n", __func__, memdesc_off);

    if (module_off == 0 || memdesc_off == 0) {
        printf("%s missing symbols\n", __func__);
        goto reloc_failure;
    }

    // reserve memory in user-space
    segdesc_off = alignUp(mem_off, 8, NULL);
    mem_off += sizeof(struct rtld_segment) * segcount;

    strtbl_off = alignUp(mem_off, 4, NULL);
    mem_off = (strtbl_off + strtblsz);

    *data_base = mem_off;

    // copying module strings
    if (module->dso_name != NULL) {
        strsz = module->dso_namesz + 1;
        ldwasm_copyout(module->dso_name, strsz, strtbl_off);
        dst_module.dso_name = (void *)strtbl_off;
        strtbl_off += strsz;
    }

    if (module->dso_vers != NULL) {
        strsz = module->dso_verssz + 1;
        ldwasm_copyout(module->dso_vers, strsz, strtbl_off);
        dst_module.dso_vers = (void *)strtbl_off;
        strtbl_off += strsz;
    }

    if (module->filepath != NULL) {
        strsz += strlen(module->filepath) + 1;
        ldwasm_copyout(module->filepath, strsz, strtbl_off);
        dst_module.filepath = (void *)strtbl_off;
        strtbl_off += strsz;
    }

    dst_memdesc.initial = mem_min;
    dst_memdesc.maximum = mem_max;
    dst_memdesc.shared = true;
    ldwasm_copyout(&dst_memdesc, sizeof(dst_memdesc), memdesc_off);
    dst_module.memdesc = (void *)memdesc_off;



    // copying element segment descriptors
    count = module->elem_segments_count;
    if (count != 0) {
        seg_p = module->elem_segments;
        dst_module.elem_segments = (void *)segdesc_off;
        for (int i = 0; i < count; i++) {
            if (seg_p->name != NULL) {
                stroff = strtbl_off;
                strtbl_off += (seg_p->namesz + 1);
                ldwasm_copyout(seg_p->name, seg_p->namesz + 1, stroff);
            } else {
                stroff = 0;
            }
            memcpy(&dst_segdesc, seg_p, sizeof(dst_segdesc));
            dst_segdesc.name = (void *)stroff;

            ldwasm_copyout(&dst_segdesc, sizeof(dst_segdesc), segdesc_off);

            // increment for next run.
            segdesc_off += sizeof(dst_segdesc);
            seg_p++;
        }
    }

    // copying data segment descriptors
    count = module->data_segments_count;
    if (count != 0) {
        seg_p = module->data_segments;
        dst_module.data_segments = (void *)segdesc_off;
        for (int i = 0; i < count; i++) {
            if (seg_p->name != NULL) {
                stroff = strtbl_off;
                strtbl_off += (seg_p->namesz + 1);
                ldwasm_copyout(seg_p->name, seg_p->namesz + 1, stroff);
            } else {
                stroff = 0;
            }
            memcpy(&dst_segdesc, seg_p, sizeof(dst_segdesc));
            dst_segdesc.name = (void *)stroff;

            ldwasm_copyout(&dst_segdesc, sizeof(dst_segdesc), segdesc_off);

            // increment for next run.
            segdesc_off += sizeof(dst_segdesc);
            seg_p++;
        }
    }

    seg_p = rtld_find_data_segment(module, ".dynsym");
    if (seg_p) {
        dst_module.dlsym_start = (void *)seg_p->addr;
        dst_module.dlsym_end = (void *)(seg_p->addr + seg_p->size);
    }

    seg_p = rtld_find_data_segment(module, ".init_array");
    if (seg_p) {
        dst_module.init_array = (void *)seg_p->addr;
        dst_module.init_array_count = (seg_p->size / sizeof(void *));
    }

    seg_p = rtld_find_data_segment(module, ".fnit_array");
    if (seg_p) {
        dst_module.fnit_array = (void *)seg_p->addr;
        dst_module.fnit_array_count = (seg_p->size / sizeof(void *));
    }

    dst_memdesc.initial = mem_min;
    dst_memdesc.maximum = mem_max;
    dst_memdesc.shared = true;
    
    error = ldwasm_copyout(&dst_memdesc, sizeof(struct rtld_memory_descriptor), memdesc_off);
    if (error != 0) {
        printf("%s got error = %d from cpy kmem to umem\n", __func__, error);
        goto reloc_failure;
    }


    error = ldwasm_copyout(&dst_module, sizeof(struct wasm_module_rt), module_off);
    if (error != 0) {
        printf("%s got error = %d from cpy kmem to umem\n", __func__, error);
        goto reloc_failure;
    }

    return (0);

reloc_failure:

    return (error);
}

int
reloc_ldwasm_internal_reloc(struct wasm_module_rt *module)
{
    struct wasm_dylink0_subsec *section;
    struct rtld_segment *data_segments;
    struct rtld_segment *elem_segments;
    struct wasm_exechdr_secinfo *code_section;
    int error;
    uint32_t elem_count, data_count;
    uint32_t count, lebsz;
    uint8_t *ptr, *end, *ptr_start, *file_start, *rloc, *sec_start;
    const char *errstr;
    union i32_value i32;

    // find section
    code_section = NULL;
    section = load_ldwasm_find_dylink0_subsection(NBDL_SUBSEC_RLOCINT);
    if (rtld_cache.exec_data == NULL || section == NULL) {
        return EINVAL;
    }

    file_start = rtld_cache.exec_data;
    ptr = file_start + section->file_offset;
    end = ptr + section->size;
    ptr_start = ptr;
    errstr = NULL;
    
    // TODO: this loading must adopt to reading back chunks..

    elem_segments = module->elem_segments;
    elem_count = module->elem_segments_count;

    data_segments = module->data_segments;
    data_count = module->data_segments_count;

    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    for(int i = 0; i < count; i++) {
        uint32_t chunksz, dst_idx, src_type, src_idx, rloc_count, dst_off, dst_base, dst_max, dst_addr, src_off, src_base;
        uint8_t rloctype;

        rloctype = *(ptr);
        ptr++;
        chunksz = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        sec_start = ptr;
        // only R_WASM_MEMORY_ADDR_I32 has dst_idx
        if (rloctype == R_WASM_MEMORY_ADDR_I32 || rloctype == R_WASM_TABLE_INDEX_I32) {
            dst_idx = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            if (dst_idx >= data_count) {
                printf("%s dst_idx %d to large\n", __func__, dst_idx);
            }
            dst_base = data_segments[dst_idx].src_offset;
            dst_max = (dst_base + data_segments[dst_idx].size) - 4;
            printf("%s dst_base = %d of rloctype = %d\n", __func__, dst_base, rloctype);

            if ((data_segments[dst_idx].flags & _RTLD_SEGMENT_NOT_EXPORTED) != 0) {
                printf("%s trying to reloc non-exported data segment..\n", __func__);
            }

            src_type = *(ptr);
            ptr++;
            src_idx = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            if (src_type == 1) { // data-segment
                if (src_idx >= data_count) {
                    printf("%s src_idx %d to large for data count %d\n", __func__, src_idx, data_count);
                    error = EINVAL;
                    goto reloc_failure;
                }
                src_base = data_segments[src_idx].addr;
            } else if (src_type == 2) { // elem-segment
                if (src_idx >= elem_count) {
                    printf("%s src_idx %d to large for elem count %d\n", __func__, src_idx, elem_count);
                    error = EINVAL;
                    goto reloc_failure;
                }
                src_base = elem_segments[src_idx].addr;
            } else {
                printf("%s INVALID_SRC_TYPE = %d\n", __func__, src_type);
                error = EINVAL;
                goto reloc_failure;
            }

            rloc_count = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            //printf("%s src_base = %d of src_type = %d rloc_count = %d\n", __func__, src_base, src_type, rloc_count);

            for (int x = 0; x < rloc_count; x++) {
                dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                dst_addr = dst_base + dst_off;                
                if (dst_addr < dst_base || dst_addr > dst_max) {
                    printf("%s ERROR i32_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d) segment = %s\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base, data_segments[dst_idx].name);
                }

                i32.value = src_base + src_off;
                rloc = file_start + dst_addr;
                rloc[0] = i32.bytes[0];
                rloc[1] = i32.bytes[1];
                rloc[2] = i32.bytes[2];
                rloc[3] = i32.bytes[3];
            }

        } else {
            src_type = *(ptr);
            ptr++;
            src_idx = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            if (code_section == NULL) {
                code_section = _rtld_exechdr_find_section(module->exechdr, WASM_SECTION_CODE, NULL);
                if (code_section == NULL) {
                    printf("%s ERROR could not find code-section for code-reloc\n", __func__);
                    error = ENOENT;
                    goto reloc_failure;
                }
            }

            dst_base = code_section->file_offset;
            dst_max = (dst_base + code_section->sec_size) - 5;
            dst_base += code_section->hdrsz;

            printf("%s dst_base = %d dst_max = %d\n", __func__, dst_base, dst_max);

            if (src_type == 1) { // data-segment
                if (src_idx >= data_count) {
                    printf("%s ERROR src_idx %d to large for data count %d\n", __func__, src_idx, data_count);
                    error = EINVAL;
                    goto reloc_failure;
                }
                src_base = data_segments[src_idx].addr;
                //printf("%s src_base = %d of type = %d\n", __func__, src_base, src_type);
            } else if (src_type == 2) { // elem-segment
                if (src_idx >= elem_count) {
                    printf("%s ERROR src_idx %d to large for elem count %d\n", __func__, src_idx, elem_count);
                    error = EINVAL;
                    goto reloc_failure;
                }
                src_base = elem_segments[src_idx].addr;
                //printf("%s src_base = %d of type = %d\n", __func__, src_base, src_type);
            } else {
                //printf("%s INVALID_SRC_TYPE = %d\n", __func__, src_type);
                error = EINVAL;
                goto reloc_failure;
            }

            rloc_count = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;

            if (rloctype == R_WASM_MEMORY_ADDR_LEB) {

                for (int x = 0; x < rloc_count; x++) {
                    dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                    src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                    dst_addr = dst_base + dst_off;
                    if (dst_addr <= dst_base || dst_addr > dst_max) {
                        printf("%s ERROR leb_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d)\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base);
                    }
                    rloc = file_start + dst_addr;
                    encodeULEB128(src_base + src_off, rloc, 5);
                }

            } else if (rloctype == R_WASM_MEMORY_ADDR_SLEB || rloctype == R_WASM_TABLE_INDEX_SLEB) {

                for (int x = 0; x < rloc_count; x++) {
                    dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                    src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                    ptr += lebsz;
                    dst_addr = dst_base + dst_off;
                    if (dst_addr < dst_base || dst_addr > dst_max) {
                        printf("%s ERROR leb_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d)\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base);
                    }
                    rloc = file_start + dst_addr;
                    encodeSLEB128(src_base + src_off, rloc, 5);
                }
            } else {
                printf("%s invalid reloc-type %d @%p\n", __func__, rloctype, ptr);
            }
        }

        ptr = sec_start + chunksz;
    }

    dbg_loading("%s did all relocs\n", __func__);

    return (0);

reloc_failure: 

    printf("%s ERROR %d failed to complete relocs\n", __func__, error);

    return error;
}

int
reloc_ldwasm_external_reloc(struct wasm_module_rt *module)
{
#if 0
    // extern reloc is similar to internal, only that these are weakly linked trough a name which are checked against other modules.
    struct wasm_loader_module_dylink_state *dl_state;
    struct wasm_loader_dylink_section *section;
    struct wa_data_segment_info *data_segments;
    struct wa_section_info *code_section;
    char *name;
    uint32_t namesz;
    int error;
    uint32_t data_count;
    uint32_t count, lebsz;
    uint32_t module_count;
    uint32_t execfd;
    uint32_t rloc_count;
    struct wa_module_info **modules;
    struct wa_module_info *submod;
    uint8_t *ptr, *end, *ptr_start, *file_start, *chunk_start;
    const char *errstr;
    union i32_value i32;
    union {
        struct wasm_loader_dynld_dlsym dlsym;
    } exec_cmd;

    // find section
    code_section = NULL;
    execfd = module->wa_module_desc_id;
    dl_state = module->wa_dylink_data;
    section = wasm_dylink0_find_subsection(dl_state, NBDL_SUBSEC_RLOCEXT);
    if (section == NULL) {
        return EINVAL;
    }

    dbg_loading("%s external reloc on module = %s addr = %p\n", __func__, module->wa_module_name, module);

    file_start = (uint8_t *)module->wa_filebuf;
    ptr_start = file_start + section->src_offset;
    ptr = ptr_start;
    end = ptr_start + section->size;
    errstr = NULL;

    data_segments = module->wa_data_segments;
    data_count = module->wa_data_count;

    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    dbg_loading("%s count %d module = %s\n", __func__, count, module->wa_module_name);


    // 
    module_count = dlctx->dl_module_count;
    modules = dlctx->dl_modules;

    code_section = wasm_find_section(module, WASM_SECTION_CODE, NULL);
    if (code_section == NULL) {
        printf("%s ERROR could not find code-section for code-reloc\n", __func__);
        error = ENOENT;
        goto errout;
    }

    for(int i = 0; i < count; i++) {
        uint32_t chunksz, dst_idx, src_type, src_idx, rloc_count, dst_off, src_off, src_base;
        uint8_t symtype;
        uint8_t *dst_base, *dst_max, *dst_addr, *dst;
        bool found;

        symtype = *(ptr);
        ptr++;
        chunksz = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        chunk_start = ptr;
        namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        name = (char *)ptr;
        ptr += namesz;

        if (symtype == 0) {
            symtype = 1;
        } else if (symtype == 1) {
            symtype = 2;
        } else {
            printf("non-supported symtype %d in module %s\n", symtype, module->wa_module_name);
            symtype = 255;
        }

        found = false;

        for (int x = 0; x < module_count; x++) {

            submod = modules[x];
            if (submod == module) {
                continue;
            }

            if (submod->wa_dylink_data == NULL || submod->wa_dylink_data->dl_dlsym == NULL) {
                printf("%s module = %s (%p) missing .dynsym data..\n", __func__, submod->wa_module_name, submod);
                continue;
            }

            struct wa_data_segment_info *dlsym = submod->wa_dylink_data->dl_dlsym;

            if (dlsym == NULL)
                continue;
            
            exec_cmd.dlsym.dynsym_start = (void *)dlsym->wa_dst_offset;
            exec_cmd.dlsym.dynsym_end = (void *)(dlsym->wa_dst_offset + dlsym->wa_size);
            exec_cmd.dlsym.symbol_name = name;
            exec_cmd.dlsym.symbol_size = namesz;
            exec_cmd.dlsym.symbol_type = symtype;
            exec_cmd.dlsym.symbol_addr = 0;
            error = wasm_exec_ioctl(EXEC_IOCTL_DYNLD_DLSYM_EARLY, &exec_cmd);
            if (error != 0) {
                continue;
            }

            found = true;
            src_base = exec_cmd.dlsym.symbol_addr;
            dbg_loading("%s found symbol '%s' on module '%s' at = %p\n", __func__, name, submod->wa_module_name, (void *)src_base);
        }

        if (!found) {
            printf("%s symbol '%s' not found (namesz = %d chunksz = %d)\n", __func__, name, namesz, chunksz);
            ptr = chunk_start + chunksz;
            continue;
        }

        // uleb relocs
        rloc_count = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (rloc_count > 0) {

            dst_base = file_start + code_section->wa_offset;
            dst_max = dst_base + (code_section->wa_size - 5);

            for (int x = 0; x < rloc_count; x++) {
                dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                dst_addr = dst_base + dst_off;
                if (dst_addr <= dst_base || dst_addr > dst_max) {
                    printf("%s ERROR leb_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d)\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base);
                }
                encodeULEB128(src_base + src_off, dst_addr, 5);
            }
        }

        // sleb relocs
        rloc_count = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (rloc_count > 0) {

            dst_base = file_start + code_section->wa_offset;
            dst_max = dst_base + (code_section->wa_size - 5);

            for (int x = 0; x < rloc_count; x++) {
                dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                dst_addr = dst_base + dst_off;
                if (dst_addr <= dst_base || dst_addr > dst_max) {
                    printf("%s ERROR leb_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d)\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base);
                }
                encodeSLEB128(src_base + src_off, dst_addr, 5);
            }
        }

        // data relocs
        rloc_count = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        if (rloc_count > 0) {
            
            for (int x = 0; x < rloc_count; x++) {
                dst_idx = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;

                if (dst_idx >= data_count) {
                    printf("%s dst_idx %d to large (in %d of %d, ptr = %p ptr_start %p)\n", __func__, dst_idx, x, rloc_count, ptr, ptr_start);
                }
                dst_base = file_start + data_segments[dst_idx].wa_src_offset;
                dst_max = dst_base + (data_segments[dst_idx].wa_size - 4);
                dst_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                src_off = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                dst_addr = dst_base + dst_off;
                if (dst_addr < dst_base || dst_addr > dst_max) {
                    printf("%s ERROR i32_vec->addr %p (%d + %d) is outside of allowed range %p to %p (size = %d) segment = %s\n", __func__, (void *)dst_addr, dst_base, dst_off, (void *)dst_base, (void *)dst_max, dst_max - dst_base, data_segments[dst_idx].wa_name);
                }
                i32.value = src_base + src_off;
                dst = dst_addr;
                dst[0] = i32.bytes[0];
                dst[1] = i32.bytes[1];
                dst[2] = i32.bytes[2];
                dst[3] = i32.bytes[3];
            }
        }

        ptr = chunk_start + chunksz;
    }

    printf("%s did all external relocs\n", __func__);

    return (0);

errout: 

    printf("%s failed to complete relocs\n", __func__);

    return error;
#endif
    return (0);
}

int
reloc_ldwasm_copy_segments(struct wasm_module_rt *module)
{
    struct rtld_segment *data_seg;
    uint32_t count, segcount;
    int error;

    // copy memory segments into user-space memory
    segcount = module->data_segments_count;
    data_seg = module->data_segments;
    
    printf("%s now copying %d data-segments from kmem to umem\n", __func__, segcount);

    for (int x = 0; x < segcount; x++) {
        int flags = data_seg->flags;
        if ((flags & _RTLD_SEGMENT_ZERO_FILL) != 0) {
            printf("%s TODO ensure that range is zero-initialized!\n", __func__);
        } else if (data_seg->addr != 0) {

            printf("%s copying data at %d name = %s from %p (kmem) to %p (umem) of size = %lu\n", __func__, x, data_seg->name, (void *)(rtld_cache.exec_data + data_seg->src_offset), (void *)(data_seg->addr), data_seg->size);

            error = ldwasm_copyout(rtld_cache.exec_data + data_seg->src_offset, data_seg->size, data_seg->addr);
            if (error != 0) {
                printf("%s got error = %d from EXEC_IOCTL_CP_KMEM_TO_UMEM\n", __func__, error);
            }

        } else {
            printf("%s ERROR %s %s missing wa_dst_offset\n", __func__, module->dso_name, data_seg->name);
        }
        data_seg++;
    }

    return (0);
}


int
reloc_ldwasm_module(uintptr_t *data_base, int32_t memory_min, int32_t memory_max, uintptr_t *ftbl_base)
{
    struct wasm_exechdr_secinfo *section;
    struct wasm_module_rt *objself;
    union {
        struct wasm_loader_cmd_mkbuf mkbuf;
        struct wasm_loader_cmd_wrbuf wrbuf;
        struct wasm_loader_cmd_run run;
        struct wasm_loader_cmd_cp_kmem_to_umem cp_kmem;
    } exec_cmd;
    uint32_t fsize, foffset;
    uint32_t sec_count;
    uint8_t *ptr;
    char errstr[255];
    int execfd; // not to be confused with the execfd sent as aux param.
    int error;

    if (memory_min == -1 || memory_max == -1) {
        printf("%s initial %d or maximum %d memory are unset, which is currently not supported..\n", __func__, memory_min, memory_max);
        return ENOEXEC;
    }
    
    if (rtld_cache.mem.min_leb_p != NULL) {
        ptr = rtld_cache.mem.min_leb_p;
        encodeULEB128(memory_min, ptr, rtld_cache.mem.min_lebsz);
        ptr += rtld_cache.mem.min_lebsz;
        encodeULEB128(memory_max, ptr, rtld_cache.mem.max_lebsz);
    }

    objself = rtld_cache.module;

    error = reloc_ldwasm_place_segments(objself, data_base, ftbl_base);
    if (error != 0) {
        printf("%s got error = %d after wasm_loader_dynld_do_internal_reloc()\n", __func__, error);
    }

    error = reloc_ldwasm_internal_reloc(objself);
    if (error != 0) {
        printf("%s got error = %d after reloc_ldwasm_internal_reloc()\n", __func__, error);
    }

    error = reloc_ldwasm_external_reloc(objself);
    if (error != 0) {
        printf("%s got error = %d after reloc_ldwasm_external_reloc()\n", __func__, error);
    }

    error = reloc_ldwasm_copy_segments(objself);
    if (error != 0) {
        printf("%s got error = %d after reloc_ldwasm_copy_segments()\n", __func__, error);
    }

    error = reloc_ldwasm_copy_object(objself, data_base, memory_min, memory_max);
    if (error != 0) {
        printf("%s got error = %d after reloc_ldwasm_copy_object()\n", __func__, error);
    }

    fsize = 8;
    foffset = 8;

    sec_count = objself->exechdr->section_cnt;
    section = objself->exechdr->secdata;

    for (int i = 0; i < sec_count; i++) {
        uint32_t sec_type = section->wasm_type;
        if (sec_type > WASM_SECTION_CUSTOM && sec_type < WASM_SECTION_DATA) {
            fsize += section->sec_size;
        } else if (sec_type == WASM_SECTION_CUSTOM && section->namesz == 4 && strncmp(section->name, "name", 4) == 0) {
            fsize += section->sec_size;
        }
        section++;
    }


    exec_cmd.mkbuf.buffer = -1;
    exec_cmd.mkbuf.size = fsize; // TODO: use exec_end_offset later on, but first we must get it up and running..
    error = wasm_exec_ioctl(EXEC_IOCTL_MKBUF, &exec_cmd);
    execfd = exec_cmd.mkbuf.buffer;

    // reset section pointer for a start over
    section = objself->exechdr->secdata;
    ptr = rtld_cache.exec_data;

    // copy signature..
    exec_cmd.wrbuf.buffer = execfd;
    exec_cmd.wrbuf.offset = 0;
    exec_cmd.wrbuf.size = 8;
    exec_cmd.wrbuf.src = ptr;
    error = wasm_exec_ioctl(EXEC_IOCTL_WRBUF, &exec_cmd);

    for (int i = 0; i < sec_count; i++) {
        uint32_t sec_type = section->wasm_type;
        bool include = false;
        if (sec_type > WASM_SECTION_CUSTOM && sec_type < WASM_SECTION_DATA) {
            include = true;
        } else if (sec_type == WASM_SECTION_CUSTOM && section->namesz == 4 && strncmp(section->name, "name", 4) == 0) {
            include = true;
        }

        if (include) {
            exec_cmd.wrbuf.buffer = execfd;
            exec_cmd.wrbuf.offset = foffset;
            exec_cmd.wrbuf.size = section->sec_size;
            exec_cmd.wrbuf.src = ptr + section->file_offset;
            error = wasm_exec_ioctl(EXEC_IOCTL_WRBUF, &exec_cmd);

            // incrementing offset for next section.
            foffset += section->sec_size;
        }

        section++;
    }


    // before sending the executable of to new WebAssembly.Module
    // there is some internal state which the exec needs to share with 
    // the rtld module which will take over the loading from here.
    // 
    // TODO: to setup the state use .dynsym along with .dynstr to do lookup
    // for a specific symbol (convert reloc offsets to kernel offsets)
    // copy the memory of the symbol, apply changes and put modified memory back.
    // 
    // setup state on __rtld_state
    // setup of rtld object 

    exec_cmd.run.buffer = execfd;
    exec_cmd.run.flags = 0;
    exec_cmd.run.err = 0;
    exec_cmd.run.strsz = 256;
    exec_cmd.run.strbuf = errstr;
    error = wasm_exec_ioctl(EXEC_IOCTL_RUN_MODULE_AS_DYN_LD, &exec_cmd);
    if (error != 0) {
        printf("%s got error = %d from running module as dynld\n", __func__, error);
    }

#if 0
    // finding __rtld_state
    static const char sym__rtld_objself = "__rtld_objself";
    rtld_state = reloc_ldwasm_find_symbol(module, rtld_cache.exec_data, sym__rtld_objself, sizeof(sym__rtld_objself));
#endif
    // TODO: we need set certain properties on __rtld_objself but it much easier todo so when memory have been copied
    //       and apply such changes there, doing it before requires a translation back to where it originates in the file..
    // needs to specify:
    // objself->data_segments
    // objself->elem_segments
    // objself->filepath
    // etc..

	return 0;

loading_failure:

    rtld_cache.state = RTLD_CACHE_FALURE;
    mutex_spin_exit(&rtld_cache.lock);

    return error;
}


int
setup_ldwasm_module_from_cache(uintptr_t *relocbase, int32_t mem_min, int32_t mem_max)
{
    uintptr_t fntbl_base;
    int error;

    error = 0;
    fntbl_base = 1;

    mutex_spin_enter(&rtld_cache.lock);
    if (rtld_cache.state == RTLD_CACHE_UNINIT) {
        error = load_rtld_module_from_disk();
    }

    if (error) {
        mutex_spin_exit(&rtld_cache.lock);
        goto loading_failure;
    }

    error = reloc_ldwasm_module(relocbase, mem_min, mem_max, &fntbl_base);
    if (error != 0) {
        printf("%s got error = %d from reloc_ldwasm_module()\n", __func__, error);
        mutex_spin_exit(&rtld_cache.lock);
        goto loading_failure;
    }

    mutex_spin_exit(&rtld_cache.lock);

    return (0);

loading_failure:

    return (error);
}

#define WASM_IMPORT_KIND_FUNC 0x00
#define WASM_IMPORT_KIND_TABLE 0x01
#define WASM_IMPORT_KIND_MEMORY 0x02
#define WASM_IMPORT_KIND_GLOBAL 0x03
#define WASM_IMPORT_KIND_TAG 0x04

/**
 * In-memory variant of 
 * @param 
 */
int
_rtld_find_import_memory(uint8_t *data_start, uint8_t *data_end, uint32_t off, struct wasm_loader_meminfo *meminfo)
{   
    const char *module_name;
    const char *name;
    struct wa_section_info *sec;
    uint32_t module_namesz, namesz, count, lebsz, secsz;
    uint8_t kind;
    uint8_t *ptr, *ptr_start, *end, *file_start, *sym_start;
    const char *errstr = NULL;

    ptr = data_start;
    end = data_end;
    errstr = NULL;

    if (*(ptr) != WASM_SECTION_IMPORT) {
        printf("%s buffer start is not import section %02x found\n", __func__, *(ptr));
        return ENOEXEC;
    }
    ptr++;

    secsz = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    if (ptr + secsz != data_end) {
        printf("%s section-size missmatch %d vs %ld\n", __func__, secsz, data_end - ptr);
        return ENOEXEC;
    }

    count = decodeULEB128(ptr, &lebsz, end, &errstr);
    ptr += lebsz;

    for (int i = 0; i < count; i++) {
        sym_start = ptr;
        lebsz = 0;
        module_namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        module_name = (const char *)ptr;
        ptr += module_namesz;

        lebsz = 0;
        namesz = decodeULEB128(ptr, &lebsz, end, &errstr);
        ptr += lebsz;
        name = (const char *)ptr;
        ptr += namesz;

        kind = *(ptr);
        ptr++;

        if (kind == WASM_IMPORT_KIND_FUNC) {
            uint32_t typeidx = decodeULEB128(ptr, &lebsz, end, &errstr);
            ptr += lebsz;
        } else if (kind == WASM_IMPORT_KIND_TABLE) {
            uint32_t min;
            uint32_t max;
            uint8_t reftype;
            uint8_t limit;
            reftype = *(ptr);
            ptr++;
            limit = *(ptr);
            ptr++;
            if (limit == 0x01) {
                min = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;

                max = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
            } else if (limit == 0x00) {
                min = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
            }

        } else if (kind == WASM_IMPORT_KIND_MEMORY) {
            uint32_t min;
            uint32_t max;
            uint32_t min_file_offset;
            uint32_t min_lebsz;
            uint32_t max_lebsz;
            bool shared;
            uint8_t limit;
            limit = *(ptr);
            ptr++;
            min_lebsz = 0;
            max_lebsz = 0;
            min_file_offset = (ptr - data_start) + off;
            if (limit == 0x01) {
                min = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                min_lebsz = lebsz;

                max = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                max_lebsz = lebsz;
                shared = false;
            } else if (limit == 0x00) {
                min = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                min_lebsz = lebsz;
                shared = false;
            } else if (limit == 0x02) {
                min = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                min_lebsz = lebsz;
                shared = true;
            } else if (limit == 0x03) {
                min = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                min_lebsz = lebsz;

                max = decodeULEB128(ptr, &lebsz, end, &errstr);
                ptr += lebsz;
                max_lebsz = lebsz;
                shared = true;
            }

            if (meminfo) {
                meminfo->limit = limit;
                meminfo->min = min;
                meminfo->max = max;
                meminfo->min_file_offset = min_file_offset;
                meminfo->min_lebsz = min_lebsz;
                meminfo->max_lebsz = max_lebsz;
                meminfo->shared = shared;
            }

            return (0);
        } else if (kind == WASM_IMPORT_KIND_GLOBAL) {
            uint8_t type;
            uint8_t mutable;

            type = *(ptr);
            ptr++;
            mutable = *(ptr);
            ptr++;
        } else if (kind == WASM_IMPORT_KIND_TAG) {
            uint8_t attr;
            uint32_t typeidx;

            attr = *(ptr);
            ptr++;
            typeidx = decodeULEB128(ptr, &lebsz, end, &errstr);

        }
    }

    return ENOENT;
}