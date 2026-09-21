#include "NodeProcess.h"

#ifndef _WIN32
#error NodeProcess currently requires Windows Job Objects.
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>

// Own this object for the application's lifetime. Closing the job also kills
// Node if CLion forcibly stops the application (without running destructors).
class NodeProcess::Impl {
public:
    Impl(const std::wstring& executable,
                const std::vector<std::wstring>& arguments,
                const std::wstring& workingDirectory, bool protocol) {
        job_ = CreateJobObjectW(nullptr, nullptr); // Non-inheritable, parent only.
        if (!job_) fail("CreateJobObject");
        try {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation,
                                         &limits, sizeof(limits))) fail("SetInformationJobObject");

            std::wstring command = quote(executable);
            for (const auto& argument : arguments) command += L" " + quote(argument);
            // CLion's own console handles need not have HANDLE_FLAG_INHERIT.
            // Give Node inheritable duplicates without modifying the originals.
            InheritedHandle input(STD_INPUT_HANDLE);
            InheritedHandle output(STD_OUTPUT_HANDLE);
            InheritedHandle error(STD_ERROR_HANDLE);
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdInput = input.value;
            startup.hStdOutput = output.value;
            startup.hStdError = error.value;
            if (protocol) {
                SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
                if (!CreatePipe(&childInput_, &input_, &security, 65536) ||
                    !CreatePipe(&output_, &childOutput_, &security, 65536)) fail("Create protocol pipes");
                if (!SetHandleInformation(input_, HANDLE_FLAG_INHERIT, 0) ||
                    !SetHandleInformation(output_, HANDLE_FLAG_INHERIT, 0)) fail("Protocol inheritance");
                startup.hStdInput = childInput_; startup.hStdOutput = childOutput_;
            }
            PROCESS_INFORMATION child{};
            // Suspend until ownership is established; Node cannot run unowned.
            if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
                                TRUE, CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr,
                                workingDirectory.c_str(), &startup, &child)) fail("CreateProcess");
            process_ = child.hProcess;
            if (childInput_) { CloseHandle(childInput_); childInput_ = nullptr; }
            if (childOutput_) { CloseHandle(childOutput_); childOutput_ = nullptr; }
            const bool assigned = AssignProcessToJobObject(job_, process_) != FALSE;
            const DWORD assignError = GetLastError();
            const DWORD resumed = assigned ? ResumeThread(child.hThread) : DWORD(-1);
            const DWORD resumeError = GetLastError();
            CloseHandle(child.hThread);
            if (!assigned || resumed == DWORD(-1)) {
                TerminateProcess(process_, 1);
                WaitForSingleObject(process_, INFINITE);
                throw std::runtime_error("Node startup failed (Windows error " +
                    std::to_string(assigned ? resumeError : assignError) + ").");
            }
        } catch (...) {
            close();
            throw;
        }
    }

    ~Impl() { close(); }


    bool sendLine(const std::string &line) {
        if (!input_ || outstanding_ || line.size() > 8192 || line.find_first_of("\r\n") != std::string::npos) return false;
        if (poll()) return false;
        // One request, smaller than the pipe buffer. No second write until a response.
        const auto wire = line + "\n"; DWORD written = 0;
        if (!WriteFile(input_, wire.data(), static_cast<DWORD>(wire.size()), &written, nullptr) || written != wire.size())
            throw std::runtime_error("Bridge input closed.");
        outstanding_ = true; return true;
    }
    std::optional<std::string> readLine() {
        if (!output_) return {};
        DWORD available = 0;
        if (!PeekNamedPipe(output_, nullptr, 0, nullptr, &available, nullptr)) throw std::runtime_error("Bridge output closed.");
        if (available) {
            char bytes[16384]; DWORD received = 0;
            if (!ReadFile(output_, bytes, std::min<DWORD>(available, sizeof(bytes)), &received, nullptr))
                throw std::runtime_error("Bridge read failed.");
            buffered_.append(bytes, received);
        }
        if (buffered_.size() > 16384) throw std::runtime_error("Bridge response exceeds limit.");
        const auto end = buffered_.find('\n');
        if (end == std::string::npos) return {};
        auto result = buffered_.substr(0, end); buffered_.erase(0, end+1); outstanding_ = false; return result;
    }
    DWORD id() const { return GetProcessId(process_); }

    std::optional<int> poll() const {
        const DWORD status = WaitForSingleObject(process_, 0);
        if (status == WAIT_TIMEOUT) return std::nullopt;
        if (status != WAIT_OBJECT_0) fail("Poll Node process");
        return wait();
    }

    int wait() const {
        if (WaitForSingleObject(process_, INFINITE) != WAIT_OBJECT_0) fail("WaitForSingleObject");
        DWORD code = 1;
        if (!GetExitCodeProcess(process_, &code)) fail("GetExitCodeProcess");
        return static_cast<int>(code);
    }

private:
    struct InheritedHandle {
        HANDLE value = nullptr;

        explicit InheritedHandle(DWORD stream) {
            const HANDLE original = GetStdHandle(stream);
            if (!original || original == INVALID_HANDLE_VALUE) {
                // A GUI host can omit stdin; this inquiry does not read input.
                if (stream == STD_INPUT_HANDLE) {
                    SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
                    value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                        &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (value == INVALID_HANDLE_VALUE) fail("Open Node stdin");
                    return;
                }
                throw std::runtime_error("Node requires a valid application output handle.");
            }
            if (!DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(),
                                 &value, 0, TRUE, DUPLICATE_SAME_ACCESS)) {
                fail("Duplicate Node console handle");
            }
        }

        ~InheritedHandle() { if (value) CloseHandle(value); }
        InheritedHandle(const InheritedHandle&) = delete;
        InheritedHandle& operator=(const InheritedHandle&) = delete;
    };

    HANDLE input_ = nullptr, output_ = nullptr, childInput_ = nullptr, childOutput_ = nullptr;
    bool outstanding_ = false;
    std::string buffered_;
    HANDLE job_ = nullptr;
    HANDLE process_ = nullptr;

    static void fail(const char* operation) {
        const DWORD error = GetLastError();
        throw std::runtime_error(std::string(operation) + " failed (Windows error " +
                                 std::to_string(error) + ").");
    }

    static std::wstring quote(const std::wstring& value) {
        std::wstring result = L"\"";
        std::size_t slashes = 0;
        for (wchar_t ch : value) {
            if (ch == L'\\') { ++slashes; continue; }
            result.append(ch == L'"' ? slashes * 2 + 1 : slashes, L'\\');
            result += ch;
            slashes = 0;
        }
        result.append(slashes * 2, L'\\');
        return result + L"\"";
    }

    void close() noexcept {
        for (auto handle : {input_, output_, childInput_, childOutput_}) if (handle) CloseHandle(handle);
        input_ = output_ = childInput_ = childOutput_ = nullptr;
        if (job_) { CloseHandle(job_); job_ = nullptr; }
        if (process_) {
            WaitForSingleObject(process_, 5000);
            CloseHandle(process_);
            process_ = nullptr;
        }
    }
};

NodeProcess::NodeProcess(const std::wstring& executable,
                         const std::vector<std::wstring>& arguments,
                         const std::wstring& workingDirectory, bool protocol)
    : impl_(std::make_unique<Impl>(executable, arguments, workingDirectory, protocol)) {}
NodeProcess::~NodeProcess() = default;
unsigned long NodeProcess::id() const { return impl_->id(); }
int NodeProcess::wait() const { return impl_->wait(); }
std::optional<int> NodeProcess::poll() const { return impl_->poll(); }


bool NodeProcess::sendLine(const std::string &line) { return impl_->sendLine(line); }
std::optional<std::string> NodeProcess::readLine() { return impl_->readLine(); }
