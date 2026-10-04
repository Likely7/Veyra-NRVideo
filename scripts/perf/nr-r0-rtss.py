"""Reuse installed RTSS API/OSD harness for three ordinary cases, two profiles.
No GPU competitor, VRAM pressure, high multiplier sweep or user profile left changed.
"""
from pathlib import Path
import importlib.util,json,shutil,sys
ROOT=Path(__file__).resolve().parents[2];BASE=Path('E:/项目/Veyra');TASK='perf-nr-20261004'
spec=importlib.util.spec_from_file_location('matrix',ROOT/'scripts/perf/nr-matrix.py');matrix=importlib.util.module_from_spec(spec);spec.loader.exec_module(matrix)
variant,label=sys.argv[1:3];matrix.assert_gpu_tests_idle();app=BASE/'tests'/TASK/(label+'-app')
shutil.copytree(matrix.PACKAGE,app,copy_function=matrix.copy_dependency)
shutil.copy2(BASE/'build'/TASK/variant/'veyra_qml_ui.exe',app/'veyra_qml_ui.exe');shutil.copytree(ROOT/'qml',app/'qml',dirs_exist_ok=True)
fixture=(ROOT/'scripts/acceptance/rtss-restart-stress.qml').read_text(encoding='utf-8').replace('RTSS_STRESS_','RTSS_NORMAL_')
(app/'qml/Veyra/RtssNormalProbe.qml').write_text(fixture,encoding='utf-8')
library=ROOT/'scripts/acceptance/rtss-restart-real.py';code=library.read_text(encoding='utf-8')
patches={
 "TASK='rtss-restart-loop-20261004'":"TASK='perf-nr-20261004'",
 "APP=BASE/'test-packages'/TASK/'Veyra-2.0.3-rtssfix-NVIDIA-win64-portable'":'APP=Path('+repr(str(app))+')',
 'expected_cases=1 if transport_only else 11':'expected_cases=3',
 "media=BASE/'tests'/TASK/'rtss-stress-2k30.mp4'":"media=BASE/'tests/rtss-restart-loop-20261004/rtss-stress-2k30.mp4'",
 "shutil.copy2(BASE/'build'/TASK/'veyra_qml_ui.exe',APP/'veyra_qml_ui.exe')":"shutil.copy2(BASE/'build'/TASK/"+repr(variant)+"/'veyra_qml_ui.exe',APP/'veyra_qml_ui.exe')",
 "(ROOT/'scripts/acceptance/rtss-restart-stress.qml').as_uri()":"(APP/'qml/Veyra/RtssNormalProbe.qml').as_uri()",
 "if transport_only:profiles=profiles[:1]":"profiles=profiles[:2]",
 "if transport_only:loader=loader.replace('item.media=','item.cases=[item.cases[10]];item.media=',1)":"loader=loader.replace('item.media=','item.cases=[item.cases[0],item.cases[1],item.cases[10]];item.media=',1)"
}
for old,new in patches.items():assert code.count(old)==1,old;code=code.replace(old,new)
code=code.replace('RTSS_STRESS_','RTSS_NORMAL_')
assert (BASE/'tests/rtss-restart-loop-20261004/rtss-stress-2k30.mp4').is_file(),'Reuse existing ordinary media; do not generate load'
identity={'library':str(library),'librarySha256':matrix.digest(library),'exeSha256':matrix.digest(app/'veyra_qml_ui.exe'),'rtssProfileCases':2,'ordinaryCases':['plain','nr-parameters','nr-resize-pause-seek'],'extraGpuLoad':False}
sys.argv=[str(library),label]
exec(compile(code,str(library),'exec'),{'__file__':str(library),'__name__':'__main__'})
folder=BASE/'logs'/TASK/label
(folder/'harness-identity.json').write_text(json.dumps(identity,ensure_ascii=False,indent=2),encoding='utf-8');matrix.assert_gpu_tests_idle();print('R0_RTSS_ORDINARY_COMPLETE',folder,flush=True)
