/****************************************************************************
 Copyright (c) 2021-2023 Xiamen Yaji Software Co., Ltd.

 http://www.cocos.com

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights to
 use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 of the Software, and to permit persons to whom the Software is furnished to do so,
 subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
****************************************************************************/
#ifdef __clang__
    #pragma clang diagnostic ignored "-Wshorten-64-to-32"
#endif
#include <algorithm>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/breadth_first_search.hpp>
#include <boost/graph/transitive_closure.hpp>
#include <boost/range/algorithm.hpp>
#include <iterator>
#include <limits>
#include <vector>
#include "FGDispatcherGraphs.h"
#include "FGDispatcherTypes.h"
#include "LayoutGraphGraphs.h"
#include "LayoutGraphTypes.h"
#include "RenderGraphGraphs.h"
#include "base/Log.h"
#include "boost/graph/depth_first_search.hpp"
#include "boost/graph/hawick_circuits.hpp"
#include "boost/graph/visitors.hpp"
#include "boost/lexical_cast.hpp"
#include "details/Range.h"
#include "gfx-base/GFXBarrier.h"
#include "gfx-base/GFXDef-common.h"
#include "gfx-base/GFXDef.h"
#include "gfx-base/GFXDevice.h"
#include "gfx-base/states/GFXBufferBarrier.h"
#include "gfx-base/states/GFXTextureBarrier.h"
#include "math/Vec2.h"
#include "pipeline/custom/RenderCommonFwd.h"
#include "pipeline/custom/RenderGraphTypes.h"
#include "pipeline/custom/details/GslUtils.h"

#ifndef BRANCH_CULLING
    #define BRANCH_CULLING 0
#endif

namespace cc {

namespace render {

static constexpr bool ENABLE_BRANCH_CULLING = BRANCH_CULLING;

void passReorder(FrameGraphDispatcher &fgDispatcher);
void memoryAliasing(FrameGraphDispatcher &fgDispatcher);
void buildBarriers(FrameGraphDispatcher &fgDispatcher);

void FrameGraphDispatcher::run() {
    if (_enablePassReorder) {
        passReorder(*this);
    }
    if (_enableMemoryAliasing) {
        memoryAliasing(*this);
    }
    buildBarriers(*this);
}

void FrameGraphDispatcher::enablePassReorder(bool enable) {
    _enablePassReorder = enable;
}

void FrameGraphDispatcher::enableMemoryAliasing(bool enable) {
    _enableMemoryAliasing = enable;
}

void FrameGraphDispatcher::setParalellWeight(float paralellExecWeight) {
    _paralellExecWeight = clampf(paralellExecWeight, 0.0F, 1.0F);
}

/////////////////////////////////////////////////////////////////////////////////////INTERNAL⚡IMPLEMENTATION/////////////////////////////////////////////////////////////////////////////////////////////

//---------------------------------------------------------------predefine------------------------------------------------------------------
using PmrString = ccstd::pmr::string;
using RAG = ResourceAccessGraph;
using LGD = LayoutGraphData;
using gfx::PassType;
using BarrierMap = FrameGraphDispatcher::BarrierMap;
using AccessVertex = ResourceAccessGraph::vertex_descriptor;
using InputStatusTuple = std::tuple<PassType /*passType*/, PmrString /*resourceName*/, gfx::ShaderStageFlagBit /*visibility*/, gfx::MemoryAccessBit /*access*/>;
using ResourceHandle = ResourceGraph::vertex_descriptor;
using ResourceNames = PmrFlatSet<PmrString>;
using EdgeList = std::pair<RelationGraph::edge_descriptor, RelationGraph::edge_descriptor>;
using CloseCircuit = std::pair<EdgeList, EdgeList>;
using CloseCircuits = std::vector<CloseCircuit>;
using RelationVert = RelationGraph::vertex_descriptor;
using RelationVerts = std::vector<RelationVert>;
using RelationEdge = RelationGraph::edge_descriptor;
using RelationEdges = std::vector<RelationEdge>;
using ScoreMap = std::map<RelationVert, std::pair<int64_t /*backward*/, int64_t /*forward*/>>;
using RasterViewsMap = PmrTransparentMap<ccstd::pmr::string, RasterView>;
using ComputeViewsMap = PmrTransparentMap<ccstd::pmr::string, ccstd::pmr::vector<ComputeView>>;
using ResourceLifeRecordMap = PmrFlatMap<PmrString, ResourceLifeRecord>;

struct Graphs {
    const RenderGraph &renderGraph;
    ResourceGraph &resourceGraph;
    const LayoutGraphData &layoutGraphData;
    ResourceAccessGraph &resourceAccessGraph;
    RelationGraph &relationGraph;
};

struct ViewStatus {
    const PmrString &name;
    const gfx::AccessFlags accessFlag;
    const ResourceRange &range;
};

auto defaultAccess = gfx::MemoryAccessBit::NONE;
auto defaultVisibility = gfx::ShaderStageFlagBit::NONE;

constexpr uint32_t EXPECT_START_ID = 0;
constexpr uint32_t INVALID_ID = 0xFFFFFFFF;

using AccessTable = PmrFlatMap<ResourceHandle /*resourceID*/, ResourceTransition /*last-curr-status*/>;
using ExternalResMap = PmrFlatMap<PmrString /*resourceName*/, TransitionStatus /*last-curr-status*/>;

// for scoped enum only
template <typename From, typename To>
class GfxTypeConverter {
public:
    To operator()(From from) {
        return static_cast<To>(
            static_cast<std::underlying_type_t<From>>(from));
    }
};

// static auto toGfxAccess = GfxTypeConverter<AccessType, gfx::MemoryAccessBit>();
gfx::MemoryAccessBit toGfxAccess(AccessType type) {
    switch (type) {
        case AccessType::READ:
            return gfx::MemoryAccessBit::READ_ONLY;
        case AccessType::WRITE:
            return gfx::MemoryAccessBit::WRITE_ONLY;
        case AccessType::READ_WRITE:
            return gfx::MemoryAccessBit::READ_WRITE;
        default:
            return gfx::MemoryAccessBit::NONE;
    }
};

// TODO(Zeqiang): remove barrier in renderpassinfo
gfx::GeneralBarrier *getGeneralBarrier(gfx::Device *device, gfx::AccessFlagBit prevAccess, gfx::AccessFlagBit nextAccess) {
    return device->getGeneralBarrier({prevAccess, nextAccess});
}

// AccessStatus.vertID : in resourceNode it's resource ID; in barrierNode it's pass ID.
AccessVertex dependencyCheck(RAG &rag, AccessVertex curVertID, const ResourceGraph &rg, const ViewStatus &status);
gfx::ShaderStageFlagBit getVisibilityByDescName(const RenderGraph &renderGraph, const LGD &lgd, uint32_t passID, const PmrString &resName);

void addAccessStatus(RAG &rag, const ResourceGraph &rg, ResourceAccessNode &node, const ViewStatus &status);
// void addCopyAccessStatus(RAG &rag, const ResourceGraph &rg, ResourceAccessNode &node, const ViewStatus &status, const gfx::ResourceRange &range);
void processRasterPass(const Graphs &graphs, uint32_t passID, const RasterPass &pass);
void processComputePass(const Graphs &graphs, uint32_t passID, const ComputePass &pass);
void processRasterSubpass(const Graphs &graphs, uint32_t passID, const RasterSubpass &pass);
void processComputeSubpass(const Graphs &graphs, uint32_t passID, const ComputeSubpass &pass);
void processCopyPass(const Graphs &graphs, uint32_t passID, const CopyPass &pass);
void processMovePass(const Graphs &graphs, uint32_t passID, const MovePass &pass);
void processRaytracePass(const Graphs &graphs, uint32_t passID, const RaytracePass &pass);
auto getResourceStatus(PassType passType, const PmrString &name, gfx::MemoryAccess memAccess, gfx::ShaderStageFlags visibility, const ResourceGraph &resourceGraph, bool rasterized);

// execution order BUT NOT LOGICALLY
bool isPassExecAdjecent(uint32_t passLID, uint32_t passRID) {
    return std::abs(static_cast<int>(passLID) - static_cast<int>(passRID)) <= 1;
}

ResourceRange getRange(const ResourceAccessGraph& rag, const PmrString& name, const ResourceGraph& resg) {
    return ResourceRange{};
}

template <uint32_t N>
constexpr uint8_t highestBitPos() {
    return highestBitPos<(N >> 1)>() + 1;
}

template <>
constexpr uint8_t highestBitPos<0>() {
    return 0;
}

constexpr uint32_t filledShift(uint8_t pos) {
    return 0xFFFFFFFF >> (32 - pos);
}

constexpr uint8_t HIGHEST_READ_POS = highestBitPos<static_cast<uint32_t>(gfx::AccessFlagBit::PRESENT)>() - 1;
constexpr uint32_t READ_ACCESS = filledShift(HIGHEST_READ_POS) | static_cast<uint32_t>(gfx::AccessFlagBit::SHADING_RATE);

inline bool hasReadAccess(gfx::AccessFlagBit flag) {
    return (static_cast<uint32_t>(flag) & READ_ACCESS) != 0;
}

inline bool isReadOnlyAccess(gfx::AccessFlagBit flag) {
    return flag < gfx::AccessFlagBit::PRESENT || flag == gfx::AccessFlagBit::SHADING_RATE;
}

bool isTransitionStatusDependent(const AccessStatus &lhs, const AccessStatus &rhs);
template <typename Graph>
bool tryAddEdge(uint32_t srcVertex, uint32_t dstVertex, Graph &graph);

// for transive_closure.hpp line 231
inline RelationGraph::vertex_descriptor add_vertex(RelationGraph &g) { // NOLINT
    thread_local uint32_t count = 0;                                   // unused
    return add_vertex(g, count++);
}

// status of resource access
void buildAccessGraph(const Graphs &graphs) {
    // what we need:
    //  - pass dependency
    //  - pass attachment access
    // AccessTable accessRecord;

    const auto &[renderGraph, resourceGraph, layoutGraphData, resourceAccessGraph, relationGraph] = graphs;
    size_t numPasses = 0;
    numPasses += renderGraph.rasterPasses.size();
    numPasses += renderGraph.computePasses.size();
    numPasses += renderGraph.copyPasses.size();
    numPasses += renderGraph.movePasses.size();
    numPasses += renderGraph.raytracePasses.size();

    resourceAccessGraph.reserve(static_cast<ResourceAccessGraph::vertices_size_type>(numPasses));
    resourceAccessGraph.resourceNames.reserve(128);
    resourceAccessGraph.resourceIndex.reserve(128);

    resourceAccessGraph.topologicalOrder.reserve(numPasses);
    resourceAccessGraph.topologicalOrder.clear();
    resourceAccessGraph.resourceLifeRecord.reserve(resourceGraph.names.size());

    if (!resourceAccessGraph.resourceLifeRecord.empty()) {
        resourceAccessGraph.resourceLifeRecord.clear();
    }

    if (!resourceAccessGraph.leafPasses.empty()) {
        resourceAccessGraph.leafPasses.clear();
    }
    if (!resourceAccessGraph.culledPasses.empty()) {
        resourceAccessGraph.culledPasses.clear();
    }

    // const auto &names = get(RenderGraph::Name, renderGraph);
    for (size_t i = 1; i <= numPasses; ++i) {
        resourceAccessGraph.leafPasses.emplace(i, LeafStatus{false, true});
    }

    auto startID = add_vertex(resourceAccessGraph, INVALID_ID - 1);
    CC_EXPECTS(startID == EXPECT_START_ID);

    add_vertex(relationGraph, startID);

    for (const auto passID : makeRange(vertices(renderGraph))) {
        visitObject(
            passID, renderGraph,
            [&](const RasterPass &pass) {
                processRasterPass(graphs, passID, pass);
            },
            [&](const RasterSubpass &pass) {
                processRasterSubpass(graphs, passID, pass);
            },
            [&](const ComputeSubpass &pass) {
                processComputeSubpass(graphs, passID, pass);
            },
            [&](const ComputePass &pass) {
                processComputePass(graphs, passID, pass);
            },
            [&](const CopyPass &pass) {
                processCopyPass(graphs, passID, pass);
            },
            [&](const MovePass &pass) {
                processMovePass(graphs, passID, pass);
            },
            [&](const RaytracePass &pass) {
                processRaytracePass(graphs, passID, pass);
            },
            [&](const auto & /*pass*/) {
                // do nothing
            });
    }

    auto &rag = resourceAccessGraph;
    auto branchCulling = [](ResourceAccessGraph::vertex_descriptor vertex, ResourceAccessGraph &rag) -> void {
        CC_EXPECTS(out_degree(vertex, rag) == 0);
        using FuncType = void (*)(ResourceAccessGraph::vertex_descriptor, ResourceAccessGraph &);
        static FuncType leafCulling = [](ResourceAccessGraph::vertex_descriptor vertex, ResourceAccessGraph &rag) {
            rag.culledPasses.emplace(vertex);
            auto &attachments = get(ResourceAccessGraph::AccessNodeTag{}, rag, vertex);
            attachments.attachmentStatus.clear();
            if (attachments.nextSubpass) {
                delete attachments.nextSubpass;
                attachments.nextSubpass = nullptr;
            }
            auto inEdges = in_edges(vertex, rag);
            for (auto iter = inEdges.first; iter < inEdges.second;) {
                auto inEdge = *iter;
                auto srcVert = source(inEdge, rag);
                remove_edge(inEdge, rag);
                if (out_degree(srcVert, rag) == 0) {
                    leafCulling(srcVert, rag);
                }
                inEdges = in_edges(vertex, rag);
                iter = inEdges.first;
            }
        };
        leafCulling(vertex, rag);
    };

    // no present pass found, add a fake node to gather leaf node(s).
    if (resourceAccessGraph.presentPassID == 0xFFFFFFFF) {
        auto ragEndNode = add_vertex(rag, RenderGraph::null_vertex());
        auto rlgEndNode = add_vertex(relationGraph, ragEndNode);
        // keep sync before pass reorder done.
        CC_EXPECTS(ragEndNode == rlgEndNode);
        resourceAccessGraph.presentPassID = ragEndNode;
        auto iter = resourceAccessGraph.leafPasses.find(ragEndNode);
        constexpr bool isExternal = true;
        constexpr bool needCulling = false;
        if (iter == resourceAccessGraph.leafPasses.end()) {
            resourceAccessGraph.leafPasses.emplace(ragEndNode, LeafStatus{isExternal, needCulling});
        } else {
            resourceAccessGraph.leafPasses.at(ragEndNode) = LeafStatus{isExternal, needCulling};
        }
    }

    // make leaf node closed walk for pass reorder
    for (auto pass : resourceAccessGraph.leafPasses) {
        bool isExternal = pass.second.isExternal;
        bool needCulling = pass.second.needCulling;

        if (pass.first != resourceAccessGraph.presentPassID) {
            if (isExternal && !needCulling) {
                add_edge(pass.first, resourceAccessGraph.presentPassID, resourceAccessGraph);
            } else {
                // write into transient resources, culled
                if constexpr (ENABLE_BRANCH_CULLING) {
                    branchCulling(pass.first, resourceAccessGraph);
                }
            }
        }
    }
    for (auto rit = resourceAccessGraph.culledPasses.rbegin(); rit != resourceAccessGraph.culledPasses.rend(); ++rit) {
        // remove culled vertices, std::less make this set ascending order, so reverse iterate
        remove_vertex(*rit, relationGraph);
    }

    for (auto rlgVert : makeRange(vertices(relationGraph))) {
        auto ragVert = get(RelationGraph::DescIDTag{}, relationGraph, rlgVert);
        rag.topologicalOrder.emplace_back(ragVert);
    }
}

#pragma region BUILD_BARRIERS
struct BarrierVisitor : public boost::bfs_visitor<> {
    using Vertex = ResourceAccessGraph::vertex_descriptor;
    using Edge = ResourceAccessGraph::edge_descriptor;
    using Graph = ResourceAccessGraph;

    explicit BarrierVisitor(
        const ResourceGraph &rg,
        BarrierMap &barriers,                  // what we get
        ResourceNames &resourceNamesIn,        // for resource record
        const AccessTable &accessRecordIn,     // resource last meet
        ResourceLifeRecordMap &rescLifeRecord, // resource lifetime
        PmrMap<ResourceAccessGraph::vertex_descriptor, FGRenderPassInfo> &rpInfosIn)
    : barrierMap(barriers), resourceGraph(rg), resourceNames(resourceNamesIn), accessRecord(accessRecordIn), resourceLifeRecord(rescLifeRecord), rpInfos(rpInfosIn) {}

    void updateResourceLifeTime(const Graph &g, const ResourceAccessNode &node, ResourceAccessGraph::vertex_descriptor u) {
        for (auto access : node.attachmentStatus) {
            auto resID = g.resourceIndex.at(access.resName);
            auto name = get(ResourceGraph::NameTag{}, resourceGraph, resID);
            if (resourceLifeRecord.find(name) == resourceLifeRecord.end()) {
                resourceLifeRecord.emplace(name, ResourceLifeRecord{u, u});
            } else {
                resourceLifeRecord.at(name).end = u;
            }
        }
    }

    struct AccessNodeInfo {
        const std::vector<ResourceStatus> &status;
        std::vector<Barrier> &edgeBarriers; // need to barrier front or back
        const Vertex &vertID;
        uint32_t subpassIndex{INVALID_ID};
    };

    void processVertex(Vertex u, const Graph &g) {
        if (in_degree(u, g) == 0 && out_degree(u, g) == 0) {
            // culled
            return;
        }

        const ResourceAccessNode &access = get(ResourceAccessGraph::AccessNodeTag{}, g, u);
        updateResourceLifeTime(g, access, u);

        if (barrierMap.find(u) == barrierMap.end()) {
            barrierMap.emplace(u, BarrierNode{{}, {}});
        }

        const auto *srcAccess = &access;

        auto *dstAccess = access.nextSubpass;
        auto &blockBarrier = barrierMap[u].blockBarrier;
        auto &barriers = barrierMap[u].subpassBarriers;

        uint32_t srcSubpass = 0;
        uint32_t dstSubpass = 1;
        bool isAdjacent = true; // subpass always adjacent to each other
        if (dstAccess) {
            if (!dstAccess->nextSubpass) {
                return;
            }
            // subpass at least two passes inside.
            // the very first pass becomes attachment status collection of all subpass
            CC_ASSERT(dstAccess->nextSubpass);
            srcAccess = dstAccess;
            dstAccess = dstAccess->nextSubpass;
        }
        while (srcAccess) {
            while (dstAccess) {
                // 2 barriers at least when subpass exist
                std::vector<Barrier> &srcRearBarriers = barriers[srcSubpass].rearBarriers;
                std::vector<Barrier> &dstFrontBarriers = barriers[dstSubpass].frontBarriers;

                AccessNodeInfo from = {srcAccess->attachmentStatus, srcRearBarriers, u, srcSubpass};
                AccessNodeInfo to = {dstAccess->attachmentStatus, dstFrontBarriers, u, dstSubpass};
                std::set<uint32_t> noUseSet;
                fillBarrier(g, from, to, isAdjacent, noUseSet);

                dstAccess = dstAccess->nextSubpass;
                ++dstSubpass;
            }
            srcAccess = srcAccess->nextSubpass;
            if (srcAccess) {
                dstAccess = srcAccess->nextSubpass;
                ++srcSubpass;
                dstSubpass = srcSubpass + 1;
            }
        }
    }

    void discover_vertex(Vertex u, const Graph &g) {
        processVertex(u, g);
    }

    void finish_vertex(Vertex u, const Graph &g) {
        auto &rearBarriers = barrierMap[u].blockBarrier.rearBarriers;
        auto &frontBarriers = barrierMap[u].blockBarrier.frontBarriers;
        Barrier *lastSubpassBarrier = nullptr;
        uint32_t subpassIdx{0};
        for (const auto &barriers : barrierMap[u].subpassBarriers) {
            if (!barriers.frontBarriers.empty()) {
                auto &subpassDependencies = rpInfos.at(u).rpInfo.dependencies;
                auto dependency = gfx::SubpassDependency{};
                dependency.srcSubpass = INVALID_ID;
                dependency.dstSubpass = subpassIdx;
                for (const auto &barrier : barriers.frontBarriers) {
                    auto resID = barrier.resourceID;
                    auto findBarrierByResID = [resID](const Barrier &barrier) {
                        return barrier.resourceID == resID;
                    };
                    auto resFinalPassID = accessRecord.at(resID).currStatus.ragVertID;
                    auto firstMeetIter = std::find_if(frontBarriers.begin(), frontBarriers.end(), findBarrierByResID);
                    auto innerResIter = std::find_if(rearBarriers.begin(), rearBarriers.end(), findBarrierByResID);

                    if (firstMeetIter == frontBarriers.end() && innerResIter == rearBarriers.end() && resFinalPassID >= u) {
                        auto &collect = frontBarriers.emplace_back(barrier);
                        collect.beginStatus.ragVertID = collect.endStatus.ragVertID = u;
                    }
                    if ((barrier.beginStatus.ragVertID > dependency.srcSubpass) && (barrier.beginStatus.ragVertID != INVALID_ID)) {
                        dependency.srcSubpass = barrier.beginStatus.ragVertID;
                    }

                    const auto &desc = get(ResourceGraph::DescTag{}, resourceGraph, resID);
                    if (desc.format == gfx::Format::DEPTH_STENCIL || desc.format == gfx::Format::DEPTH) {
                        if (g.access.at(u).attachmentStatus.size() > 1) {
                            auto &dsDep = subpassDependencies.emplace_back();
                            dsDep.srcSubpass = dependency.srcSubpass;
                            dsDep.dstSubpass = dependency.dstSubpass;
                            dsDep.prevAccesses.emplace_back(barrier.beginStatus.access.accessFlag);
                            dsDep.nextAccesses.emplace_back(barrier.endStatus.access.accessFlag);
                        }
                    } else {
                        bool isWriteAccess = !isReadOnlyAccess(barrier.endStatus.access.accessFlag);
                        if (isWriteAccess) {
                            auto &dep = subpassDependencies.emplace_back();
                            dep.srcSubpass = dependency.srcSubpass;
                            dep.dstSubpass = dependency.dstSubpass;
                            dep.prevAccesses.emplace_back(barrier.beginStatus.access.accessFlag);
                            dep.nextAccesses.emplace_back(barrier.endStatus.access.accessFlag);
                            if (hasReadAccess(barrier.endStatus.access.accessFlag)) {
                                auto &selfDep = subpassDependencies.emplace_back();
                                selfDep.srcSubpass = dependency.dstSubpass;
                                selfDep.dstSubpass = dependency.dstSubpass;
                                selfDep.prevAccesses.emplace_back(barrier.endStatus.access.accessFlag);
                                selfDep.nextAccesses.emplace_back(barrier.endStatus.access.accessFlag);
                            }
                        } else {
                            dependency.prevAccesses.emplace_back(barrier.beginStatus.access.accessFlag);
                            dependency.nextAccesses.emplace_back(barrier.endStatus.access.accessFlag);
                        }
                    }
                }
                if (!dependency.prevAccesses.empty() && dependency.srcSubpass != INVALID_ID) {
                    subpassDependencies.emplace_back(dependency);
                }
            }

            if (!barriers.rearBarriers.empty()) {
                auto &subpassDependencies = rpInfos.at(u).rpInfo.dependencies;
                auto dependency = gfx::SubpassDependency{};
                dependency.srcSubpass = subpassIdx;
                dependency.dstSubpass = INVALID_ID;
                for (const auto &barrier : barriers.rearBarriers) {
                    if (barrier.endStatus.ragVertID < dependency.dstSubpass) {
                        dependency.dstSubpass = barrier.endStatus.ragVertID;
                    }

                    auto resID = barrier.resourceID;
                    const auto &desc = get(ResourceGraph::DescTag{}, resourceGraph, resID);
                    if (desc.format == gfx::Format::DEPTH_STENCIL || desc.format == gfx::Format::DEPTH) {
                        if (g.access.at(u).attachmentStatus.size() > 1) {
                            auto &dsDep = subpassDependencies.emplace_back();
                            dsDep.srcSubpass = dependency.srcSubpass;
                            dsDep.dstSubpass = dependency.dstSubpass;
                            dsDep.prevAccesses.emplace_back(barrier.beginStatus.access.accessFlag);
                            dsDep.nextAccesses.emplace_back(barrier.endStatus.access.accessFlag);
                        }
                    } else {
                        bool isWriteAccess = !isReadOnlyAccess(barrier.endStatus.access.accessFlag);
                        if (isWriteAccess) {
                            auto &dep = subpassDependencies.emplace_back();
                            dep.srcSubpass = dependency.srcSubpass;
                            dep.dstSubpass = dependency.dstSubpass;
                            dep.prevAccesses.emplace_back(barrier.beginStatus.access.accessFlag);
                            dep.nextAccesses.emplace_back(barrier.endStatus.access.accessFlag);
                            if (hasReadAccess(barrier.endStatus.access.accessFlag)) {
                                auto &selfDep = subpassDependencies.emplace_back();
                                selfDep.srcSubpass = dependency.dstSubpass;
                                selfDep.dstSubpass = dependency.dstSubpass;
                                selfDep.prevAccesses.emplace_back(barrier.endStatus.access.accessFlag);
                                selfDep.nextAccesses.emplace_back(barrier.endStatus.access.accessFlag);
                            }
                        } else {
                            dependency.prevAccesses.emplace_back(barrier.beginStatus.access.accessFlag);
                            dependency.nextAccesses.emplace_back(barrier.endStatus.access.accessFlag);
                        }
                    }
                }
                if (!dependency.prevAccesses.empty()) {
                    subpassDependencies.emplace_back(dependency);
                }
            }
            ++subpassIdx;
        }
    }

    void fillBarrier(const Graph &g, const AccessNodeInfo &from, const AccessNodeInfo &to, bool isAdjacent, std::set<uint32_t> &subpassResourceSet) {
        const auto &[srcStatus, srcRearBarriers, srcPassVert, srcHasSubpass] = from;
        const auto &[dstStatus, dstFrontBarriers, dstPassVert, dstHasSubpass] = to;
        auto srcVert = srcPassVert;
        auto dstVert = dstPassVert;
        bool subToSubDeps = false;
        if (srcPassVert == dstPassVert) {
            srcVert = srcHasSubpass;
            dstVert = dstHasSubpass;
            subToSubDeps = true;
        }

        bool dstExternalDeps = (srcHasSubpass != INVALID_ID) && (dstHasSubpass == INVALID_ID);
        bool srcExternalDeps = (srcHasSubpass == INVALID_ID) && (dstHasSubpass != INVALID_ID);

        std::vector<ResourceGraph::vertex_descriptor> commonResources;
        //std::set_intersection(srcStatus.begin(), srcStatus.end(),
        //                      dstStatus.begin(), dstStatus.end(),
        //                      std::back_inserter(commonResources),
        //                      [&](const ResourceStatus &lhs, const ResourceStatus &rhs) {
        //                          return lhs.resName < rhs.resName;
        //                      });
        for (const auto& src : srcStatus) {
            for (const auto& dst : dstStatus) {
                if (g.resourceIndex.at(src.resName) == g.resourceIndex.at(dst.resName) &&
                    (g.movedResources.find(src.resName) == g.movedResources.end() || g.movedResources.find(dst.resName) == g.movedResources.end())) {
                    commonResources.emplace_back(g.resourceIndex.at(src.resName));
                }
            }
        }
        if (!commonResources.empty()) {
            // this edge is a logic edge added during pass reorder,
            // no real dependency between this two vertices.

            // NOLINTNEXTLINE
            for (uint32_t i = 0; i < commonResources.size(); ++i) {
                uint32_t resourceID = commonResources[i];
                if (subpassResourceSet.find(resourceID) != subpassResourceSet.end()) {
                    continue;
                }
                subpassResourceSet.emplace(resourceID);
                auto findAccessByID = [resourceID, &g](const ResourceStatus &resAccess) {
                    return g.resourceIndex.at(resAccess.resName) == resourceID;
                };
                auto fromIter = std::find_if(srcStatus.begin(), srcStatus.end(), findAccessByID);
                auto toIter = std::find_if(dstStatus.begin(), dstStatus.end(), findAccessByID);

                // can't happen
                CC_ASSERT(fromIter != srcStatus.end());
                CC_ASSERT(toIter != dstStatus.end());

                if (!isTransitionStatusDependent(fromIter->access, toIter->access)) {
                    continue;
                }

                auto findBarrierNodeByResID = [resourceID](const Barrier &barrier) { return resourceID == barrier.resourceID; };

                auto srcBarrierIter = srcRearBarriers.empty() ? srcRearBarriers.end() : std::find_if(srcRearBarriers.begin(), srcRearBarriers.end(), findBarrierNodeByResID);
                auto dstBarrierIter = dstFrontBarriers.empty() ? dstFrontBarriers.end() : std::find_if(dstFrontBarriers.begin(), dstFrontBarriers.end(), findBarrierNodeByResID);

                auto dstVertDistribute = [&](uint32_t &vertID, bool depends) {
                    if (depends) {
                        vertID = INVALID_ID;
                    } else if (subToSubDeps) {
                        vertID = dstVert;
                    } else {
                        vertID = isAdjacent ? srcVert : dstVert;
                    }
                };
                if (srcBarrierIter == srcRearBarriers.end()) {
                    auto srcVertID = dstExternalDeps ? INVALID_ID : srcVert;
                    TransitionStatus srcAccess = {srcVertID, fromIter->access};
                    auto dstVertID = dstVert;
                    dstVertDistribute(dstVertID, dstExternalDeps);
                    TransitionStatus dstAccess = {dstVertID, toIter->access};

                    srcRearBarriers.emplace_back(Barrier{
                        resourceID,
                        isAdjacent ? gfx::BarrierType::FULL : gfx::BarrierType::SPLIT_BEGIN,
                        nullptr, // generate later
                        srcAccess,
                        dstAccess,
                    });
                    srcBarrierIter = std::prev(srcRearBarriers.end());
                } else {
                    if (isAdjacent) {
                        auto srcVertID = dstExternalDeps ? INVALID_ID : srcVert;
                        TransitionStatus srcAccess = {srcVertID, fromIter->access};

                        auto dstVertID = dstVert;
                        dstVertDistribute(dstVertID, dstExternalDeps);
                        TransitionStatus dstAccess = {dstVertID, toIter->access};

                        srcBarrierIter->type = gfx::BarrierType::FULL;
                        srcBarrierIter->beginStatus = srcAccess;
                        srcBarrierIter->endStatus = dstAccess;
                    } else {
                        auto &blockBarrier = barrierMap.at(srcPassVert).blockBarrier;
                        auto lastVert = srcBarrierIter->beginStatus.ragVertID;
                        auto blockIter = blockBarrier.rearBarriers.end();
                        if (subToSubDeps) {
                            blockIter = std::find_if(blockBarrier.rearBarriers.begin(), blockBarrier.rearBarriers.end(), findBarrierNodeByResID);
                            lastVert = blockIter->beginStatus.ragVertID;
                        }
                        if (srcVert >= lastVert) {
                            TransitionStatus srcAccess = {srcVert, fromIter->access};
                            if (blockIter != blockBarrier.rearBarriers.end()) {
                                blockIter->beginStatus = srcAccess;
                            }

                            uint32_t siblingPass = lastVert;
                            if (srcVert > lastVert) {
                                auto &siblingPassBarrier = barrierMap[siblingPass].blockBarrier.rearBarriers;
                                auto siblingIter = std::find_if(siblingPassBarrier.begin(), siblingPassBarrier.end(),
                                                                [resourceID](const Barrier &barrier) {
                                                                    return resourceID == barrier.resourceID;
                                                                });
                                CC_ASSERT(siblingIter != siblingPassBarrier.end());
                                siblingPassBarrier.erase(siblingIter);
                            }
                        }
                    }
                }

                if (srcBarrierIter->type == gfx::BarrierType::SPLIT_BEGIN) {
                    auto srcVertID = srcExternalDeps ? INVALID_ID : srcVert;
                    auto dstVertID = dstVert;
                    dstVertDistribute(dstVertID, dstExternalDeps);

                    TransitionStatus srcAccess = {srcVertID, fromIter->access};
                    TransitionStatus dstAccess = {dstVertID, toIter->access};
                    if (dstBarrierIter == dstFrontBarriers.end()) {
                        // if isAdjacent, full barrier already in src rear barriers.
                        if (!isAdjacent) {
                            dstFrontBarriers.emplace_back(Barrier{
                                resourceID,
                                gfx::BarrierType::SPLIT_END,
                                nullptr,
                                srcAccess,
                                dstAccess,
                            });
                        }
                    } else {
                        if (isAdjacent) {
                            // adjacent, barrier should be commit at fromPass, and remove this iter from dstBarriers
                            srcBarrierIter->type = gfx::BarrierType::FULL;
                            srcBarrierIter->beginStatus = srcAccess;
                            srcBarrierIter->endStatus = dstAccess;
                            dstFrontBarriers.erase(dstBarrierIter);
                        } else {
                            // logic but not exec adjacent
                            // and more adjacent(distance from src) than another pass which hold a use of resourceID
                            // replace previous one

                            // 1 --> 2 --> 3
                            //             ↓
                            // 4 --> 5 --> 6

                            // [if] real pass order: 1 - 2 - 4 - 5 - 3 - 6

                            // 2 and 5 read from ResA, 6 writes to ResA
                            // 5 and 6 logically adjacent but not adjacent in execution order.
                            // barrier for ResA between 2 - 6 can be totally replaced by 5 - 6
                            auto &blockBarrier = barrierMap.at(dstPassVert).blockBarrier;
                            auto blockIter = blockBarrier.frontBarriers.end();
                            auto lastVert = dstBarrierIter->endStatus.ragVertID;
                            if (subToSubDeps) {
                                blockIter = std::find_if(blockBarrier.frontBarriers.begin(), blockBarrier.frontBarriers.end(), findBarrierNodeByResID);
                                lastVert = blockIter->endStatus.ragVertID;
                            }
                            if (dstVert <= lastVert) {
                                uint32_t siblingPass = lastVert;
                                dstBarrierIter->endStatus = dstAccess;

                                // remove the further redundant barrier
                                auto &siblingPassBarrier = barrierMap[siblingPass].blockBarrier.frontBarriers;
                                auto siblingIter = std::find_if(siblingPassBarrier.begin(), siblingPassBarrier.end(),
                                                                [resourceID](const Barrier &barrier) {
                                                                    return resourceID == barrier.resourceID;
                                                                });
                                CC_ASSERT(siblingIter != siblingPassBarrier.end());
                                siblingPassBarrier.erase(siblingIter);
                            }
                        }
                    }
                }
            }
        }
    }

    void examine_edge(Edge e, const Graph &g) {
        Vertex from = source(e, g);
        Vertex to = target(e, g);

        // hasSubpass ? fromAccess is a single node with all attachment status stored in 'attachmentStatus'
        //            : fromAccess is head of chain of subpasses, which stores all attachment status in 'attachmentStatus'
        const ResourceAccessNode &fromAccess = get(ResourceAccessGraph::AccessNodeTag{}, g, from);
        const ResourceAccessNode &toAccess = get(ResourceAccessGraph::AccessNodeTag{}, g, to);

        bool isAdjacent = isPassExecAdjecent(from, to);
        std::vector<AccessStatus> commonResources;

        const auto *srcHead = &fromAccess;
        const auto *dstHead = &toAccess;

        bool srcHasSubpass = srcHead->nextSubpass;
        bool dstHasSubpass = dstHead->nextSubpass;
        srcHead = srcHasSubpass ? srcHead->nextSubpass : srcHead;

        std::stack<const ResourceAccessNode *> reverseSubpassQ;
        while (srcHead) {
            reverseSubpassQ.push(srcHead);
            srcHead = srcHead->nextSubpass;
        }
        ccstd::set<uint32_t> subpassResourceSet;
        while (!reverseSubpassQ.empty()) {
            uint32_t srcSubpassIndex = reverseSubpassQ.size() - 1;
            srcHead = reverseSubpassQ.top();
            reverseSubpassQ.pop();
            const auto &fromStatus = srcHead->attachmentStatus;
            std::vector<Barrier> &srcRearBarriers = srcHasSubpass ? barrierMap[from].subpassBarriers[srcSubpassIndex].rearBarriers : barrierMap[from].blockBarrier.rearBarriers;
            uint32_t dstSubpassIndex = 0;
            dstHead = &toAccess;
            bool dstHasSubpass = dstHead->nextSubpass;
            dstHead = dstHasSubpass ? toAccess.nextSubpass : &toAccess;
            while (dstHead) {
                const auto &toStatus = dstHead->attachmentStatus;
                std::vector<Barrier> &dstFrontBarriers = dstHasSubpass ? barrierMap[to].subpassBarriers[dstSubpassIndex].frontBarriers : barrierMap[to].blockBarrier.frontBarriers;
                AccessNodeInfo fromInfo = {fromStatus, srcRearBarriers, from, srcHasSubpass ? static_cast<uint32_t>(reverseSubpassQ.size()) : INVALID_ID};
                AccessNodeInfo toInfo = {toStatus, dstFrontBarriers, to, dstHasSubpass ? dstSubpassIndex : INVALID_ID};
                bool isExecAdjacent = isAdjacent && (!srcHead->nextSubpass && !dstSubpassIndex);
                fillBarrier(g, fromInfo, toInfo, isAdjacent, subpassResourceSet);
                dstHead = dstHead->nextSubpass;
                ++dstSubpassIndex;
            }
        }

        auto &rearBarriers = barrierMap[from].blockBarrier.rearBarriers;
        auto &frontBarriers = barrierMap[from].blockBarrier.frontBarriers;

        uint32_t subpassIdx = 0;
        for (const auto &barriers : barrierMap[from].subpassBarriers) {
            for (const auto &barrier : barriers.rearBarriers) {
                auto resID = barrier.resourceID;
                auto resName = get(ResourceGraph::NameTag{}, resourceGraph, resID);
                auto findBarrierByResID = [resID](const Barrier &barrier) {
                    return barrier.resourceID == resID;
                };
                auto iter = std::find_if(rearBarriers.begin(), rearBarriers.end(), findBarrierByResID);
                auto resFinalPassID = accessRecord.at(resID).currStatus.ragVertID;

                if (resFinalPassID > from) {
                    const auto *dstHead = dstHasSubpass ? toAccess.nextSubpass : &toAccess;
                    const ResourceStatus *dstAccess{nullptr};
                    while (dstHead) {
                        auto iter = std::find_if(dstHead->attachmentStatus.begin(), dstHead->attachmentStatus.end(), [resName](const ResourceStatus &access) {
                            return access.resName == resName;
                        });
                        if (iter != dstHead->attachmentStatus.end()) {
                            dstAccess = &(*iter);
                            break;
                        }
                        dstHead = dstHead->nextSubpass;
                    }

                    if (!dstAccess) {
                        continue;
                    }

                    const auto *srcHead = srcHasSubpass ? fromAccess.nextSubpass : &fromAccess;
                    uint32_t step = 0;
                    while (step <= subpassIdx) {
                        srcHead = srcHead->nextSubpass;
                        ++step;
                    }

                    bool laterUse = false;
                    while (srcHead) {
                        const auto &attachments = srcHead->attachmentStatus;
                        laterUse |= std::any_of(attachments.begin(), attachments.end(), [resName](const ResourceStatus &access) {
                            return access.resName == resName;
                        });
                        srcHead = srcHead->nextSubpass;
                    }

                    // laterUse: in case it's a split begin/end.
                    const auto &srcStatus = laterUse ? barrier.endStatus.access : barrier.beginStatus.access;
                    if (isTransitionStatusDependent(srcStatus, dstAccess->access)) {
                        if (iter == rearBarriers.end()) {
                            auto &collect = rearBarriers.emplace_back(barrier);
                            collect.beginStatus.ragVertID = from;
                            collect.endStatus.ragVertID = isAdjacent ? from : to;
                        } else if (iter->endStatus.ragVertID >= to) {
                            (*iter) = barrier;
                            iter->beginStatus.ragVertID = from;
                            iter->endStatus.ragVertID = isAdjacent ? from : to;
                        }
                    }
                }
            }

            for (const auto &barrier : barriers.frontBarriers) {
                auto resID = barrier.resourceID;
                auto findBarrierByResID = [resID](const Barrier &barrier) {
                    return barrier.resourceID == resID;
                };
                auto resFinalPassID = accessRecord.at(resID).currStatus.ragVertID;
                auto firstMeetIter = std::find_if(frontBarriers.begin(), frontBarriers.end(), findBarrierByResID);
                auto innerResIter = std::find_if(rearBarriers.begin(), rearBarriers.end(), findBarrierByResID);

                if (firstMeetIter == frontBarriers.end() && innerResIter == rearBarriers.end() && resFinalPassID > from) {
                    frontBarriers.emplace_back(barrier);
                }
            }
        }
        subpassIdx++;
    }

    const AccessTable &accessRecord;
    BarrierMap &barrierMap;
    const ResourceGraph &resourceGraph;
    ResourceNames &resourceNames; // record those which been written
    ResourceLifeRecordMap &resourceLifeRecord;
    PmrMap<Vertex, FGRenderPassInfo> &rpInfos;
};

void buildBarriers(FrameGraphDispatcher &fgDispatcher) {
    auto *scratch = fgDispatcher.scratch;
    const auto &renderGraph = fgDispatcher.graph;
    const auto &layoutGraph = fgDispatcher.layoutGraph;
    auto &resourceGraph = fgDispatcher.resourceGraph;
    auto &relationGraph = fgDispatcher.relationGraph;
    auto &externalResMap = fgDispatcher.externalResMap;
    auto &rag = fgDispatcher.resourceAccessGraph;

    // record resource current in-access and out-access for every single node
    if (!fgDispatcher._accessGraphBuilt) {
        const Graphs graphs{renderGraph, resourceGraph, layoutGraph, rag, relationGraph};
        buildAccessGraph(graphs);
        fgDispatcher._accessGraphBuilt = true;
    }

    // found pass id in this map ? barriers you should commit when run into this pass
    // : or no extra barrier needed.
    BarrierMap &batchedBarriers = fgDispatcher.barrierMap;

    {
        // barrier first meet
        // O(N) actually
        ccstd::set<ccstd::pmr::string> firstMeet;
        for (size_t i = 0; i < rag.access.size(); ++i) {
            const auto *status = &rag.access[i];
            for (const auto &attachment : status->attachmentStatus) {
                TransitionStatus lastStatus = {INVALID_ID, {}};
                TransitionStatus currStatus = {static_cast<uint32_t>(i), attachment.access};
                lastStatus.access.range = currStatus.access.range;

                gfx::BarrierType bType = gfx::BarrierType::FULL;
                const auto &resName = attachment.resName;
                auto resourceID = rag.resourceIndex.at(resName);
                const auto &desc = get(ResourceGraph::DescTag{}, resourceGraph, resourceID);
                const auto &traits = get(ResourceGraph::TraitsTag{}, resourceGraph, resourceID);
                if (traits.hasSideEffects()) {
                    const auto &accessFlag = get(ResourceGraph::StatesTag{}, resourceGraph, resourceID).states;
                    if (accessFlag != gfx::AccessFlagBit::NONE) {
                        lastStatus.access.accessFlag = accessFlag;
                        bType = traits.residency == ResourceResidency::BACKBUFFER ? gfx::BarrierType::FULL : gfx::BarrierType::SPLIT_END;
                    }
                    if (!isTransitionStatusDependent(lastStatus.access, currStatus.access)) {
                        continue;
                    }
                }
                if (firstMeet.find(resName) == firstMeet.end()) {
                    firstMeet.emplace(resName);

                    if (batchedBarriers.find(i) == batchedBarriers.end()) {
                        batchedBarriers.emplace(i, BarrierNode{});
                        auto &rpInfo = rag.rpInfos[i].rpInfo;
                        if (rpInfo.subpasses.size() > 1) {
                            batchedBarriers[i].subpassBarriers.resize(rpInfo.subpasses.size());
                        }
                    }

                    auto &blockFrontBarrier = batchedBarriers.at(i).blockBarrier.frontBarriers;
                    auto resIter = std::find_if(blockFrontBarrier.begin(), blockFrontBarrier.end(), [resourceID](const Barrier &barrier) { return barrier.resourceID == resourceID; });
                    if (resIter == blockFrontBarrier.end()) {
                        Barrier firstMeetBarrier{
                            resourceID,
                            gfx::BarrierType::FULL,
                            nullptr,
                            lastStatus,
                            currStatus,
                        };
                        blockFrontBarrier.emplace_back(firstMeetBarrier);
                    }
                    auto *nextStatus = status->nextSubpass;
                    uint32_t index = 0;
                    while (nextStatus) {
                        const auto &subpassStatus = nextStatus->attachmentStatus;
                        auto &subpassBarriers = batchedBarriers.at(i).subpassBarriers;
                        auto &frontBarriers = subpassBarriers[index].frontBarriers;
                        auto iter = std::find_if(subpassStatus.begin(), subpassStatus.end(), [&resName](const ResourceStatus &status) { return status.resName == resName; });
                        if (iter != subpassStatus.end()) {
                            lastStatus.ragVertID = 0xFFFFFFFF;
                            currStatus.ragVertID = index;
                            currStatus.access.accessFlag = iter->access.accessFlag;
                            Barrier firstMeetBarrier{
                                resourceID,
                                bType,
                                nullptr,
                                lastStatus,
                                currStatus,
                            };
                            frontBarriers.emplace_back(firstMeetBarrier);
                            break;
                        }
                        nextStatus = nextStatus->nextSubpass;
                        index++;
                    }
                }
            }
        }
    }

    ResourceNames namesSet;
    {
        // barrier between passes
        BarrierVisitor visitor(resourceGraph, batchedBarriers, namesSet, rag.accessRecord, rag.resourceLifeRecord, rag.rpInfos);
        auto colors = rag.colors(scratch);
        boost::queue<AccessVertex> q;

        boost::breadth_first_visit(
            rag,
            EXPECT_START_ID,
            q,
            visitor,
            get(colors, rag));
    }

    // external res barrier for next frame
    for (const auto &pair : rag.resourceIndex) {
        auto resID = pair.second;
        const auto &resTraits = get(ResourceGraph::TraitsTag{}, resourceGraph, resID);
        auto &rescStates = get(ResourceGraph::StatesTag{}, resourceGraph, resID);

        if (!resTraits.hasSideEffects()) {
            continue;
        }

        bool backBuffer = resTraits.residency == ResourceResidency::BACKBUFFER;
        //bool needNextBarrier = rescStates.states == gfx::AccessFlagBit::NONE;

        const auto &trans = rag.accessRecord.at(resID);
        // persistant resource states cached here
        rescStates.states = trans.currStatus.access.accessFlag;

        if (!backBuffer) {
            continue;
        }

        auto passID = trans.currStatus.ragVertID;
        if (batchedBarriers.find(passID) == batchedBarriers.end()) {
            batchedBarriers.emplace(passID, BarrierNode{});
        }

        Barrier presentBarrier{
            resID,
            gfx::BarrierType::FULL,
            nullptr,
            trans.currStatus,
            {
                INVALID_ID,
                {getRange(rag, pair.first, resourceGraph), gfx::AccessFlagBit::PRESENT},
            },
        };
        rescStates.states = gfx::AccessFlagBit::PRESENT;

        bool hasSubpass = !(batchedBarriers[passID].subpassBarriers.size() <= 1);
        if (hasSubpass) {
            auto &subpassBarriers = batchedBarriers[passID].subpassBarriers;
            for (int i = static_cast<int>(subpassBarriers.size()) - 1; i >= 0; --i) {
                auto findBarrierByResID = [resID](const Barrier &barrier) {
                    return barrier.resourceID == resID;
                };

                const auto &frontBarriers = subpassBarriers[i].frontBarriers;
                auto &rearBarriers = subpassBarriers[i].rearBarriers;
                auto found = std::find_if(frontBarriers.begin(), frontBarriers.end(), findBarrierByResID) != frontBarriers.end() ||
                             std::find_if(rearBarriers.begin(), rearBarriers.end(), findBarrierByResID) != rearBarriers.end();
                if (found) {
                    rearBarriers.push_back(presentBarrier);
                    break;
                }
            }
        } else {
            auto &rearBarriers = batchedBarriers[passID].blockBarrier.rearBarriers;
            rearBarriers.emplace_back(presentBarrier);
        }
    }

    const auto &resDescs = get(ResourceGraph::DescTag{}, resourceGraph);
    auto genGFXBarrier = [&resDescs](std::vector<Barrier> &barriers) {
        for (auto &passBarrier : barriers) {
            const auto &desc = get(resDescs, passBarrier.resourceID);
            if (desc.dimension == ResourceDimension::BUFFER) {
                gfx::BufferBarrierInfo info;
                info.prevAccesses = passBarrier.beginStatus.access.accessFlag;
                info.nextAccesses = passBarrier.endStatus.access.accessFlag;
                const auto &range = passBarrier.endStatus.access.range;
                info.offset = range.firstSlice;
                info.size = range.width;
                info.type = passBarrier.type;
                passBarrier.barrier = gfx::Device::getInstance()->getBufferBarrier(info);
            } else {
                gfx::TextureBarrierInfo info;
                info.prevAccesses = passBarrier.beginStatus.access.accessFlag;
                info.nextAccesses = passBarrier.endStatus.access.accessFlag;
                const auto &range = passBarrier.beginStatus.access.range;
                info.baseMipLevel = range.mipLevel;
                info.levelCount = range.levelCount;
                info.baseSlice = range.firstSlice;
                info.sliceCount = range.numSlices;
                info.type = passBarrier.type;
                passBarrier.barrier = gfx::Device::getInstance()->getTextureBarrier(info);
            }
        }
    };

    // generate gfx barrier
    for (auto &passBarrierInfo : batchedBarriers) {
        auto &passBarrierNode = passBarrierInfo.second;
        genGFXBarrier(passBarrierNode.blockBarrier.frontBarriers);
        genGFXBarrier(passBarrierNode.blockBarrier.rearBarriers);
        for (auto &subpassBarrier : passBarrierNode.subpassBarriers) {
            genGFXBarrier(subpassBarrier.frontBarriers);
            genGFXBarrier(subpassBarrier.rearBarriers);
        }
    }

    {
        for (auto &[vert, fgRenderpassInfo] : rag.rpInfos) {
            auto &colorAttachments = fgRenderpassInfo.rpInfo.colorAttachments;
            for (uint32_t i = 0; i < colorAttachments.size(); ++i) {
                const auto &colorAccess = fgRenderpassInfo.colorAccesses[i];
                auto prevAccess = colorAttachments[i].loadOp != gfx::LoadOp::LOAD ? gfx::AccessFlagBit::NONE : colorAccess.prevAccess;
                auto nextAccess = colorAttachments[i].storeOp != gfx::StoreOp::STORE ? gfx::AccessFlagBit::NONE : colorAccess.nextAccess;
                colorAttachments[i].barrier = getGeneralBarrier(cc::gfx::Device::getInstance(), prevAccess, nextAccess);
            }
            auto &dsAttachment = fgRenderpassInfo.rpInfo.depthStencilAttachment;
            if (dsAttachment.format != gfx::Format::UNKNOWN) {
                const auto &dsAccess = fgRenderpassInfo.dsAccess;
                auto prevAccess = (dsAttachment.depthLoadOp != gfx::LoadOp::LOAD && dsAttachment.stencilLoadOp != gfx::LoadOp::LOAD) ? gfx::AccessFlagBit::NONE : dsAccess.prevAccess;
                auto nextAccess = (dsAttachment.depthStoreOp != gfx::StoreOp::STORE && dsAttachment.stencilStoreOp != gfx::StoreOp::STORE) ? gfx::AccessFlagBit::NONE : dsAccess.nextAccess;
                dsAttachment.barrier = getGeneralBarrier(cc::gfx::Device::getInstance(), prevAccess, nextAccess);
            }
        }
    }
}
#pragma endregion BUILD_BARRIERS

#pragma region PASS_REORDER

struct PassVisitor : boost::dfs_visitor<> {
    using RLGVertex = RelationGraph::vertex_descriptor;
    using RLGEdge = RelationGraph::edge_descriptor;
    using InEdgeRange = std::pair<RelationGraph::in_edge_iterator, RelationGraph::in_edge_iterator>;
    using OutEdgeRange = std::pair<RelationGraph::out_edge_iterator, RelationGraph::out_edge_iterator>;

    PassVisitor(RelationGraph &tcIn, CloseCircuits &circuitsIn) : _relationGraph(tcIn), _circuits(circuitsIn) {}

    void start_vertex(RLGVertex u, const RelationGraph &g) {}

    void discover_vertex(RLGVertex u, const RelationGraph &g) {}

    void examine_edge(RLGEdge e, const RelationGraph &g) {
    }

    void tree_edge(RLGEdge e, const RelationGraph &g) {}

    void back_edge(RLGEdge e, const RelationGraph &g) {}

    void forward_or_cross_edge(RLGEdge e, const RelationGraph &g) {
        // the vertex which:
        // 1. is ancestor of targetID;
        // 2. sourceID is reachable at this specific vert;
        // is where the closed-path started.
        // note that `reachable` may results to multiple paths, choose the shortest one.
        auto sourceID = source(e, g);
        auto targetID = target(e, g);

        using RhsRangePair = std::pair<RelationGraph::in_edge_iterator, InEdgeRange>;

        bool foundIntersect = false;
        std::queue<RhsRangePair> vertQ;
        auto iterPair = in_edges(targetID, g);
        vertQ.emplace(RhsRangePair{iterPair.first, iterPair});

        // from source vertex on this edge back to branch point
        EdgeList rhsPath;
        bool rootEdge = true;
        while (!foundIntersect && !vertQ.empty()) {
            auto rangePair = vertQ.front();
            vertQ.pop();
            auto range = rangePair.second;
            for (auto iter = range.first; iter != range.second; ++iter) {
                auto srcID = source((*iter), g);
                if (sourceID == srcID) {
                    continue;
                }
                auto e = edge(srcID, sourceID, _relationGraph);
                auto recordIter = rootEdge ? iter : rangePair.first;
                if (!e.second) {
                    vertQ.emplace(RhsRangePair{recordIter, in_edges(srcID, g)});
                } else {
                    rhsPath = {(*iter), *recordIter};
                    foundIntersect = true;
                    break;
                }
            }
            rootEdge = false;
        }
        assert(foundIntersect);

        using LhsRangePair = std::pair<RelationGraph::out_edge_iterator, OutEdgeRange>;
        auto branchVert = source(rhsPath.first, g);
        bool found = false;
        std::queue<LhsRangePair> forwardVertQ;
        auto forwardIterPair = out_edges(branchVert, g);
        forwardVertQ.emplace(LhsRangePair{forwardIterPair.first, forwardIterPair});
        EdgeList lhsPath;
        rootEdge = true;
        while (!found && !forwardVertQ.empty()) {
            auto rangePair = forwardVertQ.front();
            forwardVertQ.pop();
            auto range = rangePair.second;
            for (auto iter = range.first; iter != range.second; ++iter) {
                if ((*iter) == rhsPath.first) {
                    continue;
                }
                auto dstID = target((*iter), g);
                auto e = edge(dstID, sourceID, _relationGraph);
                auto recordIter = rootEdge ? iter : rangePair.first;
                if (!e.second) {
                    forwardVertQ.emplace(LhsRangePair{recordIter, out_edges(dstID, g)});
                } else {
                    found = true;
                    lhsPath = {*recordIter, (*iter)};
                    break;
                }
            }
            rootEdge = true;
        }
        assert(found);
        lhsPath.second = e;

        _circuits.emplace_back(CloseCircuit{lhsPath, rhsPath});
    };

private:
    RelationGraph &_relationGraph;
    CloseCircuits &_circuits;
};

// forward (vertex ascending):
// -- true: how much resource this pass writes to, which has an effect of later passes;
// -- false: how much resource this pass reads from, which is dependent from former passes.
auto evaluateHeaviness(const RAG &rag, const ResourceGraph &rescGraph, ResourceAccessGraph::vertex_descriptor vert, bool forward) {
    const ResourceAccessNode &accessNode = get(RAG::AccessNodeTag{}, rag, vert);
    int64_t score = 0;
    bool forceAdjacent = false;
    for (const auto &resc : accessNode.attachmentStatus) {
        int64_t eval = 0;
        auto rescID = rag.resourceIndex.at(resc.resName);
        const ResourceDesc &desc = get(ResourceGraph::DescTag{}, rescGraph, rescID);
        const ResourceTraits &traits = get(ResourceGraph::TraitsTag{}, rescGraph, rescID);

        gfx::MemoryAccessBit substractFilter = forward ? gfx::MemoryAccessBit::READ_ONLY : gfx::MemoryAccessBit::WRITE_ONLY;
        bool readOnly = isReadOnlyAccess(resc.access.accessFlag);
        bool affects = !(forward ^ readOnly);//forward ? readOnly : !readOnly;
        if (affects) {
            // forward calculate write(s), backward calculate read(s).
            continue;
        }

        switch (desc.dimension) {
            case ResourceDimension::BUFFER:
                eval = desc.width;
                break;
            case ResourceDimension::TEXTURE1D:
            case ResourceDimension::TEXTURE2D:
            case ResourceDimension::TEXTURE3D:
                eval = gfx::formatSize(desc.format, desc.width, desc.height, desc.depthOrArraySize);
                break;
        }

        if (traits.residency == ResourceResidency::MEMORYLESS) {
            forceAdjacent = true;
            score = forward ? std::numeric_limits<int64_t>::lowest() : std::numeric_limits<int64_t>::max();
            break;
        }
    }
    return std::make_tuple(forceAdjacent, score);
};

void evaluateAndTryMerge(const RAG &rag, const ResourceGraph &rescGraph, RelationGraph &relationGraph, const RelationGraph &relationGraphTc, const RelationVerts &lhsVerts, const RelationVerts &rhsVerts) {
    assert(lhsVerts.size() >= 2);
    assert(rhsVerts.size() >= 2);

    auto evaluate = [&rag, &rescGraph, &relationGraph](RelationVert vert, bool forward) {
        auto ragVert = get(RelationGraph::DescIDTag{}, relationGraph, vert);
        return evaluateHeaviness(rag, rescGraph, ragVert, forward);
    };

    if (lhsVerts.size() == 2 || rhsVerts.size() == 2) {
        /*
               1 ----------- 2
                \       __--/
                 3 --``
            no extra choice, only 1 - 3 - 2
        */
        const RelationVerts *shorterPath = lhsVerts.size() == 2 ? &lhsVerts : &rhsVerts;
        remove_edge((*shorterPath)[0], (*shorterPath)[1], relationGraph);
    } else {
        // fist and last joint pass in this circuit don't get involved in reorder.
        auto firstLhsNode = lhsVerts[1];
        auto lastLhsNode = lhsVerts[lhsVerts.size() - 2];

        const auto &lhsBackwardStatus = evaluate(firstLhsNode, false);
        bool lhsAdjacentToStart = std::get<0>(lhsBackwardStatus);
        const auto &lhsForwardStatus = evaluate(lastLhsNode, true);
        bool lhsAdjacentToEnd = std::get<0>(lhsForwardStatus);

        auto firstRhsNode = rhsVerts[1];
        auto lastRhsNode = rhsVerts[rhsVerts.size() - 2];

        const auto &rhsBackwardStatus = evaluate(firstRhsNode, true);
        bool rhsAdjacentToStart = std::get<0>(rhsBackwardStatus);
        const auto &rhsForwardStatus = evaluate(lastRhsNode, false);
        bool rhsAdjacentToEnd = std::get<0>(rhsForwardStatus);

        if (lhsAdjacentToStart || rhsAdjacentToEnd || lhsAdjacentToEnd || rhsAdjacentToStart) {
            const RelationVerts *formerPath = &lhsVerts;
            const RelationVerts *latterPath = &rhsVerts;
            if (rhsAdjacentToStart || lhsAdjacentToEnd) {
                swap(formerPath, latterPath);
            }

            remove_edge((*latterPath)[0], (*latterPath)[1], relationGraph);
            remove_edge((*formerPath)[formerPath->size() - 2], (*formerPath)[formerPath->size() - 1], relationGraph);

            tryAddEdge((*formerPath)[formerPath->size() - 2], (*latterPath)[1], relationGraph);
        }

        assert(lhsVerts.size() >= 3 && rhsVerts.size() >= 3);
        constexpr int64_t score = std::numeric_limits<int64_t>::lowest();
        ccstd::vector<std::queue<RelationEdge>> candidateSections;
        std::queue<RelationEdge> lhsSection;
        for (size_t i = 1; i < lhsVerts.size(); ++i) {
            auto tryE = edge(lhsVerts[i], lhsVerts[i - 1], relationGraphTc);
            auto tryRE = edge(lhsVerts[i - 1], lhsVerts[i], relationGraphTc);
            // check if original reachable
            if (!tryE.second && !tryRE.second) {
                remove_edge(lhsVerts[i - 1], lhsVerts[i], relationGraph);
                candidateSections.emplace_back(lhsSection);
                std::queue<RelationEdge> clearQ;
                lhsSection.swap(clearQ);
            }
            auto e = edge(lhsVerts[i], lhsVerts[i - 1], relationGraph);
            // verts comes in order, so either real edge exist or logic edge is added.
            CC_ASSERT(e.second);

            lhsSection.emplace(e.first);
        }
        if (candidateSections.empty()) {
            // if this one is a tight edge(no logic edge, dependent from one to its next),
            // keep this whole chain as a candidate.
            remove_edge(lhsVerts[0], lhsVerts[1], relationGraph);
            remove_edge(lhsVerts[lhsVerts.size() - 2], lhsVerts[lhsVerts.size() - 1], relationGraph);
            candidateSections.emplace_back(std::move(lhsSection));
        }

        std::queue<RelationEdge> rhsSection;
        for (size_t i = 1; i < rhsVerts.size(); ++i) {
            auto tryE = edge(rhsVerts[i], rhsVerts[i - 1], relationGraphTc);
            auto tryRE = edge(rhsVerts[i - 1], rhsVerts[i], relationGraphTc);
            if (!tryE.second && !tryRE.second) {
                remove_edge(rhsVerts[i - 1], rhsVerts[i], relationGraph);
                candidateSections.emplace_back(rhsSection);
                std::queue<RelationEdge> clearQ;
                rhsSection.swap(clearQ);
            }
            auto e = edge(rhsVerts[i], rhsVerts[i - 1], relationGraph);
            // verts comes in order, so either real edge exist or logic edge is added.
            CC_ASSERT(e.second);
            rhsSection.emplace(e.first);
        }

        // lhs verts already put in.
        if (candidateSections.size() == 1) {
            remove_edge(rhsVerts[0], rhsVerts[1], relationGraph);
            remove_edge(rhsVerts[rhsVerts.size() - 2], rhsVerts[rhsVerts.size() - 1], relationGraph);
            candidateSections.emplace_back(std::move(rhsSection));
        }

        assert(candidateSections.size() >= 2);

        ScoreMap scMap;
        auto tailVert = lhsVerts[0];
        while (!candidateSections.empty()) {
            int64_t lightest = std::numeric_limits<int64_t>::max();
            uint32_t index = 0;
            for (size_t i = 0; i < candidateSections.size(); ++i) {
                auto e = candidateSections[i].front();
                auto srcVert = source(e, relationGraph);
                auto dstVert = target(e, relationGraph);
                int64_t srcBackwardScore = 0;
                int64_t dstForwardScore = 0;
                if (scMap.find(srcVert) == scMap.end()) {
                    auto res = evaluate(srcVert, false);
                    srcBackwardScore = std::get<1>(res);
                    res = evaluate(srcVert, true);
                    auto srcForwardScore = std::get<1>(res);
                    scMap.emplace(srcVert, std::pair<int64_t, int64_t>(srcBackwardScore, srcForwardScore));
                } else {
                    srcBackwardScore = std::get<0>(scMap.at(srcVert));
                }
                if (scMap.find(dstVert) == scMap.end()) {
                    auto res = evaluate(dstVert, false);
                    dstForwardScore = std::get<1>(res);
                    res = evaluate(dstVert, true);
                    auto dstBackwardScore = std::get<1>(res);
                    scMap.emplace(dstVert, std::pair<int64_t, int64_t>(dstBackwardScore, dstForwardScore));
                } else {
                    dstForwardScore = std::get<1>(scMap.at(dstVert));
                }

                // we are in a simple path, so all the "input(this path)" resource of this path come from the first vertex,
                // all the "output(this path)" come to the last vertex, other resources are "internally(this path)" produced and destroyed.
                // so only input of first vertex and output of last vertex are taken into account.
                // [simple path]: path without diverged edges.
                auto score = dstForwardScore - srcBackwardScore;
                if (lightest > score) {
                    lightest = score;
                    index = i;
                }
            }
            auto e = candidateSections[index].front();
            candidateSections[index].pop();
            if (candidateSections[index].empty()) {
                auto iter = candidateSections.begin();
                std::advance(iter, index);
                candidateSections.erase(iter);
            }
            auto srcVert = source(e, relationGraph);
            auto dstVert = target(e, relationGraph);
            tryAddEdge(tailVert, srcVert, relationGraph);
            tailVert = dstVert;
        }

        tryAddEdge(tailVert, lhsVerts.back(), relationGraph);
    }
}

// return : can be further reduced?
bool reduce(const RAG &rag, const ResourceGraph &rescGraph, RelationGraph &relationGraph, RelationGraph &relationGraphTc, const CloseCircuit &circuit) {
    auto checkPath = [&relationGraph](std::stack<RelationGraph::vertex_descriptor> &vertices, RelationGraph::vertex_descriptor endVert, RelationVerts &stackTrace) {
        bool simpleGraph = true;
        while (!vertices.empty()) {
            auto vert = vertices.top();
            vertices.pop();
            stackTrace.emplace_back(vert);

            if (endVert == vert) {
                break;
            }

            if (out_degree(vert, relationGraph) > 1) {
                simpleGraph = false;
                break;
            }
            auto r = out_edges(vert, relationGraph);
            for (auto rIter = r.first; rIter != r.second; ++rIter) {
                auto dstID = target(*rIter, relationGraph);
                vertices.push(dstID);
            }
            if (r.first == r.second) {
                stackTrace.pop_back();
            }
        }
        return simpleGraph;
    };

    // check if there is a sub branch on lhs
    auto lhsEdges = circuit.first;
    auto startVert = target(lhsEdges.first, relationGraph);
    auto endVert = source(lhsEdges.second, relationGraph);

    std::stack<RelationGraph::vertex_descriptor> vertices;
    vertices.emplace(startVert);

    RelationVerts lhsVisited;
    auto branchStartVert = source(lhsEdges.first, relationGraph);
    auto branchEndVert = target(lhsEdges.second, relationGraph);
    lhsVisited.push_back(branchStartVert);
    // check if there is a branch on lhs path
    if (!checkPath(vertices, endVert, lhsVisited)) {
        return false;
    }
    lhsVisited.push_back(branchEndVert);
    // if it's a simple graph, lhs path must can be dfs to the end at the first time.
    assert(vertices.empty());

    auto rhsEdges = circuit.second;
    startVert = target(rhsEdges.first, relationGraph);
    endVert = source(rhsEdges.second, relationGraph);
    vertices.emplace(startVert);

    RelationVerts rhsVisited;
    rhsVisited.push_back(branchStartVert);
    if (!checkPath(vertices, endVert, rhsVisited)) {
        return false;
    }
    rhsVisited.push_back(branchEndVert);

    // merge this circuit
    // from
    /*          2 - 3 - 4
              /           \
            1               8
              \           /
                5 - 6 - 7

        to
            1 - A - B - 8 or 1 - B - A -8 depends on algorithm

            ${A} : 2 - 3 - 4
            ${B} : 5 - 6 - 7
        */

    evaluateAndTryMerge(rag, rescGraph, relationGraph, relationGraphTc, lhsVisited, rhsVisited);

    return true;
}

template <typename RelationGraph, typename TargetGraph>
void applyRelation(RelationGraph &relationGraph, const TargetGraph &targetGraph) {
    CC_EXPECTS(relationGraph.vertices.size() == targetGraph.vertices.size());

    // remove all edges
    for (auto vert : targetGraph.vertices) {
        clear_in_edges(vert, targetGraph);
        clear_out_edges(vert, targetGraph);
    }

    for (auto vert : relationGraph.vertices) {
        auto inEdges = in_edges(vert, relationGraph);
        for (auto e : makeRange(inEdges)) {
            auto srcVert = source(e, relationGraph);
            // auto checkEdge = edge(srcVert, vert, targetGraph);
            add_edge(srcVert, vert, targetGraph);
        }
    }
}

void passReorder(FrameGraphDispatcher &fgDispatcher) {
    auto *scratch = fgDispatcher.scratch;
    const auto &renderGraph = fgDispatcher.graph;
    const auto &layoutGraph = fgDispatcher.layoutGraph;
    auto &resourceGraph = fgDispatcher.resourceGraph;
    auto &relationGraph = fgDispatcher.relationGraph;
    auto &rag = fgDispatcher.resourceAccessGraph;

    if (!fgDispatcher._accessGraphBuilt) {
        const Graphs graphs{renderGraph, resourceGraph, layoutGraph, rag, relationGraph};
        buildAccessGraph(graphs);
        fgDispatcher._accessGraphBuilt = true;
    }

    {
        // determine do mem saving how many times
        RelationGraph relationGraphTc(fgDispatcher.get_allocator());
        boost::transitive_closure(relationGraph, relationGraphTc);

        CloseCircuits circuits;
        std::vector<RAG::edge_descriptor> crossEdges;
        PassVisitor visitor(relationGraphTc, circuits);
        auto colors = relationGraph.colors(scratch);
        boost::depth_first_search(relationGraph, visitor, get(colors, relationGraph));

        float percent = 0.0F;
        uint32_t count = 0;
        auto total = circuits.size();

        float memsavePercent = 1.0F - fgDispatcher._paralellExecWeight;
        for (auto iter = circuits.begin(); (iter != circuits.end()) && (percent < memsavePercent);) {
            bool reduced = reduce(rag, resourceGraph, relationGraph, relationGraphTc, (*iter));
            if (reduced) {
                ++count;
                iter = circuits.erase(iter);
                percent = count / static_cast<float>(total);
            } else {
                ++iter;
            }
        }

        // topological sort
        rag.topologicalOrder.clear();
        bool empty = relationGraph._vertices.empty();
        ScoreMap scoreMap;
        RelationVerts candidates;
        candidates.push_back(EXPECT_START_ID);

        std::vector<RelationVert> candidateBuffer;
        uint32_t coloredVerts = 0;
        while (coloredVerts < relationGraph._vertices.size()) {
            // decreasing order, pop back from vector, push into queue, then it's ascending order.
            std::sort(candidates.begin(), candidates.end(), [&](RelationVert lhsVert, RelationVert rhsVert) {
                int64_t lhsForwardScore{0};
                int64_t rhsForwardScore{0};
                int64_t lhsBackwardScore{0};
                int64_t rhsBackwardScore{0};
                if (scoreMap.find(lhsVert) == scoreMap.end()) {
                    auto lhsRagVert = get(RelationGraph::DescIDTag{}, relationGraph, lhsVert);
                    const auto &lhsForwardStatus = evaluateHeaviness(rag, resourceGraph, lhsRagVert, true);
                    lhsForwardScore = get<1>(lhsForwardStatus);
                    const auto &lhsBackwardStatus = evaluateHeaviness(rag, resourceGraph, lhsRagVert, false);
                    lhsBackwardScore = get<1>(lhsBackwardStatus);
                    scoreMap.emplace(lhsVert, std::pair<int64_t, int64_t>{lhsBackwardScore, lhsForwardScore});
                } else {
                    lhsBackwardScore = scoreMap[lhsVert].first;
                    lhsForwardScore = scoreMap[lhsVert].second;
                }

                if (scoreMap.find(rhsVert) == scoreMap.end()) {
                    auto rhsRagVert = get(RelationGraph::DescIDTag{}, relationGraph, rhsVert);
                    const auto &rhsForwardStatus = evaluateHeaviness(rag, resourceGraph, rhsRagVert, true);
                    rhsForwardScore = get<1>(rhsForwardStatus);
                    const auto &rhsBackwardStatus = evaluateHeaviness(rag, resourceGraph, rhsRagVert, false);
                    rhsBackwardScore = get<1>(rhsBackwardStatus);
                    scoreMap.emplace(rhsVert, std::pair<int64_t, int64_t>{rhsBackwardScore, rhsForwardScore});
                } else {
                    rhsBackwardScore = scoreMap[rhsVert].first;
                    rhsForwardScore = scoreMap[rhsVert].second;
                }
                return lhsBackwardScore - lhsForwardScore > rhsBackwardScore - rhsForwardScore;
            });

            const auto vert = candidates.back();
            candidates.pop_back();

            auto ragVert = get(RelationGraph::DescIDTag{}, relationGraph, vert);
            rag.topologicalOrder.emplace_back(ragVert);
            if (!candidateBuffer.empty()) {
                candidates.insert(candidates.end(), candidateBuffer.begin(), candidateBuffer.end());
                candidateBuffer.clear();
            }

            for (const auto nextGeneration : makeRange(out_edges(vert, relationGraph))) {
                auto targetID = target(nextGeneration, relationGraph);
                if (in_degree(targetID, relationGraph) == 1) {
                    candidateBuffer.emplace_back(targetID);
                }
            }

            auto deprecatedEdges = out_edges(vert, relationGraph);
            for (auto iter = deprecatedEdges.first; iter < deprecatedEdges.second;) {
                remove_edge(*iter, relationGraph);
                deprecatedEdges = out_edges(vert, relationGraph);
                iter = deprecatedEdges.first;
            }

            if (candidates.empty()) {
                candidates.insert(candidates.end(), candidateBuffer.begin(), candidateBuffer.end());
                candidateBuffer.clear();
            }

            coloredVerts++;
        }

        // remove all edges
        for (auto vert : makeRange(vertices(rag))) {
            clear_in_edges(vert, rag);
            clear_out_edges(vert, rag);
        }

        // apply relation
        for (auto rlgVert : makeRange(vertices(relationGraph))) {
            auto ragVert = get(RelationGraph::DescIDTag{}, relationGraph, rlgVert);
            auto inEdges = in_edges(rlgVert, relationGraph);
            for (auto e : makeRange(inEdges)) {
                auto srcRlgVert = source(e, relationGraph);
                auto srcRagVert = get(RelationGraph::DescIDTag{}, relationGraph, srcRlgVert);
                add_edge(srcRagVert, ragVert, rag);
            }
        }
    }
}

#pragma endregion PASS_REORDER

void memoryAliasing(FrameGraphDispatcher &fgDispatcher) {
}

#pragma region assisstantFuncDefinition
template <typename Graph>
bool tryAddEdge(uint32_t srcVertex, uint32_t dstVertex, Graph &graph) {
    auto e = edge(srcVertex, dstVertex, graph);
    if (!e.second) {
        auto res = add_edge(srcVertex, dstVertex, graph);
        CC_ENSURES(res.second);
        return true;
    }
    return false;
}

bool isTransitionStatusDependent(const AccessStatus &lhs, const AccessStatus &rhs) {
    return !(isReadOnlyAccess(lhs.accessFlag) && isReadOnlyAccess(rhs.accessFlag));
}

auto mapTextureFlags(ResourceFlags flags) {
    gfx::TextureUsage usage = gfx::TextureUsage::NONE;
    if ((flags & ResourceFlags::SAMPLED) != ResourceFlags::NONE) {
        usage |= gfx::TextureUsage::SAMPLED;
    }
    if ((flags & ResourceFlags::STORAGE) != ResourceFlags::NONE) {
        usage |= gfx::TextureUsage::STORAGE;
    }
    if ((flags & ResourceFlags::SHADING_RATE) != ResourceFlags::NONE) {
        usage |= gfx::TextureUsage::SHADING_RATE;
    }
    if ((flags & ResourceFlags::COLOR_ATTACHMENT) != ResourceFlags::NONE) {
        usage |= gfx::TextureUsage::COLOR_ATTACHMENT;
    }
    if ((flags & ResourceFlags::DEPTH_STENCIL_ATTACHMENT) != ResourceFlags::NONE) {
        usage |= gfx::TextureUsage::DEPTH_STENCIL_ATTACHMENT;
    }
    if ((flags & ResourceFlags::INPUT_ATTACHMENT) != ResourceFlags::NONE) {
        usage |= gfx::TextureUsage::INPUT_ATTACHMENT;
    }
    return usage;
}

auto getResourceStatus(PassType passType, const PmrString &name, gfx::MemoryAccess memAccess, gfx::ShaderStageFlags visibility, const ResourceGraph &resourceGraph, bool rasterized) {
    ResourceUsage usage;
    gfx::ShaderStageFlags vis{gfx::ShaderStageFlags::NONE};
    vis |= visibility;
    gfx::AccessFlags accessFlag;
    auto vertex = resourceGraph.valueIndex.at(name);
    const auto &desc = get(ResourceGraph::DescTag{}, resourceGraph, vertex);
    if (desc.dimension == ResourceDimension::BUFFER) {
        gfx::BufferUsage bufferUsage{gfx::BufferUsage::NONE};
        // copy is not included in this logic because copy can be set TRANSFER_xxx directly.
        if (gfx::hasFlag(memAccess, gfx::MemoryAccessBit::WRITE_ONLY)) {
            bufferUsage = gfx::BufferUsage::STORAGE;
        }

        if (gfx::hasFlag(memAccess, gfx::MemoryAccessBit::READ_ONLY)) {
            bool uniformFlag = (desc.flags & ResourceFlags::UNIFORM) != ResourceFlags::NONE;
            bool storageFlag = (desc.flags & ResourceFlags::STORAGE) != ResourceFlags::NONE;

            // CC_EXPECTS(uniformFlag ^ storageFlag);
            //  uniform or read-only storage buffer
            bufferUsage = uniformFlag ? gfx::BufferUsage::UNIFORM : gfx::BufferUsage::STORAGE;
        }

        if (passType == PassType::COMPUTE) {
            vis |= gfx::ShaderStageFlagBit::COMPUTE;
        }

        // those buffers not found in descriptorlayout but appear here,
        // can and only can be VERTEX/INDEX/INDIRECT BUFFER,
        // only copy pass is allowed.
        if (vis == gfx::ShaderStageFlags::NONE) {
            CC_EXPECTS(passType == PassType::COPY);
        }
        usage = bufferUsage;
        accessFlag = gfx::getAccessFlags(bufferUsage, gfx::MemoryUsage::DEVICE, memAccess, vis);
    } else {
        gfx::TextureUsage texUsage = gfx::TextureUsage::NONE;

        // TODO(Zeqiang): visbility of slot name "_" not found
        if(visibility == gfx::ShaderStageFlags::NONE) {
            vis = gfx::ShaderStageFlags::FRAGMENT;
        }

         if (memAccess == gfx::MemoryAccess::READ_ONLY) {
            if ((desc.flags & ResourceFlags::INPUT_ATTACHMENT) != ResourceFlags::NONE && rasterized) {
                texUsage |= (mapTextureFlags(desc.flags) & (gfx::TextureUsage::COLOR_ATTACHMENT | gfx::TextureUsage::DEPTH_STENCIL_ATTACHMENT | gfx::TextureUsage::INPUT_ATTACHMENT));
            } else {
                texUsage |= (mapTextureFlags(desc.flags) & (gfx::TextureUsage::SAMPLED | gfx::TextureUsage::STORAGE | gfx::TextureUsage::SHADING_RATE));
            }
        } else {
            texUsage |= (mapTextureFlags(desc.flags) & (gfx::TextureUsage::COLOR_ATTACHMENT | gfx::TextureUsage::DEPTH_STENCIL_ATTACHMENT | gfx::TextureUsage::STORAGE));
        }

        usage = texUsage;
        accessFlag = gfx::getAccessFlags(texUsage, memAccess, vis);
        CC_ASSERT(accessFlag != cc::gfx::INVALID_ACCESS_FLAGS);
    }

    return std::make_tuple(vis, usage, accessFlag);
}

void addAccessStatus(RAG &rag, const ResourceGraph &rg, ResourceAccessNode &node, const ViewStatus &status) {
    const auto &[name, accessFlag, range] = status;
    uint32_t rescID = rg.valueIndex.at(name);
    const auto &resourceDesc = get(ResourceGraph::DescTag{}, rg, rescID);
    const auto &traits = get(ResourceGraph::TraitsTag{}, rg, rescID);

    if (!isReadOnlyAccess(accessFlag)) {
        rag.writtenResource.emplace(name);
    }

    rag.resourceIndex[name] = rescID;

    node.attachmentStatus.emplace_back(ResourceStatus{
        name,
        range,
        accessFlag,
    });
}

AccessVertex dependencyCheck(RAG &rag, AccessVertex curVertID, const ResourceGraph &rg, const ViewStatus &viewStatus) {
    const auto &[name, accessFlag, range] = viewStatus;
    auto &accessRecord = rag.accessRecord;

    bool readOnly = isReadOnlyAccess(accessFlag);

    AccessVertex lastVertID = INVALID_ID;
    CC_EXPECTS(rag.resourceIndex.find(name) != rag.resourceIndex.end());
    auto resourceID = rag.resourceIndex[name];
    bool isExternalPass = get(get(ResourceGraph::TraitsTag{}, rg), resourceID).hasSideEffects();
    auto iter = accessRecord.find(resourceID);
    if (iter == accessRecord.end()) {
        accessRecord.emplace(
            resourceID,
            ResourceTransition{
                {curVertID, range, accessFlag},
                {curVertID, range, accessFlag}});
        if (isExternalPass) {
            rag.leafPasses[curVertID] = LeafStatus{true, isReadOnlyAccess(accessFlag)};
        }
    } else {
        ResourceTransition &trans = iter->second;
        auto &currAccessStatus = trans.currStatus;
        auto lastReadOnly = isReadOnlyAccess(currAccessStatus.access.accessFlag) && (currAccessStatus.access.accessFlag != gfx::AccessFlagBit::NONE);
        if (readOnly && lastReadOnly) {
            if (isExternalPass) {
                // only external res will be manually record here, leaf pass with transient resource will be culled by default,
                // those leaf passes with ALL read access on external(or with transients) res can be culled.
                rag.leafPasses[curVertID].needCulling &= isReadOnlyAccess(accessFlag);

                // current READ, no WRITE before in this frame, it's expected to be external.
                bool dirtyExternalRes = trans.lastStatus.ragVertID == INVALID_ID;
                if (!dirtyExternalRes) {
                    tryAddEdge(EXPECT_START_ID, curVertID, rag);
                    if (rag.leafPasses.find(EXPECT_START_ID) != rag.leafPasses.end()) {
                        rag.leafPasses.erase(EXPECT_START_ID);
                    }
                }
            } else {
                // 2(r) read from 1(w), 3(r) read from 1(w), current set to 3
                tryAddEdge(trans.lastStatus.ragVertID, curVertID, rag);
                if (rag.leafPasses.find(trans.lastStatus.ragVertID) != rag.leafPasses.end()) {
                    rag.leafPasses.erase(trans.lastStatus.ragVertID);
                }
            }
            trans.currStatus = {curVertID, range, accessFlag};
            lastVertID = trans.lastStatus.ragVertID;
        } else {
            // avoid subpass self depends
            lastVertID = trans.currStatus.ragVertID;
            trans.lastStatus = trans.currStatus;
            trans.currStatus = {curVertID, range, accessFlag};
            if (trans.currStatus.ragVertID != curVertID) {
                if (rag.leafPasses.find(trans.lastStatus.ragVertID) != rag.leafPasses.end()) {
                    rag.leafPasses.erase(trans.lastStatus.ragVertID);
                }
                if (rag.leafPasses.find(curVertID) != rag.leafPasses.end()) {
                    // only write into externalRes counts
                    if (isExternalPass) {
                        // same as above
                        rag.leafPasses[curVertID].needCulling &= isReadOnlyAccess(accessFlag);
                    }
                }

                lastVertID = trans.lastStatus.ragVertID;
            }
        }
    }
    return lastVertID;
}

gfx::ShaderStageFlagBit getVisibilityByDescName(const RenderGraph &renderGraph, const LGD &lgd, uint32_t passID, const PmrString &resName) {
    auto iter = lgd.attributeIndex.find(resName);
    if (iter == lgd.attributeIndex.end()) {
        iter = lgd.constantIndex.find(resName);
        if (iter == lgd.constantIndex.end()) {
            // resource not in descriptor: eg. input or output attachment.
            return gfx::ShaderStageFlagBit::NONE;
        }
    }
    auto slotID = iter->second;

    auto layoutName = get(RenderGraph::LayoutTag{}, renderGraph, passID);
    auto layoutID = locate(LayoutGraphData::null_vertex(), layoutName, lgd);
    const auto &layout = get(LayoutGraphData::LayoutTag{}, lgd, layoutID);
    for (const auto &pair : layout.descriptorSets) {
        for (const auto &block : pair.second.descriptorSetLayoutData.descriptorBlocks) {
            for (const auto &descriptor : block.descriptors) {
                if (descriptor.descriptorID.value == slotID.value) {
                    return block.visibility;
                }
            }
        }
    }

    // unreachable
    CC_EXPECTS(false);
    return gfx::ShaderStageFlagBit::NONE;
};

bool checkRasterViews(const Graphs &graphs, uint32_t vertID, uint32_t passID, PassType passType, ResourceAccessNode &node, const RasterViewsMap &rasterViews) {
    const auto &[renderGraph, resourceGraph, layoutGraphData, resourceAccessGraph, relationGraph] = graphs;
    bool dependent = false;

    for (const auto &pair : rasterViews) {
        const auto &rasterView = pair.second;
        auto access = toGfxAccess(rasterView.accessType);
        gfx::ShaderStageFlagBit tryGotVis = getVisibilityByDescName(renderGraph, layoutGraphData, passID, pair.second.slotName);
        tryGotVis |= pair.second.shaderStageFlags;
        const auto &[vis, usage, accessFlag] = getResourceStatus(passType, pair.first, access, tryGotVis, resourceGraph, true);
        ResourceRange range{};
        const auto &resDesc = get(ResourceGraph::DescTag{}, resourceGraph, resourceGraph.valueIndex.at(pair.first));
        if (resDesc.dimension == ResourceDimension::BUFFER) {
            range.width = resDesc.depthOrArraySize;
            range.firstSlice = resDesc.width;
        } else {
            range.width = resDesc.width;
            range.height = resDesc.height;
            range.firstSlice = 0;
            range.numSlices = resDesc.depthOrArraySize;
            range.mipLevel = 0;
            range.levelCount = resDesc.mipLevels;
            range.planeSlice = 0;
        }

        ViewStatus viewStatus{pair.first, accessFlag, range};
        addAccessStatus(resourceAccessGraph, resourceGraph, node, viewStatus);
        auto lastVertId = dependencyCheck(resourceAccessGraph, vertID, resourceGraph, viewStatus);
        if (lastVertId != INVALID_ID && lastVertId != vertID) {
            tryAddEdge(lastVertId, vertID, resourceAccessGraph);
            tryAddEdge(lastVertId, vertID, relationGraph);
            dependent = true;
        }
    }

    // sort for vector intersection
    std::sort(node.attachmentStatus.begin(), node.attachmentStatus.end(), [](const ResourceStatus &lhs, const ResourceStatus &rhs) { return lhs.resName < rhs.resName; });

    return dependent;
}

bool checkComputeViews(const Graphs &graphs, uint32_t vertID, uint32_t passID, PassType passType, ResourceAccessNode &node, const ComputeViewsMap &computeViews) {
    const auto &[renderGraph, resourceGraph, layoutGraphData, resourceAccessGraph, relationGraph] = graphs;
    bool dependent = false;

    for (const auto &pair : computeViews) {
        const auto &values = pair.second;
        for (const auto &computeView : values) {
            auto access = toGfxAccess(computeView.accessType);
            gfx::ShaderStageFlagBit tryGotVis = gfx::ShaderStageFlagBit::NONE;
            for (const auto &view : pair.second) {
                tryGotVis |= getVisibilityByDescName(renderGraph, layoutGraphData, passID, view.name);
                tryGotVis |= view.shaderStageFlags;
            }
            const auto &[vis, usage, accessFlag] = getResourceStatus(passType, pair.first, access, tryGotVis, resourceGraph, false);
            ResourceRange range{};
            const auto &resDesc = get(ResourceGraph::DescTag{}, resourceGraph, resourceAccessGraph.resourceIndex.at(pair.first));
            if (resDesc.dimension == ResourceDimension::BUFFER) {
                range.width = resDesc.depthOrArraySize;
                range.firstSlice = resDesc.width;
            } else {
                range.width = resDesc.width;
                range.height = resDesc.height;
                range.firstSlice = 0;
                range.numSlices = resDesc.depthOrArraySize;
                range.mipLevel = 0;
                range.levelCount = resDesc.mipLevels;
                range.planeSlice = 0;
            }

            ViewStatus viewStatus{pair.first, accessFlag, range};
            addAccessStatus(resourceAccessGraph, resourceGraph, node, viewStatus);
            auto lastVertId = dependencyCheck(resourceAccessGraph, vertID, resourceGraph, viewStatus);
            if (lastVertId != INVALID_ID && lastVertId != vertID) {
                tryAddEdge(lastVertId, vertID, resourceAccessGraph);
                tryAddEdge(lastVertId, vertID, relationGraph);
                dependent = true;
            }
        }
    }

    // sort for vector intersection
    std::sort(node.attachmentStatus.begin(), node.attachmentStatus.end(), [](const ResourceStatus &lhs, const ResourceStatus &rhs) { return lhs.resName < rhs.resName; });

    return dependent;
}

void fillRenderPassInfo(const RasterView &view, gfx::RenderPassInfo &rpInfo, uint32_t index, const ResourceDesc &viewDesc) {
    if (view.attachmentType != AttachmentType::DEPTH_STENCIL) {
        auto &colorAttachment = rpInfo.colorAttachments[index];
        colorAttachment.format = viewDesc.format;
        colorAttachment.loadOp = view.loadOp;
        colorAttachment.storeOp = view.storeOp;
        colorAttachment.sampleCount = viewDesc.sampleCount;
        colorAttachment.isGeneralLayout = gfx::hasFlag(viewDesc.textureFlags, gfx::TextureFlags::GENERAL_LAYOUT);
        // colorAttachment.barrier = getGeneralBarrier(gfx::Device::getInstance(), view, prevAccess, nextAccess);
    } else {
        auto &depthStencilAttachment = rpInfo.depthStencilAttachment;
        depthStencilAttachment.format = viewDesc.format;
        depthStencilAttachment.depthLoadOp = view.loadOp;
        depthStencilAttachment.depthStoreOp = view.storeOp;
        depthStencilAttachment.stencilLoadOp = view.loadOp;
        depthStencilAttachment.stencilStoreOp = view.storeOp;
        depthStencilAttachment.sampleCount = viewDesc.sampleCount;
        depthStencilAttachment.isGeneralLayout = gfx::hasFlag(viewDesc.textureFlags, gfx::TextureFlags::GENERAL_LAYOUT);
        // depthStencilAttachment.barrier = getGeneralBarrier(gfx::Device::getInstance(), view, prevAccess, nextAccess);
    }
}

void processRasterPass(const Graphs &graphs, uint32_t passID, const RasterPass &pass) {
    const auto &[renderGraph, resourceGraph, layoutGraphData, resourceAccessGraph, relationGraph] = graphs;

    auto vertID = add_vertex(resourceAccessGraph, passID);
    auto rlgVertID = add_vertex(relationGraph, vertID);
    CC_EXPECTS(static_cast<uint32_t>(rlgVertID) == static_cast<uint32_t>(vertID));
    auto &node = get(RAG::AccessNodeTag{}, resourceAccessGraph, vertID);
    const auto &subpasses = pass.subpassGraph.subpasses;
    bool hasSubpass = !subpasses.empty();
    auto &fgRenderpassInfo = resourceAccessGraph.rpInfos.emplace(vertID, FGRenderPassInfo{}).first->second;
    auto &rpInfo = fgRenderpassInfo.rpInfo;
    if (!hasSubpass) {
        auto &rag = resourceAccessGraph;
        auto size = std::count_if(pass.rasterViews.begin(), pass.rasterViews.end(), [](const auto &pair) {
            return pair.second.attachmentType != AttachmentType::DEPTH_STENCIL;
        });
        rpInfo.colorAttachments.resize(size);
        fgRenderpassInfo.colorAccesses.resize(size);
        PmrFlatMap<uint32_t, std::pair<ccstd::pmr::string, gfx::AccessFlags>> viewIndex(rag.get_allocator());
        for (const auto &[name, view] : pass.rasterViews) {
            auto resIter = rag.resourceIndex.find(name);
            gfx::AccessFlags prevAccess = resIter == rag.resourceIndex.end() ? gfx::AccessFlags::NONE : rag.accessRecord.at(resIter->second).currStatus.access.accessFlag;
            viewIndex.emplace(std::piecewise_construct, std::forward_as_tuple(view.slotID), std::forward_as_tuple(name, prevAccess));
        }

        bool dependent = false;
        dependent |= checkRasterViews(graphs, vertID, passID, PassType::RASTER, node, pass.rasterViews);
        dependent |= checkComputeViews(graphs, vertID, passID, PassType::RASTER, node, pass.computeViews);

        if (!dependent) {
            tryAddEdge(EXPECT_START_ID, vertID, resourceAccessGraph);
            tryAddEdge(EXPECT_START_ID, rlgVertID, relationGraph);
        }

        uint32_t index = 0;
        // initial layout(accessrecord.laststatus) and final layout(accessrecord.currstatus) can be filled here
        for (const auto &[slotID, pair] : viewIndex) {
            const auto &name = pair.first;
            const auto resID = rag.resourceIndex.at(name);
            const auto &view = pass.rasterViews.at(name);
            const auto &viewDesc = get(ResourceGraph::DescTag{}, resourceGraph, resID);
            auto prevAccess = pair.second;
            CC_ASSERT(slotID < node.attachmentStatus.size());
            // TD:remove find
            auto nodeIter = std::find_if(node.attachmentStatus.begin(), node.attachmentStatus.end(), [&name](const ResourceStatus &status) {
                return status.resName == name;
            });
            auto nextAccess = nodeIter->access.accessFlag;

            if (rpInfo.subpasses.empty()) {
                rpInfo.subpasses.emplace_back();
            }
            auto &subpassInfo = rpInfo.subpasses.front();
            if (view.attachmentType != AttachmentType::DEPTH_STENCIL) {
                if (view.attachmentType == AttachmentType::SHADING_RATE) {
                    subpassInfo.shadingRate = index;
                } else {
                    if (view.accessType != AccessType::READ) {
                        subpassInfo.colors.emplace_back(index);
                    }
                    if (view.accessType != AccessType::WRITE) {
                        subpassInfo.inputs.emplace_back(index);
                    }
                }
                fgRenderpassInfo.colorAccesses[index].prevAccess = prevAccess;
                fgRenderpassInfo.colorAccesses[index].nextAccess = nextAccess;
            } else {
                subpassInfo.depthStencil = pass.rasterViews.size() - 1;
                fgRenderpassInfo.dsAccess.prevAccess = prevAccess;
                fgRenderpassInfo.dsAccess.nextAccess = nextAccess;
            }
            fillRenderPassInfo(view, rpInfo, index, viewDesc);
        }
    } else {
        PmrFlatSet<PmrString> viewSet(resourceAccessGraph.get_allocator());
        bool hasDS{false};
        for (const auto &subpass : pass.subpassGraph.subpasses) {
            for (const auto &pair : subpass.rasterViews) {
                if (pair.second.attachmentType != AttachmentType::DEPTH_STENCIL) {
                    viewSet.emplace(pair.first);
                } else {
                    hasDS = true;
                }
            }
        }
        rpInfo.colorAttachments.resize(viewSet.size());
        fgRenderpassInfo.colorAccesses.resize(viewSet.size());
    }
}

void processComputePass(const Graphs &graphs, uint32_t passID, const ComputePass &pass) {
    const auto &[renderGraph, resourceGraph, layoutGraphData, resourceAccessGraph, relationGraph] = graphs;
    auto vertID = add_vertex(resourceAccessGraph, passID);
    auto rlgVertID = add_vertex(relationGraph, vertID);
    CC_EXPECTS(static_cast<uint32_t>(rlgVertID) == static_cast<uint32_t>(vertID));

    auto &node = get(RAG::AccessNodeTag{}, resourceAccessGraph, vertID);
    bool dependent = checkComputeViews(graphs, vertID, passID, PassType::COMPUTE, node, pass.computeViews);

    if (!dependent) {
        tryAddEdge(EXPECT_START_ID, vertID, resourceAccessGraph);
        tryAddEdge(EXPECT_START_ID, rlgVertID, relationGraph);
    }
}

void processRasterSubpass(const Graphs &graphs, uint32_t passID, const RasterSubpass &pass) {
    const auto &[renderGraph, resourceGraph, layoutGraphData, resourceAccessGraph, relationGraph] = graphs;
    const auto &obj = renderGraph.objects.at(passID);
    const auto parentID = obj.parents.front().target;
    const auto parentRagVert = resourceAccessGraph.passIndex.at(parentID);
    const auto *parentPass = get_if<RasterPass>(parentID, &renderGraph);
    CC_EXPECTS(parentPass);
    const auto &rag = resourceAccessGraph;
    const auto &resg = resourceGraph;
    const auto &uberPass = *parentPass;

    resourceAccessGraph.passIndex[passID] = parentRagVert;

    uint32_t dsIndex = INVALID_ID;
    PmrFlatMap<uint32_t, std::pair<ccstd::pmr::string, gfx::AccessFlags>> viewIndex(rag.get_allocator());
    for (const auto &[name, view] : pass.rasterViews) {
        auto resIter = rag.resourceIndex.find(name);
        gfx::AccessFlags prevAccess = resIter == rag.resourceIndex.end() ? gfx::AccessFlags::NONE : rag.accessRecord.at(resIter->second).currStatus.access.accessFlag;
        viewIndex.emplace(std::piecewise_construct, std::forward_as_tuple(view.slotID), std::forward_as_tuple(name, prevAccess));
    }

    auto &node = get(RAG::AccessNodeTag{}, resourceAccessGraph, parentRagVert);
    auto &fgRenderpassInfo = resourceAccessGraph.rpInfos.at(parentRagVert);
    auto &rpInfo = fgRenderpassInfo.rpInfo;
    auto &subpassInfo = rpInfo.subpasses.emplace_back();
    auto *lastNode = &node;
    while (lastNode->nextSubpass) {
        lastNode = lastNode->nextSubpass;
    }
    lastNode->nextSubpass = new ResourceAccessNode;
    auto *head = lastNode->nextSubpass;
    bool dependent{false};
    dependent |= checkRasterViews(graphs, parentRagVert, passID, PassType::RASTER, *head, pass.rasterViews);
    dependent |= checkComputeViews(graphs, parentRagVert, passID, PassType::RASTER, *head, pass.computeViews);

    if (!dependent) {
        tryAddEdge(EXPECT_START_ID, parentRagVert, resourceAccessGraph);
        tryAddEdge(EXPECT_START_ID, parentRagVert, relationGraph);
    }

    for (const auto &[slotID, pair] : viewIndex) {
        const auto &resName = pair.first;
        auto findByName = [&](const ResourceStatus &status) {
            return status.resName == resName;
        };
        auto iter = std::find_if(node.attachmentStatus.begin(), node.attachmentStatus.end(), findByName);
        const auto &view = pass.rasterViews.at(resName);
        auto resID = rag.resourceIndex.at(resName);
        const auto &viewDesc = get(ResourceGraph::DescTag{}, resg, rag.resourceIndex.at(resName));
        auto slot = slotID > dsIndex ? slotID - 1 : slotID;
        // TD:remove find
        auto nodeIter = std::find_if(head->attachmentStatus.begin(), head->attachmentStatus.end(), findByName);
        auto nextAccess = nodeIter->access.accessFlag;
        if (view.attachmentType != AttachmentType::DEPTH_STENCIL) {
            if (view.attachmentType == AttachmentType::SHADING_RATE) {
                subpassInfo.shadingRate = slot;
            } else {
                if (view.accessType != AccessType::READ) {
                    subpassInfo.colors.emplace_back(slot);
                }
                if (view.accessType != AccessType::WRITE) {
                    subpassInfo.inputs.emplace_back(slot);
                }
            }
            fgRenderpassInfo.colorAccesses[slot].nextAccess = nextAccess;
        } else {
            dsIndex = slotID;
            fgRenderpassInfo.dsAccess.nextAccess = nextAccess;
            subpassInfo.depthStencil = rpInfo.colorAttachments.size();
        }

        if (iter == node.attachmentStatus.end()) {
            auto curIter = std::find_if(head->attachmentStatus.begin(), head->attachmentStatus.end(), findByName);
            node.attachmentStatus.emplace_back(*curIter);

            auto colorIndex = dsIndex == INVALID_ID ? node.attachmentStatus.size() : node.attachmentStatus.size() - 1;
            auto prevAccess = pair.second;
            CC_ASSERT(head->attachmentStatus.size() > slotID);
            auto nextAccess = head->attachmentStatus[slot].access.accessFlag;

            if (view.attachmentType == AttachmentType::DEPTH_STENCIL) {
                fgRenderpassInfo.dsAccess.prevAccess = prevAccess;
            } else {
                fgRenderpassInfo.colorAccesses[slot].prevAccess = prevAccess;
            }
            fillRenderPassInfo(view, rpInfo, slot, viewDesc);
        }
    }
}

void processComputeSubpass(const Graphs &graphs, uint32_t passID, const ComputeSubpass &pass) {
    const auto &[renderGraph, resourceGraph, layoutGraphData, resourceAccessGraph, relationGraph] = graphs;
    const auto &obj = renderGraph.objects.at(passID);
    const auto parentID = obj.parents.front().target;
    const auto parentRagVert = resourceAccessGraph.passIndex.at(parentID);
    const auto *parentPass = get_if<ComputePass>(parentID, &renderGraph);
    CC_EXPECTS(parentPass);
    const auto &rag = resourceAccessGraph;
    const auto &resg = resourceGraph;
    const auto &uberPass = *parentPass;

    resourceAccessGraph.passIndex[passID] = parentRagVert;

    auto &node = get(RAG::AccessNodeTag{}, resourceAccessGraph, parentRagVert);

    auto *lastNode = &node;
    while (lastNode->nextSubpass) {
        lastNode = lastNode->nextSubpass;
    }
    lastNode->nextSubpass = new ResourceAccessNode;
    auto *head = lastNode->nextSubpass;

    bool dependent = checkComputeViews(graphs, parentRagVert, passID, PassType::COMPUTE, *head, pass.computeViews);

    if (!dependent) {
        tryAddEdge(EXPECT_START_ID, parentRagVert, resourceAccessGraph);
        tryAddEdge(EXPECT_START_ID, parentRagVert, relationGraph);
    }
}

void processCopyPass(const Graphs &graphs, uint32_t passID, const CopyPass &pass) {
    const auto &[renderGraph, resourceGraph, layoutGraphData, resourceAccessGraph, relationGraph] = graphs;

    auto vertID = add_vertex(resourceAccessGraph, passID);
    auto rlgVertID = add_vertex(relationGraph, vertID);
    CC_EXPECTS(static_cast<uint32_t>(rlgVertID) == static_cast<uint32_t>(vertID));

    auto &node = get(RAG::AccessNodeTag{}, resourceAccessGraph, vertID);
    bool dependent = false;
    for (const auto &pair : pass.copyPairs) {
        const auto &resDesc = get(ResourceGraph::DescTag{}, resourceGraph, resourceAccessGraph.resourceIndex.at(pair.source));
        const auto &rag = resourceAccessGraph;
        const auto &resg = resourceGraph; 
        auto getRange = [&](const CopyPair &pair, bool source) {
            ResourceRange range{};
            const auto &resName = source ? pair.source : pair.target;
            const auto &resDesc = get(ResourceGraph::DescTag{}, resg, rag.resourceIndex.at(resName));
            if (resDesc.dimension == ResourceDimension::BUFFER) {
                range.width = resDesc.depthOrArraySize;
                range.firstSlice = resDesc.width;
            } else {
                range.width = resDesc.width;
                range.height = resDesc.height;
                range.firstSlice = source ? pair.sourceFirstSlice : pair.targetFirstSlice;
                range.numSlices = pair.numSlices;
                range.mipLevel = source ? pair.sourceMostDetailedMip : pair.targetMostDetailedMip;
                range.levelCount = pair.mipLevels;
                range.planeSlice = source ? pair.sourcePlaneSlice : pair.targetPlaneSlice;
            }
            return range;
        };

        auto sourceRange = getRange(pair, true);
        auto targetRange = getRange(pair, false);
        

        ResourceUsage srcUsage = gfx::TextureUsage::TRANSFER_SRC;
        ViewStatus srcViewStatus{pair.source, gfx::AccessFlags::TRANSFER_READ, sourceRange};
        addAccessStatus(resourceAccessGraph, resourceGraph, node, srcViewStatus);
        ResourceUsage dstUsage = gfx::TextureUsage::TRANSFER_DST;
        ViewStatus dstViewStatus{pair.target, gfx::AccessFlags::TRANSFER_WRITE, targetRange};
        addAccessStatus(resourceAccessGraph, resourceGraph, node, dstViewStatus);

        uint32_t lastVertSrc = dependencyCheck(resourceAccessGraph, vertID, resourceGraph, srcViewStatus);
        if (lastVertSrc != INVALID_ID) {
            tryAddEdge(lastVertSrc, vertID, resourceAccessGraph);
            tryAddEdge(lastVertSrc, rlgVertID, relationGraph);
            dependent = true;
        }
        uint32_t lastVertDst = dependencyCheck(resourceAccessGraph, vertID, resourceGraph, dstViewStatus);
        if (lastVertDst != INVALID_ID) {
            tryAddEdge(lastVertDst, vertID, resourceAccessGraph);
            tryAddEdge(lastVertDst, rlgVertID, relationGraph);
            dependent = true;
        }
    }
    if (!dependent) {
        tryAddEdge(EXPECT_START_ID, vertID, resourceAccessGraph);
        tryAddEdge(EXPECT_START_ID, rlgVertID, relationGraph);
    }
    std::sort(node.attachmentStatus.begin(), node.attachmentStatus.end(), [](const ResourceStatus &lhs, const ResourceStatus &rhs) { return lhs.resName < rhs.resName; });
}

void processRaytracePass(const Graphs &graphs, uint32_t passID, const RaytracePass &pass) {
    const auto &[renderGraph, resourceGraph, layoutGraphData, resourceAccessGraph, relationGraph] = graphs;

    auto vertID = add_vertex(resourceAccessGraph, passID);
    auto rlgVertID = add_vertex(relationGraph, vertID);
    CC_EXPECTS(static_cast<uint32_t>(rlgVertID) == static_cast<uint32_t>(vertID));

    auto &node = get(RAG::AccessNodeTag{}, resourceAccessGraph, vertID);
    bool dependent = checkComputeViews(graphs, vertID, passID, PassType::RAYTRACE, node, pass.computeViews);

    if (!dependent) {
        tryAddEdge(EXPECT_START_ID, vertID, resourceAccessGraph);
        tryAddEdge(EXPECT_START_ID, rlgVertID, relationGraph);
    }
}

bool rangeIntersect(const ResourceRange &lhs, const ResourceRange &rhs) {
    bool rSlice = ((lhs.firstSlice + lhs.numSlices) <= (rhs.firstSlice + rhs.numSlices)) && ((lhs.firstSlice + lhs.numSlices) > rhs.firstSlice);
    bool lSlice = (lhs.firstSlice < (rhs.firstSlice + rhs.numSlices)) && (lhs.firstSlice >= rhs.firstSlice);
    bool rMip = ((lhs.mipLevel + lhs.levelCount) <= (rhs.mipLevel + rhs.levelCount)) && ((lhs.mipLevel + lhs.levelCount) > rhs.mipLevel);
    bool lMip = (lhs.mipLevel < (rhs.mipLevel + rhs.levelCount)) && (lhs.mipLevel > rhs.mipLevel);
    return (rSlice || lSlice) && (rMip || lMip);
}

struct SliceNode {
    bool full{false};
    ccstd::pmr::vector<uint32_t> mips;
};

struct TextureNode {
    bool full{false};
    ccstd::pmr::vector<SliceNode> slices;
};

struct ResourceNode {
    ccstd::pmr::vector<TextureNode> planes;
};

bool rangeCheck(ccstd::pmr::map<PmrString, ResourceNode> &status,
                const ResourceDesc &desc,
                const PmrString &targetName,
                uint32_t firstSlice, uint32_t numSlices,
                uint32_t firstMip, uint32_t mipLevels,
                uint32_t planeIndex) {
    if (status.find(targetName) == status.end()) {
        status.emplace(targetName, ResourceNode{});
    }

    if (planeIndex >= status[targetName].planes.size()) {
        status[targetName].planes.resize(planeIndex + 1);
        status[targetName].planes[planeIndex].slices.resize(desc.depthOrArraySize);
        for (auto &slice : status[targetName].planes[planeIndex].slices) {
            slice.mips.resize(desc.mipLevels, std::numeric_limits<uint32_t>::max());
        }
    }

    // no spare space in target
    bool check = !status[targetName].planes[planeIndex].full;
    for (auto slice = firstSlice; slice < firstSlice + numSlices; ++slice) {
        auto &slices = status[targetName].planes[planeIndex].slices;
        // no spare space in this slice
        check &= !slices[slice].full;
        for (auto mip = firstMip; slice < firstMip + mipLevels; ++mip) {
            auto &mips = slices[slice].mips;
            // this mip has been taken
            check &= mips[mip] == std::numeric_limits<uint32_t>::max();
            mips[mip] = mip;
            auto maxIter = std::max_element(mips.begin(), mips.end());
            if ((*maxIter) != std::numeric_limits<uint32_t>::max()) {
                // linear increasing
                check &= (*maxIter) == mips.size() - 1;
                slices[slice].full = true;
            }
        }
        if (std::all_of(slices.begin(), slices.end(), [](const SliceNode &sliceNode) { return sliceNode.full; })) {
            status[targetName].planes[planeIndex].full = true;
        }
    }
    return check;
}

bool moveValidation(RenderGraph::vertex_descriptor passID,
                    const MovePass &pass,
                    ResourceGraph &resourceGraph,
                    ResourceAccessGraph &rag,
                    PmrFlatMap<PmrString, ResourceStatus> &expiredValues) {
    bool check = true;
    // ccstd::pmr::map<PmrString, ResourceNode> sourceCheck;
    ccstd::pmr::map<PmrString, ResourceNode> targetCheck;
    for (const auto &movePair : pass.movePairs) {
        const auto &fromResName = movePair.source;
        const auto fromResID = resourceGraph.valueIndex.at(fromResName);
        const auto &fromResDesc = get(ResourceGraph::DescTag{}, resourceGraph, fromResID);

        const auto &toResName = movePair.target;
        const auto toResID = resourceGraph.valueIndex.at(toResName);
        const auto &toResDesc = get(ResourceGraph::DescTag{}, resourceGraph, toResID);

        const auto &fromResTraits = get(ResourceGraph::TraitsTag{}, resourceGraph, fromResID);
        const auto &toResTraits = get(ResourceGraph::TraitsTag{}, resourceGraph, toResID);
        auto commonUsage = fromResDesc.flags | toResDesc.flags;

        // bool sourceRangeValid = rangeCheck(targetCheck, toResDesc, fromResName, movePair.targetFirstSlice, movePair.numSlices, movePair.targetMostDetailedMip, movePair.mipLevels, movePair.targetPlaneSlice);
        bool targetRangeValid = rangeCheck(targetCheck, toResDesc, toResName, movePair.targetFirstSlice, movePair.numSlices, movePair.targetMostDetailedMip, movePair.mipLevels, movePair.targetPlaneSlice);

        uint32_t validConditions[] = {
            !fromResTraits.hasSideEffects(),
            rag.movedResources.find(toResName) == rag.movedResources.end() && expiredValues.find(toResName) == expiredValues.end(),
            rag.movedResources.find(fromResName) == rag.movedResources.end() && expiredValues.find(fromResName) == expiredValues.end(),
            targetRangeValid,
            fromResTraits.residency != ResourceResidency::MEMORYLESS && toResTraits.residency != ResourceResidency::MEMORYLESS,
            fromResDesc.dimension == toResDesc.dimension,
            fromResDesc.width == toResDesc.width,
            fromResDesc.height == toResDesc.height,
            fromResDesc.format == toResDesc.format,
            fromResDesc.sampleCount == toResDesc.sampleCount,
            (fromResDesc.depthOrArraySize == toResDesc.depthOrArraySize) || (toResDesc.dimension != ResourceDimension::BUFFER), // full move if resource is buffer
        };
        auto targetStatus = ResourceStatus{toResName, ResourceRange{
                                                          toResDesc.width,
                                                          toResDesc.height,
                                                          movePair.targetFirstSlice,
                                                          movePair.numSlices,
                                                          movePair.targetMostDetailedMip,
                                                          movePair.mipLevels,
                                                          movePair.targetPlaneSlice,
                                                      }};
        expiredValues.emplace(fromResName, targetStatus);
        bool val = std::min_element(std::begin(validConditions), std::end(validConditions));
        check &= val;
    }

    // full destination
    check &= std::all_of(targetCheck.begin(), targetCheck.end(), [](const auto &pair) {
        const ResourceNode &resNode = pair.second;
        return std::all_of(resNode.planes.begin(), resNode.planes.end(), [](const auto &textureNode) {
            return textureNode.full;
        });
    });

    return check;
}

void processMovePass(const Graphs &graphs, uint32_t passID, const MovePass &pass) {
    const auto &[ignore, resourceGraph, layoutGraphData, resourceAccessGraph, relationGraph] = graphs;
    auto &rescGraph = resourceGraph;
    auto &rag = resourceAccessGraph;
    PmrFlatMap<PmrString, ResourceStatus> expiredValues(resourceAccessGraph.get_allocator());
    if (moveValidation(passID, pass, rescGraph, rag, expiredValues)) {
        for (const auto &pair : expiredValues) {
            rag.movedResources[pair.first] = pair.second;
            rag.resourceIndex[pair.first] = vertex(pair.second.resName, resourceGraph);
        }
    } else {
        for (const auto &pair : pass.movePairs) {
            // copy
            CopyPass copyPass(rag.get_allocator());
            copyPass.copyPairs.emplace_back(CopyPair(
                pair.source,
                pair.target,
                1, 1,
                0, 0, 0,
                pair.targetMostDetailedMip, pair.targetFirstSlice, pair.targetPlaneSlice));
            processCopyPass(graphs, passID, copyPass);
        }
    }
}

#pragma endregion assisstantFuncDefinition

} // namespace render

} // namespace cc
