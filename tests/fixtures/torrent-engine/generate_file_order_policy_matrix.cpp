#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <torrentutils/core/torrent_engine.hpp>
#include <utility>

namespace {
using namespace torrentutils::core;

struct MatrixCase
{
    FileOrderPolicy policy;
    TorrentFormat format;
    const char* filename;
};

bool write_payload(const std::filesystem::path& path, const std::size_t size, const char value)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << std::string(size, value);
    return static_cast<bool>(output);
}

bool generate_case(const MatrixCase& matrix_case, const std::filesystem::path& output_directory,
                   const std::filesystem::path& content)
{
    CreateOptionsInput input;
    input.format = matrix_case.format;
    input.piece_length_strategy = PieceLengthStrategy::Fixed;
    input.fixed_piece_length = 16U * 1024U;
    input.file_order_policy = matrix_case.policy;
    auto options = CreateOptions::create(std::move(input));
    if (!options)
    {
        std::cerr << matrix_case.filename << ": invalid create options\n";
        return false;
    }

    const auto result = TorrentEngine{}.create(
        {content, output_directory / matrix_case.filename, std::move(options).value(), true});
    if (!result)
    {
        std::cerr << matrix_case.filename << ": " << result.error().message << '\n';
        return false;
    }
    std::cout << matrix_case.filename << " payload=" << result.value().payload_bytes
              << " piece-length=" << result.value().piece_length;
    if (const auto& v1 = result.value().info_hashes.v1(); v1)
        std::cout << " v1=" << v1->to_hex();
    if (const auto& v2 = result.value().info_hashes.v2(); v2)
        std::cout << " v2=" << v2->to_hex();
    std::cout << '\n';
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: generator <output-directory>\n";
        return 2;
    }

    const std::filesystem::path output_directory(argv[1]);
    const auto content = output_directory / "payload";
    std::error_code error;
    std::filesystem::create_directories(content / "nested", error);
    if (error || !write_payload(content / "root10.bin", 9000, 'a') ||
        !write_payload(content / "root2.bin", 9000, 'b') ||
        !write_payload(content / "nested" / "child.bin", 9000, 'c'))
    {
        std::cerr << "could not create fixture payload\n";
        return 3;
    }
    std::ofstream(content / "empty.bin", std::ios::binary | std::ios::trunc).close();

    constexpr std::array<MatrixCase, 12> cases{{
        {FileOrderPolicy::Lexicographical, TorrentFormat::V1,
         "file-order-lexicographical-v1.torrent"},
        {FileOrderPolicy::Lexicographical, TorrentFormat::V2,
         "file-order-lexicographical-v2.torrent"},
        {FileOrderPolicy::Lexicographical, TorrentFormat::Hybrid,
         "file-order-lexicographical-hybrid.torrent"},
        {FileOrderPolicy::CanonicalAlignment, TorrentFormat::V1,
         "file-order-canonical-alignment-v1.torrent"},
        {FileOrderPolicy::CanonicalAlignment, TorrentFormat::V2,
         "file-order-canonical-alignment-v2.torrent"},
        {FileOrderPolicy::CanonicalAlignment, TorrentFormat::Hybrid,
         "file-order-canonical-alignment-hybrid.torrent"},
        {FileOrderPolicy::Natural, TorrentFormat::V1, "file-order-natural-v1.torrent"},
        {FileOrderPolicy::Natural, TorrentFormat::V2, "file-order-natural-v2.torrent"},
        {FileOrderPolicy::Natural, TorrentFormat::Hybrid, "file-order-natural-hybrid.torrent"},
        {FileOrderPolicy::BreadthFirst, TorrentFormat::V1, "file-order-breadth-first-v1.torrent"},
        {FileOrderPolicy::BreadthFirst, TorrentFormat::V2, "file-order-breadth-first-v2.torrent"},
        {FileOrderPolicy::BreadthFirst, TorrentFormat::Hybrid,
         "file-order-breadth-first-hybrid.torrent"},
    }};
    for (const auto& matrix_case : cases)
    {
        if (!generate_case(matrix_case, output_directory, content))
            return 4;
    }
    return 0;
}
