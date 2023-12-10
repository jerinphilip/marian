#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "3rd_party/cnpy/cnpy.h"

using Scalar = float;

namespace cnpy {
// same as npz_save() except that it saves multiple items to .npz file in a single go, which is
// required when writing to HDFS
static inline void npz_save_from_items(std::string zipname, const npz_t &items) {
  auto tmpname = zipname + "$$";  // TODO: add thread id or something
  unlink(tmpname.c_str());        // when saving to HDFS, we cannot overwrite an existing file
  FILE *fp = fopen(tmpname.c_str(), "wb");
  if(!fp)
    throw std::runtime_error("npz_save: error opening file for writing: " + tmpname);

  std::vector<char> global_header;
  std::vector<char> local_header;
  for(const auto &kv : items) {
    NpyArray *array = kv.second.get();
    NpzItem &item = *reinterpret_cast<NpzItem *>(array);
    // auto fname = item.name;
    auto fname = kv.first;
    // first, form a "file name" by appending .npy to the item's name
    fname += ".npy";

    const auto *data = item.bytes.data();
    const auto *shape = item.shape.data();
    const auto type = item.type;
    const auto word_size = item.word_size;
    const unsigned int ndims = (unsigned int)item.shape.size();
    std::vector<char> npy_header = create_npy_header(type, word_size, shape, ndims);

    unsigned long nels = 1;
    for(size_t m = 0; m < ndims; m++)
      nels *= shape[m];
    auto nbytes = nels * word_size + npy_header.size();

    // get the CRC of the data to be added
    unsigned int crc = crc32(0L, (unsigned char *)&npy_header[0], (uInt)npy_header.size());
    crc = crc32(crc, (unsigned char *)data, nels * word_size);

    // build the local header
    local_header.clear();
    local_header += "PK";                          // first part of sig
    local_header += (unsigned short)0x0403;        // second part of sig
    local_header += (unsigned short)20;            // min version to extract
    local_header += (unsigned short)0;             // general purpose bit flag
    local_header += (unsigned short)0;             // compression method
    local_header += (unsigned short)0;             // file last mod time
    local_header += (unsigned short)0;             // file last mod date
    local_header += (unsigned int)crc;             // crc
    local_header += (unsigned int)nbytes;          // compressed size
    local_header += (unsigned int)nbytes;          // uncompressed size
    local_header += (unsigned short)fname.size();  // fname length
    local_header += (unsigned short)0;             // extra field length
    local_header += fname;

    // write everything
    unsigned int local_header_offset
        = ftell(fp);  // this is where this local item will begin in the file. Tis gets stored in
                      // the corresponding global header.
    fwrite(&local_header[0], sizeof(char), local_header.size(), fp);
    fwrite(&npy_header[0], sizeof(char), npy_header.size(), fp);
    fwrite(data, word_size, nels, fp);

    // append to global header
    // A concatenation of global headers for all objects gets written to the end of the file.
    global_header += "PK";                    // first part of sig
    global_header += (unsigned short)0x0201;  // second part of sig
    global_header += (unsigned short)20;      // version made by
    global_header.insert(global_header.end(), local_header.begin() + 4, local_header.begin() + 30);
    global_header += (unsigned short)0;  // file comment length
    global_header += (unsigned short)0;  // disk number where file starts
    global_header += (unsigned short)0;  // internal file attributes
    global_header += (unsigned int)0;    // external file attributes
    global_header
        += (unsigned int)local_header_offset;  // relative offset of local file header, since it
                                               // begins where the global header used to begin
    global_header += fname;
  }

  // write global headers
  unsigned int global_header_offset
      = ftell(fp);  // this is where the global headers get written to in the file
  fwrite(&global_header[0], sizeof(char), global_header.size(), fp);

  // build footer
  auto nrecs = items.size();
  std::vector<char> footer;
  footer += "PK";                                // first part of sig
  footer += (unsigned short)0x0605;              // second part of sig
  footer += (unsigned short)0;                   // number of this disk
  footer += (unsigned short)0;                   // disk where footer starts
  footer += (unsigned short)nrecs;               // number of records on this disk
  footer += (unsigned short)nrecs;               // total number of records
  footer += (unsigned int)global_header.size();  // nbytes of global headers
  footer += (unsigned int)global_header_offset;  // offset of start of global headers
  footer += (unsigned short)0;                   // zip file comment length

  // write footer
  fwrite(&footer[0], sizeof(char), footer.size(), fp);

  // close up
  fflush(fp);
  bool bad = ferror(fp) != 0;
  fclose(fp);

  // move to final location (atomically)
#ifdef _MSC_VER
  unlink(zipname.c_str());  // needed for Windows
#endif
  bad = bad || (rename(tmpname.c_str(), zipname.c_str()) == -1);

  if(bad) {
    unlink(tmpname.c_str());
    throw std::runtime_error("npz_save: error saving to file: " + zipname);
  }
}
}  // namespace cnpy

Scalar mean(const std::vector<Scalar> &xs) {
  if(xs.empty()) {
    return 0.0F;
  }
  Scalar sum = 0;
  for(auto &x : xs) {
    sum += x;
  }
  return sum / static_cast<Scalar>(xs.size());
}

Scalar sigma(const std::vector<Scalar> &xs) {
  if(xs.empty()) {
    return 0.0F;
  }

  Scalar mu = mean(xs);
  Scalar sum = 0;
  for(auto &x : xs) {
    sum += (x - mu) * (x - mu);
  }
  return std::sqrt(sum / static_cast<Scalar>(xs.size()));
}

struct Item {
  std::string key;
  Scalar value;
};

struct Record {
  std::string name;
  std::string key;
  Item mean_abs;
  Item stddev_abs;
  Item mean;
  Item stddev;
  Item max_abs;
};

void scan(std::istream &in, Item &item) {
  in >> item.key;
  in >> item.value;
}

void scan(std::istream &in, Record &record) {
  in >> record.name;
  in >> record.key;
  scan(in, record.mean_abs);
  scan(in, record.stddev_abs);
  scan(in, record.mean);
  scan(in, record.stddev);
  scan(in, record.max_abs);
}

template <class Map, class Key, class Default>
void default_if_not_exists(Map &map, const Key &key, Default value) {
  auto query = map.find(key);
  if(query == map.end()) {
    map.emplace(key, value);
  }
}
using InternalMap = std::unordered_map<std::string, std::vector<Scalar>>;
using Map = std::unordered_map<std::string, InternalMap>;

std::string replace_prefix(const std::string &original,
                           const std::string &prefix,
                           const std::string &replacement) {
  if(original.compare(0, prefix.length(), prefix) == 0) {
    // If the string starts with the prefix, replace it
    return replacement + original.substr(prefix.length());
  } else {
    // If the string doesn't start with the prefix, return the original string
    return original;
  }
}

Map parse_quantmult_file(const std::string &input) {
  Map map;

  std::ifstream in(input);
  Record record;
  while(!in.eof()) {
    scan(in, record);
    std::string key = replace_prefix(record.key, "F0::", "");
    std::string query = "QuantMultA";
    if(key.find(query) != std::string::npos) {
      default_if_not_exists(map, key, InternalMap());
      auto insert = [&key, &map](Item &item) {
        std::vector<Scalar> empty;
        default_if_not_exists(map[key], item.key, empty);
        map[key][item.key].push_back(item.value);
      };
      insert(record.mean_abs);
      insert(record.stddev_abs);
      insert(record.mean);
      insert(record.stddev);
      insert(record.max_abs);
    }
  }

  return map;
}

int main(int argc, char **argv) {
  std::string input(argv[1]);
  std::string model_path(argv[2]);
  std::string output_path(argv[3]);

  Map map = parse_quantmult_file(input);
  // load the entire npz file
  cnpy::npz_t model = cnpy::npz_load(model_path);
  for(auto &p : map) {
    InternalMap &internal_map = p.second;
    std::cout << p.first << ": \n";
    const std::string &name = p.first;
    for(auto &q : internal_map) {
      const std::string &statistic = q.first;
      std::cout << "\t" << statistic << " ";
      std::cout << "{mu: " << mean(q.second);
      std::cout << " std: " << sigma(q.second) << "} ";

      Scalar alpha = 127.0 / (mean(q.second) + 1.1 * sigma(q.second));
      std::cout << "alpha: " << alpha;
      std::cout << "\n";

      // Create cnpy::NpzItem
      if(statistic.find("MaxAbs") != std::string::npos) {
        std::vector<Scalar> data = {alpha};
        std::vector<unsigned int> shape = {1};
        cnpy::NpzItem item(name, data, shape);
        auto base = std::make_shared<cnpy::NpyArray>();
        base->bytes = item.bytes;
        base->shape = item.shape;
        base->type = item.type;
        base->word_size = item.word_size;
        base->fortran_order = item.fortran_order;
        model.emplace(name, base);
      }
    }
    std::cout << "\n";
  }

  auto wemb = model.find("Wemb_QuantMultA");
  if(wemb != model.end()) {
    model.emplace("none_QuantMultA", wemb->second);
  } else {
    auto none = model.find("none_QuantMultA");
    if(none != model.end()) {
      model.emplace("Wemb_QuantMultA", none->second);
    }
  }

  npz_save_from_items(output_path, model);

  return 0;
}
