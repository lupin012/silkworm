/*
   Copyright 2022 The Silkworm Authors

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#include <mdbx.h++>
#pragma GCC diagnostic pop

#include <absl/functional/function_ref.h>

#include <silkworm/core/common/base.hpp>
#include <silkworm/core/common/object_pool.hpp>
#include <silkworm/core/common/util.hpp>
#include <silkworm/node/db/util.hpp>

namespace silkworm::db {

inline constexpr std::string_view kDbDataFileName{"mdbx.dat"};

inline constexpr size_t kMdbxMaxPages{2147483648ull};

namespace detail {
    struct cursor_handle_deleter {  // default deleter for pooled cursors
        constexpr cursor_handle_deleter() noexcept = default;
        void operator()(MDBX_cursor* ptr) const noexcept { mdbx_cursor_close(ptr); }
    };
}  // namespace detail

//! \brief This class wraps a read-only transaction.
//! It is used in function signatures to clarify that read-only access is sufficient, read-write access is not required.
class ROTxn {
  public:
    explicit ROTxn(mdbx::env& env) : managed_txn_{env.start_read()} {}

    // Not copyable
    ROTxn(const ROTxn&) = delete;
    ROTxn& operator=(const ROTxn&) = delete;

    // Only movable
    ROTxn(ROTxn&& source) noexcept : managed_txn_(std::move(source.managed_txn_)) {}

    // Access to the underling raw mdbx transaction
    mdbx::txn& operator*() { return managed_txn_; }
    mdbx::txn* operator->() { return &managed_txn_; }
    operator mdbx::txn&() { return managed_txn_; }

    void abort() { managed_txn_.abort(); }

  protected:
    ROTxn(mdbx::txn_managed&& source) : managed_txn_{std::move(source)} {}

    mdbx::txn_managed managed_txn_;
};

//! \brief This class wraps a read-write transaction.
//! It is used in function signatures to clarify that read-write access is required.
//! It supports explicit disable/enable of commit capabilities.
//! Disabling commit is useful for running several stages on a handful of blocks atomically.
class RWTxn : public ROTxn {
  public:
    // This variant creates new mdbx transactions as need be.
    explicit RWTxn(mdbx::env& env) : ROTxn{env.start_write()} {}

    // Not copyable
    RWTxn(const RWTxn&) = delete;
    RWTxn& operator=(const RWTxn&) = delete;

    // Only movable
    RWTxn(RWTxn&& source) noexcept : ROTxn(std::move(source)), commit_disabled_{source.commit_disabled_} {}

    void disable_commit() { commit_disabled_ = true; }
    void enable_commit() { commit_disabled_ = false; }

    void commit(const bool renew = true) {
        /*
         * renew is required here due to RAII
         * RWTxn txn(env);
         * txn.commit();
         * env.close();
         * causes a segfault for tx being aborted when the env is already closed
         *
         * Workarounds
         * - either pass renew==false to last commit
         * - or keep RWTxn in a lower scope
         * */

        if (!commit_disabled_) {
            mdbx::env env = managed_txn_.env();
            managed_txn_.commit();
            if (renew) {
                managed_txn_ = env.start_write();  // renew transaction
            }
        }
    }
    void commit_and_renew() { commit(true); }
    void commit_and_stop() { commit(false); }

  protected:
    RWTxn(mdbx::txn_managed&& source) : ROTxn{std::move(source)} {}

    bool commit_disabled_{false};
};

//! \brief This class create ROTxn(s) on demand, it is used to enforce in some method signatures the type of db access
class ROAccess {
  public:
    explicit ROAccess(mdbx::env& env) : env_{env} {}
    ROAccess(const ROAccess& copy) = default;

    ROTxn start_ro_tx() { return ROTxn(env_); }

    mdbx::env& operator*() { return env_; }

  protected:
    mdbx::env& env_;
};

//! \brief This class create RWTxn(s) on demand, it is used to enforce in some method signatures the type of db access
class RWAccess : public ROAccess {
  public:
    explicit RWAccess(mdbx::env& env) : ROAccess{env} {}
    RWAccess(const RWAccess& copy) = default;

    RWTxn start_rw_tx() { return RWTxn(env_); }
};

//! \brief Reference to a processing function invoked by cursor_for_each & cursor_for_count on each record
using WalkFuncRef = absl::FunctionRef<void(ByteView key, ByteView value)>;

//! \brief Essential environment settings
struct EnvConfig {
    std::string path{};
    bool create{false};          // Whether db file must be created
    bool readonly{false};        // Whether db should be opened in RO mode
    bool exclusive{false};       // Whether this process has exclusive access
    bool in_memory{false};       // Whether this db is in memory
    bool shared{false};          // Whether this process opens a db already opened by another process
    bool read_ahead{false};      // Whether to enable mdbx read ahead
    bool write_map{false};       // Whether to enable mdbx write map
    size_t page_size{4_Kibi};    // Mdbx page size
    size_t max_size{3_Tebi};     // Mdbx max map size
    size_t growth_size{2_Gibi};  // Increment size for each extension
    uint32_t max_tables{128};    // Default max number of named tables
    uint32_t max_readers{100};   // Default max number of readers
};

//! \brief Configuration settings for a "map" (aka a table)
struct MapConfig {
    const char* name{nullptr};                                        // Name of the table (is key in MAIN_DBI)
    const ::mdbx::key_mode key_mode{::mdbx::key_mode::usual};         // Key collation order
    const ::mdbx::value_mode value_mode{::mdbx::value_mode::single};  // Data Storage Mode
};

//! \brief Opens an mdbx environment using the provided environment config
//! \param [in] config : A structure containing essential environment settings
//! \return A handler to mdbx::env_managed class
//! \remarks May throw exceptions
::mdbx::env_managed open_env(const EnvConfig& config);

//! \brief Opens an mdbx "map" (aka table)
//! \param [in] tx : a reference to a valid mdbx transaction
//! \param [in] config : the configuration settings for the map
//! \return A handle to the opened map
::mdbx::map_handle open_map(::mdbx::txn& tx, const MapConfig& config);

//! \brief Opens a cursor to an mdbx "map" (aka table)
//! \param [in] tx : a reference to a valid mdbx transaction
//! \param [in] config : the configuration settings for the underlying map
//! \return A handle to the opened cursor
::mdbx::cursor_managed open_cursor(::mdbx::txn& tx, const MapConfig& config);

//! \brief Computes the max size of value data to fit in a leaf data page
//! \param [in] page_size : the actually configured MDBX's page size
//! \param [in] key_size : the known key size to fit in bundle computed value size
size_t max_value_size_for_leaf_page(size_t page_size, size_t key_size);

//! \brief Computes the max size of value data to fit in a leaf data page
//! \param [in] txn : the transaction used to derive pagesize from
//! \param [in] key_size : the known key size to fit in bundle computed value size
size_t max_value_size_for_leaf_page(const ::mdbx::txn& txn, size_t key_size);

//! \brief Managed cursor class to access cursor API
//! \remarks Unlike ::mdbx::cursor_managed this class withdraws and deposits allocated MDBX_cursor handles in a
//! thread_local pool for reuse. This helps avoiding multiple mallocs on cursor creation.
class PooledCursor : public ::mdbx::cursor {
  public:
    explicit PooledCursor(::mdbx::txn& txn, const MapConfig& config);
    explicit PooledCursor(RWTxn& txn, const MapConfig& config) : PooledCursor(*txn, config){};
    ~PooledCursor();

    PooledCursor(PooledCursor&& other) noexcept;
    PooledCursor& operator=(PooledCursor&& other) noexcept;

    PooledCursor(const PooledCursor&) = delete;
    PooledCursor& operator=(const PooledCursor&) = delete;

    //! \brief (re)uses current cursor binding it to provided transaction and map
    void bind(::mdbx::txn& tx, const MapConfig& config);

    //! \brief (re)uses current cursor binding it to provided transaction and map
    void bind(RWTxn& txn, const MapConfig& config) { bind(*txn, config); }

    //! \brief Closes cursor causing de-allocation of MDBX_cursor handle
    //! \remarks After this call the cursor is not reusable and the handle does not return to the cache
    void close();

    //! \brief Returns stat info of underlying dbi
    [[nodiscard]] MDBX_stat get_map_stat() const;

    //! \brief Returns flags of underlying dbi
    [[nodiscard]] MDBX_db_flags_t get_map_flags() const;

    //! \brief Flag indicating if table is single-value or multi-value
    [[nodiscard]] bool is_multi_value() const;

    //! \brief Flag indicating if cursor has been positioned or not
    [[nodiscard]] bool is_dangling() const;

    //! \brief Returns the size of the underlying table
    [[nodiscard]] size_t size() const;

    //! \brief Returns whether the underlying table is empty
    [[nodiscard]] bool empty() const;

    //! \brief Exposes handles cache
    static const ObjectPool<MDBX_cursor, detail::cursor_handle_deleter>& handles_cache() { return handles_pool_; }

  private:
    static thread_local ObjectPool<MDBX_cursor, detail::cursor_handle_deleter> handles_pool_;
};

//! \brief Checks whether a provided map name exists in database
//! \param [in] tx : a reference to a valid mdbx transaction
//! \param [in] map_name : the name of the map to check for
//! \return True / False
bool has_map(::mdbx::txn& tx, const char* map_name);

//! \brief Builds the full path to mdbx datafile provided a directory
//! \param [in] base_path : a reference to the directory holding the data file
//! \return A path with file name
static inline std::filesystem::path get_datafile_path(const std::filesystem::path& base_path) noexcept {
    return std::filesystem::path(base_path / std::filesystem::path(kDbDataFileName));
}

//! \brief Defines the direction of cursor while looping by cursor_for_each or cursor_for_count
enum class CursorMoveDirection {
    Forward,
    Reverse
};

//! \brief Executes a function on each record reachable by the provided cursor
//! \param [in] cursor : A reference to a cursor opened on a map
//! \param [in] func : A reference to a function with the code to execute on records. Note the return value of the
//! function may stop the loop
//! \param [in] direction : Whether the cursor should navigate records forward (default) or backwards
//! \return The overall number of processed records
//! \remarks If the provided cursor is *not* positioned on any record it will be moved to either the beginning or the
//! end of the table on behalf of the move criteria
size_t cursor_for_each(::mdbx::cursor& cursor, WalkFuncRef func,
                       CursorMoveDirection direction = CursorMoveDirection::Forward);

//! \brief Executes a function on each record reachable by the provided cursor asserting keys start with provided prefix
//! \param [in] cursor : A reference to a cursor opened on a map
//! \param [in] prefix : The prefix each key must start with
//! \param [in] func : A reference to a function with the code to execute on records. Note the return value of the
//! function may stop the loop
//! \param [in] direction : Whether the cursor should navigate records forward (default) or backwards
//! \return The overall number of processed records
size_t cursor_for_prefix(::mdbx::cursor& cursor, ByteView prefix, WalkFuncRef func,
                         CursorMoveDirection direction = CursorMoveDirection::Forward);

//! \brief Executes a function on each record reachable by the provided cursor up to a max number of iterations
//! \param [in] cursor : A reference to a cursor opened on a map
//! \param [in] func : A reference to a function with the code to execute on records. Note the return value of the
//! function may stop the loop
//! \param [in] max_count : Max number of iterations
//! \param [in] direction : Whether the cursor should navigate records forward (default) or backwards
//! \return The overall number of processed records. Should it not match the value of max_count it means the cursor has
//! reached either the end or the beginning of table earlier
//! \remarks If the provided cursor is *not* positioned on any record it will be moved to either the beginning or the
//! end of the table on behalf of the move criteria
size_t cursor_for_count(::mdbx::cursor& cursor, WalkFuncRef func, size_t max_count,
                        CursorMoveDirection direction = CursorMoveDirection::Forward);

//! \brief Erases map records by cursor until any record is found
//! \param [in] cursor : A reference to a cursor opened on a map
//! \param [in] set_key : The key where to set the cursor to.
//! \param [in] direction : Whether the cursor should navigate records forward (default) or backwards.
//! \return The overall number of erased records
//! \remarks When direction is forward all keys greater equal set_key will be deleted. When direction is reverse all
//! keys lower than set_key will be deleted.
size_t cursor_erase(::mdbx::cursor& cursor, ByteView set_key,
                    CursorMoveDirection direction = CursorMoveDirection::Forward);

//! \brief Erases all records whose key starts with a prefix
//! \param [in] cursor : A reference to a cursor opened on a map
//! \param [in] prefix : Delete keys starting with this prefix
size_t cursor_erase_prefix(::mdbx::cursor& cursor, ByteView prefix);

}  // namespace silkworm::db
