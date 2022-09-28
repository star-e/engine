import { Camera, CameraUsage } from '../../render-scene/scene';
import { buildBloomPass as buildBloomPasses, buildForwardPass, buildPostprocessPass, validPunctualLightsCulling } from './define';
import { Pipeline, PipelineBuilder } from './pipeline';

export class CustomPipelineBuilder implements PipelineBuilder {
    public setup (cameras: Camera[], ppl: Pipeline): void {
        for (let i = 0; i < cameras.length; i++) {
            const camera = cameras[i];
            if (camera.scene === null) {
                continue;
            }
            // forward pass
            const forwardInfo = buildForwardPass(camera, ppl, camera.cameraUsage !== CameraUsage.EDITOR);
            if (camera.cameraUsage === CameraUsage.EDITOR) {
                return;
            }
            // bloom passes
            const bloomInfo = buildBloomPasses(camera, ppl, forwardInfo.rtName);
            // Present Pass
            buildPostprocessPass(camera, ppl, bloomInfo.rtName);
        }
    }
}
