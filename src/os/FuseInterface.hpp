
#include <errno.h>
#include <cstdint>
#include <sys/types.h>

namespace supercloud {

//TODO: remove define, this is plate-form specific.
#ifndef mode_t
#define mode_t uint32_t
#endif
#ifndef fuse_stat_t
#define fuse_stat_t stat
#define nlink_t int
#define uid_t uint32_t
#define gid_t int
#define blksize_t int
#define blkcnt_t int
    struct Stat {
        dev_t     st_dev;      /* ID du périphérique contenant le fichier */
        ino_t     st_ino;      /* Numéro inœud */
        mode_t    st_mode;     /* Protection */
        nlink_t   st_nlink;    /* Nb liens matériels */
        uid_t     st_uid;      /* UID propriétaire */
        gid_t     st_gid;      /* GID propriétaire */
        dev_t     st_rdev;     /* ID périphérique (si fichier spécial) */
        off_t     st_size;     /* Taille totale en octets */
        blksize_t st_blksize;  /* Taille de bloc pour E/S */
        blkcnt_t  st_blocks;   /* Nombre de blocs alloués */
        time_t    st_atime;    /* Heure dernier accès */
        time_t    st_mtime;    /* Heure dernière modification */
        time_t    st_ctime;    /* Heure dernier changement état */
    };
    struct FuseFileInfo {
        int 	flags;
        unsigned int 	writepage = 1;
        unsigned int 	direct_io = 1;
        unsigned int 	keep_cache = 1;
        unsigned int 	flush = 1;
        unsigned int 	nonseekable = 1;
        unsigned int 	cache_readdir = 1;
        unsigned int 	noflush = 1;
        unsigned int 	padding = 24;
        uint64_t 	fh;
        uint64_t 	lock_owner;
        uint32_t 	poll_events;
    };
#define fsblkcnt_t uint32_t
#define fsfilcnt_t uint32_t
    struct Statvfs {
        unsigned long f_bsize;   // File system block size.
        unsigned long f_frsize;  // Fundamental file system block size.
        fsblkcnt_t    f_blocks;   // Total number of blocks on file system in units of f_frsize.
        fsblkcnt_t    f_bfree;    // Total number of free blocks.
        fsblkcnt_t    f_bavail;   // Number of free blocks available to non - privileged process.
        fsfilcnt_t    f_files;    // Total number of file serial numbers.
        fsfilcnt_t    f_ffree;    // Total number of free file serial numbers.
        fsfilcnt_t    f_favail;   // Number of file serial numbers available to non - privileged process.
        unsigned long f_fsid;     // File system ID.
        unsigned long f_flag;     // Bit mask of f_flag values.
        unsigned long f_namemax;  // Maximum filename length.
    };
    enum fuse_fill_dir_flags {
        FUSE_FILL_DIR_PLUS = (1 << 1)
    };
    typedef int (*fuse_fill_dir_t) (void* buf, const char* name,
        const struct Stat* stbuf, off_t off,
        enum fuse_fill_dir_flags flags);
#endif

    //explaination of codes: https://kdave.github.io/errno.h/
    namespace ErrorCodes {
        constexpr int32_t STATUS_NOT_IMPLEMENTED = 0xc0000002;

        ////constexpr uint16_t E2BIG = Errno::E2BIG;
        ////constexpr uint16_t EACCES = Errno::EACCES;
        ////constexpr uint16_t EADDRINUSE = Errno::EADDRINUSE;
        ////constexpr uint16_t EADDRNOTAVAIL = Errno::EADDRNOTAVAIL;
        constexpr uint16_t EADV = 68;
        ////constexpr uint16_t EAFNOSUPPORT = Errno::EAFNOSUPPORT;
        ////constexpr uint16_t EAGAIN = Errno::EAGAIN;
        ////constexpr uint16_t EALREADY = Errno::EALREADY;
        constexpr uint16_t EBADE = 52;
        //constexpr uint16_t EBADF = Errno::EBADF;
        constexpr uint16_t EBADFD = 77;
        //constexpr uint16_t EBADMSG = Errno::EBADMSG;
        constexpr uint16_t EBADR = 53;
        constexpr uint16_t EBADRQC = 56;
        constexpr uint16_t EBADSLT = 57;
        constexpr uint16_t EBFONT = 59;
        //constexpr uint16_t EBUSY = Errno::EBUSY;
        //constexpr uint16_t ECANCELED = 125; //    #define ECANCELED       105 in erno ...
        //constexpr uint16_t ECHILD = Errno::ECHILD;
        constexpr uint16_t ECHRNG = 44;
        constexpr uint16_t ECOMM = 70;
        //constexpr uint16_t ECONNABORTED = Errno::ECONNABORTED;
        //constexpr uint16_t ECONNREFUSED = Errno::ECONNREFUSED;
        //constexpr uint16_t ECONNRESET = Errno::ECONNRESET;
        //constexpr uint16_t EDEADLK = Errno::EDEADLK;
        //constexpr uint16_t EDEADLOCK = return EDEADLK();
        //constexpr uint16_t EDESTADDRREQ = Errno::EDESTADDRREQ;
        //constexpr uint16_t EDOM = Errno::EDOM;
        constexpr uint16_t EDOTDOT = 73;
        //constexpr uint16_t EDQUOT = Errno::EDQUOT;
        //constexpr uint16_t EEXIST = Errno::EEXIST;
        //constexpr uint16_t EFAULT = Errno::EFAULT;
        //constexpr uint16_t EFBIG = Errno::EFBIG;
        //constexpr uint16_t EHOSTDOWN = Errno::EHOSTDOWN;
        //constexpr uint16_t EHOSTUNREACH = Errno::EHOSTUNREACH;
        //constexpr uint16_t EIDRM = Errno::EIDRM;
        //constexpr uint16_t EILSEQ = Errno::EILSEQ;
        //constexpr uint16_t EINPROGRESS = Errno::EINPROGRESS;
        //constexpr uint16_t EINTR = Errno::EINTR;
        //constexpr uint16_t EINVAL = Errno::EINVAL;
        //constexpr uint16_t EIO = Errno::EIO;
        //constexpr uint16_t EISCONN = Errno::EISCONN;
        //constexpr uint16_t EISDIR = Errno::EISDIR;
        constexpr uint16_t EISNAM = 120;
        constexpr uint16_t EKEYEXPIRED = 127;
        constexpr uint16_t EKEYREJECTED = 129;
        constexpr uint16_t EKEYREVOKED = 128;
        constexpr uint16_t EL2HLT = 51;
        constexpr uint16_t EL2NSYNC = 45;
        constexpr uint16_t EL3HLT = 46;
        constexpr uint16_t EL3RST = 47;
        constexpr uint16_t ELIBACC = 79;
        constexpr uint16_t ELIBBAD = 80;
        constexpr uint16_t ELIBEXEC = 83;
        constexpr uint16_t ELIBMAX = 82;
        constexpr uint16_t ELIBSCN = 81;
        constexpr uint16_t ELNRNG = 48;
        //constexpr uint16_t ELOOP = Errno::ELOOP;
        constexpr uint16_t EMEDIUMTYPE = 124;
        //constexpr uint16_t EMFILE = Errno::EMFILE;
        //constexpr uint16_t EMLINK = Errno::EMLINK;
        //constexpr uint16_t EMSGSIZE = Errno::EMSGSIZE;
        //constexpr uint16_t EMULTIHOP = Errno::EMULTIHOP;
        //constexpr uint16_t ENAMETOOLONG = Errno::ENAMETOOLONG;
        constexpr uint16_t ENAVAIL = 119;
        //constexpr uint16_t ENETDOWN = Errno::ENETDOWN;
        //constexpr uint16_t ENETRESET = Errno::ENETRESET;
        //constexpr uint16_t ENETUNREACH = Errno::ENETUNREACH;
        //constexpr uint16_t ENFILE = Errno::ENFILE;
        constexpr uint16_t ENOANO = 55;
        //constexpr uint16_t ENOBUFS = Errno::ENOBUFS;
        constexpr uint16_t ENOCSI = 50;
        //constexpr uint16_t ENODATA = Errno::ENODATA;
        //constexpr uint16_t ENODEV = Errno::ENODEV;
        //constexpr uint16_t ENOENT = Errno::ENOENT;
        //constexpr uint16_t ENOEXEC = Errno::ENOEXEC;
        constexpr uint16_t ENOKEY = 126;
        //constexpr uint16_t ENOLCK = Errno::ENOLCK;
        //constexpr uint16_t ENOLINK = Errno::ENOLINK;
        constexpr uint16_t ENOMEDIUM = 123;
        //constexpr uint16_t ENOMEM = Errno::ENOMEM;
        //constexpr uint16_t ENOMSG = Errno::ENOMSG;
        constexpr uint16_t ENONET = 64;
        constexpr uint16_t ENOPKG = 65;
        //constexpr uint16_t ENOPROTOOPT = Errno::ENOPROTOOPT;
        //constexpr uint16_t ENOSPC = Errno::ENOSPC;
        //constexpr uint16_t ENOSR = Errno::ENOSR;
        //constexpr uint16_t ENOSTR = Errno::ENOSTR;
        //constexpr uint16_t ENOSYS = Errno::ENOSYS;
        //constexpr uint16_t ENOTBLK = Errno::ENOTBLK;
        //constexpr uint16_t ENOTCONN = Errno::ENOTCONN;
        //constexpr uint16_t ENOTDIR = Errno::ENOTDIR;
        //constexpr uint16_t ENOTEMPTY = Errno::ENOTEMPTY;
        constexpr uint16_t ENOTNAM = 118;
        //constexpr uint16_t ENOTRECOVERABLE = 131; // #define ENOTRECOVERABLE 127 in errno
        //constexpr uint16_t ENOTSOCK = Errno::ENOTSOCK;
        //constexpr uint16_t ENOTTY = Errno::ENOTTY;
        constexpr uint16_t ENOTUNIQ = 76;
        //constexpr uint16_t ENXIO = Errno::ENXIO;
        //constexpr uint16_t EOPNOTSUPP = Errno::EOPNOTSUPP;
        //constexpr uint16_t EOVERFLOW = Errno::EOVERFLOW;
        //constexpr uint16_t EOWNERDEAD = 130; //#define EOWNERDEAD      133 in erno
        //constexpr uint16_t EPERM = Errno::EPERM;
        //constexpr uint16_t EPFNOSUPPORT = Errno::EPFNOSUPPORT;
        //constexpr uint16_t EPIPE = Errno::EPIPE;
        //constexpr uint16_t EPROTO = Errno::EPROTO;
        //constexpr uint16_t EPROTONOSUPPORT = Errno::EPROTONOSUPPORT;
        //constexpr uint16_t EPROTOTYPE = Errno::EPROTOTYPE;
        //constexpr uint16_t ERANGE = Errno::ERANGE;
        constexpr uint16_t EREMCHG = 78;
        //constexpr uint16_t EREMOTE = Errno::EREMOTE;
        constexpr uint16_t EREMOTEIO = 121;
        constexpr uint16_t ERESTART = 85;
        //constexpr uint16_t EROFS = Errno::EROFS;
        //constexpr uint16_t ESHUTDOWN = Errno::ESHUTDOWN;
        //constexpr uint16_t ESOCKTNOSUPPORT = Errno::ESOCKTNOSUPPORT;
        //constexpr uint16_t ESPIPE = Errno::ESPIPE;
        //constexpr uint16_t ESRCH = Errno::ESRCH;
        constexpr uint16_t ESRMNT = 69;
        //constexpr uint16_t ESTALE = Errno::ESTALE;
        constexpr uint16_t ESTRPIPE = 86;
        //constexpr uint16_t ETIME = Errno::ETIME;
        //constexpr uint16_t ETIMEDOUT = Errno::ETIMEDOUT;
        //constexpr uint16_t ETOOMANYREFS = Errno::ETOOMANYREFS;
        //constexpr uint16_t ETXTBSY = Errno::ETXTBSY;
        constexpr uint16_t EUCLEAN = 117;
        constexpr uint16_t EUNATCH = 49;
        //constexpr uint16_t EUSERS = Errno::EUSERS;
        //constexpr uint16_t EWOULDBLOCK = Errno::EWOULDBLOCK;
        //constexpr uint16_t EXDEV = Errno::EXDEV;
        constexpr uint16_t EXFULL = 54;
        constexpr uint16_t ENOATTR = 93;
        //constexpr uint16_t ENOTSUP = 45; // #define ENOTSUP         129 in erno
    }



    typedef uint32_t FileStat;
    namespace FileStat {
        constexpr uint64_t STAT_IFIFO = 0010000;  // named pipe (fifo)
        constexpr uint64_t STAT_IFCHR = 0020000;  // character special
        constexpr uint64_t STAT_IFDIR = 0040000;  // directory
        constexpr uint64_t STAT_IFBLK = 0060000;  // block special
        constexpr uint64_t STAT_IFREG = 0100000;  // regular
        constexpr uint64_t STAT_IFLNK = 0120000;  // symbolic link
        constexpr uint64_t STAT_IFSOCK = 0140000; // socket
        constexpr uint64_t STAT_IFMT = 0170000;   // file mask for type checks
        constexpr uint64_t STAT_ISUID = 0004000;  // set user id on execution
        constexpr uint64_t STAT_ISGID = 0002000;  // set group id on execution
        constexpr uint64_t STAT_ISVTX = 0001000;  // save swapped text even after use
        constexpr uint64_t STAT_IRUSR = 0000400;  // read permission, owner
        constexpr uint64_t STAT_IWUSR = 0000200;  // write permission, owner
        constexpr uint64_t STAT_IXUSR = 0000100;  // execute/search permission, owner
        constexpr uint64_t STAT_IRGRP = 0000040;  // read permission, group
        constexpr uint64_t STAT_IWGRP = 0000020;  // write permission, group
        constexpr uint64_t STAT_IXGRP = 0000010;  // execute/search permission, group
        constexpr uint64_t STAT_IROTH = 0000004;  // read permission, other
        constexpr uint64_t STAT_IWOTH = 0000002;  // write permission, other
        constexpr uint64_t STAT_IXOTH = 0000001;  // execute permission, other

        constexpr uint64_t ALL_READ = S_IRUSR | S_IRGRP | S_IROTH;
        constexpr uint64_t ALL_WRITE = S_IWUSR | S_IWGRP | S_IWOTH;
        constexpr uint64_t STAT_IXUGO = S_IXUSR | S_IXGRP | S_IXOTH;

    }

}
