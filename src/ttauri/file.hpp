// Copyright Take Vos 2019-2020.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at https://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "URL.hpp"
#include "byte_string.hpp"
#include "architecture.hpp"
#include <mutex>
#include <cstdint>
#include <map>
#include <span>

namespace tt {

enum class seek_whence {
    begin,
    current,
    end
};

enum class access_mode {
    read = 0x1, ///< Allow read access to a file.
    write = 0x2, ///< Allow write access to a file.
    rename = 0x4, ///< Allow renaming an open file.
    read_lock = 0x10, ///< Lock the file for reading, i.e. shared-lock.
    write_lock = 0x20, ///< Lock the file for writing, i.e. exclusive-lock.
    open = 0x100, ///< Open file if it exist, or fail.
    create = 0x200, ///< Create file if it does not exist, or fail.
    truncate = 0x400, ///< After the file has been opened, truncate it.
    random = 0x1000, ///< Hint the data should not be prefetched.
    sequential = 0x2000, ///< Hint that the data should be prefetched.
    no_reuse = 0x4000, ///< Hint that the data should not be cached.
    write_through = 0x8000, ///< Hint that writes should be send directly to disk.
    create_directories = 0x10000, ///< Create directory hierarchy, if the file could not be created.
    
    open_for_read = 0x101, ///< Default open a file for reading.
    open_for_read_and_write = 0x103, ///< Default open a file for reading and writing.
    truncate_or_create_for_write = 0x702
};

[[nodiscard]] inline access_mode operator|(access_mode lhs, access_mode rhs) noexcept
{
    return static_cast<access_mode>(static_cast<int>(lhs) | static_cast<int>(rhs));
}

[[nodiscard]] inline access_mode operator&(access_mode lhs, access_mode rhs) noexcept
{
    return static_cast<access_mode>(static_cast<int>(lhs) & static_cast<int>(rhs));
}

/** True if all bits on rhs are set in lhs.
 */
[[nodiscard]] inline bool operator>=(access_mode lhs, access_mode rhs) noexcept
{
    return (lhs & rhs) == rhs;
}

/** A File object.
 */
class file { 
public:
    /** Open a file at location.
     * \param location The file: URL locating the file.
     * \param accessMode access-mode to open the file.
     */
    file(URL const& location, access_mode accessMode);

    ~file() noexcept;

    file(file const &other) = delete;
    file(file &&other) = delete;
    file &operator=(file const &other) = delete;
    file &operator=(file &&other) = delete;

    /** Close the file.
     */
    void close();

    /** Flush and block until all data is physically written to disk.
     * Flushing is required before renaming a file, to prevent
     * data corruption when the computer crashes during the rename.
     */
    void flush();

    /** Rename an open file.
     * This function will rename an open file atomically.
     * 
     * @param destination The destination file name.
     * @param overwrite_existing Overwrite an existing file.
     * @throw io_error When failing to rename.
     */
    void rename(URL const &destination, bool overwrite_existing=true);

    /** Return the size of the file.
     */
    size_t size() const;

    /** Set the seek location.
     * @param offset To move the file pointer.
     * @param whence Where to seek from: begin, current or end
     * @return The new seek position relative to the beginning of the file.
     */
    ssize_t seek(ssize_t offset, seek_whence whence = seek_whence::begin);

    /** Get the current seek location.
     */
    ssize_t get_seek() {
        return seek(0, seek_whence::current);
    }

    /** Write data to a file.
    * 
    * @param data Pointer to data to be written.
    * @param size The number of bytes to write.
    * @param offset The offset in the file to write, or -1 when writing in the current seek location.
    * @return The number of bytes written.
    * @throw io_error
    */
    ssize_t write(std::byte const *data, ssize_t size, ssize_t offset=-1);

    /** Write data to a file.
    * 
    * @param data Pointer to data to be written.
    * @param size The number of bytes to write.
    * @param offset The offset in the file to write, or -1 when writing in the current seek location.
    * @return The number of bytes written.
    * @throw io_error
    */
    ssize_t write(void const *data, ssize_t size, ssize_t offset=-1) {
        return write(reinterpret_cast<std::byte const *>(data), size, offset);
    }

    /** Write data to a file.
    * 
    * @param data Pointer to data to be written.
    * @param size The number of bytes to write.
    * @param offset The offset in the file to write, or -1 when writing in the current seek location.
    * @return The number of bytes written.
    * @throw io_error
    */
    ssize_t write(char const *data, ssize_t size, ssize_t offset=-1) {
        return write(reinterpret_cast<std::byte const *>(data), size, offset);
    }

    /** Write data to a file.
     *
     * @param bytes The byte string to write
     * @param offset The offset in the file to write, or -1 when writing in the current seek location.
     * @return The number of bytes written.
     * @throw io_error
     */
    ssize_t write(std::span<std::byte const> bytes, ssize_t offset = -1)
    {
        return write(bytes.data(), std::ssize(bytes), offset);
    }

    /** Write data to a file.
     *
     * @param text The byte string to write
     * @param offset The offset in the file to write, or -1 when writing in the current seek location.
     * @return The number of bytes written.
     * @throw io_error
     */
    ssize_t write(bstring_view text, ssize_t offset = -1)
    {
        return write(text.data(), std::ssize(text), offset);
    }

    /** Write data to a file.
     *
     * @param text The UTF-8 string to write
     * @return The number of bytes written.
     * @throw io_error
     */
    ssize_t write(std::string_view text)
    {
        return write(text.data(), std::ssize(text));
    }

    /** Read data from a file.
     *
     * @param data Pointer to a buffer to read into.
     * @param size The number of bytes to read.
     * @param offset The offset in the file to read, or -1 when reading from the current seek location.
     * @return The number of bytes read.
     * @throw io_error
     */
    ssize_t read(std::byte *data, ssize_t size, ssize_t offset = -1);

    ssize_t read(void *data, ssize_t size, ssize_t offset = -1)
    {
        return read(reinterpret_cast<std::byte *>(data), size, offset);
    }

    /** Read bytes from the file.
     *
     * @param size The maximum number of bytes to read.
     * @param offset The offset into the file to read, or -1 when reading from the current seek location.
     * @return Data as a byte string, may return less then the requested size.
     * @throws io_error On IO error.
     */
    bstring read_bstring(ssize_t size = 10'000'000, ssize_t offset = -1);

    /** Read the whole file as a UTF-8 string.
     * This will ignore the value from `seek()`, and read the whole
     * file due to UTF-8 character sequences to be complete.
     * 
     * If there is more data in the file than the maximum amount to read
     * this function throws an io_error.
     * 
     * @param max_size The maximum size to read.
     * @return Data as a UTF-8 string.
     * @throws io_error On IO error.
     * @throws parse_error On invalid UTF-8 string.
     */
    std::string read_string(ssize_t max_size = 10'000'000);

    /** Read the whole file as a UTF-8 string.
     * This will ignore the value from `seek()`, and read the whole
     * file due to UTF-8 character sequences to be complete.
     * 
     * If there is more data in the file than the maximum amount to read
     * this function throws an io_error.
     * 
     * @param max_size The maximum size to read.
     * @return Data as a UTF-8 string.
     * @throws io_error On IO error
     * @throws parse_error On invalid UTF-8 string.
     */
    std::u8string read_u8string(ssize_t max_size = 10'000'000);

    /** Get the size of a file on the file system.
     * \return The size of the file in bytes.
     */
    [[nodiscard]] static size_t file_size(URL const &url);

    static void create_directory(URL const &url, bool hierarchy=false);

    static void create_directory_hierarchy(URL const &url);

private:
    /** The access mode used to open the file.
     */
    access_mode _access_mode;

    /** The URL that was used to open the file.
     */
    URL _location;

    /** A operating system handle to the file.
     */
    file_handle _file_handle;

    friend class file_mapping;
    friend class file_view;
};


}
