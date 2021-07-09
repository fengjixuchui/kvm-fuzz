pub usingnamespace @import("linux_std");

// We can't use the std kernel_stat, because it defines uid_t as std.os.linux.uid_t,
// which is not imported in freestanding. Instead, here we directly import uid_t
// from linux_std.
pub const stat = extern struct {
    dev: dev_t,
    ino: ino_t,
    nlink: usize,

    mode: u32,
    uid: uid_t,
    gid: gid_t,
    __pad0: u32 = undefined,
    rdev: dev_t,
    size: off_t,
    blksize: isize,
    blocks: i64,

    atim: timespec,
    mtim: timespec,
    ctim: timespec,
    __unused: [3]isize = undefined,
};
