#include "platform/Environment.h"
#include "platform/WindowsSupport.h"

#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

namespace {

class TestRun final {
  public:
    void expect(bool condition, std::string_view contract) {
        ++mAssertions;
        if (condition)
            return;
        ++mFailures;
        std::cerr << "FAIL: " << contract << '\n';
    }

    int result() const {
        if (mFailures == 0)
            std::cout << mAssertions << " assertions passed\n";
        return mFailures == 0 ? 0 : 1;
    }

  private:
    int mAssertions = 0;
    int mFailures = 0;
};

struct Pipe {
    HANDLE read = nullptr;
    HANDLE write = nullptr;
};

bool createInheritablePipe(Pipe& pipe, bool inheritRead) {
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    if (!::CreatePipe(&pipe.read, &pipe.write, &attributes, 0))
        return false;
    return ::SetHandleInformation(inheritRead ? pipe.write : pipe.read, HANDLE_FLAG_INHERIT, 0) !=
           0;
}

void closeHandle(HANDLE& handle) {
    if (handle) {
        ::CloseHandle(handle);
        handle = nullptr;
    }
}

struct ChildProcess {
    PROCESS_INFORMATION information{};
    Pipe input;
    Pipe output;
};

std::wstring wide(const std::string& utf8) {
    kue::platform::WideText<8192> text;
    kue::platform::toWide(std::string_view(utf8), text);
    return std::wstring(text.units.data(), text.size);
}

std::wstring quoted(const std::string& argument) {
    return L"\"" + wide(argument) + L"\"";
}

bool launch(ChildProcess& child, const std::wstring& commandLine) {
    if (!createInheritablePipe(child.input, true) || !createInheritablePipe(child.output, false))
        return false;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = child.input.read;
    startup.hStdOutput = child.output.write;
    startup.hStdError = child.output.write;
    std::wstring mutableCommand = commandLine;
    const BOOL created =
        ::CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                         nullptr, nullptr, &startup, &child.information);
    closeHandle(child.input.read);
    closeHandle(child.output.write);
    return created != 0;
}

bool readLineFrom(HANDLE handle, std::string& line) {
    line.clear();
    char byte = 0;
    DWORD read = 0;
    while (::ReadFile(handle, &byte, 1, &read, nullptr) && read == 1) {
        if (byte == '\n')
            return true;
        if (byte != '\r')
            line.push_back(byte);
    }
    return !line.empty();
}

std::string readAll(HANDLE handle) {
    std::string content;
    char buffer[512];
    DWORD read = 0;
    while (::ReadFile(handle, buffer, sizeof(buffer), &read, nullptr) && read > 0)
        content.append(buffer, read);
    return content;
}

bool writeLine(HANDLE handle, std::string_view text) {
    std::string line(text);
    line.push_back('\n');
    DWORD written = 0;
    return ::WriteFile(handle, line.data(), static_cast<DWORD>(line.size()), &written, nullptr) &&
           written == line.size();
}

DWORD waitExit(ChildProcess& child) {
    ::WaitForSingleObject(child.information.hProcess, 60000);
    DWORD code = 0xffffffff;
    ::GetExitCodeProcess(child.information.hProcess, &code);
    closeHandle(child.information.hProcess);
    closeHandle(child.information.hThread);
    closeHandle(child.input.write);
    closeHandle(child.output.read);
    return code;
}

bool assignEnvironment(TestRun& run, const char* name, const char* value) {
    const bool assigned = kue::platform::writeEnvironment(name, value) == 0;
    run.expect(assigned, std::string("the injector environment variable ") + name + " is assigned");
    return assigned;
}

std::string fileName(const std::string& path) {
    const std::size_t separator = path.find_last_of("\\/");
    return separator == std::string::npos ? path : path.substr(separator + 1);
}

void runRollbackCase(TestRun& run, const char* mode, const std::string& injector,
                     const std::string& fixture, const std::string& failureModule,
                     const std::string& config) {
    assignEnvironment(run, "KUE_FIXTURE_BOOT", "state-start-failure");
    assignEnvironment(run, "KUE_CONFIG", nullptr);
    assignEnvironment(run, "KUE_LOG", nullptr);
    ChildProcess target;
    const std::string label = std::string(mode) + " rollback";
    if (!launch(target,
                quoted(fixture) + L" " + wide(mode) + L" " + quoted(fileName(failureModule)))) {
        run.expect(false, label + " fixture launches");
        return;
    }
    std::string line;
    run.expect(readLineFrom(target.output.read, line) && line == "ready",
               label + " fixture becomes ready");

    assignEnvironment(run, "KUE_MODULE", failureModule.c_str());
    assignEnvironment(run, "KUE_CONFIG", config.c_str());
    assignEnvironment(run, "KUE_LOG", "replacement-log.txt");
    assignEnvironment(run, "GAME_PATTERN", fileName(fixture).c_str());
    ChildProcess inject;
    if (!launch(inject, quoted(injector))) {
        run.expect(false, label + " injector launches");
        writeLine(target.input.write, "release");
        waitExit(target);
        return;
    }
    const std::string output = readAll(inject.output.read);
    const DWORD injectorExit = waitExit(inject);
    run.expect(injectorExit == 1, label + " reports the failed start with exit status 1");
    run.expect(output.find("kue_start failed: result=3") != std::string::npos,
               label + " reaches the failed start");
    run.expect(output.find("failed startup target environment restored") != std::string::npos,
               label + " reports restored target state");
    run.expect(output.find("failed startup module handle released") != std::string::npos,
               label + " reports released failed module");
    if (injectorExit != 1 || output.find("kue_start failed: result=3") == std::string::npos)
        std::cerr << output;

    run.expect(writeLine(target.input.write, "release"), label + " fixture is released");
    const DWORD fixtureExit = waitExit(target);
    run.expect(fixtureExit == 0, label + " postcondition holds in the target process");
    if (fixtureExit != 0)
        std::cerr << "fixture exit " << fixtureExit << '\n';
}

void runNoTargetCase(TestRun& run, const std::string& injector, const std::string& failureModule,
                     const std::string& config) {
    assignEnvironment(run, "KUE_MODULE", failureModule.c_str());
    assignEnvironment(run, "KUE_CONFIG", config.c_str());
    assignEnvironment(run, "KUE_LOG", "replacement-log.txt");
    assignEnvironment(run, "GAME_PATTERN", "kue-no-such-process.exe");
    ChildProcess inject;
    if (!launch(inject, quoted(injector))) {
        run.expect(false, "no-target injector launches");
        return;
    }
    const std::string output = readAll(inject.output.read);
    run.expect(waitExit(inject) == 1, "no-target injection fails");
    run.expect(output.find("no running game matched 'kue-no-such-process.exe'") !=
                   std::string::npos,
               "no-target injection reports the exact no-target contract");
}

}

int main(int argc, char** argv) {
    TestRun run;
    run.expect(argc == 4, "the injector, fixture, and failure module paths are provided");
    if (argc != 4)
        return run.result();
    const std::string injector = argv[1];
    const std::string fixture = argv[2];
    const std::string failureModule = argv[3];
    const std::string config = fixture + ".config.json";
    std::FILE* const configFile = std::fopen(config.c_str(), "wb");
    run.expect(configFile != nullptr, "a configuration fixture is written");
    if (configFile) {
        std::fputs("{}\n", configFile);
        std::fclose(configFile);
    }
    runNoTargetCase(run, injector, failureModule, config);
    runRollbackCase(run, "unset", injector, fixture, failureModule, config);
    runRollbackCase(run, "set", injector, fixture, failureModule, config);
    std::remove(config.c_str());
    return run.result();
}
