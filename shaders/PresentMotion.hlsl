// Same contain/zoom/pan mapping as PresentBlit, with current->previous motion.
Texture2D<float2> sourceMotion : register(t0);
RWTexture2D<float2> outputMotion : register(u0);
cbuffer Mapping : register(b0) {
    float4 extent; // source extent, current client extent
    float4 currentView; // current rendered width, center x/y, reset
    float4 previousView; // current rendered height, previous center x/y, unused
    float4 previousClient; // previous client width/height, rendered width/height
};
[numthreads(8,8,1)]
void main(uint3 tid : SV_DispatchThreadID) {
    uint2 size; outputMotion.GetDimensions(size.x,size.y);
    if(any(tid.xy>=size))return;
    float2 displayUv=(float2(tid.xy)+.5)/float2(size);
    float2 sourceUv=(displayUv-.5)*extent.zw/float2(currentView.x,previousView.x)+currentView.yz;
    if(currentView.w!=0||any(sourceUv<0)||any(sourceUv>=1)) {
        outputMotion[tid.xy]=0;return;
    }
    uint2 motionSize;sourceMotion.GetDimensions(motionSize.x,motionSize.y);
    int2 samplePixel=min(int2(sourceUv*motionSize),int2(motionSize)-1);
    float2 previousUv=sourceUv+sourceMotion.Load(int3(samplePixel,0))/extent.xy;
    float2 previousDisplay=.5+(previousUv-previousView.yz)*previousClient.zw/previousClient.xy;
    outputMotion[tid.xy]=(any(previousUv<0)||any(previousUv>=1)||any(previousDisplay<0)||any(previousDisplay>=1))?
        float2(0,0):(previousDisplay-displayUv)*float2(size);
}
