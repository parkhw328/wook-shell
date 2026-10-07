import Foundation
import Darwin
import CSFTP
import WShellCore

struct SftpEntry {
    let name: String, size: UInt64, mode: UInt32, modified: UInt32
    var directory: Bool { mode & 0o170000 == 0o040000 }
    var regular: Bool { mode & 0o170000 == 0o100000 }
}
func remoteJoin(_ path: String, _ name: String) -> String { path + (path.hasSuffix("/") ? "" : "/") + name }
func portableName(_ name: String) -> Bool { name.withCString { wsftp_local_name($0) != 0 } }

private final class SftpListing { var entries: [SftpEntry] = [] }
private final class SftpTransfer {
    let fd: Int32, update: (UInt64, UInt64) -> Void, client: SftpClient
    init(_ fd: Int32, _ client: SftpClient, _ update: @escaping (UInt64, UInt64) -> Void) { self.fd = fd; self.client = client; self.update = update }
}
final class SftpClient {
    private let process = Process(), input = Pipe(), output = Pipe(), lock = NSLock()
    private var cancelled = false, codec: OpaquePointer?
    private let attempt: URL?
    var isCancelled: Bool { lock.lock(); defer { lock.unlock() }; return cancelled }
    init(arguments: [String], environment: [String: String], attempt: URL?) throws {
        self.attempt = attempt
        process.executableURL = URL(fileURLWithPath: "/usr/bin/ssh"); process.arguments = arguments; process.environment = environment
        process.standardInput = input; process.standardOutput = output; process.standardError = FileHandle.nullDevice
        try process.run()
        input.fileHandleForReading.closeFile(); output.fileHandleForWriting.closeFile()
        for fd in [input.fileHandleForWriting.fileDescriptor, output.fileHandleForReading.fileDescriptor] {
            _ = fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); _ = fcntl(fd, F_SETNOSIGPIPE, 1)
        }
        codec = wsftp_create(Unmanaged.passUnretained(self).toOpaque(), { context, bytes, count in
            Unmanaged<SftpClient>.fromOpaque(context!).takeUnretainedValue().read(bytes!, count)
        }, { context, bytes, count in
            Unmanaged<SftpClient>.fromOpaque(context!).takeUnretainedValue().write(bytes!, count)
        })
        if codec == nil { cancel(); throw WShellError("Cannot allocate the SFTP connection.") }
    }
    deinit { cancel(); wsftp_free(codec); if let attempt { try? FileManager.default.removeItem(at: attempt) } }
    func cancel() {
        lock.lock(); cancelled = true
        if process.isRunning { kill(process.processIdentifier, SIGKILL) }
        lock.unlock()
    }
    private func wait(_ fd: Int32, _ event: Int16) -> Bool {
        let deadline = Date().addingTimeInterval(120)
        while !isCancelled && Date() < deadline {
            var p = pollfd(fd: fd, events: event, revents: 0)
            let n = poll(&p, 1, 100)
            if n > 0 { return p.revents & event != 0 }
            if n < 0 && errno != EINTR { return false }
        }
        cancel(); return false
    }
    private func read(_ bytes: UnsafeMutableRawPointer, _ count: Int32) -> Int32 {
        let fd = output.fileHandleForReading.fileDescriptor
        while wait(fd, Int16(POLLIN)) {
            let n = Darwin.read(fd, bytes, Int(count))
            if n >= 0 { return Int32(n) }; if errno != EAGAIN && errno != EINTR { return -1 }
        }
        return -1
    }
    private func write(_ bytes: UnsafeRawPointer, _ count: Int32) -> Int32 {
        let fd = input.fileHandleForWriting.fileDescriptor
        while wait(fd, Int16(POLLOUT)) {
            let n = Darwin.write(fd, bytes, Int(count))
            if n >= 0 { return Int32(n) }; if errno != EAGAIN && errno != EINTR { return -1 }
        }
        return -1
    }
    private func check(_ value: Int32) throws {
        if value == 0 { throw WShellError(String(cString: wsftp_error(codec))) }
    }
    func initialize() throws -> String { try check(wsftp_init(codec)); return try canonical(".") }
    func canonical(_ path: String) throws -> String {
        var buffer = [CChar](repeating: 0, count: 32769)
        try check(wsftp_realpath(codec, path, &buffer, buffer.count))
        guard let result = String(validatingUTF8: buffer) else { throw WShellError("The server returned a non-UTF-8 path.") }
        return result
    }
    func list(_ path: String) throws -> [SftpEntry] {
        let listing = SftpListing()
        try check(wsftp_list(codec, path, { context, name, attrs in
            guard let name, let attrs, let text = String(validatingUTF8: name), !text.unicodeScalars.contains(where: { $0.value < 32 || $0.value == 127 }) else { return 0 }
            let a = attrs.pointee
            Unmanaged<SftpListing>.fromOpaque(context!).takeUnretainedValue().entries.append(SftpEntry(name: text, size: a.size, mode: a.permissions, modified: a.modified))
            return 1
        }, Unmanaged.passUnretained(listing).toOpaque()))
        return listing.entries.sorted { $0.directory != $1.directory ? $0.directory : $0.name.localizedStandardCompare($1.name) == .orderedAscending }
    }
    func attributes(_ path: String) throws -> WsFtpAttrs? {
        var attrs = WsFtpAttrs()
        if wsftp_stat(codec, path, &attrs) != 0 { return attrs }
        if wsftp_status(codec) == 2 { return nil }
        try check(0); return nil
    }
    func mkdir(_ path: String) throws { try check(wsftp_mkdir(codec, path)) }
    func remove(_ path: String, directory: Bool) throws { try check(wsftp_remove(codec, path, directory ? 1 : 0)) }
    func rename(_ from: String, _ to: String) throws {
        guard try attributes(to) == nil else { throw WShellError("The destination already exists. Choose a different name.") }
        try check(wsftp_rename(codec, from, to, 0))
    }
    private static let progress: WsFtpProgress = { context, done, total in
        let t = Unmanaged<SftpTransfer>.fromOpaque(context!).takeUnretainedValue(); t.update(done, total)
        return t.client.isCancelled ? 0 : 1
    }
    func upload(_ local: URL, _ remote: String, replace: Bool, progress: @escaping (UInt64, UInt64) -> Void) throws {
        let fd = open(local.path, O_RDONLY | O_NOFOLLOW); guard fd >= 0 else { throw WShellError("Cannot open the local file.") }; defer { close(fd) }
        var info = stat()
        guard fstat(fd, &info) == 0, info.st_mode & S_IFMT == S_IFREG else { throw WShellError("Select regular files. Links and folders are not transferred.") }
        let target = try attributes(remote)
        guard target == nil || (replace && target!.permissions & 0o170000 == 0o100000) else { throw WShellError("The destination exists or is not a regular file. Refresh and confirm replacement.") }
        guard target == nil || wsftp_atomic_replace(codec) != 0 else { throw WShellError("This server cannot replace files atomically. Choose a different filename.") }
        let parent = String(remote.prefix(upTo: remote.lastIndex(of: "/")!)), temp = remoteJoin(parent, ".wshell-\(UUID().uuidString).part")
        let transfer = SftpTransfer(fd, self, progress)
        try check(wsftp_upload(codec, temp, UInt64(info.st_size), { context, bytes, count in
            let t = Unmanaged<SftpTransfer>.fromOpaque(context!).takeUnretainedValue()
            return Int32(Darwin.read(t.fd, bytes, Int(count)))
        }, Self.progress, Unmanaged.passUnretained(transfer).toOpaque()))
        try check(wsftp_rename(codec, temp, remote, target == nil ? 0 : 1))
    }
    func download(_ remote: String, _ local: URL, replace: Bool, progress: @escaping (UInt64, UInt64) -> Void) throws {
        guard let a = try attributes(remote), a.flags & 1 != 0, a.permissions & 0o170000 == 0o100000 else { throw WShellError("Select a regular remote file. Links and folders are not transferred.") }
        var existing = stat()
        if lstat(local.path, &existing) == 0, !replace || existing.st_mode & S_IFMT != S_IFREG { throw WShellError("The local destination exists or is not a regular file. Refresh and confirm replacement.") }
        let temp = local.deletingLastPathComponent().appendingPathComponent(".wshell-\(UUID().uuidString).part")
        let fd = open(temp.path, O_WRONLY | O_CREAT | O_EXCL, 0o600)
        guard fd >= 0 else { throw WShellError("Cannot create the local transfer file.") }
        defer { close(fd); unlink(temp.path) }
        let transfer = SftpTransfer(fd, self, progress)
        try check(wsftp_download(codec, remote, a.size, { context, bytes, count in
            let t = Unmanaged<SftpTransfer>.fromOpaque(context!).takeUnretainedValue()
            return Int32(Darwin.write(t.fd, bytes, Int(count)))
        }, Self.progress, Unmanaged.passUnretained(transfer).toOpaque()))
        guard fsync(fd) == 0 else { throw WShellError("Cannot save the download. Check free disk space.") }
        // link is exclusive; rename is atomic. Both remain on the destination volume.
        let result = replace ? Darwin.rename(temp.path, local.path) : Darwin.link(temp.path, local.path)
        guard result == 0 else { throw WShellError("Cannot finish the download. The existing destination has been kept.") }
    }
}
