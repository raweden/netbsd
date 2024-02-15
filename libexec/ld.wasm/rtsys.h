
#ifndef __RTLD_SYS_WASM_H_
#define __RTLD_SYS_WASM_H_

#include <sys/stdint.h>

#ifndef NULL
#define NULL ((void *)0)
#endif

// since rtld must be free-standing from any dependency this file declares the barebone struct of the posix file-system
// interface.

typedef uint64_t dev_t;
typedef uint32_t mode_t;
typedef uint64_t ino_t;
typedef uint32_t nlink_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef int64_t time_t;
typedef int64_t off_t;
typedef int64_t blkcnt_t;
typedef int32_t blksize_t;
typedef unsigned long size_t;
typedef long ssize_t;

struct timespec {
	time_t	tv_sec;		/* seconds */
	long	tv_nsec;	/* and nanoseconds */
};

// stdio.h
/* Always ensure that these are consistent with <fcntl.h> and <unistd.h>! */
#ifndef SEEK_SET
#define	SEEK_SET	0	/* set file offset to offset */
#endif
#ifndef SEEK_CUR
#define	SEEK_CUR	1	/* set file offset to current plus offset */
#endif
#ifndef SEEK_END
#define	SEEK_END	2	/* set file offset to EOF plus offset */
#endif

// fcntl
#define O_RDONLY	0x00000000
#define	FREAD		0x00000001
#define	FWRITE		0x00000002
#define	O_NOFOLLOW	0x00000100	/* don't follow symlinks on the last path component */
#define	O_DIRECTORY	0x00200000	/* fail if not a directory */
#define	O_REGULAR	0x02000000	/* fail if not a regular file */

#define	F_GETPATH	15		/* get pathname associated with fd */

#define	PATH_MAX		 2048	/* max bytes in pathname */

struct stat {
	dev_t	  st_dev;		/* inode's device */
	mode_t	  st_mode;		/* inode protection mode */
	ino_t	  st_ino;		/* inode's number */
	nlink_t	  st_nlink;		/* number of hard links */
	uid_t	  st_uid;		/* user ID of the file's owner */
	gid_t	  st_gid;		/* group ID of the file's group */
	dev_t	  st_rdev;		/* device type */
	struct	  timespec st_atim;	/* time of last access */
	struct	  timespec st_mtim;	/* time of last data modification */
	struct	  timespec st_ctim;	/* time of last file status change */
	struct	  timespec st_birthtim;	/* time of creation */
	off_t	  st_size;		/* file size, in bytes */
	blkcnt_t  st_blocks;		/* blocks allocated for file */
	blksize_t st_blksize;		/* optimal blocksize for I/O */
	uint32_t  st_flags;		/* user defined flags for file */
	uint32_t  st_gen;		/* file generation number */
	uint32_t  st_spare[2];
};

#define	S_ISUID	0004000			/* set user id on execution */
#define	S_ISGID	0002000			/* set group id on execution */
#define	S_ISTXT	0001000			/* sticky bit */

#define	S_IRWXU	0000700			/* RWX mask for owner */
#define	S_IRUSR	0000400			/* R for owner */
#define	S_IWUSR	0000200			/* W for owner */
#define	S_IXUSR	0000100			/* X for owner */

#define	S_IREAD		S_IRUSR
#define	S_IWRITE	S_IWUSR
#define	S_IEXEC		S_IXUSR

#define	S_IRWXG	0000070			/* RWX mask for group */
#define	S_IRGRP	0000040			/* R for group */
#define	S_IWGRP	0000020			/* W for group */
#define	S_IXGRP	0000010			/* X for group */

#define	S_IRWXO	0000007			/* RWX mask for other */
#define	S_IROTH	0000004			/* R for other */
#define	S_IWOTH	0000002			/* W for other */
#define	S_IXOTH	0000001			/* X for other */

#define	_S_IFMT	  0170000		/* type of file mask */
#define	_S_IFIFO  0010000		/* named pipe (fifo) */
#define	_S_IFCHR  0020000		/* character special */
#define	_S_IFDIR  0040000		/* directory */
#define	_S_IFBLK  0060000		/* block special */
#define	_S_IFREG  0100000		/* regular */
#define	_S_IFLNK  0120000		/* symbolic link */
#define	_S_ISVTX  0001000		/* save swapped text even after use */
#define	_S_IFSOCK 0140000		/* socket */
#define	_S_IFWHT  0160000		/* whiteout */
#define	_S_ARCH1  0200000		/* Archive state 1, ls -l shows 'a' */
#define	_S_ARCH2  0400000		/* Archive state 2, ls -l shows 'A' */

#define	S_IFMT	 _S_IFMT
#define	S_IFIFO	 _S_IFIFO
#define	S_IFCHR	 _S_IFCHR
#define	S_IFDIR	 _S_IFDIR
#define	S_IFBLK	 _S_IFBLK
#define	S_IFREG	 _S_IFREG
#define	S_IFLNK	 _S_IFLNK
#define	S_ISVTX	 _S_ISVTX
#define	S_IFSOCK _S_IFSOCK
#define	S_IFWHT  _S_IFWHT

#define	S_ISDIR(m)	(((m) & _S_IFMT) == _S_IFDIR)	/* directory */
#define	S_ISCHR(m)	(((m) & _S_IFMT) == _S_IFCHR)	/* char special */
#define	S_ISBLK(m)	(((m) & _S_IFMT) == _S_IFBLK)	/* block special */
#define	S_ISREG(m)	(((m) & _S_IFMT) == _S_IFREG)	/* regular file */
#define	S_ISFIFO(m)	(((m) & _S_IFMT) == _S_IFIFO)	/* fifo */
#define	S_ISLNK(m)	(((m) & _S_IFMT) == _S_IFLNK)	/* symbolic link */
#define	S_ISSOCK(m)	(((m) & _S_IFMT) == _S_IFSOCK)	/* socket */
#define	S_ISWHT(m)	(((m) & _S_IFMT) == _S_IFWHT)	/* whiteout */

#define	ACCESSPERMS	(S_IRWXU|S_IRWXG|S_IRWXO)	/* 0777 */
							/* 7777 */
#define	ALLPERMS	(S_ISUID|S_ISGID|S_ISTXT|S_IRWXU|S_IRWXG|S_IRWXO)
							/* 0666 */
#define	DEFFILEMODE	(S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP|S_IROTH|S_IWOTH)

// from dirent.h

struct dirent {
	ino_t 	 d_fileno;			/* file number of entry */
	uint16_t d_reclen;			/* length of this record */
	uint16_t d_namlen;			/* length of string in d_name */
	uint8_t  d_type; 			/* file type, see below */
	char	 d_name[511 + 1];	/* name must be no longer than this */
};

int __sys_open(const char *filepath, int flags, long arg);
int __sys_close(int fd);
off_t __sys_lseek(int fd, off_t offset, int whence);
int __sys_getdents(int fd, char *buf, unsigned long count);
long __sys_read(int fd, void *buf, unsigned long nbyte);
long __sys_write(int fd, const void *buf, unsigned long nbyte);
int __sys_fstat(int fd, struct stat *sb);
int __sys_lstat(const char *path, struct stat *ub);
ssize_t __sys_readlink(const char *path, char *buf, size_t count);
int __sys_fcntl(int fd, int cmd, long arg);


#endif /* __RTLD_SYS_WASM_H_ */