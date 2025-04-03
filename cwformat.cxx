#include "sys.h"
#include "SourceFile.h"
#include "TranslationUnit.h"

#include <cerrno>
#include <filesystem>
#include <iostream>
#include <random>

//=============================================================================
// Command Line Options.
//
// See https://llvm.org/docs/CommandLine.html
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include <fstream>
#include <string>
#include <vector>

namespace cl = llvm::cl;

static char const* program_name = "cwformat";

// Create a custom category for our options.
cl::OptionCategory cwformat_category("cwformat options");

// Collect input files as positional arguments.
cl::list<std::string> input_files(cl::Positional,
    cl::desc("<file> [<file> ...]"),
    cl::cat(cwformat_category));

// Add clang-format specific options
cl::opt<std::string> assume_filename("assume-filename",
    cl::desc("Set filename used to determine the language and to find .clang-format file"),
    cl::value_desc("string"),
    cl::cat(cwformat_category));

cl::opt<std::string> files_list_file("files",
    cl::desc("A file containing a list of files to process, one per line"),
    cl::value_desc("filename"),
    cl::cat(cwformat_category));

cl::opt<bool> in_place("i",
    cl::desc("Inplace edit <file>s, if specified"),
    cl::cat(cwformat_category));

// Override the default --version behavior.
static void print_version(llvm::raw_ostream& ros)
{
  ros << program_name << " version 0.1.0, written in 2025 by Carlo Wood.\n";
}

// Helper function to read files from a list file
llvm::Expected<std::vector<std::string>> read_files_from_list(std::string const& filename)
{
  auto file_buffer = llvm::MemoryBuffer::getFile(filename);
  if (!file_buffer)
    return llvm::createStringError(llvm::inconvertibleErrorCode(), "Could not open file list: " + filename);

  std::vector<std::string> files;
  llvm::StringRef content = file_buffer.get()->getBuffer();

  while (!content.empty())
  {
    auto line = content.split('\n').first.trim();
    if (!line.empty())
      files.push_back(line.str());
    content = content.substr(content.find_first_of('\n') + 1);
    if (content.empty() || content.size() == 0)
      break;
  }

  return files;
}

//=============================================================================
// Generate unique temporary filename.

class RandomNumber
{
  std::mt19937_64 mersenne_twister_;

 public:
  using result_type = std::mt19937_64::result_type;

  RandomNumber()
  {
    std::random_device rd;
    result_type seed_value = rd() ^ (
            (result_type)
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()
                ).count() +
            (result_type)
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()
                ).count() );
    mersenne_twister_.seed(seed_value);
  }

  int generate(std::uniform_int_distribution<int>& distribution) { return distribution(mersenne_twister_); }
};

// Return an integer in the range [min, max].
int randrange(int min, int max)
{
  static RandomNumber rn;
  static std::uniform_int_distribution<int> dist{min, max};
  return rn.generate(dist);
}

// Get the path to a unique, non-existing file with the prefix `prefix`.
std::filesystem::path get_temp_filename(std::filesystem::path const& prefix)
{
  std::filesystem::path temp_filename;
  do
  {
    temp_filename = prefix;
    for (int n = 0; n < 4; ++n)
      temp_filename += std::format("-{:04x}", randrange(0, 0xffff));
  }
  while (std::filesystem::exists(temp_filename));

  return temp_filename;
}

//=============================================================================
// Main function; process commandline parameters.

// Forward declaration.
void process_filename(std::filesystem::path const& filename, bool use_cin);

int main(int argc, char* argv[])
{
  // Set the program name.
  cl::SetVersionPrinter(&print_version);

  // Hide all options except those in our category.
  cl::HideUnrelatedOptions(cwformat_category);

  // Parse command line options.
  cl::ParseCommandLineOptions(argc, argv);

  // Process files list if provided.
  std::vector<std::filesystem::path> files_to_process;

  if (!files_list_file.empty())
  {
    auto maybe_files = read_files_from_list(files_list_file);
    if (!maybe_files)
    {
      llvm::errs() << "Error reading files list: " << llvm::toString(maybe_files.takeError()) << "\n";
      return 1;
    }

    auto files = maybe_files.get();
    files_to_process.insert(files_to_process.end(), files.begin(), files.end());
  }

  // Add files specified directly on the command line.
  int process_cin = input_files.empty() && files_to_process.empty();
  int position = files_to_process.size();
  for (std::string const& input_file : input_files)
  {
    ++position;
    if (input_file == "-")
    {
      // Process the files in the order they are specified, including stdin.
      process_cin = position;
      continue;
    }
    files_to_process.emplace_back(input_file);
  }

  // Output information about the options.
  position = 0;
  for (std::filesystem::path const& filename : files_to_process)
  {
    ++position;
    if (position == process_cin)
    {
      process_filename({}, true);
      process_cin = false;
    }
    process_filename(filename, false);
  }
  if (process_cin)
    process_filename({}, true);

  if (!assume_filename.empty())
    llvm::outs() << "Using assumed filename: " << assume_filename << "\n";

  if (in_place)
    llvm::outs() << "Files will be edited in-place\n";
}

//=============================================================================
// Process one TU.

// Forward declaration.
void process_input_file(std::string const& input_filename, std::istream& input, std::ostream& output);

// Open the file `path` or std::cin for input, and open a temporary file based
// on `path` or open std::cout for output.
//
// If a file was opened (use_cin is false) and `in_place` is false, move
// the temporary file over the original (path) upon successful conversion.
//
// Calls process_input_file for the actual processing.
void process_filename(std::filesystem::path const& filename, bool use_cin)
{
  // Write to std::cout by default.
  std::ostream* output_stream = &std::cout;
  std::ofstream ofile;
  std::filesystem::path temp_filename;
  if (!use_cin && in_place)     // Are we writing to disk?
  {
    std::filesystem::path temp_filename_prefix = filename;
    temp_filename_prefix += ".temp-stream-";
    temp_filename = get_temp_filename(temp_filename_prefix);
    try
    {
      ofile.exceptions(std::ofstream::failbit);
      ofile.open(temp_filename, std::ios::binary);
    }
    catch (std::system_error const& error)
    {
      int err = errno;
      llvm::errs() << program_name << ": Failed to create \"" << temp_filename << "\": ";
      if (err == ENOENT)
        llvm::errs() << "No such directory\n";
      else
        llvm::errs() << std::strerror(err) << '\n';
      return;
    }
    // Change output_stream to the opened temporary file.
    output_stream = &ofile;
  }

  // Read from std::cin by default;
  std::istream* input_stream = &std::cin;
  std::ifstream ifile;
  if (!use_cin)                 // Are we reading from disk?
  {
    try
    {
      ifile.exceptions(std::ofstream::failbit);
      ifile.open(filename, std::ios::binary);
    }
    catch (std::system_error const& error)
    {
      int err = errno;
      llvm::errs() << program_name << ": Failed to open \"" << filename << "\": " << std::strerror(err) << '\n';
      if (ofile.is_open())
      {
        ofile.close();
        std::filesystem::remove(temp_filename);
      }
      return;
    }
    // Change input_stream to the opened input file.
    input_stream = &ifile;
  }

  try
  {
    process_input_file(use_cin ? "<stdin>" : filename.native(), *input_stream, *output_stream);
    output_stream->flush();
    if (ofile.is_open())
    {
      ofile.close();
      std::filesystem::rename(temp_filename, filename);
    }
    // Success.
    return;
  }
  catch (std::filesystem::filesystem_error const& error)
  {
    std::error_code ec = error.code();
    std::cerr << "Filesystem error " << ec.value() << ": " << ec.message() << std::endl;
    std::cerr << "Path1: " << error.path1() << std::endl;
    if (!error.path2().empty())
      std::cerr << "Path2: " << error.path2() << std::endl;
  }
  catch (std::bad_alloc const& error)
  {
    std::cerr << "Memory allocation failed: " << error.what() << std::endl;
  }
  catch (std::runtime_error const& error)
  {
    std::cerr << "Runtime error: " << error.what() << std::endl;
  }
  catch (std::exception const& error)
  {
    std::cerr << "Other standard exception: " << error.what() << std::endl;
  }
  catch (...)
  {
    std::cerr << "Unknown exception caught" << std::endl;
  }

  llvm::errs() << program_name << ": Failed to format \"" << filename << "\".\n";

  if (ofile.is_open())
  {
    ofile.close();
    std::filesystem::remove(temp_filename);
  }
}

// Read one source file from `input` and write formatted result to `output`.
void process_input_file(std::string const& input_filename, std::istream& input, std::ostream& output)
{
  SourceFile source_file(input_filename, input);
  TranslationUnit TU{source_file COMMA_CWDEBUG_ONLY("TU:" + input_filename)};

  std::cout << "Writing \"" << source_file.filename() << "\":\n";
  TU.print(output);
}
