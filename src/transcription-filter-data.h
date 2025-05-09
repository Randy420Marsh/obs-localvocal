#ifndef TRANSCRIPTION_FILTER_DATA_H
#define TRANSCRIPTION_FILTER_DATA_H

#ifdef ENABLE_WEBVTT
#include <obs.hpp>
#include <webvtt-in-sei.h>
#endif

#include <util/circlebuf.h>
#include <util/darray.h>
#include <media-io/audio-resampler.h>

#include <whisper.h>

#include <thread>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <string>

#include "translation/translation.h"
#include "translation/translation-includes.h"
#include "whisper-utils/silero-vad-onnx.h"
#include "whisper-utils/whisper-processing.h"
#include "whisper-utils/token-buffer-thread.h"
#include "translation/cloud-translation/translation-cloud.h"

#define MAX_PREPROC_CHANNELS 10
#define MAX_WEBVTT_TRACKS 5

#if !defined(LIBOBS_MAJOR_VERSION) || LIBOBS_MAJOR_VERSION < 31
struct encoder_packet_time {
	/* PTS used to associate uncompressed frames with encoded packets. */
	int64_t pts;

	/* Composition timestamp is when the frame was rendered,
	 * captured via os_gettime_ns().
	 */
	uint64_t cts;

	/* FERC (Frame Encode Request) is when the frame was
	 * submitted to the encoder for encoding via the encode
	 * callback (e.g. encode_texture2()), captured via os_gettime_ns().
	 */
	uint64_t fer;

	/* FERC (Frame Encode Request Complete) is when
	 * the associated FER event completed. If the encode
	 * is synchronous with the call, this means FERC - FEC
	 * measures the actual encode time, otherwise if the
	 * encode is asynchronous, it measures the pipeline
	 * delay between encode request and encode complete.
	 * FERC is also captured via os_gettime_ns().
	 */
	uint64_t ferc;

	/* PIR (Packet Interleave Request) is when the encoded packet
	 * is interleaved with the stream. PIR is captured via
	 * os_gettime_ns(). The difference between PIR and CTS gives
	 * the total latency between frame rendering
	 * and packet interleaving.
	 */
	uint64_t pir;
};
#endif

using obs_output_add_packet_callback_t =
	void(obs_output_t *output,
	     void (*packet_cb)(obs_output_t *output, struct encoder_packet *pkt,
			       struct encoder_packet_time *pkt_time, void *param),
	     void *param);
using obs_output_remove_packet_callback_t =
	void(obs_output_t *output,
	     void (*packet_cb)(obs_output_t *output, struct encoder_packet *pkt,
			       struct encoder_packet_time *pkt_time, void *param),
	     void *param);

extern obs_output_add_packet_callback_t *obs_output_add_packet_callback_;
extern obs_output_remove_packet_callback_t *obs_output_remove_packet_callback_;
extern "C" void load_packet_callback_functions();

#ifdef ENABLE_WEBVTT
struct webvtt_muxer_deleter {
	void operator()(WebvttMuxer *m) { webvtt_muxer_free(m); }
};

struct webvtt_buffer_deleter {
	void operator()(WebvttBuffer *b) { webvtt_buffer_free(b); }
};
#endif

struct transcription_filter_data {
	obs_source_t *context; // obs filter source (this filter)
	size_t channels;       // number of channels
	uint32_t sample_rate;  // input sample rate
	// How many input frames (in input sample rate) are needed for the next whisper frame
	size_t frames;
	// How many frames were processed in the last whisper frame (this is dynamic)
	size_t last_num_frames;
	// Start begining timestamp in ms since epoch
	uint64_t start_timestamp_ms;
	// Sentence counter for srt
	size_t sentence_number;
	// Minimal subtitle duration in ms
	size_t min_sub_duration;
	// Maximal subtitle duration in ms
	size_t max_sub_duration;
	// Last time a subtitle was rendered
	uint64_t last_sub_render_time;
	bool cleared_last_sub;

	/* PCM buffers */
	float *copy_buffers[MAX_PREPROC_CHANNELS];
	struct circlebuf info_buffer;
	struct circlebuf input_buffers[MAX_PREPROC_CHANNELS];
	std::atomic<bool> clear_buffers;
	struct circlebuf whisper_buffer;

	/* Resampler */
	audio_resampler_t *resampler_to_whisper;
	struct circlebuf resampled_buffer;

	/* whisper */
	std::string whisper_model_path;
	struct whisper_context *whisper_context;
	whisper_full_params whisper_params;

	/* Silero VAD */
	std::unique_ptr<VadIterator> vad;

	float filler_p_threshold;
	float sentence_psum_accept_thresh;

	bool do_silence;
	int vad_mode;
	int log_level = LOG_DEBUG;
	bool log_words;
	bool caption_to_stream;
	bool active = false;
	bool save_to_file = false;
	bool save_srt = false;
	bool truncate_output_file = false;
	bool save_only_while_recording = false;
	bool process_while_muted = false;
	bool rename_file_to_match_recording = false;
	bool translate = false;
	std::string target_lang;
	std::string translation_output;
	bool enable_token_ts_dtw = false;
	std::vector<std::tuple<std::string, std::string>> filter_words_replace;
	bool fix_utf8 = true;
	bool enable_audio_chunks_callback = false;
	bool source_signals_set = false;
	bool initial_creation = true;
	bool partial_transcription = false;
	int partial_latency = 1000;
	float duration_filter_threshold = 2.25f;
	// Duration of the target segment buffer in ms
	int segment_duration = 7000;

	// Cloud translation options
	bool translate_cloud = false;
	CloudTranslatorConfig translate_cloud_config;
	std::string translate_cloud_target_language;
	std::string translate_cloud_output;
	bool translate_cloud_only_full_sentences = true;
	std::string last_text_for_cloud_translation;
	std::string last_text_cloud_translation;

	// Transcription context sentences
	int n_context_sentences;
	std::deque<std::string> last_transcription_sentence;

	// Text source to output the subtitles
	std::string text_source_name;
	// Callback to set the text in the output text source (subtitles)
	std::function<void(const DetectionResultWithText &result)> setTextCallback;
	// Output file path to write the subtitles
	std::string output_file_path;
	std::string whisper_model_file_currently_loaded;
	bool whisper_model_loaded_new;

	// Use std for thread and mutex
	std::thread whisper_thread;

	std::mutex whisper_buf_mutex;
	std::mutex whisper_ctx_mutex;
	std::condition_variable wshiper_thread_cv;
	std::optional<std::condition_variable> input_cv;

	// translation context
	struct translation_context translation_ctx;
	std::string translation_model_index;
	std::string translation_model_path_external;
	bool translate_only_full_sentences;
	// Last transcription result
	std::string last_text_for_translation;
	std::string last_text_translation;

	bool buffered_output = false;
	TokenBufferThread captions_monitor;
	TokenBufferThread translation_monitor;
	int buffered_output_num_lines = 2;
	int buffered_output_num_chars = 30;
	TokenBufferSegmentation buffered_output_output_type =
		TokenBufferSegmentation::SEGMENTATION_TOKEN;

#ifdef ENABLE_WEBVTT
	enum struct webvtt_output_type {
		Streaming,
		Recording,
	};

	struct webvtt_output {
		OBSWeakOutputAutoRelease output;
		webvtt_output_type output_type;
		uint64_t start_timestamp_ms;

		bool initialized = false;
		std::map<std::string, uint8_t> language_to_track;
		std::unique_ptr<WebvttMuxer, webvtt_muxer_deleter>
			webvtt_muxer[MAX_OUTPUT_VIDEO_ENCODERS];
		CodecFlavor codec_flavor[MAX_OUTPUT_VIDEO_ENCODERS] = {};
	};

	std::mutex active_outputs_mutex;
	std::vector<webvtt_output> active_outputs;

	std::mutex webvtt_settings_mutex;
	uint16_t latency_to_video_in_msecs;
	uint8_t send_frequency_hz;
	std::vector<std::string> active_languages;

	std::atomic<bool> webvtt_caption_to_stream;
	std::atomic<bool> webvtt_caption_to_recording;
#endif

	// ctor
	transcription_filter_data() : whisper_buf_mutex(), whisper_ctx_mutex(), wshiper_thread_cv()
	{
		// initialize all pointers to nullptr
		for (size_t i = 0; i < MAX_PREPROC_CHANNELS; i++) {
			copy_buffers[i] = nullptr;
		}
		context = nullptr;
		resampler_to_whisper = nullptr;
		whisper_model_path = "";
		whisper_context = nullptr;
		output_file_path = "";
		whisper_model_file_currently_loaded = "";
	}
};

// Audio packet info
struct transcription_filter_audio_info {
	uint32_t frames;
	uint64_t timestamp_offset_ns; // offset (since start of processing) timestamp in ns
};

// Callback sent when the transcription has a new result
void set_text_callback(uint64_t possible_end_ts, struct transcription_filter_data *gf,
		       const DetectionResultWithText &str);
void clear_current_caption(transcription_filter_data *gf_);

// Callback sent when the VAD finds an audio chunk. Sample rate = WHISPER_SAMPLE_RATE, channels = 1
// The audio chunk is in 32-bit float format
void audio_chunk_callback(struct transcription_filter_data *gf, const float *pcm32f_data,
			  size_t frames, int vad_state, const DetectionResultWithText &result);

#endif /* TRANSCRIPTION_FILTER_DATA_H */
