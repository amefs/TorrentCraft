#include <filesystem>
#include <fstream>
#include <iostream>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/hex.hpp>
#include <libtorrent/torrent_info.hpp>

namespace {
namespace lt = libtorrent;

bool write_payload(const std::filesystem::path& path, std::size_t size, char value)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << std::string(size, value);
    return static_cast<bool>(output);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: generator <torrent-path> <work-directory>\n";
        return 2;
    }

    const std::filesystem::path torrent_path(argv[1]);
    const std::filesystem::path work_directory(argv[2]);
    const auto content = work_directory / "payload";
    std::error_code filesystem_error;
    std::filesystem::create_directories(content, filesystem_error);
    if (filesystem_error || !write_payload(content / "a.bin", 10000, 'a') ||
        !write_payload(content / "b.bin", 100, 'b'))
    {
        std::cerr << "could not create fixture payload\n";
        return 3;
    }

    lt::file_storage files;
    lt::add_files(files, content.string());
    lt::create_torrent creator(files, 16 * 1024);
    creator.set_creation_date(0);
    lt::error_code hash_error;
    lt::set_piece_hashes(creator, content.parent_path().string(), hash_error);
    if (hash_error)
    {
        std::cerr << "could not hash fixture payload: " << hash_error.message() << '\n';
        return 4;
    }

    const auto encoded = creator.generate_buf();
    std::ofstream output(torrent_path, std::ios::binary | std::ios::trunc);
    output.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    output.close();
    if (!output)
    {
        std::cerr << "could not write fixture torrent\n";
        return 5;
    }

    lt::error_code metadata_error;
    const lt::torrent_info metadata(encoded.data(), static_cast<int>(encoded.size()),
                                    metadata_error);
    if (metadata_error || metadata.num_pieces() != 2 || metadata.files().num_files() != 4)
    {
        std::cerr << "generated fixture did not retain the expected Hybrid layout\n";
        return 6;
    }
    const auto hashes = metadata.info_hashes();
    std::cout << "v1=" << lt::aux::to_hex(hashes.v1.to_string()) << '\n';
    std::cout << "v2=" << lt::aux::to_hex(hashes.v2.to_string()) << '\n';
    std::filesystem::remove_all(work_directory, filesystem_error);
    return 0;
}
