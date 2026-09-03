/*
The MIT License (MIT)

Copyright © 2026 John Settlemyer

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "GenericPython.h"

#include <dotconf.h>

GString *venv_path;
GString *source_path;

bool debug_module=false;

static DOTCONF_CB(cb_venvpath);
static DOTCONF_CB(cb_sourcepath);
static DOTCONF_CB(cb_debug_module);

static const configoption_t options[] = {
	{"debug", ARG_STR, cb_debug_module, NULL, CTX_ALL},
	{"venvPath", ARG_STR, cb_venvpath, NULL, CTX_ALL},
	{"sourcePath", ARG_STR, cb_sourcepath, NULL, CTX_ALL},
	LAST_OPTION
};

bool built_voice_table = false;

int module_config(const char *configfilepath)
{
	MSG(1, "opening .conf file %s\n", configfilepath);

	venv_path = g_string_new("");
	source_path = g_string_new("");

	configfile_t *configfile;

	configfile = dotconf_create((char* )configfilepath,
				    options, NULL, CASE_INSENSITIVE);

	if (!configfile) {
		MSG(2, "Error: opening config file\n");
		return -1;
	}

	if (dotconf_command_loop(configfile) == 0)
		MSG(2, "Nothing in config file\n");

	dotconf_cleanup(configfile);

	DEBUG_PRINT("finished parsing config file\n");

	return 0;
}

DOTCONF_CB(cb_venvpath)
{
	g_string_assign(venv_path, cmd->data.str);
	MSG(5, "Info: Using '%s' for venv\n", venv_path->str);
	return NULL;
}

DOTCONF_CB(cb_sourcepath)
{
	g_string_assign(source_path, cmd->data.str);
	MSG(5, "Info: Using '%s' for python source file\n", source_path->str);
	return NULL;
}

DOTCONF_CB(cb_debug_module)
{
	if (!strcmp(cmd->data.str, "TRUE")){
		MSG(5, "Debug enabled\n");
		debug_module = true;
	}
	return NULL;
}

int module_init(char **msg)
{
	MSG(1, "Initializing\n");

	int ret = init_model_thread_pool();
	if (ret == 0){
		*msg = strdup("ok!");
	} else{
		*msg = strdup("Failed Init");
	}

	return ret;
}

SPDVoice **module_list_voices(void)
{
	SPDVoice **ret = malloc(3*sizeof(*ret));

	ret[0] = malloc(sizeof(*(ret[0])));
	ret[0]->name = strdup("GenericPython - English (America)");
	ret[0]->language = strdup("en");
	ret[0]->variant = NULL;

	ret[1] = malloc(sizeof(*(ret[0])));
	ret[1]->name = strdup("French (France)");
	ret[1]->language = strdup("fr");
	ret[1]->variant = NULL;

	ret[2] = NULL;

	return ret;
}

int module_set(const char *var, const char *val)
{
	DEBUG_PRINT("module_set: var '%s' to be set to '%s'\n", var, val);

	if (!strcmp(var, "synthesis_voice")) {
		// change the voice.
		model_change_voice(var, val);
		return 0;
	} else if (!strcmp(var, "rate")) {
		// change the speed.
		model_change_speed(var, val);
		return 0;
	}

	return 0;
}

int module_audio_set(const char *var, const char *val)
{
	/* Optional: interpret audio parameter */
	if (!strcmp(var, "audio_output_method")) {
		/* Only server-side audio supported */
		if (strcmp(val, "server") != 0)
			return -1;
		return 0;
	}
	return -1;
}

int module_audio_init(char **status)
{
	return 0;
}

int module_loglevel_set(const char *var, const char *val)
{
	return 0;
}

int module_debug(int enable, const char *file)
{
	return 0;
}

int module_loop(void)
{
	/* Main loop */
	MSG(1, "Main Loop\n");

	/* Let module_process run the protocol */
	int ret = module_process(STDIN_FILENO, 1);

	if (ret != 0)
		MSG(2, "Error: Broken pipe, exiting...\n");

	return ret;
}

int module_speak(char *data, size_t bytes, SPDMessageType msgtype)
{
    add_generate_speech_task(data, bytes);
    return 1; // delivery to the synthesizer is successful
}

size_t module_pause(void)
{
	/* Pause playing */
	DEBUG_PRINT("Pausing\n");

	model_stop_generation();
	module_report_event_stop();

	return 0;
}

int module_stop(void)
{
	/* Stop any current synth */
	DEBUG_PRINT("Stopping\n");

	model_stop_generation();
	module_report_event_stop();

	return 0;
}

int module_close(void)
{
	/* Deinitialize synthesizer */
	MSG(1, "Closing\n");

	cleanup_threads();

	return 0;
}