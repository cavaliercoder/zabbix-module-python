#include "libzbxpython.h"

char *
python_str(PyObject *pyValue)
{
    char *buf = NULL;
    PyObject *pyStr = NULL;

    if(NULL != (pyStr = PyObject_Str(pyValue))) {
        buf = strdup(PyUnicode_AsUTF8(pyStr));
        Py_DECREF(pyStr);
    }

    return buf;
}

int
python_log_error(AGENT_RESULT *result)
{
    if (PyErr_Occurred()) {
        PyObject *pyType, *pyValue, *pyTraceback;
        char *str;
        
        PyErr_Fetch(&pyType, &pyValue, &pyTraceback);

        // log error description
        if (NULL != (str = python_str(pyValue ? pyValue : pyType))) {
            zabbix_log(LOG_LEVEL_ERR, str);

            // set error message on result struct
            if (NULL != result)
                SET_MSG_RESULT(result, str);
            else
                free(str);
        }

        if (pyType)
            Py_DECREF(pyType);
        
        if (pyValue)
            Py_DECREF(pyValue);
        
        if (pyTraceback)
            Py_DECREF(pyTraceback);

        return 1;
    }

    return 0;
}

PyObject *
python_import_module(const char *module)
{
    PyObject *pyName = NULL;
    PyObject *pyModule = NULL;

    pyName = PyUnicode_FromString(module);
    pyModule = PyImport_Import(pyName);
    Py_DECREF(pyName);

    if (NULL == pyModule) {
        zabbix_log(LOG_LEVEL_ERR, "cannot import module %s", module);
        python_log_error(NULL);
    }

    return pyModule;
}

int
python_module_init(PyObject *pyModule)
{
    int ret = ZBX_MODULE_FAIL;
    PyObject *pyFunc;

    const char *moduleName = PyModule_GetName(pyModule);    

    // check for zbx_module_item_list function in module
    if(NULL == (pyFunc = PyObject_GetAttrString(pyModule, "zbx_module_init")) || 0 == PyFunction_Check(pyFunc)) {
        zabbix_log(LOG_LEVEL_DEBUG, "function not found: %s.zbx_module_init", moduleName);
    } else {
        // call function
        if(NULL == (PyObject_CallObject(pyFunc, NULL))) {
            zabbix_log(LOG_LEVEL_ERR, "error calling %s.zbx_module_init", moduleName);
            python_log_error(NULL);
        } else {
            ret = ZBX_MODULE_OK;
        }

        Py_DECREF(pyFunc);
    }

    return ret;
}

/******************************************************************************
 *                                                                            *
 * Function: python_unmarshal_item                                            *
 *                                                                            *
 * Purpose: marshall a Python zabbix_module.AgentItem to a Zabbix ZBX_METRIC  *
 *          C struct for use in zbx_module_item_list                          *
 *                                                                            *
 * Return value: pointer to a ZBX_METRIC struct                               *
 *                                                                            *
 ******************************************************************************/
static ZBX_METRIC*
python_unmarshal_item(PyObject *pyItem, ZBX_METRIC *item)
{
    PyObject *pyValue;

    if (NULL == item)
        item = (ZBX_METRIC*) calloc(1, sizeof(ZBX_METRIC));

    // unmarshall key
    if(pyValue = PyObject_GetAttrString(pyItem, "key")) {
        item->key = PyUnicode_AsUTF8(pyValue);
        Py_DECREF(pyValue);
    }

    // unmarshall flags
    if(pyValue = PyObject_GetAttrString(pyItem, "flags")) {
        item->flags = (int) PyLong_AsLong(pyValue) | CF_HAVEPARAMS;
        Py_DECREF(pyValue);
    }

    // unmarshall test parameter
    if(pyValue = PyObject_GetAttrString(pyItem, "test_param")) {
        item->test_param = PyUnicode_AsUTF8(pyValue);
        Py_DECREF(pyValue);
    }

    // always callback to the router function
    item->function = PYTHON_ROUTER;

    return item;
}

/******************************************************************************
 *                                                                            *
 * Function: python_module_item_list                                          *
 *                                                                            *
 * Purpose: call zbx_module_item_list in the given Python module and return   *
 *          an array of unmarshaled ZBX_METRIC items                          *
 *                                                                            *
 * Return value: pointer to a ZBX_METRIC struct array                         *
 *                                                                            *
 ******************************************************************************/
ZBX_METRIC *
python_module_item_list(PyObject *pyModule)
{
    ZBX_METRIC *keys = NULL, *item;
    PyObject *pyFunc, *pyKeys, *pyIter, *pyItem;
    Py_ssize_t keys_len;

    const char *moduleName = PyModule_GetName(pyModule);

    // check for zbx_module_item_list function in module
    if(NULL == (pyFunc = PyObject_GetAttrString(pyModule, "zbx_module_item_list")) || 0 == PyFunction_Check(pyFunc)) {
        zabbix_log(LOG_LEVEL_INFORMATION, "function not found: %s.zbx_module_item_list", moduleName);
    } else {
        // call function
        if(NULL == (pyKeys = PyObject_CallObject(pyFunc, NULL)) || !PyList_Check(pyKeys)) {
            python_log_error(NULL);
            zabbix_log(LOG_LEVEL_ERR, "error calling %s.zbx_module_item_list", moduleName);
        } else {
            if(NULL == (pyIter = PyObject_GetIter(pyKeys))) {
                python_log_error(NULL);
                zabbix_log(LOG_LEVEL_ERR, "error iterating key list returned by %s.zbx_module_item_list", moduleName);
            } else {
                // alloc item list
                keys_len = PyObject_Length(pyKeys);
                keys = calloc(keys_len+1, sizeof(ZBX_METRIC));
                item = keys;

                // marshall items
                while (pyItem = PyIter_Next(pyIter)) {
                    python_unmarshal_item(pyItem, item);                    
                    Py_DECREF(pyItem);
                    item++;
                }

                Py_DECREF(pyIter);
            }
        }

        if (pyKeys)
            Py_DECREF(pyKeys);
    }

    return keys;
}
