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

/**
 * ========================= !DO NOT CHANGE THE FOLLOWING SECTION MANUALLY! =========================
 * The following section is auto-generated.
 * ========================= !DO NOT CHANGE THE FOLLOWING SECTION MANUALLY! =========================
 */
/* eslint-disable max-len */
import { getPhaseID, InstancedBuffer, PipelineStateManager } from '..';
import { assert, cclegacy, RecyclePool } from '../../core';
import intersect from '../../core/geometry/intersect';
import { Sphere } from '../../core/geometry/sphere';
import {
    AccessFlagBit,
    Attribute,
    Buffer,
    BufferInfo,
    BufferUsageBit,
    BufferViewInfo,
    Color,
    ColorAttachment,
    CommandBuffer,
    DepthStencilAttachment,
    DescriptorSet,
    DescriptorSetInfo,
    Device,
    deviceManager,
    Format,
    Framebuffer,
    FramebufferInfo,
    GeneralBarrierInfo,
    InputAssembler,
    InputAssemblerInfo,
    LoadOp,
    MemoryUsageBit,
    PipelineState,
    Rect,
    RenderPass,
    RenderPassInfo,
    Shader,
    StoreOp,
    SurfaceTransform,
    Swapchain,
    Texture,
    TextureInfo,
    TextureType,
    TextureUsageBit,
    Viewport,
} from '../../gfx';
import { legacyCC } from '../../core/global-exports';
import { Vec3 } from '../../core/math/vec3';
import { Vec4 } from '../../core/math/vec4';
import { Pass } from '../../render-scene';
import { Camera } from '../../render-scene/scene/camera';
import { ShadowType } from '../../render-scene/scene/shadows';
import { Root } from '../../root';
import { IRenderPass, isEnableEffect, SetIndex, UBODeferredLight, UBOForwardLight, UBOLocal } from '../define';
import { PipelineSceneData } from '../pipeline-scene-data';
import { PipelineInputAssemblerData } from '../render-pipeline';
import { DescriptorSetData, LayoutGraphData, PipelineLayoutData, RenderPhaseData, RenderStageData } from './layout-graph';
import { BasicPipeline } from './pipeline';
import { SceneVisitor } from './scene';
import {
    Blit,
    ClearView,
    ComputePass,
    ComputeSubpass,
    ComputeView,
    CopyPass,
    Dispatch,
    FormatView,
    ManagedBuffer,
    ManagedResource,
    ManagedTexture,
    MovePass,
    RasterPass,
    RasterSubpass,
    RasterView,
    RaytracePass,
    RenderData,
    RenderGraph,
    RenderGraphVisitor,
    RenderQueue,
    RenderSwapchain,
    ResolvePass,
    ResourceDesc,
    ResourceGraph,
    ResourceGraphVisitor,
    ResourceTraits,
    SceneData,
    SubresourceView,
} from './render-graph';
import {
    AttachmentType,
    QueueHint,
    ResourceDimension,
    ResourceFlags,
    ResourceResidency,
    SceneFlags,
    UpdateFrequency,
} from './types';
import { PipelineUBO } from '../pipeline-ubo';
import { RenderInfo, RenderObject, WebSceneTask, WebSceneTransversal } from './web-scene';
import { WebSceneVisitor } from './web-scene-visitor';
import { RenderAdditiveLightQueue } from '../render-additive-light-queue';
import { RenderShadowMapBatchedQueue } from '../render-shadow-map-batched-queue';
import { DefaultVisitor, depthFirstSearch, ReferenceGraphView } from './graph';
import { VectorGraphColorMap } from './effect';
import {
    getDescBindingFromName,
    getDescriptorSetDataFromLayout,
    getDescriptorSetDataFromLayoutId,
    getRenderArea,
    mergeSrcToTargetDesc,
    updateGlobalDescBinding,
    validPunctualLightsCulling,
} from './define';
import { RenderReflectionProbeQueue } from '../render-reflection-probe-queue';
import { builtinResMgr } from '../../asset/asset-manager/builtin-res-mgr';
import { Texture2D } from '../../asset/assets/texture-2d';
import { SceneCulling } from './scene-culling';

class ResourceVisitor implements ResourceGraphVisitor {
    name: string;
    constructor (resName = '') {
        this.name = resName;
    }
    set resName (value: string) {
        this.name = value;
    }
    createDeviceTex (value: Texture | Framebuffer | ManagedResource | RenderSwapchain): void {
        const deviceTex = new DeviceTexture(this.name, value);
        context.deviceTextures.set(this.name, deviceTex);
    }
    managed (value: ManagedResource): void {
        this.createDeviceTex(value);
    }
    managedBuffer (value: ManagedBuffer): void {
        // noop
    }
    managedTexture (value: ManagedTexture): void {
        // noop
    }
    persistentBuffer (value: Buffer): void {
    }
    persistentTexture (value: Texture): void {
        this.createDeviceTex(value);
    }
    framebuffer (value: Framebuffer): void {
        this.createDeviceTex(value);
    }
    swapchain (value: RenderSwapchain): void {
        this.createDeviceTex(value);
    }
    formatView (value: FormatView): void {
    }
    subresourceView (value: SubresourceView): void {
    }
}

let context: ExecutorContext;
class DeviceResource {
    protected _name: string;
    constructor (name: string) {
        this._name = name;
    }
    get name (): string { return this._name; }
}
class DeviceTexture extends DeviceResource {
    protected _texture: Texture | null = null;
    protected _swapchain: Swapchain | null = null;
    protected _framebuffer: Framebuffer | null = null;
    protected _desc: ResourceDesc | null = null;
    protected _trait: ResourceTraits | null = null;
    get texture (): Texture | null { return this._texture; }
    set framebuffer (val: Framebuffer | null) { this._framebuffer = val; }
    get framebuffer (): Framebuffer | null { return this._framebuffer; }
    get description (): ResourceDesc | null { return this._desc; }
    get trait (): ResourceTraits | null { return this._trait; }
    get swapchain (): Swapchain | null { return this._swapchain; }
    private _setDesc (desc: ResourceDesc): void {
        if (!this._desc) {
            this._desc = new ResourceDesc();
        }
        this._desc.alignment = desc.alignment;
        this._desc.depthOrArraySize = desc.depthOrArraySize;
        this._desc.dimension = desc.dimension;
        this._desc.flags = desc.flags;
        this._desc.format = desc.format;
        this._desc.height = desc.height;
        this._desc.mipLevels = desc.mipLevels;
        this._desc.sampleCount = desc.sampleCount;
        this._desc.textureFlags = desc.textureFlags;
        this._desc.width = desc.width;
    }
    constructor (name: string, tex: Texture | Framebuffer | RenderSwapchain | ManagedResource) {
        super(name);
        const resGraph = context.resourceGraph;
        const verID = resGraph.vertex(name);
        this._setDesc(resGraph.getDesc(verID));
        this._trait = resGraph.getTraits(verID);
        if (tex instanceof Texture) {
            this._texture = tex;
            return;
        }
        if (tex instanceof Framebuffer) {
            this._framebuffer = tex;
            return;
        }
        if (tex instanceof RenderSwapchain) {
            this._swapchain = tex.swapchain;
            return;
        }

        const info = this._desc!;
        let type = TextureType.TEX2D;

        switch (info.dimension) {
        case ResourceDimension.TEXTURE1D:
            type = TextureType.TEX1D;
            break;
        case ResourceDimension.TEXTURE2D:
            type = TextureType.TEX2D;
            break;
        case ResourceDimension.TEXTURE3D:
            type = TextureType.TEX3D;
            break;
        default:
        }
        let usageFlags = TextureUsageBit.NONE;
        if (info.flags & ResourceFlags.COLOR_ATTACHMENT) usageFlags |= TextureUsageBit.COLOR_ATTACHMENT;
        if (info.flags & ResourceFlags.DEPTH_STENCIL_ATTACHMENT) usageFlags |= TextureUsageBit.DEPTH_STENCIL_ATTACHMENT;
        if (info.flags & ResourceFlags.INPUT_ATTACHMENT) usageFlags |= TextureUsageBit.INPUT_ATTACHMENT;
        if (info.flags & ResourceFlags.SAMPLED) usageFlags |= TextureUsageBit.SAMPLED;
        if (info.flags & ResourceFlags.STORAGE) usageFlags |= TextureUsageBit.STORAGE;
        if (info.flags & ResourceFlags.TRANSFER_SRC) usageFlags |= TextureUsageBit.TRANSFER_SRC;
        if (info.flags & ResourceFlags.TRANSFER_DST) usageFlags |= TextureUsageBit.TRANSFER_DST;

        this._texture = context.device.createTexture(new TextureInfo(
            type,
            usageFlags,
            info.format,
            info.width,
            info.height,
        ));
    }

    release (): void {
        if (this.framebuffer) {
            this.framebuffer.destroy();
            this._framebuffer = null;
        }
        if (this.texture) {
            this.texture.destroy();
            this._texture = null;
        }
    }
}

function isShadowMap (graphScene: GraphScene): boolean | null {
    const pSceneData: PipelineSceneData = cclegacy.director.root.pipeline.pipelineSceneData;
    return pSceneData.shadows.enabled
        && pSceneData.shadows.type === ShadowType.ShadowMap
        && graphScene.scene
        && (graphScene.scene.flags & SceneFlags.SHADOW_CASTER) !== 0;
}

class DeviceBuffer extends DeviceResource {
    constructor (name: string) {
        super(name);
    }
}

const _vec4Array = new Float32Array(4);
class BlitDesc {
    private _isUpdate = false;
    private _isGatherLight = false;
    private _blit: Blit;
    private _screenQuad: PipelineInputAssemblerData | null = null;
    private _queue: DeviceRenderQueue | null = null;
    private _stageDesc: DescriptorSet | undefined;
    // If VOLUMETRIC_LIGHTING is turned on, it needs to be assigned
    private _lightVolumeBuffer: Buffer | null = null;
    private _lightMeterScale = 10000.0;
    private _lightBufferData!: Float32Array;
    get screenQuad (): PipelineInputAssemblerData | null { return this._screenQuad; }
    get blit (): Blit { return this._blit; }
    set blit (blit: Blit) { this._blit = blit; }
    get stageDesc (): DescriptorSet | undefined { return this._stageDesc; }
    constructor (blit: Blit, queue: DeviceRenderQueue) {
        this._blit = blit;
        this._queue = queue;
    }
    /**
     * @zh
     * 创建四边形输入汇集器。
     */
    protected _createQuadInputAssembler (): PipelineInputAssemblerData {
        return context.blit.pipelineIAData;
    }
    createScreenQuad (): void {
        if (!this._screenQuad) {
            this._screenQuad = this._createQuadInputAssembler();
        }
    }
    private _gatherVolumeLights (camera: Camera): void {
        if (!camera.scene) { return; }
        const pipeline = context.pipeline;
        const cmdBuff = context.commandBuffer;

        const sphereLights = camera.scene.sphereLights;
        const spotLights = camera.scene.spotLights;
        const _sphere = Sphere.create(0, 0, 0, 1);
        const exposure = camera.exposure;

        let idx = 0;
        const maxLights = UBODeferredLight.LIGHTS_PER_PASS;
        const elementLen = Vec4.length; // sizeof(vec4) / sizeof(float32)
        const fieldLen = elementLen * maxLights;

        for (let i = 0; i < sphereLights.length && idx < maxLights; i++, ++idx) {
            const light = sphereLights[i];
            Sphere.set(_sphere, light.position.x, light.position.y, light.position.z, light.range);
            if (intersect.sphereFrustum(_sphere, camera.frustum)) {
                // cc_lightPos
                Vec3.toArray(_vec4Array, light.position);
                _vec4Array[3] = 0;
                this._lightBufferData.set(_vec4Array, idx * elementLen);

                // cc_lightColor
                Vec3.toArray(_vec4Array, light.color);
                if (light.useColorTemperature) {
                    const tempRGB = light.colorTemperatureRGB;
                    _vec4Array[0] *= tempRGB.x;
                    _vec4Array[1] *= tempRGB.y;
                    _vec4Array[2] *= tempRGB.z;
                }

                if (pipeline.pipelineSceneData.isHDR) {
                    _vec4Array[3] = light.luminance * exposure * this._lightMeterScale;
                } else {
                    _vec4Array[3] = light.luminance;
                }

                this._lightBufferData.set(_vec4Array, idx * elementLen + fieldLen * 1);

                // cc_lightSizeRangeAngle
                _vec4Array[0] = light.size;
                _vec4Array[1] = light.range;
                _vec4Array[2] = 0.0;
                this._lightBufferData.set(_vec4Array, idx * elementLen + fieldLen * 2);
            }
        }

        for (let i = 0; i < spotLights.length && idx < maxLights; i++, ++idx) {
            const light = spotLights[i];
            Sphere.set(_sphere, light.position.x, light.position.y, light.position.z, light.range);
            if (intersect.sphereFrustum(_sphere, camera.frustum)) {
                // cc_lightPos
                Vec3.toArray(_vec4Array, light.position);
                _vec4Array[3] = 1;
                this._lightBufferData.set(_vec4Array, idx * elementLen + fieldLen * 0);

                // cc_lightColor
                Vec3.toArray(_vec4Array, light.color);
                if (light.useColorTemperature) {
                    const tempRGB = light.colorTemperatureRGB;
                    _vec4Array[0] *= tempRGB.x;
                    _vec4Array[1] *= tempRGB.y;
                    _vec4Array[2] *= tempRGB.z;
                }
                if (pipeline.pipelineSceneData.isHDR) {
                    _vec4Array[3] = light.luminance * exposure * this._lightMeterScale;
                } else {
                    _vec4Array[3] = light.luminance;
                }
                this._lightBufferData.set(_vec4Array, idx * elementLen + fieldLen * 1);

                // cc_lightSizeRangeAngle
                _vec4Array[0] = light.size;
                _vec4Array[1] = light.range;
                _vec4Array[2] = light.spotAngle;
                this._lightBufferData.set(_vec4Array, idx * elementLen + fieldLen * 2);

                // cc_lightDir
                Vec3.toArray(_vec4Array, light.direction);
                this._lightBufferData.set(_vec4Array, idx * elementLen + fieldLen * 3);
            }
        }

        // the count of lights is set to cc_lightDir[0].w
        const offset = fieldLen * 3 + 3;
        this._lightBufferData.set([idx], offset);

        cmdBuff.updateBuffer(this._lightVolumeBuffer!, this._lightBufferData);
    }
    update (): void {
        if (this.blit.sceneFlags & SceneFlags.VOLUMETRIC_LIGHTING
            && this.blit.camera && !this._isGatherLight) {
            this._gatherVolumeLights(this.blit.camera);
            this._isGatherLight = true;
            this._isUpdate = false;
        }
        if (!this._isUpdate) {
            this._stageDesc!.update();
            this._isUpdate = true;
        }
    }

    reset (): void {
        this._isUpdate = false;
        this._isGatherLight = false;
    }

    createStageDescriptor (): void {
        const blit = this.blit;
        const pass = blit.material!.passes[blit.passID];
        const device = context.device;
        this._stageDesc = context.blit.stageDescs.get(pass);
        if (!this._stageDesc) {
            this._stageDesc = device.createDescriptorSet(new DescriptorSetInfo(pass.localSetLayout));
            context.blit.stageDescs.set(pass, this._stageDesc);
        }
        if (this.blit.sceneFlags & SceneFlags.VOLUMETRIC_LIGHTING) {
            this._lightVolumeBuffer = context.blit.lightVolumeBuffer;
            const deferredLitsBufView = context.blit.deferredLitsBufView;
            this._lightBufferData = context.blit.lightBufferData;
            this._lightBufferData.fill(0);
            // const binding = isEnableEffect() ? getDescBindingFromName('CCForwardLight') : UBOForwardLight.BINDING;
            this._stageDesc.bindBuffer(UBOForwardLight.BINDING, deferredLitsBufView);
        }
        this._stageDesc.bindBuffer(UBOLocal.BINDING, context.blit.emptyLocalUBO);
    }
}

class DeviceRenderQueue {
    private _preSceneTasks: DevicePreSceneTask[] = [];
    private _sceneTasks: DeviceSceneTask[] = [];
    private _postSceneTasks: DevicePostSceneTask[] = [];
    private _devicePass: DeviceRenderPass | undefined;
    private _hint: QueueHint =  QueueHint.NONE;
    private _graphQueue!: RenderQueue;
    private _phaseID: number = getPhaseID('default');
    private _renderPhase: RenderPhaseData | null = null;
    private _descSetData: DescriptorSetData | null = null;
    private _viewport: Viewport | null = null;
    private _scissor: Rect | null = null;
    private _layoutID = -1;
    private _isUpdateUBO = false;
    private _isUploadInstance = false;
    private _isUploadBatched = false;
    protected _transversal: DeviceSceneTransversal | null = null;
    get phaseID (): number { return this._phaseID; }
    set layoutID (value: number) {
        this._layoutID = value;
        const layoutGraph = context.layoutGraph;
        this._renderPhase = layoutGraph.tryGetRenderPhase(value);
        const layout = layoutGraph.getLayout(value);
        this._descSetData = layout.descriptorSets.get(UpdateFrequency.PER_PHASE)!;
    }
    get layoutID (): number { return this._layoutID; }
    get descSetData (): DescriptorSetData | null { return this._descSetData; }
    get renderPhase (): RenderPhaseData | null { return this._renderPhase; }
    get viewport (): Viewport | null { return this._viewport; }
    get scissor (): Rect | null { return this._scissor; }
    private _sceneVisitor!: WebSceneVisitor;
    private _blitDesc: BlitDesc | null = null;
    private _queueId = -1;
    set queueId (val) { this._queueId = val; }
    get queueId (): number { return this._queueId; }
    set isUpdateUBO (update: boolean) { this._isUpdateUBO = update; }
    get isUpdateUBO (): boolean { return this._isUpdateUBO; }
    set isUploadInstance (value: boolean) { this._isUploadInstance = value; }
    get isUploadInstance (): boolean { return this._isUploadInstance; }
    set isUploadBatched (value: boolean) { this._isUploadBatched = value; }
    get isUploadBatched (): boolean { return this._isUploadBatched; }
    init (devicePass: DeviceRenderPass, renderQueue: RenderQueue, id: number): void {
        this.reset();
        this._graphQueue = renderQueue;
        this.queueHint = renderQueue.hint;
        const viewport = this._viewport = renderQueue.viewport;
        if (viewport) {
            this._scissor = new Rect(viewport.left, viewport.top, viewport.width, viewport.height);
        }
        this.queueId = id;
        this._devicePass = devicePass;
        this._phaseID = cclegacy.rendering.getPhaseID(devicePass.passID, context.renderGraph.getLayout(id));
        if (!this._sceneVisitor) {
            this._sceneVisitor = new WebSceneVisitor(
                context.commandBuffer,
                context.pipeline.pipelineSceneData,
            );
        }
    }
    createBlitDesc (blit: Blit): void {
        if (!this._blitDesc) {
            this._blitDesc = new BlitDesc(blit, this);
        }
        this._blitDesc.createScreenQuad();
        this._blitDesc.createStageDescriptor();
    }
    addSceneTask (scene: GraphScene): void {
        if (!this._transversal) {
            this._transversal = new DeviceSceneTransversal(this, context.pipelineSceneData,  scene);
        }
        this._transversal.graphScene = scene;
        this._preSceneTasks.push(this._transversal.preRenderPass(this._sceneVisitor));
        this._sceneTasks.push(this._transversal.transverse(this._sceneVisitor));
    }
    reset (): void {
        this._postSceneTasks.length = this._preSceneTasks.length = this._sceneTasks.length = 0;
        this._isUpdateUBO = false;
        this._isUploadInstance = false;
        this._isUploadBatched = false;
        this._blitDesc?.reset();
    }
    get graphQueue (): RenderQueue { return this._graphQueue; }
    get blitDesc (): BlitDesc | null { return this._blitDesc; }
    get sceneTasks (): DeviceSceneTask[] { return this._sceneTasks; }
    set queueHint (value: QueueHint) { this._hint = value; }
    get queueHint (): QueueHint { return this._hint; }
    get devicePass (): DeviceRenderPass { return this._devicePass!; }
    get preSceneTasks (): DevicePreSceneTask[] { return this._preSceneTasks; }
    preRecord (): void {
        for (const task of this._preSceneTasks) {
            task.start();
            task.join();
            task.submit();
        }
    }

    record (): void {
        if (this._descSetData && this._descSetData.descriptorSet) {
            context.commandBuffer
                .bindDescriptorSet(SetIndex.COUNT, this._descSetData.descriptorSet);
        }
        for (const task of this._sceneTasks) {
            task.start();
            task.join();
            task.submit();
        }
    }

    postRecord (): void {
        for (const task of this._postSceneTasks) {
            task.start();
            task.join();
            task.submit();
        }
    }
}

class SubmitInfo {
    public instances = new Set<InstancedBuffer>();
    public renderInstanceQueue: InstancedBuffer[] = [];
    public opaqueList: RenderInfo[] = [];
    public transparentList: RenderInfo[] = [];
    // <scene id, shadow queue>
    public shadowMap: Map<number, RenderShadowMapBatchedQueue> = new Map<number, RenderShadowMapBatchedQueue>();
    public additiveLight: RenderAdditiveLightQueue | null = null;
    public reflectionProbe: RenderReflectionProbeQueue | null = null;

    private _clearInstances (): void {
        const it = this.instances.values(); let res = it.next();
        while (!res.done) {
            res.value.clear();
            res = it.next();
        }
        this.instances.clear();
    }

    private _clearShadowMap (): void {
        for (const shadowMap of this.shadowMap) {
            shadowMap[1].clear();
        }
        this.shadowMap.clear();
    }

    reset (): void {
        this._clearInstances();
        this.renderInstanceQueue.length = 0;
        this.opaqueList.length = 0;
        this.transparentList.length = 0;
        this._clearShadowMap();
        this.additiveLight = null;
        this.reflectionProbe = null;
    }
}

class RenderPassLayoutInfo {
    protected _layoutID = 0;
    protected _stage: RenderStageData | null = null;
    protected _layout: PipelineLayoutData;
    protected _inputName: string;
    protected _descriptorSet: DescriptorSet | null = null;
    constructor (layoutId: number, input: [string, ComputeView[]]) {
        this._inputName = input[0];
        this._layoutID = layoutId;
        const lg = context.layoutGraph;
        this._stage = lg.getRenderStage(layoutId);
        this._layout = lg.getLayout(layoutId);
        const layoutData =  this._layout.descriptorSets.get(UpdateFrequency.PER_PASS);
        const globalDesc = context.pipeline.descriptorSet;
        if (layoutData) {
            // find resource
            const deviceTex = context.deviceTextures.get(this._inputName);
            const gfxTex = deviceTex?.texture;
            const layoutDesc = layoutData.descriptorSet!;
            if (!gfxTex) {
                throw Error(`Could not find texture with resource name ${this._inputName}`);
            }
            const resId = context.resourceGraph.vertex(this._inputName);
            const samplerInfo = context.resourceGraph.getSampler(resId);
            // bind descriptors
            for (const descriptor of input[1]) {
                const descriptorName = descriptor.name;
                const descriptorID = lg.attributeIndex.get(descriptorName);
                // find descriptor binding
                for (const block of layoutData.descriptorSetLayoutData.descriptorBlocks) {
                    for (let i = 0; i !== block.descriptors.length; ++i) {
                        // const buffer = layoutDesc.getBuffer(block.offset + i);
                        // const texture = layoutDesc.getTexture(block.offset + i);
                        if (descriptorID === block.descriptors[i].descriptorID) {
                            layoutDesc.bindTexture(block.offset + i, gfxTex);
                            layoutDesc.bindSampler(block.offset + i, context.device.getSampler(samplerInfo));
                            if (!this._descriptorSet) this._descriptorSet = layoutDesc;
                            continue;
                        }
                        // if (!buffer && !texture) {
                        //     layoutDesc.bindBuffer(block.offset + i, globalDesc.getBuffer(block.offset + i));
                        //     layoutDesc.bindTexture(block.offset + i, globalDesc.getTexture(block.offset + i));
                        //     layoutDesc.bindSampler(block.offset + i, globalDesc.getSampler(block.offset + i));
                        // }
                    }
                }
            }
        }
    }
    get descriptorSet (): DescriptorSet | null { return this._descriptorSet; }
    get layoutID (): number { return this._layoutID; }
    get stage (): RenderStageData | null { return this._stage; }
    get layout (): PipelineLayoutData { return this._layout; }
}

class RasterPassInfo {
    protected _id!: number;
    protected _pass!: RasterPass;
    get id (): number { return this._id; }
    get pass (): RasterPass { return this._pass; }
    private _copyPass (pass: RasterPass): void {
        const rasterPass = this._pass || new RasterPass();
        rasterPass.width = pass.width;
        rasterPass.height = pass.height;
        rasterPass.versionName = pass.versionName;
        rasterPass.version = pass.version;
        rasterPass.showStatistics = pass.showStatistics;
        rasterPass.viewport.copy(pass.viewport);
        for (const val of pass.rasterViews) {
            const currRasterKey = val[0];
            const currRasterView = val[1];
            const rasterView = rasterPass.rasterViews.get(currRasterKey) || new RasterView();
            rasterView.slotName = currRasterView.slotName;
            rasterView.accessType = currRasterView.accessType;
            rasterView.attachmentType = currRasterView.attachmentType;
            rasterView.loadOp = currRasterView.loadOp ? LoadOp.CLEAR : LoadOp.LOAD;
            rasterView.storeOp = currRasterView.storeOp;
            rasterView.clearFlags = currRasterView.clearFlags;
            rasterView.slotID = currRasterView.slotID;
            rasterView.clearColor.copy(currRasterView.clearColor);
            rasterPass.rasterViews.set(currRasterKey, rasterView);
        }
        for (const val of pass.computeViews) {
            const currComputeViews = val[1];
            const currComputeKey = val[0];
            const computeViews: ComputeView[] = rasterPass.computeViews.get(currComputeKey) || [];
            if (computeViews.length) computeViews.length = currComputeViews.length;
            let idx = 0;
            for (const currComputeView of currComputeViews) {
                const computeView = computeViews[idx] || new ComputeView();
                computeView.name = currComputeView.name;
                computeView.accessType = currComputeView.accessType;
                computeView.clearFlags = currComputeView.clearFlags;
                computeView.clearValue.x = currComputeView.clearValue.x;
                computeView.clearValue.y = currComputeView.clearValue.y;
                computeView.clearValue.z = currComputeView.clearValue.z;
                computeView.clearValue.w = currComputeView.clearValue.w;
                computeView.clearValueType = currComputeView.clearValueType;
                computeViews[idx] = computeView;
                idx++;
            }
            rasterPass.computeViews.set(currComputeKey, computeViews);
        }
        this._pass = rasterPass;
    }
    applyInfo (id: number, pass: RasterPass): void {
        this._id = id;
        this._copyPass(pass);
    }
}

const profilerViewport = new Viewport();
const renderPassArea = new Rect();
const resourceVisitor = new ResourceVisitor();
class DeviceRenderPass {
    protected _renderPass: RenderPass;
    protected _framebuffer: Framebuffer;
    protected _clearColor: Color[] = [];
    protected _deviceQueues: DeviceRenderQueue[] = [];
    protected _clearDepth = 1;
    protected _clearStencil = 0;
    protected _passID: number;
    protected _layoutName: string;
    protected _viewport: Viewport | null = null;
    private _rasterInfo: RasterPassInfo;
    private _layout: RenderPassLayoutInfo | null = null;
    constructor (passInfo: RasterPassInfo) {
        this._rasterInfo = passInfo;
        const device = context.device;
        this._layoutName = context.renderGraph.getLayout(passInfo.id);
        this._passID = cclegacy.rendering.getPassID(this._layoutName);
        const depthStencilAttachment = new DepthStencilAttachment();
        depthStencilAttachment.format = Format.DEPTH_STENCIL;
        depthStencilAttachment.depthLoadOp = LoadOp.DISCARD;
        depthStencilAttachment.stencilLoadOp = LoadOp.DISCARD;
        depthStencilAttachment.stencilStoreOp = StoreOp.DISCARD;
        depthStencilAttachment.depthStoreOp = StoreOp.DISCARD;

        const colors: ColorAttachment[] = [];
        const colorTexs: Texture[] = [];
        let depthTex: Texture | null = null;
        let swapchain: Swapchain | null = null;
        let framebuffer: Framebuffer | null = null;
        for (const cv of passInfo.pass.computeViews) {
            this._applyRenderLayout(cv);
        }
        // update the layout descriptorSet
        if (this.renderLayout && this.renderLayout.descriptorSet) {
            this.renderLayout.descriptorSet.update();
        }
        for (const [resName, rasterV] of passInfo.pass.rasterViews) {
            let resTex = context.deviceTextures.get(resName);
            if (!resTex) {
                this.visitResource(resName);
                resTex = context.deviceTextures.get(resName)!;
            } else {
                const resGraph = context.resourceGraph;
                const resId = resGraph.vertex(resName);
                const resFbo = resGraph._vertices[resId]._object;
                if (resTex.framebuffer && resFbo instanceof Framebuffer && resTex.framebuffer !== resFbo) {
                    resTex.framebuffer = resFbo;
                } else if (resTex.texture) {
                    const desc = resGraph.getDesc(resId);
                    if (resTex.texture.width !== desc.width || resTex.texture.height !== desc.height) {
                        resTex.texture.resize(desc.width, desc.height);
                    }
                }
            }
            if (!swapchain) swapchain = resTex.swapchain;
            if (!framebuffer) framebuffer = resTex.framebuffer;
            const clearFlag = rasterV.clearFlags & 0xffffffff;
            switch (rasterV.attachmentType) {
            case AttachmentType.RENDER_TARGET:
                {
                    if (!resTex.swapchain && !resTex.framebuffer) colorTexs.push(resTex.texture!);
                    const colorAttachment = new ColorAttachment();
                    colorAttachment.format = resTex.description!.format;
                    colorAttachment.sampleCount = resTex.description!.sampleCount;
                    colorAttachment.loadOp = rasterV.loadOp;
                    colorAttachment.storeOp = rasterV.storeOp;
                    colorAttachment.barrier = device.getGeneralBarrier(new GeneralBarrierInfo(
                        rasterV.loadOp === LoadOp.LOAD ? AccessFlagBit.COLOR_ATTACHMENT_WRITE : AccessFlagBit.NONE,
                        rasterV.storeOp === StoreOp.STORE ? AccessFlagBit.COLOR_ATTACHMENT_WRITE : AccessFlagBit.NONE,
                    ));
                    this._clearColor.push(rasterV.clearColor);
                    colors.push(colorAttachment);
                }
                break;
            case AttachmentType.DEPTH_STENCIL:
                depthStencilAttachment.depthStoreOp = rasterV.storeOp;
                depthStencilAttachment.stencilStoreOp = rasterV.storeOp;
                depthStencilAttachment.depthLoadOp = rasterV.loadOp;
                depthStencilAttachment.stencilLoadOp = rasterV.loadOp;
                depthStencilAttachment.barrier = device.getGeneralBarrier(new GeneralBarrierInfo(
                    rasterV.loadOp === LoadOp.LOAD ? AccessFlagBit.DEPTH_STENCIL_ATTACHMENT_WRITE : AccessFlagBit.NONE,
                    rasterV.storeOp === StoreOp.STORE ? AccessFlagBit.DEPTH_STENCIL_ATTACHMENT_WRITE : AccessFlagBit.NONE,
                ));
                if (!resTex.swapchain && !resTex.framebuffer) depthTex = resTex.texture!;
                this._clearDepth = rasterV.clearColor.x;
                this._clearStencil = rasterV.clearColor.y;
                break;
            case AttachmentType.SHADING_RATE:
                // noop
                break;
            default:
            }
        }
        if (colors.length === 0) {
            const colorAttachment = new ColorAttachment();
            colors.push(colorAttachment);
        }
        if (colorTexs.length === 0 && !swapchain && !framebuffer) {
            const currTex = device.createTexture(new TextureInfo());
            colorTexs.push(currTex);
        }
        // if (!depthTex && !swapchain && !framebuffer) {
        //     depthTex = device.createTexture(new TextureInfo(
        //         TextureType.TEX2D,
        //         TextureUsageBit.DEPTH_STENCIL_ATTACHMENT | TextureUsageBit.SAMPLED,
        //         Format.DEPTH_STENCIL,
        //         colorTexs[0].width,
        //         colorTexs[0].height,
        //     ));
        // }
        this._renderPass = device.createRenderPass(new RenderPassInfo(colors, depthStencilAttachment));
        this._framebuffer = framebuffer || device.createFramebuffer(new FramebufferInfo(
            this._renderPass,
            swapchain ? [swapchain.colorTexture] : colorTexs,
            swapchain ? swapchain.depthStencilTexture : depthTex,
        ));
    }
    get layoutName (): string { return this._layoutName; }
    get passID (): number { return this._passID; }
    get renderLayout (): RenderPassLayoutInfo | null { return this._layout; }
    get renderPass (): RenderPass { return this._renderPass; }
    get framebuffer (): Framebuffer { return this._framebuffer; }
    get clearColor (): Color[] { return this._clearColor; }
    get clearDepth (): number { return this._clearDepth; }
    get clearStencil (): number { return this._clearStencil; }
    get deviceQueues (): DeviceRenderQueue[] { return this._deviceQueues; }
    get rasterPassInfo (): RasterPassInfo { return this._rasterInfo; }
    get viewport (): Viewport | null { return this._viewport; }
    visitResource (resName: string): void {
        const resourceGraph = context.resourceGraph;
        const vertId = resourceGraph.vertex(resName);
        resourceVisitor.resName = resName;
        resourceGraph.visitVertex(resourceVisitor, vertId);
    }
    addQueue (queue: DeviceRenderQueue): void { this._deviceQueues.push(queue); }
    prePass (): void {
        for (const queue of this._deviceQueues) {
            queue.preRecord();
        }
    }
    protected _applyRenderLayout (input: [string, ComputeView[]]): void {
        const stageName = context.renderGraph.getLayout(this.rasterPassInfo.id);
        if (stageName) {
            const layoutGraph = context.layoutGraph;
            const stageId = layoutGraph.locateChild(layoutGraph.nullVertex(), stageName);
            if (stageId !== 0xFFFFFFFF) {
                this._layout = new RenderPassLayoutInfo(stageId, input);
            }
        }
    }
    getGlobalDescData (): DescriptorSetData {
        const stageId = context.layoutGraph.locateChild(context.layoutGraph.nullVertex(), 'default');
        assert(stageId !== 0xFFFFFFFF);
        const layout = context.layoutGraph.getLayout(stageId);
        const layoutData = layout.descriptorSets.get(UpdateFrequency.PER_PASS)!;
        return layoutData;
    }

    protected _applyViewport (frameTex: Texture): void {
        this._viewport = null;
        const viewport = this._rasterInfo.pass.viewport;
        if (viewport.left !== 0
            || viewport.top !== 0
            || viewport.width !== 0
            || viewport.height !== 0) {
            this._viewport = viewport;
        }
    }

    protected _showProfiler (rect: Rect): void {
        const profiler = context.pipeline.profiler!;
        if (!profiler || !profiler.enabled) {
            return;
        }
        const renderPass = this._renderPass;
        const cmdBuff = context.commandBuffer;
        const submodel = profiler.subModels[0];
        const pass = submodel.passes[0];
        const ia = submodel.inputAssembler;
        const device = context.device;
        const pso = PipelineStateManager.getOrCreatePipelineState(
            device,
            pass,
            submodel.shaders[0],
            renderPass,
            ia,
        );
        const descData = getDescriptorSetDataFromLayoutId(pass.passID)!;
        mergeSrcToTargetDesc(descData.descriptorSet, context.pipeline.descriptorSet, true);
        profilerViewport.width = rect.width;
        profilerViewport.height = rect.height;
        cmdBuff.setViewport(profilerViewport);
        cmdBuff.setScissor(rect);
        cmdBuff.bindPipelineState(pso);
        cmdBuff.bindDescriptorSet(SetIndex.MATERIAL, pass.descriptorSet);
        cmdBuff.bindDescriptorSet(SetIndex.LOCAL, submodel.descriptorSet);
        cmdBuff.bindInputAssembler(ia);
        cmdBuff.draw(ia);
    }

    // record common buffer
    record (): void {
        const tex = this.framebuffer.colorTextures[0]!;
        this._applyViewport(tex);
        const cmdBuff = context.commandBuffer;
        if (this._viewport) {
            renderPassArea.x = this._viewport.left;
            renderPassArea.y = this._viewport.top;
            renderPassArea.width = this._viewport.width;
            renderPassArea.height = this._viewport.height;
        } else {
            renderPassArea.y = renderPassArea.x = 0;
            renderPassArea.width = tex.width;
            renderPassArea.height = tex.height;
        }
        cmdBuff.beginRenderPass(
            this.renderPass,
            this.framebuffer,
            renderPassArea,
            this.clearColor,
            this.clearDepth,
            this.clearStencil,
        );
        cmdBuff.bindDescriptorSet(
            SetIndex.GLOBAL,
            context.pipeline.descriptorSet,
        );
        for (const queue of this._deviceQueues) {
            queue.record();
        }
        if (this._rasterInfo.pass.showStatistics) {
            this._showProfiler(renderPassArea);
        }
        cmdBuff.endRenderPass();
    }

    private _clear (): void {
        for (const [cam, infoMap] of context.submitMap) {
            for (const [id, info] of infoMap) {
                info.additiveLight?.clear();
                const it = info.instances.values(); let res = it.next();
                while (!res.done) {
                    res.value.clear();
                    res = it.next();
                }
                info.renderInstanceQueue = [];
                info.instances.clear();
            }
        }
    }

    postPass (): void {
        this._clear();
        // this.submitMap.clear();
        for (const queue of this._deviceQueues) {
            queue.postRecord();
        }
    }
    resetResource (id: number, pass: RasterPass): void {
        this._rasterInfo.applyInfo(id, pass);
        this._layoutName = context.renderGraph.getLayout(id);
        this._passID = cclegacy.rendering.getPassID(this._layoutName);
        this._deviceQueues.length = 0;
        let framebuffer: Framebuffer | null = null;
        const colTextures: Texture[] = [];
        let depTexture = this._framebuffer ? this._framebuffer.depthStencilTexture : null;
        for (const cv of this._rasterInfo.pass.computeViews) {
            this._applyRenderLayout(cv);
        }
        // update the layout descriptorSet
        if (this.renderLayout && this.renderLayout.descriptorSet) {
            this.renderLayout.descriptorSet.update();
        }
        for (const [resName, rasterV] of this._rasterInfo.pass.rasterViews) {
            let deviceTex = context.deviceTextures.get(resName)!;
            const currTex = deviceTex;
            if (!deviceTex) {
                this.visitResource(resName);
                deviceTex = context.deviceTextures.get(resName)!;
            }
            const resGraph = context.resourceGraph;
            const resId = resGraph.vertex(resName);
            const resFbo = resGraph._vertices[resId]._object;
            const resDesc = resGraph.getDesc(resId);
            if (deviceTex.framebuffer && resFbo instanceof Framebuffer && deviceTex.framebuffer !== resFbo) {
                framebuffer = this._framebuffer = deviceTex.framebuffer = resFbo;
            } else if (!currTex || (deviceTex.texture
                && (deviceTex.texture.width !== resDesc.width || deviceTex.texture.height !== resDesc.height))) {
                const gfxTex = deviceTex.texture!;
                if (currTex) gfxTex.resize(resDesc.width, resDesc.height);
                switch (rasterV.attachmentType) {
                case AttachmentType.RENDER_TARGET:
                    colTextures.push(gfxTex);
                    break;
                case AttachmentType.DEPTH_STENCIL:
                    depTexture = gfxTex;
                    break;
                case AttachmentType.SHADING_RATE:
                    // noop
                    break;
                default:
                }
            }
        }
        if (!framebuffer && colTextures.length) {
            this._framebuffer.destroy();
            this._framebuffer = context.device.createFramebuffer(new FramebufferInfo(
                this._renderPass,
                colTextures,
                depTexture,
            ));
        }
    }
}

class DeviceSceneTransversal extends WebSceneTransversal {
    protected _currentQueue: DeviceRenderQueue;
    protected _graphScene: GraphScene;
    protected _preSceneTask: DevicePreSceneTask | undefined;
    protected _sceneTask: DeviceSceneTask | undefined;
    protected _postSceneTask: DevicePostSceneTask | undefined;
    constructor (quque: DeviceRenderQueue, sceneData: PipelineSceneData, graphSceneData: GraphScene) {
        const camera = graphSceneData.scene ? graphSceneData.scene.camera : null;
        super(camera, sceneData, context.ubo);
        this._currentQueue = quque;
        this._graphScene = graphSceneData;
    }
    set graphScene (graphScene: GraphScene) {
        this._graphScene = graphScene;
        this._camera = graphScene.scene ? graphScene.scene.camera : null;
        if (this._camera) this._scene = this._camera.scene!;
    }
    get graphScene (): GraphScene { return this._graphScene; }
    public preRenderPass (visitor: WebSceneVisitor): DevicePreSceneTask {
        if (!this._preSceneTask) {
            this._preSceneTask = new DevicePreSceneTask(this._currentQueue, this._graphScene, visitor);
        }
        this._preSceneTask.apply(this._currentQueue, this.graphScene);
        return this._preSceneTask;
    }
    public transverse (visitor: WebSceneVisitor): DeviceSceneTask {
        if (!this._sceneTask) {
            this._sceneTask = new DeviceSceneTask(this._currentQueue, this._graphScene, visitor);
        }
        this._sceneTask.apply(this._currentQueue, this.graphScene);
        return this._sceneTask;
    }
    public postRenderPass (visitor: WebSceneVisitor): DevicePostSceneTask {
        if (!this._postSceneTask) {
            this._postSceneTask = new DevicePostSceneTask(this._sceneData, context.ubo, this._camera, visitor);
        }
        return this._postSceneTask;
    }
}
class GraphScene {
    scene: SceneData | null = null;
    blit: Blit | null = null;
    dispatch: Dispatch | null = null;
    sceneID = -1;
    private _copyScene (scene: SceneData | null): void {
        if (scene) {
            if (!this.scene) {
                this.scene = new SceneData();
            }
            this.scene.scene = scene.scene;
            this.scene.light.level = scene.light.level;
            this.scene.light.light = scene.light.light;
            this.scene.flags = scene.flags;
            this.scene.camera = scene.camera;
            return;
        }
        this.scene = null;
    }
    private _copyBlit (blit: Blit | null): void {
        if (blit) {
            if (!this.blit) {
                this.blit = new Blit(blit.material, blit.passID, blit.sceneFlags, blit.camera);
            }
            this.blit.material = blit.material;
            this.blit.passID = blit.passID;
            this.blit.sceneFlags = blit.sceneFlags;
            this.blit.camera = blit.camera;
            return;
        }
        this.blit = null;
    }
    init (scene: SceneData | null, blit: Blit | null, sceneID): void {
        this._copyScene(scene);
        this._copyBlit(blit);
        this.sceneID = sceneID;
    }
}
class DevicePreSceneTask extends WebSceneTask {
    protected _currentQueue: DeviceRenderQueue;
    protected _renderPass: RenderPass;
    protected _submitInfo: SubmitInfo | null = null;
    protected _graphScene: GraphScene;
    private _cmdBuff: CommandBuffer;
    constructor (queue: DeviceRenderQueue, graphScene: GraphScene, visitor: SceneVisitor) {
        super(
            context.pipelineSceneData,
            context.ubo,
            graphScene.scene && graphScene.scene.camera ? graphScene.scene.camera : null,
            visitor,
        );
        this._currentQueue = queue;
        this._graphScene = graphScene;
        this._renderPass = queue.devicePass.renderPass;
        this._cmdBuff = context.commandBuffer;
    }
    apply (queue: DeviceRenderQueue, graphScene: GraphScene): void {
        this._currentQueue = queue;
        this._graphScene = graphScene;
        this._renderPass = queue.devicePass.renderPass;
        this._cmdBuff = context.commandBuffer;
        const camera = graphScene.scene && graphScene.scene.camera ? graphScene.scene.camera : null;
        if (camera) {
            this._scene = camera.scene!;
            this._camera = camera;
        }
    }
    get graphScene (): GraphScene { return this._graphScene; }

    public start (): void {
        if (this.graphScene.blit) {
            this._currentQueue.createBlitDesc(this.graphScene.blit);
        }
    }

    public submit (): void {
        if (this.graphScene.blit) {
            this._currentQueue.blitDesc!.update();
        }
        // if (isShadowMap(this.graphScene)) {

        // }
        // this._uploadInstanceBuffers();
    }
}

const sceneViewport = new Viewport();
class DeviceSceneTask extends WebSceneTask {
    protected _currentQueue: DeviceRenderQueue;
    protected _renderPass: RenderPass;
    protected _graphScene: GraphScene;
    constructor (queue: DeviceRenderQueue, graphScene: GraphScene, visitor: SceneVisitor) {
        super(
            context.pipelineSceneData,
            context.ubo,
            graphScene.scene && graphScene.scene.camera ? graphScene.scene.camera : null,
            visitor,
        );
        this._currentQueue = queue;
        this._renderPass = this._currentQueue.devicePass.renderPass;
        this._graphScene = graphScene;
    }
    apply (queue: DeviceRenderQueue, graphScene: GraphScene): void {
        this._currentQueue = queue;
        this._graphScene = graphScene;
        this._renderPass = queue.devicePass.renderPass;
        const camera = graphScene.scene && graphScene.scene.camera ? graphScene.scene.camera : null;
        if (camera) {
            this._scene = camera.scene!;
            this._camera = camera;
        }
    }
    get graphScene (): GraphScene { return this._graphScene; }
    public start (): void {}
    protected _recordRenderList (isTransparent: boolean): void {
        const submitMap = context.submitMap;
        const currSubmitInfo = submitMap.get(this.camera!)!.get(this._currentQueue.phaseID)!;
        const renderList = isTransparent ? currSubmitInfo.transparentList
            : currSubmitInfo.opaqueList;
        for (let i = 0; i < renderList.length; ++i) {
            const { subModel, passIdx } = renderList[i];
            const inputAssembler: InputAssembler = subModel.inputAssembler;
            const pass: Pass = subModel.passes[passIdx];
            const shader: Shader = subModel.shaders[passIdx];
            const descriptorSet: DescriptorSet = subModel.descriptorSet;
            const pso = PipelineStateManager.getOrCreatePipelineState(
                deviceManager.gfxDevice,
                pass,
                shader,
                this._renderPass,
                inputAssembler,
            );
            this.visitor.bindPipelineState(pso);
            this.visitor.bindDescriptorSet(SetIndex.MATERIAL, pass.descriptorSet);
            this.visitor.bindDescriptorSet(SetIndex.LOCAL, descriptorSet);
            this.visitor.bindInputAssembler(inputAssembler);
            this.visitor.draw(inputAssembler);
        }
    }
    protected _recordOpaqueList (): void {
        this._recordRenderList(false);
    }
    protected _recordInstences (): void {
        const submitMap = context.submitMap;
        const currSubmitInfo = submitMap.get(this.camera!)!.get(this._currentQueue.phaseID)!;
        const it = currSubmitInfo.renderInstanceQueue.length === 0
            ? currSubmitInfo.instances.values()
            : currSubmitInfo.renderInstanceQueue.values();
        let res = it.next();
        while (!res.done) {
            const { instances, pass, hasPendingModels } = res.value;
            if (hasPendingModels) {
                this.visitor.bindDescriptorSet(SetIndex.MATERIAL, pass.descriptorSet);
                let lastPSO: PipelineState | null = null;
                for (let b = 0; b < instances.length; ++b) {
                    const instance = instances[b];
                    if (!instance.count) { continue; }
                    const shader = instance.shader!;
                    const pso = PipelineStateManager.getOrCreatePipelineState(
                        deviceManager.gfxDevice,
                        pass,
                        shader,
                        this._renderPass,
                        instance.ia,
                    );
                    if (lastPSO !== pso) {
                        this.visitor.bindPipelineState(pso);
                        lastPSO = pso;
                    }
                    const ia: InputAssembler = instance.ia;
                    this.visitor.bindDescriptorSet(SetIndex.LOCAL, instance.descriptorSet, res.value.dynamicOffsets);
                    this.visitor.bindInputAssembler(ia);
                    this.visitor.draw(ia);
                }
            }
            res = it.next();
        }
    }
    protected _recordUI (): void {
        const batches = this.camera!.scene!.batches;
        for (let i = 0; i < batches.length; i++) {
            const batch = batches[i];
            let visible = false;
            if (this.camera!.visibility & batch.visFlags) {
                visible = true;
            }

            if (!visible) continue;
            // shaders.length always equals actual used passes.length
            const count = batch.shaders.length;
            for (let j = 0; j < count; j++) {
                const pass = batch.passes[j];
                if (pass.phaseID !== this._currentQueue.phaseID) continue;
                const shader = batch.shaders[j];
                const inputAssembler: InputAssembler = batch.inputAssembler!;
                const pso = PipelineStateManager.getOrCreatePipelineState(deviceManager.gfxDevice, pass, shader, this._renderPass, inputAssembler);
                this.visitor.bindPipelineState(pso);
                this.visitor.bindDescriptorSet(SetIndex.MATERIAL, pass.descriptorSet);
                const ds = batch.descriptorSet!;
                this.visitor.bindDescriptorSet(SetIndex.LOCAL, ds);
                this.visitor.bindInputAssembler(inputAssembler);
                this.visitor.draw(inputAssembler);
            }
        }
    }
    protected _recordTransparentList (): void {
        this._recordRenderList(true);
    }
    protected _recordShadowMap (): void {
        const submitMap = context.submitMap;
        const currSubmitInfo = submitMap.get(this.camera!)!.get(this._currentQueue.phaseID)!;
        currSubmitInfo.shadowMap.get(this.graphScene.sceneID)!.recordCommandBuffer(
            context.device,
            this._renderPass,

            context.commandBuffer,
        );
    }
    protected _recordReflectionProbe (): void {
        const submitMap = context.submitMap;
        const currSubmitInfo = submitMap.get(this.camera!)!.get(this._currentQueue.phaseID)!;
        currSubmitInfo.reflectionProbe?.recordCommandBuffer(
            context.device,
            this._renderPass,

            context.commandBuffer,
        );
    }

    private _clearExtBlitDesc (desc, extResId: number[]): void {
        const toGpuDesc = desc.gpuDescriptorSet;
        for (let i = 0; i < extResId.length; i++) {
            const currDesc = toGpuDesc.gpuDescriptors[extResId[i]];
            if (currDesc.gpuBuffer) currDesc.gpuBuffer = null;
            else if (currDesc.gpuTextureView) {
                currDesc.gpuTextureView = null;
                currDesc.gpuSampler = null;
            } else if (currDesc.gpuTexture) {
                currDesc.gpuTexture = null;
                currDesc.gpuSampler = null;
            }
        }
    }

    private _recordBlit (): void {
        if (!this.graphScene.blit) { return; }

        const blit = this.graphScene.blit;
        const currMat = blit.material;
        const pass = currMat!.passes[blit.passID];
        pass.update();
        const shader = pass.getShaderVariant();
        const devicePass = this._currentQueue.devicePass;
        const screenIa: InputAssembler = this._currentQueue.blitDesc!.screenQuad!.quadIA!;
        const globalDesc = context.pipeline.descriptorSet;
        let pso: PipelineState | null = null;
        if (pass !== null && shader !== null && screenIa !== null) {
            pso = PipelineStateManager.getOrCreatePipelineState(
                context.device,
                pass,
                shader,
                devicePass.renderPass,
                screenIa,
            );
        }
        if (pso) {
            this.visitor.bindPipelineState(pso);
            const layoutStage = devicePass.renderLayout;
            const layoutDesc = layoutStage!.descriptorSet!;
            const extResId: number[] = [];
            // if (isEnableEffect()) this.visitor.bindDescriptorSet(SetIndex.GLOBAL, layoutDesc);
            this.visitor.bindDescriptorSet(SetIndex.MATERIAL, pass.descriptorSet);
            this.visitor.bindDescriptorSet(SetIndex.LOCAL, this._currentQueue.blitDesc!.stageDesc!);
            this.visitor.bindInputAssembler(screenIa);
            this.visitor.draw(screenIa);
            // The desc data obtained from the outside should be cleaned up so that the data can be modified
            this._clearExtBlitDesc(layoutDesc, extResId);
            // if (isEnableEffect()) this.visitor.bindDescriptorSet(SetIndex.GLOBAL, globalDesc);
        }
    }
    private _recordAdditiveLights (): void {
        const devicePass = this._currentQueue.devicePass;
        const submitMap = context.submitMap;
        const currSubmitInfo = submitMap.get(this.camera!)!.get(this._currentQueue.phaseID)!;
        currSubmitInfo.additiveLight?.recordCommandBuffer(
            context.device,
            this._renderPass,
            context.commandBuffer,
        );
    }
    protected _updateGlobal (data: RenderData): void {
        const devicePass = this._currentQueue.devicePass;
        updateGlobalDescBinding(data, context.renderGraph.getLayout(devicePass.rasterPassInfo.id));
    }

    protected _updateRenderData (): void {
        if (this._currentQueue.isUpdateUBO) return;
        const devicePass = this._currentQueue.devicePass;
        const rasterId = devicePass.rasterPassInfo.id;
        const passRenderData = context.renderGraph.getData(rasterId);
        // CCGlobal
        this._updateGlobal(passRenderData);
        // CCCamera, CCShadow, CCCSM
        const queueId = this._currentQueue.queueId;
        const queueRenderData = context.renderGraph.getData(queueId)!;
        this._updateGlobal(queueRenderData);

        const layoutName = context.renderGraph.getLayout(rasterId);
        const descSetData = getDescriptorSetDataFromLayout(layoutName);
        mergeSrcToTargetDesc(descSetData!.descriptorSet, context.pipeline.descriptorSet, true);
        this._currentQueue.isUpdateUBO = true;
    }
    public submit (): void {
        const devicePass = this._currentQueue.devicePass;
        const queueViewport = this._currentQueue.viewport;
        const sceneCulling = context.culling;
        this._updateRenderData();
        if (queueViewport) {
            this.visitor.setViewport(queueViewport);
            this.visitor.setScissor(this._currentQueue.scissor!);
        } else if (!this._currentQueue.devicePass.viewport) {
            const texture = this._currentQueue.devicePass.framebuffer.colorTextures[0]!;
            const graphScene = this.graphScene;
            const lightInfo = graphScene.scene ? graphScene.scene.light : null;
            const area = isShadowMap(this.graphScene) && graphScene.scene && lightInfo!.light
                ? getRenderArea(this.camera!, texture.width, texture.height, lightInfo!.light, lightInfo!.level)
                : getRenderArea(this.camera!, texture.width, texture.height);
            sceneViewport.left = area.x;
            sceneViewport.top = area.y;
            sceneViewport.width = area.width;
            sceneViewport.height = area.height;
            this.visitor.setViewport(sceneViewport);
            this.visitor.setScissor(area);
        }
        // Currently processing blit and camera first
        if (this.graphScene.blit) {
            this._recordBlit();
            return;
        }
        const renderQueueDesc = sceneCulling.sceneQueryIndex.get(this.graphScene.sceneID)!;
        const renderQueue =  sceneCulling.renderQueues[renderQueueDesc.renderQueueTarget];
        const graphSceneData = this.graphScene.scene!;
        if (graphSceneData.flags & SceneFlags.OPAQUE_OBJECT
            || graphSceneData.flags & SceneFlags.CUTOUT_OBJECT) {
            renderQueue.opaqueQueue.recordCommandBuffer(deviceManager.gfxDevice, this._renderPass, context.commandBuffer);
        }
        renderQueue.opaqueInstancingQueue.recordCommandBuffer(this._renderPass, context.commandBuffer);
        renderQueue.transparentInstancingQueue.recordCommandBuffer(this._renderPass, context.commandBuffer);
        // if (graphSceneData.flags & SceneFlags.DEFAULT_LIGHTING) {
        //     this._recordAdditiveLights();
        // }
        // this.visitor.bindDescriptorSet(SetIndex.GLOBAL,
        //     context.pipeline.descriptorSet);
        if (graphSceneData.flags & SceneFlags.TRANSPARENT_OBJECT) {
            // this._recordTransparentList();
            renderQueue.transparentQueue.recordCommandBuffer(deviceManager.gfxDevice, this._renderPass, context.commandBuffer);
        }
        if (graphSceneData.flags & SceneFlags.GEOMETRY) {
            this.camera!.geometryRenderer?.render(
                devicePass.renderPass,
                context.commandBuffer,

                context.pipeline.pipelineSceneData,
            );
        }
        if (graphSceneData.flags & SceneFlags.UI) {
            this._recordUI();
        }
        if (graphSceneData.flags & SceneFlags.REFLECTION_PROBE) {
            this._recordReflectionProbe();
        }
    }
}

class DevicePostSceneTask extends WebSceneTask {}

class ExecutorPools {
    constructor (context: ExecutorContext) {
        this.deviceQueuePool = new RecyclePool<DeviceRenderQueue>((): DeviceRenderQueue => new DeviceRenderQueue(), 16);
        this.graphScenePool = new RecyclePool<GraphScene>((): GraphScene => new GraphScene(), 16);
        this.rasterPassInfoPool = new RecyclePool<RasterPassInfo>((): RasterPassInfo => new RasterPassInfo(), 16);
        this.reflectionProbe = new RecyclePool<RenderReflectionProbeQueue>((): RenderReflectionProbeQueue => new RenderReflectionProbeQueue(context.pipeline), 8);
        this.passPool = new RecyclePool<IRenderPass>((): { priority: number; hash: number; depth: number; shaderId: number; subModel: any; passIdx: number; } => ({
            priority: 0,
            hash: 0,
            depth: 0,
            shaderId: 0,
            subModel: null!,
            passIdx: 0,
        }), 64);
    }
    addPassInfo (): IRenderPass {
        return this.passPool.add();
    }
    resetPassInfo (): void {
        this.passPool.reset();
    }
    addDeviceQueue (): DeviceRenderQueue {
        return this.deviceQueuePool.add();
    }
    addGraphScene (): GraphScene {
        return this.graphScenePool.add();
    }
    addReflectionProbe (): RenderReflectionProbeQueue {
        return this.reflectionProbe.add();
    }
    addRasterPassInfo (): RasterPassInfo {
        return this.rasterPassInfoPool.add();
    }
    reset (): void {
        this.deviceQueuePool.reset();
        this.graphScenePool.reset();
        this.reflectionProbe.reset();
        this.resetPassInfo();
    }
    readonly deviceQueuePool: RecyclePool<DeviceRenderQueue>;
    readonly graphScenePool: RecyclePool<GraphScene>;
    readonly reflectionProbe: RecyclePool<RenderReflectionProbeQueue>;
    readonly passPool: RecyclePool<IRenderPass>;
    readonly rasterPassInfoPool: RecyclePool<RasterPassInfo>;
}

const vbData = new Float32Array(4 * 4);
const quadRect = new Rect();
// The attribute length of the volume light
const volLightAttrCount = 5;

class BlitInfo {
    private _pipelineIAData: PipelineInputAssemblerData;
    private _context: ExecutorContext;
    private _width: number;
    private _height: number;
    private _lightVolumeBuffer!: Buffer;
    private _lightBufferData!: Float32Array;
    private _deferredLitsBufView!: Buffer;
    private _localUBO!: Buffer;
    private _stageDescs: Map<Pass, DescriptorSet> = new Map();
    get pipelineIAData (): PipelineInputAssemblerData {
        return this._pipelineIAData;
    }
    get deferredLitsBufView (): Buffer {
        return this._deferredLitsBufView;
    }
    get lightVolumeBuffer (): Buffer {
        return this._lightVolumeBuffer;
    }
    get lightBufferData (): Float32Array {
        return this._lightBufferData;
    }
    get stageDescs (): Map<Pass, DescriptorSet> {
        return this._stageDescs;
    }
    get emptyLocalUBO (): Buffer {
        return this._localUBO;
    }
    constructor (context: ExecutorContext) {
        this._context = context;
        this._width = context.width;
        this._height = context.height;
        this._pipelineIAData = this._createQuadInputAssembler();
        const vb = this._genQuadVertexData(SurfaceTransform.IDENTITY, new Rect(0, 0, context.width, context.height));
        this._pipelineIAData.quadVB!.update(vb);
        this._createLightVolumes();
        this._localUBO = context.device.createBuffer(new BufferInfo(
            BufferUsageBit.UNIFORM | BufferUsageBit.TRANSFER_DST,
            MemoryUsageBit.DEVICE,
            UBOLocal.SIZE,
            UBOLocal.SIZE,
        ));
    }

    resize (width, height): void {
        if (width !== this._width || height !== this._height) {
            quadRect.y = quadRect.x = 0;
            quadRect.width = width;
            quadRect.height = height;
            const vb = this._genQuadVertexData(SurfaceTransform.IDENTITY, quadRect);
            this._pipelineIAData.quadVB!.update(vb);
        }
    }

    private _createLightVolumes (): void {
        const device = this._context.root.device;
        let totalSize = Float32Array.BYTES_PER_ELEMENT * volLightAttrCount * 4 * UBODeferredLight.LIGHTS_PER_PASS;
        totalSize = Math.ceil(totalSize / device.capabilities.uboOffsetAlignment) * device.capabilities.uboOffsetAlignment;

        this._lightVolumeBuffer = device.createBuffer(new BufferInfo(
            BufferUsageBit.UNIFORM | BufferUsageBit.TRANSFER_DST,
            MemoryUsageBit.HOST | MemoryUsageBit.DEVICE,
            totalSize,
            device.capabilities.uboOffsetAlignment,
        ));

        this._deferredLitsBufView = device.createBuffer(new BufferViewInfo(this._lightVolumeBuffer, 0, totalSize));
        this._lightBufferData = new Float32Array(totalSize / Float32Array.BYTES_PER_ELEMENT);
    }

    private _genQuadVertexData (surfaceTransform: SurfaceTransform, renderArea: Rect): Float32Array {
        const minX = renderArea.x / this._context.width;
        const maxX = (renderArea.x + renderArea.width) / this._context.width;
        let minY = renderArea.y / this._context.height;
        let maxY = (renderArea.y + renderArea.height) / this._context.height;
        if (this._context.root.device.capabilities.screenSpaceSignY > 0) {
            const temp = maxY;
            maxY       = minY;
            minY       = temp;
        }
        let n = 0;
        switch (surfaceTransform) {
        case (SurfaceTransform.IDENTITY):
            n = 0;
            vbData[n++] = -1.0; vbData[n++] = -1.0; vbData[n++] = minX; vbData[n++] = maxY;
            vbData[n++] = 1.0; vbData[n++] = -1.0; vbData[n++] = maxX; vbData[n++] = maxY;
            vbData[n++] = -1.0; vbData[n++] = 1.0; vbData[n++] = minX; vbData[n++] = minY;
            vbData[n++] = 1.0; vbData[n++] = 1.0; vbData[n++] = maxX; vbData[n++] = minY;
            break;
        case (SurfaceTransform.ROTATE_90):
            n = 0;
            vbData[n++] = -1.0; vbData[n++] = -1.0; vbData[n++] = maxX; vbData[n++] = maxY;
            vbData[n++] = 1.0; vbData[n++] = -1.0; vbData[n++] = maxX; vbData[n++] = minY;
            vbData[n++] = -1.0; vbData[n++] = 1.0; vbData[n++] = minX; vbData[n++] = maxY;
            vbData[n++] = 1.0; vbData[n++] = 1.0; vbData[n++] = minX; vbData[n++] = minY;
            break;
        case (SurfaceTransform.ROTATE_180):
            n = 0;
            vbData[n++] = -1.0; vbData[n++] = -1.0; vbData[n++] = minX; vbData[n++] = minY;
            vbData[n++] = 1.0; vbData[n++] = -1.0; vbData[n++] = maxX; vbData[n++] = minY;
            vbData[n++] = -1.0; vbData[n++] = 1.0; vbData[n++] = minX; vbData[n++] = maxY;
            vbData[n++] = 1.0; vbData[n++] = 1.0; vbData[n++] = maxX; vbData[n++] = maxY;
            break;
        case (SurfaceTransform.ROTATE_270):
            n = 0;
            vbData[n++] = -1.0; vbData[n++] = -1.0; vbData[n++] = minX; vbData[n++] = minY;
            vbData[n++] = 1.0; vbData[n++] = -1.0; vbData[n++] = minX; vbData[n++] = maxY;
            vbData[n++] = -1.0; vbData[n++] = 1.0; vbData[n++] = maxX; vbData[n++] = minY;
            vbData[n++] = 1.0; vbData[n++] = 1.0; vbData[n++] = maxX; vbData[n++] = maxY;
            break;
        default:
            break;
        }

        return vbData;
    }
    private _createQuadInputAssembler (): PipelineInputAssemblerData {
        // create vertex buffer
        const inputAssemblerData = new PipelineInputAssemblerData();

        const vbStride = Float32Array.BYTES_PER_ELEMENT * 4;
        const vbSize = vbStride * 4;
        const device = cclegacy.director.root.device;
        const quadVB: Buffer = device.createBuffer(new BufferInfo(
            BufferUsageBit.VERTEX | BufferUsageBit.TRANSFER_DST,
            MemoryUsageBit.DEVICE | MemoryUsageBit.HOST,
            vbSize,
            vbStride,
        ));

        if (!quadVB) {
            return inputAssemblerData;
        }

        // create index buffer
        const ibStride = Uint8Array.BYTES_PER_ELEMENT;
        const ibSize = ibStride * 6;

        const quadIB: Buffer = device.createBuffer(new BufferInfo(
            BufferUsageBit.INDEX | BufferUsageBit.TRANSFER_DST,
            MemoryUsageBit.DEVICE,
            ibSize,
            ibStride,
        ));

        if (!quadIB) {
            return inputAssemblerData;
        }

        const indices = new Uint8Array(6);
        indices[0] = 0; indices[1] = 1; indices[2] = 2;
        indices[3] = 1; indices[4] = 3; indices[5] = 2;

        quadIB.update(indices);

        // create input assembler

        const attributes = new Array<Attribute>(2);
        attributes[0] = new Attribute('a_position', Format.RG32F);
        attributes[1] = new Attribute('a_texCoord', Format.RG32F);

        const quadIA = device.createInputAssembler(new InputAssemblerInfo(
            attributes,
            [quadVB],
            quadIB,
        ));

        inputAssemblerData.quadIB = quadIB;
        inputAssemblerData.quadVB = quadVB;
        inputAssemblerData.quadIA = quadIA;
        return inputAssemblerData;
    }
}

class ExecutorContext {
    constructor (
        pipeline: BasicPipeline,
        ubo: PipelineUBO,
        device: Device,
        resourceGraph: ResourceGraph,
        renderGraph: RenderGraph,
        layoutGraph: LayoutGraphData,
        width: number,
        height: number,
    ) {
        this.pipeline = pipeline;
        this.device = device;
        this.commandBuffer = device.commandBuffer;
        this.pipelineSceneData = pipeline.pipelineSceneData;
        this.resourceGraph = resourceGraph;
        this.renderGraph = renderGraph;
        this.root = legacyCC.director.root;
        this.ubo = ubo;
        this.layoutGraph = layoutGraph;
        this.width = width;
        this.height = height;
        this.additiveLight = new RenderAdditiveLightQueue(pipeline);
        this.shadowMapBatched = new RenderShadowMapBatchedQueue(pipeline);
        this.pools = new ExecutorPools(this);
        this.blit = new BlitInfo(this);
        this.culling = new SceneCulling();
    }
    reset (): void {
        this.culling.clear();
        this.pools.reset();
        this.cullCamera = null;
        for (const infoMap of this.submitMap) {
            for (const info of infoMap[1]) info[1].reset();
        }
    }
    resize (width: number, height: number): void {
        this.width = width;
        this.height = height;
        this.blit.resize(width, height);
    }
    readonly device: Device;
    readonly pipeline: BasicPipeline;
    readonly commandBuffer: CommandBuffer;
    readonly pipelineSceneData: PipelineSceneData;
    readonly resourceGraph: ResourceGraph;
    readonly devicePasses: Map<number, DeviceRenderPass> = new Map<number, DeviceRenderPass>();
    readonly deviceTextures: Map<string, DeviceTexture> = new Map<string, DeviceTexture>();
    readonly layoutGraph: LayoutGraphData;
    readonly root: Root;
    readonly ubo: PipelineUBO;
    readonly additiveLight: RenderAdditiveLightQueue;
    readonly shadowMapBatched: RenderShadowMapBatchedQueue;
    readonly submitMap: Map<Camera, Map<number, SubmitInfo>> = new Map<Camera, Map<number, SubmitInfo>>();
    readonly pools: ExecutorPools;
    readonly blit: BlitInfo;
    readonly culling: SceneCulling;
    renderGraph: RenderGraph;
    width: number;
    height: number;
    cullCamera;
}

export class Executor {
    constructor (
        pipeline: BasicPipeline,
        ubo: PipelineUBO,
        device: Device,
        resourceGraph: ResourceGraph,
        layoutGraph: LayoutGraphData,
        width: number,
        height: number,
    ) {
        context = this._context = new ExecutorContext(
            pipeline,
            ubo,
            device,
            resourceGraph,
            new RenderGraph(),
            layoutGraph,
            width,
            height,
        );
    }

    resize (width: number, height: number): void {
        context.resize(width, height);
    }

    private _removeDeviceResource (): void {
        const pipeline: any = context.pipeline;
        const resourceUses = pipeline.resourceUses;
        const deletes: string[] = [];
        const deviceTexs = context.deviceTextures;
        for (const [name, dTex] of deviceTexs) {
            const resId = context.resourceGraph.vertex(name);
            const trait = context.resourceGraph.getTraits(resId);
            if (!resourceUses.includes(name)) {
                switch (trait.residency) {
                case ResourceResidency.MANAGED:
                    deletes.push(name);
                    break;
                default:
                }
            }
        }
        for (const name of deletes) {
            deviceTexs.get(name)!.release();
            deviceTexs.delete(name);
        }
    }

    execute (rg: RenderGraph): void {
        context.renderGraph = rg;
        context.reset();
        const cmdBuff = context.commandBuffer;
        context.culling.buildRenderQueues(rg, context.layoutGraph, context.pipelineSceneData);
        context.culling.uploadInstancing(cmdBuff);
        this._removeDeviceResource();
        cmdBuff.begin();
        if (!this._visitor) this._visitor = new RenderVisitor();
        depthFirstSearch(this._visitor.graphView, this._visitor, this._visitor.colorMap);
        cmdBuff.end();
        context.device.queue.submit([cmdBuff]);
    }

    release (): void {
        context.devicePasses.clear();
        for (const [k, v] of context.deviceTextures) {
            v.release();
        }
        context.deviceTextures.clear();
    }
    readonly _context: ExecutorContext;
    private _visitor: RenderVisitor | undefined;
}

class BaseRenderVisitor {
    public queueID = 0xFFFFFFFF;
    public sceneID = 0xFFFFFFFF;
    public passID = 0xFFFFFFFF;
    public currPass: DeviceRenderPass | undefined;
    public currQueue: DeviceRenderQueue | undefined;
    public rg: RenderGraph;
    constructor () {
        this.rg = context.renderGraph;
    }
    protected _isRasterPass (u: number): boolean {
        return !!context.renderGraph.tryGetRasterPass(u);
    }
    protected _isQueue (u: number): boolean {
        return !!context.renderGraph.tryGetQueue(u);
    }
    protected _isScene (u: number): boolean {
        return !!context.renderGraph.tryGetScene(u);
    }
    protected _isBlit (u: number): boolean {
        return !!context.renderGraph.tryGetBlit(u);
    }
    applyID (id: number): void {
        if (this._isRasterPass(id)) { this.passID = id; } else if (this._isQueue(id)) { this.queueID = id; } else if (this._isScene(id) || this._isBlit(id)) { this.sceneID = id; }
    }
}

class PreRenderVisitor extends BaseRenderVisitor implements RenderGraphVisitor {
    constructor () {
        super();
    }
    clear (value: ClearView[]): void {
        // do nothing
    }
    viewport (value: Viewport): void {
        // do nothing
    }
    rasterPass (pass: RasterPass): void {
        if (!this.rg.getValid(this.passID)) return;
        const devicePasses = context.devicePasses;
        const passHash = pass.hashValue;
        this.currPass = devicePasses.get(passHash);
        if (!this.currPass) {
            const rasterInfo = context.pools.addRasterPassInfo();
            rasterInfo.applyInfo(this.passID, pass);
            this.currPass = new DeviceRenderPass(rasterInfo);
            devicePasses.set(passHash, this.currPass);
        } else {
            this.currPass.resetResource(this.passID, pass);
        }
    }
    rasterSubpass (value: RasterSubpass): void {}
    computeSubpass (value: ComputeSubpass): void {}
    compute (value: ComputePass): void {}
    resolve (value: ResolvePass): void {}
    copy (value: CopyPass): void {}
    move (value: MovePass): void {}
    raytrace (value: RaytracePass): void {}
    queue (value: RenderQueue): void {
        if (!this.rg.getValid(this.queueID)) return;
        const deviceQueue = context.pools.addDeviceQueue();
        deviceQueue.init(this.currPass!, value, this.queueID);
        this.currQueue = deviceQueue;
        this.currPass!.addQueue(deviceQueue);
        const layoutName = this.rg.getLayout(this.queueID);
        if (layoutName) {
            const layoutGraph = context.layoutGraph;
            if (this.currPass!.renderLayout) {
                const layoutId = layoutGraph.locateChild(this.currPass!.renderLayout.layoutID, layoutName);
                this.currQueue.layoutID = layoutId;
            }
        }
    }
    scene (value: SceneData): void {
        if (!this.rg.getValid(this.sceneID)) return;
        const graphScene = context.pools.addGraphScene();
        graphScene.init(value, null, this.sceneID);
        this.currQueue!.addSceneTask(graphScene);
    }
    blit (value: Blit): void {
        if (!this.rg.getValid(this.sceneID)) return;
        const graphScene = context.pools.addGraphScene();
        graphScene.init(null, value, -1);
        this.currQueue!.addSceneTask(graphScene);
    }
    dispatch (value: Dispatch): void {}
}

class PostRenderVisitor extends BaseRenderVisitor implements RenderGraphVisitor {
    constructor () {
        super();
    }
    clear (value: ClearView[]): void {
        // do nothing
    }
    viewport (value: Viewport): void {
        // do nothing
    }
    rasterPass (pass: RasterPass): void {
        const devicePasses = context.devicePasses;
        const passHash = pass.hashValue;
        const currPass = devicePasses.get(passHash);
        if (!currPass) return;
        this.currPass = currPass;
        this.currPass.prePass();
        this.currPass.record();
        this.currPass.postPass();
    }
    rasterSubpass (value: RasterSubpass): void {}
    computeSubpass (value: ComputeSubpass): void {}
    resolve (value: ResolvePass): void {}
    compute (value: ComputePass): void {}
    copy (value: CopyPass): void {}
    move (value: MovePass): void {}
    raytrace (value: RaytracePass): void {}
    queue (value: RenderQueue): void {
        // collect scene results
    }
    scene (value: SceneData): void {
        // scene command list finished
    }
    blit (value: Blit): void {}
    dispatch (value: Dispatch): void {}
}

export class RenderVisitor extends DefaultVisitor {
    private _preVisitor: PreRenderVisitor;
    private _postVisitor: PostRenderVisitor;
    private _graphView: ReferenceGraphView<RenderGraph>;
    private _colorMap: VectorGraphColorMap;
    constructor () {
        super();
        this._preVisitor = new PreRenderVisitor();
        this._postVisitor = new PostRenderVisitor();
        this._graphView = new ReferenceGraphView<RenderGraph>(context.renderGraph);
        this._colorMap = new VectorGraphColorMap(context.renderGraph.numVertices());
    }

    get graphView (): ReferenceGraphView<RenderGraph> { return this._graphView; }
    get colorMap (): VectorGraphColorMap { return this._colorMap; }
    discoverVertex (u: number, gv: ReferenceGraphView<RenderGraph>): void {
        const g = gv.g;
        this._preVisitor.applyID(u);
        g.visitVertex(this._preVisitor, u);
    }
    finishVertex (v: number, gv: ReferenceGraphView<RenderGraph>): void {
        const g = gv.g;
        g.visitVertex(this._postVisitor, v);
    }
}
