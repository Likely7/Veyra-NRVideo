"""Fetch/prepare only RedDuke's MIT network layer at a fixed commit, outside Git.
No NVIDIA SDK, DLL, model, translation cache or full game mod is fetched here.
"""
import argparse,hashlib,json,urllib.request
from pathlib import Path
PIN='ab455d179922adc11f65675d399e1d553bf423f0'
BLOBS={"runtime/core/network.cpp":"aa8628396dfe5d70d9f56e77072ad34596540552","runtime/core/network.h":"037a6463060c561e5baa6b66aaac26d65ccf81c1","runtime/common/cuda_min.h":"54ba01035ae482db84cd5a45aa0930025d355d67","runtime/common/ngx_cuda.h":"52593d310f2b7c3ed189e7a88c4b4aa197eeda9c","LICENSE":"ad7bd58931ae2ca601959041fdd8fb94390f04c0"}
FILES=['runtime/core/network.cpp','runtime/core/network.h','runtime/common/cuda_min.h','runtime/common/ngx_cuda.h','LICENSE']
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args()
a.out.resolve().relative_to(Path('E:/项目/Veyra').resolve())
rows=[]
for name in FILES:
    source=a.out/'source-pinned'/name;source.parent.mkdir(parents=True,exist_ok=True)
    if not source.exists():
        url=f'https://raw.githubusercontent.com/RedDukeDev/dlss5-nr-amd-custom/{PIN}/{name}'
        source.write_bytes(urllib.request.urlopen(url,timeout=30).read())
    raw=source.read_bytes();blob=hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()
    if blob!=BLOBS[name]:raise SystemExit(f'pinned Git blob differs: {name} {blob}')
    text=source.read_text('utf8').replace('\r\n','\n')
    if name.endswith('network.h'):
        text=text.replace('    std::wstring data_dir;','    uint64_t adapter_luid = 0; // Veyra: the host D3D12 adapter\n    std::wstring data_dir;',1)
        text=text.replace('    bool staged_by_list = false;','    bool staged_by_list = false;\n    bool synchronise = true; // Veyra: native GPU-fence path may enqueue without a CPU wait',1)
    elif name.endswith('network.cpp'):
        before='''            CUdevice device = 0;
            if (!cu_ok(g.cu.cuDeviceGet(&device, 0), "cuDeviceGet", error)) return false;'''
        after='''            CUdevice device = 0;
            int count = 0;
            if (!cu_ok(g.cu.cuDeviceGetCount(&count), "cuDeviceGetCount", error) || count < 1) return false;
            using GetLuid = CUresult (*)(char*, unsigned*, CUdevice);
            auto get_luid = reinterpret_cast<GetLuid>(GetProcAddress(g.gate, "cuDeviceGetLuid"));
            bool matched = false;
            for (int index=0; get_luid && index<count; ++index) {
                CUdevice candidate=0; unsigned node=0; uint64_t luid=0;
                if (g.cu.cuDeviceGet(&candidate,index)==CUDA_SUCCESS &&
                    get_luid(reinterpret_cast<char*>(&luid),&node,candidate)==CUDA_SUCCESS &&
                    luid==paths.adapter_luid) { device=candidate; matched=true; break; }
            }
            // A single ROCm GPU is also admitted, but only after the worker's
            // real D3D12 shared-image import has succeeded on the selected LUID.
            if (!matched && count==1) {
                if (!cu_ok(g.cu.cuDeviceGet(&device,0), "cuDeviceGet", error)) return false;
            } else if (!matched) {
                error="cannot match the selected D3D12 LUID among multiple CUDA/HIP devices"; return false;
            }'''
        assert text.count(before)==1,'pinned adapter-selection source differs'
        text=text.replace(before,after)
        before='if (!cu_ok(g.cu.cuCtxSynchronize(), "cuCtxSynchronize", error)) return false;'
        pos=text.index('bool Network::evaluate(const EvaluateDesc &d, std::string &error)')
        tail=text[pos:];assert before in tail
        text=text[:pos]+tail.replace(before,'if (d.synchronise && !cu_ok(g.cu.cuCtxSynchronize(), "cuCtxSynchronize", error)) return false;',1)
    dest=a.out/'prepared'/name;dest.parent.mkdir(parents=True,exist_ok=True)
    dest.write_text(text,encoding='utf8',newline='\n')
    rows.append(dict(path=name,originalSha256=hashlib.sha256(source.read_bytes()).hexdigest(),preparedSha256=hashlib.sha256(dest.read_bytes()).hexdigest()))
(a.out/'source-manifest.json').write_text(json.dumps(dict(repository='RedDukeDev/dlss5-nr-amd-custom',commit=PIN,license='MIT',changes=['select host adapter LUID or reject ambiguous multi-device mapping','optional asynchronous enqueue paired with mandatory native GPU semaphore by Veyra worker'],files=rows),indent=2),encoding='utf8')
print(json.dumps(dict(prepared=str(a.out/'prepared'),files=len(rows),commit=PIN)))
