#pragma once

// Drive the production callback adapter through a real DirectShow input pin,
// allocator and IMediaSample. An error returned by Receive stops many producers.
template<class Check> void captureVideoDeliveryCases(const AM_MEDIA_TYPE& type,const std::vector<uint8_t>& raw,Check check){
    using namespace veyra::source;
    ComPtr<IBaseFilter> filter;ComPtr<IPin> input,output;
    ComPtr<IMemInputPin> memory;ComPtr<IMemAllocator> allocator;
    output.Attach(new OutputPin);
    unsigned delivered=0;double lastPts=-1;HRESULT downstream=S_OK;
    auto delivery=timedCaptureDelivery([&](double pts,IMediaSample* sample){
        BYTE* bytes=nullptr;sample->GetPointer(&bytes);
        check(bytes&&std::equal(raw.begin(),raw.end(),bytes),"timestamp adapter preserves captured pixels");
        ++delivered;lastPts=pts;return downstream;
    });
    check(delivery(nullptr)==E_POINTER,"timestamp adapter rejects null sample");
    check(createNativeCaptureSink(type,delivery,filter,input)==S_OK&&SUCCEEDED(filter.As(&memory))&&input->ReceiveConnection(output.Get(),&type)==S_OK,"timed video uses production native terminal");
    if(!memory)return;
    ALLOCATOR_PROPERTIES wanted{},actual{};
    const bool ready=memory->GetAllocatorRequirements(&wanted)==S_OK&&memory->GetAllocator(&allocator)==S_OK&&allocator->SetProperties(&wanted,&actual)==S_OK&&allocator->Commit()==S_OK;
    check(ready,"timed video real sample allocator ready");
    if(ready){
        ComPtr<IMediaSample> sample;allocator->GetBuffer(&sample,nullptr,nullptr,0);
        BYTE* bytes=nullptr;sample->GetPointer(&bytes);memcpy(bytes,raw.data(),raw.size());sample->SetActualDataLength(LONG(raw.size()));
        filter->Run(0);
        REFERENCE_TIME start=0,end=0;
        check(sample->GetTime(&start,&end)==VFW_E_SAMPLE_TIME_NOT_SET,"real sample reproduces untimed startup HRESULT");
        // The previous CaptureCardSource lambda returned this failure to Receive.
        check(FAILED(sample->GetTime(&start,&end)),"old callback would signal upstream delivery failure");
        check(memory->Receive(sample.Get())==S_OK&&delivered==0,"untimed priming sample acknowledged without invented frame");
        start=0;end=166833;sample->SetTime(&start,&end);
        check(memory->Receive(sample.Get())==S_OK&&delivered==1&&lastPts==0,"producer continues with real timestamp zero after priming");
        start=166833;sample->SetTime(&start,nullptr);
        check(memory->Receive(sample.Get())==S_OK&&delivered==2&&lastPts==double(start)/1e7,"valid start with missing stop timestamp is delivered exactly");
        sample->SetTime(nullptr,nullptr);
        check(memory->Receive(sample.Get())==S_OK&&delivered==2,"later untimed sample cannot kill an established video stream");
        start=500499;end=667332;sample->SetTime(&start,&end);
        check(memory->Receive(sample.Get())==S_OK&&delivered==3&&lastPts==double(start)/1e7,"timed stream resumes preserving actual PTS gap");
        downstream=E_FAIL;
        check(memory->Receive(sample.Get())==E_FAIL&&delivered==4,"real downstream errors remain visible to producer");
        filter->Stop();sample.Reset();allocator->Decommit();
    }
    input->Disconnect();
}
