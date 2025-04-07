#include "sys.h"

#include "SourceFile.h"
#include "TranslationUnit.h"
#include "Parser.h"

#include <cerrno>
#include <filesystem>
#include <iostream>
#include <chrono>
#include <random>
#include <system_error>

//=============================================================================
// Command Line Options.
//
// See https://llvm.org/docs/CommandLine.html
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace cl = llvm::cl;

// For error messages and --version output. Set to filename component of argv[0].
static std::string program_name;

// Create a custom category for our options.
cl::OptionCategory cwformat_category("cwformat options");

// Collect input files as positional arguments.
cl::list<std::string> input_files(cl::Positional, cl::desc("<file> [<file> ...]"), cl::cat(cwformat_category));

// Add clang-format specific options
cl::opt<std::string> assume_filename("assume-filename", cl::desc("Set filename used to determine the language and to find .clang-format file"),
  cl::value_desc("string"), cl::cat(cwformat_category));

cl::opt<std::string> files_list_file(
  "files", cl::desc("A file containing a list of files to process, one per line"), cl::value_desc("filename"), cl::cat(cwformat_category));

cl::opt<bool> in_place("i", cl::desc("Inplace edit <file>s, if specified"), cl::cat(cwformat_category));

// Override the default --version behavior.
static void print_version(llvm::raw_ostream& ros)
{
  ros << program_name << " version 0.1.0, written in 2025 by Carlo Wood.\n";
}

// Helper function to read files from a list file.
llvm::Expected<std::vector<std::filesystem::path>> read_files_from_list(std::filesystem::path const& filename)
{
  auto file_buffer_or_err = llvm::MemoryBuffer::getFile(filename.native());
  if (!file_buffer_or_err)
    return llvm::createStringError(file_buffer_or_err.getError(), "Could not open file list: " + filename.native());

  std::vector<std::filesystem::path> files;
  llvm::StringRef content = file_buffer_or_err.get()->getBuffer();

  while (!content.empty())
  {
    llvm::StringRef line;
    std::tie(line, content) = content.split('\n');
    line = line.trim();

    if (!line.empty())
      files.emplace_back(line.str());

    if (content.empty())
      break;
  }

  return files;
}

#ifdef _WIN32
#define WINDOWS_ONLY(x) x
#else
#define WINDOWS_ONLY(x)
#endif

static std::string get_program_name(char const* argv0)
{
  std::string program_name{argv0};
  char const* slash = "/" WINDOWS_ONLY("\\");
  return program_name.substr(program_name.find_last_of(slash) + 1);
}

//=============================================================================
// Generate unique temporary filename.

class RandomNumber
{
 private:
  std::mt19937_64 mersenne_twister_;

 public:
  using result_type = std::mt19937_64::result_type;

  RandomNumber()
  {
    std::random_device rd;
    result_type seed_value = rd() ^
      ((result_type)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() +
        (result_type)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch())
          .count());
    mersenne_twister_.seed(seed_value);
  }

  int generate(std::uniform_int_distribution<int>& distribution) { return distribution(mersenne_twister_); }
};

// Get the path to a unique, non-existing file with the prefix `prefix`.
std::filesystem::path get_temp_filename(RandomNumber& rn, std::filesystem::path const& prefix)
{
  std::filesystem::path temp_filename;
  std::uniform_int_distribution<int> dist{0, 0xffff};
  do
  {
    temp_filename = prefix;
    for (int n = 0; n < 4; ++n)
      temp_filename += std::format("-{:04x}", rn.generate(dist));
  } while (std::filesystem::exists(temp_filename));

  return temp_filename;
}

//=============================================================================
// Main function; process commandline parameters.

// Forward declaration.
void process_filename(Parser& parser, RandomNumber& rn, std::filesystem::path const& filename, bool use_cin);

int main(int argc, char* argv[])
{
  // Set the program name.
  program_name = get_program_name(argv[0]);

  // Function to call upon --version.
  cl::SetVersionPrinter(&print_version);

  // Hide all options except those in our category.
  cl::HideUnrelatedOptions(cwformat_category);

  // Parse command line options.
  cl::ParseCommandLineOptions(argc, argv);

  // Process files list if provided.
  std::vector<std::filesystem::path> files_to_process;

  if (!files_list_file.empty())
  {
    auto maybe_files = read_files_from_list(files_list_file.getValue());
    if (!maybe_files)
    {
      llvm::errs() << program_name << ": Error reading files list '" << files_list_file.getValue()
                   << "': " << llvm::toString(maybe_files.takeError()) << "\n";
      return 1;
    }

    auto const& files = maybe_files.get();
    files_to_process.insert(files_to_process.end(), files.begin(), files.end());
  }

  // Add files specified directly on the command line.
  bool process_cin_requested = false;
  std::vector<std::pair<std::filesystem::path, bool>> work_items; // path, is_stdin

  for (std::string const& input_file : input_files)
  {
    if (input_file == "-")
    {
      work_items.push_back({{}, true}); // Empty path signifies stdin
      process_cin_requested = true;
    }
    else
    {
      work_items.push_back({std::filesystem::path(input_file), false});
    }
  }

  // If no positional args and no --files args, default to stdin
  if (files_to_process.empty() && work_items.empty())
  {
    work_items.push_back({{}, true});
    process_cin_requested = true;
  }

  // Combine --files list and positional arguments into a single processing list
  // Insert items from --files at the beginning
  for (const auto& path_from_list : files_to_process)
  {
    work_items.insert(work_items.begin(), {path_from_list, false});
  }

  // Output information about the options (optional debug info).
  if (!assume_filename.empty())
    llvm::outs() << "Using assumed filename: " << assume_filename << "\n";

  if (in_place && process_cin_requested)
    llvm::errs() << program_name << ": warning: -i ignored when reading from stdin.\n";
  else if (in_place)
    llvm::outs() << "Files will be edited in-place\n";

  // Create a Parser instance.
  Parser parser;
  // Needed for temporary file name generation.
  RandomNumber rn;

  // Process the combined list.
  int return_code = 0; // Track if any file processing failed
  for (const auto& item : work_items)
  {
    try
    {
      process_filename(parser, rn, item.first, item.second);
    }
    catch (std::exception& e)
    {
      llvm::errs() << program_name << ": Error processing '" << (item.second ? "<stdin>" : item.first.native()) << "': " << e.what() << "\n";
      return_code = 1; // Mark failure
    }
    catch (...)
    {
      llvm::errs() << program_name << ": Unknown error processing '" << (item.second ? "<stdin>" : item.first.native()) << "'\n";
      return_code = 1; // Mark failure
    }
  }

  // Output information about the options.
  if (!assume_filename.empty())
    llvm::outs() << "Using assumed filename: " << assume_filename << "\n";

  if (in_place)
    llvm::outs() << "Files will be edited in-place\n";

  return return_code; // Return 0 on success, 1 on any failure
}

//=============================================================================
// Process one TU.

// Acquire the input as an llvm::MemoryBuffer (from file or stdin) and open
// the appropriate output stream (stdout or temporary file).
//
// If a file was opened (use_cin is false) and `in_place` is false, move
// the temporary file over the original (path) upon successful conversion.
//
// Calls process_input_buffer for the actual processing.
void process_filename(Parser& parser, RandomNumber& rn, std::filesystem::path const& filename, bool use_cin)
{
  // --- 1. Acquire Input Buffer ---
  std::unique_ptr<llvm::MemoryBuffer> input_buffer;
  std::string input_filename_str = use_cin ? "<stdin>" : filename.native();
  std::string stdin_content_holder; // Must outlive input_buffer if getMemBuffer is used

  if (use_cin)
  {
    // Read all of stdin into a string first.
    stdin_content_holder.assign((std::istreambuf_iterator<char>(std::cin)), (std::istreambuf_iterator<char>()));

    if (std::cin.bad())
    {
      throw std::runtime_error("Failed reading from stdin");
    }

    // Create a MemoryBuffer *referencing* the string's data. No copy.
    // 'stdin_content_holder' MUST outlive the use of 'input_buffer'.
    input_buffer = llvm::MemoryBuffer::getMemBuffer(llvm::StringRef(stdin_content_holder), input_filename_str, /*RequiresNullTerminator=*/true);

    if (!input_buffer)
    {
      throw std::runtime_error("Could not create MemoryBuffer for stdin content");
      // (This is unlikely unless maybe out of memory for the buffer object itself)
    }
  }
  else
  {
    // Use MemoryBuffer::getFile to memory-map or read the file.
    auto buffer_or_err = llvm::MemoryBuffer::getFile(filename.native(), -1, /*RequiresNullTerminator=*/true);
    if (!buffer_or_err)
    {
      // Propagate LLVM error as a runtime_error exception
      throw std::runtime_error("Failed to open '" + filename.native() + "': " + buffer_or_err.getError().message());
    }
    // Move the buffer out of the Expected object
    input_buffer = std::move(*buffer_or_err);
  }

  // --- 2. Setup Output Stream ---
  std::ostream* output_stream_ptr = &std::cout; // Default to stdout
  std::ofstream temp_ofile;                     // Store ofstream here if used
  std::filesystem::path temp_filename;          // Store temp filename path here if used
  bool writing_to_temp_file = !use_cin && in_place;

  if (writing_to_temp_file)
  {
    std::filesystem::path temp_filename_prefix = filename;
    temp_filename_prefix += ".cwformat-tmp"; // Use a slightly different temp prefix
    temp_filename = get_temp_filename(rn, temp_filename_prefix);

    // Note: ofstream constructor throws on failure if exceptions(badbit | failbit) is set.
    // Default is goodbit, so check is_open() after construction.
    temp_ofile.open(temp_filename, std::ios::binary | std::ios::trunc);
    if (!temp_ofile.is_open())
    {
      // Get error details if possible (less reliable without exceptions enabled)
      std::error_code ec(errno, std::system_category());
      throw std::runtime_error("Failed to create temporary file '" + temp_filename.native() + "': " + ec.message());
    }
    output_stream_ptr = &temp_ofile; // Point to the file stream
  }

  // --- 3. Process the Buffer ---
  // output_stream_ptr is either &std::cout or &temp_ofile
  // input_buffer holds the data (ownership will be passed)
  // Use a try-finally like structure for cleanup (RAII with ofstream helps)

  bool success = false;
  try
  {
    parser.process_input_buffer(input_filename_str, std::move(input_buffer), *output_stream_ptr);

    // Flush the output stream to ensure data is written and check for errors
    output_stream_ptr->flush();
    if (!output_stream_ptr->good())
    {
      throw std::runtime_error("Failed writing to output stream for " + input_filename_str);
    }

    // If we wrote to a temporary file, close it before renaming
    if (temp_ofile.is_open())
    {
      temp_ofile.close();
      // Check state *after* closing
      if (!temp_ofile.good())
      {
        throw std::runtime_error("Failed writing or closing temporary file '" + temp_filename.native() + "'");
      }
    }
    success = true; // Mark success only if all steps complete without exceptions
  }
  catch (...)
  {
    // Cleanup on error: If we created a temp file, remove it.
    if (temp_ofile.is_open())
    {
      temp_ofile.close(); // Close first (might fail, ignore failure here)
    }
    if (writing_to_temp_file && !temp_filename.empty() && std::filesystem::exists(temp_filename))
    {
      std::error_code ignored_ec;
      std::filesystem::remove(temp_filename, ignored_ec); // Ignore remove errors during cleanup
    }
    throw;                                                // Re-throw the original exception
  }

  // --- 4. Finalize (Rename if necessary) ---
  if (success && writing_to_temp_file)
  {
    // Rename temporary file to original file
    std::error_code ec;
    std::filesystem::rename(temp_filename, filename, ec);
    if (ec)
    {
      // If rename failed, try to remove the temp file and report error
      std::error_code remove_ec;
      std::filesystem::remove(temp_filename, remove_ec); // Attempt cleanup
      throw std::runtime_error(
        "Failed to rename temporary file '" + temp_filename.native() + "' to '" + filename.native() + "': " + ec.message());
    }
    // Success! Temporary file has been renamed.
  }
  // If success and not writing_to_temp_file, output went to stdout, nothing more to do.

  // input_buffer went out of scope or was moved into process_input_buffer.
  // temp_ofile goes out of scope, closing file if not already closed.
  // stdin_content_holder goes out of scope.
}
