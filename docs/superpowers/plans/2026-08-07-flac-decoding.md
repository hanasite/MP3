# FLAC 流式解码 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 HTTP 流式播放链路增加 FLAC 支持（16/24bit），复用 es_parser 模式 + eos 收尾。

**Architecture:** `api_stream` 识别分支加 `fLaC` 与 `fmt=flac`，路由到新函数 `stream_flac`。FLAC 用 simple_dec **parser 模式**（`use_frame_dec=false`，官方测试用法），数据直接喂 `esp_audio_simple_dec_process`，流结束时 `eos=true` 刷新尾部缓存。I2S/音量/日志复用现有机制。

**Tech Stack:** ESP-IDF 5.5.3 + esp_audio_codec 2.6.1（预编译，FLAC 解码器 `CONFIG_AUDIO_DECODER_FLAC_SUPPORT=y`）

## Global Constraints

- 规格：`docs/superpowers/specs/2026-08-07-flac-decoding-design.md`
- 只改 `F:\ESP32idf\MP3_project\s3_MP3\main\http_api.c`（不新建文件，不动其他文件）
- FLAC 用 parser 模式（`use_frame_dec=false`），**不写自建帧同步**
- 流结束必须 `eos=true` 再 process 一次（FLAC parser 需要刷新缓存）
- 16bit 和 24bit 都要支持；I2S 位深按 `get_info` 配置（`i2s_ensure_config` 已支持 16/24/32）
- 构建：ESP-IDF 终端（VSCode 底部）`idf.py build`；烧录 `idf.py -p COMx flash monitor`（COMx 以设备管理器为准）
- 测试文件：组件自带 `s3_MP3/components/esp_audio_codec/test_apps/audio_codec_test/main/test.flac`（1.1MB）；24bit 文件下载自 `ietf-wg-cellar/flac-test-files`（GitHub 慢则用 gh-proxy 技能）
- git：`F:\ESP32idf\MP3_project` 当前**无 git 仓库**（Task 0 初始化本地仓库；计划/规格文档提交到远程 `hanasite/MP3` 的 docs/ 目录）

---

### Task 0: 初始化本地 git 仓库

**Files:**
- Create: `F:\ESP32idf\MP3_project\.gitignore`
- (git init 于 `F:\ESP32idf\MP3_project`)

- [ ] **Step 1: git init + .gitignore**

```bash
cd /f/ESP32idf/MP3_project
git init
```

创建 `.gitignore`：

```gitignore
build/
i2s_std/
.vscode/
*.log
```

- [ ] **Step 2: 首次提交**

```bash
git add -A
git commit -m "chore: 初始化本地仓库（MP3 播放器固件 + 文档）
Co-Authored-By: Claude <noreply@anthropic.com>"
```

Expected: 提交成功（含 s3_MP3/main、docs/、MUSIC_example/ 等）

---

### Task 1: FLAC 识别 + 解码器 + stream_flac（16bit 先通）

**Files:**
- Modify: `F:\ESP32idf\MP3_project\s3_MP3\main\http_api.c`
  - `api_stream` 识别分支（第 586-604 行附近）
  - 在 `stream_mp3` 之后新增 `flac_decoder_init` + `stream_flac`

**Interfaces:**
- Consumes: `i2s_ensure_config(tx_chan, rate, bits, ch)`（现有）、`send_err`/`send_ok`（现有）、`s_volume`（现有）
- Produces: `stream_flac(httpd_req_t *req, i2s_chan_handle_t *tx_chan, uint8_t *pre_buf, int pre_len, int remaining)` —— Task 2 继续扩展此函数

- [ ] **Step 1: 修改格式识别（api_stream）**

把 `api_stream` 中这段（现有第 586-604 行）：

```c
    // 格式识别：RIFF=WAV，ID3 标签或 0xFF 帧同步=MP3，未知按 fmt 参数兜底
    bool is_mp3 = false;
    if (got >= 4) {
        if (memcmp(pre_buf, "ID3", 3) == 0 ||
            (pre_buf[0] == 0xFF && (pre_buf[1] & 0xE0) == 0xE0)) {
            is_mp3 = true;
        } else if (memcmp(pre_buf, "RIFF", 4) != 0) {
            char fmt[8] = "wav";
            get_query_param(req, "fmt", fmt, sizeof(fmt));
            is_mp3 = (strcmp(fmt, "mp3") == 0);
        }
    }
    ESP_LOGI(TAG, "stream: 识别为 %s，剩余 %d 字节", is_mp3 ? "MP3" : "WAV", remaining);

    if (is_mp3) {
        return stream_mp3(req, &tx_chan, pre_buf, got, remaining);
    }
    return stream_wav(req, tx_chan, pre_buf, got, remaining);
```

替换为：

```c
    // 格式识别：RIFF=WAV，ID3 标签或 0xFF 帧同步=MP3，fLaC 头=FLAC，未知按 fmt 参数兜底
    bool is_mp3 = false, is_flac = false;
    if (got >= 4) {
        if (memcmp(pre_buf, "ID3", 3) == 0 ||
            (pre_buf[0] == 0xFF && (pre_buf[1] & 0xE0) == 0xE0)) {
            is_mp3 = true;
        } else if (memcmp(pre_buf, "fLaC", 4) == 0) {
            is_flac = true;
        } else if (memcmp(pre_buf, "RIFF", 4) != 0) {
            char fmt[8] = "wav";
            get_query_param(req, "fmt", fmt, sizeof(fmt));
            if (strcmp(fmt, "mp3") == 0) is_mp3 = true;
            else if (strcmp(fmt, "flac") == 0) is_flac = true;
        }
    }
    ESP_LOGI(TAG, "stream: 识别为 %s，剩余 %d 字节",
             is_mp3 ? "MP3" : (is_flac ? "FLAC" : "WAV"), remaining);

    if (is_flac) {
        return stream_flac(req, &tx_chan, pre_buf, got, remaining);
    }
    if (is_mp3) {
        return stream_mp3(req, &tx_chan, pre_buf, got, remaining);
    }
    return stream_wav(req, tx_chan, pre_buf, got, remaining);
```

- [ ] **Step 2: 新增 flac_decoder_init + stream_flac**

在 `stream_mp3` 函数结束（第 556 行 `}` 之后）插入：

```c
/* ---------- FLAC 解码播放（simple_dec parser 模式 + eos 收尾） ---------- */

static esp_audio_simple_dec_handle_t s_flac_dec = NULL;

static esp_err_t flac_decoder_init(void)
{
    if (s_flac_dec) {
        return ESP_OK;
    }
    // 注册全部启用的解码器（含 FLAC）。重复调用无害（注册表幂等）
    esp_audio_dec_register_default();

    esp_audio_simple_dec_cfg_t cfg = {
        .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC,
        .use_frame_dec = false,  // parser 模式：任意大小输入，内部帧同步
                                 // （FLAC 同步码 14 位+帧头 CRC-8，官方测试即此用法）
    };
    if (esp_audio_simple_dec_open(&cfg, &s_flac_dec) != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "FLAC 解码器打开失败");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "FLAC 解码器就绪");
    return ESP_OK;
}

/* POST /api/stream（FLAC 路径）：parser 模式直接喂数据，流结束 eos 刷新缓存 */
static esp_err_t stream_flac(httpd_req_t *req, i2s_chan_handle_t *tx_chan,
                             uint8_t *pre_buf, int pre_len, int remaining)
{
    if (flac_decoder_init() != ESP_OK) {
        return send_err(req, "500 Internal Server Error", "decoder init failed");
    }

    static uint8_t in_buf[512];
    static int16_t pcm_buf[8192];       // 解码输出缓冲（static 防任务栈溢出）
    static bool s_i2s_configured = false;
    static uint32_t s_rate = 44100;
    static uint8_t s_bits = 16;
    static uint8_t s_ch = 2;
    size_t total_written = 0;
    size_t total_recv = pre_len;
    uint32_t err_cnt = 0;

    int64_t last_report = esp_timer_get_time();
    ESP_LOGI(TAG, "stream: 开始 FLAC 解码播放，剩余 %d 字节", remaining);

    bool first_block = true;
    int eos_calls = 0;
    while (1) {
        int r = 0;
        bool eos = false;
        if (first_block) {
            first_block = false;
            r = pre_len;
            memcpy(in_buf, pre_buf, pre_len);
        } else if (remaining > 0) {
            int want = remaining < (int)sizeof(in_buf) ? remaining : (int)sizeof(in_buf);
            r = httpd_req_recv(req, (char *)in_buf, want);
            if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
            if (r <= 0) break;  // 连接断：直接退出
            remaining -= r;
            total_recv += r;
        } else {
            eos = true;  // 数据收完：eos 刷新 parser/解码器缓存的尾部数据
        }

        esp_audio_simple_dec_raw_t raw = {
            .buffer = in_buf,
            .len = r,
            .eos = eos,
        };
        esp_audio_simple_dec_out_t frame = {
            .buffer = (uint8_t *)pcm_buf,
            .len = sizeof(pcm_buf),
        };
        esp_audio_err_t dec_ret = esp_audio_simple_dec_process(s_flac_dec, &raw, &frame);
        if (dec_ret != ESP_AUDIO_ERR_OK && dec_ret != ESP_AUDIO_ERR_BUFF_NOT_ENOUGH) {
            err_cnt++;
            if (err_cnt < 10) ESP_LOGW(TAG, "FLAC 解码错误 %d", dec_ret);
        }

        if (frame.decoded_size > 0) {
            // 首次解出 PCM：按音频参数配置 I2S
            esp_audio_simple_dec_info_t info;
            if (!s_i2s_configured &&
                esp_audio_simple_dec_get_info(s_flac_dec, &info) == ESP_AUDIO_ERR_OK) {
                if (i2s_ensure_config(tx_chan, info.sample_rate,
                                      info.bits_per_sample, info.channel) == ESP_OK) {
                    ESP_LOGI(TAG, "I2S 配置: %luHz / %u bit / %u 声道",
                             (unsigned long)info.sample_rate, info.bits_per_sample, info.channel);
                }
                s_i2s_configured = true;
                s_rate = info.sample_rate;
                s_bits = info.bits_per_sample;
                s_ch = info.channel;
            }

            // 数字音量（16bit 采样）
            if (s_volume != 100) {
                int ns = frame.decoded_size / 2;
                float gain = s_volume / 100.0f;
                for (int i = 0; i < ns; i++) {
                    pcm_buf[i] = (int16_t)(pcm_buf[i] * gain);
                }
            }

            if (total_written == 0) {
                int16_t *p = pcm_buf;
                ESP_LOGI(TAG, "首帧PCM: %d %d %d %d %d %d %d %d (decoded=%d)",
                         p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
                         frame.decoded_size);
            }

            size_t written = 0;
            i2s_channel_write(*tx_chan, pcm_buf, frame.decoded_size, &written, portMAX_DELAY);
            total_written += written;

            int64_t now = esp_timer_get_time();
            if (now - last_report >= 10 * 1000000LL) {
                last_report = now;
                double secs = (double)total_written / ((double)s_rate * s_ch * (s_bits / 8.0));
                ESP_LOGI(TAG, "播放进度: %.1f 秒（%d 字节） 堆: %d 错帧:%lu",
                         secs, (int)total_written, (int)esp_get_free_heap_size(),
                         (unsigned long)err_cnt);
            }
        }

        if (eos) {
            if (frame.decoded_size == 0) break;  // 缓存刷新完
            if (++eos_calls > 8) break;          // 保险：最多刷新 8 次
        }
    }

    ESP_LOGI(TAG, "stream: FLAC 播放结束：接收 %d / %d 字节，解码写入 %d 字节 PCM",
             (int)total_recv, req->content_len, (int)total_written);
    return send_ok(req);
}
```

- [ ] **Step 3: 编译**

ESP-IDF 终端（VSCode 底部）：

```bash
cd F:\ESP32idf\MP3_project\s3_MP3
idf.py build
```

Expected: 编译通过（无 error；若报 FLAC 相关 undefined，检查 `CONFIG_AUDIO_DECODER_FLAC_SUPPORT=y` 在 sdkconfig 中）

- [ ] **Step 4: 烧录并实测 test.flac**

```bash
idf.py -p COMx flash monitor
```

Expected: 启动日志与之前一致（SD 卡挂载失败正常，HTTP 照常）。

另开终端播放（IP 为板子串口日志里的地址）：

```bash
curl -X POST --data-binary "@F:\ESP32idf\MP3_project\s3_MP3\components\esp_audio_codec\test_apps\audio_codec_test\main\test.flac" "http://<IP>/api/stream?fmt=flac"
```

Expected 串口日志：
- `stream: 识别为 FLAC`
- `FLAC 解码器就绪`（首次）
- `I2S 配置: <rate>Hz / <bits> bit / <n> 声道`
- `首帧PCM: ... (decoded=<N>)` N>0
- `播放进度: x.x 秒` 递增，**错帧:0**（若有错帧>0 记下来，这是 es_parser 同步问题的信号）
- 功放/耳机能听到音乐

- [ ] **Step 5: Commit**

```bash
cd /f/ESP32idf/MP3_project
git add s3_MP3/main/http_api.c
git commit -m "feat: FLAC 流式解码（parser 模式 + eos 收尾，16bit 验证通过）
Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 2: 24bit 支持（int32 容器 + 音量 + I2S）+ 容器格式验证

**Files:**
- Modify: `F:\ESP32idf\MP3_project\s3_MP3\main\http_api.c`（`stream_flac` 内）

**Interfaces:**
- Consumes: Task 1 的 `stream_flac`
- Produces: 24bit FLAC 全链路可播；首帧日志确认解码器输出容器格式

- [ ] **Step 1: pcm_buf 升级为 int32 + 24bit 分支**

`stream_flac` 中做三处修改：

(1) 缓冲声明改为：

```c
    static int32_t pcm_buf[8192];       // 32KB：容纳 24bit（int32 容器）输出
```

(2) 音量段（原 16bit 代码）替换为：

```c
            // 数字音量（16bit 用 int16 缩放，24bit 用 int32 缩放）
            if (s_volume != 100) {
                float gain = s_volume / 100.0f;
                if (s_bits == 24) {
                    int ns = frame.decoded_size / 4;  // 24bit 输出为 int32 容器（左对齐假设，Step 4 验证）
                    for (int i = 0; i < ns; i++) {
                        pcm_buf[i] = (int32_t)(pcm_buf[i] * gain);  // |v|≤2^23，gain≤1，不溢出
                    }
                } else {
                    int16_t *p16 = (int16_t *)pcm_buf;
                    int ns = frame.decoded_size / 2;
                    for (int i = 0; i < ns; i++) {
                        p16[i] = (int16_t)(p16[i] * gain);
                    }
                }
            }
```

(3) 首帧 PCM 日志段替换为（按位深打印，验证容器格式）：

```c
            if (total_written == 0) {
                if (s_bits == 24) {
                    int32_t *p = pcm_buf;
                    ESP_LOGI(TAG, "首帧PCM(24bit): %d %d %d %d %d %d %d %d (decoded=%d)",
                             p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
                             frame.decoded_size);
                } else {
                    int16_t *p = (int16_t *)pcm_buf;
                    ESP_LOGI(TAG, "首帧PCM: %d %d %d %d %d %d %d %d (decoded=%d)",
                             p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7],
                             frame.decoded_size);
                }
            }
```

- [ ] **Step 2: 编译**

```bash
idf.py build
```

Expected: 编译通过

- [ ] **Step 3: 准备 24bit 测试文件**

下载 flac-test-files 仓库的 24bit 测试文件到 MUSIC_example（GitHub 慢则用 gh-proxy 技能加速）：

```bash
curl -L -o /f/ESP32idf/MP3_project/MUSIC_example/test24.flac "https://raw.githubusercontent.com/ietf-wg-cellar/flac-test-files/master/subset/24-bit/00000005.flac"
ls -la /f/ESP32idf/MP3_project/MUSIC_example/test24.flac
```

Expected: 文件存在且 >10KB。（若 URL 404，浏览器打开 https://github.com/ietf-wg-cellar/flac-test-files 在 `subset/24-bit/` 目录挑一个 flac 下载。）

- [ ] **Step 4: 烧录实测 24bit 文件 + 判定容器格式**

```bash
idf.py -p COMx flash monitor
curl -X POST --data-binary "@F:\ESP32idf\MP3_project\MUSIC_example\test24.flac" "http://<IP>/api/stream?fmt=flac"
```

Expected 串口日志：`I2S 配置: ... / 24 bit / ...` + `首帧PCM(24bit): v1..v8`

**按首帧 PCM 值判定输出容器格式（关键验证点）：**

| 现象 | 结论 | 处理 |
|---|---|---|
| 值在 ±8388608 量级，且所有值 `% 256 == 0` | int32 **左对齐** 24bit（假设成立） | 无需改动，直接可用 |
| 值很小（< ±32768），低位有随机 bit | int32 **右对齐** | 音量循环加 `pcm_buf[i] <<= 8` 后再缩放 |
| 解码器报错或 I2S 出声异常但 decoded_size>0 | 其他格式 | 打印 decoded_size 与各字节（`(uint8_t*)pcm_buf` 前 12 字节）发回分析 |

若为右对齐：音量循环改为

```c
                        pcm_buf[i] = (int32_t)(pcm_buf[i] * gain) << 8;
```

（I2S 24bit 模式要求左对齐数据。）

- [ ] **Step 5: 回归测试（MP3/WAV 不坏）**

```bash
curl -X POST --data-binary "@F:\ESP32idf\MP3_project\MUSIC_example\阿桑 - 一直很安静.wav" "http://<IP>/api/stream"
curl -X POST --data-binary "@F:\ESP32idf\MP3_project\MUSIC_example\泠鸢yousa - 勾指起誓.mp3" "http://<IP>/api/stream"
```

Expected: WAV 识别 WAV 正常播；MP3 识别 MP3 正常播（不受 FLAC 改动影响）

- [ ] **Step 6: Commit**

```bash
cd /f/ESP32idf/MP3_project
git add s3_MP3/main/http_api.c MUSIC_example/test24.flac
git commit -m "feat: FLAC 24bit 支持（int32 容器 + 音量缩放 + I2S 24bit）
Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 3: 计划与规格同步远程仓库

**Files:**
- Copy: 计划/规格到 hanasite/MP3 clone 的 `docs/superpowers/plans|specs/`

- [ ] **Step 1: 同步文档到远程**

```bash
# 假设 clone 位于 /tmp/mp3repo（已包含 specs/2026-08-07-flac-decoding-design.md）
cp "/f/ESP32idf/MP3_project/docs/superpowers/plans/2026-08-07-flac-decoding.md" /tmp/mp3repo/docs/superpowers/plans/
cd /tmp/mp3repo
git add docs/superpowers/plans/2026-08-07-flac-decoding.md
git commit -m "docs: FLAC 流式解码实施计划
Co-Authored-By: Claude <noreply@anthropic.com>"
git push
```

Expected: push 成功
