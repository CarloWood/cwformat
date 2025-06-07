#include "sys.h"

#include "SourceFile.h"
#include "TranslationUnit.h"
#include "utils/AIAlert.h"

#include <cerrno>
#include <filesystem>
#include <iostream>
#include <chrono>
#include <random>
#include <system_error>

#include "debug.h"

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
#include <utility>

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

cl::list<std::string> include_directories("I",
    cl::desc("Add the directory <dir> to the list of directories to be searched for header files during preprocessing."),
    cl::value_desc("dir"), cl::cat(cwformat_category));

cl::list<std::string> commandline_macros_define("D",
    cl::desc("Define a macro using -D<name> or -D<name>=<value> during preprocessing."),
    cl::value_desc("name[=value]"),
    cl::ValueRequired,          // Require a value after -D.
    cl::Prefix,                 // Allow the value to be attached to the option (e.g., -DFOO).
    cl::cat(cwformat_category));

cl::list<std::string> commandline_macros_undef("U",
    cl::desc("Undefine a macro using -U<name> during preprocessing."),
    cl::value_desc("name"),
    cl::ValueRequired,          // Require a value after -U.
    cl::Prefix,                 // Allow the value to be attached to the option (e.g., -UFOO).
    cl::cat(cwformat_category));

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
void process_filename(ClangFrontend& clang_frontend, RandomNumber& rn, std::filesystem::path const& filename, bool use_cin);

llvm::raw_ostream& operator<<(llvm::raw_ostream& os, AIAlert::Error const& error)
{
  int lines = 0;
  for (auto& line : error.lines())
    if (!line.is_prefix())
      ++lines;
  char const* indent_str = "\n    ";
  if (lines > 1)
    os << indent_str;
  unsigned int suppress_mask = 0;
  for (auto& line : error.lines())
  {
    if (line.suppressed(suppress_mask))
      continue;
    if (lines > 1 && line.prepend_newline())   // Empty line.
      os << indent_str;
    if (line.is_prefix())
    {
      os << line.getXmlDesc();
      if (line.is_function_name() || line.is_filename_line())
        os << ": ";
    }
    else
      os << translate::getString(line.getXmlDesc(), line.args());
  }
  return os;
}

int main(int argc, char* argv[])
{
  Debug(NAMESPACE_DEBUG::init());

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

  auto configure_header_search_options = [](HeaderSearchOptions& header_search_options){
    for (std::string const& dir : include_directories)
    {
      Dout(dc::notice, "Adding include directory \"" << dir << "\".");
      header_search_options.AddPath(dir, clang::frontend::Angled, false, false);
    }
  };

  auto configure_commandline_macro_definitions = [](clang::PreprocessorOptions& preprocessor_options){
    using position_to_type_map_type = std::map<unsigned, std::pair<char, std::string>>;
    position_to_type_map_type position_to_type_map;
    for (unsigned i = 0; i < commandline_macros_define.getNumOccurrences(); ++i)
      position_to_type_map.emplace(commandline_macros_define.getPosition(i), position_to_type_map_type::mapped_type{'D', commandline_macros_define[i]});
    for (unsigned i = 0; i < commandline_macros_undef.getNumOccurrences(); ++i)
      position_to_type_map.emplace(commandline_macros_undef.getPosition(i), position_to_type_map_type::mapped_type{'U', commandline_macros_undef[i]});
    for (auto&& p : position_to_type_map)
    {
      if (p.second.first == 'D')
      {
        Dout(dc::notice, "Adding macro definition \"" << p.second.second << "\"");
        preprocessor_options.addMacroDef(p.second.second);
      }
      else
      {
        Dout(dc::notice, "Undefining macro \"" << p.second.second << "\"");
        preprocessor_options.addMacroUndef(p.second.second);
      }
    }
  };

  // Create a ClangFrontend instance.
  ClangFrontend clang_frontend(configure_header_search_options, configure_commandline_macro_definitions);
  // Needed for temporary file name generation.
  RandomNumber rn;

  // Process the combined list.
  int return_code = 0; // Track if any file processing failed
  for (const auto& item : work_items)
  {
    try
    {
      process_filename(clang_frontend, rn, item.first, item.second);
    }
    catch (AIAlert::Error const& error)
    {
      llvm::errs() << program_name << ": Error processing '" << (item.second ? "<stdin>" : item.first.native()) << "': " << error << "\n";
      return_code = 1; // Mark failure
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
void process_filename(ClangFrontend& clang_frontend, RandomNumber& rn, std::filesystem::path const& filename, bool use_cin)
{
  std::string input_filename_str = use_cin ? "<stdin>" : filename.native();
  std::string stdin_content_holder; // Must outlive input_buffer if getMemBuffer is used.

  std::filesystem::path full_path;
  if (!use_cin)
    full_path = std::filesystem::absolute(filename);

  // --- 1. Acquire Input Buffer ---
  std::unique_ptr<llvm::MemoryBuffer> input_buffer;

  if (use_cin)
  {
    // Read all of stdin into a string first.
    stdin_content_holder.assign((std::istreambuf_iterator<char>(std::cin)), (std::istreambuf_iterator<char>()));

    if (std::cin.bad())
      THROW_LALERT("Error reading from std::cin");

    // Create a MemoryBuffer *referencing* the string's data. No copy.
    // 'stdin_content_holder' MUST outlive the use of 'input_buffer'.
    input_buffer = llvm::MemoryBuffer::getMemBuffer(llvm::StringRef(stdin_content_holder), input_filename_str, /*RequiresNullTerminator=*/true);
  }
  else
  {
    // Use MemoryBuffer::getFile to memory-map or read the file.
    auto buffer_or_err = llvm::MemoryBuffer::getFile(filename.native(), -1, /*RequiresNullTerminator=*/true);
    if (!buffer_or_err)
      THROW_LALERTC(buffer_or_err.getError(), "Failed to open '[FILENAME]'", AIArgs("[FILENAME]", filename.native()));

    // Move the buffer out of the Expected object.
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
    temp_filename_prefix += ".cwformat-tmp";
    temp_filename = get_temp_filename(rn, temp_filename_prefix);

    temp_ofile.open(temp_filename, std::ios::binary | std::ios::trunc);
    if (!temp_ofile.is_open())
      THROW_LALERTE("Failed to create temporary file '[FILENAME]'", AIArgs("[FILENAME]", temp_filename.native()));
    output_stream_ptr = &temp_ofile; // Point to the file stream
  }

  // Create a SourceFile object from the input buffer.
  SourceFile const source_file(input_filename_str, full_path, std::move(input_buffer));
  // Create a TranslationUnit object to hold the result.
  TranslationUnit translation_unit(clang_frontend, source_file COMMA_CWDEBUG_ONLY(input_filename_str));

  // --- 3. Process the SourceFile ---
  // output_stream_ptr is either &std::cout or &temp_ofile.
  // source_file holds the data.
  // Use a try-finally like structure for cleanup (RAII with ofstream helps).

  bool success = false;
  try
  {
    // Read the source_file into translation_unit.
    translation_unit.process(source_file);
    // Write the result to the output stream.
    translation_unit.print(*output_stream_ptr);

    // Flush the output stream to ensure data is written and check for errors
    output_stream_ptr->flush();
    if (!output_stream_ptr->good())
      THROW_LALERT("Failed writing to output stream for '[FILENAME]'", AIArgs("[FILENAME]", input_filename_str));

    // If we wrote to a temporary file, close it before renaming
    if (temp_ofile.is_open())
    {
      temp_ofile.close();
      // Check state *after* closing.
      if (!temp_ofile.good())
      {
        std::error_code ignored_ec;
        std::filesystem::remove(temp_filename, ignored_ec);
        THROW_LALERT("Failed closing temporary file '[FILENAME]'", AIArgs("[FILENAME]", temp_filename.native()));
      }
    }
    success = true; // Mark success only if all steps complete without exceptions.
  }
  catch (...)
  {
    // Cleanup on error: If we created a temp file, remove it.
    if (temp_ofile.is_open())
      temp_ofile.close(); // Close first (might fail, ignore failure here)
    if (writing_to_temp_file && !temp_filename.empty() && std::filesystem::exists(temp_filename))
    {
      std::error_code ignored_ec;
      std::filesystem::remove(temp_filename, ignored_ec); // Ignore remove errors during cleanup.
    }
    throw;                                                // Re-throw the original exception.
  }

  // --- 4. Finalize (Rename if necessary) ---
  if (success && writing_to_temp_file)
  {
#if -0
    // Rename temporary file to original file
    std::error_code ec;
    std::filesystem::rename(temp_filename, filename, ec);
    if (ec)
    {
      std::error_code ignored_ec;
      std::filesystem::remove(temp_filename, ignored_ec);
      THROW_LALERTC(ec, "Failed to rename temporary file '[FILENAME]' to '[NEWFILENAME]'", AIArgs("[FILENAME]", temp_filename.native())("[NEWFILENAME]", filename.native()));
    }
    // Success! Temporary file has been renamed.
#else
    Dout(dc::warning, "Not renaming temporary file '" << temp_filename.native() << "' to '" << filename.native() << "'; writing to disk isn't implemented yet.");
#endif
  }
  // If success and not writing_to_temp_file, output went to stdout, nothing more to do.

  // input_buffer went out of scope or was moved into process_input_buffer.
  // temp_ofile goes out of scope, closing file if not already closed.
  // stdin_content_holder goes out of scope.
}
