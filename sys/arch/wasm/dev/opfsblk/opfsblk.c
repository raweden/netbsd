/*	$NetBSD: opfsblk.c,v 1.64 2016/07/07 06:55:44 msaitoh Exp $	*/

/*-
 * Copyright (c) 2024 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jesper Svensson
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Block device emulation. This implementation poses as a block device
 * to the kernel and allows disk-images in Orgin Private File System to
 * interacted with as if they where physical disks.
 *
 * The implementation was initially based on rumpblk, and still it borrows the dev_t from such.
 */
#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: opfsblk.c,v 1.64 2016/07/07 06:55:44 msaitoh Exp $");

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/conf.h>
#include <sys/condvar.h>
#include <sys/device.h>
#include <sys/device_impl.h>
#include <sys/disklabel.h>
#include <sys/evcnt.h>
#include <sys/fcntl.h>
#include <sys/kmem.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/cprng.h>
#include <sys/dkio.h>
#include <sys/lwp.h>
#include <sys/module.h>

#include <wasm/wasm_inst.h>
#include <wasm/wasm_module.h>

#include "ioconf.h"


CFDRIVER_DECL(opfsblk, DV_DISK, NULL);

#define OPFSBLK_DEBUG 1

#if defined(OPFSBLK_DEBUG) && OPFSBLK_DEBUG == 1
#define opfsblk_log(...) printf(__VA_ARGS__)
#else
#define opfsblk_log(...)
#endif

#define OPFSBLK_FAILURE 0


//
struct device opfs_rootdev = {
	.dv_unit = 0,
	.dv_xname = "rumpblk0",			// TODO: assign own name to blk dev driver
	.dv_class = DV_DISK,			// other types will not be mountable..
	.dv_cfdriver = &opfsblk_cd,
};

#define OPFSBLK_DRIVER_VERS 2

#define OPFSBLK_DEVMAJOR 197 /* from conf/majors, XXX: not via config yet */


#define OPFSBLK_BIO_READ	0x01
#define OPFSBLK_BIO_WRITE	0x02
#define OPFSBLK_BIO_SYNC	0x04
#define OPFSBLK_BDEV_INIT	0x05
#define OPFSBLK_BDEV_IOCTL	0x06

enum opfsblk_ftype {
	OPFSBLK_FT_DSKIMG = 1, // A regular file that poses as a disk image
};

enum opfsblk_state {
	OPFSBLK_STATE_UNUSED = 0, 	// The block-device private is unused.
	OPFSBLK_STATE_INIT = 1,
	OPFSBLK_STATE_READY = 2,
	OPFSBLK_STATE_KILL = 3,
	OPFSBLK_STATE_FAILURE_SETUP = 4,
	OPFSBLK_STATE_FAILURE = 5,
};

enum opfsblk_req_state {
	BIOREQ_STATE_UNUSED = 0, 	// the request object is not currently in used.
	BIOREQ_STATE_INIT = 1,		// the request object has been taken and might be submitted.
	BIOREQ_STATE_DONE = 3,		// the IO of request object is done.
};

#define OPFSBLK_OPEN_RDONLY	0x0000
#define OPFSBLK_OPEN_WRONLY	0x0001
#define OPFSBLK_OPEN_RDWR		0x0002
#define OPFSBLK_OPEN_ACCMODE	0x0003 /* "yay" */
#define OPFSBLK_OPEN_CREATE	0x0004 /* create file if it doesn't exist */
#define OPFSBLK_OPEN_EXCL		0x0008 /* exclusive open */
#define OPFSBLK_OPEN_BIO		0x0010 /* open device for block i/o */

#ifndef RBLKDEV_REQ_POOLSZ
#define RBLKDEV_REQ_POOLSZ 128
#endif 

#ifndef RBLKDEV_NREQSLOTCNT
#define RBLKDEV_NREQSLOTCNT 32
#endif 

struct opfsblk_bio_req;

#define OPFSBLK_DEVCNT 16
static struct rblkdev {
	int rblk_state;
	char *rblk_path;
	int rblk_fd;				// unused
	int rblk_mode;

	uint64_t rblk_size;			// unused
	uint64_t rblk_hostoffset;	// unused
	uint64_t rblk_hostsize;		// unused
	int rblk_ftype;				// unused
	uint32_t rblk_seq;
	struct opfsblk_bio_req *rblk_requests[RBLKDEV_NREQSLOTCNT];

	struct disklabel rblk_label;
} minors[OPFSBLK_DEVCNT];

static struct evcnt ev_io_total;
static struct evcnt ev_io_async;

static struct evcnt ev_bwrite_total;
static struct evcnt ev_bwrite_async;
static struct evcnt ev_bread_total;

typedef void (*opfsblk_biodone_fn)(void *, size_t, int);
typedef void (*async_fn)(void *);

struct opfsblk_bio_req {
	int bio_op;
	uint32_t bio_ready_state;
	int bio_error;
	int bio_result;
	void *bio_data;
	size_t bio_dlen;
	off_t bio_off;
	struct buf *bio_raw;

	opfsblk_biodone_fn bio_done;
	void *bio_donearg;
	uint32_t bio_reqseq; 	// copy of rblk_seq at the time of posting.
	// async scheduling
	int *bio_wchan;			// wait channel.
	async_fn sched_fn;
	void *sched_args;
};

struct opfsblkd_init_cmd {
	int bio_op;
	uint32_t vers;
	uint32_t bio_ready_state;
	int bio_error;
	int bio_result;
	struct rblkdev *bio_driver;
	const char *filepath;
	const char *arguments;
	// schedular
	uintptr_t aio_lwp;
	uintptr_t aio_wchan;
	uintptr_t aio_taskque;
};

struct opfsblk_bio_req rblkdev_reqpool[RBLKDEV_REQ_POOLSZ];

int __spawn_blkdev_worker(struct rblkdev *data, void *init_cmd) __WASM_IMPORT(kern, spawn_blkdev_worker);

int opfsblk_open(dev_t, int, int, struct lwp *);
int opfsblk_close(dev_t, int, int, struct lwp *);
int opfsblk_read(dev_t, struct uio *, int);
int opfsblk_write(dev_t, struct uio *, int);
int opfsblk_ioctl(dev_t, u_long, void *, int, struct lwp *);
void opfsblk_strategy(struct buf *);
void opfsblk_strategy_fail(struct buf *);
int opfsblk_dump(dev_t, daddr_t, void *, size_t);
int opfsblk_size(dev_t);

static const struct bdevsw opfsblk_bdevsw = {
	.d_open = opfsblk_open,
	.d_close = opfsblk_close,
	.d_strategy = opfsblk_strategy,
	.d_ioctl = opfsblk_ioctl,
	.d_dump = nodump,
	.d_psize = nosize,
	.d_discard = nodiscard,
	.d_flag = D_DISK
};

static const struct cdevsw opfsblk_cdevsw = {
	.d_open = opfsblk_open,
	.d_close = opfsblk_close,
	.d_read = opfsblk_read,
	.d_write = opfsblk_write,
	.d_ioctl = opfsblk_ioctl,
	.d_stop = nostop,
	.d_tty = notty,
	.d_poll = nopoll,
	.d_mmap = nommap,
	.d_kqfilter = nokqfilter,
	.d_discard = nodiscard,
	.d_flag = D_DISK
};

/* fail every n out of BLKFAIL_MAX */
#define BLKFAIL_MAX 10000
static int blkfail;
static unsigned randstate;
static kmutex_t opfsblk_lock;
static int sectshift = DEV_BSHIFT;

static void
makedefaultlabel(struct disklabel *lp, off_t size, int part)
{
	int i;

	memset(lp, 0, sizeof(*lp));

	lp->d_secperunit = size;
	lp->d_secsize = 1 << sectshift;
	lp->d_nsectors = size >> sectshift;
	lp->d_ntracks = 1;
	lp->d_ncylinders = 1;
	lp->d_secpercyl = lp->d_nsectors;

	/* oh dear oh dear */
	strncpy(lp->d_typename, "rumpd", sizeof(lp->d_typename));
	strncpy(lp->d_packname, "fictitious", sizeof(lp->d_packname));

	lp->d_type = DKTYPE_RUMPD;
	lp->d_rpm = 11;
	lp->d_interleave = 1;
	lp->d_flags = 0;

	/* XXX: RAW_PART handling? */
	for (i = 0; i < part; i++) {
		lp->d_partitions[i].p_fstype = FS_UNUSED;
	}
	lp->d_partitions[part].p_size = size >> sectshift;
	lp->d_npartitions = part+1;
	/* XXX: file system type? */

	lp->d_magic = DISKMAGIC;
	lp->d_magic2 = DISKMAGIC;
	lp->d_checksum = 0; /* XXX */
}

int
opfsblk_init(void)
{
	char buf[64];
	devmajor_t opfsblkmaj = OPFSBLK_DEVMAJOR;
	unsigned tmp;
	int i;

	mutex_init(&opfsblk_lock, MUTEX_DEFAULT, IPL_NONE);


	memset(minors, 0, sizeof(minors));
	for (i = 0; i < OPFSBLK_DEVCNT; i++) {
		minors[i].rblk_fd = -1;
	}

	evcnt_attach_dynamic(&ev_io_total, EVCNT_TYPE_MISC, NULL, "opfsblk", "I/O reqs");
	evcnt_attach_dynamic(&ev_io_async, EVCNT_TYPE_MISC, NULL, "opfsblk", "async I/O");

	evcnt_attach_dynamic(&ev_bread_total, EVCNT_TYPE_MISC, NULL, "opfsblk", "bytes read");
	evcnt_attach_dynamic(&ev_bwrite_total, EVCNT_TYPE_MISC, NULL, "opfsblk", "bytes written");
	evcnt_attach_dynamic(&ev_bwrite_async, EVCNT_TYPE_MISC, NULL, "opfsblk", "bytes written async");

	return devsw_attach("rumpblk", &opfsblk_bdevsw, &opfsblkmaj, &opfsblk_cdevsw, &opfsblkmaj);
}

// wasm_scheduler.c
void *__scheduler_taskque(void);

int
opfsblk_register(const char *path, devminor_t *dmin, uint64_t offset, uint64_t size)
{
	struct rblkdev *rblk;
	uint64_t flen;
	size_t len;
	int ftype, error, i, wret;

	mutex_enter(&opfsblk_lock);
	for (i = 0; i < OPFSBLK_DEVCNT; i++) {
		if (minors[i].rblk_path && strcmp(minors[i].rblk_path, path) == 0) {
			mutex_exit(&opfsblk_lock);
			*dmin = i;
			return 0;
		}
	}

	for (i = 0; i < OPFSBLK_DEVCNT; i++)
		if (minors[i].rblk_path == NULL)
			break;
	
	if (i == OPFSBLK_DEVCNT) {
		mutex_exit(&opfsblk_lock);
		return EBUSY;
	}

	opfsblk_log("%s path = %s minor = %d", __func__, path, i);

	rblk = &minors[i];
	rblk->rblk_path = __UNCONST("taken");
	mutex_exit(&opfsblk_lock);

	len = strlen(path);
	rblk->rblk_path = kmem_alloc(len + 1, M_WAITOK);
	strcpy(rblk->rblk_path, path);
	rblk->rblk_hostoffset = offset;
	rblk->rblk_hostsize = flen;
	rblk->rblk_ftype = ftype;
	makedefaultlabel(&rblk->rblk_label, rblk->rblk_size, i);

	struct opfsblkd_init_cmd init_cmd;
	memset(&init_cmd, 0, sizeof(init_cmd));
	init_cmd.bio_op = OPFSBLK_BDEV_INIT;
	init_cmd.vers = OPFSBLK_DRIVER_VERS;
	init_cmd.bio_driver = rblk;
	init_cmd.aio_lwp = (uintptr_t)&lwp0;
	init_cmd.aio_wchan = (uintptr_t)&lwp0.l_md.md_wakesig;
	init_cmd.aio_taskque = (uintptr_t)__scheduler_taskque();
	init_cmd.filepath = rblk->rblk_path;
	
	error = __spawn_blkdev_worker(rblk, &init_cmd);
	wret = __builtin_futex_wait32(&init_cmd.bio_ready_state, 0, -1); // wait two sec (2000000000)
	if (wret == ATOMIC_WAIT_TIMEOUT) {
		opfsblk_log("bio init did timeout");
	}

	*dmin = i;
	return 0;
}

/*
 * Unregister opfsblk.  It's the callers responsibility to make
 * sure it's no longer in use.
 */
int
opfsblk_deregister(const char *path)
{
	struct rblkdev *rblk;
	int i;

	mutex_enter(&opfsblk_lock);
	for (i = 0; i < OPFSBLK_DEVCNT; i++) {
		if (minors[i].rblk_path && strcmp(minors[i].rblk_path, path) == 0) {
			break;
		}
	}
	mutex_exit(&opfsblk_lock);

	if (i == OPFSBLK_DEVCNT)
		return ENOENT;

	opfsblk_log("%s minor = %d", __func__, i);

	rblk = &minors[i];
	
	// TODO: send worker shutdown command

	free(rblk->rblk_path, M_TEMP);
	memset(&rblk->rblk_label, 0, sizeof(rblk->rblk_label));
	rblk->rblk_path = NULL;

	return 0;
}

/*
 * Release all backend resource such as dedicated IO workers.
 * This is called when the kernel is being shut down.
 */
void
opfsblk_fini(void)
{
	int i;

	for (i = 0; i < OPFSBLK_DEVCNT; i++) {
		struct rblkdev *rblk;

		rblk = &minors[i];
		// TODO: send shutdown command
	}
}


int
opfsblk_open(dev_t dev, int flag, int fmt, struct lwp *l)
{
	struct rblkdev *rblk = &minors[minor(dev)];

	opfsblk_log("%s minor = %d", __func__, minor(dev));

	if (rblk->rblk_state == OPFSBLK_STATE_READY) {
		return 0;
	} else if (rblk->rblk_state == OPFSBLK_STATE_FAILURE_SETUP) {
		return ENXIO;
	}

	if (rblk->rblk_fd == -1)
		return ENXIO;

	if (((flag & (FREAD|FWRITE)) & ~rblk->rblk_mode) != 0) {
		return EACCES;
	}

	return 0;
}

int
opfsblk_close(dev_t dev, int flag, int fmt, struct lwp *l)
{

	return 0;
}

int
opfsblk_ioctl(dev_t dev, u_long xfer, void *addr, int flag, struct lwp *l)
{
	opfsblk_log("%s minor = %d", __func__, minor(dev));

	devminor_t dmin = minor(dev);
	struct rblkdev *rblk = &minors[dmin];
	struct partinfo *pi;
	struct partition *dp;
	int error = 0;

	/* well, me should support a few more, but we don't for now */
	switch (xfer) {
	case DIOCGDINFO:
		*(struct disklabel *)addr = rblk->rblk_label;
		break;

	case DIOCGPARTINFO:
		dp = &rblk->rblk_label.d_partitions[DISKPART(dmin)];
		pi = addr;
		pi->pi_offset = dp->p_offset;
		pi->pi_size = dp->p_size;
		pi->pi_secsize = rblk->rblk_label.d_secsize;
		pi->pi_bsize = BLKDEV_IOSIZE;
		pi->pi_fstype = dp->p_fstype;
		pi->pi_fsize = dp->p_fsize;
		pi->pi_frag = dp->p_frag;
		pi->pi_cpg = dp->p_cpg;
		break;

	/* it's synced enough along the write path */
	case DIOCCACHESYNC:
		break;

	case DIOCGMEDIASIZE:
		*(off_t *)addr = (off_t)rblk->rblk_size;
		break;

	default:
		error = ENOTTY;
		break;
	}

	return error;
}

static int
do_physio(dev_t dev, struct uio *uio, int which)
{
	void (*strat)(struct buf *) = opfsblk_strategy;

	return physio(strat, NULL, dev, which, minphys, uio);
}

int
opfsblk_read(dev_t dev, struct uio *uio, int flags)
{

	return do_physio(dev, uio, B_READ);
}

int
opfsblk_write(dev_t dev, struct uio *uio, int flags)
{

	return do_physio(dev, uio, B_WRITE);
}

// 

static void
opfsblk_biodone(void *arg, size_t count, int error)
{
	opfsblk_log("%s arg = %p count = %zu error = %d\n", __func__, arg, count, error);
}


static struct opfsblk_bio_req * __noinline
rblkdev_reqpool_get(void)
{
	struct opfsblk_bio_req *req;

	for (int i = 0; i < RBLKDEV_REQ_POOLSZ; i++) {
		req = &rblkdev_reqpool[i];
		int old = __builtin_atomic_rmw_cmpxchg32(&req->bio_ready_state, BIOREQ_STATE_UNUSED, BIOREQ_STATE_INIT);
		if (old == 0) {
			return req;
		}
	}

	return NULL;
}

static void __noinline
rblkdev_reqpool_put(struct opfsblk_bio_req *req)
{
	struct opfsblk_bio_req *tmp;
	bool found = false;
	if (req == NULL)
		return;

	for (int i = 0; i < RBLKDEV_REQ_POOLSZ; i++) {
		tmp = &rblkdev_reqpool[i];
		if (tmp == req) {
			found = true;
			break;
		}
	}

	if (!found) {
		opfsblk_log("invalid request address used %p", req);
		return;
	}

	uint32_t ret = __builtin_atomic_rmw_cmpxchg32(&req->bio_ready_state, BIOREQ_STATE_DONE, BIOREQ_STATE_UNUSED);
}

// This below might give compiler warnings, but as its have to be wrong in order to be right.
// The concept is quite simple, there is a array of 32 points which is used as a buffer space,
// what we need todo is to write these with atomic operation. Whats below is what i've found working
// even if all the types uses `i32.atomic.rmw.cmpxchg` with the slot feels completly wrong..
static int __noinline
rblkdev_post_req(struct rblkdev *rblk, struct opfsblk_bio_req *req)
{
	uint32_t **slot;
	uint32_t reqseq, gen;
	bool found;
	if (rblk == NULL || req == NULL) {
		return EINVAL;
	}

	// This took way to many trail & error to figure out how two cast point to (uint32_t *)
	slot = &rblk->rblk_requests[0];
	found = false;
	for (int i = 0; i < RBLKDEV_NREQSLOTCNT; i++) {
		int old = __builtin_atomic_rmw_cmpxchg32(slot, 0, (uint32_t)req);
		if (old == 0) {
			found = true;
			break;
		}
		slot++;
	}

	if (!found) {
		opfsblk_log("%s: %d slots is not enough..", __func__, RBLKDEV_NREQSLOTCNT);
		return EBUSY;
	}

	reqseq = __builtin_atomic_rmw_add32(&rblk->rblk_seq, 1);
	__builtin_atomic_store32(&req->bio_reqseq, reqseq + 1);
	__builtin_futex_notify(&rblk->rblk_seq, 1);

	return (0);
}

// this is called on the aio thread.
static void
opfsblk_async_bio_done(void *arg)
{
	struct opfsblk_bio_req *req;
	struct buf *bp;

	req = (struct opfsblk_bio_req *)arg;
	bp = req->bio_raw;

	bp->b_oflags |= BO_DONE;
	bp->b_error = req->bio_error;
	bp->b_cflags &= ~BC_BUSY; // is it our task to clear the BUSY flag?

	// return method object..
	rblkdev_reqpool_put(req);

	opfsblk_log("%s arg = %p count = %zu error = %d\n", __func__, arg, 0, bp->b_error);

	cv_broadcast(&bp->b_busy);
	cv_broadcast(&bp->b_done);
}

void
opfsblk_strategy(struct buf *bp)
{
	//opfsblk_log("%s minor = %d", __func__, minor(bp->b_dev));

	struct rblkdev *rblk;
	off_t off;
	int async;
	int op;

	rblk = &minors[minor(bp->b_dev)];
	async = (bp->b_flags & B_ASYNC) != 0;

#ifdef OPFSBLK_DEBUG
	if (async)
		opfsblk_log("%s bp %p is async\n", __func__, bp);
#endif

	//opfsblk_log("%s async = %d b_blkno = %lld b_bcount = %d data-addr: %p\n", __func__, async, bp->b_blkno, bp->b_bcount, bp->b_data);

#if 0
	if (bp->b_bcount % (1 << sectshift) != 0) {
		opfsblk_biodone(bp, 0, EINVAL);
		return;
	}
#endif

	/* collect statistics */
	ev_io_total.ev_count++;
	if (async)
		ev_io_async.ev_count++;
	if (BUF_ISWRITE(bp)) {
		ev_bwrite_total.ev_count += bp->b_bcount;
		if (async)
			ev_bwrite_async.ev_count += bp->b_bcount;
	} else {
		ev_bread_total.ev_count++;
	}

	struct opfsblk_bio_req *req = rblkdev_reqpool_get();

	if (req == NULL) {
		opfsblk_log("%s: pool did dry out of requst object %d seams to not be enough...", __func__, RBLKDEV_REQ_POOLSZ);
		bp->b_error = ENOMEM;
		return;
	}

	req->bio_op = BUF_ISREAD(bp) ? OPFSBLK_BIO_READ : OPFSBLK_BIO_WRITE;
	req->bio_data = bp->b_data;
	req->bio_dlen = bp->b_bcount;
	req->bio_off = bp->b_blkno;
	req->bio_raw = bp;
	req->bio_done = opfsblk_biodone;
	req->bio_donearg = bp;

	//if (BUF_ISWRITE(bp) && !async)
	//	req->bio_op |= OPFSBLK_BIO_SYNC;
	
	if (async) {
		req->bio_wchan = &lwp0.l_md.md_wakesig;
		req->sched_fn = opfsblk_async_bio_done;
		req->sched_args = req;
	} else {
		req->bio_wchan = 0;
	}

	rblkdev_post_req(rblk, req);
	if (!async) {
		int wret = __builtin_futex_wait32(&req->bio_ready_state, BIOREQ_STATE_INIT, -1);
		bp->b_oflags |= BO_DONE;
		bp->b_error = req->bio_error;
		bp->b_cflags &= ~BC_BUSY; // is it our task to clear the BUSY flag?
		rblkdev_reqpool_put(req);
	} else {
		opfsblk_log("%s waiting async %p\n", __func__, bp);
	}

}