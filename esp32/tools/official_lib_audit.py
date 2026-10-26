#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""official_lib_audit.py — 官方库对照审计（机器可复跑版，v3 配套）

消除"扫描全不全"疑虑的机制：
  ①文件覆盖由 find 枚举保证（不靠记忆），与 docs/审计文件清单.txt 做 diff——
    未入清单的文件会被点名；
  ②分类覆盖由 CATEGORIES 固定分类学保证（25 类"该用官方库"的检测规则，
    每类一条正则，任何人可复核/扩充）；
  ③本脚本随每次清单版本更新重跑，报告落 docs/evidence/。

用法: python official_lib_audit.py [--update-manifest]
"""
import os, re, sys, argparse

ROOTS = [
    r'esp32\components', r'esp32\main', r'esp32\tests', r'esp32\tools',
    r'project_tcs\App', r'project_tcs\Core', r'project_tcs\tests', r'project_tcs\tools',
]
EXTS = ('.c', '.h', '.py')
MANIFEST = r'docs\审计文件清单.txt'
REPORT = r'docs\evidence\官方库审计_机器报告.md'

# 25 类"该用官方库"分类学：(类名, 命中正则, 官方候选)
CATEGORIES = [
    ('CRC/校验',      r'crc|checksum|parity',            'esp_rom_crc(16/32)/硬件CRC(注:Q5禁用硬件)'),
    ('哈希/加密',      r'sha\d|md5|\baes\b|\brsa\b|mbedtls', 'mbedtls(官方)'),
    ('Base64/hex编码', r'base64|b64|from_hex|to_hex',     'mbedtls/base64'),
    ('JSON/XML',      r'cjson|json\.dump|xml\.|expat',   'cJSON(官方json组件)/expat'),
    ('位敲总线',       r'\bscl\b|\bsda\b|bit.?bang',      'HAL_I2C/i2c_master(官方)'),
    ('I2C/SPI 驱动',   r'HAL_I2C|i2c_master_|spi_',       'HAL/esp_driver_i2c/esp_driver_spi'),
    ('环形缓冲/队列',   r'ring|txq|\bfifo\b|circular',     'FreeRTOS StreamBuffer/esp_ringbuf'),
    ('RTOS 原语',      r'xTaskCreate|vTaskDelay|xQueue|Semaphore|StreamBuffer', 'FreeRTOS(官方)'),
    ('数学/DSP',      r'\bsqrt|\bpow|\bsin\b|\bcos\b|\bfft|lowpass|pid', 'esp-dsp(评估过拒绝)'),
    ('延时/时钟',      r'delay_us|delay_ms|esp_timer_get|getuptime', 'esp_timer/vTaskDelay(官方)'),
    ('字符串/格式化',   r'itoa|strtok|snprintf',           'newlib(官方 libc)'),
    ('字符编码/字模',   r'utf-?8|gbk|hzk|font_gen|glyph',  'lv_font_conv(LVGL 官方,候选F8)'),
    ('压缩/文件格式',   r'\bgz\b|deflate|inflate|\brle\b|\bbmp\b', 'miniz/EMBER(按需)'),
    ('网络协议',       r'\bsocket|lwip|httpd|websocket',   'lwip/esp_http_server/httpd_ws(官方)'),
    ('存储',          r'\bnvs|flash_write|eeprom|fatfs|sdcard', 'nvs_flash/fatfs(官方)'),
    ('OTA/分区',      r'\bota\b|rollback|partition_table', 'esp_ota/esp_partition(官方)'),
    ('控制台/shell',   r'console|repl|linenoise',         'esp_console(官方)/Embedded CLI(备选)'),
    ('触摸驱动',       r'ft5446|ft5x06|touch_read|tp_config', 'esp_lcd_touch_ft5x06(参考)/HAL I2C4(F2候选)'),
    ('显示驱动',       r'ltdc|\blcd\b|epaper|uc81|st77|dma2d', 'ST HAL LTDC/LVGL(G线)'),
    ('LED/单线',       r'ws2812|sk68|led_strip',          'espressif/led_strip(已用)'),
    ('传感器',         r'tmp117|\bds18\b|\bsht\d|adc1_get', '组件库检索:仅GPL第三方(F5拒)'),
    ('UI 框架',        r'lvgl|lv_|tcs_ui',                'LVGL v9(官方,迁移中)/TouchGFX(F13拒)'),
    ('QR 码',         r'qrcodegen|qr_encode',            'nayuki(已用,MIT)'),
    ('看门狗',         r'\bwdt\b|iwdg|wwdg',              'esp_task_wdt/ST HAL IWDG(已用)'),
    ('私有协议',       r'm207|tcs_pl_|mt_frame',          '无官方库(厂商私有,合法自研)'),
]

# 构建目录一律前缀匹配（与五桶清点 skip_re 的 build* 通配口径一致，防新增 build-xxx 变体假红）
def _is_skip_dir(name):
    return (name.startswith('build') or name in
            {'managed_components', '.git', 'node_modules', 'qr'})
# W7（v13.4 §7）：查证注释存在性核查——🟦 自研驱动近邻文件必须带"官方查证："注释。
# 缺失=黄灯（一个版本期不阻断 exit；§1c W7 门禁=黄灯数 0；新增 🟦 文件应随 SOP 同步登记本表）
VERIFY_COMMENT_FILES = (
    'esp32/components/tcs_m207/m207_service.c',
    'esp32/components/tcs_m207/m207_proto.c',
    'esp32/components/tcs_link/link_task.c',
    'esp32/components/tcs_link/comm.c',
    'esp32/components/tcs_tmp117/tmp117_service.c',
    'esp32/components/tcs_fan/fan_service.c',
    'esp32/main/tcs_console.c',
)

# qr/ 为第三方 nayuki 库（LICENSE 在库），单独标注不参与分类判定
VENDORED = ('qrcodegen.c', 'qrcodegen.h')


def iter_files():
    for root in ROOTS:
        if not os.path.isdir(root):
            continue
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames if not _is_skip_dir(d)]
            for fn in filenames:
                if fn.endswith(EXTS) and fn not in VENDORED:
                    yield os.path.join(dirpath, fn)


def main():
    # W5（v13.4 §6）：锚定仓库根——ROOTS 与五桶 os.walk('.') 都按仓库根解析，
    # 使 nightly.sh/run_all.sh 从 project_tcs 侧调用结果与仓库根直跑一致
    os.chdir(os.path.normpath(os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..', '..')))
    ap = argparse.ArgumentParser()
    ap.add_argument('--update-manifest', action='store_true')
    args = ap.parse_args()

    files = sorted(iter_files())
    files = [f.replace(chr(92), '/') for f in files]   # 桶清点统一正斜杠
    manifest_path = os.path.join(os.path.dirname(os.path.abspath(__file__)) or '.',
                                 '..', '..', MANIFEST)
    manifest_path = os.path.normpath(manifest_path)

    lines = ['# 官方库审计机器报告（自动生成，勿手改）', '',
             '生成: `python esp32/tools/official_lib_audit.py`  分类学=CATEGORIES 25 类',
             '文件枚举=os.walk 四树（esp32 components/main/tests/tools + project_tcs App/Core/tests/tools）',
             '第三方引入(qrcodegen)单独标注不参与判定。', '']

    if args.update_manifest:
        # W5 红队发现 1：覆写前保留既有 '#' SOP 注释行（读写两端对称，防刷新自毁）
        keep = []
        if os.path.isfile(manifest_path):
            with open(manifest_path, encoding='utf-8') as f:
                keep = [l.rstrip('\r\n') for l in f if l.strip().startswith('#')]
        with open(manifest_path, 'w', encoding='utf-8', newline='\n') as f:
            f.write('\n'.join(keep + files) + '\n')
        print('manifest updated: %d files (%d SOP lines kept)' % (len(files), len(keep)))

    covered = set()
    if os.path.isfile(manifest_path):
        with open(manifest_path, encoding='utf-8') as f:
            # '# '开头=SOP 注释行（W5），不算清单条目
            covered = {l.strip().replace(chr(92), '/') for l in f
                       if l.strip() and not l.strip().startswith('#')}

    missing = [f for f in files if f not in covered]
    stale = [f for f in covered if f not in files]

    lines.append('## 文件覆盖（完整性机器证明）')
    lines.append('- 枚举文件数: %d' % len(files))
    lines.append('- 清单已覆盖: %d' % len(covered & set(files)))
    lines.append('- **未入清单(缺口): %d** %s' % (len(missing), missing[:10] if missing else ''))
    lines.append('- 清单过期(文件已不存在): %d %s' % (len(stale), stale[:10] if stale else ''))
    lines.append('')

    lines.append('## 分类命中（文件 × 类别）')
    for fn in files:
        try:
            src = open(fn, encoding='utf-8', errors='replace').read()
        except OSError:
            continue
        hits = [name for name, pat, _ in CATEGORIES if re.search(pat, src, re.I)]
        if hits:
            rel = fn.replace('\\', '/')
            lines.append('- `%s`: %s' % (rel, ', '.join(hits)))

    lines.append('')
    lines.append('## 分类学（25 类 × 官方候选）')
    for name, pat, off in CATEGORIES:
        lines.append('- %s（检测 `%s`）→ 官方候选: %s' % (name, pat, off))

    # W7 黄灯：查证注释存在性（缺=黄，不阻断 exit；§1c W7 门禁=黄灯数 0）
    yellow = []
    for vf in VERIFY_COMMENT_FILES:
        try:
            with open(vf, encoding='utf-8', errors='replace') as f:
                head = f.read(4096)          # 查证注释约定在文件头注释块
        except OSError:
            yellow.append(vf + '  (文件缺失)')
            continue
        if '官方查证：' not in head:
            yellow.append(vf)
    lines.append('')
    lines.append('## 查证注释核查（W7 黄灯）')
    lines.append('- **黄灯数: %d** %s' % (
        len(yellow), yellow if yellow else '（%d 文件全带官方查证注释）' % len(VERIFY_COMMENT_FILES)))

    # ====== 全树五桶清点（每个文件必须有归属；未归类>0 → 失败）======
    import fnmatch
    skip_re = re.compile(r'/(build[^/]*|managed_components|\.git|node_modules|'
                         r'__pycache__|epaper)/')   # build* 通配所有构建变体目录
    buckets = {}
    unclassified = []
    for dirpath, dirnames, filenames in os.walk('.'):
        dirnames[:] = [d for d in dirnames if not skip_re.search('/' + d + '/')]
        for fn in filenames:
            f = os.path.join(dirpath, fn).replace(chr(92), '/')
            if f.startswith('./'):
                f = f[2:]
            if fn.endswith('.inc'):
                b = 'A 自研生成数据(字库, gen_font_cn 产物)'
            elif 'web_audit' in fn or fn.startswith('_web_'):
                b = 'C 测试产物(web 审计)'
            elif fn == 'post_build_objcopy.py':
                b = 'E 工程/构建配置'
            elif '/qr/qrcodegen' in f:
                b = 'B-vendor nayuki QR'
            elif '/mock/' in f:
                b = 'C 素材/mock'
            elif fn.endswith(('.ninja_deps', '.ninja_log', 'build.ninja', '.log',
                              '.bin', '.elf', '.hex', '.map', '.o', '.yaml')):
                b = 'C 构建产物/日志'
            elif fn.endswith(('.png', '.jpg', '.jpeg', '.mp4', '.gif')):
                b = 'D 取证媒体'
            elif f.startswith('project_tcs/Drivers/'):
                b = 'B-vendor ST HAL'
            elif f.startswith('project_tcs/third_party/'):
                b = 'B-vendor LVGL'
            elif f.startswith('docs/'):
                b = 'D 文档/证据'
            elif f in covered or f in files:
                b = 'A 自研代码(已审计)'
            elif '/web_files/' in f:
                b = 'C 前端资源(自研页面+Chart.js 官方发行版)'
            elif fn.endswith(('.csv', '.sh', '.lock', '.yml', '.json') ) or                  fn.startswith(('sdkconfig', 'Kconfig', 'partitions', 'CMakeLists', 'Makefile')) or                  fn.endswith(('.cmake', '.ld', '.s', '.cfg', '.project', '.cproject', '.launch',
                              '.ioc', '.uvprojx', '.uvoptx', '.gitignore', '.gitattributes',
                              '.editorconfig', '.prettierrc', '.flake8', '.claude.md')) or                  fn in ('AGENTS.md', 'README.md', 'LICENSE', 'LVGL_PINNED_VERSION'):
                b = 'E 工程/构建配置(CMake/Keil/Cube/脚本)'
            elif fn.endswith(('.md', '.txt')):
                b = 'D 文档/证据'
            else:
                b = 'F 未归类'
                unclassified.append(f)
            buckets[b] = buckets.get(b, 0) + 1

    lines.append('## 全树五桶清点（总账：每个文件都有归属）')
    for b in sorted(buckets):
        lines.append('- %s: %d' % (b, buckets[b]))
    lines.append('- **未归类: %d %s**' % (len(unclassified),
                 unclassified[:15] if unclassified else ''))
    lines.append('')

    out = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                        '..', '..', REPORT))
    os.makedirs(os.path.dirname(out), exist_ok=True)
    # W5 红队发现 2：临时文件+原子替换（防并发互踩产生半截报告）
    tmp_out = out + '.tmp'
    with open(tmp_out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')
    os.replace(tmp_out, out)
    print('report: %s (%d files, %d uncovered, %d unclassified)'
          % (out, len(files), len(missing), len(unclassified)))
    return 1 if (missing or unclassified) else 0


if __name__ == '__main__':
    sys.exit(main())
