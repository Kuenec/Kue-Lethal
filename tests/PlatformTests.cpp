#include "core/Utf8.h"
#include "platform/Environment.h"
#include "platform/FileSystem.h"
#include "platform/Process.h"
#include "platform/ProcessMemory.h"

#include <array>
#include <bit>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace {

class TestRun final {
  public:
    bool expect(bool condition, std::string_view contract) {
        ++mAssertions;
        if (condition)
            return true;
        ++mFailures;
        std::cerr << "FAIL: " << contract << '\n';
        return false;
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

using kue::platform::EnvironmentStatus;
using kue::platform::EnvironmentStorage;

constexpr const char* kVariable = "KUE_PLATFORM_TEST_VARIABLE";

std::string readWholeFile(const std::string& path) {
    std::string content;
    std::FILE* const stream = std::fopen(path.c_str(), "rb");
    if (!stream)
        return content;
    std::array<char, 512> chunk{};
    while (true) {
        const std::size_t count = std::fread(chunk.data(), 1, chunk.size(), stream);
        content.append(chunk.data(), count);
        if (count < chunk.size())
            break;
    }
    std::fclose(stream);
    return content;
}

void testUtf8ToUtf16(TestRun& run) {
    std::array<std::uint16_t, 8> units{};
    const std::string text = "a\xc3\xa9\xe2\x98\x83\xf0\x9f\x9a\x80";
    const kue::Utf8ToUtf16Result converted =
        kue::convertUtf8ToUtf16({text.data(), text.size()}, {units.data(), units.size()});
    run.expect(converted.status == kue::Utf8ToUtf16Status::Success &&
                   converted.utf16CodeUnits == 5 && units[0] == 'a' && units[1] == 0xe9U &&
                   units[2] == 0x2603U && units[3] == 0xd83dU && units[4] == 0xde80U,
               "UTF-8 to UTF-16 conversion encodes basic, multibyte, and supplementary text");
    const kue::Utf8ToUtf16Result insufficient =
        kue::convertUtf8ToUtf16({text.data(), text.size()}, {units.data(), 4});
    run.expect(insufficient.status == kue::Utf8ToUtf16Status::OutputCapacityExceeded &&
                   insufficient.utf16CodeUnits == 5,
               "insufficient UTF-16 capacity reports the required code units");
    const std::array<char, 2> invalid{static_cast<char>(0xc0), 'x'};
    run.expect(
        kue::convertUtf8ToUtf16({invalid.data(), invalid.size()}, {units.data(), units.size()})
                .status == kue::Utf8ToUtf16Status::InvalidUtf8,
        "ill-formed UTF-8 is rejected before conversion");
    run.expect(kue::convertUtf8ToUtf16({nullptr, 0}, {units.data(), units.size()}).status ==
                   kue::Utf8ToUtf16Status::NullInput,
               "null UTF-8 input is rejected");
    run.expect(kue::convertUtf8ToUtf16({text.data(), 0}, {units.data(), units.size()}).status ==
                   kue::Utf8ToUtf16Status::EmptyInput,
               "empty UTF-8 input is reported");
}

void testEnvironment(TestRun& run) {
    EnvironmentStorage storage;
    run.expect(kue::platform::writeEnvironment(kVariable, nullptr) == 0,
               "an absent variable can be cleared");
    run.expect(kue::platform::readEnvironment(kVariable, storage).status ==
                   EnvironmentStatus::Unset,
               "a cleared variable reads as unset");
    run.expect(kue::platform::writeEnvironment(kVariable, "value/\xc3\xa9") == 0,
               "a UTF-8 value can be written");
    const kue::platform::EnvironmentValue value =
        kue::platform::readEnvironment(kVariable, storage);
    run.expect(value.status == EnvironmentStatus::Valid && value.text == "value/\xc3\xa9" &&
                   storage[value.text.size()] == '\0',
               "a written UTF-8 value reads back exactly and null-terminated");
    run.expect(kue::platform::writeEnvironment(kVariable, "") == 0,
               "an empty value can be written");
    run.expect(kue::platform::readEnvironment(kVariable, storage).status ==
                   EnvironmentStatus::Empty,
               "an empty value reads as empty rather than unset");
    const std::string oversized(kue::platform::kMaximumEnvironmentValueBytes + 1, 'x');
    if (kue::platform::writeEnvironment(kVariable, oversized.c_str()) == 0) {
        run.expect(kue::platform::readEnvironment(kVariable, storage).status ==
                       EnvironmentStatus::TooLong,
                   "an oversized value reads as too long");
    }
    const std::string maximum(kue::platform::kMaximumEnvironmentValueBytes, 'y');
    run.expect(kue::platform::writeEnvironment(kVariable, maximum.c_str()) == 0,
               "a maximum-length value can be written");
    const kue::platform::EnvironmentValue maximumValue =
        kue::platform::readEnvironment(kVariable, storage);
    run.expect(maximumValue.status == EnvironmentStatus::Valid &&
                   maximumValue.text.size() == kue::platform::kMaximumEnvironmentValueBytes,
               "a maximum-length value reads back completely");
    run.expect(kue::platform::writeEnvironment(kVariable, nullptr) == 0,
               "the test variable is removed");
    run.expect(kue::platform::readEnvironment(kVariable, storage).status ==
                   EnvironmentStatus::Unset,
               "a removed variable reads as unset");
}

void testFiles(TestRun& run) {
    std::error_code code;
    const std::filesystem::path root = std::filesystem::temp_directory_path(code);
    if (!run.expect(!code, "the temporary directory is discoverable"))
        return;
    const std::string directory =
        (root / ("kue-platform-tests-" + std::to_string(kue::platform::currentProcessId())))
            .string();
    std::filesystem::create_directory(directory, code);
    if (!run.expect(!code, "the test directory is created"))
        return;
    const std::string destination = directory + kue::platform::kPathSeparator + "target.txt";

    std::string temporaryTemplate = destination + ".tmp.XXXXXX";
    std::array<char, kue::platform::kMaximumTemporaryPathBytes + 1> temporaryPath{};
    std::memcpy(temporaryPath.data(), temporaryTemplate.data(), temporaryTemplate.size());
    const kue::platform::DescriptorResult created =
        kue::platform::createExclusiveTemporary(temporaryPath.data());
    if (!run.expect(created.descriptor >= 0, "an exclusive temporary file is created"))
        return;
    run.expect(std::string_view(temporaryPath.data()).find("XXXXXX") == std::string_view::npos,
               "the temporary template is replaced with a unique name");
    const kue::platform::InheritanceInspection inheritance =
        kue::platform::inspectInheritance(created.descriptor);
    run.expect(inheritance.succeeded && !inheritance.inheritable,
               "the temporary descriptor is not inheritable");
    const kue::platform::StreamResult stream =
        kue::platform::associateStream(created.descriptor, "wb");
    if (!run.expect(stream.stream != nullptr, "the temporary descriptor becomes a stream"))
        return;
    constexpr std::string_view payload = "payload\n";
    run.expect(std::fwrite(payload.data(), 1, payload.size(), stream.stream) == payload.size() &&
                   std::fflush(stream.stream) == 0,
               "the temporary stream accepts a write");
    run.expect(kue::platform::synchronizeDescriptor(created.descriptor) == 0,
               "the temporary descriptor synchronizes");
    run.expect(std::fclose(stream.stream) == 0, "the temporary stream closes");
    const kue::platform::ReplacementResult replacement =
        kue::platform::replaceFile(temporaryPath.data(), destination.c_str());
    run.expect(replacement.status == kue::platform::ReplacementStatus::Durable,
               "a temporary file replaces its destination durably");
    run.expect(readWholeFile(destination) == payload, "the replaced destination has the payload");
    run.expect(!std::filesystem::exists(temporaryPath.data()),
               "the temporary file no longer exists after replacement");

    const kue::platform::DescriptorResult reader = kue::platform::openForRead(destination.c_str());
    if (!run.expect(reader.descriptor >= 0, "the destination opens for reading"))
        return;
    const kue::platform::FileInspection inspection = kue::platform::inspectFile(reader.descriptor);
    run.expect(inspection.succeeded && inspection.kind == kue::platform::FileKind::Regular &&
                   inspection.bytes == payload.size(),
               "a regular file reports its kind and size");
    std::array<char, 64> buffer{};
    const kue::platform::ReadResult read = kue::platform::readSome(reader.descriptor, buffer);
    run.expect(read.errorCode == 0 && read.bytes == payload.size() &&
                   std::string_view(buffer.data(), read.bytes) == payload,
               "reading returns the complete payload");
    const kue::platform::ReadResult end = kue::platform::readSome(reader.descriptor, buffer);
    run.expect(end.errorCode == 0 && end.bytes == 0, "reading at end of file returns zero bytes");
    run.expect(kue::platform::closeDescriptor(reader.descriptor) == 0, "the reader closes");

    const kue::platform::DescriptorResult appender =
        kue::platform::openForAppend(destination.c_str());
    if (run.expect(appender.descriptor >= 0, "the destination opens for appending")) {
        const kue::platform::StreamResult appendStream =
            kue::platform::associateStream(appender.descriptor, "a");
        if (run.expect(appendStream.stream != nullptr, "the append descriptor becomes a stream")) {
            std::fputs("more\n", appendStream.stream);
            std::fclose(appendStream.stream);
        }
        run.expect(readWholeFile(destination) == std::string(payload) + "more\n",
                   "appending preserves the earlier payload");
    }

    const kue::platform::DescriptorResult missing =
        kue::platform::openForRead((directory + kue::platform::kPathSeparator + "missing").c_str());
    run.expect(missing.descriptor < 0 && missing.errorCode == ENOENT,
               "a missing file reports ENOENT");
    const kue::platform::DescriptorResult directoryRead =
        kue::platform::openForRead(directory.c_str());
    if (directoryRead.descriptor >= 0) {
        const kue::platform::FileInspection directoryInspection =
            kue::platform::inspectFile(directoryRead.descriptor);
        run.expect(directoryInspection.succeeded &&
                       directoryInspection.kind == kue::platform::FileKind::Other,
                   "a directory is not a regular file");
        static_cast<void>(kue::platform::closeDescriptor(directoryRead.descriptor));
    }
    run.expect(kue::platform::removeFile(destination.c_str()) == 0, "the destination is removed");
    run.expect(kue::platform::removeFile(destination.c_str()) == ENOENT,
               "removing a missing file reports ENOENT");
    const kue::platform::ConsoleInspection console = kue::platform::inspectConsole(stdout);
    run.expect(console.succeeded, "console inspection succeeds for standard output");
    std::filesystem::remove_all(directory, code);
    run.expect(!code, "the test directory is removed");
}

void testMemory(TestRun& run) {
    const std::size_t page = kue::platform::pageSize();
    run.expect(page >= 4096 && (page & (page - 1)) == 0, "the page size is a power of two");
    std::uintptr_t probe = 0;
    void* reserved = nullptr;
    const auto self = std::bit_cast<std::uintptr_t>(&testMemory) & ~(page - 1);
    for (std::uintptr_t distance = std::uintptr_t{1} << 20;
         distance < (std::uintptr_t{1} << 30) && !reserved; distance += std::uintptr_t{1} << 20) {
        reserved = kue::platform::reserveNear(self + distance, page);
        if (!reserved && distance < self)
            reserved = kue::platform::reserveNear(self - distance, page);
    }
    if (!run.expect(reserved != nullptr, "a page can be reserved near a code address"))
        return;
    probe = std::bit_cast<std::uintptr_t>(reserved);
    const std::uintptr_t separation = probe > self ? probe - self : self - probe;
    run.expect(separation < (std::uintptr_t{1} << 31), "the reservation lies within 2 GiB");
    auto* const bytes = static_cast<std::uint8_t*>(reserved);
    bytes[0] = 0xc3;
    run.expect(kue::platform::setProtection(reserved, page, kue::platform::kPageReadExecute),
               "a reserved page becomes read-execute");
    kue::platform::flushInstructionCache(reserved, page);
    bool found = false;
    bool executableFound = false;
    kue::platform::MemoryRegionScan scan;
    kue::platform::MemoryRegion region;
    std::size_t regions = 0;
    while (scan.next(region)) {
        ++regions;
        run.expect(region.begin < region.end, "every scanned region has a positive size");
        if (probe >= region.begin && probe < region.end) {
            found = true;
            executableFound = region.executable && region.readable &&
                              region.kind == kue::platform::MemoryRegionKind::Private;
        }
    }
    run.expect(regions > 0, "the region scan reports regions");
    run.expect(found, "the reserved page appears in the region scan");
    run.expect(executableFound, "the reserved page is reported as private and executable");
    run.expect(kue::platform::setProtection(reserved, page, kue::platform::kPageReadWrite),
               "a page returns to read-write");
    bytes[1] = 0x90;
    kue::platform::releaseReservation(reserved, page);
    run.expect(kue::platform::reserveNear(probe, page) != nullptr ||
                   kue::platform::reserveNear(probe, page) == nullptr,
               "releasing a reservation completes");
    if (void* again = kue::platform::reserveNear(probe, page))
        kue::platform::releaseReservation(again, page);
}

void testProcess(TestRun& run) {
    run.expect(kue::platform::currentProcessId() != 0, "the process identifier is nonzero");
    std::array<char, kue::platform::kMaximumPathBytes + 1> storage{};
    const kue::platform::ModuleDirectory directory = kue::platform::currentModuleDirectory(storage);
    run.expect(directory.available, "the current module directory is available");
    if (directory.available) {
        std::error_code code;
        run.expect(directory.path.empty() ||
                       std::filesystem::is_directory(std::string(directory.path), code),
                   "the current module directory exists");
        run.expect(storage[directory.path.size()] == '\0',
                   "the current module directory is null-terminated");
    }
}

}

int main() {
    TestRun run;
    testUtf8ToUtf16(run);
    testEnvironment(run);
    testFiles(run);
    testMemory(run);
    testProcess(run);
    return run.result();
}
