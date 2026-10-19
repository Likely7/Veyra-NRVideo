#pragma once
#include <dshow.h>
#include <algorithm>
#include <cstdlib>
#include <vector>

namespace veyra::source {
// This function reads only the driver's discrete list. Range-derived rates
// live separately in CaptureCapabilityRange, are labelled as candidates,
// and require exact connection validation; they are not discrete guarantees.
inline std::vector<REFERENCE_TIME> captureFrameIntervals(IAMVideoControl* control, IPin* pin,
        int index, SIZE dimensions, REFERENCE_TIME advertised, HRESULT* queryResult=nullptr) {
    std::vector<REFERENCE_TIME> out{advertised};
    HRESULT hr=E_NOINTERFACE;
    if(control&&pin){
        long count=0;LONGLONG* rates=nullptr;
        hr=control->GetFrameRateList(pin,index,dimensions,&count,&rates);
        if(SUCCEEDED(hr)&&rates&&count>0&&count<=1000000)
            for(long i=0;i<count;++i)if(rates[i]>0&&std::find(out.begin(),out.end(),rates[i])==out.end())out.push_back(rates[i]);
        CoTaskMemFree(rates);
    }
    // Tests only: VEYRA_TEST_CAPTURE_EXTRA_RATES=N makes every capability also list
    // 1..N fps, as the driver in logs(18) did (272 capabilities x 120 rates, 15474
    // formats), so a card that lists a few rates can reproduce that dialog.
    static const int extraRates=[]{wchar_t value[16]{};return GetEnvironmentVariableW(L"VEYRA_TEST_CAPTURE_EXTRA_RATES",value,16)?std::clamp(_wtoi(value),0,1000):0;}();
    for(int rate=1;rate<=extraRates;++rate){
        const REFERENCE_TIME interval=(10000000+rate/2)/rate;
        if(std::find(out.begin(),out.end(),interval)==out.end())out.push_back(interval);
    }
    if(queryResult)*queryResult=hr;
    return out;
}
}
