1:14 PMThat's a smart approach. In that case, the cleanest solution is to keep all the Windows code as-is and just define a minimal OVERLAPPED shim in your #else block that mimics the struct and all the functions that use it.
You already have the right idea in your header with defines like #define HANDLE int and #define DWORD std::uint32_t. Just extend that pattern:
cpp#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <poll.h>
    #include <sys/socket.h>
    #include <sys/un.h>
    #include <cstring>

    #define HANDLE int
    #define INVALID_HANDLE_VALUE -1
    #define GENERIC_READ O_RDONLY
    #define GENERIC_WRITE O_WRONLY
    #define DWORD std::uint32_t
    #define INFINITE -1
    #define WAIT_OBJECT_0 0
    #define WAIT_TIMEOUT 1
    #define ERROR_IO_PENDING EAGAIN
    #define TRUE true
    #define FALSE false
    #define OPEN_EXISTING 0
    #define FILE_FLAG_OVERLAPPED 0  // no-op on POSIX

    // OVERLAPPED shim — hEvent is just the fd itself
    struct OVERLAPPED {
        int hEvent = -1;
    };

    inline int CreateEvent(void*, bool, bool, void*) {
        return 0; // dummy, we don't need real events
    }

    inline void CloseHandle(int) {}  // no-op, socket closed separately

    inline void ResetEvent(int) {}   // no-op

    inline bool WriteFile(int fd, const void* buf, DWORD size, DWORD* written, OVERLAPPED*) {
        ssize_t result = write(fd, buf, size);
        if (written) *written = result >= 0 ? result : 0;
        return result == (ssize_t)size;
    }

    inline bool ReadFile(int fd, void* buf, DWORD size, DWORD* bytesRead, OVERLAPPED*) {
        ssize_t result = read(fd, buf, size);
        if (bytesRead) *bytesRead = result >= 0 ? result : 0;
        return result == (ssize_t)size;
    }

    inline DWORD WaitForSingleObject(int, DWORD) {
        return WAIT_OBJECT_0; // blocking read/write already waited
    }

    inline bool GetOverlappedResult(int, OVERLAPPED*, DWORD*, bool) {
        return true; // blocking I/O already completed
    }

    inline DWORD GetLastError() {
        return errno;
    }

    inline void CancelIo(int) {} // no-op

    // replaces CreateFileW for Discord Unix socket
    inline HANDLE CreateFileW(const wchar_t* path, int, int, void*, int, int, void*) {
        // convert wchar path to narrow string
        std::string narrow;
        for (int i = 0; path[i]; i++) narrow += (char)path[i];

        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return INVALID_HANDLE_VALUE;

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, narrow.c_str(), sizeof(addr.sun_path) - 1);

        if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
            close(fd);
            return INVALID_HANDLE_VALUE;
        }
        return fd;
    }

    The key insight is that since ReadFile/WriteFile are blocking on POSIX, WaitForSingleObject and GetOverlappedResult become no-ops that just return success — the waiting already happened in the read/write call itself.
You'll also need to fix connectToDiscord since the pipe path format is different on POSIX, but you can do that with a small #ifdef just in that one function:
cppinline HANDLE connectToDiscord() {
    for (int i = 0; i < 10; i++) {
    #ifdef _WIN32
        std::wstring pipe = L"\\\\?\\pipe\\discord-ipc-" + std::to_wstring(i);
        HANDLE hPipe = CreateFileW(pipe.c_str(), GENERIC_READ|GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    #else
        const char* tmp = getenv("XDG_RUNTIME_DIR") ?: getenv("TMPDIR") ?: "/tmp";
        std::wstring pipe = std::wstring(tmp, tmp + strlen(tmp)) + L"/discord-ipc-" + std::to_wstring(i);
        HANDLE hPipe = CreateFileW(pipe.c_str(), 0, 0, nullptr, 0, 0, nullptr);
    #endif
        if (hPipe != INVALID_HANDLE_VALUE) return hPipe;
    }
    return INVALID_HANDLE_VALUE;
}
This way the rest of your sendFrame, drainFrame, and drainLoop functions stay completely untouched.