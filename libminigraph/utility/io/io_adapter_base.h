//
// Created by hsiaoko on 2022/3/14.
//

#ifndef MINIGRAPH_UTILITY_IO_IO_ADAPTER_BASE_H
#define MINIGRAPH_UTILITY_IO_IO_ADAPTER_BASE_H

#include "graphs/immutable_csr.h"
#include <folly/AtomicHashArray.h>
#include <folly/AtomicHashMap.h>
#include <iostream>
#include <string>

namespace minigraph {
namespace utility {
namespace io {

template <typename GID_T, typename VID_T, typename VDATA_T, typename EDATA_T>
class IOAdapterBase {
 public:
  IOAdapterBase(){};
  ~IOAdapterBase(){};

  virtual bool IsExist(const std::string& pt) const = 0;
  virtual void MakeDirectory(const std::string& pt) = 0;
  virtual void Touch(const std::string& pt) = 0;
};

}  // namespace io
}  // namespace utility
}  // namespace libminigraph

#endif  // MINIGRAPH_UTILITY_IO_IO_ADAPTER_BASE_H
