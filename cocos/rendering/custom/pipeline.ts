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
import { Material } from '../../asset/assets';
import { Camera } from '../../render-scene/scene/camera';
import { GeometryRenderer } from '../geometry-renderer';
import { Buffer, BufferInfo, ClearFlagBit, Color, CommandBuffer, DescriptorSet, DescriptorSetLayout, Device, DrawInfo, Format, InputAssembler, LoadOp, PipelineState, Rect, ResolveMode, Sampler, ShaderStageFlagBit, StoreOp, Swapchain, Texture, TextureInfo, Viewport } from '../../gfx';
import { GlobalDSManager } from '../global-descriptor-set-manager';
import { Mat4, Quat, Vec2, Vec4 } from '../../core/math';
import { MacroRecord } from '../../render-scene/core/pass-utils';
import { PipelineSceneData } from '../pipeline-scene-data';
import { AccessType, CopyPair, LightInfo, MovePair, QueueHint, ResolvePair, ResourceResidency, SceneFlags, TaskType, UpdateFrequency, UploadPair } from './types';
import { RenderWindow } from '../../render-scene/core/render-window';
import { Model } from '../../render-scene/scene';

/**
 * @internal
 * @en PipelineRuntime is the runtime of both classical and custom pipelines.
 * It is used internally and should not be called directly.
 * @zh PipelineRuntime是经典管线以及自定义管线的运行时。
 * 属于内部实现，用户不应直接调用。
 */
export interface PipelineRuntime {
    /**
     * @en Activate PipelineRuntime with default swapchain
     * @zh 用默认交换链初始化PipelineRuntime
     */
    activate (swapchain: Swapchain): boolean;
    /**
     * @en Destroy resources of PipelineRuntime
     * @zh 销毁PipelineRuntime所持资源
     */
    destroy (): boolean;
    /**
     * @en Render contents of cameras
     * @zh 根据相机进行绘制
     */
    render (cameras: Camera[]): void;
    /**
     * @en Get graphics device
     * @zh 获得图形设备
     */
    readonly device: Device;
    /**
     * @en Get user macros
     * @zh 获得用户宏列表
     */
    readonly macros: MacroRecord;
    /**
     * @en Get global descriptor set manager
     * @zh 获得全局(Global)级别描述符集(DescriptorSet)管理器
     */
    readonly globalDSManager: GlobalDSManager;
    /**
     * @en Get global descriptor set layout
     * @zh 获得全局(Global)级别描述符集的布局(DescriptorSet Layout)
     */
    readonly descriptorSetLayout: DescriptorSetLayout;
    /**
     * @en Get global descriptor set
     * @zh 获得全局(Global)级别描述符集(DescriptorSet)
     */
    readonly descriptorSet: DescriptorSet;
    /**
     * @en Get command buffers of render pipeline
     * @zh 获得渲染管线的命令缓冲(CommandBuffer)列表
     */
    readonly commandBuffers: CommandBuffer[];
    /**
     * @en Get scene data of render pipeline.
     * Scene data contains render configurations of the current scene.
     * @zh 获得渲染管线相关的场景数据，此场景数据一般包含渲染所需配置信息
     */
    readonly pipelineSceneData: PipelineSceneData;
    /**
     * @en Get constant macros.
     * Constant macro is platform-dependent and immutable.
     * @zh 获得常量宏列表，常量宏平台相关且无法修改
     */
    readonly constantMacros: string;
    /**
     * @en Get profiler model.
     * This model is used to render profile information in Debug mode.
     * @zh 获得分析工具(Profiler)的渲染实例，用于Debug模式下显示调试与性能检测信息
     */
    profiler: Model | null;
    /**
     * @en Get geometry renderer.
     * Geometry renderer is used to render procedural geometries.
     * @zh 获得几何渲染器(GeometryRenderer)，几何渲染器用于程序化渲染基础几何图形
     */
    readonly geometryRenderer: GeometryRenderer | null;
    /**
     * @en Get shading scale.
     * Shading scale affects shading texels per pixel.
     * @zh 获得渲染倍率(ShadingScale)，每像素(pixel)绘制的纹素(texel)会根据渲染倍率进行调整。
     */
    shadingScale: number;
    /**
     * @en Get macro as string.
     * @zh 根据宏名获得字符串
     * @param name @en Name of macro @zh 宏的名字
     */
    getMacroString (name: string): string;
    /**
     * @en Get macro as integer.
     * @zh 根据宏名获得整型
     * @param name @en Name of macro @zh 宏的名字
     */
    getMacroInt (name: string): number;
    /**
     * @en Get macro as boolean.
     * @zh 根据宏名获得布尔值
     * @param name @en Name of macro @zh 宏的名字
     */
    getMacroBool (name: string): boolean;
    /**
     * @en Assign string value to macro.
     * @zh 给宏赋值字符串
     * @param name @en Name of macro @zh 宏的名字
     */
    setMacroString (name: string, value: string): void;
    /**
     * @en Assign integer value to macro.
     * @zh 给宏赋值整型
     * @param name @en Name of macro @zh 宏的名字
     */
    setMacroInt (name: string, value: number): void;
    /**
     * @en Assign boolean value to macro.
     * @zh 给宏赋值布尔值
     * @param name @en Name of macro @zh 宏的名字
     */
    setMacroBool (name: string, value: boolean): void;
    /**
     * @en trigger pipeline state change event
     * @zh 触发管线状态更新事件
     */
    onGlobalPipelineStateChanged (): void;
}

export enum PipelineType {
    BASIC,
    STANDARD,
}

export function getPipelineTypeName (e: PipelineType): string {
    switch (e) {
    case PipelineType.BASIC:
        return 'BASIC';
    case PipelineType.STANDARD:
        return 'STANDARD';
    default:
        return '';
    }
}

export enum SubpassCapabilities {
    NONE = 0,
    INPUT_DEPTH_STENCIL = 1 << 0,
    INPUT_COLOR = 1 << 1,
    INPUT_COLOR_MRT = 1 << 2,
}

export class PipelineCapabilities {
    subpass: SubpassCapabilities = SubpassCapabilities.NONE;
}

export interface RenderNode {
    name: string;
    /**
     * @beta function signature might change
     */
    setCustomBehavior (name: string): void;
}

export interface Setter extends RenderNode {
    setMat4 (name: string, mat: Mat4): void;
    setQuaternion (name: string, quat: Quat): void;
    setColor (name: string, color: Color): void;
    setVec4 (name: string, vec: Vec4): void;
    setVec2 (name: string, vec: Vec2): void;
    setFloat (name: string, v: number): void;
    setArrayBuffer (name: string, arrayBuffer: ArrayBuffer): void;
    setBuffer (name: string, buffer: Buffer): void;
    setTexture (name: string, texture: Texture): void;
    setReadWriteBuffer (name: string, buffer: Buffer): void;
    setReadWriteTexture (name: string, texture: Texture): void;
    setSampler (name: string, sampler: Sampler): void;
}

export interface RenderQueueBuilder extends Setter {
    /**
     * @deprecated method will be removed in 3.9.0
     */
    addSceneOfCamera (
        camera: Camera,
        light: LightInfo,
        sceneFlags?: SceneFlags): void;
    addFullscreenQuad (
        material: Material,
        passID: number,
        sceneFlags?: SceneFlags): void;
    addCameraQuad (
        camera: Camera,
        material: Material,
        passID: number,
        sceneFlags?: SceneFlags): void;
    clearRenderTarget (name: string, color?: Color): void;
    setViewport (viewport: Viewport): void;
    /**
     * @beta function signature might change
     */
    addCustomCommand (customBehavior: string): void;
}

export interface BasicRenderPassBuilder extends Setter {
    addRenderTarget (
        name: string,
        loadOp?: LoadOp,
        storeOp?: StoreOp,
        color?: Color): void;
    addDepthStencil (
        name: string,
        loadOp?: LoadOp,
        storeOp?: StoreOp,
        depth?: number,
        stencil?: number,
        clearFlags?: ClearFlagBit): void;
    addTexture (
        name: string,
        slotName: string,
        sampler?: Sampler | null,
        plane?: number): void;
    addQueue (hint?: QueueHint, phaseName?: string): RenderQueueBuilder;
    setViewport (viewport: Viewport): void;
    setVersion (name: string, version: number): void;
    showStatistics: boolean;
}

export interface BasicPipeline extends PipelineRuntime {
    readonly type: PipelineType;
    readonly capabilities: PipelineCapabilities;
    beginSetup (): void;
    endSetup (): void;
    containsResource (name: string): boolean;
    addRenderWindow (
        name: string,
        format: Format,
        width: number,
        height: number,
        renderWindow: RenderWindow): number;
    updateRenderWindow (name: string, renderWindow: RenderWindow): void;
    addRenderTarget (
        name: string,
        format: Format,
        width: number,
        height: number,
        residency?: ResourceResidency): number;
    addDepthStencil (
        name: string,
        format: Format,
        width: number,
        height: number,
        residency?: ResourceResidency): number;
    updateRenderTarget (
        name: string,
        width: number,
        height: number,
        format?: Format): void;
    updateDepthStencil (
        name: string,
        width: number,
        height: number,
        format?: Format): void;
    beginFrame (): void;
    update (camera: Camera): void;
    endFrame (): void;
    addRenderPass (
        width: number,
        height: number,
        passName?: string): BasicRenderPassBuilder;
    addMultisampleRenderPass (
        width: number,
        height: number,
        count: number,
        quality: number,
        passName?: string): BasicRenderPassBuilder;
    addResolvePass (resolvePairs: ResolvePair[]): void;
    addCopyPass (copyPairs: CopyPair[]): void;
    getDescriptorSetLayout (shaderName: string, freq: UpdateFrequency): DescriptorSetLayout | null;
}

export interface RenderSubpassBuilder extends Setter {
    addRenderTarget (
        name: string,
        accessType: AccessType,
        slotName?: string,
        loadOp?: LoadOp,
        storeOp?: StoreOp,
        color?: Color): void;
    addDepthStencil (
        name: string,
        accessType: AccessType,
        depthSlotName?: string,
        stencilSlotName?: string,
        loadOp?: LoadOp,
        storeOp?: StoreOp,
        depth?: number,
        stencil?: number,
        clearFlags?: ClearFlagBit): void;
    addTexture (
        name: string,
        slotName: string,
        sampler?: Sampler | null,
        plane?: number): void;
    addStorageBuffer (
        name: string,
        accessType: AccessType,
        slotName: string): void;
    addStorageImage (
        name: string,
        accessType: AccessType,
        slotName: string): void;
    setViewport (viewport: Viewport): void;
    addQueue (hint?: QueueHint, phaseName?: string): RenderQueueBuilder;
    showStatistics: boolean;
    /**
     * @beta function signature might change
     */
    setCustomShaderStages (name: string, stageFlags: ShaderStageFlagBit): void;
}

export interface MultisampleRenderSubpassBuilder extends RenderSubpassBuilder {
    resolveRenderTarget (source: string, target: string): void;
    resolveDepthStencil (
        source: string,
        target: string,
        depthMode?: ResolveMode,
        stencilMode?: ResolveMode): void;
}

export interface ComputeQueueBuilder extends Setter {
    addDispatch (
        threadGroupCountX: number,
        threadGroupCountY: number,
        threadGroupCountZ: number,
        material?: Material,
        passID?: number): void;
}

export interface ComputeSubpassBuilder extends Setter {
    addRenderTarget (name: string, slotName: string): void;
    addTexture (
        name: string,
        slotName: string,
        sampler?: Sampler | null,
        plane?: number): void;
    addStorageBuffer (
        name: string,
        accessType: AccessType,
        slotName: string): void;
    addStorageImage (
        name: string,
        accessType: AccessType,
        slotName: string): void;
    addQueue (phaseName?: string): ComputeQueueBuilder;
    /**
     * @beta function signature might change
     */
    setCustomShaderStages (name: string, stageFlags: ShaderStageFlagBit): void;
}

export interface RenderPassBuilder extends BasicRenderPassBuilder {
    addStorageBuffer (
        name: string,
        accessType: AccessType,
        slotName: string): void;
    addStorageImage (
        name: string,
        accessType: AccessType,
        slotName: string): void;
    /**
     * @beta function signature might change
     */
    addMaterialTexture (resourceName: string, flags?: ShaderStageFlagBit): void;
    addRenderSubpass (subpassName: string): RenderSubpassBuilder;
    addMultisampleRenderSubpass (
        count: number,
        quality: number,
        subpassName: string): MultisampleRenderSubpassBuilder;
    addComputeSubpass (subpassName?: string): ComputeSubpassBuilder;
    /**
     * @beta function signature might change
     */
    setCustomShaderStages (name: string, stageFlags: ShaderStageFlagBit): void;
}

export interface ComputePassBuilder extends Setter {
    addTexture (
        name: string,
        slotName: string,
        sampler?: Sampler | null,
        plane?: number): void;
    addStorageBuffer (
        name: string,
        accessType: AccessType,
        slotName: string): void;
    addStorageImage (
        name: string,
        accessType: AccessType,
        slotName: string): void;
    addMaterialTexture (resourceName: string, flags?: ShaderStageFlagBit): void;
    addQueue (phaseName?: string): ComputeQueueBuilder;
    /**
     * @beta function signature might change
     */
    setCustomShaderStages (name: string, stageFlags: ShaderStageFlagBit): void;
}

export interface SceneVisitor {
    readonly pipelineSceneData: PipelineSceneData;
    setViewport (vp: Viewport): void;
    setScissor (rect: Rect): void;
    bindPipelineState (pso: PipelineState): void;
    bindInputAssembler (ia: InputAssembler): void;
    draw (info: DrawInfo): void;

    bindDescriptorSet (set: number, descriptorSet: DescriptorSet, dynamicOffsets?: number[]): void;
    updateBuffer (buffer: Buffer, data: ArrayBuffer, size?: number): void;
}

export interface SceneTask {
    readonly taskType: TaskType;
    start (): void;
    join (): void;
    submit (): void;
}

export interface SceneTransversal {
    transverse (visitor: SceneVisitor): SceneTask;
}

export interface Pipeline extends BasicPipeline {
    addStorageBuffer (
        name: string,
        format: Format,
        size: number,
        residency?: ResourceResidency): number;
    addStorageTexture (
        name: string,
        format: Format,
        width: number,
        height: number,
        residency?: ResourceResidency): number;
    addShadingRateTexture (
        name: string,
        width: number,
        height: number,
        residency?: ResourceResidency): number;
    updateStorageBuffer (
        name: string,
        size: number,
        format?: Format): void;
    updateStorageTexture (
        name: string,
        width: number,
        height: number,
        format?: Format): void;
    updateShadingRateTexture (
        name: string,
        width: number,
        height: number): void;
    addRenderPass (
        width: number,
        height: number,
        passName: string): RenderPassBuilder;
    addComputePass (passName: string): ComputePassBuilder;
    addUploadPass (uploadPairs: UploadPair[]): void;
    addMovePass (movePairs: MovePair[]): void;
    /**
     * @beta function signature might change
     */
    addCustomBuffer (
        name: string,
        info: BufferInfo,
        type: string): number;
    /**
     * @beta function signature might change
     */
    addCustomTexture (
        name: string,
        info: TextureInfo,
        type: string): number;
}

export interface PipelineBuilder {
    setup (cameras: Camera[], pipeline: BasicPipeline): void;
}

export interface RenderingModule {
    getPassID (name: string): number;
    getSubpassID (passID: number, name: string): number;
    getPhaseID (subpassOrPassID: number, name: string): number;
}
