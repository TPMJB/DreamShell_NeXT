/* Host ABI shim for the production FatFs VFS adapter. Structure declarations
 * from KallistiOS fs.h, Copyright (C) KallistiOS contributors (BSD license). */
#ifndef FAT_TEST_KOS_FS_H
#define FAT_TEST_KOS_FS_H
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
#include <limits.h>
#undef NAME_MAX
#define NAME_MAX 256
#define O_DIR 0x100000
#define O_MODE_MASK O_ACCMODE
#define NMMGR_FLAGS_NEEDSFREE 1
#define NMMGR_TYPE_VFS 1
#define NMMGR_LIST_INIT {0}
typedef uint8_t uint8;
typedef uint32_t uint32;
typedef int64_t _off64_t;
typedef struct { char pathname[256]; int in_kernel; uint32_t version; int flags, type; void *list[2]; } nmmgr_handler_t;
int nmmgr_handler_add(nmmgr_handler_t *h);
int nmmgr_handler_remove(nmmgr_handler_t *h);
typedef struct kos_dirent {
    int size;               /**< \brief Size of the file in bytes. */
    char name[NAME_MAX];    /**< \brief Name of the file. */
    time_t time;            /**< \brief Last access/mod/change time (depends on VFS) */
    uint32_t attr;          /**< \brief Attributes of the file. */
} dirent_t;

/* Forward declaration */
struct vfs_handler;

/* stat_t.unique */
/** \brief stat_t.unique: Constant to use denoting file has no unique ID */
#define STAT_UNIQUE_NONE    0

/* stat_t.type */
/** \brief stat_t.type: Unknown / undefined / not relevant */
#define STAT_TYPE_NONE      0

/** \brief stat_t.type: Standard file */
#define STAT_TYPE_FILE      1

/** \brief stat_t.type: Standard directory */
#define STAT_TYPE_DIR       2

/** \brief stat_t.type: A virtual device of some sort (pipe, socket, etc) */
#define STAT_TYPE_PIPE      3

/** \brief stat_t.type: Meta data */
#define STAT_TYPE_META      4

/** \brief stat_t.type: Symbolic link */
#define STAT_TYPE_SYMLINK   5

/* stat_t.attr */
#define STAT_ATTR_NONE  0x00    /**< \brief stat_t.attr: No attributes */
#define STAT_ATTR_R     0x01    /**< \brief stat_t.attr: Read-capable */
#define STAT_ATTR_W     0x02    /**< \brief stat_t.attr: Write-capable */

/** \brief stat_t.attr: Read/Write capable */
#define STAT_ATTR_RW    (STAT_ATTR_R | STAT_ATTR_W)

/** \brief  File descriptor type */
typedef int file_t;

/** \brief  Invalid file handle constant (for open failure, etc) */
#define FILEHND_INVALID ((file_t)-1)

/** \brief  VFS handler interface.

    All VFS handlers must implement this interface.

    \headerfile kos/fs.h
*/
typedef struct vfs_handler {
    /** \brief Name manager handler header */
    nmmgr_handler_t nmmgr;

    /* Some VFS-specific pieces */
    /** \brief Allow VFS caching; 0=no, 1=yes */
    int cache;
    /** \brief Pointer to private data for the handler */
    void *privdata;

    /** \brief Open a file on the given VFS; return a unique identifier */
    void *(*open)(struct vfs_handler *vfs, const char *fn, int mode);

    /** \brief Close a previously opened file */
    int (*close)(void *hnd);

    /** \brief Read from a previously opened file */
    ssize_t (*read)(void *hnd, void *buffer, size_t cnt);

    /** \brief Write to a previously opened file */
    ssize_t (*write)(void *hnd, const void *buffer, size_t cnt);

    /** \brief Seek in a previously opened file */
    off_t (*seek)(void *hnd, off_t offset, int whence);

    /** \brief Return the current position in a previously opened file */
    off_t (*tell)(void *hnd);

    /** \brief Return the total size of a previously opened file */
    size_t (*total)(void *hnd);

    /** \brief Read the next directory entry in a directory opened with O_DIR */
    const dirent_t *(*readdir)(void *hnd);

    /** \brief Execute a device-specific call on a previously opened file */
    int (*ioctl)(void *hnd, int cmd, va_list ap);

    /** \brief Rename/move a file on the given VFS */
    int (*rename)(struct vfs_handler *vfs, const char *fn1, const char *fn2);

    /** \brief Delete a file from the given VFS */
    int (*unlink)(struct vfs_handler *vfs, const char *fn);

    /** \brief "Memory map" a previously opened file */
    void *(*mmap)(void *fd);

    /** \brief Perform an I/O completion (async I/O) for a previously opened
               file */
    int (*complete)(void *fd, ssize_t *rv);

    /** \brief Get status information on a file on the given VFS
        \note  path will not be passed through realpath() before calling the
               filesystem-level function. It is also important to not call
               realpath() in any implementation of this function as it is
               possible that realpath() will call this function. */
    int (*stat)(struct vfs_handler *vfs, const char *path, struct stat *buf,
                int flag);

    /** \brief Make a directory on the given VFS */
    int (*mkdir)(struct vfs_handler *vfs, const char *fn);

    /** \brief Remove a directory from the given VFS */
    int (*rmdir)(struct vfs_handler *vfs, const char *fn);

    /** \brief Manipulate file control flags on the given file */
    int (*fcntl)(void *fd, int cmd, va_list ap);

    /** \brief Check if an event is pending on the given file */
    short (*poll)(void *fd, short events);

    /** \brief Create a hard link */
    int (*link)(struct vfs_handler *vfs, const char *path1, const char *path2);

    /** \brief Create a symbolic link */
    int (*symlink)(struct vfs_handler *vfs, const char *path1,
                   const char *path2);

    /* 64-bit file access functions. Generally, you should only define one of
       the 64-bit or 32-bit versions of these functions. */

    /** \brief Seek in a previously opened file (64-bit offsets) */
    _off64_t (*seek64)(void *hnd, _off64_t offset, int whence);

    /** \brief Return the current position in an opened file (64-bit offset) */
    _off64_t (*tell64)(void *hnd);

    /** \brief Return the size of an opened file as a 64-bit integer */
    uint64_t (*total64)(void *hnd);

    /** \brief Read the value of a symbolic link
        \note  path will not be passed through realpath() before calling the
               filesystem-level function. It is also important to not call
               realpath() in any implementation of this function as it is
               possible that realpath() will call this function. */
    ssize_t (*readlink)(struct vfs_handler *vfs, const char *path, char *buf,
                        size_t bufsize);

    /** \brief Rewind a directory stream to the start */
    int (*rewinddir)(void *hnd);

    /** \brief Get status information on an already opened file. */
    int (*fstat)(void *hnd, struct stat *st);
} vfs_handler_t;


#endif
