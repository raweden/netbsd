/*	$NetBSD: efi.h,v 1.15 2022/08/20 10:55:27 riastradh Exp $	*/

/*-
 * Copyright (c) 2004 Marcel Moolenaar
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _X86_EFI_H_
#define _X86_EFI_H_

#include <sys/uuid.h>

#include <dev/efi/efi.h>

#define	EFI_PAGE_SHIFT		12
#define	EFI_PAGE_SIZE		(1 << EFI_PAGE_SHIFT)
#define	EFI_PAGE_MASK		(EFI_PAGE_SIZE - 1)

#define	EFI_TABLE_ACPI20						\
	{0x8868e871,0xe4f1,0x11d3,0xbc,0x22,{0x00,0x80,0xc7,0x3c,0x88,0x81}}
#define	EFI_TABLE_ACPI10						\
	{0xeb9d2d30,0x2d88,0x11d3,0x9a,0x16,{0x00,0x90,0x27,0x3f,0xc1,0x4d}}
#define	EFI_TABLE_SMBIOS						\
	{0xeb9d2d31,0x2d88,0x11d3,0x9a,0x16,{0x00,0x90,0x27,0x3f,0xc1,0x4d}}
#define	EFI_TABLE_SMBIOS3						\
	{0xf2fd1544,0x9794,0x4a2c,0x99,0x2e,{0xe5,0xbb,0xcf,0x20,0xe3,0x94}}

extern const struct uuid EFI_UUID_ACPI20;
extern const struct uuid EFI_UUID_ACPI10;
extern const struct uuid EFI_UUID_SMBIOS;
extern const struct uuid EFI_UUID_SMBIOS3;

extern bool bootmethod_efi;

#if defined(__amd64__)
typedef uint64_t uintn;
#elif defined(__i386__)
typedef uint32_t uintn;
#elif defined(__WASM)
typedef uint32_t uintn;
#endif

typedef uintn efi_tpl;
typedef void *efi_event;
typedef void (*efi_event_notify)(efi_event, void *);
typedef void *efi_handle;
typedef struct {
	uint8_t type;
	uint8_t subtype;
	uint8_t ldnegth[2];
} efi_device_path;

struct efi_bs {
	struct efi_tblhdr bs_hdr;
#define	EFI_BS_SIG	0x56524553544f4f42UL
	efi_tpl         (*bs_raisetpl)(efi_tpl);
	void            (*bs_restoretpl)(efi_tpl);
	efi_status      (*bs_allocatepages)(uint32_t, uint32_t,
	    uintn, paddr_t *);
	efi_status      (*bs_freepages)(paddr_t, uintn);
	efi_status      (*bs_getmemorymap)(uintn *, struct efi_md *,
	    uintn *, uintn *, uint32_t *);
	efi_status      (*bs_allocatepool)(uint32_t, uintn, void **);
	efi_status      (*bs_freepool)(void *);
	efi_status      (*bs_createevent)(uint32_t, efi_tpl, efi_event_notify,
	    void *, efi_event *);
	efi_status      (*bs_settimer)(efi_event, uint32_t, uint64_t);
	efi_status      (*bs_waitforevent)(uintn, efi_event *, uintn *);
	efi_status      (*bs_signalevent)(efi_event);
	efi_status      (*bs_closeevent)(efi_event);
	efi_status      (*bs_checkevent)(efi_event);
	efi_status      (*bs_installprotocolinterface)(efi_handle *,
	    struct uuid *, uint32_t, void *);
	efi_status      (*bs_reinstallprotocolinterface)(efi_handle *,
	    struct uuid *, void *, void *);
	efi_status      (*bs_uninstallprotocolinterface)(efi_handle *,
	    struct uuid *, void *);
	efi_status      (*bs_handleprotocol)(efi_handle,
	    struct uuid *, void **);
	efi_status      (*bs_pchandleprotocol)(efi_handle,
	    struct uuid *, void **);
	efi_status      (*bs_registerprotocolnotify)(struct uuid *, efi_event,
	    void **);
	efi_status      (*bs_locatehandle)(uint32_t, struct uuid *, void *,
	    uintn *, efi_handle *);
	efi_status      (*bs_locatedevicepath)(struct uuid *, efi_device_path **,
	    efi_handle *);
	efi_status      (*bs_installconfigurationtable)(struct uuid *, void *);
	efi_status      (*bs_loadimage)(uint8_t, efi_handle, efi_device_path *,
	    void *, uintn, efi_handle *);
	efi_status      (*bs_startimage)(efi_handle, uintn *, efi_char **);
	efi_status      (*bs_exit)(efi_handle, efi_status, uintn, efi_char *);
	efi_status      (*bs_unloadimage)(efi_handle);
	efi_status      (*bs_exitbootservices)(efi_handle, uintn);
	efi_status      (*bs_getnextmonotoniccount)(uint64_t *);
	efi_status      (*bs_stall)(uintn);
	efi_status      (*bs_setwatchdogtimer)(uintn, uint64_t,
	    uintn, efi_char *);
	efi_status      (*bs_connectcontroller)(efi_handle, efi_handle *,
	    efi_device_path *, uint8_t);
	efi_status      (*bs_disconnectcontroller)(efi_handle, efi_handle,
	    efi_handle);
	efi_status      (*bs_openprotocol)(efi_handle, struct uuid *, void **,
	    efi_handle, efi_handle, uint32_t);
	efi_status      (*bs_closeprotocol)(efi_handle, struct uuid *,
	    efi_handle, efi_handle);
	efi_status      (*bs_openprotocolinformation)(efi_handle, efi_handle,
	    uint32_t, uint32_t);
	efi_status      (*bs_protocolsperhandle)(efi_handle,
	    struct uuid ***, uintn *);
	efi_status      (*bs_locatehandlebuffer)(uint32_t, struct uuid *,
	    void *, uintn *, efi_handle **);
	efi_status      (*bs_locateprotocol)(struct uuid *, void *, void **);
	efi_status      (*bs_installmultipleprotocolinterfaces)(efi_handle *,
	    ...);
	efi_status      (*bs_uninstallmultipleprotocolinterfaces)(efi_handle,
	    ...);
	efi_status      (*bs_calculatecrc32)(void *, uintn, uint32_t *);
	efi_status      (*bs_copymem)(void *, void *, uintn);
	efi_status      (*bs_setmem)(void *, uintn, uint8_t);
	efi_status      (*bs_createeventex)(uint32_t, efi_tpl,
	    efi_event_notify, void *, struct uuid, efi_event *);
};

struct efi_input;

struct efi_key {
	uint16_t	scancode;
	efi_char	unicodechar;
};

struct efi_input {
	efi_status     (*ei_reset)(struct efi_input *, uint8_t);
	efi_status     (*ei_readkeystroke)(struct efi_input *,
	    struct efi_key *);
	void            *ei_waitforkey;
};

typedef struct efi_input efi_input;

struct efi_text_output_mode {
	int32_t	maxmode;
	int32_t mode;
	int32_t attribute;
	int32_t cursorcolumn;
	int32_t cursorrow;
	uint8_t cursorvisible;
};


struct efi_output;

struct efi_output {
	efi_status     (*ei_reset)(struct efi_output *, uint8_t);
	efi_status     (*ei_outputstring)(struct efi_output *, efi_char *);
	efi_status     (*ei_teststring)(struct efi_output *, efi_char *);
	efi_status     (*ei_textquerymode)(struct efi_output *,
	    uintn, uintn *, uintn *);
	efi_status     (*ei_textsetmode)(struct efi_output *, uintn);
	efi_status     (*ei_setattribute)(struct efi_output *, uintn);
	efi_status     (*ei_clearscreen)(struct efi_output *);
	efi_status     (*ei_setcursorposition)(struct efi_output *,
	    uintn, uintn);
	efi_status     (*ei_enablecursor)(struct efi_output *, uint8_t);
	struct efi_text_output_mode *ei_mode;
};

typedef struct efi_output efi_output;

#if defined(__amd64__)
struct efi_cfgtbl32 {
	struct uuid		ct_uuid;
	uint32_t		ct_data;	/* void * */
};

struct efi_systbl32 {
	struct efi_tblhdr	st_hdr;

	uint32_t		st_fwvendor;
	uint32_t		st_fwrev;
	uint32_t		st_cin;		/* = 0 */
	uint32_t		st_cinif;	/* = 0 */
	uint32_t		st_cout;	/* = 0 */
	uint32_t		st_coutif;	/* = 0 */
	uint32_t		st_cerr;	/* = 0 */
	uint32_t		st_cerrif;	/* = 0 */
	uint32_t		st_rt;		/* struct efi_rt32 * */
	uint32_t		st_bs;		/* = 0 */
	uint32_t		st_entries;
	uint32_t		st_cfgtbl;	/* struct efi_cfgtbl32 * */
};
#elif defined(__i386__)
struct efi_cfgtbl64 {
	struct uuid		ct_uuid;
	uint64_t		ct_data;	/* void * */
};

struct efi_systbl64 {
	struct efi_tblhdr	st_hdr;

	uint64_t		st_fwvendor;
	uint32_t		st_fwrev;
	uint32_t		__pad;
	uint64_t		st_cin;		/* = 0 */
	uint64_t		st_cinif;	/* = 0 */
	uint64_t		st_cout;	/* = 0 */
	uint64_t		st_coutif;	/* = 0 */
	uint64_t		st_cerr;	/* = 0 */
	uint64_t		st_cerrif;	/* = 0 */
	uint64_t		st_rt;		/* struct efi_rt64 * */
	uint64_t		st_bs;		/* = 0 */
	uint64_t		st_entries;
	uint64_t		st_cfgtbl;	/* struct efi_cfgtbl64 * */
};
#endif

void               efi_init(void);
bool               efi_probe(void);
paddr_t            efi_getsystblpa(void);
struct efi_systbl *efi_getsystbl(void);
paddr_t            efi_getcfgtblpa(const struct uuid*);
void              *efi_getcfgtbl(const struct uuid*);
int                efi_getbiosmemtype(uint32_t, uint64_t);
const char        *efi_getmemtype_str(uint32_t);
struct btinfo_memmap;
struct btinfo_memmap *efi_get_e820memmap(void);

/*
void efi_boot_finish(void);
int efi_boot_minimal(uint64_t);
void *efi_get_table(struct uuid *);
void efi_get_time(struct efi_tm *);
struct efi_md *efi_md_first(void);
struct efi_md *efi_md_next(struct efi_md *);
void efi_reset_system(void);
efi_status efi_set_time(struct efi_tm *);
*/

#endif /* _X86_EFI_H_ */
