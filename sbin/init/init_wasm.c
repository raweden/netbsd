/*
 * Copyright (c) 2024 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Raweden @github 2024.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/stdbool.h>
#include <sys/fcntl.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fstab.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

// for WebAssembly /sbin/init also mounts the default file-system based on the /etc/fstab file

static char *nextfld(char **, const char *);


static char *
nextfld(char **str, const char *sep)
{
	char *ret;

	_DIAGASSERT(str != NULL);
	_DIAGASSERT(sep != NULL);

	while ((ret = stresep(str, sep, '\\')) != NULL && *ret == '\0')
		continue;
	return ret;
}


static int __noinline
fstabscan_np(char *data_start, char *data_end, int *readsz, int *lineno, struct fstab *fstab)
{
	char *cp, *lp, *sp, *ln_start, *ln_end;
#define	MAXLINELENGTH	1024
	static char line[MAXLINELENGTH];
	char subline[MAXLINELENGTH];
	static const char sep[] = ":\n";
	static const char ws[] = " \t\n";
	static const char *fstab_type[] = {
	    FSTAB_RW, FSTAB_RQ, FSTAB_RO, FSTAB_SW, FSTAB_DP, FSTAB_XX, NULL 
	};
    int _fs_lineno;

	(void)memset(fstab, 0, sizeof(struct fstab));

    if (data_start == NULL) {
        return -1;
    }

    _fs_lineno = lineno != NULL ? *lineno : 0;
    lp = data_start;
    ln_start = lp;

	for (;;) {
        if (lp == NULL) {
            return -1;
        }
        ln_end = strchr(lp, '\n');
        if (ln_end == NULL) {
            ln_end = data_end;
        } else {
            ln_end = ln_end + 1;
        }
		_fs_lineno++;
/* OLD_STYLE_FSTAB */
		if (!strpbrk(lp, " \t")) {
			fstab->fs_spec = nextfld(&lp, sep);
			if (!fstab->fs_spec || *fstab->fs_spec == '#')
				continue;
			fstab->fs_file = nextfld(&lp, sep);
			fstab->fs_type = nextfld(&lp, sep);
			if (fstab->fs_type) {
				if (!strcmp(fstab->fs_type, FSTAB_XX))
					continue;
				fstab->fs_mntops = fstab->fs_type;
				fstab->fs_vfstype = __UNCONST(strcmp(fstab->fs_type, FSTAB_SW) ? "ufs" : "swap");
				if ((cp = nextfld(&lp, sep)) != NULL) {
					fstab->fs_freq = atoi(cp);
					if ((cp = nextfld(&lp, sep)) != NULL) {
						fstab->fs_passno = atoi(cp);
						goto out;
					}
				}
			}
			goto bad;
		}
/* OLD_STYLE_FSTAB */
		fstab->fs_spec = nextfld(&lp, ws);
		if (!fstab->fs_spec || *fstab->fs_spec == '#')
			continue;
		fstab->fs_file = nextfld(&lp, ws);
		fstab->fs_vfstype = nextfld(&lp, ws);
		fstab->fs_mntops = nextfld(&lp, ws);
		if (fstab->fs_mntops == NULL)
			goto bad;
		fstab->fs_freq = 0;
		fstab->fs_passno = 0;
		if ((cp = nextfld(&lp, ws)) != NULL) {
			fstab->fs_freq = atoi(cp);
			if ((cp = nextfld(&lp, ws)) != NULL)
				fstab->fs_passno = atoi(cp);
		}

		/* subline truncated iff line truncated */
		(void)strlcpy(subline, fstab->fs_mntops, sizeof(subline));
		sp = subline;

		while ((cp = nextfld(&sp, ",")) != NULL) {
			const char **tp;

			if (strlen(cp) != 2)
				continue;

			for (tp = fstab_type; *tp; tp++)
				if (strcmp(cp, *tp) == 0) {
					fstab->fs_type = __UNCONST(*tp);
					break;
				}
			if (*tp)
				break;
		}
		if (fstab->fs_type == NULL)
			goto bad;
		if (strcmp(fstab->fs_type, FSTAB_XX) == 0)
			continue;
		if (cp != NULL)
			goto out;

bad:
		warnx("%lu: Missing fields", (u_long)_fs_lineno);
	}

out:

    if (readsz != NULL)
        *readsz = ln_end - ln_start;

    if (lineno != NULL)
        *lineno = _fs_lineno;

    return 0;
}


struct ufs_args {
    char *fspec;    /* block special file to mount */
};

struct procfs_args {
	int version;
	int flags;
};

struct tmpfs_args {
	int			ta_version;

	/* Size counters. */
	ino_t			ta_nodes_max;
	off_t			ta_size_max;

	/* Root node attributes. */
	uid_t			ta_root_uid;
	gid_t			ta_root_gid;
	mode_t			ta_root_mode;
};

struct ptyfs_args {
	int version;
	gid_t gid;
	mode_t mode;
	int flags;
};

#define CONS_MAJOR 240
#define CTTY_MAJOR 241
#define MEM_MAJOR 242
#define SWAP_MAJOR 243
#define LOG_MAJOR 246
#define FILEDESC_MAJOR 247
#define RND_MAJOR 248
#define KSYM_MAJOR 251
#define OPFSBLK_DEVMAJOR 197
#define DISPLAY_SERV_DEVMAJOR 215

void
finalize_mountroot_fstab(void)
{
	struct stat sbuf;
    struct fstab ent;
	char *data, *ptr, *end;
	int fd, error, fsize, rlen, ln_no;
    int flags = 0;
	int guard = 0;
    void *mnt_data_ptr;
    int mnt_data_sz;
    union {
        struct ufs_args ufs;
        struct procfs_args proc;
        struct tmpfs_args tmpfs;
        struct ptyfs_args ptyfs;
    } data_arg;

	fprintf(stdout, "did enter %s\n", __func__);

	fd = open("/etc/fstab", O_RDONLY);
	if (fd == -1) {
		error = *__errno();
		fprintf(stderr, "could not open /etc/fstab got error %d\n", error);
		return;
	}

	error = fstat(fd, &sbuf);
	if (error == -1) {
		error = *__errno();
		fprintf(stderr, "got error = %d from fstat\n", error);
		return;
	}

	fsize = sbuf.st_size;
	if (fsize == 0) {
		fprintf(stdout, "/etc/fstab is empty nothing to do\n");
		return;
	}

	data = malloc(fsize + 1);
	if (data == NULL) {
		fprintf(stderr, "got ENOMEM for /etc/fstab contents\n");
		return;
	}

	error = read(fd, data, fsize);
	if (error == -1) {
		error = *__errno();
		fprintf(stderr, "got error = %d from read /etc/fstab\n", error);
		goto error_out;
	}

    close(fd);
    fd = -1;

	fprintf(stdout, "content of '/etc/fstab':\n%s", data);
	data[fsize] = '\x00';
    ptr = data;
    end = ptr + fsize;

    rlen = 0;
    ln_no = 0;

    while (true) {
        error = fstabscan_np(ptr, end, &rlen, &ln_no, &ent);
        if (error != 0) {
            fprintf(stdout, "got error = %d from fstabscan_np\n", error);
            break;
        }
        if (rlen == 0) {
            fprintf(stdout, "read length is zero; aborting\n");
            break;
        }
        ptr += rlen;
        fprintf(stdout, "%s mount type = %s dir = %s fs_mntops = %s fs_type = %s fs_vfstype = %s flags = %d\n", __func__, ent.fs_spec, ent.fs_file, ent.fs_mntops, ent.fs_type, ent.fs_vfstype, 0);
        
        flags = 0;

        mnt_data_ptr = NULL;
        mnt_data_sz = 0;

        if (strcmp(ent.fs_vfstype, "ext2") == 0 || strcmp(ent.fs_vfstype, "ffs") == 0) {
            memset(&data_arg, 0, sizeof(data_arg));
            data_arg.ufs.fspec = NULL;
            mnt_data_sz = sizeof(struct ufs_args);
            mnt_data_ptr = &data_arg;
        } else if (strcmp(ent.fs_vfstype, "procfs") == 0) {
            memset(&data_arg, 0, sizeof(data_arg));
            data_arg.proc.version = 1;
            data_arg.proc.flags = 0;
            mnt_data_sz = sizeof(struct procfs_args);
            mnt_data_ptr = &data_arg;
        } else if (strcmp(ent.fs_vfstype, "ptyfs") == 0) {
            memset(&data_arg, 0, sizeof(data_arg));
            data_arg.ptyfs.version = 2;
            data_arg.ptyfs.flags = 0;
            data_arg.ptyfs.gid = 0;
            data_arg.ptyfs.mode = 0777;
            mnt_data_sz = sizeof(struct ptyfs_args);
            mnt_data_ptr = &data_arg;
        } else if (strcmp(ent.fs_vfstype, "tmpfs") == 0) {
            memset(&data_arg, 0, sizeof(data_arg));
            data_arg.tmpfs.ta_version = 1;
            data_arg.tmpfs.ta_nodes_max = 1024;
            data_arg.tmpfs.ta_root_uid = 1000;
			data_arg.tmpfs.ta_root_gid = 1000;
			data_arg.tmpfs.ta_size_max = 4096 * 1024;
            data_arg.tmpfs.ta_root_mode = 0644;
            mnt_data_sz = sizeof(struct tmpfs_args);
            mnt_data_ptr = &data_arg;
        }

        if (strlen(ent.fs_file) == 1 && ent.fs_file[0] == '/') {
            flags |= MNT_UPDATE;
        } else if (strcmp(ent.fs_file, "/dev/pts") == 0) {
			// TODO: enable this when devfs is ready
#if 0
            error = stat("/dev/pts", &sbuf);
            if (error == -1 && *(__errno()) == 2) {
                error = mkdir("/dev/pts", 0777);
                if (error == -1) {
                    error = *__errno();
                    fprintf(stderr, "%s got error = %d when trying to create /dev/pts directory\n", __func__, error);
                } else {
                    fprintf(stdout, "%s created /dev/pts sucessfully\n", __func__);
                }
            }
#endif
        }

        error = mount(ent.fs_vfstype, ent.fs_file, flags, mnt_data_ptr, mnt_data_sz);
        if (error != 0) {
            error = *__errno();
            fprintf(stdout, "got error = %d from mount(%s, %s, %d, NULL, 0)\n", error, ent.fs_vfstype, ent.fs_file, flags);
        }


        if (ptr >= end) {
            fprintf(stdout, "found EOF breaking loop\n");
            break;
        }

        guard++;
		if (guard > 100)
			break;
    }

	struct statvfs sb;

	error = statvfs("/dev", &sb);

	if (error == -1 || strcmp(sb.f_fstypename, "tmpfs") != 0) {
		printf("no tmpfs mounted at '/dev'\n");
		goto error_out;
	}

	mknod("/dev/console", S_IFCHR | 0600, makedev(CONS_MAJOR, 0));
	mknod("/dev/constty", S_IFCHR | 0600, makedev(CONS_MAJOR, 1));
	mknod("/dev/drum", S_IFCHR | 0640, makedev(SWAP_MAJOR, 0));
	mknod("/dev/kmem", S_IFCHR | 0640, makedev(MEM_MAJOR, 1));
	mknod("/dev/mem", S_IFCHR | 0640, makedev(MEM_MAJOR, 0));
	mknod("/dev/null", S_IFCHR | 0666, makedev(MEM_MAJOR, 2));
	mknod("/dev/full", S_IFCHR | 0666, makedev(MEM_MAJOR, 11));
	mknod("/dev/zero", S_IFCHR | 0666, makedev(MEM_MAJOR, 12));
	mknod("/dev/klog", S_IFCHR | 0600, makedev(LOG_MAJOR, 0));
	mknod("/dev/ksyms", S_IFCHR | 0440, makedev(KSYM_MAJOR, 0));
	mknod("/dev/random", S_IFCHR | 0444, makedev(RND_MAJOR, 0));
	mknod("/dev/urandom", S_IFCHR | 0644, makedev(RND_MAJOR, 1));

	mknod("/dev/tty", S_IFCHR | 0666, makedev(CTTY_MAJOR, 0));
	mknod("/dev/stdin", S_IFCHR | 0666, makedev(FILEDESC_MAJOR, 0));
	mknod("/dev/stdout", S_IFCHR | 0666, makedev(FILEDESC_MAJOR, 1));
	mknod("/dev/stderr", S_IFCHR | 0666, makedev(FILEDESC_MAJOR, 2));

	mknod("/dev/opfsblk0", S_IFCHR | S_IFBLK | 0666, makedev(OPFSBLK_DEVMAJOR, 0));
	mknod("/dev/display-server0", S_IFCHR | 0666, makedev(DISPLAY_SERV_DEVMAJOR, 0));

	printf("did setup of '/dev' char devices.\n");
    
    // int mount(const char *type, const char *dir, int flags, void *data, size_t data_len);

error_out:

	if (data != NULL)
		free(data);

    if (fd != -1)
        close(fd);

	return;
}