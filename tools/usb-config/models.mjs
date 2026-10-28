// Group a service's model IDs for each dropdown. Matching is a hint only:
// every model stays selectable under "all models", and manual entry remains.
export const DEFAULTS = {
  chat_model: "gpt-4o-mini", asr_model: "whisper-1", tts_model: "tts-1", tts_voice: "alloy",
};
export const VOICES = ["alloy", "ash", "ballad", "coral", "echo", "fable", "nova", "onyx",
  "sage", "shimmer", "verse"];

const ASR = /whisper|transcri|asr|sensevoice|paraformer|speech-?to-?text|stt/iu;
const TTS = /tts|text-?to-?speech|cosyvoice|speech/iu;
const NOT_CHAT = /embed|moderation|dall-?e|image|rerank|whisper|tts|transcri|audio|realtime|speech|asr|sora|davinci|babbage/iu;

export function recommended(kind, id) {
  if (kind === "asr_model") return ASR.test(id);
  if (kind === "tts_model") return TTS.test(id) && !ASR.test(id);
  return !NOT_CHAT.test(id);
}

export function groupModels(kind, models) {
  const ids = [...new Set(models)].filter((id) => typeof id === "string" && id).sort();
  return {
    recommended: ids.filter((id) => recommended(kind, id)),
    others: ids.filter((id) => !recommended(kind, id)),
  };
}
