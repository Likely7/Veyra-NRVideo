// Effect registry mock — mirrors current Veyra settings (EnhancementSettings / NrSettings / VideoHdrSettings / ColorSettings / FG).
// In the real build this comes from the C++ EffectRegistry (plan §6.1). `max` = how many instances a node graph may hold.
window.VY_REG = {
  categories: [
    { id: 'quality', name: '画质' },
    { id: 'color', name: '色彩' },
    { id: 'hdr', name: 'HDR' },
    { id: 'motion', name: '补帧' },
    { id: 'util', name: '工具' }
  ],
  types: {
    sr: {
      name: '超分辨率', cat: 'quality', icon: 'sparkles', hue: '#4F7BFF', max: 1,
      summary: p => `${p.algo} · ${p.target}${p.algo === 'RTX 视频超分' ? ' · 质量 ' + p.q : ''}`,
      params: [
        { k: 'algo', label: '算法', type: 'select', opts: ['RTX 视频超分', 'DLSS 超分', 'FSR 3.1 超分'], def: 'RTX 视频超分', rebuild: true },
        { k: 'target', label: '目标尺寸', type: 'seg', opts: ['2K', '4K', '8K'], def: '4K', rebuild: true },
        { k: 'q', label: '质量', type: 'seg', opts: ['1', '2', '3', '4'], def: '3', when: p => p.algo === 'RTX 视频超分' },
        { k: 'sharp', label: '锐度', type: 'slider', min: 0, max: 1, step: .01, def: .2, when: p => p.algo === 'DLSS 超分' }
      ]
    },
    nr: {
      name: 'NR 画面增强', cat: 'quality', icon: 'wand', hue: '#FF8A3D', exp: true, max: 4,
      summary: p => `${p.runtime} · 强度 ${(+p.intensity).toFixed(2)} · ${p.size}`,
      params: [
        { k: 'runtime', label: '运行版本', type: 'select', opts: ['NVIDIA 原版', '社区兼容版', 'RTX 30 兼容'], def: 'NVIDIA 原版', rebuild: true },
        { k: 'size', label: '处理尺寸', type: 'seg', opts: ['实时', '原生'], def: '实时', rebuild: true },
        { k: 'intensity', label: '模型强度', type: 'slider', min: 0, max: 1, step: .01, def: 1 },
        { k: 'tone', label: '局部明暗', type: 'slider', min: 0, max: 1, step: .01, def: 1, group: '模型参数' },
        { k: 'structure', label: '局部结构', type: 'slider', min: 0, max: 1, step: .01, def: 1, group: '模型参数' },
        { k: 'skin', label: '肤质 · 未证实', type: 'slider', min: -1, max: 1, step: .01, def: -1, group: '模型参数' },
        { k: 'style', label: '风格 · 实验', type: 'seg', opts: ['0', '1', '2'], def: '0', group: '模型参数' },
        { k: 'autoMask', label: '自动遮罩 · 实验', type: 'toggle', def: false, group: '模型参数' },
        { k: 'uiFix', label: 'UI 修正 · 未证实', type: 'toggle', def: false, group: '模型参数' },
        { k: 'total', label: '总变化强度', type: 'slider', min: 0, max: 2, step: .01, def: 1, group: '增强变化量' },
        { k: 'darken', label: '暗化变化', type: 'slider', min: 0, max: 2, step: .01, def: 1, group: '增强变化量' },
        { k: 'brighten', label: '亮化变化', type: 'slider', min: 0, max: 2, step: .01, def: 1, group: '增强变化量' },
        { k: 'rColor', label: '色彩变化', type: 'slider', min: 0, max: 2, step: .01, def: 1, group: '增强变化量' },
        { k: 'rLuma', label: '明度变化', type: 'slider', min: 0, max: 2, step: .01, def: 1, group: '增强变化量' },
        { k: 'temporal', label: '时间域防闪烁', type: 'toggle', def: false, group: '实验' },
        { k: 'lowLatency', label: '低延迟模式（先 NR 再超分）', type: 'toggle', def: false, group: '实验', when: (p, idx) => true }
      ]
    },
    protect: {
      name: 'NR 保护区域', cat: 'util', icon: 'scan', hue: '#8A8A96', max: 1,
      summary: p => `${p.count} / 4 个区域 · 羽化 ${p.feather}px`,
      params: [
        { k: 'count', label: '已选区域', type: 'readout', def: 1 },
        { k: 'feather', label: '羽化', type: 'slider', min: 0, max: 32, step: 1, def: 8 }
      ]
    },
    color: {
      name: '调色', cat: 'color', icon: 'palette', hue: '#E0C341', max: 6,
      summary: p => `曝光 ${(+p.exposure).toFixed(2)} · 对比 ${Math.round(p.contrast)} · ${p.lut}`,
      params: [
        { k: 'exposure', label: '曝光（EV）', type: 'slider', min: -5, max: 5, step: .05, def: 0, center: true, group: '亮' },
        { k: 'contrast', label: '对比度', type: 'slider', min: -100, max: 100, step: 1, def: 0, center: true, group: '亮' },
        { k: 'highlights', label: '高光', type: 'slider', min: -100, max: 100, step: 1, def: 0, center: true, group: '亮' },
        { k: 'shadows', label: '阴影', type: 'slider', min: -100, max: 100, step: 1, def: 0, center: true, group: '亮' },
        { k: 'whites', label: '白色', type: 'slider', min: -100, max: 100, step: 1, def: 0, center: true, group: '亮' },
        { k: 'blacks', label: '黑色', type: 'slider', min: -100, max: 100, step: 1, def: 0, center: true, group: '亮' },
        { k: 'temperature', label: '色温（相对）', type: 'slider', min: -100, max: 100, step: 1, def: 0, center: true, group: '颜色', grad: 'linear-gradient(90deg,#4F8BFF,#ddd,#FFB547)' },
        { k: 'tint', label: '色调', type: 'slider', min: -100, max: 100, step: 1, def: 0, center: true, group: '颜色', grad: 'linear-gradient(90deg,#4CD964,#ddd,#FF5AC8)' },
        { k: 'vibrance', label: '自然饱和度', type: 'slider', min: -100, max: 100, step: 1, def: 0, center: true, group: '颜色' },
        { k: 'saturation', label: '饱和度', type: 'slider', min: -100, max: 100, step: 1, def: 0, center: true, group: '颜色' },
        { k: 'curve', label: '曲线', type: 'curve', def: null, group: '曲线' },
        { k: 'mixer', label: '混色器', type: 'mixer', def: null, group: '混色器' },
        { k: 'grading', label: '颜色分级', type: 'wheels', def: null, group: '颜色分级' },
        { k: 'gBlend', label: '混合', type: 'slider', min: 0, max: 100, step: 1, def: 50, group: '颜色分级' },
        { k: 'gBalance', label: '平衡', type: 'slider', min: -100, max: 100, step: 1, def: 0, center: true, group: '颜色分级' },
        { k: 'calShadow', label: '阴影色调', type: 'slider', min: -100, max: 100, step: 1, def: 0, center: true, group: '校准' },
        { k: 'calRH', label: '红原色 · 色相', type: 'slider', min: -100, max: 100, step: 1, def: 0, center: true, group: '校准' },
        { k: 'calRS', label: '红原色 · 饱和度', type: 'slider', min: -100, max: 100, step: 1, def: 0, center: true, group: '校准' },
        { k: 'calGH', label: '绿原色 · 色相', type: 'slider', min: -100, max: 100, step: 1, def: 0, center: true, group: '校准' },
        { k: 'calGS', label: '绿原色 · 饱和度', type: 'slider', min: -100, max: 100, step: 1, def: 0, center: true, group: '校准' },
        { k: 'calBH', label: '蓝原色 · 色相', type: 'slider', min: -100, max: 100, step: 1, def: 0, center: true, group: '校准' },
        { k: 'calBS', label: '蓝原色 · 饱和度', type: 'slider', min: -100, max: 100, step: 1, def: 0, center: true, group: '校准' },
        { k: 'lut', label: '3D LUT', type: 'select', opts: ['不使用 LUT', 'Kodak 2383.cube', 'Teal & Orange.cube'], def: '不使用 LUT', group: 'LUT' },
        { k: 'lutSpace', label: '输入空间', type: 'seg', opts: ['Cineon', 'sRGB', 'PQ'], def: 'Cineon', group: 'LUT' },
        { k: 'lutStrength', label: 'LUT 强度', type: 'slider', min: 0, max: 100, step: 1, def: 100, group: 'LUT' }
      ]
    },
    vhdr: {
      name: 'RTX Video HDR', cat: 'hdr', icon: 'sun', hue: '#FFB547', max: 1, outHdr: true,
      summary: p => `峰值 ${p.peak} nits · 中灰 ${p.mid}`,
      params: [
        { k: 'peak', label: '峰值亮度', type: 'slider', min: 400, max: 2000, step: 10, def: 1000 },
        { k: 'mid', label: '中间灰', type: 'slider', min: 10, max: 100, step: 1, def: 50 },
        { k: 'contrast', label: '对比', type: 'slider', min: 0, max: 200, step: 1, def: 100 },
        { k: 'sat', label: '饱和', type: 'slider', min: 0, max: 200, step: 1, def: 100 }
      ]
    },
    fg: {
      name: '补帧', cat: 'motion', icon: 'layers', hue: '#3DDC84', max: 1, last: true,
      summary: p => `${p.method.replace(' 帧生成', '').replace(' · 实验', '')} · ${p.mult}${p.strict ? ' · 严格节奏' : ''} · 上限 ${p.cap === '自定义' ? p.capFps + ' fps' : p.cap}`,
      params: [
        { k: 'method', label: '补帧方式', type: 'select', opts: ['DLSS 帧生成', 'Intel XeSS · 实验'], def: 'DLSS 帧生成', rebuild: true },
        { k: 'mult', label: '倍率', type: 'seg', opts: p => p.method === 'DLSS 帧生成' ? ['2X', '3X', '4X', '6X'] : ['2X', '3X', '4X'], def: '2X', rebuild: true },
        { k: 'strict', label: '严格补帧节奏', type: 'toggle', def: false, sub: '帧同步 · 默认关闭' },
        { k: 'flow', label: '光流来源', type: 'select', opts: ['NVIDIA NVOF', 'AMD FidelityFX · 实验', 'GPU DIS · FAST 实验'], def: 'NVIDIA NVOF', rebuild: true, group: '光流 · 运动估算' },
        { k: 'amdHalf', label: 'AMD 性能档 · 宽高减半', type: 'toggle', def: false, group: '光流 · 运动估算', when: p => p.flow.startsWith('AMD') },
        { k: 'flowQ', label: '运动估算质量', type: 'seg', opts: ['性能', '平衡', '质量'], def: '平衡', group: '光流 · 运动估算' },
        { k: 'cadence', label: '内容节奏', type: 'select', opts: ['采用源时间戳', '自动识别内容节奏', '识别 30fps 内容节奏', '识别 50fps 内容节奏', '识别 60fps 内容节奏', '采集 60→30fps 处理（PS5 30 帧）'], def: '采用源时间戳', group: '光流 · 运动估算' },
        { k: 'lowQueue', label: '低延迟队列', type: 'toggle', def: false, group: '显示同步与输出上限', sub: '减少排队；本机无法验收' },
        { k: 'vsync', label: '显示同步', type: 'seg', opts: ['允许撕裂', '垂直同步', '自动'], def: '自动', group: '显示同步与输出上限' },
        { k: 'cap', label: '输出上限', type: 'seg', opts: ['关闭', '跟随显示器', '自定义'], def: '跟随显示器', group: '显示同步与输出上限' },
        { k: 'capFps', label: '自定义上限', type: 'number', unit: 'FPS', def: '141.000', group: '显示同步与输出上限', when: p => p.cap === '自定义' }
      ]
    }
  },
  fgModes: [
    { id: 'off', name: '关', note: '' },
    { id: '2X', name: '2X', note: 'DLSS 帧生成' },
    { id: '3X', name: '3X', note: 'DLSS 多帧' },
    { id: '4X', name: '4X', note: 'DLSS 多帧' },
    { id: '6X', name: '6X', note: '仅 DLSS · RTX 50 · 实验', exp: true }
  ],
  // Presets are saved in Pro mode; the minimal-mode "预设" pill only picks one.
  presets: [
    { id: 'off', name: '原画', note: '所有增强关闭', kind: 'list', builtin: true },
    { id: 'smooth', name: '流畅', note: '超分 2K · 补帧 2X', kind: 'list', builtin: true },
    { id: 'balanced', name: '均衡', note: '超分 4K · NR 1 层 · 补帧 2X', kind: 'list', builtin: true },
    { id: 'max', name: '极致', note: '超分 4K · NR 2 层 · HDR · 补帧 2X', kind: 'list', builtin: true },
    { id: 'night', name: '夜间游戏', note: '9月24日保存 · 超分 4K · NR 1 层', kind: 'list' },
    { id: 'movie', name: '4K 电影', note: '9月20日保存 · 超分 4K · 调色', kind: 'list' },
    { id: 'n-grade', name: '首尾双调色', note: '调色 → 超分 → NR×2 → HDR → 调色 → 补帧', kind: 'node' },
    { id: 'n-nrfirst', name: 'NR 先行', note: 'NR → 超分 → 补帧', kind: 'node' }
  ]
};

(function () {
  const mk = (type, over = {}, extra = {}) => {
    const t = VY_REG.types[type]; const p = {};
    t.params.forEach(x => p[x.k] = x.def);
    Object.assign(p, over);
    return Object.assign({ id: type + '-' + Math.random().toString(36).slice(2, 7), type, on: true, p }, extra);
  };
  window.VY_MK = mk;
  // List view: the fixed product order (SR → NR layers → protect → HDR → FG).
  window.VY_DEFAULT_CHAIN = () => [
    mk('sr', { target: '4K', q: '3' }),
    mk('nr', { intensity: .8 }),
    mk('nr', { intensity: .35, style: '1' }),
    mk('protect', { count: 1 }),
    mk('vhdr', {}),
    mk('fg', {})
  ];
})();
