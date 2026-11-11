#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>

class MappedFile {
public:
    MappedFile() = default;
    ~MappedFile();

    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    static std::optional<MappedFile> open(const std::filesystem::path& path);

    const unsigned char* data() const { return data_; }
    std::size_t size() const { return size_; }

private:
    const unsigned char* data_ = nullptr;
    std::size_t size_ = 0;
};
