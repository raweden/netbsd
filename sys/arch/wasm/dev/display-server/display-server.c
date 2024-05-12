

#include <sys/cdefs.h>

#include <sys/param.h>
#include <sys/null.h>
#include <sys/errno.h>
#include <sys/stdint.h>
#include <sys/stdbool.h>
#include <sys/conf.h>
#include <sys/mutex.h>
#include <sys/device.h>
#include <sys/device_impl.h>
#include <sys/event.h>
#include <sys/lwp.h>
#include <sys/proc.h>
#include <sys/poll.h>
#include <sys/selinfo.h>
#include <sys/select.h>


#include <wasm/../mm/mm.h>
#include <wasm/wasm_inst.h>
#include <wasm/wasm_module.h>
#include <wasm/wasm/builtin.h>


// The display-server0 device is pseudo device that acts a a pipe-like
// link for passing messages back and forth to the display-server.
// The backend of the disply-server is implemented in a Worker.
//
// Is there away of having a device file implementation that is above the
// struct cdevsw layer? It would be nice to be able to pass reference to 
// a fd which a client app then renders to in some cases.. 
// [That should be to have a special entry in devfs]

CFDRIVER_DECL(dsport, DV_VIRTUAL, NULL);

struct device dsport_rootdev = {
    .dv_unit = 0,
    .dv_xname = "display-server0",
    .dv_class = DV_VIRTUAL,
    .dv_cfdriver = &dsport_cd,
};

#define DISPLAY_SERV_VERS 1
#define DISPLAY_SERV_DEVMAJOR 215

#define DISPLAY_SERV_NUM_MSGSLOT 64
#define DISPLAY_SERV_MSG_CONNECT 1
#define DISPLAY_SERV_MSG_DISCONNECT 2

struct dsport_instance;

struct dsport_slotmsg {
    uint16_t dm_msgtype;
    uint16_t dm_msgsize;
    uint32_t dm_headgen;
    uint32_t dm_wakesig;
    struct dsport_instance *dm_port;
};

struct dsport_buf {
    kmutex_t b_lock;
    void *b_buffer;
    uint32_t b_gen;
    uint16_t b_size;
    uint16_t b_cnt;
    uint16_t b_off;
};

// holds a instance of a communcation channel
struct dsport_instance {
    struct dsport_instance *li_prev;    // `global_ports.lock` must be held
    struct dsport_instance *li_next;
    kmutex_t lock;
    struct proc *li_owner;
    uint32_t state;
    uint32_t refcount;
    uint32_t ch_gen;
    struct dsport_buf in;
    struct dsport_buf out;
    struct {
        void (*aio_fn)(void *);
        void *aio_arg;
    } async_cmd;
    kcondvar_t ch_cv;
    struct selinfo ch_rsel;
};

// holds the global state.
static struct dsport_private {
    int head_state;
    kmutex_t lock;
    uint32_t dh_gen;
    uintptr_t main_queue[DISPLAY_SERV_NUM_MSGSLOT]; // main_queue (sync open/close fd)
    // client_datas (linked list?)
    struct dsport_instance *li_first;
    struct dsport_instance *li_last;
} global_ports;

static int display_serv_open(dev_t dev, int flag, int fmt, struct lwp *l);
static int display_serv_close(dev_t dev, int flag, int fmt, struct lwp *l);
static int display_serv_read(dev_t dev, struct uio *uio, int flags);
static int display_serv_write(dev_t dev, struct uio *uio, int flags);
static int display_serv_ioctl(dev_t dev, u_long xfer, void *addr, int flag, struct lwp *l);
static int display_serv_poll(dev_t dev, int band, struct lwp *l);
static int display_serv_kqfilter(dev_t dev, struct knote *kn);

static void dschannel_aio_async(void *data);

static const struct cdevsw display_serv_cdevsw = {
	.d_open = display_serv_open,
	.d_close = display_serv_close,
	.d_read = display_serv_read,
	.d_write = display_serv_write,
	.d_ioctl = display_serv_ioctl,
	.d_stop = nostop,
	.d_tty = notty,
	.d_poll = display_serv_poll,
	.d_mmap = nommap,
	.d_kqfilter = display_serv_kqfilter,
	.d_discard = nodiscard,
	.d_flag = D_OTHER
};

#define WASM_HYPER_DISP_PORT_INIT 754

struct dschannel_init_msg {
    struct dsport_private *global;
    uintptr_t aio_lwp;
    uintptr_t aio_wchan;
    uintptr_t aio_taskque;
    void (*aio_fn)(void *);
};

int wasm_exec_ioctl(int cmd, void *arg) __WASM_IMPORT(kern, exec_ioctl);

// wasm_scheduler.c
void *__scheduler_taskque(void);

// simple_lock.c
bool simple_lock_owned(kmutex_t *mtx);
bool simple_trylock(kmutex_t *mtx);
void simple_lock(kmutex_t *mtx);
void simple_unlock(kmutex_t *mtx);

int
display_serv_init(void)
{
    struct dschannel_init_msg init_cmd;
    devmajor_t dsp_bmajor = -1;
	devmajor_t dsp_cmajor = DISPLAY_SERV_DEVMAJOR;
    int error;

	mutex_init(&global_ports.lock, MUTEX_DEFAULT, IPL_NONE);

	error = devsw_attach("display-server", NULL, &dsp_bmajor, &display_serv_cdevsw, &dsp_cmajor);

    if (error != 0) {
        printf("%s got error = %d from devsw_attach\n", __func__, error);
    }

    init_cmd.global = &global_ports;
	init_cmd.aio_lwp = (uintptr_t)&lwp0;
	init_cmd.aio_wchan = (uintptr_t)&lwp0.l_md.md_wakesig;
	init_cmd.aio_taskque = (uintptr_t)__scheduler_taskque();
    init_cmd.aio_fn = NULL;

    error = wasm_exec_ioctl(WASM_HYPER_DISP_PORT_INIT, &init_cmd);

    if (error != 0) {
        printf("%s got error = %d from wasm_exec_ioctl\n", __func__, error);
    }

    __builtin_futex_wait32((uint32_t *)&global_ports.head_state, 0, -1); // TODO: set timeout.

    return error;
}

/**
 * Expects `global_ports.lock` to be held.
 */
static struct dsport_instance *
display_serv_find_open_port(struct proc *p)
{
    struct dsport_instance *port;

    for (port = global_ports.li_first; port != NULL; port = port->li_next) {
        if (port->li_owner == p) {
            return port;
        }
    }

    return NULL;
}

static int
display_serv_post_msgslot_wait(struct dsport_slotmsg *msg)
{
    volatile uint32_t *p;
    uint32_t ret, old;
    bool posted;

    p = (uint32_t *)(&global_ports.main_queue[0]);
    posted = false;

    for (int i = 0; i < DISPLAY_SERV_NUM_MSGSLOT; i++) {
        old = __builtin_atomic_rmw_cmpxchg32(p, 0, (uint32_t)msg);
        if (old == 0) {
            posted = true;
            break;
        }
        p++;
    }

    if (!posted) {
        return EBUSY;
    }

    msg->dm_headgen = __builtin_atomic_rmw_add32(&global_ports.dh_gen, 1);
    __builtin_futex_notify(&global_ports.dh_gen, 1);
    ret = __builtin_futex_wait32(&msg->dm_wakesig, 0, -1);

    return (0);
}

static int
display_serv_open(dev_t dev, int flag, int fmt, struct lwp *l)
{
    struct dsport_instance *port;
    struct proc *p;
    struct dsport_slotmsg msg;
    void *pg_mem;
    uint32_t error;
    p = l->l_proc;

    printf("%s called\n", __func__);

    mutex_enter(&global_ports.lock);

    port = display_serv_find_open_port(p);

    if (port != NULL) {
        mutex_exit(&global_ports.lock);
        return (0);
    }

    port = kmem_zalloc(sizeof(struct dsport_instance), 0);
    if (port == NULL) {
        mutex_exit(&global_ports.lock);
        return ENOMEM;
    }

    pg_mem = kmem_page_alloc(1, 0);
    if (pg_mem == NULL) {
        kmem_free(port, sizeof(struct dsport_instance));
        mutex_exit(&global_ports.lock);
        return ENOMEM;
    }
    
    port->in.b_buffer = pg_mem;
    port->in.b_size = PAGE_SIZE;

    pg_mem = kmem_page_alloc(1, 0);
    if (pg_mem == NULL) {
        kmem_page_free(port->in.b_buffer, 1);
        kmem_free(port, sizeof(struct dsport_instance));
        mutex_exit(&global_ports.lock);
        return ENOMEM;
    }
    
    port->out.b_buffer = pg_mem;
    port->out.b_size = PAGE_SIZE;

    //mutex_init(&port->lock, MUTEX_DEFAULT, IPL_NONE);
    //mutex_init(&port->in.b_lock, MUTEX_SPIN, IPL_NONE);
    //mutex_init(&port->out.b_lock, MUTEX_SPIN, IPL_NONE);

    cv_init(&port->ch_cv, "dschannel");
    selinit(&port->ch_rsel);

    port->li_owner = p;
    if (global_ports.li_last == NULL) {
        global_ports.li_last = port;
        global_ports.li_first = port;
    } else {
        port->li_prev = global_ports.li_last;
        global_ports.li_last->li_next = port;
        global_ports.li_last = port;
    }

    mutex_exit(&global_ports.lock);

    printf("%s port = %p\n", __func__, port);

    // Send a creation Event! (could include something that provides credentails such as execpath)
    port->refcount = 1;
    port->async_cmd.aio_fn = dschannel_aio_async;
    port->async_cmd.aio_arg = port;

    wasm_memory_fill(&msg, 0, sizeof(struct dsport_slotmsg));

    msg.dm_msgtype = DISPLAY_SERV_MSG_CONNECT;
    msg.dm_msgsize = sizeof(struct dsport_slotmsg);
    msg.dm_port = port;

    error = display_serv_post_msgslot_wait(&msg);

    return (0);
}

static int
display_serv_destory_port(struct dsport_instance *port)
{
    // detaching self
    if (port == global_ports.li_first) {
        global_ports.li_first = port->li_next;
    }

    if (port == global_ports.li_last) {
        global_ports.li_last = port->li_prev;
    }

    if (port->li_prev) {
        port->li_prev->li_next = port->li_next;
    }

    if (port->li_next) {
        port->li_next->li_prev = port->li_prev;
    }

    seldestroy(&port->ch_rsel);

    simple_unlock(&port->lock);
}

static int
display_serv_close(dev_t dev, int flag, int fmt, struct lwp *l)
{
    struct dsport_instance *port;
    struct proc *p;
    p = l->l_proc;

    mutex_enter(&global_ports.lock);

    port = display_serv_find_open_port(p);

    if (port == NULL) {
        mutex_exit(&global_ports.lock);
        return (0);
    }

    // TODO: accuire port lock!

    simple_lock(&port->lock);
    port->refcount--;
    if (port->refcount == 0) {
        // detaching self
        display_serv_destory_port(port);
    }
    
    mutex_exit(&global_ports.lock);

    return (0);
}

static int
display_serv_read(dev_t dev, struct uio *uio, int flags)
{
    struct dsport_instance *port;
    struct dsport_buf *buf;
    struct proc *p;
    size_t rlen;
    p = curlwp->l_proc;

    mutex_enter(&global_ports.lock);

    port = display_serv_find_open_port(p);
    
    mutex_exit(&global_ports.lock);

    if (port == NULL)
        return EBADF;

    buf = &port->in;
    simple_lock(&buf->b_lock);

    if (buf->b_cnt > 0) {
        rlen = MIN(uio->uio_resid, buf->b_cnt);
        uiomove(buf->b_buffer + buf->b_off, rlen, uio);
        if (rlen == buf->b_cnt) {
            buf->b_off = 0;
            buf->b_cnt = 0;
        } else {
            buf->b_off += rlen;
            buf->b_cnt -= rlen;
        }
    }

    simple_unlock(&buf->b_lock);

    __builtin_atomic_rmw_add32(&buf->b_gen, 1);
    __builtin_futex_notify(&buf->b_gen, 1);

    return (0);
}

static int
display_serv_write(dev_t dev, struct uio *uio, int flags)
{
    struct dsport_instance *port;
    struct dsport_buf *buf;
    struct proc *p;
    void *dst;
    size_t tail, wlen;
    p = curlwp->l_proc;

    mutex_enter(&global_ports.lock);

    port = display_serv_find_open_port(p);

    mutex_exit(&global_ports.lock);

    if (port == NULL)
        return EBADF;

    
    buf = &port->out;
    simple_lock(&buf->b_lock);

    if (buf->b_cnt != buf->b_size) {
        tail = buf->b_size - (buf->b_off + buf->b_cnt);
        if (buf->b_off != 0 && uio->uio_resid > tail) {
            // relocate buffer data to make more room in tail.
            wasm_memory_copy(buf->b_buffer, buf->b_buffer + buf->b_off, buf->b_cnt);
            tail += buf->b_off;
            buf->b_off = 0;
            dst = buf->b_buffer + buf->b_cnt;
        } else {
            dst = buf->b_buffer + (buf->b_off + buf->b_cnt);
        }
        wlen = MIN(uio->uio_resid, tail);
        uiomove(dst, wlen, uio);
        buf->b_cnt += wlen;
    }

    simple_unlock(&buf->b_lock);

    __builtin_atomic_rmw_add32(&buf->b_gen, 1);
    __builtin_futex_notify(&buf->b_gen, 1);

    return (0);
}

static int
display_serv_ioctl(dev_t dev, u_long xfer, void *addr, int flag, struct lwp *l)
{
    return (0);
}

// for poll/select calls
static int
display_serv_poll(dev_t dev, int events, struct lwp *l)
{
    struct dsport_instance *port;
    struct proc *p;
    int revents;

    revents = 0;
    p = l->l_proc;

    mutex_enter(&global_ports.lock);
    port = display_serv_find_open_port(p);
    mutex_exit(&global_ports.lock);

    if (port == NULL) {
        return revents;
    }

	if (events & (POLLOUT | POLLWRNORM)) {
		revents |= events & (POLLOUT | POLLWRNORM);
    }

	if (events & (POLLIN | POLLRDNORM)) {
		if (port->in.b_cnt > 0) {
			revents |= events & (POLLIN | POLLRDNORM);
        } else {
			selrecord(l, &port->ch_rsel);
        }
	}

	return revents;
}

// 

// this runs in the aio/schedular thread whenever data is available to client.
static void
dschannel_aio_async(void *data)
{
    struct dsport_instance *port = data;

    //printf("%s port = %p\n", __func__, port);

    cv_broadcast(&port->ch_cv);
    selnotify(&port->ch_rsel, POLLIN | POLLRDNORM, NOTE_SUBMIT);
}

static void
filt_dsportrdetach(struct knote *kn)
{
    struct dsport_instance *port = kn->kn_hook;

	simple_lock(&port->lock);
	selremove_knote(&port->ch_rsel, kn);
	simple_unlock(&port->lock);
}

static int
filt_dsportread(struct knote *kn, long hint)
{
	struct dsport_instance *port = kn->kn_hook;
    int rv;

	if (hint == NOTE_SUBMIT) {
		KASSERT(simple_lock_owned(&port->in.b_lock));
    } else {
		simple_lock(&port->in.b_lock);
    }

	kn->kn_data = port->in.b_cnt;
    rv = kn->kn_data > 0;

    //printf("%s port = %p port->in.b_cnt = %d\n", __func__, port, port->in.b_cnt);

	if (hint == NOTE_SUBMIT) {
		KASSERT(simple_lock_owned(&port->in.b_lock));
    } else {
		simple_unlock(&port->in.b_lock);
    }

	return rv;
}

static const struct filterops dsportr_filtops = {
	.f_flags = FILTEROP_ISFD | FILTEROP_MPSAFE,
	.f_attach = NULL,
	.f_detach = filt_dsportrdetach,
	.f_event = filt_dsportread,
};

// for users of kqueue/kevent
static int
display_serv_kqfilter(dev_t dev, struct knote *kn)
{
    struct dsport_instance *port;
    struct proc *p;
    void *dst;

    p = curlwp->l_proc;

    mutex_enter(&global_ports.lock);
    port = display_serv_find_open_port(p);
    mutex_exit(&global_ports.lock);

    if (port == NULL)
        return EINVAL;

    printf("%s port = %p\n", __func__, port);

    /* Validate the event filter.  */
	switch (kn->kn_filter) {
        case EVFILT_READ:
            kn->kn_fop = &dsportr_filtops;
            kn->kn_hook = port;
            simple_lock(&port->lock);
            selrecord_knote(&port->ch_rsel, kn);
            simple_unlock(&port->lock);
            break;
        case EVFILT_WRITE:
            break;
        default:
            return EINVAL;
	}

    return (0);
}