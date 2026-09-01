#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/hex.hpp>
#include <libtorrent/torrent_info.hpp>
#include <string_view>

namespace {
namespace lt = libtorrent;

struct MatrixCase
{
    std::string_view filename;
    lt::create_flags_t flags;
};

bool write_payload(const std::filesystem::path& path, std::size_t size, char value)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << std::string(size, value);
    return static_cast<bool>(output);
}

bool generate_case(const MatrixCase& matrix_case, const std::filesystem::path& output_directory,
                   const std::filesystem::path& content)
{
    lt::file_storage files;
    files.add_file("payload/a.bin", 10000);
    files.add_file("payload/nested/b.bin", 100);
    lt::create_torrent creator(files, 16 * 1024, matrix_case.flags | lt::create_torrent::symlinks);
    creator.set_priv(true);
    creator.set_creation_date(0);
    creator.add_tracker("https://one.example/announce", 0);
    creator.add_tracker("https://two.example/announce", 1);
    creator.add_url_seed("https://seed.example/payload/");

    lt::error_code hash_error;
    lt::set_piece_hashes(creator, content.parent_path().string(), hash_error);
    if (hash_error)
    {
        std::cerr << matrix_case.filename << ": hashing failed: " << hash_error.message() << '\n';
        return false;
    }

    const auto encoded = creator.generate_buf();
    const auto torrent_path = output_directory / matrix_case.filename;
    std::ofstream output(torrent_path, std::ios::binary | std::ios::trunc);
    output.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
    output.close();
    if (!output)
    {
        std::cerr << matrix_case.filename << ": write failed\n";
        return false;
    }

    lt::error_code metadata_error;
    const lt::torrent_info metadata(encoded.data(), static_cast<int>(encoded.size()),
                                    metadata_error);
    if (metadata_error)
    {
        std::cerr << matrix_case.filename << ": reread failed: " << metadata_error.message()
                  << '\n';
        return false;
    }

    const auto hashes = metadata.info_hashes();
    std::cout << matrix_case.filename << " files=" << metadata.files().num_files();
    if (hashes.has_v1())
        std::cout << " v1=" << lt::aux::to_hex(hashes.v1.to_string());
    if (hashes.has_v2())
        std::cout << " v2=" << lt::aux::to_hex(hashes.v2.to_string());
    std::cout << '\n';
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "usage: generator <output-directory> <work-directory>\n";
        return 2;
    }

    const std::filesystem::path output_directory(argv[1]);
    const std::filesystem::path work_directory(argv[2]);
    const auto content = work_directory / "payload";
    std::error_code filesystem_error;
    std::filesystem::create_directories(content / "nested", filesystem_error);
    if (filesystem_error || !write_payload(content / "a.bin", 10000, 'a') ||
        !write_payload(content / "nested" / "b.bin", 100, 'b'))
    {
        std::cerr << "could not create fixture payload\n";
        return 3;
    }

    const std::array<MatrixCase, 3> cases{{
        {"create-matrix-v1.torrent", lt::create_torrent::v1_only},
        {"create-matrix-v2.torrent", lt::create_torrent::v2_only},
        {"create-matrix-hybrid.torrent", {}},
    }};
    for (const auto& matrix_case : cases)
    {
        if (!generate_case(matrix_case, output_directory, content))
            return 4;
    }

    std::filesystem::remove_all(work_directory, filesystem_error);
    return 0;
}
