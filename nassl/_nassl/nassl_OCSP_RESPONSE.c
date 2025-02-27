#define PY_SSIZE_T_CLEAN
#include <Python.h>

// Fix symbol clashing on Windows
// https://bugs.launchpad.net/pyopenssl/+bug/570101
#ifdef _WIN32
#include "winsock.h"
#endif

#include <openssl/x509.h>
#include <openssl/ocsp.h>

#include "python_utils.h"
#include "nassl_errors.h"
#include "nassl_OCSP_RESPONSE.h"


static PyObject* nassl_OCSP_RESPONSE_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    PyErr_SetString(PyExc_NotImplementedError, "Cannot directly create an OCSP_RESPONSE object. Get it from SSL.get_tlsext_status_ocsp_resp()");
    return NULL;
}


static void nassl_OCSP_RESPONSE_dealloc(nassl_OCSP_RESPONSE_Object *self)
{
 	if (self->ocspResp != NULL)
 	{
  		OCSP_RESPONSE_free(self->ocspResp);
  		self->ocspResp = NULL;
  	}
    if (self->peerCertChain != NULL)
    {
        /*int i = 0;
        int certNum = sk_X509_num(self->peerCertChain);
        for(i=0;i<certNum;i++) {
            sk_X509_pop_free(self->peerCertChain, &X509_free);
        } */
        sk_X509_free(self->peerCertChain);
        self->peerCertChain = NULL;
    }
    Py_TYPE(self)->tp_free((PyObject*)self);
}


static PyObject* nassl_OCSP_RESPONSE_as_text(nassl_OCSP_RESPONSE_Object *self)
{
    PyObject* ocsResp_PyString = NULL;
    BIO *memBio = NULL;
    unsigned int txtLen = 0;
    char *txtBuffer = NULL;

    // Print the OCSP response to a memory BIO
    memBio = BIO_new(BIO_s_mem());
    if (memBio == NULL)
    {
        return raise_OpenSSL_error();
    }

    OCSP_RESPONSE_print(memBio, self->ocspResp, 0);

    // Extract the text from the BIO
    txtLen = BIO_pending(memBio);
    txtBuffer = (char *) PyMem_Malloc(txtLen);
    if (txtBuffer == NULL)
    {
        BIO_vfree(memBio);
        return PyErr_NoMemory();
    }

    BIO_read(memBio, txtBuffer, txtLen);
    // An OCSP response may contain non-utf8 characters (if there are certificates in it) so we return it as bytes
    // To handle decoding errors in Python
    ocsResp_PyString = PyBytes_FromStringAndSize(txtBuffer, txtLen);
    PyMem_Free(txtBuffer);
    BIO_vfree(memBio);

    return ocsResp_PyString;
}


static PyObject* nassl_OCSP_RESPONSE_as_der_bytes(nassl_OCSP_RESPONSE_Object *self)
{
    PyObject *res = NULL;
    char *ocspBuf = NULL;
    unsigned int ocspRespLen = 0;

    ocspRespLen = i2d_OCSP_RESPONSE(self->ocspResp, (unsigned char **) &ocspBuf);
    if (ocspRespLen < 0)
    {
        PyErr_SetString(PyExc_ValueError, "Could not convert OCSP response do DER bytes");
        return NULL;
    }
    res = PyBytes_FromStringAndSize(ocspBuf, ocspRespLen);
    OPENSSL_free(ocspBuf);
    return res;
}


static PyObject* nassl_OCSP_RESPONSE_basic_verify(nassl_OCSP_RESPONSE_Object *self, PyObject *args)
{
    X509_STORE *trustedCAs = NULL;
    int certNum = 0, verifyRes = 0, i = 0, respStatus = 0;
    OCSP_BASICRESP *basicResp = NULL;
    char *caFilePath = NULL;
    if (PyArg_ParseFilePath(args, &caFilePath) == NULL)
    {
        return NULL;
    }

    // Ensure the response that can be verified
    respStatus = OCSP_response_status(self->ocspResp);
    if (respStatus != OCSP_RESPONSE_STATUS_SUCCESSFUL)
    {
        PyErr_SetString(PyExc_ValueError, "Cannot verify an OCSP response with a non-successful status");
        return NULL;
    }

    // Load the file containing the trusted CA certs
    trustedCAs = X509_STORE_new();
    if (trustedCAs == NULL)
    {
        return raise_OpenSSL_error();
    }

    X509_STORE_load_locations(trustedCAs, caFilePath, NULL);

    // Verify the OCSP response
    basicResp = OCSP_response_get1_basic(self->ocspResp);

    // Add the server's certificate chain to the OCSP response. Is this correct ?
    // Maybe ? http://www.mail-archive.com/openssl-users@openssl.org/msg70201.html
    certNum = sk_X509_num(self->peerCertChain);
    for(i=0; i<certNum; i++)
    {
        X509 *cert = sk_X509_value(self->peerCertChain, i);
        OCSP_basic_add1_cert(basicResp, cert);
    }

    verifyRes = OCSP_basic_verify(basicResp, NULL, trustedCAs, 0);
    OCSP_BASICRESP_free(basicResp);
    X509_STORE_free(trustedCAs);
    if (verifyRes <= 0)
    {
        return raise_OpenSSL_error();
    }
    Py_RETURN_NONE;
}


static PyMethodDef nassl_OCSP_RESPONSE_Object_methods[] =
{
    {"as_text", (PyCFunction)nassl_OCSP_RESPONSE_as_text, METH_NOARGS,
     "OpenSSL's OCSP_RESPONSE_print()."
    },
    {"as_der_bytes", (PyCFunction)nassl_OCSP_RESPONSE_as_der_bytes, METH_NOARGS,
     "OpenSSL's i2d_OCSP_RESPONSE()."
    },
    {"basic_verify", (PyCFunction)nassl_OCSP_RESPONSE_basic_verify, METH_VARARGS,
     "OpenSSL's OCSP_basic_verify()."
    },
    {NULL}  // Sentinel
};


PyTypeObject nassl_OCSP_RESPONSE_Type =
{
    PyVarObject_HEAD_INIT(NULL, 0)
    "_nassl.OCSP_RESPONSE",             /*tp_name*/
    sizeof(nassl_OCSP_RESPONSE_Object),             /*tp_basicsize*/
    0,                         /*tp_itemsize*/
    (destructor)nassl_OCSP_RESPONSE_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
    "OCSP_RESPONSE objects",           /* tp_doc */
    0,                     /* tp_traverse */
    0,                     /* tp_clear */
    0,                     /* tp_richcompare */
    0,                     /* tp_weaklistoffset */
    0,                     /* tp_iter */
    0,                     /* tp_iternext */
    nassl_OCSP_RESPONSE_Object_methods,             /* tp_methods */
    0,             /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    0,      /* tp_init */
    0,                         /* tp_alloc */
    nassl_OCSP_RESPONSE_new,                 /* tp_new */
};


void module_add_OCSP_RESPONSE(PyObject* m)
{
	nassl_OCSP_RESPONSE_Type.tp_new = nassl_OCSP_RESPONSE_new;
	if (PyType_Ready(&nassl_OCSP_RESPONSE_Type) < 0)
	{
    	return;
	}

    Py_INCREF(&nassl_OCSP_RESPONSE_Type);
    PyModule_AddObject(m, "OCSP_RESPONSE", (PyObject *)&nassl_OCSP_RESPONSE_Type);
}
