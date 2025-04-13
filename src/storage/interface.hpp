#ifndef STORAGE_INTERFACE_HPP
#define STORAGE_INTERFACE_HPP

#include <string_view>

/**
 * This file defines an interface for a storage backend.
 *
 * So far, the only implementation is a filesystem backend, but the interface
 * should allow other implementations, such as a database or a cloud storage.
 */

/** An item in a folder, either a file, or a subdirectory */
struct folder_entry
{
  /** The name of the file or folder */
  std::string name;
  /**
   * If this entry is a file, this is a null option. Else the vector contains
   * the entries in the folder
   */
  std::optional<std::vector<folder_entry>> children;
};

/**
 * An interface (abstract class) for a temporary folder where the image
 * processing results can be stored and later atomically committed to
 * the storage backend.
 * 
 * The image processing code never attempts to create a file or folder
 * with the same name twice, so you do not need to handle safety of
 * such situations. However, your API should be designed to allow
 * creation of entries in different paths simultaneously from different
 * threads.
 * 
 * **Important**: You should implement a destructor that cleans up the
 * temporary folder, unless this folder was committed to the backend
 * (in which case the folder was probably moved and no cleanup is necessary).
 */
class staged_folder
{
public:
  virtual ~staged_folder() = default;

  /**
   * 
   * 
   * Note about the arguments: while using a vector here would be cleaner,
   * libvips returns a pointer+size, which is practically impossible to
   * convert to a vector without copying.
   * See https://stackoverflow.com/a/15203325/7292139
   */
  virtual void create_file(std::string_view name,
                           std::uint8_t const* data,
                           size_t size) = 0;

  virtual void create_folder(std::string_view name) = 0;
};

/**
 * This is an abstract class which defines the interface for a storage backend.
 * You need to implement all the methods in a derived class. See docstrings
 * for the individual methods for more information.
 */
class storage_backend
{
public:
  virtual ~storage_backend() = default;

  /**
   * Set configuration value for the storage backend. If the entry is invalid,
   * this should throw an exception.
   * 
   * Example: if the configuration file contains the following:
   * storage.backend=aws_s3
   * storage.api_key=abcdefg123456789
   * then the config parser will instantiate an AWS S3 backend and call
   * set_config("api_key", "abcdefg123456789")
   */
  virtual void set_config(std::string_view key, std::string_view value) = 0;

  /**
   * Validate the current loaded configuration. If this returns without throwing
   * an exception, your backend must now be ready to use.
   */
  virtual void validate() const = 0;

  /**
   * Initialize the storage backend. This is called after the configuration is
   * loaded and validated.
   */
  virtual void init() = 0;

  /**
   * Return a recursive listing (a tree) of all the files and folders in a folder.
   * 
   * This does not need to be thread-safe: it will be called on data already committed
   * to the storage, which shouldn't be modified while the server is running.
   * 
   * If the folder does not exist, this should return std::nullopt.
   */
  virtual std::optional<std::vector<folder_entry>> walk_folder(
    std::string_view path) const = 0;

  /**
   * Create a new temporary folder in the backend. See documentation of the staged_folder
   * class for more information.
   * 
   * Obviously, your implementation can return a unique_ptr to a class derived from
   * staged_folder, which implements the methods in a way that is appropriate for your
   * backend.
   * 
   * The name passed to this function is the filesystem-safe name that the folder should
   * have after commit.
   */
  virtual std::unique_ptr<staged_folder> create_staged_folder(
    std::string_view name) = 0;

  /**
   * Commit the staged folder to the backend. This will be called when all image
   * processing results are staged into the folder, and the folder should become
   * available in the public storage. This should act atomically - e.g. for the
   * filesystem backend, this atomically moves the folder to the final location,
   * so either all files become immediately available, or none of them do (if the
   * operation fails for some reason).
   * 
   * This will be called with an object returned by create_staged_folder, so you
   * can dynamic_cast it to your subclass, and throw an exception if the cast fails.
   */
  virtual void commit_staged_folder(staged_folder& folder) = 0;
};

#endif // STORAGE_INTERFACE_HPP
