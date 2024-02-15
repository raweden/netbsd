



#include <sys/file.h>
#include <sys/filedesc.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/termios.h>
#include <sys/poll.h>

#include <wasm/wasm_module.h>
#include <wasm/../mm/mm.h>

int __kcons_write(const char *buf, unsigned int bufsz, unsigned int flags, unsigned int level) __WASM_IMPORT(kern, cons_write);

// A simple proxy file that prints to the console.log or console.error of the hosting enviroment
// this is so that lwp(s) can be setup to print to the native console. This implementation might
// still be useful even if the app has GUI and or wasm netbsd is provided with a tty emulator.

#define WASM_CONS_LVL_KERN_LOG 0
#define WASM_CONS_LVL_KERN_WARN 1
#define WASM_CONS_LVL_KERN_ERR 2
#define WASM_CONS_LVL_USER_LOG 3
#define WASM_CONS_LVL_USER_WARN 4
#define WASM_CONS_LVL_USER_ERR 5

#define WASM_CONS_FLAG_USER_SPACE (1 << 3)

static int wasm_log_file_write(file_t *fp, off_t *offp, struct uio *uio, kauth_cred_t cred, int flags);
static int wasm_log_ioctl(struct file *fp, u_long cmd, void *data);
static int wasm_log_stat(struct file *fp, struct stat *sb);

static struct fileops wasm_log_ops = {
    .fo_name = "t39.console",
    .fo_read = (void *)nullop,
    .fo_write = wasm_log_file_write,
    .fo_ioctl = wasm_log_ioctl,
    .fo_fcntl = fnullop_fcntl,
    .fo_stat = wasm_log_stat,
	.fo_close = (void *)nullop,
	.fo_kqfilter = fnullop_kqfilter,
	.fo_restart = fnullop_restart,
};

void
wasm_logdev_init_curlwp(void)
{
	struct file *fp;
	int fd, error;

	KASSERT(fd_getfile(0) == NULL);
	KASSERT(fd_getfile(1) == NULL);
	KASSERT(fd_getfile(2) == NULL);
	
	/* then, map a file descriptor to the device */
	if ((error = fd_allocfile(&fp, &fd)) != 0)
		panic("cons fd_allocfile failed: %d", error);

	fp->f_flag = FWRITE;
	fp->f_type = DTYPE_MISC;
	fp->f_ops = &wasm_log_ops;
	fp->f_data = NULL;
	fd_affix(curproc, fp, fd);

	KASSERT(fd == 0);
	error += fd_dup2(fp, 1, 0);
	error += fd_dup2(fp, 2, 0);

	if (error)
		panic("failed to dup fd 0/1/2");
}

int
wasm_logdev_init(struct file *fd, int log_level)
{
    fd->f_flag = FWRITE;
	fd->f_type = DTYPE_MISC;
	fd->f_data = (void *)log_level;
	fd->f_ops = &wasm_log_ops;

    return 0;
}

/**
 * When this call is invoked the memory will be in kernel-space
 */
static int
wasm_log_file_write(file_t *fp, off_t *offp, struct uio *uio, kauth_cred_t cred, int flags)
{
    struct iovec *iov;
    char *buf;
    size_t len, n;
    int error = 0;
    int level = (int)(fp->f_undata.fd_data);

    if (uio->uio_iovcnt == 1) {
        iov = uio->uio_iov;
        len = iov->iov_len;
        __kcons_write(uio->uio_iov->iov_base, len, WASM_CONS_FLAG_USER_SPACE, level);
        // our caller expects this to be set.
        iov->iov_base = (char *)iov->iov_base + len;
		iov->iov_len -= len;
		uio->uio_resid -= len;
		uio->uio_offset += len;
        return 0;
    } else {

        buf = kmem_page_alloc(1, 0);
        while (uio->uio_resid > 0) {
            len = uimin(PAGE_SIZE, uio->uio_resid);
            error = uiomove(buf, len, uio);
            if (error)
                break;

            __kcons_write(buf, len, 0, level);
        }
        kmem_page_free(buf, 1);

        return error;
    }
}

static int
wasm_log_ioctl(struct file *fp, u_long cmd, void *data)
{

	if (cmd == TIOCGETA)
		return 0;

	return ENOTTY;
}

static int
wasm_log_stat(struct file *fp, struct stat *sb)
{
	struct timespec ts;

	getnanoboottime(&ts);

	memset(sb, 0, sizeof(*sb));
	sb->st_mode = 0600 | _S_IFCHR;
	sb->st_atimespec = sb->st_mtimespec = sb->st_ctimespec = ts;
	sb->st_birthtimespec = ts;

	return 0;
}

static int
wasm_log_poll(struct file *fp, int events)
{
	int revents = 0;

	if (events & (POLLOUT | POLLWRNORM))
		revents |= events & (POLLOUT | POLLWRNORM);

	return revents;
}
