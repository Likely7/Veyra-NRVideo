// QML bridge: Capture responsibilities.
#include "../QmlPlayerBridge_p.h"
#include "veyra/source/WasapiAudioInput.h"
#include "VirtualCaptureAudioSetup.h"

namespace veyra::ui {
// --- capture dialog ----------------------------------------------------------
static std::vector<source::CaptureDevice> captureAudioDeviceDetails() {
    auto devices = source::CaptureCardSource::deviceDetails(true);
    for (auto& endpoint : source::WasapiAudioInput::devices(true))
        devices.push_back({std::move(endpoint.name), std::move(endpoint.id), false, true, true});
    return devices;
}
static QString captureDeviceUiLabel(const source::CaptureDevice& device) {
    return source::decklink::isPath(device.path)
        ? uiText(device.name).replace(QStringLiteral("跟随驱动输入"), uiText(L"跟随驱动输入")) : utf8Of(device.name);
}
QVariantList QmlPlayerBridge::captureDevices() const {
    // The source's own enumeration (asynchronous, see refreshCaptureDevices). An
    // empty list means the machine reports no capture devices.
    QVariantList out;
    for (const auto& d : impl_->captureVideo) {
        const bool native=source::decklink::isPath(d.path);
        const bool enabled=impl_->prefBool("blackmagicNativeCapture",false);
        if(native&&!enabled)continue;
        if(!native&&enabled&&source::isBlackmagicCaptureDevice(d.name,d.path))continue;
        out << QVariantMap{{"id", utf8Of(d.path)}, {"label", captureDeviceUiLabel(d)}, {"embeddedAudio", d.hasEmbeddedAudio}};
    }
    return out;
}
QString QmlPlayerBridge::captureDeviceId() const { return utf8Of(impl_->captureDevice); }
void QmlPlayerBridge::setCaptureDeviceId(const QString& value) {
    if (impl_->captureDriverSettingsOpen) return;
    if (impl_->captureDevice == wideOf(value)) return;
    impl_->captureDevice = wideOf(value);
    impl_->captureFormatList.clear();++impl_->captureFormatGeneration;
    impl_->captureResolutionChoice.clear();impl_->captureRateChoice.clear();
    impl_->captureRequestedResolution.clear();
    impl_->captureFormatKey=impl_->captureDevice==impl_->capturePrefs.videoPath?impl_->capturePrefs.formatKey:std::wstring{};
    impl_->captureDriverFormatKey.clear();
    // Colour follows the device, as in 1.4.4; audio returns to "none" unless it
    // is the remembered device.
    impl_->captureColor = impl_->capturePrefs.colorForDevice(impl_->captureDevice);
    if(impl_->captureDevice==impl_->capturePrefs.videoPath)impl_->restoreCaptureForm();
    else {impl_->captureAudioChoice=source::kCaptureAudioDisabled;impl_->captureAudioMissing=false;}
    if(!impl_->persistCaptureSelection())emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);
    if (!impl_->captureDevice.empty()) queryCapture(1, impl_->captureDevice);
    emit captureChanged();
}
QString QmlPlayerBridge::captureDeviceLabel() const {
    for (const auto& d : impl_->captureVideo) if (d.path == impl_->captureDevice) return captureDeviceUiLabel(d);
    return impl_->captureDevice.empty() ? tr("未选择设备") : tr("设备未连接");
}
static QString captureFormatUiLabel(const source::CaptureFormat& f, const std::wstring& device) {
    auto label=uiText(f.label);
    if(source::decklink::isPath(device))label=label.replace(QStringLiteral("逐行"),QmlPlayerBridge::tr("逐行")).replace(QStringLiteral("隔行"),QmlPlayerBridge::tr("隔行"));
    if(source::captureMjpegFormat(f))label+=uiText(L"（不建议使用）");
    return label;
}
QVariantList QmlPlayerBridge::captureFormats() const {
    return impl_->captureList(impl_->captureFormatsCache, impl_->captureDevice, {}, [this] {
        QVariantList out;
        out.reserve(qsizetype(impl_->captureFormatList.size()));
        for (const auto& f : impl_->captureFormatList) {
            out << QVariantMap{{"id", utf8Of(f.key)}, {"label", captureFormatUiLabel(f, impl_->captureDevice)}, {"tier", f.tier},
                               {"costHint", source::captureFormatNeedsCostHint(static_cast<source::CaptureFormatTier>(f.tier))}, {"driverCurrent", f.driverCurrent},
                               {"width", int(f.width)}, {"height", int(f.height)}, {"fps", f.fps}, {"pixel", uiText(f.pixel)},
                               {"scan", uiText(f.scan)}, {"interlaced", f.interlaced}, {"disabled", !f.supported}, {"rangeCandidate", f.rangeCandidate}, {"danger",source::captureMjpegFormat(f)}};
        }
        return out;
    });
}
QString QmlPlayerBridge::captureFormatKey() const { return utf8Of(impl_->captureFormatKey); }
QString QmlPlayerBridge::captureFormatLabel() const {
    if (impl_->captureFormatKey.empty()) return {};
    for (const auto& f : impl_->captureFormatList)
        if (f.key == impl_->captureFormatKey) return captureFormatUiLabel(f, impl_->captureDevice);
    return {};
}
bool QmlPlayerBridge::captureFormatCostHint() const {
    if (impl_->captureFormatKey.empty()) return false;
    for (const auto& f : impl_->captureFormatList)
        if (f.key == impl_->captureFormatKey) return source::captureFormatNeedsCostHint(static_cast<source::CaptureFormatTier>(f.tier));
    return false;
}
void QmlPlayerBridge::setCaptureFormatKey(const QString& key) {
    const auto* format=source::selectCaptureFormat(impl_->captureFormatList,-1,wideOf(key));
    if(!format){
        // Retain the attempted identity as invalid, so a subsequent Connect
        // cannot silently open the previous selection instead.
        impl_->captureFormatKey=wideOf(key);
        impl_->captureStatus=tr("此采集卡无此格式，请重新选择分辨率、帧率和像素格式。");
        emit notice(impl_->captureStatus,true);emit captureChanged();return;
    }
    if (impl_->captureFormatKey == format->key && impl_->captureResolutionChoice == source::captureResolutionId(*format) && impl_->captureRateChoice == source::captureFrameRateGroupId(*format)) return;
    impl_->captureResolutionChoice=source::captureResolutionId(*format);
    impl_->captureRateChoice=source::captureFrameRateGroupId(*format);
    impl_->captureFormatKey = format->key;
    impl_->captureStatus.clear();
    if(!impl_->persistCaptureSelection(true))emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);
    emit captureChanged();
}
namespace {
QVariantList captureChoiceList(const std::vector<source::CaptureFormatOption>& options) {
    QVariantList out;
    for (const auto& option : options)
        out << QVariantMap{{"id", utf8Of(option.id)}, {"label", uiText(option.label)+(option.discouraged?uiText(L"（不建议使用）"):QString{})}, {"disabled",option.disabled},{"danger",option.discouraged}};
    return out;
}
const source::CaptureFormat* selectedCaptureFormat(const std::vector<source::CaptureFormat>& formats, const std::wstring& key) {
    return source::selectCaptureFormat(formats, -1, key);
}
}
QVariantList QmlPlayerBridge::captureResolutionOptions() const {
    return impl_->captureList(impl_->captureResolutionCache, {}, {}, [this] { return captureChoiceList(source::captureResolutionOptions(impl_->captureFormatList)); });
}
QString QmlPlayerBridge::captureResolutionId() const {
    return utf8Of(impl_->captureResolutionChoice);
}
void QmlPlayerBridge::setCaptureResolutionId(const QString& id) {
    QString normalized=id.trimmed().toLower();normalized.remove(QLatin1Char(' '));
    normalized.replace(QChar(0x00D7),QLatin1Char('x'));
    const auto wide = wideOf(normalized);
    if(impl_->captureResolutionChoice==wide)return;
    const auto* sample=source::captureResolutionSample(impl_->captureFormatList,wide);
    if(!sample){
        const auto hint=source::captureResolutionHint(wide);
        if(hint.width&&hint.height&&!impl_->captureDevice.empty()&&!source::decklink::isPath(impl_->captureDevice)){
            impl_->captureRequestedResolution=wide;queryCapture(4,impl_->captureDevice);return;
        }
        emit notice(tr("此采集卡无此格式，请重新选择分辨率、帧率和像素格式。"),true);return;
    }
    impl_->captureResolutionChoice=wide;
    if(!source::captureRateSample(impl_->captureFormatList,wide,impl_->captureRateChoice))
        if(const auto* rate=source::captureDefaultFrameRate(impl_->captureFormatList,wide))impl_->captureRateChoice=source::captureFrameRateGroupId(*rate);
    impl_->captureFormatKey.clear();
    if(!impl_->persistCaptureSelection(true))emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);
    emit captureChanged();
}
QVariantList QmlPlayerBridge::captureFrameRateOptions() const {
    // Common rates only unless 显示全部可用帧率 is on (captureVisibleFrameRates).
    const bool all = impl_->prefBool("captureShowAllFrameRates", false);
    return impl_->captureList(impl_->captureRateCache, impl_->captureResolutionChoice, impl_->captureRateChoice + (all ? L"|all" : L""), [this, all] {
        size_t hidden = 0;
        const auto rates = source::captureVisibleFrameRates(source::captureFrameRateOptions(impl_->captureFormatList,
            source::captureResolutionSample(impl_->captureFormatList, impl_->captureResolutionChoice)), impl_->captureRateChoice, all, &hidden);
        impl_->captureHiddenRates = int(hidden);
        return captureChoiceList(rates);
    });
}
int QmlPlayerBridge::captureHiddenFrameRates() const {
    (void)captureFrameRateOptions();   // the count is built with the list
    return impl_->captureHiddenRates;
}
QString QmlPlayerBridge::captureFrameRateId() const {
    return utf8Of(impl_->captureRateChoice);
}
void QmlPlayerBridge::setCaptureFrameRateId(const QString& id) {
    const auto wide = wideOf(id);
    if(impl_->captureResolutionChoice.empty()){emit notice(tr("请先选择分辨率"),true);return;}
    if(impl_->captureRateChoice==wide)return;
    if(!source::captureRateSample(impl_->captureFormatList,impl_->captureResolutionChoice,wide)){
        emit notice(tr("此采集卡无此格式，请重新选择分辨率、帧率和像素格式。"),true);return;
    }
    const auto* sample=source::captureRateSample(impl_->captureFormatList,impl_->captureResolutionChoice,wide);
    impl_->captureRateChoice=source::captureFrameRateGroupId(*sample);impl_->captureFormatKey.clear();
    if(!impl_->persistCaptureSelection(true))emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);
    emit captureChanged();
}
QVariantList QmlPlayerBridge::capturePixelOptions() const {
    return impl_->captureList(impl_->capturePixelCache, impl_->captureResolutionChoice, impl_->captureRateChoice, [this] {
        return captureChoiceList(source::capturePixelOptions(impl_->captureFormatList, source::captureRateSample(impl_->captureFormatList,impl_->captureResolutionChoice,impl_->captureRateChoice)));
    });
}
QString QmlPlayerBridge::capturePixelId() const {
    const auto* format = source::selectCapturePixelFormat(impl_->captureFormatList,impl_->captureResolutionChoice,impl_->captureRateChoice,impl_->captureFormatKey);
    return format ? utf8Of(format->key) : QString();
}
void QmlPlayerBridge::setCapturePixelId(const QString& id) {
    const auto key = wideOf(id);
    if(impl_->captureResolutionChoice.empty()){emit notice(tr("请先选择分辨率"),true);return;}
    if(!source::selectCapturePixelFormat(impl_->captureFormatList,impl_->captureResolutionChoice,impl_->captureRateChoice,key)){
        impl_->captureFormatKey=key;
        impl_->captureStatus=tr("此采集卡无此格式，请重新选择分辨率、帧率和像素格式。");
        emit notice(impl_->captureStatus,true);emit captureChanged();return;
    }
    setCaptureFormatKey(id);
}
QVariantList QmlPlayerBridge::captureAudioInputs() const {
    // Only what the user picks explicitly is ever opened (1.4.4 rule).
    QVariantList out{QVariantMap{{"id", source::kCaptureAudioDisabled}, {"label", tr("不监听音频")}}};
    if(impl_->captureAudioMissing)out<<QVariantMap{{"id",-4},{"label",tr("上次的音频输入未连接")},{"disabled",true}};
    for (const auto& d : impl_->captureVideo)
        if (d.path == impl_->captureDevice)
            out << QVariantMap{{"id", source::kCaptureAudioFromVideoDevice},
                               {"label", d.hasEmbeddedAudio ? tr("视频设备内置音频（已检测）") : tr("尝试视频设备内置音频")}};
    for (size_t i = 0; i < impl_->captureAudio.size(); ++i) {
        const auto& device = impl_->captureAudio[i];
        const auto label = device.virtualSpeaker ? tr("Veyra 虚拟扬声器 · %1").arg(utf8Of(device.name))
            : QStringLiteral("[%1] %2").arg(device.wasapi ? QStringLiteral("WASAPI") : QStringLiteral("DirectShow"), utf8Of(device.name));
        out << QVariantMap{{"id", int(i)}, {"label", label}};
    }
    return out;
}
int QmlPlayerBridge::captureAudioChoice() const { return impl_->captureAudioMissing?-4:impl_->captureAudioChoice; }
void QmlPlayerBridge::setCaptureAudioChoice(int choice) {
    if (choice < source::kCaptureAudioFromVideoDevice || choice >= int(impl_->captureAudio.size())) return;
    impl_->captureAudioChoice = choice;
    impl_->captureAudioMissing=false;
    // Remember the audio choice for this device as soon as it is picked, not only on
    // 连接并开始 (field report 2026-10-01: picked, closed, and it was gone next time).
    if(!impl_->persistCaptureSelection())emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);
    emit captureChanged();
}
int QmlPlayerBridge::captureColorSpace() const { return int(source::captureColorSpace(impl_->captureColor)); }
void QmlPlayerBridge::setCaptureColorSpace(int value) {
    if (value < 0 || value > 3) return;
    impl_->captureColor = source::captureColorOverride(unsigned(value), source::captureColorRange(impl_->captureColor));
    if(!impl_->persistCaptureSelection())emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);
    emit captureChanged();
}
int QmlPlayerBridge::captureColorRange() const { return int(source::captureColorRange(impl_->captureColor)); }
void QmlPlayerBridge::setCaptureColorRange(int value) {
    if (value < 0 || value > 2) return;
    impl_->captureColor = source::captureColorOverride(source::captureColorSpace(impl_->captureColor), unsigned(value));
    if(!impl_->persistCaptureSelection())emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);
    emit captureChanged();
}
double QmlPlayerBridge::captureRequestedFps() const { return impl_->captureFps; }
void QmlPlayerBridge::setCaptureRequestedFps(double value) {
    if (!source::validCaptureFrameRate(value)) { emit notice(tr("采集帧率请输入 1–1000 的数字（可带小数），或 0 沿用设备默认"), true); return; }
    if (impl_->captureFps == value) return;
    impl_->captureFps = value;
    // Saved as soon as it is typed, as the audio choice is (field report 2026-10-01: the
    // new rate was lost unless another option was changed before closing).
    if(!impl_->persistCaptureSelection())emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);
    veyra::log::info("capture-ui", std::format("requested fps={}", value));
    emit captureChanged();
}
int QmlPlayerBridge::captureAudioIngress() const { return int(settings().captureAudio); }
void QmlPlayerBridge::setCaptureAudioIngress(int value) {
    if (value < 0 || value > 2) return;
    auto s = settings(); s.captureAudio = static_cast<engine::CaptureAudioIngress>(value);
    if(impl_->commit(std::move(s))){impl_->prefs["captureAudioIngress"]=value;if(!impl_->savePrefs())emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);}
    emit settingsChanged();
}
int QmlPlayerBridge::captureBufferMode() const { return int(settings().captureBuffer); }
void QmlPlayerBridge::setCaptureBufferMode(int value) {
    if (value < 0 || value > 2) return;
    auto s = settings(); s.captureBuffer = static_cast<source::CaptureBufferMode>(value);
    if(impl_->commit(std::move(s))){impl_->prefs["captureBufferMode"]=value;if(!impl_->savePrefs())emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);}
    emit settingsChanged();
}
bool QmlPlayerBridge::captureQueryBusy() const { return impl_->captureBusy || impl_->captureDriverSettingsOpen; }
QString QmlPlayerBridge::captureStatus() const { return impl_->captureStatus; }
bool QmlPlayerBridge::captureBlackmagicDevice() const {
    for (const auto& d : impl_->captureVideo) if (d.path == impl_->captureDevice) return source::decklink::isPath(d.path)||source::isBlackmagicCaptureDevice(d.name, d.path);
    return false;
}
bool QmlPlayerBridge::openCaptureDriverSettings(bool inputSelector) {
    auto& i = *impl_;
    if (captureQueryBusy() || i.captureDevice.empty()) { emit notice(tr("请先选择设备并等待查询完成"), true); return false; }
    if (i.openingSource || (i.snapshot.capture && !i.engine.idle())) { emit notice(tr("请等待连接结束并断开采集，再打开驱动设置"), true); return false; }
    HWND owner = i.videoWindow ? GetAncestor(i.videoWindow, GA_ROOT) : nullptr;
    if (!owner) if (auto* window = QGuiApplication::focusWindow()) owner = reinterpret_cast<HWND>(window->winId());
    i.captureDriverSettingsOpen = true; emit captureChanged();
    std::wstring error;
    const bool ok = source::CaptureCardSource::showDeviceProperties(i.captureDevice, reinterpret_cast<uintptr_t>(owner), inputSelector, error, &i.captureDriverFormatKey);
    i.captureDriverSettingsOpen = false;
    if (!ok) { i.captureStatus = uiText(error); emit notice(i.captureStatus, true); emit captureChanged(); return false; }
    // Some property pages change only this filter instance's IAMStreamConfig.
    // Carry its exact identity to the new graph instead of discarding it.
    queryCapture(3, i.captureDevice);
    return true;
}
bool QmlPlayerBridge::captureMagewellDevice() const { return source::magewell::isProCaptureDevicePath(impl_->captureDevice); }
QString QmlPlayerBridge::captureMagewellStatus() const {
    std::wstring runtime;
    if (!source::magewell::runtimeAvailable(&runtime))
        return tr("找不到美乐威运行库 LibMWCapture.dll（应在软件的 runtime\\magewell 文件夹里，或装美乐威驱动/SDK）");
    return veyra::ui::i18n::text(source::magewell::statusText());
}
bool QmlPlayerBridge::captureVirtualAudioInstalled() const { return impl_->captureVirtualAudioInstalled; }
static bool captureVirtualAudioRemovalBlockedBy(const engine::PlayerSnapshot& snapshot) {
    return snapshot.transport == engine::TransportState::Opening ||
           snapshot.transport == engine::TransportState::Stopping ||
           (snapshot.capture && snapshot.running);
}
bool QmlPlayerBridge::captureVirtualAudioRemovalBlocked() const {
    return impl_->openingSource || captureVirtualAudioRemovalBlockedBy(impl_->snapshot);
}
bool QmlPlayerBridge::captureVirtualAudioBusy() const {
    return impl_->captureVirtualAudioSetup && impl_->captureVirtualAudioSetup->busy();
}
void QmlPlayerBridge::setupCaptureVirtualAudio(bool uninstall) {
    if (captureVirtualAudioBusy()) return;
    if (uninstall && (impl_->openingSource || captureVirtualAudioRemovalBlockedBy(impl_->engine.snapshot()))) {
        emit notice(tr("请先断开采集并等待连接结束"), true);
        return;
    }
    auto& i = *impl_;
    i.captureVirtualAudioInstalled = !source::WasapiAudioInput::devices(true,true).empty();
    emit captureChanged();
    if (i.captureVirtualAudioInstalled != uninstall) {
        emit notice(uninstall ? tr("未检测到已安装的 VB-CABLE") : tr("已检测到 VB-CABLE，无需重复安装"), false);
        refreshCaptureDevices();
        return;
    }
    if (!i.captureVirtualAudioSetup) {
        i.captureVirtualAudioSetup = new VirtualCaptureAudioSetup(this);
        connect(i.captureVirtualAudioSetup, &VirtualCaptureAudioSetup::busyChanged, this, &QmlPlayerBridge::captureChanged);
        connect(i.captureVirtualAudioSetup, &VirtualCaptureAudioSetup::completed, this,
            [this](const QString& message, bool warning) {
                emit notice(message, warning);
                refreshCaptureDevices();
            });
    }
    i.captureVirtualAudioSetup->start(dataDirectory());
}
void QmlPlayerBridge::openCaptureWindowsListen() {
    if (!VirtualCaptureAudioSetup::openRecordingSettings())
        emit notice(tr("无法打开 Windows 录制设备，请在声音控制面板打开“录制”"), true);
}
void QmlPlayerBridge::openCaptureVirtualAudioVendor() {
    if (!QDesktopServices::openUrl(QUrl(QStringLiteral("https://vb-audio.com/Cable/"))))
        emit notice(tr("无法打开 VB-Audio 官网"), true);
}

void QmlPlayerBridge::refreshCaptureDevices() {
    if (impl_->captureDriverSettingsOpen) return;
    impl_->captureVirtualAudioInstalled = !source::WasapiAudioInput::devices(true,true).empty();
    impl_->capturePrefs = ui::CapturePreferenceStore(impl_->dataDir).load();
    queryCapture(0, {});
}
void QmlPlayerBridge::queryCapture(int kind, std::wstring device) {
    auto& i = *impl_;
    if (captureQueryBusy()) { i.captureNextQuery=std::pair{kind,std::move(device)};return; }
    i.captureBusy = true; i.captureQueryKind = kind;
    i.captureStatus = kind == 0 ? tr("正在查询采集设备…当前播放继续") : tr("正在读取设备支持的格式…");
    const auto preferred=kind==2?i.capturePrefs.formatKey:
        kind==3&&!i.captureDriverFormatKey.empty()?i.captureDriverFormatKey:i.captureFormatKey;
    const auto requested=kind==4?i.captureRequestedResolution:
        preferred.empty()&&wideOf(i.prefs.value("captureDraftDevice").toString())==device?
            wideOf(i.prefs.value("captureDraftResolution").toString()):std::wstring{};
    i.captureQuery = std::async(std::launch::async, [kind, device, preferred, requested] {
        Impl::CaptureQuery q; q.kind = kind; q.device = device;q.requestedResolution=requested;
        const auto initialized=CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if(FAILED(initialized)){q.info.code=initialized;q.info.stage=L"COM initialization";return q;}
        try {
            if (kind == 0) { q.video = source::CaptureCardSource::deviceDetails(); q.audio = captureAudioDeviceDetails(); }
            else {
                if (kind == 2) { q.video = source::CaptureCardSource::deviceDetails(); q.audio = captureAudioDeviceDetails(); }
                q.formats = source::CaptureCardSource::formatsByPath(device,&q.info,preferred,requested);
            }
        } catch (...) {q.info.code=E_FAIL;q.info.stage=L"Capture capability exception";}
        CoUninitialize();
        return q;
    });
    emit captureChanged();
}
void QmlPlayerBridge::tickCapture() {
    auto& i = *impl_;
    if (!i.captureBusy || i.captureQuery.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto q = i.captureQuery.get();
    i.captureBusy = false;
    if(i.captureNextQuery){auto next=std::move(*i.captureNextQuery);i.captureNextQuery.reset();queryCapture(next.first,std::move(next.second));return;}
    if(FAILED(q.info.code)&&q.formats.empty()){
        if(q.kind!=0&&q.device!=i.captureDevice){emit captureChanged();return;}
        if(q.kind!=0&&q.kind!=4){i.captureFormatList.clear();++i.captureFormatGeneration;i.captureResolutionChoice.clear();i.captureRateChoice.clear();}
        i.captureStatus=tr("读取采集格式失败：%1（0x%2）。请检查设备占用与驱动后重试。")
            .arg(utf8Of(q.info.stage),QString::number(quint32(q.info.code),16).rightJustified(8,QLatin1Char('0')));
        emit notice(i.captureStatus,true);emit captureChanged();return;
    }
    if (q.kind == 0 || q.kind == 2) { i.captureVideo = std::move(q.video); i.captureAudio = std::move(q.audio); i.captureListed = true; }
    // The remembered rate and audio input, matched against the inputs listed now. Both the
    // panel (kind 0) and 继续上次 (kind 2) need it: the resume path skipped it and connected
    // without sound until the panel was opened once (field report 2026-10-02).
    const auto restoreRememberedAudio = [&i] {
        i.restoreCaptureForm();
    };
    if (q.kind == 2 && q.device == i.capturePrefs.videoPath) restoreRememberedAudio();
    if (q.kind == 0) {
        veyra::log::info("capture-ui", std::format("devices video={} audio={}", i.captureVideo.size(), i.captureAudio.size()));
        // Restore the remembered device; otherwise the first one. Never guess audio.
        std::wstring pick;
        const auto choices=captureDevices();
        auto allowed=[&](std::wstring_view path){for(const auto& value:choices)if(wideOf(value.toMap().value("id").toString())==path)return true;return false;};
        if(allowed(i.capturePrefs.videoPath))pick=i.capturePrefs.videoPath;
        // The two APIs are separate identities. Restore an explicitly used
        // endpoint, or select a unique native endpoint; never map by ordinal.
        const bool native=i.prefBool("blackmagicNativeCapture",false);
        const auto remembered=wideOf(i.prefs.value(native?"blackmagicNativeLastDevice":"blackmagicDirectShowLastDevice").toString());
        if(pick.empty()&&allowed(remembered))pick=remembered;
        if(pick.empty()&&native){std::vector<std::wstring> candidates;for(const auto& d:i.captureVideo)if(source::decklink::isPath(d.path))candidates.push_back(d.path);if(candidates.size()==1)pick=candidates.front();}
        if (pick.empty() && !choices.empty() && i.capturePrefs.videoPath.empty()&&!native) pick=wideOf(choices.front().toMap().value("id").toString());
        i.captureStatus = i.captureVideo.empty() ? tr("未找到采集设备。连接后点击刷新。")
                        : pick.empty() ? tr("上次采集设备未连接。插回后刷新，或手动选择其他设备。") : QString();
        if (!pick.empty()) {
            i.captureDevice.clear();
            setCaptureDeviceId(utf8Of(pick));
            if (pick == i.capturePrefs.videoPath) restoreRememberedAudio();
        }else{setCaptureDeviceId({});if(native)i.captureStatus=tr("请选择 BMD 原生设备；若列表中没有，请安装 Desktop Video 驱动。多个设备不会自动猜选。");}
        emit captureChanged();
        return;
    }
    if (q.device != i.captureDevice) { emit captureChanged(); return; }   // stale answer
    if(q.kind==4&&!source::captureResolutionSample(q.formats,q.requestedResolution)){
        i.captureStatus=tr("驱动未提供该分辨率，或尺寸不符合步进要求；原选择已保留。");
        emit notice(i.captureStatus,true);emit captureChanged();return;
    }
    i.captureFormatList = std::move(q.formats);
    ++i.captureFormatGeneration;
    const bool remembered = i.captureDevice == i.capturePrefs.videoPath;
    const std::wstring driverSelection = i.captureDriverFormatKey.empty() ? i.captureFormatKey : i.captureDriverFormatKey;
    if (q.kind == 3) i.captureDriverFormatKey.clear();
    i.captureFormatKey.clear();
    if (q.kind == 3 && !driverSelection.empty()) {
        if (const auto* f = source::selectCaptureFormat(i.captureFormatList, -1, driverSelection)) i.captureFormatKey = f->key;
    } else if (remembered && !i.capturePrefs.formatKey.empty()) {
        if (const auto* f = source::selectCaptureFormat(i.captureFormatList, -1, i.capturePrefs.formatKey)) i.captureFormatKey = f->key;
    }
    i.captureResolutionChoice.clear();i.captureRateChoice.clear();
    if(const auto* f=selectedCaptureFormat(i.captureFormatList,i.captureFormatKey)){
        i.captureResolutionChoice=source::captureResolutionId(*f);i.captureRateChoice=source::captureFrameRateGroupId(*f);
    }else if(remembered&&i.capturePrefs.formatKey.empty()&&wideOf(i.prefs.value("captureDraftDevice").toString())==i.captureDevice){
        const auto resolution=wideOf(i.prefs.value("captureDraftResolution").toString());
        const auto rate=wideOf(i.prefs.value("captureDraftRate").toString());
        if(source::captureResolutionSample(i.captureFormatList,resolution)){
            i.captureResolutionChoice=resolution;
            if(const auto* sample=source::captureRateSample(i.captureFormatList,resolution,rate))i.captureRateChoice=source::captureFrameRateGroupId(*sample);
        }
    }
    if(q.kind==4){
        i.captureResolutionChoice=q.requestedResolution;i.captureFormatKey.clear();
        if(const auto* rate=source::captureDefaultFrameRate(i.captureFormatList,i.captureResolutionChoice))i.captureRateChoice=source::captureFrameRateGroupId(*rate);
        if(!i.persistCaptureSelection(true))emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);
    }
    if(!i.captureFormatKey.empty()&&!i.persistCaptureSelection())emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);
    i.captureStatus = i.captureFormatList.empty() ? tr("驱动未提供可解析的视频格式，请检查设备与驱动后刷新。")
                    : i.captureFormatKey.empty() ? (!i.capturePrefs.formatKey.empty()?tr("此采集卡无此格式，请重新选择分辨率、帧率和像素格式。"):tr("先选择分辨率，再选择该分辨率支持的帧率和像素格式"))
                    : q.kind == 3 ? tr("驱动设置已关闭。请核对所选格式与设备帧率，再连接。")
                    : source::decklink::isPath(i.captureDevice) ? tr("原生采集先确认有效信号；请在设备列表选择对应的 HDMI/SDI 输入，支持自动检测的卡建议选择自动。Driver 保留驱动当前输入。")
                    : captureBlackmagicDevice() ? tr("请选择与输入信号一致的格式。驱动默认格式不代表检测到的信号。")
                    : tr("连接后使用当前增强设置。格式与音频变更需要重新连接。");
    if(q.info.partial)i.captureStatus=tr("部分采集能力读取失败（%1，0x%2）；当前列表可能不完整，请刷新重试。")
        .arg(utf8Of(q.info.stage),QString::number(quint32(q.info.code),16).rightJustified(8,QLatin1Char('0')));
    veyra::log::info("capture-ui", std::format("formats={} selected={} raw={} rejected={} partial={} fallback={}", i.captureFormatList.size(), !i.captureFormatKey.empty(),q.info.advertised,q.info.rejected,q.info.partial,q.info.fallback));
    emit captureChanged();
    if (q.kind == 2) {
        if (i.captureFormatKey.empty()) emit notice(tr("上次的采集格式不可用，请在采集卡面板重新选择"), true);
        else startCaptureSession();
    }
}
bool QmlPlayerBridge::startCaptureSession() {
    auto& i = *impl_;
    if (captureQueryBusy()) { emit notice(tr("设备查询或驱动设置尚未完成"), true); return false; }
    int device = -1;
    for (size_t k = 0; k < i.captureVideo.size(); ++k) if (i.captureVideo[k].path == i.captureDevice) device = int(k);
    const auto* format = source::selectCapturePixelFormat(i.captureFormatList,i.captureResolutionChoice,i.captureRateChoice,i.captureFormatKey);
    if(source::decklink::isPath(i.captureDevice)!=i.prefBool("blackmagicNativeCapture",false)&&captureBlackmagicDevice()){
        emit notice(tr("采集接口已切换，请刷新并重新选择设备和格式"),true);return false;
    }
    if(device<0){emit notice(tr("请先选择采集设备和格式"),true);return false;}
    if(!format){emit notice(i.captureFormatKey.empty()&&i.capturePrefs.formatKey.empty()?tr("请先选择分辨率和像素格式"):tr("此采集卡无此格式，请重新选择分辨率、帧率和像素格式。"),true);return false;}
    if(!format->supported){emit notice(tr("设备提供此格式，但当前播放器不支持其尺寸或像素布局，请选择其他格式"),true);return false;}
    if(i.captureAudioMissing){emit notice(tr("上次的音频输入未连接，请插回设备或重新选择音频监听"),true);return false;}
    const int audio = i.captureAudioChoice;
    const source::CaptureDevice* audioDevice = audio >= 0 && size_t(audio) < i.captureAudio.size() ? &i.captureAudio[size_t(audio)] : nullptr;
    if (audioDevice && audioDevice->virtualSpeaker) {
        if (captureVirtualAudioBusy()) {
            emit notice(tr("正在处理 VB-CABLE 安装器…"), true);
            return false;
        }
        const auto outputs = sink::enumerateRenderEndpoints();
        const auto preferred = wideOf(i.prefString("audioDevice"));
        auto output = std::find_if(outputs.begin(), outputs.end(), [&](const auto& device) { return device.id == preferred; });
        if (output == outputs.end())
            output = std::find_if(outputs.begin(), outputs.end(), [](const auto& device) { return device.isDefault; });
        if (output != outputs.end() && output->id == audioDevice->path) {
            emit notice(tr("虚拟扬声器回录需要将音频输出设为真实耳机或音箱"), true);
            return false;
        }
    }
    const auto path = source::CaptureCardSource::makeCapturePath(unsigned(device), i.captureVideo[size_t(device)], format->index, audio,
                                                                 audioDevice, i.captureColor, i.captureFps, format->key);
    if (path.empty()) { emit notice(tr("无法生成采集连接参数"), true); return false; }
    if(!i.persistCaptureSelection())emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);
    i.facade.noteCaptureSession({i.captureDevice, format->key, i.facade.presets().entries().empty() ? std::wstring{} : wideOf(currentPresetName()),
                                 i.captureFps > 0 ? i.captureFps : format->fps, true});
    veyra::log::info("capture-ui", std::format("connect format={} audio={} color={} fps={}", QString::fromStdWString(format->label).toStdString(), audio, i.captureColor, i.captureFps));
    i.prefs["captureDeviceName"] = utf8Of(i.captureVideo[size_t(device)].name);
    if(captureBlackmagicDevice())i.prefs[source::decklink::isPath(i.captureDevice)?"blackmagicNativeLastDevice":"blackmagicDirectShowLastDevice"]=utf8Of(i.captureDevice);
    rememberSource(QStringLiteral("capture"), utf8Of(i.captureVideo[size_t(device)].name));
    i.prefs["captureFormatLabel"] = utf8Of(format->label);
    i.savePrefs();
    // Started beside the capture open, not after it. A running PS5 stream still
    // holds the console's slot until the capture replaces it; the tick starts
    // PS5 手柄控制 once the capture card is live instead.
    if (i.prefBool("ps5CaptureControl", false) && !i.snapshot.remotePlay) startPs5Control(false);
    openSourceUri(path, i.captureVideo[size_t(device)].name);
    emit snapshotChanged();
    return true;
}
void QmlPlayerBridge::openSourceUri(const std::wstring& uri, const std::wstring& label, bool screenFill) {
    // A capture or stream open supersedes a waiting Bilibili mirror retry just as a
    // file open does; this path does not go through openPath, so it bumps the
    // generation itself and drops the retry state that belonged to the old source.
    // 采集/串流打开同样要顶掉还没触发的 B 站镜像重试；这段在 2.1.2 拆桥接层时丢过。
    ++impl_->biliOpenGen;
    impl_->biliMirrors.clear();
    impl_->biliMirrorIndex = 0;
    impl_->biliRetriesLeft = 0;
    impl_->biliRetryPending = false;
    rememberPosition(true);
    impl_->openingSource = true;
    impl_->openingSessionId = 0;
    impl_->sourceLabel = label;
    impl_->resumeSession = 0;
    impl_->screenFillActive = screenFill;
    // Honor the dialog's initial viewport before input can be taken over.
    // Later explicit aspect choices still replace this session preset.
    if (!impl_->gameModeUri.empty() && uri == impl_->gameModeUri) impl_->aspectMode = 0;
    const bool screen = uri.find(L"screen") != std::wstring::npos;
    veyra::log::info("capture-ui", "open uri " + QString::fromStdWString(uri).toStdString());
    openAfterCinema(screen ? tr("正在开始屏幕捕获…") : tr("正在打开采集设备 · %1").arg(utf8Of(label)), [this, uri] {
        if (impl_->preOpen) impl_->preOpen();
        impl_->engine.previewView({});
        impl_->engine.open(impl_->videoWindow, uri, impl_->options);
        impl_->openingSessionId = impl_->engine.snapshot().sessionId;
        // Only the exact capture startScreenCapture opened in 游戏模式 is one.
        impl_->gameModeSessionId = !impl_->gameModeUri.empty() && uri == impl_->gameModeUri ? impl_->openingSessionId : 0;
        impl_->gameModeLive = false;
        impl_->gameModeUri.clear();
        updateGameMode();
        refreshSubtitles({});
    });
}

void QmlPlayerBridge::openAfterCinema(const QString& pending, std::function<void()> open) {
    const int gen = ++impl_->liveOpenGen;
    const bool fromHome = impl_->currentPage.isEmpty() || impl_->currentPage == QLatin1String("home");
    if (!fromHome || !impl_->gameModeUri.empty()) {
        // Game mode does not wait for the cinema animation or feature startup.
        if (fromHome) emit navigate(QStringLiteral("min"));
        impl_->pendingLiveText.clear(); open(); return;
    }
    // User report 2026-09-29: the window blacked out and jumped into 极简 when
    // the engine opened in the same instant as the page switch and the window's
    // height spring. Switch first, say that it is connecting, then open once
    // the page (.22s out + 150 ms + rise) and the .7s height spring have settled.
    emit navigate(QStringLiteral("min"));
    impl_->pendingLiveText = pending;
    emit snapshotChanged();
    const int delay = reducedMotion() ? 0 : 820;
    veyra::log::info("ui-live-open", std::format("deferred ms={} gen={}", delay, gen));
    QTimer::singleShot(delay, this, [this, gen, open = std::move(open)] {
        if (gen != impl_->liveOpenGen) { veyra::log::info("ui-live-open", std::format("superseded gen={}", gen)); return; }
        impl_->pendingLiveText.clear();
        veyra::log::info("ui-live-open", std::format("open gen={}", gen));
        open();
        emit snapshotChanged();
    });
}

// Force-SDR and vertical flip are real settings the engine already carries, so
// these are genuine controls rather than placeholders.
bool QmlPlayerBridge::captureForceSdr() const { return settings().forceSdrPreview; }
void QmlPlayerBridge::setCaptureForceSdr(bool value) {
    auto s = settings();
    if (s.forceSdrPreview == value) return;
    s.forceSdrPreview = value;
    if(impl_->commit(std::move(s))){impl_->prefs["captureForceSdr"]=value;if(!impl_->savePrefs())emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);}
    emit settingsChanged();
}
bool QmlPlayerBridge::captureFlipVertical() const { return settings().captureFlipVertical; }
void QmlPlayerBridge::setCaptureFlipVertical(bool value) {
    auto s = settings();
    if (s.captureFlipVertical == value) return;
    s.captureFlipVertical = value;
    if(impl_->commit(std::move(s))){impl_->prefs["captureFlipVertical"]=value;if(!impl_->savePrefs())emit notice(tr("采集设置保存失败，请检查配置目录是否可写"),true);}
    emit settingsChanged();
}

// --- screen capture ----------------------------------------------------------
QVariantList QmlPlayerBridge::screenTargets() const {
    // Enumerated from the source for the chosen kind; cached until refreshed so a
    // binding re-evaluation does not walk every window again.
    QVariantList out;
    for (const auto& target : impl_->screenList) {
        QVariantMap item;
        item["id"] = QString::number(target.handle);
        QString label = QString::fromWCharArray(target.name.c_str());
        if (target.width > 0)
            label += QStringLiteral(" · %1x%2").arg(target.width).arg(target.height);
        if (target.refresh > 0) label += QStringLiteral(" · %1Hz").arg(target.refresh);
        item["label"] = label;
        item["width"] = int(target.width);
        item["height"] = int(target.height);
        out << item;
    }
    return out;
}
QVariantMap QmlPlayerBridge::screenOptions() const {
    QVariantMap d{{"kind", 0}, {"method", 0}, {"fps", 0}, {"cursor", true}, {"fill", false},
                  {"left", 0}, {"top", 0}, {"right", 0}, {"bottom", 0}, {"passthrough", false}};
    const auto saved = impl_->prefs.value("screen").toMap();
    for (auto it = saved.begin(); it != saved.end(); ++it) if (d.contains(it.key())) d[it.key()] = it.value();
    if (d.value("kind").toInt() == 0) d["method"] = 0;           // DXGI is monitors only
    if (d.value("method").toInt() == 1) d["cursor"] = false;     // and has no pointer
    return d;
}
bool QmlPlayerBridge::setScreenOption(const QString& key, const QVariant& value) {
    static const QHash<QString, std::pair<int, int>> ranges{{"kind", {0, 1}}, {"method", {0, 1}}, {"fps", {0, 5}},
        {"left", {0, 16384}}, {"top", {0, 16384}}, {"right", {0, 16384}}, {"bottom", {0, 16384}}};
    auto saved = impl_->prefs.value("screen").toMap();
    if (ranges.contains(key)) {
        bool ok = false; const int n = value.toInt(&ok);
        const auto [low, high] = ranges.value(key);
        if (!ok || n < low || n > high) { emit notice(tr("数值超出范围"), true); return false; }
        saved[key] = n;
    } else if (key == QLatin1String("cursor") || key == QLatin1String("fill") || key == QLatin1String("passthrough")) {
        saved[key] = value.toBool();
    } else return false;
    impl_->prefs["screen"] = saved;
    impl_->savePrefs();
    if (key == QLatin1String("kind")) { impl_->screenTarget.clear(); refreshCaptureTargets(); }
    emit captureChanged();
    return true;
}
bool QmlPlayerBridge::startScreenCapture() {
    const auto o = screenOptions();
    source::ScreenCaptureOptions value;
    bool found = false;
    for (const auto& t : impl_->screenList)
        if (QString::number(t.handle) == impl_->screenTarget) { value.kind = t.kind; value.target = t.handle; found = true; }
    if (!found) { emit notice(tr("先选择一个捕获目标"), true); return false; }
    constexpr unsigned rates[] = {0, 30, 60, 120, 144, 240};
    value.method = source::ScreenCaptureMethod(o.value("method").toInt());
    value.fps = rates[std::clamp(o.value("fps").toInt(), 0, 5)];
    value.cursor = o.value("cursor").toBool();
    value.left = o.value("left").toUInt(); value.top = o.value("top").toUInt();
    value.right = o.value("right").toUInt(); value.bottom = o.value("bottom").toUInt();
    // 游戏模式: the real pointer is drawn on top, so the picture carries none (a
    // second, late pointer otherwise), the picture must match the screen 1:1 for
    // clicks to land where they are seen (no crop), and the yellow border goes.
    const bool game = o.value("passthrough").toBool();
    if (game) {
        value.cursor = false; value.borderless = true;
        value.left = value.top = value.right = value.bottom = 0;
        const HWND window = value.kind == source::ScreenTargetKind::Window ? reinterpret_cast<HWND>(value.target) : nullptr;
        impl_->gameMonitor = window ? MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST) : reinterpret_cast<HMONITOR>(value.target);
        impl_->game.setTarget(window, impl_->gameMonitor, !window);
        impl_->gameModeUri = value.uri();
        // Exclusive fullscreen still cannot be covered by another process.
        QUERY_USER_NOTIFICATION_STATE state{};
        if (SUCCEEDED(SHQueryUserNotificationState(&state)) && state == QUNS_RUNNING_D3D_FULL_SCREEN)
            emit notice(tr("检测到独占全屏的游戏：Windows 不允许其它窗口盖在它上面，请把游戏改成无边框窗口或窗口化"), true);
    } else impl_->gameModeUri.clear();
    veyra::log::info("screen-ui", std::format("start kind={} method={} fps={} cursor={} crop={},{},{},{} fill={} gameMode={}", int(value.kind), int(value.method),
        value.fps, value.cursor, value.left, value.top, value.right, value.bottom, o.value("fill").toBool(), game));
    openSourceUri(value.uri(), wideOf(screenTargetLabel()), o.value("fill").toBool());
    impl_->screenFitClient = impl_->screenFitImage = QSize();
    if (gameMode()) { updateGameGeometry(); updateGameMode(); emit gameModeChanged(); }
    emit compareChanged();
    rememberSource(QStringLiteral("screen"), screenTargetLabel());
    return true;
}
bool QmlPlayerBridge::gameMode() const { return impl_->gameModeSessionId != 0; }
bool QmlPlayerBridge::passthroughActive() const { return impl_->passthroughShown; }
bool QmlPlayerBridge::gameInputActive() const { return impl_->gameInputShown; }
bool QmlPlayerBridge::gameScaledPreview() const {
    return gameMode() && (ui::gameViewportScaled(impl_->screenFillActive, impl_->aspectMode) || previewRotation()!=0);
}
QRectF QmlPlayerBridge::gameVideoRect() const { return impl_->gameVideoRect; }
void QmlPlayerBridge::attachMainWindow(qulonglong nativeHandle) {
    impl_->game.attach(reinterpret_cast<HWND>(nativeHandle));
    for (auto* window:QGuiApplication::allWindows())
        if (qulonglong(window->winId())==nativeHandle) { impl_->gameWindow=window; break; }
}
void QmlPlayerBridge::updateGameGeometry() {
    if (!gameMode() || !impl_->presentation.fullscreen || !impl_->gameWindow) return;
    RECT r=impl_->game.pictureBounds();
    if (gameScaledPreview()) {
        const HWND main = reinterpret_cast<HWND>(impl_->gameWindow->winId());
        POINT origin{};
        MONITORINFO info{sizeof(info)};
        if (!GetMonitorInfoW(impl_->gameMonitor, &info) || !ClientToScreen(main, &origin)) return;
        r=info.rcMonitor; OffsetRect(&r,-origin.x,-origin.y);
    }
    const qreal scale=impl_->gameWindow->devicePixelRatio();
    if (scale<=0) return;
    const QRectF rect(r.left/scale,r.top/scale,(r.right-r.left)/scale,(r.bottom-r.top)/scale);
    if (!rect.isEmpty() && rect!=impl_->gameVideoRect) {
        impl_->gameVideoRect=rect; emit gameModeChanged();
    }
}
void QmlPlayerBridge::setGamePanelOpen(bool open) {
    if (impl_->gamePanelOpen == open) return;
    impl_->gamePanelOpen = open;
    updateGameMode();
}
int QmlPlayerBridge::gameModeScreenIndex() const {
    if (!impl_->gameMonitor) return -1;
    const auto screens = QGuiApplication::screens();
    for (int i = 0; i < screens.size(); ++i) {
        const auto* native = screens[i]->nativeInterface<QNativeInterface::QWindowsScreen>();
        if (native && native->handle() == impl_->gameMonitor) return i;
    }
    return -1;
}
void QmlPlayerBridge::testBlockUi(int ms) {
    if (GetEnvironmentVariableW(L"VEYRA_TEST_UI_BLOCK", nullptr, 0) == 0) return;
    veyra::log::warn("game-mode", std::format("test: GUI thread blocked for {} ms", ms));
    Sleep(DWORD(std::clamp(ms, 0, 20000)));
}
// 游戏模式 state: click-through while the game-mode capture is live and the picture
// is fullscreen, the panel while the Home panel is open, otherwise a normal window.
void QmlPlayerBridge::updateGameMode() {
    using Mode = ui::GamePassthrough::Mode;
    auto& i = *impl_;
    const auto& s = i.snapshot;
    if (i.gameModeSessionId) {
        const bool same = s.sessionId == i.gameModeSessionId;
        if (same && s.running && !s.failed) i.gameModeLive = true;
        const bool ended = (same && (s.failed || s.transport == engine::TransportState::Stopping ||
            s.transport == engine::TransportState::Empty || (i.gameModeLive && !s.running))) || s.sessionId > i.gameModeSessionId;
        if (ended) {
            const bool shown = i.game.mode() != Mode::Off;
            veyra::log::info("game-mode", std::format("capture session {} ended failed={} pictureInFront={}", i.gameModeSessionId, s.failed, shown));
            i.gameModeSessionId = 0; i.gameModeLive = false;
            if (shown) {
                i.gameInputShown = i.passthroughShown = false;
                emit gameModeChanged();
                i.game.apply(Mode::Off);
                const QString why = s.failed ? uiText(s.status) : QString();
                emit gameModeStopped(why.isEmpty() ? tr("捕获已结束，已退出游戏模式") : why);
            }
        }
    }
    if (!i.presentation.fullscreen) i.gameRefused = false;
    Mode want = ui::gameInputMode(i.gameModeSessionId != 0, i.presentation.fullscreen,
                                  i.gameRefused, i.gamePanelOpen, gameScaledPreview());
    if (want!=Mode::Off && i.gameVideoRect.isEmpty()) updateGameGeometry();
    // A listener failure holds until fullscreen is left.
    if (want != Mode::Off && i.game.mode() == Mode::Off && !i.game.arm()) { i.gameRefused = true; want = Mode::Off; }
    if (want != i.game.mode()) {
        // QML binds the window's click-through flags to passthroughActive: publish
        // first, so Qt has changed the window before the foreground moves.
        const bool through = want == Mode::Through;
        const bool input = through || want == Mode::Scaled;
        if (through != i.passthroughShown || input != i.gameInputShown) {
            i.passthroughShown = through; i.gameInputShown = input; emit gameModeChanged();
        }
        if (want == Mode::Scaled && i.game.mode() != Mode::Panel)
            emit notice(tr("全屏缩放已启用：键盘和手柄控制源窗口，鼠标点击已拦截以免错位；需要鼠标穿透时切回贴合原窗口"), false);
        i.game.apply(want);
    }
    const bool mode = i.gameModeSessionId != 0;
    if ((!mode || !i.presentation.fullscreen) && !i.gameVideoRect.isEmpty()) {
        i.gameVideoRect={}; emit gameModeChanged();
    }
    if (mode != i.gameModeShown) { i.gameModeShown = mode; emit gameModeChanged(); }
}
QString QmlPlayerBridge::screenTargetId() const { return impl_->screenTarget; }
void QmlPlayerBridge::setScreenTargetId(const QString& value) {
    impl_->screenTarget = value;
    emit captureChanged();
}
QString QmlPlayerBridge::screenTargetLabel() const {
    const QString id = screenTargetId();
    if (id.isEmpty()) return tr("未选择目标");
    for (const auto& item : screenTargets()) {
        if (item.toMap().value(QStringLiteral("id")).toString() == id)
            return item.toMap().value(QStringLiteral("label")).toString();
    }
    return tr("目标已失效");
}
void QmlPlayerBridge::refreshCaptureTargets() {
    const int kind = screenOptions().value("kind").toInt();
    impl_->screenList = source::ScreenCaptureSource::targets(kind == 1 ? source::ScreenTargetKind::Monitor : source::ScreenTargetKind::Window);
    bool stillThere = false;
    for (const auto& t : impl_->screenList) stillThere = stillThere || QString::number(t.handle) == impl_->screenTarget;
    if (!stillThere) impl_->screenTarget = impl_->screenList.empty() ? QString() : QString::number(impl_->screenList.front().handle);
    emit captureChanged();
}


} // namespace veyra::ui
