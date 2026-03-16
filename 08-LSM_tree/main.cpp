#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <algorithm>
#include <sstream>
#include <iomanip>


namespace fs = std::filesystem;

static constexpr size_t MEMTABLE_FLUSH_THRESHOLD = 4;
static const std::string TOMBSTONE = "\x00TOMBSTONE\x00";

struct SSTableEntry {
    std::string key;
    std::string value;  // equals TOMBSTONE sentinel if deleted
};

class LSMTree {
public:
    LSMTree(const std::string& dataDir) : dataDir_(dataDir), sstableCount_(0) {
        // creates dir if it does not exist
        if(fs::create_directories(dataDir_)) {
            return; // no need to iterate directory if it was just created
        }

        // collect all entries (unspecified order)
        for(const auto& dir : fs::directory_iterator(dataDir_)) {
            std::ostringstream oss;
            oss << dataDir_ << "/" << dir.path().filename().string();
            sstablePaths_.push_back(oss.str());
            sstableCount_++;
        }
        std::sort(sstablePaths_.begin(), sstablePaths_.end(), std::greater<>()); // newest first 
    }

    void put(const std::string& key, const std::string& value) {
        memtable_[key] = value;
        maybeFlush();
    }

    void del(const std::string& key) {
        put(key, TOMBSTONE);
    }

    std::optional<std::string> get(const std::string& key) {

        // find in memory table first
        auto it = memtable_.find(key);
        if(it != memtable_.end()) {
            if (it->second == TOMBSTONE) return std::nullopt;
            return it->second;
        }

        // find in sstables 
        for(const auto& path : sstablePaths_) {
            std::vector<SSTableEntry> table = readSSTable(path);
            for(const auto& entry : table) {
                if(key == entry.key) {
                    if (entry.value == TOMBSTONE) return std::nullopt;
                    return entry.value;
                }
            }
        }
        
        // could not find any
        return std::nullopt;
    }

private:
    void maybeFlush() {
        if (memtable_.size() >= MEMTABLE_FLUSH_THRESHOLD) {
            flush();
        }
    }

    void flush() {
        // e.g., "lsm_data/sstable_0000.sst"
        std::ostringstream oss;
        oss << dataDir_ << "/sstable_" << std::setw(4) << std::setfill('0') << sstableCount_ << ".sst";

        writeSSTable(memtable_, oss.str());
        memtable_.clear();
    }

    void writeSSTable(const std::map<std::string, std::string>& data,
                      const std::string& path) {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) return;
        
        // [key_len: uint32_t][key bytes][val_len: uint32_t][val bytes]
        for(const auto& [key, val] : data) {
            uint32_t key_len = static_cast<uint32_t>(key.size());
            file.write(reinterpret_cast<const char*>(&key_len), sizeof(key_len));
            file.write(key.data(), static_cast<std::streamsize>(key.size()));

            uint32_t val_len = static_cast<uint32_t>(val.size());
            file.write(reinterpret_cast<const char*>(&val_len), sizeof(val_len));
            file.write(val.data(), static_cast<std::streamsize>(val.size()));
        }

        sstablePaths_.push_back(path);
        std::sort(sstablePaths_.begin(), sstablePaths_.end(), std::greater<>()); // newest first 
        sstableCount_++;
    }

    std::vector<SSTableEntry> readSSTable(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) return {};

        std::vector<SSTableEntry> entries;
        while(file) {
            uint32_t key_len = 0;
            file.read(reinterpret_cast<char*>(&key_len), sizeof(key_len));
            if (!file) break;
            std::string key(key_len, '\0');
            file.read(key.data(), key_len);

            uint32_t val_len = 0;
            file.read(reinterpret_cast<char*>(&val_len), sizeof(val_len));
            if (!file) break;
            std::string val(val_len, '\0');
            file.read(val.data(), val_len);

            SSTableEntry entry;
            entry.key = key;
            entry.value = val;

            entries.push_back(entry);
        }
        return entries;
    }

    std::map<std::string, std::string> memtable_;
    std::string dataDir_;
    int sstableCount_;
    std::vector<std::string> sstablePaths_;  // oldest at [0], newest at back
};

int main() {
    LSMTree lsm("./lsm_data");

    lsm.put("apple",  "fruit");
    lsm.put("banana", "yellow");
    lsm.put("cherry", "red");
    lsm.put("date",   "sweet");        // triggers flush #1

    lsm.put("elderberry", "dark");
    lsm.put("fig",        "chewy");
    lsm.put("grape",      "vine");
    lsm.put("apple",      "v2");       // overwrites apple; triggers flush #2

    lsm.del("banana");                 // tombstone in memtable

    // Expected output:
    std::cout << "apple: "      << lsm.get("apple").value_or("NOT FOUND")      << "\n"; // v2
    std::cout << "banana: "     << lsm.get("banana").value_or("NOT FOUND")     << "\n"; // NOT FOUND
    std::cout << "cherry: "     << lsm.get("cherry").value_or("NOT FOUND")     << "\n"; // red
    std::cout << "date: "       << lsm.get("date").value_or("NOT FOUND")       << "\n"; // sweet
    std::cout << "elderberry: " << lsm.get("elderberry").value_or("NOT FOUND") << "\n"; // dark
    std::cout << "missing: "    << lsm.get("missing").value_or("NOT FOUND")    << "\n"; // NOT FOUND
}
