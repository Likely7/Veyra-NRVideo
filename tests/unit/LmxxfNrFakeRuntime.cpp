// SPDX-License-Identifier: GPL-3.0-only
// A test-only identity-copy C ABI provider. No HIP, weights or NR inference.
#include "../../third_party/lmxxf/LmxxfNrApi.h"
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <string>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace {
struct Session { ID3D12Resource* color=nullptr; ComPtr<ID3D12Resource> output; ComPtr<ID3D12Device> device; ID3D12CommandQueue* queue=nullptr; unsigned state=0; };
std::string mode(){char b[64]{};GetEnvironmentVariableA("VEYRA_TEST_LMXXF_MODE",b,sizeof(b));return b;}
int caps(LmxxfNrCapabilities* p){p->abi_version=1;p->max_input_width=1920;p->max_input_height=1080;return 0;}
int create(const LmxxfNrCreateInfo* p,void** out){if(p->flags||!p->device||!p->queue)return 2;auto* s=new Session;s->device=static_cast<ID3D12Device*>(p->device);s->queue=static_cast<ID3D12CommandQueue*>(p->queue);*out=s;return 0;}
int destroy(void* p){delete static_cast<Session*>(p);return 0;}
int ok(void*){return 0;}
int prepare(void* p,const LmxxfNrFrameInfo* info,LmxxfNrJob* job){
    auto* s=static_cast<Session*>(p);if(s->state||info->color_state!=D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)return 2;
    if(mode()=="oldabi"&&info->struct_size!=LMXXF_NR_FRAME_INFO_V1_SIZE)return 2;
    s->color=static_cast<ID3D12Resource*>(info->color);
    if(!s->output){auto desc=s->color->GetDesc();desc.Flags=D3D12_RESOURCE_FLAG_NONE;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;
        if(FAILED(s->device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&s->output))))return 5;}
    job->handle=s;job->private_output=s->output.Get();s->state=1;return 0;
}
int inputs(void* p,void* job,void* list){auto* s=static_cast<Session*>(p);if(job!=s||!list||s->state!=1)return 2;s->state=2;return 0;}
int enqueue(void* p,void* job,void* queue){auto* s=static_cast<Session*>(p);if(job!=s||queue!=s->queue||s->state!=2)return 2;if(mode()=="fail-enqueue")return 5;s->state=3;return 0;}
int outputs(void* p,void* job,void* cmd){
    auto* s=static_cast<Session*>(p);if(job!=s||!cmd||s->state!=3)return 2;
    auto* list=static_cast<ID3D12GraphicsCommandList*>(cmd);D3D12_RESOURCE_BARRIER b[2]{};
    for(auto& v:b)v.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b[0].Transition={s->color,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE};
    b[1].Transition={s->output.Get(),D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST};
    list->ResourceBarrier(2,b);list->CopyResource(s->output.Get(),s->color);
    for(auto& v:b)std::swap(v.Transition.StateBefore,v.Transition.StateAfter);list->ResourceBarrier(2,b);s->state=4;return 0;
}
int retire(void* p,void* job){auto* s=static_cast<Session*>(p);if(job!=s||s->state!=4)return 2;s->state=0;return 0;}
int cancel(void* p,void*){static_cast<Session*>(p)->state=0;return 0;}
int poll(void* p,void*,uint32_t* state){*state=static_cast<Session*>(p)->state;return 0;}
int error(char* b,uint32_t n){if(n)strncpy_s(b,n,"test provider: requested failure / incorrect call order",_TRUNCATE);return 0;}
int status(void*,char* b,uint32_t n){return error(b,n);}
}
extern "C" __declspec(dllexport) int32_t LmxxfNrGetApi(uint32_t abi,LmxxfNrApi* p){
    if(abi!=1)return 1;if(mode()=="oldabi"&&p->struct_size>LMXXF_NR_API_V1_SIZE)return 2;
    p->abi_version=1;p->QueryCapabilities=caps;p->Create=create;p->Destroy=destroy;p->PrepareSession=ok;p->PrepareFrame=prepare;
    p->RecordInputs=inputs;p->EnqueueHip=enqueue;p->RecordOutputs=outputs;p->ExecuteAfterProducer=enqueue;p->CancelUnsubmitted=cancel;
    p->Poll=poll;p->Retire=retire;p->ResetHistory=ok;p->Drain=ok;p->GetStatus=status;p->GetLastError=error;
    if(mode()=="bad-table")p->EnqueueHip=nullptr;
    return 0;
}
