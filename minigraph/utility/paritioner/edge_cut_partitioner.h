//
// Created by hsiaoko on 2022/3/15.
//

#ifndef MINIGRAPH_UTILITY_EDGE_CUT_PARTITIONER_H
#define MINIGRAPH_UTILITY_EDGE_CUT_PARTITIONER_H

#include "portability/sys_types.h"
#include "utility/io/csr_io_adapter.h"
#include "utility/io/io_adapter_base.h"
#include <folly/AtomicHashMap.h>
#include <folly/FBVector.h>
#include <vector>

namespace minigraph {
namespace utility {
namespace partitioner {

// With an edgecut partition, each vertex is assigned to a fragment.
// In a fragment, inner vertices are those vertices assigned to it, and the
// outer vertices are the remaining vertices adjacent to some of the inner
// vertices. The load strategy defines how to store the adjacency between inner
// and outer vertices.
//
// For example, a graph
// G = {V, E}
// V = {v0, v1, v2, v3, v4}
// E = {(v0, v2), (v0, v3), (v1, v0), (v3, v1), (v3, v4), (v4, v1), (v4, v2)}
// might be splitted into F0 that consists of  V_F0: {v0, v1, v2}, E_F0: {(v0,
// v2), (v0, v3), (v1, v0)} and F1 that consists of V_F1: {v3, v4}, E_F1: {(v3,
// v1), (v3, v4), (v4, v1), (v4, v2)}
template <typename GID_T, typename VID_T, typename VDATA_T, typename EDATA_T>
class EdgeCutPartitioner {
 public:
  EdgeCutPartitioner(const std::string& graph_pt, const std::string& root_pt) {
    graph_pt_ = graph_pt;
    root_pt_ = root_pt;
    XLOG(INFO, "  Origin Graph: ", graph_pt_);
  };

  bool RunPartition(const size_t& number_partitions) {
    XLOG(INFO, "RunPartition");
    if (!SplitImmutableCSR(number_partitions, graph_pt_)) {
      return false;
    };
    auto count = 0;
    for (auto& iter_fragments : *fragments_) {
      graphs::ImmutableCSR<GID_T, VID_T, VDATA_T, EDATA_T>* fragment =
          (graphs::ImmutableCSR<GID_T, VID_T, VDATA_T, EDATA_T>*)iter_fragments;
      fragment->ShowGraph();
      fragment->Serialize();
      std::string vertex_pt =
          root_pt_ + "/vertex/" + std::to_string(count) + ".v";
      std::string meta_out_pt =
          root_pt_ + "/meta/out/" + std::to_string(count) + ".meta";
      std::string meta_in_pt =
          root_pt_ + "/meta/in/" + std::to_string(count) + ".meta";
      std::string vdata_pt =
          root_pt_ + "/vdata/" + std::to_string(count) + ".vdata";
      std::string localid2globalid_pt =
          root_pt_ + "/localid2globalid/" + std::to_string(count) + ".idmap";
      XLOG(INFO, meta_out_pt);
      XLOG(INFO, meta_in_pt);
      XLOG(INFO, vdata_pt);
      XLOG(INFO, localid2globalid_pt);
      csr_io_adapter_->Write(*fragment, vertex_pt, meta_in_pt, meta_out_pt,
                             vdata_pt, localid2globalid_pt);
      count++;
    }
  }

  bool SplitImmutableCSR(const size_t& num_partitions,
                         const std::string& graph_pt) {
    XLOG(INFO, "SplitImmutableCSR");
    csr_io_adapter_ =
        std::make_unique<io::CSRIOAdapter<GID_T, VID_T, VDATA_T, EDATA_T>>();
    auto immutable_csr =
        new graphs::ImmutableCSR<GID_T, VID_T, VDATA_T, EDATA_T>;
    if (!csr_io_adapter_->Read(
            (graphs::Graph<GID_T, VID_T, VDATA_T, EDATA_T>*)immutable_csr,
            edge_graph_csv, 0, graph_pt)) {
      return false;
    }
    fragments_ =
        new std::vector<graphs::Graph<GID_T, VID_T, VDATA_T, EDATA_T>*>();
    const size_t num_vertex_per_fragments =
        immutable_csr->get_num_vertexes() / num_partitions;
    VID_T localid = 0;
    VID_T globalid = 0;
    GID_T gid = 0;
    size_t count = 0;
    graphs::ImmutableCSR<GID_T, VID_T, VDATA_T, EDATA_T>* csr_fragment =
        nullptr;
    auto iter_vertexes = immutable_csr->vertexes_info_->begin();
    while (iter_vertexes != immutable_csr->vertexes_info_->end()) {
      if (csr_fragment == nullptr || count > num_vertex_per_fragments) {
        if (csr_fragment != nullptr) {
          csr_fragment->gid_ = gid++;
          csr_fragment->num_vertexes_ = csr_fragment->vertexes_info_->size();
          fragments_->push_back(csr_fragment);
          csr_fragment = nullptr;
          count = 0;
          localid = 0;
        }
        csr_fragment = new graphs::ImmutableCSR<GID_T, VID_T, VDATA_T, EDATA_T>;
        csr_fragment->map_localid2globalid_->insert(
            std::make_pair(localid, iter_vertexes->second->vid));
        csr_fragment->map_globalid2localid_->insert(
            std::make_pair(iter_vertexes->second->vid, localid));
        iter_vertexes->second->vid = localid;
        csr_fragment->sum_in_edges_ += iter_vertexes->second->indegree;
        csr_fragment->sum_out_edges_ += iter_vertexes->second->outdegree;
        csr_fragment->vertexes_info_->insert(
            std::make_pair(localid, iter_vertexes->second));
        immutable_csr->vertexes_info_->erase(iter_vertexes->first);
        iter_vertexes++;
        ++localid;
        ++count;
      } else {
        csr_fragment->map_localid2globalid_->insert(
            std::make_pair(localid, iter_vertexes->second->vid));
        csr_fragment->map_globalid2localid_->insert(
            std::make_pair(iter_vertexes->second->vid, localid));
        iter_vertexes->second->vid = localid;
        csr_fragment->sum_in_edges_ += iter_vertexes->second->indegree;
        csr_fragment->sum_out_edges_ += iter_vertexes->second->outdegree;
        csr_fragment->vertexes_info_->insert(
            std::make_pair(localid, iter_vertexes->second));
        immutable_csr->vertexes_info_->erase(iter_vertexes->first);
        iter_vertexes++;
        ++localid;
        ++count;
      }
    }
    if (csr_fragment != nullptr) {
      csr_fragment->gid_ = gid++;
      csr_fragment->num_vertexes_ = csr_fragment->vertexes_info_->size();
      fragments_->push_back(csr_fragment);
    }
    if (fragments_->size() > 0) {
      return true;
    } else {
      return false;
    }
  }

 private:
  std::string graph_pt_;

  // to store fragments
  std::string root_pt_;

  std::vector<graphs::Graph<GID_T, VID_T, VDATA_T, EDATA_T>*>* fragments_ =
      nullptr;
  std::unique_ptr<io::CSRIOAdapter<GID_T, VID_T, VDATA_T, EDATA_T>>
      csr_io_adapter_ = nullptr;
};

}  // namespace partitioner
}  // namespace utility
}  // namespace minigraph

#endif  // MINIGRAPH_UTILITY_EDGE_CUT_PARTITIONER_H