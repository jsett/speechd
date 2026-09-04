/*
The MIT License (MIT)

Copyright © 2026 John Settlemyer

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "GenericPython.h"

typedef struct {
    char *data;
    size_t size;
} GeneratePayload;

static GMutex model_mutex;
static GMutex stop_mutex;
static GMutex ahead_mutex;
GAsyncQueue *message_queue;
GAsyncQueue *wav_queue;

float ahead_by;
bool stop_generation=false;

enum wavCommands { BEGIN, STOP, DATA };

typedef struct {
    enum wavCommands cmd;
    GArray *op;
    int bitrate;
    GString *mark;
} WavPayload;

static pthread_t kitten_generation_thread;
static pthread_t kitten_play_wav_thread;

void free_WavPayload(gpointer data){
    WavPayload *wp = (WavPayload*) data;
    if (wp->cmd == DATA){
        g_string_free(wp->mark, TRUE);
        g_array_unref(wp->op);
    }
    g_free(data);
}

void free_GeneratePayload(gpointer data){
    GeneratePayload *tmp = (GeneratePayload *) data;
    g_free(tmp->data);
    g_free(tmp);
}

// changes the voice, the inputs are the same as module_set
// and this should be called from module_set
int model_change_voice(const char *var, const char *val){
    g_mutex_lock(&model_mutex);

    // This will be sent to python to handle.
    PyEval_RestoreThread(server_tstate);
    call_change_voice_method(val);
    PyEval_SaveThread();

    g_mutex_unlock(&model_mutex);
}

// changes the speed, the inputs are the same as module_set
// and this should be called from module_set
int model_change_speed(const char *var, const char *val){
    g_mutex_lock(&model_mutex);

    // parse the string into a int
    char *endptr = NULL;
    gint64 int_val = g_ascii_strtoll(val, &endptr, 10);
    int result = (int)int_val;

    // This will be sent to python to handle.
    PyEval_RestoreThread(server_tstate);
    call_change_speed_method(result);
    PyEval_SaveThread();

    g_mutex_unlock(&model_mutex);
}

// adds a wave to our wav_queue
void send_wav(GArray *wav, int bitrate, const char *mark){
    WavPayload *wp = g_new(WavPayload, 1);
    wp->cmd = DATA;
    wp->bitrate = bitrate;
    wp->op = wav;
    if (mark == NULL){
        wp->mark = g_string_new("");
    } else {
        wp->mark = g_string_new(mark);
    }
    // everything pushed to the wav_queue should get freed by
    // _play_wav_thread or the free_WavPayload on destroy.
    g_async_queue_push(wav_queue, wp);
}

// add a begin command to the wav_queue
// this will be processed into a module_report_event_begin();
void send_wav_start(){
    WavPayload *wp = g_new(WavPayload, 1);
    wp->cmd = BEGIN;
    // everything pushed to the wav_queue should get freed by
    // _play_wav_thread or the free_WavPayload on destroy.
    g_async_queue_push(wav_queue, wp);
}

// add a begin command to the wav_queue
// this will be processed into a module_report_event_end();
void send_wav_end(){
    WavPayload *wp = g_new(WavPayload, 1);
    wp->cmd = STOP;
    // everything pushed to the wav_queue should get freed by
    // _play_wav_thread or the free_WavPayload on destroy.
    g_async_queue_push(wav_queue, wp);
}

// this gets called on our audio play thread to send out our audio.
void send_samples(short *wav, int len, int rate)
{
	if (!len)
		return;

	AudioTrack track = {
		.bits = 16,
		.num_channels = 1,
		.sample_rate = rate,
		.num_samples = len,
		.samples = wav,
	};
	module_tts_output_server(&track, SPD_AUDIO_LE);
}

void ahead_add(float val){
    g_mutex_lock(&ahead_mutex);
    ahead_by += val;
    g_mutex_unlock(&ahead_mutex);
}

void ahead_set(float val){
    g_mutex_lock(&ahead_mutex);
    ahead_by = val;
    g_mutex_unlock(&ahead_mutex);
}

float ahead_get(){
    float tmp;
    g_mutex_lock(&ahead_mutex);
    tmp = ahead_by;
    g_mutex_unlock(&ahead_mutex);
    return tmp;
}

void ahead_print(){
    DEBUG_PRINT("Ahead by %f seconds of audio\n", ahead_get());
}

void stop_set(bool val){
    g_mutex_lock(&stop_mutex);
    stop_generation=val;
    g_mutex_unlock(&stop_mutex);
}

bool stop_get(){
    bool tmp;
    g_mutex_lock(&stop_mutex);
    tmp = stop_generation;
    g_mutex_unlock(&stop_mutex);
    return tmp;
}


// TODO: going to keep the ssml parser but need to be changed into a python callback. this will allow python user to use ssml string in a single call.
/*
we use lib xml to parse the ssml string into chunks containing
the text(usally a sentence) and the mark. These then get passed
into a gqueue for future use and returned by the function.
*/
// GQueue* parse_ssml_to_gqueue(const char *data, size_t bytes){
//     GQueue *output = g_queue_new();

//     // use libxml to parse the ssml data.
//     xmlDocPtr doc = xmlReadMemory(data, bytes, "noname.xml", NULL, 0);
//     if (doc == NULL) {
//         MSG(2, "Error: Failed to parse XML\n");
//         return output;
//     }

//     xmlNodePtr root = xmlDocGetRootElement(doc); // <speak> node
//     xmlBufferPtr buffer = xmlBufferCreate();

//     // Iterate through child nodes inside <speak>
//     for (xmlNodePtr cur = root->children; cur != NULL; cur = cur->next) {
//         if (cur->type == XML_TEXT_NODE) {
//             // Append text content to buffer
//             xmlBufferCat(buffer, cur->content);
//         }
//         else if (cur->type == XML_ELEMENT_NODE && xmlStrcmp(cur->name, (const xmlChar *)"mark") == 0) {

//             xmlChar *mark_name = xmlGetProp(cur, (const xmlChar *)"name");

//             SSMLPayload *pl = g_new(SSMLPayload,1);

//             pl->mark = g_string_new(mark_name);
//             pl->text = g_string_new((const char *)xmlBufferContent(buffer));

//             g_queue_push_tail(output,pl);

//             // Clean up the attribute memory and reset the buffer for the next segment
//             if (mark_name) xmlFree(mark_name);
//             xmlBufferEmpty(buffer);

//         }
//     }
//     // add any trailing text after the final mark.
//     if (xmlBufferLength(buffer) > 0) {
//         SSMLPayload *pl = g_new(SSMLPayload,1);

//         pl->mark = g_string_new("");
//         pl->text = g_string_new((const char *)xmlBufferContent(buffer));

//         g_queue_push_tail(output, pl);
//     }

//     xmlBufferFree(buffer);
//     xmlFreeDoc(doc);
//     xmlCleanupParser();

//     return output;
// }

int model_generate_speech(const char *data, size_t bytes){
    g_mutex_lock(&model_mutex);

    stop_set(false); // make sure that stop generation has been reset to false.
    ahead_set(0.0); // number of seconds the generation is ahead of the played audio.

    send_wav_start();

    // have python handle this.
    PyEval_RestoreThread(worker_tstate);
    call_speak_method(data, bytes);
    PyEval_SaveThread();

    send_wav_end();

    g_mutex_unlock(&model_mutex);
    return 0;
}

// this thread loop handles the generation of audio.
void *_generation_thread(void *nothing)
{
	while (1) {
        // use a async queue to handle signalling.
        GeneratePayload *message_payload = (GeneratePayload *) g_async_queue_pop(message_queue);// this is thread safe and will block if nothing in the queue.
        DEBUG_PRINT("thread loop: got message: size %zd\n",message_payload->size);

        model_generate_speech(message_payload->data, message_payload->size);

        free_GeneratePayload(message_payload);
	}

	pthread_exit(NULL);
}

// this thread loop handles sendding the audio out
// since sending the samples can block,
// it needs to be on its own thread so we don't block
// the model generation thread.
void *_play_wav_thread(void *nothing)
{
	while (1) {
        // use a async queue to handle signalling.
        WavPayload *wp = (WavPayload*) g_async_queue_pop(wav_queue);// this is thread safe and will block if nothing in the queue.
        DEBUG_PRINT("wav_thread: got payload\n");

        if (wp->cmd == BEGIN){
            DEBUG_PRINT("wav_thread: module_report_event_begin\n");
            module_report_event_begin();
        } else if (wp->cmd == STOP){
            DEBUG_PRINT("wav_thread: module_report_event_end\n");
            module_report_event_end();
        } else if (wp->cmd == DATA){
            DEBUG_PRINT("wav_thread: send_samples\n");
            GArray *op = (GArray *) wp->op;
            GString *mark = wp->mark;
            int bitrate = wp->bitrate;
            send_samples((short*)op->data, op->len, bitrate);
            ahead_add(-1 * ((float)op->len/bitrate));
            if (mark->len != 0){
                module_report_index_mark(mark->str);
            }
        }
        free_WavPayload(wp);
	}

	pthread_exit(NULL);
}

// this pushs a message that will be spoken
// this should be called from module_speak
int add_generate_speech_task(const char* data, size_t bytes) {
    GeneratePayload *message_payload;
    message_payload = g_new(GeneratePayload, 1);

    message_payload->data = g_memdup2(data, bytes);
    message_payload->size = bytes;
    // everything pushed to the message_queue should get freed by
    // _generation_thread or the free_GeneratePayload on destroy.
    g_async_queue_push(message_queue, message_payload);

    return 0;
}

// this will stop our generation
// its called from module_pause and module_stop
int model_stop_generation(){
    stop_set(true);
    return 0;
}

int init_model_thread_pool(){
    DEBUG_PRINT("init_model_thread_pool();\n");
    GError *pool_error = NULL;

    g_mutex_init(&model_mutex);
    g_mutex_init(&stop_mutex);
    g_mutex_init(&ahead_mutex);

    message_queue = g_async_queue_new_full((GDestroyNotify) free_GeneratePayload);
    wav_queue = g_async_queue_new_full((GDestroyNotify) free_WavPayload);

    // lock the model mutex while loading
    g_mutex_lock(&model_mutex);

    if (spd_pthread_create(&kitten_generation_thread, NULL, _generation_thread, NULL) != 0){
        MSG(2, "ERROR: Creating _generation_thread()\n");
        g_mutex_unlock(&model_mutex);
        return -1;
    }

    if (spd_pthread_create(&kitten_play_wav_thread, NULL, _play_wav_thread, NULL) != 0){
        MSG(2, "ERROR: Creating _play_wav_thread()\n");
        g_mutex_unlock(&model_mutex);
        return -1;
    }

    if (setup_python() != 0){
        fprintf(stderr, "ERROR: Failed to start python.\n");
        return -1;
    }

    g_mutex_unlock(&model_mutex);

    return 0;
}

int cleanup_threads(){

    g_mutex_lock(&model_mutex);
    cleanup_python();
    g_mutex_unlock(&model_mutex);

    //free the mutex's and queues.
    g_mutex_clear(&model_mutex);
    g_mutex_clear(&stop_mutex);
    g_mutex_clear(&ahead_mutex);

    g_async_queue_unref(wav_queue);
    g_async_queue_unref(message_queue);

    g_string_free(venv_path, TRUE);
    g_string_free(source_path, TRUE);

}