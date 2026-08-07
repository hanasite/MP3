# FLAC 流式解码设计

> 日期：2026-08-07
> 目标：为现有 HTTP 流式播放链路增加 FLAC 支持（16/24bit）
> 前置：MP3 流式播放全通（自建帧同步 + 帧模式解码），WAV 直通

## 1. 现状与背景

- 播放链路：`POST /api/stream` → 预收 512B → 格式识别（RIFF=WAV / ID3·0xFF=MP3 / fmt 兜底）→ `stream_mp3` 或 `stream_wav`，都在 httpd 线程（stack 16384）
- 组件 `esp_audio_codec` 已含 FLAC 解码器（`CONFIG_AUDIO_DECODER_FLAC_SUPPORT=y`），**simple_dec 的 FLAC 走 parser 模式**（官方测试 `simple_decoder_test.c` 中 FLAC 在非帧模式组，`use_frame_dec=false`）
- FLAC 与 MP3 的关键差异：帧头无帧长字段；simple_dec 的 FLAC parser 需要 **eos 标志**刷新尾部缓存（`esp_audio_simple_dec.h` 注释明确）
- FLAC 同步码 14 位（0x3FFE）+ 帧头 CRC-8 校验，误同步概率远低于 MP3（11 位无校验）→ 不需要自建帧同步

## 2. 方案

**es_parser（parser）模式优先**：新开 FLAC 专用 simple_dec handle，数据直接喂 `esp_audio_simple_dec_process`（任意大小），parser 内部处理 fLaC 头 / metadata blocks / 帧同步。错误统计监控，实测发现丢帧再评估自建帧同步（本次不做，留待后续）。

## 3. 设计

### 3.1 格式识别（http_api.c `api_stream`）

- 新增分支：`memcmp(pre_buf, "fLaC", 4) == 0` → `is_flac`
- fmt 参数兜底支持 `fmt=flac`
- 路由到新函数 `stream_flac(req, &tx_chan, pre_buf, got, remaining)`

### 3.2 解码器初始化

```c
static esp_audio_simple_dec_handle_t s_flac_dec = NULL;
esp_audio_simple_dec_cfg_t cfg = {
    .dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC,
    .use_frame_dec = false,
};
```

复用现有 `esp_audio_dec_register_default()`（注册全部启用的解码器）。惰性初始化（首次播放时 open）。

### 3.3 stream_flac 播放流程

结构复用 stream_mp3（in_buf 512 / pcm_buf static / I2S 配置 / 音量 / 进度日志），差异：

1. **无自建帧同步**：`pre_buf` 及后续数据直接喂 process（任意大小）
2. **eos 收尾**：流结束（remaining==0 或连接断）后调一次 process 带 `raw.eos = true`，刷新尾部缓存数据
3. **I2S 配置**：沿用 `i2s_ensure_config`，按 `get_info` 的 sample_rate / bits_per_sample / channel（FLAC 单文件参数固定，无需中途重配逻辑）
4. **音量**：16bit 沿用现有 int16 缩放；24bit 用 int32 缩放
5. **诊断日志**：沿用 10 秒进度 + 堆内存 + 首帧 PCM 打印

### 3.4 24bit 输出处理

- PCM5102A 原生支持 24bit；`i2s_audio_init` 已支持 16/24/32bit 位深
- **实测点**：解码器输出 buffer 中 24bit 采样的容器格式未知（int32 左对齐或 3 字节小端）——首帧 PCM 打印验证后按实测写转换
- 若解码器输出 32bit 容器（int32 存 24bit 数据），I2S 配 32bit 位深即可正常出声

### 3.5 错误监控

- parser 模式解码失败返回错误码时统计 `s_err_cnt`（沿用 MP3 的统计模式）
- 连续大量错误 = es_parser 同步问题早期信号，日志告警

## 4. 测试

1. 组件自带 `test.flac`（1.1MB）→ `/api/stream?fmt=flac` 实测：解码、参数、出声
2. 首帧 PCM 打印验证 24bit 容器格式
3. 真歌 24bit FLAC（hifi 资源）验证高采样率
4. 回归：WAV / MP3 路径不受影响（改动仅识别分支 + 新函数）

## 5. 明确不做

- 不做 seek（流式不可 seek，与现状一致）
- 不抽公共函数重构（stream_* 各自独立，符合现有模式；`i2s_ensure_config` / `wav_write_pcm` / `s_volume` 已共享）
- 不做自建 FLAC 帧同步（es_parser 实测通过即不启用）

## 6. 交付物

1. `http_api.c`：识别分支 + `stream_flac` 函数（FLAC 解码器初始化 / 播放循环 / eos 收尾 / 24bit 音量）
2. 实测通过：test.flac + 24bit 真歌出声
