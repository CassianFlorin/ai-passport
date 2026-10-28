#include "ai_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void feed_all(ai_wav_parser_t *parser, const uint8_t *data, size_t length,
                     size_t step, uint8_t *pcm, size_t *pcm_length)
{
    *pcm_length = 0;
    for (size_t offset = 0; offset < length; offset += step) {
        const uint8_t *chunk = data + offset;
        size_t remaining = length - offset < step ? length - offset : step;
        ai_wav_result_t result = ai_wav_feed(parser, &chunk, &remaining);
        assert(result != AI_WAV_INVALID);
        if (result == AI_WAV_PCM) {
            memcpy(pcm + *pcm_length, chunk, remaining);
            *pcm_length += remaining;
        }
    }
}

int main(void)
{
    char url[64];
    assert(ai_endpoint(url, sizeof(url), "https://api.example/v1/", "/chat/completions"));
    assert(!strcmp(url, "https://api.example/v1/chat/completions"));
    assert(ai_endpoint(url, sizeof(url), "http://127.0.0.1:8080", "/audio/speech"));
    assert(!strcmp(url, "http://127.0.0.1:8080/audio/speech"));
    assert(!ai_endpoint(url, sizeof(url), "wss://api.example/v1", "/chat/completions"));
    assert(!ai_endpoint(url, sizeof(url), "https://api.example/v1?key=1", "/x"));
    assert(!ai_endpoint(url, sizeof(url), "https:///", "/x"));
    assert(!ai_endpoint(url, 20, "https://api.example/v1", "/chat/completions") && !url[0]);
    assert(!strcmp(ai_model_or_default("", "whisper-1"), "whisper-1"));
    assert(!strcmp(ai_model_or_default("custom", "whisper-1"), "custom"));

    char small[8];
    assert(ai_utf8_copy(small, sizeof(small), "ab\xe4\xbd\xa0\xe5\xa5\xbd") == 5);
    assert(!strcmp(small, "ab\xe4\xbd\xa0"));
    assert(ai_utf8_copy(small, sizeof(small), "ab\xe4") == 2);

    char text[128] = "  **你好**\r\n\n\n\n# 标题\t`code` \xf0\x9f\x98\x80!\xe2\x9d\xa4\xef\xb8\x8f\n\n";
    ai_text_clean(text);
    assert(!strcmp(text, "你好\n\n 标题 code !\xe2\x9d\xa4"));

    char body[512];
    const ai_turn_t turns[] = {
        { "第一句", "回答\"一\"" },
        { "", "ignored" },
    };
    size_t length = ai_chat_body(body, sizeof(body), "gpt", "sys\n", turns, 2, "a\\b\x01");
    assert(length == strlen(body));
    assert(!strcmp(body, "{\"model\":\"gpt\",\"stream\":false,\"messages\":["
        "{\"role\":\"system\",\"content\":\"sys\\n\"},"
        "{\"role\":\"user\",\"content\":\"第一句\"},"
        "{\"role\":\"assistant\",\"content\":\"回答\\\"一\\\"\"},"
        "{\"role\":\"user\",\"content\":\"a\\\\b\\u0001\"}]}"));
    assert(ai_chat_body(body, 40, "gpt", "sys", NULL, 0, "hello") == 0 && !body[0]);
    length = ai_speech_body(body, sizeof(body), "tts-1", "alloy", "你好");
    assert(!strcmp(body, "{\"model\":\"tts-1\",\"voice\":\"alloy\",\"input\":\"你好\","
                         "\"response_format\":\"wav\"}"));

    length = ai_transcription_head(body, sizeof(body), "whisper-1");
    assert(length && strstr(body, "name=\"model\"\r\n\r\nwhisper-1\r\n"));
    assert(strstr(body, "filename=\"speech.wav\"\r\nContent-Type: audio/wav\r\n\r\n"));
    assert(!ai_transcription_head(body, sizeof(body), "bad\"model"));
    assert(!ai_transcription_head(body, 32, "whisper-1"));
    assert(!strcmp(ai_transcription_tail(), "\r\n--" AI_MULTIPART_BOUNDARY "--\r\n"));

    char prefix[12];
    assert(ai_chunk_prefix(prefix, 1024) == 5 && !strcmp(prefix, "400\r\n"));

    const char *chat = "{\"id\":\"x\",\"choices\":[{\"index\":0,\"message\":{\"role\":\"assistant\","
        "\"reasoning_content\":\"skip\",\"content\":\"\\u4f60\\u597d \\ud83d\\ude00\\n\\\"ok\\\"\"}}],"
        "\"note\":\"\\\"content\\\": decoy\"}";
    char value[64];
    assert(ai_json_find_string(chat, strlen(chat), "message", "content", value, sizeof(value)));
    assert(!strcmp(value, "你好 \xf0\x9f\x98\x80\n\"ok\""));
    assert(ai_json_find_string(chat, strlen(chat), "message", "content", value, 5));
    assert(!strcmp(value, "你"));
    const char *decoy = "{\"text\":\"\\\"content\\\": no\",\"message\":{\"content\":null}}";
    assert(!ai_json_find_string(decoy, strlen(decoy), "message", "content", value, sizeof(value)));
    const char *transcript = "{ \"text\" : \"\xe4\xbb\x8a\xe5\xa4\xa9\" }";
    assert(ai_json_find_string(transcript, strlen(transcript), NULL, "text", value, sizeof(value)));
    assert(!strcmp(value, "今天"));
    assert(!ai_json_find_string(transcript, 12, NULL, "text", value, sizeof(value)));
    const char *bad = "{\"text\":\"\\udc00\"}";
    assert(!ai_json_find_string(bad, strlen(bad), NULL, "text", value, sizeof(value)));

    // The device's own streaming header must be accepted by the playback parser.
    uint8_t wav[128];
    ai_wav_header(wav, 16000, 16, 1);
    assert(!memcmp(wav, "RIFF", 4) && !memcmp(wav + 36, "data", 4));
    for (int i = 0; i < 10; i++) wav[AI_WAV_HEADER_SIZE + i] = (uint8_t)i;
    uint8_t pcm[128];
    size_t pcm_length;
    ai_wav_parser_t parser;
    for (size_t step = 1; step <= 60; step++) {
        ai_wav_init(&parser);
        feed_all(&parser, wav, AI_WAV_HEADER_SIZE + 10, step, pcm, &pcm_length);
        assert(pcm_length == 10 && pcm[9] == 9 && parser.rate == 16000 && parser.channels == 1);
    }

    // Extra chunks are skipped and a sized data chunk drops trailing bytes.
    uint8_t sized[] = {
        'R','I','F','F', 0,0,0,0, 'W','A','V','E',
        'L','I','S','T', 3,0,0,0, 'a','b','c', 0,
        'f','m','t',' ', 18,0,0,0, 1,0, 2,0, 0xc0,0x5d,0,0, 0,0,0,0, 4,0, 16,0, 0,0,
        'd','a','t','a', 4,0,0,0, 1,2,3,4, 9,9,
    };
    ai_wav_init(&parser);
    feed_all(&parser, sized, sizeof(sized), 3, pcm, &pcm_length);
    assert(pcm_length == 4 && pcm[3] == 4 && parser.rate == 24000 && parser.channels == 2);

    uint8_t mp3[] = { 'I', 'D', '3', 4, 0, 0, 0, 0, 0, 0, 0, 0 };
    const uint8_t *cursor = mp3;
    size_t remaining = sizeof(mp3);
    ai_wav_init(&parser);
    assert(ai_wav_feed(&parser, &cursor, &remaining) == AI_WAV_INVALID);
    ai_wav_header(wav, 16000, 8, 1);
    cursor = wav;
    remaining = AI_WAV_HEADER_SIZE;
    ai_wav_init(&parser);
    assert(ai_wav_feed(&parser, &cursor, &remaining) == AI_WAV_INVALID);

    // Model IDs survive any read split; nested and escaped strings are not IDs.
    const char *models = "{\"object\":\"list\",\"data\":[{\"id\": \"deepseek-chat\","
        "\"object\":\"model\",\"owned_by\":\"id\"},{\"id\":\"Qwen/Qwen2.5-7B\"},"
        "{\"id\":\"bad id\"},{\"id\":\"esc\\\"aped\"},{\"note\":\"id\",\"id\":\"FunAudioLLM/SenseVoiceSmall\"},"
        "{\"id\":7},{\"meta\":{\"id\":\"nested-ok\"}}]}";
    char list[256];
    ai_model_scanner_t scanner;
    for (size_t step = 1; step <= strlen(models); step++) {
        ai_model_scanner_init(&scanner, list, sizeof(list));
        for (size_t offset = 0; offset < strlen(models); offset += step) {
            size_t count = strlen(models) - offset < step ? strlen(models) - offset : step;
            ai_model_scanner_feed(&scanner, models + offset, count);
        }
        assert(!strcmp(list, "\"deepseek-chat\",\"Qwen/Qwen2.5-7B\","
                             "\"FunAudioLLM/SenseVoiceSmall\",\"nested-ok\""));
        assert(scanner.count == 4 && !scanner.truncated);
    }
    ai_model_scanner_init(&scanner, list, 20);
    ai_model_scanner_feed(&scanner, models, strlen(models));
    assert(scanner.count == 1 && scanner.truncated && !strcmp(list, "\"deepseek-chat\""));
    char long_id[200] = "{\"id\":\"";
    memset(long_id + 7, 'x', 70);
    strcpy(long_id + 77, "\"}");
    ai_model_scanner_init(&scanner, list, sizeof(list));
    ai_model_scanner_feed(&scanner, long_id, strlen(long_id));
    assert(scanner.count == 0 && !list[0]);

    puts("AI protocol helpers: PASS");
    return 0;
}
