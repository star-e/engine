#pragma once

#include "renderer/gfx-base/GFXAccelerationStructure.h"
#include "base/Ptr.h"
#include "base/std/container/unordered_set.h"

namespace cc
{

namespace scene{
class RenderScene;
class Model;
}
namespace pipeline
{
    class GlobalDSManager;
    class SceneAccelerationStructure final
    {
    public:
        SceneAccelerationStructure();
        void update(const scene::RenderScene* scene);
        void destroy();
    protected:
        IntrusivePtr<gfx::AccelerationStructure> _topLevelAccelerationStructure;
        ccstd::vector<IntrusivePtr<gfx::AccelerationStructure>> _bottomLevelAccelerationStructures;
        ccstd::unordered_map<uint64_t,IntrusivePtr<gfx::AccelerationStructure>> _blasMap;
        ccstd::unordered_map<ccstd::string,std::pair<bool,gfx::ASInstance>> _modelMap;

        class InstanceDataDesc {
            
        };

        IntrusivePtr<gfx::Buffer> _sceneDataGPUDesc;
        ccstd::vector<InstanceDataDesc> _sceneDataDesc;
         
        pipeline::GlobalDSManager* _globalDSManager{nullptr};
    private:
    };
} // namespace pipeline
} // namespace cc
