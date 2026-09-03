/*
The MIT License (MIT)

Copyright © 2026 John Settlemyer

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <stdio.h>
#include <glib.h>
#include <Python.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include "module_utils.h"
#include <speechd_types.h>
#include "spd_module_main.h"

extern bool debug_module;

extern GString *venv_path;
extern GString *source_path;

#define DEBUG_PRINT(...) \
    do { \
        if (debug_module) { \
            MSG(5, __VA_ARGS__); \
        } \
    } while (0)

// GenericPython_worker.c
int init_model_thread_pool();
int cleanup_threads();
int model_change_voice(const char *var, const char *val);
int model_change_speed(const char *var, const char *val);
int add_generate_speech_task(const char* data, size_t bytes);
int model_stop_generation();
void send_wav(GArray *wav, char *mark);
void ahead_add(float val);
void ahead_set(float val);
float ahead_get();
void ahead_print();
void stop_set(bool val);
bool stop_get();


// GenericPython_client.c
int cleanup_python();
int setup_python();
int call_change_speed_method(int speed);
int call_change_voice_method(const char* voice);
int call_list_voices_method();
int call_speak_method(const char* text);