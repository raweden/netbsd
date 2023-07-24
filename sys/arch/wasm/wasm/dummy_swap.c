

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/atomic.h>
#include <sys/buf.h>
#include <sys/bufq.h>
#include <sys/conf.h>

#include <wasm/wasm_module.h>

void __panic_abort(void) __WASM_IMPORT(kern, panic_abort);

// fake swap device

#if 0
struct bdevsw {
	int		(*d_open)(dev_t, int, int, struct lwp *);
	int		(*d_cancel)(dev_t, int, int, struct lwp *);
	int		(*d_close)(dev_t, int, int, struct lwp *);
	void		(*d_strategy)(struct buf *);
	int		(*d_ioctl)(dev_t, u_long, void *, int, struct lwp *);
	int		(*d_dump)(dev_t, daddr_t, void *, size_t);
	int		(*d_psize)(dev_t);
	int		(*d_discard)(dev_t, off_t, off_t);
	int		(*d_devtounit)(dev_t);
	struct cfdriver	*d_cfdriver;
	int		d_flag;
};

/*
 * Character device switch table
 */
struct cdevsw {
	int		(*d_open)(dev_t, int, int, struct lwp *);
	int		(*d_cancel)(dev_t, int, int, struct lwp *);
	int		(*d_close)(dev_t, int, int, struct lwp *);
	int		(*d_read)(dev_t, struct uio *, int);
	int		(*d_write)(dev_t, struct uio *, int);
	int		(*d_ioctl)(dev_t, u_long, void *, int, struct lwp *);
	void		(*d_stop)(struct tty *, int);
	struct tty *	(*d_tty)(dev_t);
	int		(*d_poll)(dev_t, int, struct lwp *);
	paddr_t		(*d_mmap)(dev_t, off_t, int);
	int		(*d_kqfilter)(dev_t, struct knote *);
	int		(*d_discard)(dev_t, off_t, off_t);
	int		(*d_devtounit)(dev_t);
	struct cfdriver	*d_cfdriver;
	int		d_flag;
};
#endif

static int
swopen(dev_t dev, int flag, int mode, struct lwp *l)
{
	static bool inited = false;

	if (!inited) {
		inited = true;
		return 0;
	}
	return ENODEV;
}

static int
swclose_noop(dev_t dev, int flag, int mode, struct lwp *l)
{
    return ENODEV;
}

static void
swstrategy(struct buf *bp)
{
    __panic_abort();
}

/*
 * swread: the read function for the drum (just a call to physio)
 */
/*ARGSUSED*/
static int
swread(dev_t dev, struct uio *uio, int ioflag)
{
    __panic_abort();
    return (0);
}

/*
 * swwrite: the write function for the drum (just a call to physio)
 */
/*ARGSUSED*/
static int
swwrite(dev_t dev, struct uio *uio, int ioflag)
{
    __panic_abort();
    return (0);
}

const struct bdevsw swap_bdevsw = {
	.d_open = swopen,
	.d_close = swclose_noop,
	.d_strategy = swstrategy,
	.d_ioctl = noioctl,
	.d_dump = nodump,
	.d_psize = nosize,
	.d_discard = nodiscard,
	.d_flag = D_OTHER
};

const struct cdevsw swap_cdevsw = {
	.d_open = nullopen,
	.d_close = nullclose,
	.d_read = swread,
	.d_write = swwrite,
	.d_ioctl = noioctl,
	.d_stop = nostop,
	.d_tty = notty,
	.d_poll = nopoll,
	.d_mmap = nommap,
	.d_kqfilter = nokqfilter,
	.d_discard = nodiscard,
	.d_flag = D_OTHER,
};