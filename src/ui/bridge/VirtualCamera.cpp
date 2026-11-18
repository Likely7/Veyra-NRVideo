// Virtual camera controls; pixels and device lifetime are owned by the engine.
#include "../QmlPlayerBridge_p.h"
#include "veyra/ui/UiLanguage.h"
namespace veyra::ui {
bool QmlPlayerBridge::virtualCameraEnabled()const{return impl_->virtualCameraState.value("active").toBool();}
QVariantMap QmlPlayerBridge::virtualCameraState()const{return impl_->virtualCameraState;}
int QmlPlayerBridge::virtualCameraFps()const{
    // 0 (the default): the camera follows the rate its consumer negotiated; 30/60/120 cap it.
    const int n=impl_->prefInt("virtualCameraFps",0);return n==30||n==60||n==120?n:0;
}
void QmlPlayerBridge::setVirtualCameraEnabled(bool enabled){
    std::wstring error;
    if(impl_->engine.setVirtualCamera(enabled,uint32_t(virtualCameraFps()),error)&&enabled&&!virtualCameraEnabled())
        emit notice(tr("虚拟摄像机已启用，在直播软件中选择 Veyra Virtual Camera"),false);
    pollVirtualCamera();
}
void QmlPlayerBridge::uninstallVirtualCamera(){
    std::wstring error;
    if(!impl_->engine.uninstallVirtualCamera(error))emit notice(i18n::text(error),true);
    else if(!impl_->engine.virtualCameraSnapshot().installing)emit notice(tr("已卸载当前用户的 Veyra 虚拟摄像机"),false);
    pollVirtualCamera();
}
void QmlPlayerBridge::installVirtualCamera(){
    std::wstring error;
    if(!impl_->engine.installVirtualCamera(error))emit notice(i18n::text(error),true);
    else emit notice(tr("请在 Windows 弹窗中确认管理员安装"),false);
    pollVirtualCamera();
}
void QmlPlayerBridge::pollVirtualCamera(){
    const auto s=impl_->engine.virtualCameraSnapshot();
    QVariantMap state{{"active",s.active},{"installed",s.installed},{"failed",s.failed},
        {"systemInstalled",s.systemInstalled},{"installing",s.installing},
        {"width",s.width},{"height",s.height},{"fpsLimit",s.fpsLimit},{"consumerFps",s.consumerFps},
        {"presentedFps",s.presentedFps},{"systemOutdated",s.systemOutdated},
        {"processingWidth",s.processingWidth},{"processingHeight",s.processingHeight},
        {"frames",QVariant::fromValue(qulonglong(s.frames))},{"dropped",QVariant::fromValue(qulonglong(s.dropped))},
        {"error",s.error.empty()?QString():i18n::text(s.error)},
        {"setupError",s.setupError.empty()?QString():i18n::text(s.setupError)}};
    if(state==impl_->virtualCameraState)return;
    const auto old=impl_->virtualCameraState;impl_->virtualCameraState=state;emit virtualCameraChanged();
    if(s.failed&&!s.error.empty()&&old.value("error")!=state.value("error"))emit notice(state.value("error").toString(),true);
    if(!s.setupError.empty()&&old.value("setupError")!=state.value("setupError"))emit notice(state.value("setupError").toString(),true);
    if(old.value("installing").toBool()&&!s.installing&&s.setupError.empty())
        emit notice(s.systemInstalled?tr("摄像机组件已安装，请刷新或重新打开直播软件"):tr("虚拟摄像机设备已卸载"),false);
}
}
