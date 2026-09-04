/*
The MIT License (MIT)

Copyright © 2026 John Settlemyer

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "GenericPython.h"

// This holds our thread state.
PyThreadState *main_tstate;
PyThreadState *server_tstate;
PyThreadState *worker_tstate;

// Global storage for the retained reference
static PyObject* pSpeechDispatchClassInstance = NULL;

/*
Finds the correct path to the python executable based on specific rules.
The caller is responsible for freeing the returned GString with g_string_free().
*/
GString* find_python_executable(const GString *input_path) {

    GString *output = g_string_new("");

    gchar *bin_python_path = g_build_filename(input_path->str, "bin", "python", NULL);
    gchar *python_path = g_build_filename(input_path->str, "python", NULL);

    if (!input_path || !input_path->str) {
        g_string_assign(output, "");
    }

    // Check if input_path points to a file and ends with "/python"
    if (g_file_test(input_path->str, G_FILE_TEST_IS_REGULAR) &&
        g_str_has_suffix(input_path->str, "/python")) {
        g_string_assign(output, input_path->str);
    }

    // Check if input_path has a subfolder "/bin" containing a file named "python"
    // Construct path: input_path + "/bin/python"
    if (g_file_test(bin_python_path, G_FILE_TEST_IS_REGULAR)) {
        g_string_assign(output, bin_python_path);
    }

    // Check if input_path is a folder, ends with "/bin", and contains "python"
    if (g_file_test(input_path->str, G_FILE_TEST_IS_DIR) &&
        g_str_has_suffix(input_path->str, "/bin")) {

        if (g_file_test(python_path, G_FILE_TEST_IS_REGULAR)) {
            g_string_assign(output, python_path);
        }
    }

    // Check if input_path is a folder, ends with "/bin/", and contains "python"
    if (g_file_test(input_path->str, G_FILE_TEST_IS_DIR) &&
        g_str_has_suffix(input_path->str, "/bin/")) {

        if (g_file_test(python_path, G_FILE_TEST_IS_REGULAR)) {
            g_string_assign(output, python_path);
        }
    }

    g_free(bin_python_path);
    g_free(python_path);
    return output;
}

/*
Call the change_speed method in our python source.
*/
int call_change_speed_method(int speed){
    DEBUG_PRINT("C host: call_change_speed_method()\n");
    PyObject *pResult = PyObject_CallMethod(pSpeechDispatchClassInstance, "change_speed", "i", speed);
    if (pResult != NULL) {
        Py_DECREF(pResult);
    }
    return 1;
}

/*
Call the change_voice method in our python source.
*/
int call_change_voice_method(const char* voice){
    DEBUG_PRINT("C host: call_change_voice_method()\n");
    PyObject *pResult = PyObject_CallMethod(pSpeechDispatchClassInstance, "change_voice", "s", voice);
    if (pResult != NULL) {
        Py_DECREF(pResult);
    }
    return 1;
}

void process_list_result(PyObject *result) {
    DEBUG_PRINT("C host: process_list_result()\n");
    // Verify the result is actually a Python list
    if (!PyList_Check(result)) {
        PyErr_SetString(PyExc_TypeError, "Expected result to be a list");
        PyErr_Print();
        return;
    }

    // Get the number of items in the list
    Py_ssize_t size = PyList_Size(result);

    for (Py_ssize_t i = 0; i < size; i++) {
        // PyList_GetItem returns a BORROWED reference (do not DECREF dict_item)
        PyObject *dict_item = PyList_GetItem(result, i);

        if (!PyDict_Check(dict_item)) {
            fprintf(stderr, "Item at index %zd is not a dictionary\n", i);
            continue;
        }

        // Extract items from the dictionary using string keys
        // PyDict_GetItemString returns a BORROWED reference
        PyObject *name_obj = PyDict_GetItemString(dict_item, "name");
        PyObject *lang_obj = PyDict_GetItemString(dict_item, "language");

        // Convert Python strings (PyUnicode) into C char* strings
        const char *name = NULL;
        const char *language = NULL;

        if (name_obj && PyUnicode_Check(name_obj)) {
            name = PyUnicode_AsUTF8(name_obj);
        }

        if (lang_obj && PyUnicode_Check(lang_obj)) {
            language = PyUnicode_AsUTF8(lang_obj);
        }

        // Output or save the values in C
        fprintf(stderr,"Index %zd -> Name: %s, Language: %s\n",
               i,
               name ? name : "UNKNOWN",
               language ? language : "UNKNOWN");
        // TODO: create a global copy that can be returned in the module_list_voices function in _server.
    }
    DEBUG_PRINT("C host: process_list_result() - Done\n");
}

/*
Call the list_voices method in our python source and get a list of returned voices.
*/
int call_list_voices_method(){
    DEBUG_PRINT("C host: call_list_voices_method()\n");
    PyObject *pResult = PyObject_CallMethod(pSpeechDispatchClassInstance, "list_voices", NULL);
    if (pResult != NULL) {
        process_list_result(pResult);
        Py_DECREF(pResult);
    }
    return 1;
}

/*
Call the speak method in our python source and get returned data.
*/
// int call_speak_method(const char* text, size_t bytes){
//     DEBUG_PRINT("C host: call_speak_method() - stub\n");
//     PyObject *pResult = PyObject_CallMethod(pSpeechDispatchClassInstance, "speak", "s#", text, bytes);
//     if (pResult != NULL) {
//         Py_DECREF(pResult);
//     }
//     return 1;
// }

int call_speak_method(const char* text, size_t bytes){
    DEBUG_PRINT("C host: call_speak_method()\n");
    DEBUG_PRINT("C host: text: %s\n", text);
    clock_t gen;
    clock_t end;

    gen = clock();
    //this should just return a generator and not do any actual processing.
    PyObject* pGenerator = PyObject_CallMethod(pSpeechDispatchClassInstance, "speak", "s#", text, bytes);
    end = clock();
    double seconds = (double)(end - gen) / CLOCKS_PER_SEC;
    DEBUG_PRINT("PyObject_CallMethod: Took %f seconds\n", seconds);

    if (pGenerator == NULL) {
        fprintf(stderr, "Python error inside speak() method:\n");
        PyErr_Print();
        return -1;
    }

    if (!PyGen_Check(pGenerator)){
        fprintf(stderr, "ERROR: python Speak() method must return a generator(use a yield)\n");
        return -1;
    }

    PyObject* result=NULL;

    gen = clock();
    // loop throught the returned generator.
    // Most of the python processing will happen during PyIter_Next
    while ((result = PyIter_Next(pGenerator))) {
        DEBUG_PRINT("C host: PyIter_Next()\n");
        // It should return a list.
        if (!PyList_Check(result)) {
            PyErr_SetString(PyExc_TypeError, "Expected method to return a list");
            Py_XDECREF(pGenerator);
            Py_XDECREF(result);
            return -1;
        }

        // Created memory for our buffer.
        Py_ssize_t size = PyList_Size(result);
        GArray *output_array = g_array_sized_new(FALSE, FALSE, sizeof(short), size);
        if (!output_array){
            PyErr_NoMemory();
            Py_DECREF(result);
            return -1;
        }

        // Process each element
        for (Py_ssize_t i = 0; i < size; i++) {
            // PyList_GetItem returns a BORROWED reference
            PyObject *item = PyList_GetItem(result, i);

            long val = PyLong_AsLong(item);
            if (val == -1 && PyErr_Occurred()) {
                DEBUG_PRINT("C host: PyErr_Occurred()\n");
                //free(c_array);
                g_array_unref(output_array);
                Py_XDECREF(result);
                Py_XDECREF(pGenerator);
                return -1;
            }

            // Bounds check for C short overflow (-32768 to 32767)
            if (val < SHRT_MIN || val > SHRT_MAX) {
                DEBUG_PRINT("C host: Bounds check()\n");
                PyErr_SetString(PyExc_OverflowError, "Value out of range for C short");
                //free(c_array);
                g_array_unref(output_array);
                Py_XDECREF(result);
                Py_XDECREF(pGenerator);
                return -1;
            }

            g_array_append_val(output_array, val);
        }

        end = clock();
        double aseconds = (double)(end - gen) / CLOCKS_PER_SEC;
        DEBUG_PRINT("PyIter_Next took %f seconds\n", aseconds);

        // print some of the output array.
        DEBUG_PRINT("C host: output array:\n");
        for (int i = 0; i < 10 && i < size; i++) {
            DEBUG_PRINT("%d ", g_array_index(output_array, short, i));
        }
        DEBUG_PRINT("\n");

        //push to the speak queue here
        if (output_array!=NULL){
            double seconds = (double)(end - gen) / CLOCKS_PER_SEC;
            DEBUG_PRINT("Generated %f seconds of audio in %f seconds\n", output_array->len/24000.0, seconds);

            //The output_array array will be freed by the _generation_thread or free_GeneratePayload on destroy.
            // TODO: make sure to handle the mark correctly. for now just setting it to ""
            DEBUG_PRINT("Sending output\n");
            send_wav(output_array, ""); // on the last chunk send the mark also.
            DEBUG_PRINT("Done Sending output\n");

            ahead_add(output_array->len/24000.0);
            ahead_print();
        }

        // if we get more then a 90 seconds ahead then block the thread,
        // no need to blow up the cpu on super long texts.
        float last_ahead = ahead_get();
        while (last_ahead >= 90.0){
            DEBUG_PRINT("ahead 90sec, blocking\n");
            sleep(20);// 20 seconds.
            last_ahead = ahead_get();

            if (stop_get())
                break;
        }

        // break the loop if we get a stop event.
        if (stop_get())
            break;

        Py_XDECREF(result); // Free each yielded item

        gen = clock();
    }


    // Cleanup and return
    Py_DECREF(pGenerator);
    return 0;
}

/*
Register the passed in speechd class to our pSpeechDispatchClassInstance.
This will be called from inside our python source code using something like.
speechd.start(SpeechDispatch())
*/
static PyObject* register_speechd_class(PyObject* self, PyObject* args) {
    PyObject* temp_obj;

    // Use "O" format specifier to extract a raw PyObject pointer
    if (!PyArg_ParseTuple(args, "O", &temp_obj)) {
        return NULL;
    }

    // Clean up any previously stored object reference
    Py_XDECREF(pSpeechDispatchClassInstance);

    // Store pointer and increment reference count to prevent garbage collection
    pSpeechDispatchClassInstance = temp_obj;
    Py_INCREF(pSpeechDispatchClassInstance);

    fprintf(stderr, "INFO: C Host Successfully stored reference to object at %p\n", (void*)pSpeechDispatchClassInstance);

    Py_RETURN_NONE;
}

static PyMethodDef SpeechdMethods[] = {
    {"start", register_speechd_class, METH_VARARGS, "Store a reference the speechd class"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef speechd_module = {
    PyModuleDef_HEAD_INIT, "speechd", NULL, -1, SpeechdMethods
};

// setup the module.
static PyObject* PyInit_speechd(void) {
    return PyModule_Create(&speechd_module);
}

/*
Runs the python source file.
Changes the working directory to mathc the source files directory.
*/
int run_python_module(const char *source_path_string){

    // Get the directory path of our source file.
    GString *source_dir = g_string_new_take(g_path_get_dirname(source_path_string));

    // Change the working directory to match the source dir.
    if (chdir(source_dir->str) != 0) {
        fprintf(stderr, "ERROR: Failed to chdir\n");
        g_string_free(source_dir, TRUE);
        return -1;
    }

    // Open the source file
    FILE *fp = fopen(source_path_string, "r");
    if (fp == NULL) {
        fprintf(stderr, "ERROR: Could not open file '%s'\n", source_path_string);
        g_string_free(source_dir, TRUE);
        return -1;
    }

    // Execute the Python file
    // 1 tells Python to close 'fp' automatically
    int result = PyRun_SimpleFileEx(fp, source_path_string, 1);

    if (result != 0) {
        fprintf(stderr, "ERROR: Failed to execute Python script '%s'\n", source_path_string);
        g_string_free(source_dir, TRUE);
        return -1;
    }

    g_string_free(source_dir, TRUE);
    return 0;
}

/*
Sets up our module for import.
Starts python inside a venv.
Changes sys.stdout = sys.stderr so we never print to stdout.
*/
int configure_python(const char* venv_path_string) {
    PyStatus status;
    PyConfig config;

    GString *venv_path = g_string_new(venv_path_string);
    GString *venv_python_path = find_python_executable(venv_path);

    if (venv_python_path != NULL) {
        g_printerr("INFO: using the path '%s' for venv\n", venv_python_path->str);
    }

    if (venv_python_path == NULL || venv_python_path->len == 0) {
        g_printerr("ERROR: Failed to find venv for path.\n");
        if (venv_path) g_string_free(venv_path, TRUE);
        if (venv_python_path) g_string_free(venv_python_path, TRUE);
        return -1;
    }

    // Add our module in.
    PyImport_AppendInittab("speechd", PyInit_speechd);

    // Initialize configuration structure
    PyConfig_InitPythonConfig(&config);

    // config path must be a wchar_t, so convert it.
    wchar_t *wpath = Py_DecodeLocale(venv_python_path->str, NULL);
    if (wpath == NULL) {
        g_printerr("ERROR: Failed to decode path string.\n");
        g_string_free(venv_path, TRUE);
        g_string_free(venv_python_path, TRUE);
        PyConfig_Clear(&config);
        return -1;
    }

    // Set the Python configuration string
    status = PyConfig_SetString(&config, &config.executable, wpath);

    // wpath is no longer needed after PyConfig_SetString copies it
    PyMem_RawFree(wpath);
    wpath = NULL;

    // Check the status
    if (PyStatus_Exception(status)) {
        g_printerr("ERROR: Failed to set python executable path.\n");
        g_string_free(venv_path, TRUE);
        g_string_free(venv_python_path, TRUE);
        PyConfig_Clear(&config);
        return -1;
    }

    // Initialize the Python interpreter with our custom configuration
    status = Py_InitializeFromConfig(&config);
    if (PyStatus_Exception(status)) {
        g_printerr("ERRROR: Failed to initialize python from config.\n");
        g_string_free(venv_path, TRUE);
        g_string_free(venv_python_path, TRUE);
        PyConfig_Clear(&config);
        return -1;
    }

    // Free config resources immediately after initialization
    PyConfig_Clear(&config);

    // Make sure python will only be printing to STDERR
    PyRun_SimpleString(
        "import sys\n"
        "sys.stdout = sys.stderr\n"
    );

    // Clean up
    g_string_free(venv_path, TRUE);
    g_string_free(venv_python_path, TRUE);
    return 0;
}

int setup_python_threads(){
    // Save current thread state and release GIL from main thread
    main_tstate = PyEval_SaveThread();

    // Create a new PyThreadState for this thread using the main interpreter state
    server_tstate = PyThreadState_New(main_tstate->interp);

    // Create a new PyThreadState for worker thread using the main interpreter state
    worker_tstate = PyThreadState_New(main_tstate->interp);

    return 1;
}

int setup_python(){

    if (configure_python(venv_path->str) == -1){
        g_printerr("ERROR: Failed to configure python\n");
        return -1;
    }

    if (run_python_module(source_path->str) == -1){
        Py_XDECREF(pSpeechDispatchClassInstance);
        pSpeechDispatchClassInstance = NULL;
        Py_Finalize();
        return -1;
    }

    setup_python_threads();

    return 0;
}

int cleanup_python(){
    // Clean up and finalize
    PyEval_RestoreThread(main_tstate);
    PyThreadState_Clear(server_tstate);
    PyThreadState_Delete(server_tstate);
    PyThreadState_Clear(worker_tstate);
    PyThreadState_Delete(worker_tstate);
    Py_XDECREF(pSpeechDispatchClassInstance);
    pSpeechDispatchClassInstance = NULL;
    Py_Finalize();
}