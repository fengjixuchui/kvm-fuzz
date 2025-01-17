usingnamespace @import("common.zig");
const fs = @import("../fs/fs.zig");
const utils = @import("../utils/utils.zig");
const Allocator = std.mem.Allocator;
const FileDescriptorTable = @This();

/// Map from file descriptors to pointers to file descriptions
table: HashMap,

/// FileDescriptorTable reference counter
ref: RefCounter,

/// Linux defines a flag for each file descriptor, called FD_CLOEXEC, which can
/// be set with `open` using O_CLOEXEC, or with `fcntl` using F_SETFD. This
/// bitset indicates if a file descriptor has that flag set or not. Note this
/// flag is associated with the file descriptor itself, not with the file
/// description.
cloexec: std.DynamicBitSet,

const HashMap = std.AutoHashMap(linux.fd_t, *fs.FileDescription);
const RefCounter = utils.RefCounter(FileDescriptorTable);

fn destroy(ref: *RefCounter) void {
    const self = @fieldParentPtr(FileDescriptorTable, "ref", ref);

    // Unref every FileDescription in the table
    var iter = self.table.valueIterator();
    while (iter.next()) |file_ptr| {
        file_ptr.*.ref.unref();
    }

    // Deinit and free the object
    self.table.deinit();
    self.cloexec.deinit();
    self.ref.allocator.destroy(self);
}

pub fn createDefault(allocator: *Allocator, limit_fd: usize) !*FileDescriptorTable {
    // Allocate the file descriptor table and initialize it
    const ret = try allocator.create(FileDescriptorTable);
    errdefer allocator.destroy(ret);
    ret.* = FileDescriptorTable{
        .table = HashMap.init(allocator),
        .ref = RefCounter.init(allocator, destroy),
        .cloexec = try std.DynamicBitSet.initEmpty(limit_fd, allocator),
    };
    errdefer {
        ret.table.deinit();
        ret.cloexec.deinit();
    }

    // Open the standard files
    const stdin = try fs.file_manager.openStdin(allocator);
    errdefer stdin.ref.unref();
    const stdout = try fs.file_manager.openStdout(allocator);
    errdefer stdout.ref.unref();
    const stderr = try fs.file_manager.openStderr(allocator);
    errdefer stderr.ref.unref();

    // Insert the files in the table
    try ret.table.put(linux.STDIN_FILENO, stdin);
    try ret.table.put(linux.STDOUT_FILENO, stdout);
    try ret.table.put(linux.STDERR_FILENO, stderr);

    return ret;
}

pub fn setCloexec(self: *FileDescriptorTable, fd: linux.fd_t) void {
    self.cloexec.set(std.meta.cast(usize, fd));
}

pub fn unsetCloexec(self: *FileDescriptorTable, fd: linux.fd_t) void {
    self.cloexec.unset(std.meta.cast(usize, fd));
}

pub fn setCloexecValue(self: *FileDescriptorTable, fd: linux.fd_t, value: bool) void {
    self.cloexec.setValue(std.meta.cast(usize, fd), value);
}

pub fn isCloexecSet(self: *FileDescriptorTable, fd: linux.fd_t) bool {
    return self.cloexec.isSet(std.meta.cast(usize, fd));
}
