// Adapted from OBS Studio 7e3aa99811798255bd1a13a6da7260ada76a4d95,
// shared/obs-shared-memory-queue/shared-memory-queue.c; GPL-2.0-or-later.
// Veyra: own namespace, padded rows, bounded locking, multiple consumers,
// validation and crash heartbeat. Protocol 2 (2026-11-17): a six-frame ring in
// write order with sequence numbers, camera-grid timestamps and write times, and
// readers announcing their consumer's frame interval. Original fingerprint:
// ../UPSTREAM.json.
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stddef.h>
#include "shared-memory-queue.h"
#include "tiny-nv12-scale.h"

#define MAGIC 0x56595241u
#define VERSION 2u
#define SLOTS 6u
#define CONSUMERS 8u
#define FRAME_HEADER_SIZE 32u
#define CONSUMER_TIMEOUT_MS 3000u
struct frame_header {            /* FRAME_HEADER_SIZE bytes before each frame's pixels */
    uint64_t timestamp;          /* place on the camera grid (video_queue_now_100ns) */
    uint64_t sequence;           /* 1, 2, 3 ... in write order; 0 = never written */
    uint64_t written;            /* when the producer finished writing it */
    uint64_t reserved;
};
struct consumer_slot {           /* a live reader: the token it chose, a heartbeat, its interval */
    volatile LONG token;
    volatile LONG tick;
    volatile LONG64 interval;
};
struct queue_header {
    volatile LONG write_idx,read_idx,state;
    uint32_t offsets[SLOTS];
    uint32_t type,cx,cy;
    uint64_t interval;
    uint32_t magic,version,owner;
    volatile DWORD alive_tick;
    volatile LONG dirty;
    uint32_t reserved;
    volatile LONG64 sequence;    /* the newest written frame */
    struct consumer_slot consumers[CONSUMERS];
};
struct video_queue {
    HANDLE handle,mutex,owner;
    struct queue_header* header;
    struct frame_header* info[SLOTS];
    uint8_t* frame[SLOTS];
    uint8_t* scratch;
    size_t frame_bytes;
    bool is_writer,writable;
    LONG token;                  /* reader: the token of our consumer slot */
    int consumer;                /* reader: our consumer slot, -1 none */
};
/* The same layout in the 64-bit producer and the 32/64-bit camera modules. */
typedef char queue_header_abi[(sizeof(struct queue_header)==216&&offsetof(struct queue_header,interval)==48&&
    offsetof(struct queue_header,sequence)==80&&offsetof(struct queue_header,consumers)==88)?1:-1];
typedef char frame_header_abi[(sizeof(struct frame_header)==FRAME_HEADER_SIZE)?1:-1];
uint64_t video_queue_now_100ns(void){
    static volatile LONG64 frequency=0;
    LARGE_INTEGER now;LONG64 f=frequency;
    if(!f){LARGE_INTEGER q;QueryPerformanceFrequency(&q);f=q.QuadPart;InterlockedExchange64(&frequency,f);}
    QueryPerformanceCounter(&now);
    const uint64_t c=(uint64_t)now.QuadPart,u=(uint64_t)f;
    return c/u*10000000ull+c%u*10000000ull/u;
}
static void object_name(wchar_t* out,const wchar_t* kind){
    wchar_t suffix[65]={0};
    const DWORD n=GetEnvironmentVariableW(L"VEYRA_TEST_VIRTUALCAM_NAMESPACE",suffix,65);
    if(n>=65)suffix[0]=0;
    for(size_t i=0;suffix[i];++i)
        if(!((suffix[i]>=L'a'&&suffix[i]<=L'z')||(suffix[i]>=L'A'&&suffix[i]<=L'Z')||(suffix[i]>=L'0'&&suffix[i]<=L'9')||suffix[i]==L'-')){suffix[0]=0;break;}
    /* V2: a protocol-1 camera module finds no queue (its placeholder) instead of misreading this one. */
    _snwprintf_s(out,192,_TRUNCATE,L"Local\\VeyraVirtualCamera%sV2%s%s",kind,suffix[0]?L"-":L"",suffix);
}
static bool valid_extent(uint32_t cx,uint32_t cy){
    return cx>=2&&cy>=2&&cx<=8192&&cy<=8192&&!(cx&1)&&!(cy&1)&&(uint64_t)cx*cy<=33554432u;
}
static uint32_t align32(uint32_t size){return (size+31u)&~31u;}
static void initialize_frames(video_queue_t* vq){
    for(size_t i=0;i<SLOTS;++i){
        uint8_t* frame=(uint8_t*)vq->header+vq->header->offsets[i];
        vq->info[i]=(struct frame_header*)frame;vq->frame[i]=frame+FRAME_HEADER_SIZE;
    }
}
static uint64_t newest(const video_queue_t* vq){
    return (uint64_t)InterlockedCompareExchange64((volatile LONG64*)&vq->header->sequence,0,0);
}
void video_queue_close(video_queue_t* vq){
    if(!vq)return;
    if(vq->header&&vq->is_writer)InterlockedExchange(&vq->header->state,SHARED_QUEUE_STATE_STOPPING);
    if(vq->header&&vq->writable&&!vq->is_writer&&vq->consumer>=0)
        InterlockedCompareExchange(&vq->header->consumers[vq->consumer].token,0,vq->token);
    if(vq->header)UnmapViewOfFile(vq->header);
    if(vq->handle)CloseHandle(vq->handle);
    if(vq->mutex)CloseHandle(vq->mutex);
    if(vq->owner)CloseHandle(vq->owner);
    free(vq->scratch);free(vq);
}
video_queue_t* video_queue_create(uint32_t cx,uint32_t cy,uint64_t interval){
    if(!valid_extent(cx,cy)||interval<10000||interval>10000000)return NULL;
    video_queue_t* vq=calloc(1,sizeof(*vq));if(!vq)return NULL;
    vq->consumer=-1;
    const uint32_t frame_bytes=cx*cy*3u/2u;
    uint32_t size=align32(sizeof(struct queue_header));
    struct queue_header header={0};
    for(size_t i=0;i<SLOTS;++i){header.offsets[i]=size;size=align32(size+frame_bytes+FRAME_HEADER_SIZE);}
    header.cx=cx;header.cy=cy;header.interval=interval;header.magic=MAGIC;header.version=VERSION;
    header.owner=GetCurrentProcessId();header.alive_tick=GetTickCount();header.state=SHARED_QUEUE_STATE_STARTING;
    wchar_t name[192];object_name(name,L"FrameMutex");
    vq->mutex=CreateMutexW(NULL,FALSE,name);if(!vq->mutex)goto fail;
    object_name(name,L"Video");SetLastError(0);
    vq->handle=CreateFileMappingW(INVALID_HANDLE_VALUE,NULL,PAGE_READWRITE,0,size,name);
    if(!vq->handle||GetLastError()==ERROR_ALREADY_EXISTS)goto fail;
    vq->header=MapViewOfFile(vq->handle,FILE_MAP_ALL_ACCESS,0,0,0);if(!vq->header)goto fail;
    memcpy(vq->header,&header,sizeof(header));vq->is_writer=vq->writable=true;vq->frame_bytes=frame_bytes;
    initialize_frames(vq);
    for(size_t i=0;i<SLOTS;++i)memset(vq->info[i],0,FRAME_HEADER_SIZE);
    return vq;
fail:
    video_queue_close(vq);return NULL;
}
video_queue_t* video_queue_open(void){
    video_queue_t* vq=calloc(1,sizeof(*vq));if(!vq)return NULL;
    vq->consumer=-1;
    wchar_t name[192];object_name(name,L"Video");
    /* Write access only for the consumer slots; read-only still works, without announcing. */
    vq->handle=OpenFileMappingW(FILE_MAP_READ|FILE_MAP_WRITE,FALSE,name);
    if(vq->handle){vq->header=MapViewOfFile(vq->handle,FILE_MAP_READ|FILE_MAP_WRITE,0,0,0);vq->writable=vq->header!=NULL;}
    if(!vq->header){
        if(vq->handle){CloseHandle(vq->handle);vq->handle=NULL;}
        vq->handle=OpenFileMappingW(FILE_MAP_READ,FALSE,name);if(!vq->handle)goto fail;
        vq->header=MapViewOfFile(vq->handle,FILE_MAP_READ,0,0,0);if(!vq->header)goto fail;
    }
    const struct queue_header* h=vq->header;
    if(h->magic!=MAGIC||h->version!=VERSION||!valid_extent(h->cx,h->cy)||h->interval<10000||h->interval>10000000)goto fail;
    MEMORY_BASIC_INFORMATION info={0};if(!VirtualQuery(h,&info,sizeof(info)))goto fail;
    vq->frame_bytes=(size_t)h->cx*h->cy*3u/2u;
    for(size_t i=0;i<SLOTS;++i)
        if(h->offsets[i]<sizeof(*h)||(uint64_t)h->offsets[i]+FRAME_HEADER_SIZE+vq->frame_bytes>info.RegionSize)goto fail;
    object_name(name,L"FrameMutex");vq->mutex=OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,name);if(!vq->mutex)goto fail;
    vq->owner=OpenProcess(SYNCHRONIZE,FALSE,h->owner);
    vq->token=(LONG)(((uint32_t)GetCurrentProcessId()*2654435761u)^(uint32_t)(uintptr_t)vq^GetTickCount())|1;
    initialize_frames(vq);return vq;
fail:
    video_queue_close(vq);return NULL;
}
void video_queue_get_info(video_queue_t* vq,uint32_t* cx,uint32_t* cy,uint64_t* interval){
    *cx=vq->header->cx;*cy=vq->header->cy;*interval=vq->header->interval;
}
void video_queue_heartbeat(video_queue_t* vq){
    if(vq&&vq->is_writer)InterlockedExchange((volatile LONG*)&vq->header->alive_tick,(LONG)GetTickCount());
}
bool video_queue_write(video_queue_t* vq,uint8_t** data,uint32_t* pitch,uint64_t timestamp){
    if(!vq||!vq->is_writer||!data||!data[0]||!data[1]||!pitch||pitch[0]<vq->header->cx||pitch[1]<vq->header->cx)return false;
    const DWORD acquired=WaitForSingleObject(vq->mutex,2);
    if(acquired!=WAIT_OBJECT_0&&acquired!=WAIT_ABANDONED)return false;
    struct queue_header* h=vq->header;
    InterlockedExchange(&h->dirty,1);
    const uint64_t sequence=newest(vq)+1u;const unsigned index=(unsigned)(sequence%SLOTS);
    struct frame_header* info=vq->info[index];
    info->sequence=0; /* invalid while the pixels change */
    uint8_t* dst=vq->frame[index];
    for(uint32_t row=0;row<h->cy;++row)memcpy(dst+(size_t)row*h->cx,data[0]+(size_t)row*pitch[0],h->cx);
    dst+=(size_t)h->cx*h->cy;
    for(uint32_t row=0;row<h->cy/2;++row)memcpy(dst+(size_t)row*h->cx,data[1]+(size_t)row*pitch[1],h->cx);
    info->timestamp=timestamp;info->written=video_queue_now_100ns();info->sequence=sequence;
    InterlockedExchange64(&h->sequence,(LONG64)sequence);
    h->write_idx=(LONG)sequence;InterlockedExchange(&h->read_idx,(LONG)sequence);InterlockedExchange(&h->dirty,0);
    video_queue_heartbeat(vq);InterlockedExchange(&h->state,SHARED_QUEUE_STATE_READY);
    ReleaseMutex(vq->mutex);return true;
}
enum queue_state video_queue_state(video_queue_t* vq){
    if(!vq)return SHARED_QUEUE_STATE_INVALID;
    if((vq->owner&&WaitForSingleObject(vq->owner,0)==WAIT_OBJECT_0)||
        (DWORD)(GetTickCount()-vq->header->alive_tick)>2000)return SHARED_QUEUE_STATE_STOPPING;
    return (enum queue_state)vq->header->state;
}
static bool lock(video_queue_t* vq){
    const DWORD acquired=WaitForSingleObject(vq->mutex,5);
    if(acquired==WAIT_ABANDONED){ReleaseMutex(vq->mutex);return false;}
    return acquired==WAIT_OBJECT_0;
}
static bool copy_out(video_queue_t* vq,nv12_scale_t* scale,void* dst,uint64_t sequence,uint64_t* timestamp){
    if(!vq||!scale||!dst||video_queue_state(vq)!=SHARED_QUEUE_STATE_READY)return false;
    if(scale->src_cx!=(int)vq->header->cx||scale->src_cy!=(int)vq->header->cy||!valid_extent(scale->dst_cx,scale->dst_cy))return false;
    const bool direct=scale->format==TARGET_FORMAT_NV12&&scale->src_cx==scale->dst_cx&&scale->src_cy==scale->dst_cy;
    if(!direct&&!vq->scratch){vq->scratch=malloc(vq->frame_bytes);if(!vq->scratch)return false;}
    if(!lock(vq))return false;
    if(!sequence)sequence=newest(vq);
    const unsigned index=(unsigned)(sequence%SLOTS);
    if(vq->header->dirty||!sequence||vq->info[index]->sequence!=sequence||video_queue_state(vq)!=SHARED_QUEUE_STATE_READY){ReleaseMutex(vq->mutex);return false;}
    if(timestamp)*timestamp=vq->info[index]->timestamp;
    memcpy(direct?dst:vq->scratch,vq->frame[index],vq->frame_bytes);
    ReleaseMutex(vq->mutex);
    if(!direct)nv12_do_scale(scale,dst,vq->scratch);
    return true;
}
bool video_queue_read(video_queue_t* vq,nv12_scale_t* scale,void* dst,uint64_t* timestamp){
    return timestamp&&copy_out(vq,scale,dst,0,timestamp);
}
bool video_queue_read_sequence(video_queue_t* vq,nv12_scale_t* scale,void* dst,uint64_t sequence){
    return sequence&&copy_out(vq,scale,dst,sequence,NULL);
}
bool video_queue_peek(video_queue_t* vq,uint64_t after,uint64_t* sequence,uint64_t* timestamp,uint64_t* written){
    if(!vq||vq->is_writer||!sequence||!timestamp||!written||video_queue_state(vq)!=SHARED_QUEUE_STATE_READY)return false;
    if(newest(vq)<=after||!lock(vq))return false;
    /* Under the lock no write is in progress: every slot holds one of the newest SLOTS frames. */
    const uint64_t last=newest(vq);bool found=false;
    if(last>after){
        uint64_t next=after+1u;
        if(last>=SLOTS&&next<last-SLOTS+1u)next=last-SLOTS+1u;
        for(;next<=last;++next){
            const struct frame_header* info=vq->info[next%SLOTS];
            if(info->sequence!=next)continue;
            *sequence=next;*timestamp=info->timestamp;*written=info->written;found=true;break;
        }
    }
    ReleaseMutex(vq->mutex);return found;
}
void video_queue_set_consumer(video_queue_t* vq,uint64_t interval){
    if(!vq||vq->is_writer||!vq->writable||interval<10000||interval>10000000)return;
    struct consumer_slot* slots=vq->header->consumers;
    if(vq->consumer>=0&&slots[vq->consumer].token!=vq->token)vq->consumer=-1; /* taken over after a stall */
    for(int i=0;vq->consumer<0&&i<(int)CONSUMERS;++i){
        const LONG holder=slots[i].token;
        const bool stale=holder&&(DWORD)(GetTickCount()-(DWORD)slots[i].tick)>CONSUMER_TIMEOUT_MS;
        if((!holder||stale)&&InterlockedCompareExchange(&slots[i].token,vq->token,holder)==holder)vq->consumer=i;
    }
    if(vq->consumer<0)return;
    InterlockedExchange64(&slots[vq->consumer].interval,(LONG64)interval);
    InterlockedExchange(&slots[vq->consumer].tick,(LONG)GetTickCount());
}
uint64_t video_queue_consumer_interval(video_queue_t* vq){
    if(!vq||!vq->header)return 0;
    uint64_t best=0;
    for(int i=0;i<(int)CONSUMERS;++i){
        const struct consumer_slot* slot=&vq->header->consumers[i];
        if(!slot->token||(DWORD)(GetTickCount()-(DWORD)slot->tick)>CONSUMER_TIMEOUT_MS)continue;
        const uint64_t value=(uint64_t)InterlockedCompareExchange64((volatile LONG64*)&slot->interval,0,0);
        if(value>=10000&&value<=10000000&&(!best||value<best))best=value;
    }
    return best;
}
