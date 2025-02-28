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

#include <filesystem>

#include <silkworm/node/bittorrent/settings.hpp>
#include <silkworm/node/snapshot/path.hpp>

namespace silkworm {

struct SnapshotSettings {
    std::filesystem::path repository_dir{kDefaultSnapshotDir};  // Path to the snapshot repository on disk
    bool enabled{true};                                         // Flag indicating if snapshots are enabled
    bool no_downloader{false};                                  // Flag indicating if snapshots download is disabled
    bool verify_on_startup{true};                               // Flag indicating if snapshots will be verified on startup
    uint64_t segment_size{kDefaultSegmentSize};                 // The segment size measured as number of blocks
    BitTorrentSettings bittorrent_settings;                     // The Bittorrent protocol settings
};

}  // namespace silkworm
