#include "2d_pie/auto_app_base.h"
#include "2d_pie/edge_map_reduce.h"
#include "2d_pie/vertex_map_reduce.h"
#include "executors/task_runner.h"
#include "graphs/graph.h"
#include "minigraph_sys.h"
#include "portability/sys_data_structure.h"
#include "portability/sys_types.h"
#include "utility/logging.h"
#include <folly/concurrency/DynamicBoundedQueue.h>

template <typename GRAPH_T, typename CONTEXT_T>
class WCCVMap : public minigraph::VMapBase<GRAPH_T, CONTEXT_T> {
  using VertexInfo = minigraph::graphs::VertexInfo<typename GRAPH_T::vid_t,
                                                   typename GRAPH_T::vdata_t,
                                                   typename GRAPH_T::edata_t>;

 public:
  WCCVMap(const CONTEXT_T& context)
      : minigraph::VMapBase<GRAPH_T, CONTEXT_T>(context) {}
  bool C(const VertexInfo& u) override { return false; }
  bool F(VertexInfo& u, GRAPH_T* graph = nullptr) override { return false; }
};

template <typename GRAPH_T, typename CONTEXT_T>
class WCCEMap : public minigraph::EMapBase<GRAPH_T, CONTEXT_T> {
  using VertexInfo = minigraph::graphs::VertexInfo<typename GRAPH_T::vid_t,
                                                   typename GRAPH_T::vdata_t,
                                                   typename GRAPH_T::edata_t>;
  using GID_T = typename GRAPH_T::gid_t;
  using VID_T = typename GRAPH_T::vid_t;
  using VDATA_T = typename GRAPH_T::vdata_t;
  using EDATA_T = typename GRAPH_T::edata_t;
  using Frontier = folly::DMPMCQueue<VertexInfo, false>;

 public:
  WCCEMap(const CONTEXT_T& context)
      : minigraph::EMapBase<GRAPH_T, CONTEXT_T>(context) {}

  bool C(const VertexInfo& u, const VertexInfo& v) override {
    if (u.vdata[0] < v.vdata[0]) {
      return true;
    } else {
      return false;
    }
  }

  bool F(const VertexInfo& u, VertexInfo& v) override {
    v.vdata[0] = u.vdata[0];
    return true;
  }

  static bool kernel_pull_border_vertexes(
      size_t tid, Frontier* frontier_out, VertexInfo& u, GRAPH_T* graph,
      std::unordered_map<VID_T, VDATA_T>* global_border_vertexes_vdata,
      bool* visited) {
    bool tag = false;
    for (size_t i = 0; i < u.indegree; i++) {
      auto iter = global_border_vertexes_vdata->find(u.in_edges[i]);
      if (iter != global_border_vertexes_vdata->end()) {
        if (*u.vdata > iter->second) {
          tag = true;
          *u.vdata = iter->second;
          visited[u.vid] = 1;
        }
      }
    }
    if (tag) frontier_out->enqueue(u);
    return tag;
  }
};

template <typename GRAPH_T, typename CONTEXT_T>
class WCCPIE : public minigraph::AutoAppBase<GRAPH_T, CONTEXT_T> {
  using VertexInfo = minigraph::graphs::VertexInfo<typename GRAPH_T::vid_t,
                                                   typename GRAPH_T::vdata_t,
                                                   typename GRAPH_T::edata_t>;

 public:
  WCCPIE(minigraph::VMapBase<GRAPH_T, CONTEXT_T>* vmap,
         minigraph::EMapBase<GRAPH_T, CONTEXT_T>* emap,
         const CONTEXT_T& context)
      : minigraph::AutoAppBase<GRAPH_T, CONTEXT_T>(vmap, emap, context) {}

  using Frontier = folly::DMPMCQueue<VertexInfo, false>;

  bool Init(GRAPH_T& graph) override { return true; }

  bool PEval(GRAPH_T& graph,
             minigraph::executors::TaskRunner* task_runner) override {
    LOG_INFO("PEval() - Processing gid: ", graph.gid_);
    bool* visited = (bool*)malloc(graph.get_num_vertexes());
    memset(visited, 0, sizeof(bool) * graph.get_num_vertexes());
    Frontier* frontier_in = new Frontier(graph.get_num_vertexes() + 10);
    for (size_t i = 0; i < graph.get_num_vertexes(); i++) {
      frontier_in->enqueue(graph.GetVertexByIndex(i));
    }
    while (!frontier_in->empty()) {
      frontier_in = this->emap_->Map(frontier_in, visited, graph, task_runner);
    }
    auto tag = this->msg_mngr_->UpdateBorderVertexes(graph, visited);
    free(visited);
    LOG_INFO("###");
    return tag;
  }

  bool IncEval(GRAPH_T& graph,
               minigraph::executors::TaskRunner* task_runner) override {
    auto global_border_vertexes_vdata =
        *this->msg_mngr_->border_vertexes_->GetBorderVertexVdata();
    bool* visited = (bool*)malloc(graph.get_num_vertexes());
    memset(visited, 0, sizeof(bool) * graph.get_num_vertexes());
    Frontier* frontier_in = new Frontier(graph.get_num_vertexes() + 1);

    for (size_t i = 0; i < graph.get_num_vertexes(); i++) {
      VertexInfo&& u = graph.GetVertexByIndex(i);
      if (u.vdata[0] == 1) continue;
      for (size_t j = 0; j < u.indegree; j++) {
        auto iter_nbr = global_border_vertexes_vdata.find(u.in_edges[j]);
        if (iter_nbr == global_border_vertexes_vdata.end()) continue;
        if (iter_nbr->second < u.vdata[0]) {
          u.vdata[0] = iter_nbr->second;
          visited[u.vid] = 1;
        }
      }
    }

    for (size_t i = 0; i < graph.get_num_vertexes(); i++) {
      frontier_in->enqueue(graph.GetVertexByIndex(i));
    }
    bool tag = false;

    LOG_INFO("IncEval() - Processing gid: ", graph.gid_);
    VertexInfo u;
    while (!frontier_in->empty()) {
      frontier_in->dequeue(u);
      for (size_t i = 0; i < u.outdegree; i++) {
        auto local_id = graph.globalid2localid(u.out_edges[i]);
        if (local_id == VID_MAX) continue;
        VertexInfo&& v = graph.GetVertexByVid(local_id);
        if (v.vdata[0] >  u.vdata[0]) {
          v.vdata[0] = u.vdata[0];
          visited[v.vid] = 1;
        }
      }
    }
    tag = this->msg_mngr_->UpdateBorderVertexes(graph, visited);
    free(visited);
    return tag;
  }
};

struct Context {
  size_t root_id = 0;
};

using CSR_T = minigraph::graphs::ImmutableCSR<gid_t, vid_t, vdata_t, edata_t>;
using WCCPIE_T = WCCPIE<CSR_T, Context>;

int main(int argc, char* argv[]) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  std::string work_space = FLAGS_i;
  size_t num_workers_lc = FLAGS_lc;
  size_t num_workers_cc = FLAGS_cc;
  size_t num_workers_dc = FLAGS_dc;
  size_t num_cores = FLAGS_cores;
  Context context;
  auto wcc_emap = new WCCEMap<CSR_T, Context>(context);
  auto wcc_vmap = new WCCVMap<CSR_T, Context>(context);
  auto bfs_pie = new WCCPIE<CSR_T, Context>(wcc_vmap, wcc_emap, context);
  auto app_wrapper =
      new minigraph::AppWrapper<WCCPIE<CSR_T, Context>, gid_t, vid_t, vdata_t,
                                edata_t>(bfs_pie);

  minigraph::MiniGraphSys<CSR_T, WCCPIE_T> minigraph_sys(
      work_space, num_workers_lc, num_workers_cc, num_workers_dc, num_cores,
      app_wrapper);
  minigraph_sys.RunSys();
  minigraph_sys.ShowResult();
  gflags::ShutDownCommandLineFlags();
}