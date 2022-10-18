#include "SceneAccelerationStructure.h"

#include "GlobalDescriptorSetManager.h"
#include "3d/assets/Mesh.h"
#include "gfx-base/GFXDevice.h"
#include "scene/Model.h"
#include "scene/RenderScene.h"
#include "core/Root.h"
#include "custom/RenderInterfaceTypes.h"

namespace cc
{
	namespace pipeline
	{

        SceneAccelerationStructure::SceneAccelerationStructure() {
            auto* pipelineRuntime = Root::getInstance()->getPipeline();
            _globalDSManager = pipelineRuntime->getGlobalDSManager(); 
        }

        void SceneAccelerationStructure::update(const scene::RenderScene* scene) {

            bool _needRebuild = false;
            bool _needUpdate = false;
            bool _needRecreate = false;

            auto* device = gfx::Device::getInstance();

            for (const auto& pModel : scene->getModels()) {
                if (!pModel->getNode()->isValid() || !pModel->getNode()->isActive() || pModel->getNode()->getName() == "Profiler_Root") {
                    continue;
                }

                const auto name = pModel->getNode()->getName();

                const auto model_uuid = pModel->getNode()->getUuid();
                auto model_it = modelMap.find(model_uuid);

                if (model_it != modelMap.cend()) {
                    // set alive flag true
                    model_it->second.first = true;

                    if (pModel->getTransform()->getChangedFlags()) {
                        // Instance transform changed, tlas should be updated.
                        auto last_update_transfrom = &model_it->second.second.transform;
                        auto current_transfrom = &pModel->getTransform()->getWorldMatrix();

                        auto similarTransform = [](const Mat4* mat1, const Mat4* mat2) -> bool {
                            Vec3 v1, v2;
                            mat1->getScale(&v1);
                            mat2->getScale(&v2);
                            bool similarScale = (v1 - v2).lengthSquared() < 1;
                            mat1->getTranslation(&v1);
                            mat2->getTranslation(&v2);
                            bool similarTranslate = (v1 - v2).lengthSquared() < 1;
                            Quaternion q1, q2;
                            return similarScale && similarTranslate;
                        };

                        if (similarTransform(last_update_transfrom, current_transfrom)) {
                            _needUpdate = true;
                        } else {
                            _needRebuild = true;
                        }
                        model_it->second.second.transform = *current_transfrom;
                    }
                }else {
                    gfx::AccelerationStructureInfo blasInfo{};
                    const auto& subModels = pModel->getSubModels();
                    // New instance should be added to top-level acceleration structure.
                    // Tlas should be recreate and rebuild.
                    _needRecreate = true;
                    _needRebuild = true;
                    gfx::ASInstance tlasGeom{};
                    //tlasGeom.stype = gfx::ASGeometryType::INSTANCE;
                    tlasGeom.instanceCustomIdx = 0;

                    if (pModel->getNode()->getName() == "Cube-001") {
                        tlasGeom.instanceCustomIdx = 1;
                    } else if (pModel->getNode()->getName() == "Cube-002") {
                        tlasGeom.instanceCustomIdx = 2;
                    } else if (pModel->getNode()->getName() == "Cube-003") {
                        tlasGeom.instanceCustomIdx = 3;
                    } else if (pModel->getNode()->getName() == "Cube-004"){
                        tlasGeom.instanceCustomIdx = 4;
                    } else if (pModel->getNode()->getName() == "stenford_dragon_high") {
                        tlasGeom.instanceCustomIdx = 5;
                    } else if (pModel->getNode()->getName() == "Cube") {
                        tlasGeom.instanceCustomIdx = 6;
                    } else if (pModel->getNode()->getName() == "wall3") {
                        tlasGeom.instanceCustomIdx = 7;
                    }
                    
                    tlasGeom.shaderBindingTableRecordOffset = 0;
                    tlasGeom.mask = 0xFF;
                    tlasGeom.transform = pModel->getTransform()->getWorldMatrix();
                    tlasGeom.flags = gfx::GeometryInstanceFlagBits::TRIANGLE_FACING_CULL_DISABLE;

                    if (pModel->getNode()->getName() == "AABB")
                        tlasGeom.flags = gfx::GeometryInstanceFlagBits::FORCE_OPAQUE;

                    uint64_t mesh_uuid = reinterpret_cast<uint64_t>(subModels[0]->getSubMesh());

                    if (pModel->getNode()->getName() == "AABB")
                        mesh_uuid += 1024;

                    auto it = blasMap.find(mesh_uuid);
                    if (it != blasMap.cend()) {
                        // Blas could be reused.
                        tlasGeom.accelerationStructureRef = it->second;
                    } else {
                        // New Blas should be create and build.
                        if (pModel->getNode()->getName() == "AABB") {
                            gfx::ASAABB blasGeom{};
                            blasGeom.flag = gfx::ASGeometryFlagBit::GEOMETRY_OPAQUE;
                            blasGeom.minX = -0.5;
                            blasGeom.minY = -0.5;
                            blasGeom.minZ = -0.5;
                            blasGeom.maxX = 0.5;
                            blasGeom.maxY = 0.5;
                            blasGeom.maxZ = 0.5;
                            blasInfo.aabbs.push_back(blasGeom);
                        }else {
                            for (const auto& pSubModel : subModels) {
                                gfx::ASTriangleMesh blasGeom{};
                                const auto* inputAssembler = pSubModel->getInputAssembler();
                                blasGeom.flag = gfx::ASGeometryFlagBit::GEOMETRY_OPAQUE;
                                blasGeom.vertexCount = inputAssembler->getVertexCount();
                                blasGeom.indexCount = inputAssembler->getIndexCount();
                                blasGeom.indexBuffer = inputAssembler->getIndexBuffer();

                                const auto& attributes = inputAssembler->getAttributes();

                                auto pred = [](const gfx::Attribute& attr) {
                                    return attr.name == gfx::ATTR_NAME_POSITION;
                                };

                                auto posAttribute = std::find_if(attributes.cbegin(), attributes.cend(), pred);

                                if (posAttribute != attributes.cend()) {
                                    const auto vertexBufferList = inputAssembler->getVertexBuffers();
                                    auto* const posBuffer = vertexBufferList[posAttribute->stream];
                                    blasGeom.vertexBuffer = posBuffer;
                                    blasGeom.vertexFormat = posAttribute->format;
                                    blasGeom.vertexStride = posBuffer->getStride();
                                }

                                blasInfo.triangels.push_back(blasGeom);
                            }
                        }

                        blasInfo.buildFlag = gfx::ASBuildFlagBits::ALLOW_COMPACTION | gfx::ASBuildFlagBits::PREFER_FAST_TRACE;
                        gfx::AccelerationStructure* blas = device->createAccelerationStructure(blasInfo);
                        blas->build();
                        blas->compact();
                        
                        blasMap.emplace(mesh_uuid, blas);
                        tlasGeom.accelerationStructureRef = blas;
                    }
                    modelMap.emplace(model_uuid, std::pair{true, tlasGeom});
                }
            }

            //sweep deactive model entries
            auto model_it = modelMap.begin();
            while (model_it != modelMap.end()) {
                if (model_it->second.first) {
                    model_it->second.first = false;
                    ++model_it;
                } else {
                    model_it = modelMap.erase(model_it);
                    _needRebuild = true;
                }
            } 

            //sweep deactive blas
            auto blas_it = blasMap.begin();
            while (blas_it!=blasMap.end()) {
                if (blas_it->second->getRefCount()==0) {
                    //blas_it = blasMap.erase(blas_it);
                    //blas_it->second->destroy();
                }else {
                    ++blas_it;
                }
            }

            if (_needRebuild||_needUpdate) {
                gfx::AccelerationStructureInfo tlasInfo{};
                tlasInfo.buildFlag = gfx::ASBuildFlagBits::ALLOW_UPDATE | gfx::ASBuildFlagBits::PREFER_FAST_TRACE;
                tlasInfo.instances.reserve(modelMap.size());
                for (const auto& inst : modelMap) {
                    tlasInfo.instances.push_back(inst.second.second);
                }
                if (_needRecreate) {
                    if (topLevelAccelerationStructure) {
                        topLevelAccelerationStructure->destroy();
                    }
                    topLevelAccelerationStructure = device->createAccelerationStructure(tlasInfo);
                } else {
                    topLevelAccelerationStructure->setInfo(tlasInfo);
                }
                if (_needRebuild) {
                    topLevelAccelerationStructure->build();
                } else if (_needUpdate) {
                    topLevelAccelerationStructure->update();
                }
            }

            if (_needRecreate) {
                _globalDSManager->bindAccelerationStructure(pipeline::TOPLEVELAS::BINDING, topLevelAccelerationStructure);
                _globalDSManager->update();
            }

            _needRecreate = _needRebuild = _needUpdate = false;
		}

        void SceneAccelerationStructure::destroy() {
            
        }

	} // namespace scene
} // namespace cc